# engine/include/ay_engine/hw/

Hardware-emulation layer: the AY-3-8910/YM2149 sound chip, the Atari ST's MFP68901 timer chip and DMA-sound/STE DAC, and the adapters binding the third-party Z80 and 68000 CPU cores into the same memory/interrupt model the original Pascal units used. `atari_emulate.h` ties `m68k_bus.h`, `mfp.h`, `dma_sound.h` and `ay.h` together into the Atari ST scheduling loop; `z80_bus.h` plays the analogous role for Z80-driven (ZX Spectrum/Amstrad CPC) playback.

## atari_emulate.h

Declares `atari_emulate`, which bundles an `m68k_bus`, an `mfp`, a `dma_sound`, a borrowed `ay_engine*`, and the scheduling state (cycle odometer, VBL period/base, IRQ-pending flags, a fixed-size deferred-AY-write queue) needed to drive one Atari ST session. `atari_emulate_step` computes a bounded cycle budget from the nearest VBL/MFP-timer/DMA-sample event, runs the 68000 that far, injects interrupts, and flushes the AY mixer.

Ported from: ay_emul/atari.pas (`Atari_Emulate`, atari.pas:1436-1523, plus `Atari_CheckOuts`/`AddOut`/`ClearOuts`, atari.pas:517-543,1574-1596). Confirmed live: `Atari_Emulate_One_VBL` and `DMASndSkipMC68000Takts` (atari.pas:27,345) are the seek/scrub fast-forward path and are NOT ported here (MIG-0017, open). Two real cycle-accounting divergences from the original's Starscream-based scheduling remain open (MIG-0046), and a ~1.34%/30s Musashi-vs-Starscream clock-drift is documented as still-open residual jitter (MIG-0054, MIG-0056) with an experimental opt-in correction (`atari_emulate_enable_starscream_timing`) that only covers 3 known opcode patterns.

## ay.h

Declares `ay_chip` (mirrors `TSoundChip`'s register file and oscillator/envelope/noise state) and `ay_engine` (mirrors AY.pas's unit-level mixer globals, now for two chips: `chip`/`chip2` plus `ts_mode` for Turbosound/dual-chip mixing), along with the synthesis entry points (`ay_synthesizer_ay`, `ay_synthesizer_stereo16`/`mono16`/`stereo8`/`mono8`, filter setup/apply, and visualizer sampling). `ay_engine.on_mix_dma` is the hook `atari_emulate.h` wires to `dma_sound_mix` so DMA sound gets mixed in before the AY channels, matching `Atari_MixDMASnd`'s call site.

Ported from: ay_emul/AY.pas (`TSoundChip`, `Synthesizer_Logic_Q`, `Synthesizer_Mixer_Q`/`_Mono`, `SetAYRegister`/`SetAYRegisterFast`, `ApplyFilter`, `SynthesizerAY`, `Synthesizer_Stereo16`/`Mono16`/`Stereo8`/`Mono8`). Dual-chip (Turbosound) mixing and 8-bit output, once tracked as open debt, are now closed and validated (MIG-0109, MIG-0009); DMA-sound mixing wiring is closed and validated (MIG-0008). The second chip's own visualizer register snapshot (`ay_vis_point.r[1]`, AY.pas's `TVisPoint.R[1]`) is now ported too (MIG-0110, closing MIG-0109's own GUI-side follow-up).

## dma_sound.h

Declares `dma_sound`, the Atari STE DMA-sound/DAC state (raw `$FF89xx` control/mode/start/end registers, latched playback state, sample-and-hold `prev_l`/`prev_r`), plus register read/write handlers meant to be wired into `m68k_bus.h` as a callback region, a next-boundary-cycles query for the scheduler, and `dma_sound_mix` which fetches/holds a sample and mixes it into a stereo level pair.

Ported from: ay_emul/atari.pas (`Ctrl_DMASnd`, `stedac_readbyte`/`stedac_writebyte`, `Atari_MixDMASnd`, atari.pas:314,784,853,1725). The STE Microwire registers are ported as the same inert dummy bit-shifter the original uses. `DMASndSkipMC68000Takts` (the seek-only fast-forward helper) is confirmed unreached during normal playback and is deliberately not ported, tracked under the same open seek/scrub gap as `atari_emulate.h` (MIG-0017).

## m68k_bus.h

Adapter binding the third-party Musashi 68000 core (`engine/third_party/musashi`) to the address-region/interrupt-acknowledge model `Starcpu.inc` and `mc68000.pas`/`atari.pas` expose to the rest of the emulator: a flat or callback-backed memory-region table (`m68k_bus_add_flat_region`/`m68k_bus_add_callback_region`), exec/reset/IRQ entry points, and an `int_ack` callback mirroring `s68000interrupt(level,vector)`'s vector-acknowledge contract. Musashi's core hooks are fixed-name functions resolved at link time rather than a callback struct, so this adapter keeps one process-wide "active bus" pointer (`m68k_bus_activate`) — noted in the file as a real, if currently harmless, singleton constraint.

This file has no direct Pascal source file of its own — it is an adapter binding the third-party Musashi CPU core to the interface `Starcpu.inc` (assembly, not ported) and `atari.pas`/`mc68000.pas` expect, so there is no `Ported from:` Pascal equivalent beyond that interface contract.

## mfp.h

Declares `mfp_timer` (one of the MFP68901's four delay-mode timers: enable/mask, prescaler, expanded data register, live countdown, and a retry counter) and `mfp` (24-byte register file, the four timers, and the single shared level-6 IRQ-pending slot), plus a callback-region adapter (`mfp_bus_context`/`mfp_bus_read`/`mfp_bus_write`) for `m68k_bus.h`, and `mfp_emulate_timer`/`mfp_ack_interrupt` for advancing/servicing timers.

Ported from: ay_emul/atari.pas (`MFP_Registers`, `TMFP_DelayTimer`, `EmulateTimer`, `SetMFPRegister`, `mfp_readbyte`/`mfp_writebyte`, atari.pas:111-160,356-489,892-922,1383-1431). Only delay-mode timers are ported, matching the original (event-count/pulse mode is never implemented there either). The header's own extensive comments document two real, since-fixed bugs found porting this against real SNDH tunes: a missing shared-level-6-slot retry mechanism (the level's single pending-request slot is shared by all four timers, not one per timer) and a missing timeslice-release on scheduling-relevant register writes — both now closed and validated.

## z80_bus.h

Adapter binding the third-party superzazu/z80 core (`engine/third_party/z80`) to the memory/port model actually exercised by `Z80.pas`'s live (non-assembly) branch: flat 64K RAM, the ZX Spectrum and Amstrad CPC AY port-decode protocols including their mid-stream auto-detect/switch, and the once-per-frame maskable-interrupt acceptance window. Declares `z80_bus` and callback typedefs for chip-frequency change, AY register read/write, and beeper-edge notification.

This file has no single Pascal source unit of its own — it is an adapter binding the third-party superzazu/z80 CPU core to the port-decode and interrupt-timing behavior of ay_emul/Z80.pas's live `{$else Z80Emu_noASM}` branch (Z80.pas:10854-22052; the `{$ifdef Z80Emu_ASM}` branch above it is dead code per `tests/zexall/FIDELITY_GATE.md`), so `Ported from:` here means that behavioral contract rather than a 1:1 file. `OutZXConverter`/`OutCPCConverter` (register-write export recording, Z80.pas:11141-11183) are deliberately not ported (MIG-0010, open); IM0 is not distinguished from IM1 since the original's live code never distinguishes them either (MIG-0001).
