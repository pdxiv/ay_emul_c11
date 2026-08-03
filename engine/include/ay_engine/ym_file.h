/* C11 port of ay_emul/Players.pas's YM5/YM6 register-dump format support
 * (FT.YM5/FT.YM6 - the LHA-wrapped "YM Archive" style files, confirmed via
 * songs/cpc/"The Last V8.ym"), built on engine/lh5.h for decompression and
 * the existing engine/ay.h mixer (pure AY.pas-style data interpretation -
 * no Z80 execution, matches PSG/VTX/YM3 in shape, see ym_file.c's file
 * comment for how it differs from those simpler formats).
 *
 * Ports:
 *  - LHA container detection + TLZHFileHeader parse (Players.pas:
 *    7708-7736) and the YM5/YM6 header parse incl. digidrum descriptor
 *    table + Title/Author/Comment string scan to locate the register-data
 *    offset (Players.pas:7745-7814, TYM5FileHeader, Players.pas:2812-2870
 *    for the corresponding per-play setup).
 *  - The register-plane reader for the "extended" (bit0 of Song_Attr's
 *    last byte set) YM5 variant, YM5i_Get_Registers (Players.pas:
 *    13653-13819) - confirmed via this project's own real test file to be
 *    the variant actually needed; the non-extended YM5_Get_Registers and
 *    the YM6/YM6i variants are structurally similar but NOT ported this
 *    milestone (see migration_debt.yaml).
 *  - The Atari-MFP-style sub-timer "extra effects" engine,
 *    YM6_Extra_GetRegisters (Players.pas:12915-13004) - shared by all
 *    YM5/YM6 register-reader variants, drives up to two simultaneous
 *    hardware-envelope/digidrum/sinus effect channels (SE1/SE2) timed
 *    against a virtual Atari MFP-alike divisor clock.
 *  - SynthesizerYM6 (AY.pas:1117-1127) - trivial tick-accumulator dispatch
 *    into the same Synthesizer_Stereo16/mixer already ported (engine/ay.h).
 *  - The Set_Chip_Frq/Set_Player_Frq/Set_MFP_Frq arithmetic cores needed
 *    to derive AY_Freq/Interrupt_Freq/MFPTimerFrq/Delay_In_Tiks/
 *    YM6TiksOnInt from the file's own ChipFrq/InterFrq header fields
 *    (MainWin.pas:1544-1548,1570,2070-2072) - same bounded-arithmetic-only
 *    approach as engine/ay_file.c's ay_file_set_chip_freq.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - YM5_Get_Registers / YM6_Get_Registers / YM6i_Get_Registers (the
 *    non-extended-YM5 and both YM6 variants) - our real test file is
 *    extended-YM5; these are structurally similar but unexercised.
 *  - Digidrum sample format conversion (YMizeSample / the linear-PCM-to-
 *    YM-amplitude-table lookup, Players.pas:2833-2852) - our real test
 *    file has Num_of_Dig=0, making this dead code for it; the descriptor
 *    table itself IS parsed (ym_file_digidrum) so a future file with real
 *    digidrums fails loudly (index out of range) rather than silently
 *    misplaying, but the samples' bytes are used as-is, unconverted.
 *  - AtariSE1Type/AtariSE2Type values other than what YM5i's own code
 *    path drives (SE1 type 0 "square/hardware-envelope" always; SE2 type
 *    1 "digidrum" is structurally reachable but provably inert for any
 *    file with zero digidrum samples, per Players.pas:13772's
 *    `AtariParam2 >= Length(DDrumSamples)` guard - confirmed for our file).
 *  - YM3/YM3b/YM2/VTX (the simpler, non-LHA, non-extended-header sibling
 *    formats) - a different code path entirely (VTX_YM3_YM3b_Get_Registers
 *    / MakeBufferVTX), out of scope here (VTX gets its own milestone).
 */
#ifndef AY_ENGINE_YM_FILE_H
#define AY_ENGINE_YM_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay.h"

typedef enum {
  YM_FILE_OK = 0,
  YM_FILE_ERR_BAD_HEADER,
  YM_FILE_ERR_UNSUPPORTED_TYPE, /* not YM5!/YM6! (extended variant) */
  YM_FILE_ERR_LZH_INVALID,
  YM_FILE_ERR_TRUNCATED,
} ym_file_status;

typedef struct ym_digidrum {
  int32_t offset; /* byte offset into ym_file->data */
  int32_t length;
} ym_digidrum;

typedef struct ym_file {
  ay_engine ay;

  uint8_t* data; /* owned, decompressed YM5!/YM6! buffer (header + register
                  * planes), freed by ym_file_free */
  int32_t data_size;

  int32_t vtx_offset;     /* Players.pas: VTX_Offset, register-data start */
  int32_t number_of_vbls; /* Players.pas: NumberOfVBLs */
  int32_t loop_vbl;       /* Players.pas: LoopVBL */
  int32_t position_in_vtx;

  bool is_ym6; /* magic was YM6! (vs YM5!) - only affects which
                * Get_Registers variant would be selected; this milestone
                * only implements the YM5-extended reader (see file
                * comment), so ym_file_load rejects YM6! for now. */

  ym_digidrum* digidrums;
  int digidrum_count;

  /* YM6_Extra_GetRegisters state - Players.pas:4009-4025's PrepareToPlay
   * init block, and the globals it and YM5i_Get_Registers/
   * YM6_Extra_GetRegisters share. */
  int atari_se1_channel, atari_se2_channel;
  int atari_se1_type, atari_se2_type;
  int atari_se1_tp, atari_se2_tp; /* AtariSE1TP/AtariSE2TP */
  double atari_timer_period1, atari_timer_period2;
  double atari_timer_counter1, atari_timer_counter2;
  int atari_param1, atari_param2;
  int atari_v1, atari_v2;
  int atari_se1_pos, atari_se2_pos;
  int ym6_sinus_pos1, ym6_sinus_pos2;

  double ay_freq;         /* settings.pas: AY_Freq, from the header's ChipFrq */
  double interrupt_freq;  /* settings.pas: Interrupt_Freq, from InterFrq*1000 */
  double mfp_timer_frq;   /* MainWin.pas: MFPTimerFrq */
  double ym6_tiks_on_int; /* Players.pas: YM6TiksOnInt */
  double ym6_cur_tik;     /* Players.pas: YM6CurTik */

  int64_t global_tick_counter;
  int64_t global_tick_max;
  bool do_loop;
  bool real_end_all;
} ym_file;

/* Parses `data`/`size` (the whole .ym file's bytes, LHA-compressed or not)
 * and sets up f->ay for playback. sample_rate selects the output PCM rate
 * (Delay_In_Tiks derivation) - pass YM_FILE_SAMPLE_RATE_DEF for
 * settings.pas's own default unless the caller has a reason to override
 * it. `f` takes ownership of a decompressed copy of the file; call
 * ym_file_free when done. */
ym_file_status ym_file_load(ym_file* f, const uint8_t* data, size_t size,
                             int sample_rate);

void ym_file_free(ym_file* f);

#define YM_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferYM5 (13006-13062), the extended-Song_Attr branch
 * only (YM5i_Get_Registers + YM6_Extra_GetRegisters + SynthesizerYM6 per
 * tick) - see ym_file.h's file comment for what's not ported. Same
 * contract as ay_file_make_buffer: runs until `buffer_length` stereo16
 * sample frames are written or the song ends (f->real_end_all). */
int ym_file_make_buffer(ym_file* f, int16_t* buf, int buffer_length);

#endif /* AY_ENGINE_YM_FILE_H */
