/* C11 port of ay_emul/Players.pas's PSM ("Pro Sound Maker") module
 * format support (FT.PSM). Eighth of the 13 tracker formats being ported
 * to make all 76 test_corpus_76 files playable identically to the real
 * Pascal codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/STP/PT2/FXM
 * came first). PSM_Get_Registers is Players.pas:11974-12281;
 * InitTrackerModule's FT.PSM branch is Players.pas:3845-3871.
 * LoadTrackerModule has no FT.PSM-specific branch - its on-disk pointers
 * need no load-time relocation, like STC. */
#ifndef AY_ENGINE_PSM_FILE_H
#define AY_ENGINE_PSM_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  PSM_FILE_OK = 0,
  PSM_FILE_ERR_TRUNCATED,
} psm_file_status;

typedef struct psm_channel {
  uint16_t address_in_pattern;
  uint16_t ret_address;
  uint16_t div_shift;
  uint16_t ton;
  uint8_t number_of_notes_to_skip;
  uint8_t note_skip_counter;
  uint8_t amplitude;
  uint8_t ret_cnt;
  uint8_t vol;
  uint8_t vol_cnt;
  uint8_t loop_cnt;
  uint8_t orn;
  uint8_t env_type;
  uint8_t env_div;
  uint8_t samp;
  int8_t orn_tick;
  int8_t smp_tick;
  int8_t note;
} psm_channel;

typedef struct psm_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t current_position;
  int8_t transposition;
  bool finished; /* Players.pas: PSM_Parameters.Finished - a PSM-native
                  * "ran off the end of the position list" flag, freezing
                  * pattern-walk state (PSM_Get_Registers early-exits
                  * once set) but NOT the same thing as real_end_all
                  * below: Pascal's own MakeBufferTracker loop only
                  * consults Real_End_All (set by CheckLoopAndStop), so
                  * a PSM song with `finished` true keeps producing
                  * silent frames (amplitudes already zeroed) until
                  * global_tick_counter separately reaches
                  * global_tick_max - see player.c's player_real_end_all
                  * for how both are now exposed distinctly (MIG-0108). */
  psm_channel chan_a, chan_b, chan_c;
  uint16_t positions_pointer;
  uint16_t samples_pointer;
  uint16_t ornaments_pointer;
  uint16_t patterns_pointer;
  int64_t global_tick_counter;
  int64_t global_tick_max; /* MIG-0101: computed by psm_file_load via a
                            * faithful port of GetTimePSM (Players.pas:
                            * 16819-16881), a pattern-opcode-only
                            * simulation (no audio synthesis) that walks
                            * the position list exactly once. 0 only if
                            * the file's position/pattern data is
                            * structurally degenerate - real files
                            * always get a real value. */
  int64_t loop_tick;       /* GetTimePSM's `Lp` output - the tick at
                             * which the decoded loop position is
                             * reached; informational only in this port
                             * (no caller currently reads it, matching
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
                       * reaches global_tick_max with do_loop false -
                       * NOTE: distinct from `finished` below, PSM's own
                       * separate Finished-flag mechanism (see this
                       * struct's own comment on `finished`, and
                       * player.c's player_real_end_all). */

  /* Raw (untranscoded CP1251), space-trimmed title - Players.pas:
   * "else if FType = FT.PSM" (7532-7550): a variable-length "remark"
   * field at file offset 8, length PositionsPointer-8 (only present if
   * PositionsPointer > 8). If the remark starts with a "psm1\0" prefix
   * AND is longer than 5 bytes, that prefix is stripped and the rest
   * used as the title; if the remark is EXACTLY "psm1\0" (5 bytes, that
   * literal content), it's a sentinel meaning "no real title" and this
   * stays empty; otherwise the whole remark is the title. Added for
   * the Phase 5 GUI, MIG-0083. */
  char title[64];
} psm_file;

psm_file_status psm_file_load(psm_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PSM_FILE_AY_FREQ_DEF 1773400
#define PSM_FILE_INTERRUPT_FREQ_DEF 50000
#define PSM_FILE_SAMPLE_RATE_DEF 48000
int psm_file_make_buffer(psm_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see stc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; psm_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool psm_file_step_registers(psm_file* f, ay_chip* chip);
#endif
