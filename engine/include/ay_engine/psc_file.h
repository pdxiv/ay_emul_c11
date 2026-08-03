/* C11 port of ay_emul/Players.pas's PSC ("ProSAM Compiler"? - the actual
 * historical name isn't documented in Players.pas, only the FT.PSC tag)
 * module format support. Eleventh of the 13 tracker formats being
 * ported to make all 76 test_corpus_76 files playable identically to
 * the real Pascal codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/STP/
 * PT2/FXM/PSM/ASC/FTC came first). PSC_Get_Registers is
 * Players.pas:10053-10423; InitTrackerModule's FT.PSC branch is
 * Players.pas:3516-3569; the Version-selection logic is Players.pas:
 * 2600-2606 (folded into psc_file_load, since it's a load-time-derived
 * constant, not something that changes per tick). LoadTrackerModule has
 * no FT.PSC-specific branch - its on-disk pointers need no load-time
 * relocation, like STC/PSM.
 *
 * Two behaviors distinct from every other format ported so far: (1) two
 * opcodes ($7A envelope-set, $7B noise-base-set) only take effect when
 * processed by channel B specifically (`if @Chan = @PlParams[CNum].
 * PSC_B`) - replicated via an explicit chan_id parameter rather than
 * pointer-identity comparison; for $7A specifically, channel B ALSO
 * advances its pattern pointer two bytes further than A/C would for the
 * exact same opcode (a real, literal asymmetry in the source, not a
 * porting simplification - safe in practice because real pattern data
 * only ever places $7A in channel B's own stream). (2) the note-set
 * opcode (0..$56) does NOT terminate PatternInterpreter's opcode loop
 * (unlike every other tracker format ported so far) - only the $C0-$FF
 * "note skip count" opcode sets `quit`, so a note can be followed by
 * more commands in the same tick before the interpreter returns. */
#ifndef AY_ENGINE_PSC_FILE_H
#define AY_ENGINE_PSC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay.h"

typedef enum {
  PSC_FILE_OK = 0,
  PSC_FILE_ERR_TRUNCATED,
} psc_file_status;

typedef struct psc_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t ton;
  int16_t current_ton_sliding;
  int16_t ton_accumulator;
  int16_t addition_to_ton;
  int8_t initial_volume;
  int8_t note_skip_counter;
  uint8_t note;
  uint8_t volume;
  uint8_t amplitude;
  uint8_t volume_counter;
  uint8_t volume_counter1;
  uint8_t volume_counter_init;
  uint8_t noise_accumulator;
  uint8_t position_in_sample;
  uint8_t loop_sample_position;
  uint8_t position_in_ornament;
  uint8_t loop_ornament_position;
  bool enabled;
  bool ornament_enabled;
  bool envelope_enabled;
  bool gliss;
  bool ton_slide_enabled;
  bool break_sample_loop;
  bool break_ornament_loop;
  bool volume_inc;
} psc_channel;

typedef struct psc_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint16_t delay;
  uint16_t delay_counter;
  uint16_t lines_counter;
  uint16_t noise_base;
  uint16_t positions_pointer;
  int version;
  psc_channel chan_a, chan_b, chan_c;
  uint16_t ornaments_pointer_base;
  uint16_t samples_pointers[32];
  int64_t global_tick_counter;
} psc_file;

psc_file_status psc_file_load(psc_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PSC_FILE_AY_FREQ_DEF 1773400
#define PSC_FILE_INTERRUPT_FREQ_DEF 50000
#define PSC_FILE_SAMPLE_RATE_DEF 48000
int psc_file_make_buffer(psc_file* f, int16_t* buf, int buffer_length);
#endif
