/* C11 port of ay_emul/Players.pas's GTR (Sam Coupe/ZX "Globe Tracker" -
 * pattern format tag FT.GTR) module format support. Second of the 13
 * tracker formats being ported to make all 76 test_corpus_76 files
 * playable identically to the real Pascal codebase (see MIG-0028's
 * summary; PT1 was the first). GTR_Get_Registers is Players.pas:11523-
 * 11698; InitTrackerModule's FT.GTR branch is Players.pas:3090-3157;
 * LoadTrackerModule's FT.GTR pointer-normalization branch is
 * Players.pas:2534-2546. */
#ifndef AY_ENGINE_GTR_FILE_H
#define AY_ENGINE_GTR_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  GTR_FILE_OK = 0,
  GTR_FILE_ERR_TRUNCATED,
} gtr_file_status;

typedef struct gtr_channel {
  uint16_t sample_pointer;
  uint16_t ornament_pointer;
  uint16_t address_in_pattern;
  uint16_t ton;
  uint8_t position_in_sample;
  uint8_t loop_sample_position;
  uint8_t sample_length;
  uint8_t position_in_ornament;
  uint8_t loop_ornament_position;
  uint8_t ornament_length;
  uint8_t volume;
  uint8_t note;
  uint8_t amplitude;
  int8_t note_skip_counter;
  bool envelope_enabled;
  bool enabled;
} gtr_channel;

typedef struct gtr_pattern_ptrs {
  uint16_t a, b, c;
} gtr_pattern_ptrs;

typedef struct gtr_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t id[4];        /* GTR_ID - id[3]==0x10 selects an alternate variant */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t current_position;
  gtr_channel chan_a, chan_b, chan_c;
  uint16_t samples_pointers[15];
  uint16_t ornaments_pointers[16];
  gtr_pattern_ptrs patterns_pointers[32];
  uint8_t number_of_positions;
  uint8_t loop_position;
  uint32_t positions_offset;
  int64_t global_tick_counter;
  int64_t global_tick_max; /* MIG-0101: computed by gtr_file_load via a
                            * faithful port of GetTimeGTR (Players.pas:
                            * 14950-15001), a pattern-opcode-only
                            * simulation (no audio synthesis) that walks
                            * the position list exactly once. 0 only if
                            * the file's position/pattern data is
                            * structurally degenerate - real files
                            * always get a real value. */
  int64_t loop_tick;       /* GetTimeGTR's `Lp` output - the tick at
                             * which GTR_LoopPosition is reached;
                             * informational only in this port (no
                             * caller currently reads it, matching
                             * pt3_file.h's loop_tick precedent). */
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
   * "else if FType = FT.GTR" (7345-7353): a fixed 32-byte field at file
   * offset 7, no length prefix or terminator (space-padded, not NUL-
   * terminated) - added for the Phase 5 GUI, MIG-0082. */
  char title[64];
} gtr_file;

gtr_file_status gtr_file_load(gtr_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define GTR_FILE_AY_FREQ_DEF 1773400
#define GTR_FILE_INTERRUPT_FREQ_DEF 50000
#define GTR_FILE_SAMPLE_RATE_DEF 48000
int gtr_file_make_buffer(gtr_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see gtr_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; gtr_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool gtr_file_step_registers(gtr_file* f, ay_chip* chip);
#endif
