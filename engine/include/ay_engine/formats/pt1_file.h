/* C11 port of ay_emul/Players.pas's Pro Tracker 1 (PT1) module format
 * support (FT.PT1) - the oldest/simplest of the ZX Spectrum tracker
 * formats this project ports (no sample-length envelope-slide fields
 * like PT3, no turbosound, no per-format note table selection - always
 * PT3NoteTable_ST). Built on the existing engine/ay.h mixer, same shape
 * as engine/pt3_file.c (PatternInterpreter + GetRegisters against a flat
 * 65536-byte ZRAM buffer, SynthesizerZX50 fixed-tick-budget cadence) -
 * see that file for the shared conventions (register index mapping,
 * `rd16` helper, etc).
 *
 * Scope: standalone .pt1 files loaded directly (matches pt3_file.h's
 * same restriction - no embedded/rebased-pointer loading).
 *
 * Ports:
 *  - The file-into-RAM load (Players.pas:2248-2264, shared with PT3 -
 *    a raw byte copy into a flat 65536-byte buffer, zero-padded).
 *  - PT1_Get_Registers in full (Players.pas:11176-11335): PatternInterpreter
 *    (a single flat opcode-stream walker - notes 0..$5F, sample-select
 *    $60..$6F, ornament-select $70..$7F, note-off $80, envelope-off $81,
 *    envelope-on $82..$8F, rest $90, delay-set $91..$A0, volume-set
 *    $A1..$B0, note-skip-count else) and GetRegisters (per-tick tone/
 *    amplitude/noise/envelope derivation from sample+ornament state,
 *    reusing PT3NoteTable_ST - Players.pas:11246-11268's arithmetic is
 *    transcribed verbatim, not simplified).
 *  - The InitTrackerModule FT.PT1 branch (Players.pas:3570-3621).
 *
 * Also ports:
 *  - GetTimePT1 (duration/loop-point precompute, Players.pas:16679-16770)
 *    as pt1_get_time, called once at the end of pt1_file_load, storing
 *    into global_tick_max/loop_tick below. MIG-0108: a CheckLoopAndStop-
 *    equivalent gate is now wired into pt1_file_make_buffer's tick loop
 *    (same hoisted-into-make_buffer shape as pt3_file's own, since this
 *    port has no per-tick Get_Registers-level early-exit convenience
 *    the way Pascal's own CheckLoopAndStop call does) - do_loop/
 *    real_end_all below are live, not informational.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - Embedded/rebased-pointer PT1 loading - same rationale as PT3.
 */
#ifndef AY_ENGINE_PT1_FILE_H
#define AY_ENGINE_PT1_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  PT1_FILE_OK = 0,
  PT1_FILE_ERR_BAD_HEADER,
  PT1_FILE_ERR_TRUNCATED,
} pt1_file_status;

typedef struct pt1_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t ton;
  uint8_t number_of_notes_to_skip;
  uint8_t volume;
  uint8_t loop_sample_position;
  uint8_t position_in_sample;
  uint8_t sample_length;
  uint8_t amplitude;
  uint8_t note;
  int8_t note_skip_counter;
  bool envelope_enabled;
  bool enabled;
} pt1_channel;

typedef struct pt1_file {
  ay_engine ay;

  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */

  uint8_t delay;        /* PT1_Parameters.Delay */
  uint8_t delay_counter; /* PT1_Parameters.DelayCounter */
  uint8_t current_position;

  pt1_channel chan_a, chan_b, chan_c;

  uint16_t patterns_pointer; /* PT1_PatternsPointer */
  uint16_t samples_pointers[16];
  uint16_t ornaments_pointers[16];
  uint8_t number_of_positions;
  uint8_t loop_position;
  uint32_t position_list_offset; /* byte offset of PT1_PositionList[0] */

  int64_t global_tick_counter; /* informational only - see file comment */

  int64_t global_tick_max; /* GetTimePT1's `Tm` output (Players.pas:
                            * 16679-16770) - computed by pt1_file_load via
                            * a faithful port of GetTimePT1, a pattern-
                            * opcode-only simulation (no audio synthesis)
                            * that walks the position list exactly once.
                            * 0 if the file's structure is degenerate
                            * enough that Pascal's own GetTimePT1 would
                            * RaiseBadFileStructure. */
  int64_t loop_tick;       /* GetTimePT1's `Lp` output - the tick at which
                            * PT1_LoopPosition is reached. */
  bool do_loop;      /* MIG-0108: Players.pas: Do_Loop - see pt3_file.h's
                       * own fields for the shape this follows. */
  bool force_loop;   /* MIG-0114: Players.pas: Force_Loop (Tools.pas's
                       * CBForceLoop checkbox) - lets THIS voice keep
                       * generating registers (and so keep audibly
                       * looping its own pattern data) past its own
                       * natural end instead of freezing on its last
                       * frame's frozen register values, so a shorter
                       * voice in a mismatched-length Turbosound pair
                       * doesn't just go silent/frozen while the longer
                       * voice keeps playing - see <fmt>_file_step_
                       * registers's own CheckLoopAndStop-equivalent
                       * logic (Players.pas:8730-8746) for the exact
                       * semantics. Distinct from do_loop (which makes
                       * the WHOLE song loop, never setting real_end_
                       * all at all) - force_loop still marks real_
                       * end_all true, it just doesn't stop register
                       * generation once that happens. */
  bool real_end_all; /* MIG-0108: Players.pas: Real_End_All, set by
                       * CheckLoopAndStop once global_tick_counter
                       * reaches global_tick_max with do_loop false. */

  /* Raw (untranscoded CP1251), space-trimmed module title - Players.pas:
   * "else if FType = FT.PT1" (7383-7391): a fixed 30-byte field at file
   * offset 69 - added for the Phase 5 GUI, MIG-0082. */
  char title[64];
} pt1_file;

/* Parses `data`/`size` (the whole .pt1 file's bytes - standalone files
 * only) and sets up f->ay + f's tracker state for playback from the
 * beginning of the song. */
pt1_file_status pt1_file_load(pt1_file* f, const uint8_t* data, size_t size,
                               int sample_rate);

#define PT1_FILE_AY_FREQ_DEF 1773400      /* settings.pas: AY_FreqDef */
#define PT1_FILE_INTERRUPT_FREQ_DEF 50000 /* settings.pas: Interrupt_FreqDef */
#define PT1_FILE_SAMPLE_RATE_DEF 48000    /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferTracker (12283-12300) + SynthesizerZX50
 * (AY.pas:1075-1082), same contract as pt3_file_make_buffer: writes
 * exactly `buffer_length` frames (no natural end-of-song concept, see
 * file comment). */
int pt1_file_make_buffer(pt1_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see pt1_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; pt1_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool pt1_file_step_registers(pt1_file* f, ay_chip* chip);
#endif /* AY_ENGINE_PT1_FILE_H */
