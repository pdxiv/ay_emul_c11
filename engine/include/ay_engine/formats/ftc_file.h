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
#endif
