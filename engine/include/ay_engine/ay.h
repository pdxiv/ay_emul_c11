/* C11 port of ay_emul/AY.pas - AY-3-8910/YM2149 sound chip emulation core.
 *
 * Scope for this milestone: the chip logic and mixer path actually used by
 * Z80-driven playback (Players.pas's MakeBufferAY -> SynthesizerAY), i.e.
 * AY.pas's TSoundChip object, its register file, Synthesizer_Logic_Q /
 * Synthesizer_Mixer_Q(_Mono), ApplyFilter, and SynthesizerAY itself, plus
 * the stereo16/mono16 output stages needed to produce a testable PCM
 * buffer. AY.pas's other Synthesizer* variants (ZX50/OUT/YM6/EPSG/ZXAY) are
 * the cadence functions for the *other* file formats Players.pas decodes
 * without a real Z80 core - those belong to the Players.pas format-parser
 * milestone (out of scope here), not this one.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - TSMode / dual-chip (Turbosound) support - single chip only in this
 *    milestone (MIG-0007).
 *  - Atari_MixDMASnd (atari.pas, 68000/Atari-ST DMA sound) - stubbed as a
 *    no-op; out of scope until the Musashi core milestone (MIG-0008).
 *  - Synthesizer_Stereo8 / Synthesizer_Mono8 (8-bit output) - straightforward
 *    duplicates of the 16-bit paths ported here, deferred as low-risk
 *    remaining work (MIG-0009).
 *  - VisPoints visualization sampling (FillVis) - GUI-only, no consumer
 *    until the GUI milestone.
 */
#ifndef AY_ENGINE_AY_H
#define AY_ENGINE_AY_H

#include <stdbool.h>
#include <stdint.h>

/* Amplitude tables of sound chips, (c) Hacker KAY - AY.pas:19-23,33-37.
 * Carried over byte-for-byte: this is a fidelity-sensitive constant, not a
 * value to substitute with a generic library's own DAC table. */
extern const uint16_t AY_AMPLITUDES_AY[16];
extern const uint16_t AY_AMPLITUDES_YM[32];

typedef enum {
  AY_CHIP_TYPE_AY = 0, /* AY.pas: AY_Chip */
  AY_CHIP_TYPE_YM = 1  /* AY.pas: YM_Chip */
} ay_chip_type;

/* Mirrors AY.pas's TSoundChip. RegisterAY is kept as a flat 16-byte
 * register file (index[0..13] used, matching TRegisterAY's packed variant
 * layout: index[0..1]=TonA (LE word), [2..3]=TonB, [4..5]=TonC, [6]=Noise,
 * [7]=Mixer, [8]=AmplitudeA, [9]=AmplitudeB, [10]=AmplitudeC,
 * [11..12]=Envelope (LE word), [13]=EnvType) rather than a packed union, to
 * avoid relying on compiler-specific struct packing for something C11
 * doesn't guarantee portably. */
typedef struct ay_chip {
  uint8_t reg[16];

  bool first_period;
  int ampl; /* AY.pas: Ampl */

  uint32_t ton_counter_a, ton_counter_b, ton_counter_c; /* .Hi half only used */
  uint32_t noise_counter;
  uint64_t envelope_counter;

  int ton_a, ton_b, ton_c;

  /* AY.pas's Noise is a packed variant record overlaying Seed:longword with
   * (Low:word; Val:dword) at the same offset. Val's low 16 bits therefore
   * always mirror Seed's high 16 bits (Val's own high 16 bits are only ever
   * explicitly zeroed at reset and never written again) - since
   * NoiseGenerator bounds Seed to 17 bits ($1ffff), Seed>>16 is always 0 or
   * 1, i.e. Noise.Val is just the top LFSR bit used as a 0/1 gate. Modeled
   * here as a plain field derived from noise_seed (see ay_noise_val())
   * rather than reproducing the byte-overlap. */
  uint32_t noise_seed; /* Noise.Seed */

  bool ton_en_a, ton_en_b, ton_en_c;
  bool noise_en_a, noise_en_b, noise_en_c;
  bool envelope_en_a, envelope_en_b, envelope_en_c;

  uint8_t current_register_ay; /* AY.pas: Current_RegisterAY */

  /* Which Case_EnvType_* variant is selected (AY.pas: Case_EnvType
   * procedure-of-object pointer). Stored as an enum tag instead of a
   * function pointer for a simpler, branch-based C11 equivalent. */
  int env_type_case;
} ay_chip;

uint16_t ay_reg_tona(const ay_chip* c);
uint16_t ay_reg_tonb(const ay_chip* c);
uint16_t ay_reg_tonc(const ay_chip* c);
uint16_t ay_reg_envelope(const ay_chip* c);
uint32_t ay_noise_val(const ay_chip* c); /* see noise_seed comment above */

