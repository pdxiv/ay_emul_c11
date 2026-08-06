/* C11 port of ay_emul/Players.pas's real Sergey Bulba/FUXOFT `.ay` file
 * format support (FT.AY, TypeID "EMUL" only - "AMAD"/"ST11" are different,
 * unrelated container formats sharing the same "ZXAY" outer magic and are
 * rejected here, see migration_debt.yaml).
 *
 * Ports:
 *  - The header/song-table lookup (Players.pas:2915-2949, itself mirroring
 *    OpenAYFile's relative-pointer walk, Players.pas:7132-7236) - all
 *    multi-byte header fields are big-endian, relative pointers are
 *    relative to their OWN field's file position (read the pointer, add
 *    its value to the position of the field just read).
 *  - The ZRAM/register setup (Players.pas:3926-4008): fills the RET/$FF/0
 *    memory regions, injects the DumpIM1/DumpIM2 player-stub trampoline
 *    patched with the song's Init/Inter addresses, sets SP/PC and the
 *    Z80's register file from TSongData.HiReg/LoReg, then walks the
 *    (dest_addr,length,rel_offset) triples doing direct byte copies from
 *    the file into Z80 RAM (no decompression - real .ay files store their
 *    Z80 code/data uncompressed).
 *  - The MakeBufferAY/AY_Get_Registers playback loop (Players.pas:
 *    13989-14047): steps the already-ported Z80/AY core
 *    (engine/z80_bus.h + engine/ay.h) frame-by-frame, flushing the AY
 *    mixer once per Z80 frame (MaxTStates rollover) via ay_synthesizer_ay,
 *    with the same deferred-write-across-buffer-boundary handling
 *    (int_flag/int_beeper/int_ay) already implemented in engine/ay.c's
 *    mixer functions.
 *  - Author/title/comment string extraction (OpenAYFile's AuthorString/
 *    MiscString/SongName relative-pointer walk, Players.pas:7154-7184) -
 *    added for the Phase 5 GUI (song metadata display), see f->author/
 *    f->title/f->comment below. Stored as raw, untranscoded bytes -
 *    CP1251->UTF-8 conversion for display is a GUI-layer concern (see
 *    gui/src/playback.c), not done here.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - AMAD (FT.FXM) / ST11 (FT.ST1) TypeIDs - different formats, out of
 *    scope for this milestone (only the real files in songs/ are in
 *    scope, and Discmac20_0.ay is TypeID "EMUL").
 *  - FadeLength-based fade-out at song end - a cosmetic playback detail,
 *    not needed for correct core audio (loop/end-of-song still works via
 *    SongLength alone, matching PlayList.pas:722's Global_Tick_Max source).
 *  - The AYFileEnableAutoSwitch-triggered chip-frequency GUI recalculation
 *    (MainWin.pas's Set_Chip_Frq's filter/label updates) - only the
 *    Delay_In_Tiks/FrqAyByFrqZ80 arithmetic core of Set_Chip_Frq is
 *    reproduced (ay_file_set_chip_freq), via z80_bus's on_chip_freq_change
 *    callback, matching what's actually needed for correct playback speed
 *    when the CPC-protocol auto-detect fires mid-song.
 */
#ifndef AY_ENGINE_AY_FILE_H
#define AY_ENGINE_AY_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay.h"
#include "ay_engine/z80_bus.h"

typedef enum {
  AY_FILE_OK = 0,
  AY_FILE_ERR_BAD_HEADER,      /* not a "ZXAY" file */
  AY_FILE_ERR_UNSUPPORTED_TYPE, /* TypeID isn't "EMUL" (AMAD/ST11/other) */
  AY_FILE_ERR_BAD_SONG_INDEX,
  AY_FILE_ERR_TRUNCATED        /* header/song-table/block data ran past EOF */
} ay_file_status;

typedef struct ay_file {
  z80_bus bus;
  ay_engine ay;

  int64_t global_tick_counter; /* Players.pas: PlConsts[0].Global_Tick_Counter */
  int64_t global_tick_max;     /* Players.pas: PlConsts[0].Global_Tick_Max,
                                 * from PlayList.pas:722 = the song's
                                 * SongLength field (ticks/frames) */
  bool do_loop;                /* settings.pas: Do_Loop (default false) */
  bool real_end_all;           /* Players.pas: Real_End_All */

  int frq_z80;      /* MainWin.pas: FrqZ80 (Hz) */
  int sample_rate;  /* settings.pas: SampleRate (Hz) - kept for
                      * ay_file_set_chip_freq's Delay_In_Tiks recompute
                      * when z80_bus's on_chip_freq_change callback fires
                      * mid-song (CPC-protocol auto-detect). */

  int song_count;   /* data[16]+1 (TAYFileHeader.NumOfSongs, 0-based max
                      * valid song_index) - a GUI-facing addition (Phase 5
                      * kickoff), not present in the original Pascal as a
                      * distinct getter; real OpenAYFile just iterates
                      * 0..NumOfSongs when building playlist entries
                      * (Players.pas), which is exactly what this exposes
                      * for a caller that wants to offer song selection. */

  /* Raw (untranscoded CP1251) author/per-song-title/comment strings -
   * empty if the file has none (e.g. no PAuthor pointer, or it points
   * past EOF - matches OpenAYFile's own "just get an empty string"
   * behavior on a malformed/absent pointer, no special error path). */
  char author[256];
  char title[256];
  char comment[256];
} ay_file;

/* Parses `data`/`size` (the whole .ay file's bytes) and sets up f->bus /
 * f->ay for playback of song `song_index` (0-based; pass 0 for the common
 * single-song case). f->bus/f->ay are fully (re)initialized by this call.
 * ay_freq/frq_z80 select the initial AY and Z80 clocks (Hz) - pass
 * AY_FILE_AY_FREQ_DEF/AY_FILE_FRQ_Z80_DEF for settings.pas's own defaults
 * (AY_FreqDef/FrqZ80Def) unless the caller has a reason to override them. */
ay_file_status ay_file_load(ay_file* f, const uint8_t* data, size_t size,
                             int song_index, int ay_freq, int frq_z80,
                             int sample_rate);

#define AY_FILE_AY_FREQ_DEF 1773400   /* settings.pas: AY_FreqDef */
#define AY_FILE_FRQ_Z80_DEF 3494400   /* settings.pas: FrqZ80Def */
#define AY_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */
#define AY_FILE_MAX_TSTATES_DEF 69888 /* settings.pas: MaxTStatesDef */

/* Players.pas: MakeBufferAY (13989-14029) - runs the Z80 core until
 * `buffer_length` stereo16 sample frames have been written to `buf` or the
 * song ends (f->real_end_all becomes true, matching Real_End_All). Returns
 * the number of sample frames actually written (may be less than
 * buffer_length only on end-of-song with do_loop false). Only the
 * stereo16 output path is wired here (settings.pas's own defaults,
 * NumberOfChannels=2/SampleBit=16); engine/ay.c's mono16/stereo8/mono8
 * mixer paths are already-ported and can be driven the same way by a
 * caller that needs them - not a functional gap, just an unexercised
 * combination in this milestone's loader glue. */
int ay_file_make_buffer(ay_file* f, int16_t* buf, int buffer_length);

#endif /* AY_ENGINE_AY_FILE_H */
