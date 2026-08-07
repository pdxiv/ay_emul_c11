/* C11 port of ay_emul/Players.pas's FLS ("Flash Tracker") module format
 * support (FT.FLS). Third of the 13 tracker formats being ported to make
 * all 76 test_corpus_76 files playable identically to the real Pascal
 * codebase (see MIG-0028's summary; PT1/GTR came first). FLS_Get_Registers
 * is Players.pas:11337-11521; InitTrackerModule's FT.FLS branch is
 * Players.pas:3216-3260; LoadTrackerModule's FT.FLS branch (the
 * heuristic base-address detection this format needs, since - unlike
 * GTR - it carries no explicit on-disk load-address field) is
 * Players.pas:2463-2532. */
#ifndef AY_ENGINE_FLS_FILE_H
#define AY_ENGINE_FLS_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  FLS_FILE_OK = 0,
  FLS_FILE_ERR_TRUNCATED,
  FLS_FILE_ERR_ADDR_NOT_DETECTED, /* Players.pas's ErFLSAddrNotDetected */
  FLS_FILE_ERR_BAD_HEADER,
} fls_file_status;

typedef struct fls_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t ton;
  uint8_t sample_length;
  uint8_t loop_sample_position;
  uint8_t position_in_sample;
  uint8_t amplitude;
  uint8_t number_of_notes_to_skip;
  uint8_t note;
  int8_t note_skip_counter;
  int8_t sample_tik_counter;
  bool envelope_enabled;
  bool ornament_enabled;
} fls_channel;

typedef struct fls_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay;
  uint8_t delay_counter;
  uint8_t current_position;
  fls_channel chan_a, chan_b, chan_c;
  uint16_t positions_pointer;
  uint16_t ornaments_pointer;
  uint16_t samples_pointer;
  int64_t global_tick_counter;
} fls_file;

fls_file_status fls_file_load(fls_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define FLS_FILE_AY_FREQ_DEF 1773400
#define FLS_FILE_INTERRUPT_FREQ_DEF 50000
#define FLS_FILE_SAMPLE_RATE_DEF 48000
int fls_file_make_buffer(fls_file* f, int16_t* buf, int buffer_length);
#endif
