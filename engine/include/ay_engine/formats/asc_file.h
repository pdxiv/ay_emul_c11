/* C11 port of ay_emul/Players.pas's ASC ("ASC Sound Master") module
 * format support (FT.ASC and FT.ASC0). Ninth of the 13 tracker formats
 * being ported to make all 76 test_corpus_76 files playable identically
 * to the real Pascal codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/
 * STP/PT2/FXM/PSM came first). ASC_Get_Registers is Players.pas:9709-
 * 10051; InitTrackerModule's shared FT.ASC/FT.ASC0 branch is
 * Players.pas:3262-3367 (almost entirely FillChar-redundant comments -
 * the only live code sets Delay/DelayCounter and the 3 channels' initial
 * Address_In_Pattern); LoadTrackerModule's FT.ASC0 branch is
 * Players.pas:2383-2392.
 *
 * ASC0 is an older on-disk variant that LoadTrackerModule converts to
 * ASC1's own layout at load time: ASC0's header (Delay@0,
 * PatternsPointers@1, SamplesPointers@3, OrnamentsPointers@5,
 * NumberOfPositions@7, Positions@8) is missing ASC1's LoopingPosition
 * byte (@1), so the real Pascal code does an in-place `Move` that shifts
 * every byte from offset 1 onward right by one slot (freeing byte 1 for
 * a zeroed LoopingPosition), then increments the 3 now-shifted pointer
 * VALUES by 1 to compensate. asc_file_load replicates this exact shift+
 * fixup for ASC0 input before treating both ASC/ASC0 identically as
 * ASC1's layout - confirmed by reading Players.pas:2383-2392 literally
 * rather than assumed. */
#ifndef AY_ENGINE_ASC_FILE_H
#define AY_ENGINE_ASC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  ASC_FILE_OK = 0,
  ASC_FILE_ERR_TRUNCATED,
} asc_file_status;

typedef struct asc_channel {
  uint16_t initial_point_in_sample;
  uint16_t point_in_sample;
  uint16_t loop_point_in_sample;
  uint16_t initial_point_in_ornament;
  uint16_t point_in_ornament;
  uint16_t loop_point_in_ornament;
  uint16_t address_in_pattern;
  uint16_t ton;
  uint16_t ton_deviation;
  uint8_t note;
  uint8_t addition_to_note;
  uint8_t number_of_notes_to_skip;
  uint8_t initial_noise;
  uint8_t current_noise;
  uint8_t volume;
  uint8_t ton_sliding_counter;
  uint8_t amplitude;
  uint8_t amplitude_delay;
  uint8_t amplitude_delay_counter;
  int16_t current_ton_sliding;
  int16_t substruction_for_ton_sliding;
  int8_t note_skip_counter;
  int8_t addition_to_amplitude;
  bool envelope_enabled;
  bool sound_enabled;
  bool sample_finished;
  bool break_sample_loop;
  bool break_ornament_loop;
} asc_channel;

typedef struct asc_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM), already ASC0->
                        * ASC1 shifted if the input was ASC0 */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t current_position;
  uint8_t looping_position;
  uint8_t number_of_positions;
  asc_channel chan_a, chan_b, chan_c;
  uint16_t patterns_pointer;
  uint16_t samples_pointer;
  uint16_t ornaments_pointer;
  int64_t global_tick_counter;
  int64_t global_tick_max; /* GetTimeASC's `Tm` output (Players.pas:15042-
                            * 15182) - computed by asc_file_load via
                            * asc_get_time, a pattern-opcode-only
                            * simulation (no audio synthesis) that walks
                            * the position list exactly once. 0 if the
                            * position list is degenerate/malformed. */
  int64_t loop_tick;       /* GetTimeASC's `Lp` output - the tick at which
                            * ASC1_LoopingPosition is reached. */
  bool do_loop;      /* MIG-0108: Players.pas: Do_Loop - see pt3_file.h's
                       * own fields for the shape this follows. Shared by
                       * both ASC/ASC1 and ASC0, same as every other
                       * field in this struct. */
  bool force_loop;   /* MIG-0114: Players.pas: Force_Loop (Tools.pas's
                       * CBForceLoop checkbox) - lets THIS voice keep
                       * generating registers (and so keep audibly
                       * looping its own pattern data) past its own
                       * natural end instead of freezing on its last
                       * frame's frozen register values, so a shorter
                       * voice in a mismatched-length Turbosound pair
                       * doesn't just go silent/frozen while the longer
                       * voice keeps playing - see asc_file_step_
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

  /* Raw (untranscoded CP1251), space-trimmed title/author - Players.pas:
   * "else if FType = FT.ASC"/"FT.ASC0" (7436-7473): read at
   * PatternsPointers-44/-20 (20 bytes each) IF PatternsPointers -
   * NumberOfPositions equals a magic constant (72 for ASC1, 71 for
   * ASC0's own pre-shift numbering - see asc_file.c's own comment on
   * why this port needs only ONE check/offset pair post-normalization).
   * Empty if that check fails (a real, common case - not every ASC
   * file has this metadata block). Added for the Phase 5 GUI, MIG-0083. */
  char title[64];
  char author[64];
} asc_file;

/* is_asc0: true for FT.ASC0 input (the older, LoopingPosition-less
 * on-disk layout), false for FT.ASC (ASC1) input. */
asc_file_status asc_file_load(asc_file* f, const uint8_t* data, size_t size,
                               bool is_asc0, int sample_rate);
#define ASC_FILE_AY_FREQ_DEF 1773400
#define ASC_FILE_INTERRUPT_FREQ_DEF 50000
#define ASC_FILE_SAMPLE_RATE_DEF 48000
int asc_file_make_buffer(asc_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see stc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; asc_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback.
 * Shared by ASC/ASC1 and ASC0, same as every other function in this
 * file. */
bool asc_file_step_registers(asc_file* f, ay_chip* chip);
#endif
