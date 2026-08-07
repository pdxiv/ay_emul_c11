/* C11 port of ay_emul/Players.pas's Pro Tracker 3 (PT3) module format
 * support (FT.PT3) - a genuine event/tracker engine (pattern/sample/
 * ornament interpretation with note-pitch tables and effect commands),
 * not a register-dump format like AY/YM/PSG. Built on the existing
 * engine/ay.h mixer (PT3_Get_Registers only ever calls
 * ay_chip_set_ampl_a/b/c / ay_chip_set_mixer_register /
 * ay_chip_set_envelope_register / raw register writes - no Z80
 * execution, matches the shape of every other non-Z80 format ported so
 * far).
 *
 * Scope: standalone .pt3 files loaded directly (not embedded inside
 * another container format) - confirmed via LoadTrackerModule
 * (Players.pas:2265-2312) that the pointer-rebase-by-MAddr logic only
 * runs for embedded/non-.pt3-extension modules, which is dead code for
 * both real test files (songs/pt3/ARTe_ST1.pt3,
 * songs/turbo_sound/Gasman_-_dynamite.pt3 - both have a real .pt3
 * extension and are loaded standalone) - so this port skips that rebase
 * entirely and reads the file's own absolute-from-buffer-start pointers
 * as-is.
 *
 * Ports:
 *  - The file-into-RAM load (Players.pas:2248-2264 - just a raw byte copy
 *    into a flat 65536-byte buffer, zero-padded).
 *  - PT3_Get_Registers in full (Players.pas:12302-12767): GetNoteFreq
 *    (pitch-table select - only PT3NoteTable_ST is ported, see below),
 *    PatternInterpreter (the per-channel opcode-stream walker: note-on,
 *    sample/ornament select, envelope, volume, and the 9 numbered effect
 *    commands), ChangeRegisters (per-tick tone/amplitude/noise/envelope
 *    derivation), and the main per-frame body (delay gate,
 *    position/pattern advance, final AY register writes).
 *  - CheckLoopAndStop (Players.pas:8732-8746) - simplified: this
 *    milestone doesn't port GetTimePT3 (the pattern-walking simulation
 *    that computes a song's real duration/loop point up front, needed
 *    only for UI seek/scrub and duration display - Players.pas:15315-
 *    15645), so there's no real Global_Tick_Max to check against; see
 *    pt3_file_make_buffer's contract below.
 *  - The InitTrackerModule FT.PT3 branch (Players.pas:3717-3806) and the
 *    CaseTrModules FT.PT3 branch's Version derivation (Players.pas:
 *    2577-2582).
 *  - GetNoteFreq's full 4-way PT3_TonTableId dispatch (Players.pas:
 *    12304-12322), i.e. all 6 PT3NoteTable_* variants (PT_33_34r/PT_34_35/
 *    ST/ASM_34r/ASM_34_35/REAL_34r/REAL_34_35, each version-gated except
 *    ST) - MIG-0040 closes the "only TonTableId=1 supported" gap MIG-0020
 *    originally left open.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - Turbosound/TSMode (dual-chip PT3, marker-byte-triggered) - confirmed
 *    in an earlier session not to be exercised by either real test file
 *    (ARTe_ST1.pt3 is version 4, below the >=7 gate; Gasman_-_dynamite.pt3
 *    is version 7 but its marker byte is blank) - MIG-0007.
 *  - GetTimePT3 (duration/loop-point precompute) - MIG-0101: now ported
 *    (pt3_get_time in pt3_file.c), transcribed 1:1 from Players.pas:
 *    15333-15662. The earlier framing here ("UI-only, not needed for
 *    correct audio playback") was itself incomplete - Global_Tick_Max
 *    (what GetTimePT3 computes) is what CheckLoopAndStop uses to decide
 *    when a PT3 song naturally ends with Do_Loop off (Players.pas:
 *    8732-8746), so this was a real playback-correctness gap, not just
 *    a UI one; see migration_debt.yaml MIG-0101 for the full trace.
 *    TSMode (turbosound) is still not simulated here either, matching
 *    the port's existing single-chip scope above - GetTimePT3's own
 *    `vars[1]`/second-chip half is skipped, equivalent to the
 *    original's `TS = $20` case.
 *  - Embedded/rebased-pointer PT3 loading (see Scope above) - MAddr is
 *    always 0 for standalone files, so the rebase branch never runs for
 *    either real test file.
 */
#ifndef AY_ENGINE_PT3_FILE_H
#define AY_ENGINE_PT3_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay.h"

typedef enum {
  PT3_FILE_OK = 0,
  PT3_FILE_ERR_BAD_HEADER,
  PT3_FILE_ERR_TRUNCATED,
} pt3_file_status;

