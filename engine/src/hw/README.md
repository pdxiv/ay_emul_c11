# engine/src/hw/

Hardware/chip-emulation layer: the AY-3-8910/YM2149 sound chip core, the Atari ST's MFP68901, DMA sound and 68000 scheduling loop, plus the two adapter modules that bind the third-party Z80 and 68000 CPU cores (`engine/third_party/z80`, `engine/third_party/musashi`) into ay_emul's memory/port/interrupt model. Together these files reproduce the chip- and bus-level behavior `ay_emul/AY.pas`, `ay_emul/atari.pas`, and `ay_emul/Z80.pas` implement in Pascal.

## ay.c

The AY-3-8910/YM2149 sound chip core: register file, tone/noise/envelope generators, the stereo/mono 16-bit and 8-bit mixer and output-buffer synthesizer paths, the Hamming-windowed-sinc FIR filter, and Turbosound (dual-chip, `ts_mode`) mixing.

Ported from: ay_emul/AY.pas (`TSoundChip.Synthesizer_Logic_Q`, `Synthesizer_Mixer_Q`/`Synthesizer_Mixer_Q_Mono`, `Case_EnvType_*`, `SetAYRegister`/`SetAYRegisterFast`, `SetMixerRegister`/`SetEnvelopeRegister`/`SetAmplA`/`B`/`C`, `Calculate_Level_Tables`, `ApplyFilter`, `SynthesizerAY`, `Synthesizer_Stereo16`/`Stereo8`/`Mono16`/`Mono8`, `FillVis`, `AYVisualisation`'s Calc block). One area remains open migration debt: the alternate DAC amplitude tables (MIG-0004). Dual-chip mixing (MIG-0109) and the visualizer's second-chip register snapshot (MIG-0110) are both closed and validated.

## atari_emulate.c

The Atari ST scheduling loop that drives the Musashi 68000 core forward in cycle-budgeted bursts, computes the next MFP-timer/VBL/DMA-sound deadline to clamp each burst to, asserts and acknowledges IPL4 (VBL) and IPL6 (MFP) interrupts, and reentrantly flushes pending AY register writes into `ay.c`'s chip core mid-instruction-stream when a render buffer fills. It also owns the memory-mapped I/O region wiring for the MFP ($FFFA00-$FFFA2F), DMA sound ($FF8900-$FF89FF), and YM2149 ($FF8800-$FF88FF) callback windows.

Ported from: ay_emul/atari.pas (`Atari_Emulate`, `soundchip_writebyte`/`soundchip_readbyte`). Several cycle-accounting and interrupt-scheduling bugs found while chasing WAV-output divergence against the Pascal oracle are documented and fixed in code comments (MIG-0046 through MIG-0056 in migration_debt.yaml); MIG-0017 (seek/scrub support, `Atari_Emulate_One_VBL`/`Atari_SeekTo`) remains open, deliberately out of scope as forward-playback-only.

## mfp.c

Emulates the MC68901 MFP's four programmable timers (A/B/C/D), their delay-mode/data-register semantics, interrupt enable/mask/pending register bank, and the shared level-6 interrupt request/coalesce/retry mechanism (including the `ICnt` backlog counter).

Ported from: ay_emul/atari.pas (`MFP_Registers`/`TMFP_DelayTimer`, `CalcTimerCnt`, `SetTimerDataRegister`, `SetTimerDelayMode`, `SetMFPRegister`, `mfp_writebyte`/`mfp_readbyte`, `EmulateTimer`). A timeslice-release fix and a stale-IRQ-snapshot fix found during this work are recorded under MIG-0054, which remains open pending a residual, much smaller sustained clock-rate drift against the oracle.

## dma_sound.c

Emulates the Atari ST's DMA sound chip: control/mode/start/end registers, the microwire shift-register interface, the live sample-position counter used for register readback, and the mono/stereo sample-mixing tap into the AY output stream.

Ported from: ay_emul/atari.pas (`Ctrl_DMASnd`, `CalcDmaSndCounter`, `stedac_readbyte`, `stedac_writebyte`, the DMA-boundary clamp and `Atari_MixDMASnd` call inside `Atari_Emulate`). The reentrant SynthesizerSNDH flush and IntFlag bookkeeping that the original performs directly inside its $FF8901 control-register write handler are left to the caller (render-loop orchestration, out of scope for this module - see the file's own comments).

## m68k_bus.c

Not a port of Pascal logic — new adapter code binding the third-party Musashi 68000 core (`engine/third_party/musashi`) to ay_emul's memory/interrupt model, playing the role `ay_emul/Starcpu.inc` and `ay_emul/mc68000.pas` fill in the original. It implements Musashi's fixed-name, link-time memory-access hooks (`m68k_read_memory_8/16/32`, `m68k_write_memory_8/16/32`) against an address-ranged region table (flat buffers or read/write callback pairs), an interrupt-acknowledge trampoline, and an opt-in per-opcode timing-override hook that corrects a handful of instruction patterns where Musashi's cycle cost diverges from Starscream's (the original's own CPU core), used to narrow a residual clock-rate drift against the Pascal oracle (MIG-0056, still open).

Faithfully matched hardware/software conventions: real 68000 big-endian byte-order bus semantics for word/long access construction from byte accesses (sidestepping the original's host-native-word `IntelizeMemory` pre-swap trick); level-triggered IPL4/IPL6 interrupt lines and by-level-number (not IPL-mask) priority ordering, matching what `Starcpu32.asm`'s `flush_interrupts` actually does; and Musashi's requirement that the caller (not the core itself) lower an IRQ line after acknowledgment, since Musashi does not auto-clear `CPU_INT_LEVEL` once a custom `int_ack` callback is registered.

## z80_bus.c

Not a port of Pascal logic in the traditional sense — new adapter code binding the third-party superzazu/z80 core (`engine/third_party/z80`) to ay_emul's memory/port model, but the port-decode protocols and interrupt-timing rules it implements are transcribed line-for-line from `ay_emul/Z80.pas`'s live code path. It reconstructs the full 16-bit I/O port (superzazu/z80's port callbacks only expose the low 8 bits) from the CPU's B or A register depending on which opcode form is executing, implements the ZX-Spectrum and Amstrad-CPC AY port-decode protocols with mid-stream auto-detection between them, and drives the once-per-frame maskable-interrupt acceptance window.

Ported from: ay_emul/Z80.pas (live `{$else Z80Emu_noASM}` branch: `ZXInProc`/`ZXOutProc`, `CPCInProc`/`CPCOutProc`/`CPCCheckPIO`, `InitialInProc`/`InitialOutProc`). One deliberate, documented divergence: superzazu/z80 charges the standard Zilog-documented 13/19 T-states for IM1/IM2 interrupt acceptance, while Z80.pas's live code charges 12/18 - per explicit user direction this is treated as a genuine bug in the Pascal original, not replicated (MIG-0018). `OutZXConverter`/`OutCPCConverter` (register-write export recording) are not ported, tracked as MIG-0010.
