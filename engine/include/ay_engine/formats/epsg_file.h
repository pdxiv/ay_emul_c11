/* C11 port of ay_emul/Players.pas's FT.EPSG format - a 16-byte-header
 * register-write-log (identification: Players.pas:7816-7843: bytes[0..3]
 * = "EPSG", byte[4] = $1A, byte[5] selects the per-frame T-state period:
 * 0 -> 70908, 1 -> 71680, 255 -> a custom little-endian dword at
 * bytes[6..9]; anything else is rejected). Register records begin at
 * byte offset 16 (bytes[10..15] reserved/unused, matching PSG's own
 * 16-byte-header convention), each 5 bytes on disk: Reg(byte), Data
 * (byte), TSt (a 3-byte little-endian T-state value - TEPSGRec's own
 * `Reg,Data:byte; TSt:longword` variant is only ever populated from a
 * 5-byte UniRead, so TSt's top byte is always implicitly 0, confirmed
 * by direct trace of Players.pas:300-305/8868/8914's own `EPSGRec.All :=
 * 0` zero-init preceding every read loop - not stack garbage, a fully
 * deterministic 24-bit field in practice). All-$FF-bytes (Reg=Data=TSt=
 * $FFFFFF) is a frame-boundary sentinel record, not a real register
 * write.
 *
 * Like FT.OUT (see out_file.h), has TWO SEPARATE register-generation
 * algorithms in the original:
 *  - SynthesizerEPSG (AY.pas:1137-1157) + MakeBufferEPSG (Players.pas:
 *    8842-8905): live playback, Number_Of_Tiks Q32.32 accumulation from
 *    each record's own AY_Takt delta - simpler than SynthesizerOUT (no
 *    17472-style wraparound; a sentinel record instead rebases
 *    Previous_AY_Takt by EPSG_TStateMax so the delta stays continuous
 *    across the frame boundary: `Dec(Previous_AY_Takt, EPSG_TStateMax)`).
 *    Ported as epsg_file_make_buffer below.
 *  - EPSG_Get_Registers (Players.pas:8907-8922), used only by Convs.
 *    pas's EPSG2PSG converter: genuinely simpler still - no timing math
 *    at all, just applies every non-sentinel record's register write
 *    immediately and stops at the next sentinel or EOF. One CALL = one
 *    output tick, matching every other format's X_file_step_registers
 *    contract directly - no adaptation needed the way OUT_Get_Registers
 *    needed (see out_file.h). Ported as epsg_file_step_registers below.
 *
 * No native Global_Tick_Counter/Global_Tick_Max concept exists in the
 * original here either - global_tick_max is derived the same way as
 * out_file.h's own (Players.pas:16942-16965's GetTime scan -> Convs.
 * pas:763's EPSG2PSG ProgrMax formula), for the same reason (letting
 * this format participate in player_get_tick_position/player_step_
 * registers_any's existing generic contract).
 */
#ifndef AY_ENGINE_EPSG_FILE_H
#define AY_ENGINE_EPSG_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  EPSG_FILE_OK = 0,
  EPSG_FILE_ERR_BAD_HEADER,   /* not "EPSG"+$1A, or an unrecognized
                                * byte[5] selector (Mes_UnsupportedEPSGCType
                                * in the original) */
  EPSG_FILE_ERR_TRUNCATED,
} epsg_file_status;

typedef struct epsg_file {
  ay_engine ay;

  uint8_t* data; /* owned copy of the whole raw file, including its own
                   * 16-byte header (pos starts at 16, matching Players.
                   * pas:4034-4038's InitForAllTypes FT.EPSG branch). */
  size_t data_size;
  size_t pos;

  int32_t epsg_tstate_max; /* Players.pas: EPSG_TStateMax, from byte[5]/
                             * bytes[6..9] of this file's own header. */

  /* Retained across calls, matching Players.pas's own unit-scope globals
   * (AY.pas:150/162-163) - needed for MakeBufferEPSG's deferred-write-
   * across-buffer-boundary resume (`if IntFlag then begin
   * SynthesizerEPSG(Buf); ... if Flg <> 0 then SetAYRegister(...)`). */
  uint8_t ay_reg;
  uint8_t ay_data;
  int32_t ay_takt;          /* AY.pas: AY_Takt (longint) */
  int32_t previous_ay_takt; /* AY.pas: Previous_AY_Takt */
  int16_t flg;              /* AY.pas: Flg - here doubles as "was the last
                              * record read a real register write (1) vs
                              * a frame-boundary sentinel (0)", matching
                              * MakeBufferEPSG's own use exactly (a
                              * different role than out_file's flg, which
                              * is SynthesizerOUT-specific wraparound
                              * state - the two formats share the
                              * variable's NAME in Pascal, not its
                              * meaning). */

  int64_t global_tick_counter;
  int64_t global_tick_max;
  bool do_loop;
  bool real_end_all;

  int frq_z80; /* MainWin.pas: FrqZ80 (Hz) - EPSG_FILE_FRQ_Z80_DEF unless
                * overridden. */
} epsg_file;

/* Parses `data`/`size` (the whole .epsg file's bytes, header included)
 * and sets up f->ay for playback. Takes an internal copy; call
 * epsg_file_free when done. ay_freq/frq_z80 select the initial AY/Z80
 * clocks - pass EPSG_FILE_AY_FREQ_DEF/EPSG_FILE_FRQ_Z80_DEF for
 * settings.pas's own defaults unless the caller has a reason to
 * override them. */
epsg_file_status epsg_file_load(epsg_file* f, const uint8_t* data,
                                 size_t size, int ay_freq, int frq_z80,
                                 int sample_rate);

void epsg_file_free(epsg_file* f);

#define EPSG_FILE_AY_FREQ_DEF 1773400   /* settings.pas: AY_FreqDef */
#define EPSG_FILE_FRQ_Z80_DEF 3494400   /* settings.pas: FrqZ80Def */
#define EPSG_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferEPSG (8842-8905) - runs until `buffer_length`
 * stereo16 sample frames have been written or the song ends
 * (f->real_end_all, or loops if f->do_loop). */
int epsg_file_make_buffer(epsg_file* f, int16_t* buf, int buffer_length);

/* Players.pas: EPSG_Get_Registers (8907-8922) - the audio-synthesis-free
 * register stepper Convs.pas's EPSG2PSG uses, matching this port's
 * one-call-per-tick player_step_registers_any contract directly (no
 * adaptation needed, unlike out_file_step_registers - see this file's
 * own comment). Returns true if a real tick was generated, false once
 * real_end_all is set (an idempotent no-op after that point, matching
 * every other X_file_step_registers). */
bool epsg_file_step_registers(epsg_file* f);

#endif /* AY_ENGINE_EPSG_FILE_H */