typedef struct pt3_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t ton;
  uint8_t loop_ornament_position;
  uint8_t ornament_length;
  uint8_t position_in_ornament;
  uint8_t loop_sample_position;
  uint8_t sample_length;
  uint8_t position_in_sample;
  uint8_t volume;
  uint8_t number_of_notes_to_skip;
  uint8_t note;
  uint8_t slide_to_note;
  uint8_t amplitude;
  bool envelope_enabled;
  bool enabled;
  bool simple_gliss;
  int16_t current_amplitude_sliding;
  int16_t ton_slide_count;
  int16_t current_onoff;
  int16_t onoff_delay;
  int16_t offon_delay;
  int16_t ton_slide_delay;
  int16_t current_ton_sliding;
  int16_t ton_accumulator;
  int16_t ton_slide_step;
  int16_t ton_delta;
  int8_t note_skip_counter;
  uint8_t current_noise_sliding;
  uint8_t current_envelope_sliding;
} pt3_channel;

typedef struct pt3_file {
  ay_engine ay;

  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */

  int16_t env_base;      /* PT3_Parameters: Env_Base.wrd */
  int16_t cur_env_slide;
  int16_t env_slide_add;
  int8_t cur_env_delay;
  int8_t env_delay;
  uint8_t noise_base;
  uint8_t delay;
  uint8_t add_to_noise;
  uint8_t delay_counter;
  uint8_t current_position;

  pt3_channel chan_a, chan_b, chan_c;

  int version;               /* PlConsts[n].Version */
  uint8_t ton_table_id;      /* PT3_TonTableId - selects among 6 note
                              * tables via GetNoteFreq, see pt3_file.c's
                              * get_note_freq() */
  uint16_t patterns_pointer; /* PT3_PatternsPointer */
  uint16_t samples_pointers[32];
  uint16_t ornaments_pointers[16];
  uint8_t number_of_positions;
  uint8_t loop_position;
  uint32_t position_list_offset; /* byte offset of PT3_PositionList[0] */

  int64_t global_tick_counter; /* PlConsts[CNum].Global_Tick_Counter -
                                * incremented every call; checked against
                                * global_tick_max by the caller the same
                                * way ay_file/ym_file/vtx_file/sndh_file
                                * already do (MIG-0101, see below). */
  int64_t global_tick_max;     /* PlConsts[CNum].Global_Tick_Max -
                                * MIG-0101: computed by pt3_file_load via
                                * a faithful port of GetTimePT3
                                * (Players.pas:15333-15662), a pattern-
                                * opcode-only simulation (no audio
                                * synthesis) that walks the position list
                                * exactly once. 0 only if the file's
                                * position list is somehow degenerate
                                * (empty) - real files always get a real
                                * value. */
  int64_t loop_tick;           /* GetTimePT3's `Lp` output - the tick at
                                 * which PT3_LoopPosition is reached;
                                 * informational only in this port (no
                                 * caller currently reads it - Pascal's
                                 * own RerollMusic doesn't use Lp for
                                 * IsAYNativeFileType seeking either, only
                                 * for a "loop shorter TS pair" case this
                                 * port's TSMode-less scope doesn't have,
                                 * MIG-0007). */
  bool do_loop;                 /* settings.pas: Do_Loop (default false) -
                                  * see CheckLoopAndStop, MIG-0101 */
  bool real_end_all;            /* Players.pas: Real_End_All - MIG-0101 */

  /* Raw (untranscoded CP1251), space-trimmed title/author - Players.pas:
   * "else if FType = FT.PT3" (7405-7427): fixed 32-byte fields at file
   * offsets 0x1E/0x42 - added for the Phase 5 GUI, MIG-0082. The
   * KsaId2[1]/[2] one-byte reads in that same branch (offsets 13 and
   * 98) only affect PLItem.FormatSpec's PT-v3.7-TS-format flag, a
   * playlist/conversion detail out of this port's scope, not ported. */
  char title[64];
  char author[64];
} pt3_file;

/* Parses `data`/`size` (the whole .pt3 file's bytes - standalone files
 * only, see pt3_file.h's file comment) and sets up f->ay + f's tracker
 * state for playback from the beginning of the song. */
pt3_file_status pt3_file_load(pt3_file* f, const uint8_t* data, size_t size,
                               int sample_rate);

#define PT3_FILE_AY_FREQ_DEF 1773400  /* settings.pas: AY_FreqDef */
#define PT3_FILE_INTERRUPT_FREQ_DEF 50000 /* settings.pas: Interrupt_FreqDef */
#define PT3_FILE_SAMPLE_RATE_DEF 48000     /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferTracker (12283-12300) + SynthesizerZX50
 * (AY.pas:1075-1082), the PT3-only (TSMode always false) case, PLUS
 * CheckLoopAndStop (Players.pas:8732-8746, MIG-0101) - sets
 * f->real_end_all once global_tick_counter reaches global_tick_max
 * (when the caller isn't looping), the same "natural end of song"
 * contract ay_file/ym_file/vtx_file/sndh_file's own make_buffer
 * functions already have; looping within the pattern data itself
 * (CurrentPosition wrapping to LoopPosition) keeps working exactly as
 * before regardless. Returns the number of sample frames written -
 * `buffer_length` unless the song ends first (real_end_all become
 * true), same short-return contract as every other format. */
int pt3_file_make_buffer(pt3_file* f, int16_t* buf, int buffer_length);

#endif /* AY_ENGINE_PT3_FILE_H */
