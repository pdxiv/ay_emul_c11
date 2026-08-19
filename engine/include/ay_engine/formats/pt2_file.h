/* C11 port of ay_emul/Players.pas's Pro Tracker 2 (PT2) module format
 * support (FT.PT2). Sixth of the 13 tracker formats being ported to make
 * all 76 test_corpus_76 files playable identically to the real Pascal
 * codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/STP came first).
 * PT2_Get_Registers is Players.pas:9091-9324; InitTrackerModule's FT.PT2
 * branch is Players.pas:3622-3708ish; LoadTrackerModule's FT.PT2 branch
 * is Players.pas:2313-2358. In this project's headless-loading
 * convention (PPLItem=nil, MAddr=0 always - see ay_player and every
 * OracleHarness.pas RunXXXFileTest), LoadTrackerModule's FT.PT2 branch
 * ALWAYS re-derives PT2_NumberOfPositions itself (scanning
 * PT2_PositionList for the run of values <128), overwriting whatever was
 * on disk, while its pointer-relocation half (gated on MAddr<>0) is a
 * no-op - both facts confirmed by reading the literal Pascal condition,
 * not assumed. */
#ifndef AY_ENGINE_PT2_FILE_H
#define AY_ENGINE_PT2_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  PT2_FILE_OK = 0,
  PT2_FILE_ERR_TRUNCATED,
  PT2_FILE_ERR_BAD_HEADER,
} pt2_file_status;

typedef struct pt2_channel {
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
  int16_t current_ton_sliding;
  int16_t ton_delta;
  int gliss_type;
  bool envelope_enabled;
  bool enabled;
  int8_t glissade;
  int8_t addition_to_noise;
  int8_t note_skip_counter;
} pt2_channel;

typedef struct pt2_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay_counter;
  uint8_t delay;
  uint8_t current_position;
  pt2_channel chan_a, chan_b, chan_c;
  uint8_t number_of_positions;
  uint8_t loop_position;
  uint16_t samples_pointers[32];
  uint16_t ornaments_pointers[16];
  uint16_t patterns_pointer;
  uint32_t position_list_offset;
  int64_t global_tick_counter;

  int64_t global_tick_max; /* GetTimePT2's `Tm` output (Players.pas:
                            * 15216-15331) - computed by pt2_file_load via
                            * a faithful port of GetTimePT2, a pattern-
                            * opcode-only simulation (no audio synthesis)
                            * that walks the position list exactly once.
                            * 0 if the file's structure is degenerate
                            * enough that Pascal's own GetTimePT2 would
                            * RaiseBadFileStructure. */
  int64_t loop_tick;       /* GetTimePT2's `Lp` output - the tick at which
                            * PT2_LoopPosition is reached. */
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
   * "else if FType = FT.PT2" (7394-7402): a fixed 30-byte field at file
   * offset 101 - added for the Phase 5 GUI, MIG-0082. */
  char title[64];
} pt2_file;

pt2_file_status pt2_file_load(pt2_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PT2_FILE_AY_FREQ_DEF 1773400
#define PT2_FILE_INTERRUPT_FREQ_DEF 50000
#define PT2_FILE_SAMPLE_RATE_DEF 48000
int pt2_file_make_buffer(pt2_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see pt2_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; pt2_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool pt2_file_step_registers(pt2_file* f, ay_chip* chip);
#endif
