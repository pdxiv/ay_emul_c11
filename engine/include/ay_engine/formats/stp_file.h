/* C11 port of ay_emul/Players.pas's STP ("SoundTrackerPro") module format
 * support (FT.STP). Fifth of the 13 tracker formats being ported to make
 * all 76 test_corpus_76 files playable identically to the real Pascal
 * codebase (see MIG-0028's summary; PT1/GTR/FLS/STC came first).
 * STP_Get_Registers is Players.pas:9517-9702; InitTrackerModule's shared
 * FT.STP/FT.STF branch is Players.pas:3439-3488; LoadTrackerModule's
 * FT.STP branch is Players.pas:2359-2382. Only FT.STP itself is ported -
 * FT.STF is an older on-disk variant that LoadTrackerModule converts to
 * FT.STP's own layout at load time (STFDepack/STF2STP) before ever
 * reaching STP_Get_Registers; the test corpus contains no .stf files, so
 * that converter is out of scope here (real, tracked incompleteness, not
 * silently dropped). LoadTrackerModule's FT.STP branch relocates the
 * on-disk pattern-pointer table by the load address (MAddr) - this
 * project's oracle-harness convention (and ay_player's headless loading)
 * always passes MAddr=0, so that relocation is a byte-for-byte no-op
 * here; only its bounds-validation half (STP_Init_Id/i1 in [0..255]) is
 * replicated, since STP_Init_Id itself is never read by STP_Get_Registers
 * or InitTrackerModule's FT.STP branch. */
#ifndef AY_ENGINE_STP_FILE_H
#define AY_ENGINE_STP_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  STP_FILE_OK = 0,
  STP_FILE_ERR_TRUNCATED,
  STP_FILE_ERR_BAD_HEADER,
} stp_file_status;

typedef struct stp_channel {
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t address_in_pattern;
  uint16_t ton;
  uint8_t position_in_ornament;
  uint8_t loop_ornament_position;
  uint8_t ornament_length;
  uint8_t position_in_sample;
  uint8_t loop_sample_position;
  uint8_t sample_length;
  uint8_t volume;
  uint8_t number_of_notes_to_skip;
  uint8_t note;
  uint8_t amplitude;
  int16_t current_ton_sliding;
  bool envelope_enabled;
  bool enabled;
  int8_t glissade;
  int8_t note_skip_counter;
} stp_channel;

typedef struct stp_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay_counter;
  uint8_t current_position;
  uint8_t transposition;
  stp_channel chan_a, chan_b, chan_c;
  uint8_t delay;
  uint16_t positions_pointer;
  uint16_t patterns_pointer;
  uint16_t ornaments_pointer;
  uint16_t samples_pointer;
  int64_t global_tick_counter;

  /* Raw (untranscoded CP1251), space-trimmed title - Players.pas:
   * "else if FType = FT.STP" (7498-7530): a 28-byte signature check at
   * file offset 10 against KsaId = 'KSA SOFTWARE COMPILATION OF ' -
   * only if it matches is there a title at all (25 bytes, right after
   * the signature, offset 38). No Author field in this branch. Added
   * for the Phase 5 GUI, MIG-0083. */
  char title[64];
} stp_file;

stp_file_status stp_file_load(stp_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define STP_FILE_AY_FREQ_DEF 1773400
#define STP_FILE_INTERRUPT_FREQ_DEF 50000
#define STP_FILE_SAMPLE_RATE_DEF 48000
int stp_file_make_buffer(stp_file* f, int16_t* buf, int buffer_length);
#endif
