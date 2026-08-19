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

#include "ay_engine/hw/ay.h"

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

  int64_t global_tick_max; /* GetTimePSC's `Tm` output - MIG-0103: computed
                            * by psc_file_load via a faithful port of
                            * GetTimePSC (Players.pas:15664-15767), a
                            * pattern-opcode-only simulation (no audio
                            * synthesis) that walks the position list
                            * exactly once. 0 only if the file's position
                            * list is somehow degenerate (empty/malformed)
                            * - real files always get a real value.
                            * MIG-0108: now consumed by psc_file_make_
                            * buffer's own CheckLoopAndStop check, no
                            * longer informational-only. */
  int64_t loop_tick;       /* GetTimePSC's `Lp` output - the tick at which
                            * the position list's loop point is reached;
                            * informational only in this port (no caller
                            * currently reads it). */
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

  /* Raw (untranscoded CP1251), space-trimmed title/author - Players.pas:
   * "else if FType = FT.PSC" (7354-7370): fixed 20-byte fields at file
   * offsets 0x19/0x31 - added for the Phase 5 GUI, MIG-0082. */
  char title[64];
  char author[64];
} psc_file;

psc_file_status psc_file_load(psc_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define PSC_FILE_AY_FREQ_DEF 1773400
#define PSC_FILE_INTERRUPT_FREQ_DEF 50000
#define PSC_FILE_SAMPLE_RATE_DEF 48000
int psc_file_make_buffer(psc_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see psc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; psc_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool psc_file_step_registers(psc_file* f, ay_chip* chip);
#endif