void ay_chip_reset(ay_chip* c, bool zeroregs);
void ay_chip_set_ay_register(ay_chip* c, int num, uint8_t value); /* SetAYRegister */
void ay_chip_set_ay_register_fast(ay_chip* c, int num, uint8_t value); /* SetAYRegisterFast */
void ay_chip_synthesizer_logic_q(ay_chip* c);

/* Global mixer/engine state, mirroring AY.pas's unit-level globals for the
 * single-chip (non-Turbosound) case. One instance per player session. */
typedef struct ay_engine {
  ay_chip chip; /* AY.pas: SoundChip[0] */

  ay_chip_type chip_type;   /* AY.pas: ChType */
  uint8_t pre_amp;          /* AY.pas: PreAmp */
  uint8_t pre_amp_max;      /* AY.pas: PreAmpMax */
  int number_of_channels;   /* settings.pas: NumberOfChannels (1 or 2) */
  int sample_bits;          /* settings.pas: SampleBit (8 or 16) */
  uint8_t beeper_max;       /* settings.pas: BeeperMax (BeeperMaxDef=146) */
  uint8_t atari_dma_max;    /* settings.pas: Atari_DMAMax - 0 disables the
                             * DMA-sound headroom contribution entirely
                             * (matches original behavior with DMA sound
                             * absent/stopped), nonzero when a caller wires
                             * up engine/dma_sound.h via on_mix_dma below. */
  int atari_dma_level;      /* AY.pas: Atari_DMALevel, computed by
                             * ay_engine_calculate_level_tables - the scale
                             * factor engine/dma_sound.h's dma_sound_mix
                             * expects. */

  /* AY.pas: Atari_MixDMASnd(LevL,LevR), called from Synthesizer_Mixer_Q/
   * _Mono before the AY tone/noise/envelope channels are added in. NULL
   * (the default) matches the original with DMA sound inactive - a real
   * hook belongs to whichever milestone wires up a live dma_sound alongside
   * this engine (see migration_debt.yaml). */
  void (*on_mix_dma)(void* userdata, int* lev_l, int* lev_r);
  void* on_mix_dma_userdata;

  /* Per-channel index/pan weights, settings-derived (AY.pas: Index_AL etc). */
  uint8_t index_al, index_ar, index_bl, index_br, index_cl, index_cr;

  /* Precomputed mixing-level tables (AY.pas: Level_AL/AR/BL/BR/CL/CR). */
  int level_al[32], level_ar[32];
  int level_bl[32], level_br[32];
  int level_cl[32], level_cr[32];

  int beeper_level;  /* AY.pas: BeeperLevel, computed by Calculate_Level_Tables */
  int beeper, beeper_next;
  bool int_flag, int_beeper, int_ay;
  int reg_num_next;
  uint8_t dat_next;

  int64_t frq_ay_by_frq_z80; /* AY.pas: FrqAyByFrqZ80 */
  int64_t previous_tact;     /* AY.pas: Previous_Tact */
  int64_t number_of_tiks;    /* AY.pas: Number_Of_Tiks.Re */
  int64_t current_tik;       /* AY.pas: Current_Tik */
  /* AY.pas's Tick_Counter is a packed variant (Lo:word;Hi:word) aliased as
   * Re:integer; only .Hi is ever incremented or read (Lo is never written
   * after the initial reset-to-zero), so Tick_Counter.Re always equals
   * Tick_Counter.Hi << 16 exactly - modeled here as the single
   * tick_counter_hi field, with .Re's value computed on demand
   * (ay_tick_counter_re()) rather than tracked as separate, driftable
   * state. */
  uint32_t tick_counter_hi;  /* AY.pas: Tick_Counter.Hi */
  int64_t tik_re;            /* AY.pas: Tik.Re */
  uint32_t delay_in_tiks;    /* AY.pas: Delay_in_tiks */

  int prev_left, prev_right;
  int left_chan, right_chan;
  int left_chan1, right_chan1;

  /* ApplyFilter state. Filt_XL/Filt_XR are separate delay-line buffers that
   * intentionally share one cursor, Filt_I, kept in sync across the L/R
   * calls each sample by saving/restoring it around the pair (AY.pas:
   * `Tmp := Filt_I; LevelL := ApplyFilter(...,Filt_XL); Filt_I := Tmp;
   * LevelR := ApplyFilter(...,Filt_XR)`) - not a bug, a deliberate way to
   * keep both channels at the same delay-line position. Reproduced as-is
   * in ay_synthesizer_stereo16. */
  int* filt_k;   /* AY.pas: Filt_K, length filt_m + 1 */
  int filt_m;    /* AY.pas: Filt_M */
  int is_filt;   /* AY.pas: IsFilt - tri-state: -1 disables filtering
                  * entirely (ay_apply_filter is not called); 0 or 1 both
                  * call it (differing only in the interpolator flavor
                  * selected in ay_synthesizer_stereo16/mono16), so both
                  * require filt_k/filt_m/filt_xl/filt_xr to already be set
                  * up. AY.pas's own default is 1, but that assumes
                  * mixerctl.pas/digsound.pas (not ported this milestone,
                  * see migration_debt.yaml) already populated Filt_K/Filt_M
                  * - ay_engine_init() defaults to -1 instead so a caller
                  * that hasn't wired up filter coefficients yet gets
                  * unfiltered output rather than a null-pointer crash. */
  int* filt_xl;  /* AY.pas: Filt_XL, length filt_m + 1 */
  int* filt_xr;  /* AY.pas: Filt_XR, length filt_m + 1 */
  int filt_i;    /* AY.pas: Filt_I */

  /* Output buffer cursor, mirrors Players.pas's BufP/BuffLen/BufferLength
   * globals - owned by the caller in the real port (Players.pas), exposed
   * here only so the stereo16/mono16 output stages ported for this
   * milestone's differential testing can run standalone. */
  /* AY.pas: BufP is an untyped `pointer`, cast to PS16/PS8/PM16/PM8 by
   * whichever Synthesizer_* variant is active - kept untyped here too
   * rather than picking one width, since both 16-bit and 8-bit output are
   * supported (ay_synthesizer_stereo16/mono16 vs _stereo8/_mono8). */
  void* buf;
  int buf_len;
  int buffer_length;
} ay_engine;

