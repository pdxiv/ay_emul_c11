/* C11 port of ay_emul/Players.pas's STC (SoundTracker Compiled/"Pro
 * Tracker" ancestor) module format support (FT.STC). Fourth of the 13
 * tracker formats being ported to make all 76 test_corpus_76 files
 * playable identically to the real Pascal codebase (see MIG-0028's
 * summary; PT1/GTR/FLS came first). STC_Get_Registers is
 * Players.pas:9325-9515; InitTrackerModule's shared FT.STC/ST1/ST3
 * branch is Players.pas:3160-3214. Only FT.STC itself is ported here -
 * FT.ST1/FT.ST3 are older on-disk variants that LoadTrackerModule
 * converts to FT.STC's own layout at load time (ST12STC/ST32STC,
 * Players.pas:1766/2051) before ever reaching STC_Get_Registers; the
 * test corpus (test_corpus_76/) contains no .st1/.st3 files, so those
 * converters are out of scope here (real, tracked incompleteness - not
 * silently dropped - if a .st1/.st3 file is ever added to the corpus,
 * this port will need extending, or that file will simply fail to
 * load). Unlike GTR/FLS, STC's on-disk pointers need no load-time
 * relocation at all (LoadTrackerModule has no FT.STC-specific branch). */
#ifndef AY_ENGINE_STC_FILE_H
#define AY_ENGINE_STC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  STC_FILE_OK = 0,
  STC_FILE_ERR_TRUNCATED,
} stc_file_status;

typedef struct stc_channel {
  uint16_t address_in_pattern;
  uint16_t sample_pointer;
  uint16_t ornament_pointer;
  uint16_t ton;
  uint8_t amplitude;
  uint8_t note;
  uint8_t position_in_sample;
  uint8_t number_of_notes_to_skip;
  int8_t sample_tik_counter;
  int8_t note_skip_counter;
  bool envelope_enabled;
} stc_channel;

typedef struct stc_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t delay_counter;
  uint8_t transposition;
  uint8_t current_position;
  stc_channel chan_a, chan_b, chan_c;
  uint8_t delay;
  uint16_t positions_pointer;
  uint16_t ornaments_pointer;
  uint16_t patterns_pointer;
  int64_t global_tick_counter;
  int64_t global_tick_max; /* MIG-0101: computed by stc_file_load via a
                            * faithful port of GetTimeSTC (Players.pas:
                            * 15003-15040), a pattern-opcode-only
                            * simulation (no audio synthesis) that walks
                            * the position list exactly once. Unlike
                            * GTR/PT3/PSM's GetTimeXXX, STC has no
                            * loop-point (`Lp`) output, so no loop_tick
                            * field is added here - see pt3_file.h's
                            * loop_tick for the shape that formats WITH
                            * a loop point get. 0 only if the file's
                            * position/pattern data is structurally
                            * degenerate - real files always get a real
                            * value. */
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
} stc_file;

stc_file_status stc_file_load(stc_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define STC_FILE_AY_FREQ_DEF 1773400
#define STC_FILE_INTERRUPT_FREQ_DEF 50000
#define STC_FILE_SAMPLE_RATE_DEF 48000
int stc_file_make_buffer(stc_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see stc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; stc_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool stc_file_step_registers(stc_file* f, ay_chip* chip);
#endif
