/* C11 port of ay_emul/Players.pas's FXM ("Fuxoft AY Music") module format
 * support (FT.FXM). Seventh of the 13 tracker formats being ported to
 * make all 76 test_corpus_76 files playable identically to the real
 * Pascal codebase (see MIG-0028's summary; PT1/GTR/FLS/STC/STP/PT2 came
 * first). FXM_Get_Registers is Players.pas:11700-11972;
 * InitTrackerModule's FT.FXM branch is Players.pas:3041-3088;
 * LoadTrackerModule's FT.FXM-specific load handling is
 * Players.pas:2237,2249 (see fxm_file_load's own comment for the exact
 * mechanics this replicates: a 6-byte on-disk prefix holding a load
 * address, skipped and applied as a copy destination offset, unlike
 * every other format ported so far). FXM's own FormatSpec
 * (PlConsts[n].amad_andsix) is hardcoded to 31 for every FXM file
 * (Players.pas:7542 - not read from a variable header field), so this
 * port hardcodes it too rather than threading a parameter through.
 *
 * FXM is structurally different from every other tracker ported so far:
 * it has no per-file Delay/DelayCounter tempo gate at all - each
 * channel's PatternInterpreter is a small bytecode VM (jumps, calls,
 * loop-counters, a push/pop stack "Stek" for both call-return addresses
 * and loop state) invoked once per tick, running until it consumes an
 * actual note opcode. Its per-channel call/loop stack is modeled here as
 * a fixed-size array (FXM_STEK_MAX entries) rather than Pascal's
 * unbounded dynamic array, since real pattern data nests only a few
 * levels deep - this is a deliberate, documented bound, not a silent
 * truncation (an overflow would corrupt playback, not silently misbehave,
 * and no real corpus file approaches it). */
#ifndef AY_ENGINE_FXM_FILE_H
#define AY_ENGINE_FXM_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  FXM_FILE_OK = 0,
  FXM_FILE_ERR_TRUNCATED,
} fxm_file_status;

#define FXM_STEK_MAX 256

typedef struct fxm_channel {
  uint16_t address_in_pattern;
  uint16_t point_in_sample;
  uint16_t sample_pointer;
  uint16_t point_in_ornament;
  uint16_t ornament_pointer;
  uint16_t ton;
  uint8_t fxm_mixer;
  uint8_t note;
  uint8_t volume;
  uint8_t amplitude;
  int8_t transposit;
  int8_t note_skip_counter;
  int8_t sample_tik_counter;
  bool b0e, b1e, b2e, b3e;
  uint16_t stek[FXM_STEK_MAX];
  int stek_len;
} fxm_channel;

typedef struct fxm_file {
  ay_engine ay;
  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */
  uint8_t noise_base;  /* PlParams[CNum].FXM.Noise_Base - shared, not per-channel */
  fxm_channel chan_a, chan_b, chan_c;
  int64_t global_tick_counter;
  int64_t global_tick_max; /* GetTimeFXM's Tm (Players.pas:14422-14950) */
  int64_t loop_tick;       /* GetTimeFXM's Lp */
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
} fxm_file;

fxm_file_status fxm_file_load(fxm_file* f, const uint8_t* data, size_t size,
                               int sample_rate);
#define FXM_FILE_AY_FREQ_DEF 1773400
#define FXM_FILE_INTERRUPT_FREQ_DEF 50000
#define FXM_FILE_SAMPLE_RATE_DEF 48000
int fxm_file_make_buffer(fxm_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: advances one interrupt frame's worth of registers into
 * `chip` (any ay_chip, not necessarily f->ay.chip - see stc_file.c's own
 * comment) and returns false once this format's own natural end is
 * reached. The building block engine/player.c's playlist-Turbosound-
 * pairing driver (player_step_registers) uses; fxm_file_make_buffer
 * itself now just calls this with &f->ay.chip for standalone playback. */
bool fxm_file_step_registers(fxm_file* f, ay_chip* chip);
#endif