void ay_engine_init(ay_engine* e);
void ay_engine_reset_chip(ay_engine* e, bool zeroregs); /* ResetAYChipEmulation */
void ay_engine_calculate_level_tables(ay_engine* e); /* Calculate_Level_Tables */

/* MainWin.pas: TFrmMain.SetFilter/SetFilter2/CalcFiltKoefs (MainWin.pas:
 * 5073-5168) - designs a Hamming-windowed-sinc lowpass FIR (or disables
 * filtering, matching the original's fallbacks) for the given AY chip
 * clock and output sample rate, and (re)allocates filt_k/filt_xl/filt_xr
 * accordingly. filter_quality mirrors settings.pas's FilterQuality: 0
 * selects the averager-only path (matches original's "RBResamAvg" GUI
 * choice), nonzero selects FIR (matches "RBResamFIR"). Frees any
 * previously-allocated filt_k/filt_xl/filt_xr first, so this is safe to
 * call again on a sample-rate or AY-clock change, exactly as the original
 * calls SetFilter/CalcFiltKoefs from Set_Chip_Frq/Set_Sample_Rate. */
void ay_engine_set_filter(ay_engine* e, int filter_quality, int ay_freq,
                          int sample_rate);

/* Frees filt_k/filt_xl/filt_xr. Call before an ay_engine goes out of scope
 * if ay_engine_set_filter was ever called on it. */
void ay_engine_free_filter(ay_engine* e);

/* Applies a 16-bit sample through the FIR filter (AY.pas: ApplyFilter,
 * cpu64/non-asm fallback path - the only path live in any buildable
 * configuration of the original, see tests/zexall/FIDELITY_GATE.md for the
 * analogous finding on the Z80 core). */
int ay_apply_filter(int lev, int* filt_x, const int* filt_k, int filt_m, int* filt_i);

/* AY.pas: SynthesizerAY - the cadence function Z80-driven playback
 * (Players.pas's MakeBufferAY) calls from its port-write handlers and once
 * per frame rollover. Takes the current cycle count explicitly rather than
 * reaching into a Z80-core global (a deliberate adapter-layer cleanup over
 * the original's direct global-variable coupling - see the approved plan). */
void ay_synthesizer_ay(ay_engine* e, int64_t current_tact);

/* AY.pas: Synthesizer_Stereo16 / Synthesizer_Mono16 / Synthesizer_Stereo8 /
 * Synthesizer_Mono8. e->buf is written as int16_t pairs/singles for the
 * 16-bit variants, uint8_t pairs/singles (unsigned, 128-centered) for the
 * 8-bit variants - matches AY.pas's PS16/PM16/PS8/PM8 pointer casts over
 * the same untyped BufP. */
void ay_synthesizer_stereo16(ay_engine* e);
void ay_synthesizer_mono16(ay_engine* e);
void ay_synthesizer_stereo8(ay_engine* e);
void ay_synthesizer_mono8(ay_engine* e);

#endif /* AY_ENGINE_AY_H */
