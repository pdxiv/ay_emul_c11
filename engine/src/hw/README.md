# engine/src/hw/

Implementations of the emulated hardware layer: the AY/YM sound chip core, the Atari ST scheduling/MFP/DMA-sound subsystem, and the CPU-bus adapters binding the third-party Z80/68000 cores, paired with the headers in `engine/include/ay_engine/hw/`.

## atari_emulate.c

Implements the Atari_Emulate scheduling loop, wiring together the MFP callback region ($FFFA00-$FFFA2F, delegating to `mfp.c`), DMA sound, and the AY mixer to drive the emulated Atari system's timing.

Ported from: ay_emul/atari.pas (Atari_Emulate).

## ay.c

Implements the AY-3-8910/YM2149 chip core: the register file, amplitude tables (AY_AMPLITUDES_AY), synthesis logic/mixer, filtering, and PCM output stage generation for Z80-driven playback.

Ported from: ay_emul/AY.pas.

## dma_sound.c

Implements DMA sound / STE DAC emulation: mono/stereo 8-bit DMA sample playback and the Microwire dummy bit-shifter (`microwire_dummy_shift`), matching the original's confirmed no-op audio effect.

Ported from: ay_emul/atari.pas (Ctrl_DMASnd, stedac_readbyte/writebyte, Atari_MixDMASnd).

## m68k_bus.c

Implements the Musashi adapter: memory-region registration/dispatch and the process-wide "active bus" pointer (`g_active_bus`) that Musashi's fixed-name, link-time memory-access hooks consult.

Ported from: Not a port of Pascal logic — new adapter code binding Musashi to the memory/interrupt conventions of ay_emul/Starcpu.inc, ay_emul/mc68000.pas, and ay_emul/atari.pas.

## mfp.c

Implements MFP68901 timer emulation: the 4 delay-mode timers and their register read/write semantics, plus an optional diagnostic trace (`AY_ENGINE_TIMERA_TRACE` env var) whose log format mirrors `ay_emul/TraceLog.pas`'s TraceLogTimerA for direct diffing against the Pascal oracle.

Ported from: ay_emul/atari.pas (MFP_Registers, TMFP_DelayTimer, EmulateTimer, SetMFPRegister, mfp_readbyte/mfp_writebyte); its diagnostic trace format mirrors ay_emul/TraceLog.pas.

## z80_bus.c

Implements the superzazu/z80 adapter: RAM-backed memory read/write callbacks and the ZX-Spectrum/Amstrad-CPC AY port-decode protocol with mid-stream auto-detect-and-switch.

Ported from: Not a direct port of Pascal logic — new adapter code, but it faithfully reproduces port-decode behavior (PORT_MASK/CPC_PORT_MASK constants and protocol logic) from ay_emul/Z80.pas's live `{$else Z80Emu_noASM}` branch.
