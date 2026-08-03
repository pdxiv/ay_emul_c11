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

#include "ay_engine/ay.h"

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
  bool finished;
  psm_channel chan_a, chan_b, chan_c;
  uint16_t positions_pointer;
  uint16_t samples_pointer;
  uint16_t ornaments_pointer;
  uint16_t patterns_pointer;
  int64_t global_tick_counter;
} psm_file;

psm_file_status psm_file_load(psm_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PSM_FILE_AY_FREQ_DEF 1773400
#define PSM_FILE_INTERRUPT_FREQ_DEF 50000
#define PSM_FILE_SAMPLE_RATE_DEF 48000
int psm_file_make_buffer(psm_file* f, int16_t* buf, int buffer_length);
#endif
