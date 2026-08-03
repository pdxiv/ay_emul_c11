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

#include "ay_engine/ay.h"

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
} pt2_file;

pt2_file_status pt2_file_load(pt2_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PT2_FILE_AY_FREQ_DEF 1773400
#define PT2_FILE_INTERRUPT_FREQ_DEF 50000
#define PT2_FILE_SAMPLE_RATE_DEF 48000
int pt2_file_make_buffer(pt2_file* f, int16_t* buf, int buffer_length);
#endif
