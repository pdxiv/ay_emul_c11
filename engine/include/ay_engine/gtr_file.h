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

#include "ay_engine/ay.h"

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
} gtr_file;

gtr_file_status gtr_file_load(gtr_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define GTR_FILE_AY_FREQ_DEF 1773400
#define GTR_FILE_INTERRUPT_FREQ_DEF 50000
#define GTR_FILE_SAMPLE_RATE_DEF 48000
int gtr_file_make_buffer(gtr_file* f, int16_t* buf, int buffer_length);
#endif
