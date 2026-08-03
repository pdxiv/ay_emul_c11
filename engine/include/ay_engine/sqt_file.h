/* C11 port of ay_emul/Players.pas's SQT ("SQ-Tracker") module format
 * support (FT.SQT). Twelfth and second-to-last of the 13 tracker
 * formats being ported to make all 76 test_corpus_76 files playable
 * identically to the real Pascal codebase (see MIG-0028's summary;
 * PT1/GTR/FLS/STC/STP/PT2/FXM/PSM/ASC/FTC/PSC came first), and
 * structurally the most complex: SQT_Get_Registers's PatternInterpreter
 * is written as a small tree of named subroutines (Call_LC1D1,
 * Call_LC2A8, Call_LC2D9, Call_LC283, Call_LC191 - names that read like
 * disassembly labels, suggesting this player was itself reverse-
 * engineered from the original Z80 SQ-Tracker replay code) sharing a
 * local "Ptr" cursor via Pascal nested-procedure closure. This port
 * models that with plain functions taking an explicit `uint16_t* ptr`
 * parameter instead of a closure.
 *
 * SQT_Get_Registers is Players.pas:10425-10843; the Version-independent
 * load-time pointer relocation is LoadTrackerModule's FT.SQT branch,
 * Players.pas:2393-2423 - a genuine heuristic (like FLS's, not a stored
 * address like GTR's): the base offset is derived as
 * `SQT_SamplesPointer - 10` (a fixed constant, not read from the file),
 * then the position-list data is scanned to determine how many
 * consecutive header/pattern-table WORDS need the base subtracted
 * (SQT_SamplesPointer/OrnamentsPointer/PatternsPointer/PositionsPointer/
 * LoopPointer and the immediately-following pattern-index table, all
 * being one contiguous run of words starting at SQT_SamplesPointer's own
 * on-disk location) - replicated exactly since this branch is
 * unconditional (no PPLItem/MAddr gating, unlike FTC's). */
#ifndef AY_ENGINE_SQT_FILE_H
#define AY_ENGINE_SQT_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay.h"

typedef enum {
  SQT_FILE_OK = 0,
  SQT_FILE_ERR_TRUNCATED,
  SQT_FILE_ERR_BAD_HEADER,
} sqt_file_status;

typedef struct sqt_channel {
  uint16_t address_in_pattern;
  uint16_t sample_pointer;
  uint16_t point_in_sample;
  uint16_t ornament_pointer;
  uint16_t point_in_ornament;
  uint16_t ton;
  uint16_t ix27;
  uint8_t volume;
  uint8_t amplitude;
  uint8_t note;
  uint8_t ix21;
  int16_t ton_slide_step;
  int16_t current_ton_sliding;
  int8_t sample_tik_counter;
  int8_t ornament_tik_counter;
  int8_t transposit;
  bool enabled;
  bool envelope_enabled;
  bool ornament_enabled;
  bool gliss;
  bool mix_noise;
  bool mix_ton;
  bool b4ix0, b6ix0, b7ix0;
} sqt_channel;

typedef struct sqt_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM), already
                        * relocated (LoadTrackerModule's FT.SQT branch) */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t lines_counter;
  uint16_t positions_pointer;
  sqt_channel chan_a, chan_b, chan_c;
  uint16_t samples_pointer;
  uint16_t ornaments_pointer;
  uint16_t patterns_pointer;
  uint16_t loop_pointer;
  int64_t global_tick_counter;
} sqt_file;

sqt_file_status sqt_file_load(sqt_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define SQT_FILE_AY_FREQ_DEF 1773400
#define SQT_FILE_INTERRUPT_FREQ_DEF 50000
#define SQT_FILE_SAMPLE_RATE_DEF 48000
int sqt_file_make_buffer(sqt_file* f, int16_t* buf, int buffer_length);
#endif
