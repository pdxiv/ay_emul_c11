/* C11 port of ay_emul/Players.pas's FTC ("Fasttracker Compiled") module
 * format support (FT.FTC). Tenth of the 13 tracker formats being ported
 * to make all 76 test_corpus_76 files playable identically to the real
 * Pascal codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/STP/PT2/FXM/
 * PSM/ASC came first). FTC_Get_Registers is Players.pas:10845-11174;
 * InitTrackerModule's FT.FTC branch is Players.pas:3368-3420ish; the
 * Version-selection logic (Players.pas:2611-2617, gating GetNoteFreq's
 * choice of note table) is folded into ftc_file_load directly since it's
 * a load-time-derived constant, not something that changes per tick. */
#ifndef AY_ENGINE_FTC_FILE_H
#define AY_ENGINE_FTC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  FTC_FILE_OK = 0,
  FTC_FILE_ERR_TRUNCATED,
} ftc_file_status;

typedef struct ftc_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t envelope_accumulator;
  uint16_t envelope;
  uint16_t ton;
  uint8_t ornament_length;
  uint8_t loop_ornament_position;
  uint8_t position_in_ornament;
  uint8_t sample_length;
  uint8_t loop_sample_position;
  uint8_t position_in_sample;
  uint8_t sample_noise_accumulator;
  uint8_t noise_accumulator;
  uint8_t note_accumulator;
  uint8_t ton_slide_direction;
  uint8_t volume;
  uint8_t noise;
  uint8_t amplitude;
  uint8_t previous_note;
  uint8_t note;
  int8_t note_skip_counter;
  int8_t volume_slide;
  int16_t addition_to_ton;
  int16_t ton_slide_step;
  int16_t ton_slide_step1;
  int16_t current_ton_sliding;
  int16_t ton_accumulator;
  bool envelope_enabled;
  bool sample_enabled;
} ftc_channel;

typedef struct ftc_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t transposition;
  uint8_t current_position;
  uint8_t env_t;
  uint8_t retrig;
  int version;
  ftc_channel chan_a, chan_b, chan_c;
  uint16_t patterns_pointer;
  uint16_t samples_pointers[32];
  uint16_t ornaments_pointers[33];
  uint32_t positions_offset;
  int64_t global_tick_counter;

  int64_t global_tick_max; /* GetTimeFTC's `Tm` output - MIG-0103: computed
                            * by ftc_file_load via a faithful port of
                            * GetTimeFTC (Players.pas:15769-15887), a
                            * pattern-opcode-only simulation (no audio
                            * synthesis) that walks the position list
                            * exactly once, including its own DLCatcher
                            * (256) safety net against pathologically long
                            * per-position note-walks. 0 only if the
                            * file's position list is somehow degenerate
                            * (empty/malformed) - real files always get a
                            * real value. MIG-0108: now consumed by
                            * ftc_file_make_buffer's own CheckLoopAndStop
                            * check, no longer informational-only. */
  int64_t loop_tick;       /* GetTimeFTC's `Lp` output - the tick at which
                            * FTC_Loop_Position is reached; informational
                            * only in this port (no caller currently reads
                            * it). */
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
   * "else if FType = FT.FTC" (7372-7380): a fixed 42-byte field at file
   * offset 8 (within FTC_MusicName[0..68]@0's larger 69-byte area) -
   * added for the Phase 5 GUI, MIG-0082. */
  char title[64];
} ftc_file;

ftc_file_status ftc_file_load(ftc_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define FTC_FILE_AY_FREQ_DEF 1773400
#define FTC_FILE_INTERRUPT_FREQ_DEF 50000
#define FTC_FILE_SAMPLE_RATE_DEF 48000
int ftc_file_make_buffer(ftc_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see ftc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; ftc_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool ftc_file_step_registers(ftc_file* f, ay_chip* chip);
#endif
