/* C11 port of ay_emul/Players.pas's FT.OUT format - a raw ZX Spectrum
 * hardware port-write trace (no header at all): a flat stream of 5-byte
 * records (ZX_Takt:smallint LE, ZX_Port:word LE, ZX_Port_Data:byte),
 * decoded by masking ZX_Port against PortMask ($c002, Z80.pas:31) and
 * comparing against $FFFD (AY register SELECT) / $BFFD (AY register
 * WRITE) - the same two ZX Spectrum AY I/O ports real hardware/software
 * used, just replayed from a captured log instead of live Z80 execution.
 * ZX_Takt=-1 is a "no timing info this record" marker (used e.g. for the
 * file's very first record); ZX_Takt=0 (after the -1 substitution below)
 * marks a frame boundary.
 *
 * Genuinely has TWO SEPARATE, independently-evolved register-generation
 * algorithms in the original - not a simplification on this port's part,
 * a real property of the source:
 *  - SynthesizerOUT (AY.pas:1092-1123) + MakeBufferOUT (Players.pas:
 *    8748-8799): live playback, using the SAME Number_Of_Tiks Q32.32
 *    fractional-tick accumulator (ay_engine's number_of_tiks/
 *    frq_ay_by_frq_z80) SynthesizerAY uses for the real .ay format, fed
 *    by each record's own captured T-state delta instead of a live Z80
 *    tact counter. Ported as out_file_make_buffer below.
 *  - OUT_Get_Registers (Players.pas:8801-8840), used only by Convs.pas's
 *    OUT2PSG converter: a much simpler per-CALL accumulator
 *    (OUTZXAYConv_TotalTime, a whole-integer T-state sum compared
 *    against the live MaxTStates setting, NOT Number_Of_Tiks/
 *    FrqAyByFrqZ80 at all) that exits exactly once one MaxTStates-worth
 *    of file time has been consumed - i.e. each CALL corresponds to
 *    exactly one output "tick", the same per-tick contract every other
 *    format's own X_file_step_registers already has. Ported as
 *    out_file_step_registers below.
 * Both share PortMask-based register decode and Previous_AY_Takt
 * (reset to 0 at load/loop-restart, InitForAllTypes's FT.OUT branch,
 * Players.pas:3888-3892) but are otherwise independent - Flg (AY.pas:
 * 149, SynthesizerOUT's own wraparound-continuation flag) is never
 * touched by OUT_Get_Registers, and OUTZXAYConv_TotalTime is never
 * touched by SynthesizerOUT.
 *
 * No native Global_Tick_Counter/Global_Tick_Max concept exists in the
 * original for this format (natural end is simply "ran out of file
 * bytes", UniFilePos>=UniFileSize) - global_tick_max here is a NEW,
 * this-port-only derived value (round((duration_ms/1000)*(FrqZ80/
 * MaxTStates)), Convs.pas:728's own OUT2PSG ProgrMax formula, fed by a
 * duration computed the same way Players.pas:16929-16941's GetTime scan
 * computes it for playlist display) so this format can participate in
 * player_get_tick_position/player_step_registers_any's existing
 * generic per-tick contract exactly like every other format - see
 * out_file_step_registers's own comment for why this is a faithful
 * derivation, not an invented concept.
 *
 * File identification: purely extension-based (.out) - Players.pas has
 * no content-signature check for this format anywhere (confirmed by
 * direct search), matching this port's own existing Tier-A-only
 * handling for STC/PT1/GTR/etc.
 */
#ifndef AY_ENGINE_OUT_FILE_H
#define AY_ENGINE_OUT_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  OUT_FILE_OK = 0,
  OUT_FILE_ERR_TRUNCATED, /* fewer than 5 bytes (not even one record) */
} out_file_status;

typedef struct out_file {
  ay_engine ay;

  uint8_t* data; /* owned copy of the whole raw file */
  size_t data_size;
  size_t pos; /* Players.pas: UniFilePos (byte cursor into data) */

  /* Retained across calls exactly like Players.pas's own unit-scope
   * globals (ZX_Takt/ZX_Port/ZX_Port_Data, AY.pas:159-161) - needed for
   * MakeBufferOUT's deferred-write-across-buffer-boundary resume at its
   * own top (`if IntFlag then begin SynthesizerOUT(Buf); ...`). */
  int16_t zx_takt;
  uint16_t zx_port;
  uint8_t zx_port_data;

  int32_t previous_ay_takt; /* AY.pas: Previous_AY_Takt (longint) - shared
                              * reset point between both algorithms, never
                              * cross-read between them within one call. */
  int16_t flg;               /* AY.pas: Flg (smallint) - SynthesizerOUT's
                               * own wraparound-continuation state only. */
  int32_t out_conv_total_time; /* Players.pas: OUTZXAYConv_TotalTime -
                                 * OUT_Get_Registers's own accumulator
                                 * only, unrelated to flg above. */

  int64_t global_tick_counter;
  int64_t global_tick_max;
  bool do_loop;
  bool real_end_all;

  int frq_z80;     /* MainWin.pas: FrqZ80 (Hz) - AY_FILE_FRQ_Z80_DEF unless
                     * overridden. */
  int max_tstates; /* settings.pas: MaxTStates - AY_FILE_MAX_TSTATES_DEF
                     * unless overridden. Only consulted by out_file_step_
                     * registers/the load-time global_tick_max derivation,
                     * NOT by out_file_make_buffer's own live-playback
                     * cadence (which uses frq_ay_by_frq_z80/Number_Of_
                     * Tiks instead, matching SynthesizerOUT exactly). */
} out_file;

/* Parses `data`/`size` (the whole .out file's bytes, no header) and sets
 * up f->ay for playback. Takes an internal copy; call out_file_free when
 * done. ay_freq/frq_z80/max_tstates select the initial AY/Z80 clocks and
 * (for max_tstates) the export/step-registers frame-quantization period -
 * pass OUT_FILE_AY_FREQ_DEF/OUT_FILE_FRQ_Z80_DEF/OUT_FILE_MAX_TSTATES_DEF
 * for settings.pas's own defaults unless the caller has a reason to
 * override them. */
out_file_status out_file_load(out_file* f, const uint8_t* data, size_t size,
                               int ay_freq, int frq_z80, int max_tstates,
                               int sample_rate);

void out_file_free(out_file* f);

#define OUT_FILE_AY_FREQ_DEF 1773400    /* settings.pas: AY_FreqDef */
#define OUT_FILE_FRQ_Z80_DEF 3494400    /* settings.pas: FrqZ80Def */
#define OUT_FILE_SAMPLE_RATE_DEF 48000  /* settings.pas: SampleRateDef */
#define OUT_FILE_MAX_TSTATES_DEF 69888  /* settings.pas: MaxTStatesDef */

/* Players.pas: MakeBufferOUT (8748-8799) - runs until `buffer_length`
 * stereo16 sample frames have been written or the song ends
 * (f->real_end_all, or loops if f->do_loop). */
int out_file_make_buffer(out_file* f, int16_t* buf, int buffer_length);

/* Players.pas: OUT_Get_Registers (8801-8840) - the frame-quantized,
 * audio-synthesis-free register stepper Convs.pas's OUT2PSG uses,
 * adapted to this port's one-call-per-tick player_step_registers_any
 * contract (see player.h). Returns true if a real tick was generated,
 * false once real_end_all is set (an idempotent no-op after that
 * point, matching every other X_file_step_registers). */
bool out_file_step_registers(out_file* f);

#endif /* AY_ENGINE_OUT_FILE_H */
