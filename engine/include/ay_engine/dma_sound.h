/* C11 port of ay_emul/atari.pas's DMA sound / STE DAC emulation
 * (Ctrl_DMASnd, stedac_readbyte/writebyte, Atari_MixDMASnd, atari.pas:
 * 67-72,157-160,286-314,693-891,1630-1693). Exposed as an
 * engine/m68k_bus.h callback region at $FF8900-$FF89FF.
 *
 * Scope for this milestone (see migration_debt.yaml): plain mono/stereo
 * 8-bit DMA sample playback, which is what SNDH content actually uses.
 * STE Microwire ($FF8922-25) is ported as the same dummy bit-shifter the
 * original uses - confirmed to have zero effect on the audio path in the
 * original either, so this isn't a simplification relative to it.
 * DMASndSkipMC68000Takts (atari.pas:317-354) is NOT ported - confirmed
 * (this milestone's exploration) it is called only when Seeking is true
 * (Atari_SeekTo/SynthesizerSNDH's seek branch), never during normal
 * forward playback - it belongs with the already-deferred seek/scrub
 * support (see migration_debt.yaml).
 *
 * Ctrl_DMASnd's original also triggers a REENTRANT SynthesizerSNDH call
 * and IntFlag/BufferLength bookkeeping directly inside the $FF8901 write
 * handler (atari.pas:817-828) - that's render-loop orchestration this
 * milestone deliberately doesn't wire up yet (see engine/atari_emulate.h);
 * dma_sound_write_byte_at only performs the state-machine transition
 * itself (matching Ctrl_DMASnd proper), not the reentrant flush. */
#ifndef AY_ENGINE_DMA_SOUND_H
#define AY_ENGINE_DMA_SOUND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dma_sound {
  /* Raw hardware registers (atari.pas: DMASnd_Ctrl/Mode/Start/End). */
  uint8_t ctrl, mode;
  uint32_t start, end;
  uint16_t microwire_mask, microwire_data;
  int microwire_shift;

  /* Latched playback state (atari.pas: DMASnd_Play/PLoop/PMono/PRate/
   * PStart/PEnd/PPos/PBase/PCurr) - only updated on a stopped->playing
   * edge, per Ctrl_DMASnd. PBase/PCurr are cycle-domain (matches the
   * original's `double`, wide enough for MC68000Freq-scale cycle counts
   * without the original's odometer-overflow renormalization concern -
   * see engine/m68k_bus.h and migration_debt.yaml). */
  bool play, ploop, pmono;
  uint8_t prate;
  uint32_t pstart, pend;
  int32_t ppos;
  double pbase, pcurr;

  /* atari.pas: prevl/prevr - last-fetched sample, held between DMA
   * sample boundaries (Atari_MixDMASnd's sample-and-hold behavior). */
  int prev_l, prev_r;
} dma_sound;

void dma_sound_init(dma_sound* d);

/* Callback-region handlers matching stedac_readbyte/writebyte's address
 * dispatch (exact $FF89xx addresses, not a division like MFP's). `od` is
 * the current cycle count, needed for the Ctrl-register playback-start
 * edge and the live position-counter read - see mfp.h for why this is
 * passed explicitly rather than read from a global. */
uint8_t dma_sound_read_byte_at(const dma_sound* d, uint32_t address,
                                int64_t od);
void dma_sound_write_byte_at(dma_sound* d, uint32_t address, uint8_t value,
                              int64_t od);

/* Like dma_sound_read_byte_at, but also handles the three registers whose
 * value depends on mc68000_freq (the live $FF8909/0B/0D position counter,
 * CalcDmaSndCounter) and the Microwire dummy-shift-on-read registers
 * ($FF8922/24) - split out because those need one more parameter than the
 * rest of the register file. This is the real callback-region entry
 * point; dma_sound_read_byte_at alone is for callers (e.g. tests) that
 * only care about the plain registers. */
uint8_t dma_sound_read_counter_byte_at(dma_sound* d, uint32_t address,
                                        double mc68000_freq, int64_t od);

/* Matches Atari_Emulate's DMA-boundary cycle-budget clamp
 * (atari.pas:1468-1481): returns the cycle count until the next DMA sample
 * boundary if DMA is playing, or -1 if not (caller should not clamp). */
int64_t dma_sound_next_boundary_cycles(const dma_sound* d,
                                        double mc68000_freq);

/* Matches Atari_MixDMASnd (atari.pas:1630-1693): fetches/holds a sample
 * from `mem` (a flat 68000 RAM buffer of `mem_size` bytes - plain
 * big-endian bytes, no byte-swap compensation needed, see m68k_bus.c's
 * file comment) and adds it, scaled by `atari_dma_level` (matches AY.pas's
 * Atari_DMALevel), into *lev_l and *lev_r. Advances the internal cycle cursor
 * by mc68000_freq*8/ay_freq per call, matching the original's per-mixer-
 * sample step. */
void dma_sound_mix(dma_sound* d, const uint8_t* mem, uint32_t mem_size,
                    int atari_dma_level, double mc68000_freq, double ay_freq,
                    int* lev_l, int* lev_r);

#endif /* AY_ENGINE_DMA_SOUND_H */
