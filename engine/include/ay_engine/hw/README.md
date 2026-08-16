# engine/include/ay_engine/hw/

Public headers for the emulated hardware layer: the AY/YM sound chip core, the Atari ST scheduling/MFP/DMA-sound subsystem, and the CPU-bus adapters binding the third-party Z80/68000 cores into that hardware model.

## atari_emulate.h

Declares the Atari_Emulate scheduling loop that ties `m68k_bus.h` (Musashi), `mfp.h`, `dma_sound.h`, and `ay.h` together — VBL/MFP/DMA cycle-budget clamping, interrupt injection, and dispatch into the AY mixer. Scope for this milestone is the scheduling logic itself, proven via synthetic 68000 programs; not wired to a real render-loop caller (MakeBufferSNDH) or SNDH file loading.

Ported from: ay_emul/atari.pas (Atari_Emulate, atari.pas:1436-1523).

## ay.h

Declares the AY-3-8910/YM2149 sound chip emulation core: the chip register file, Synthesizer_Logic_Q/Synthesizer_Mixer_Q(_Mono), ApplyFilter, SynthesizerAY, and stereo16/mono16 PCM output stages. Scope is limited to the Z80-driven playback path (MakeBufferAY -> SynthesizerAY); the other Synthesizer* cadence variants (ZX50/OUT/YM6/EPSG/ZXAY) belong to the format-parser files instead. Turbosound (dual-chip / TSMode) is deliberately not ported in this milestone (see migration_debt.yaml).

Ported from: ay_emul/AY.pas (TSoundChip and related synthesis routines).

## dma_sound.h

Declares DMA sound / STE DAC emulation exposed as an `m68k_bus.h` callback region at $FF8900-$FF89FF: plain mono/stereo 8-bit DMA sample playback and the STE Microwire dummy bit-shifter. DMASndSkipMC68000Takts is not ported (confirmed dead outside of seek/scrub support, which is separately deferred).

Ported from: ay_emul/atari.pas (Ctrl_DMASnd, stedac_readbyte/writebyte, Atari_MixDMASnd).

## m68k_bus.h

Adapter binding Musashi (`engine/third_party/musashi`) to the memory/interrupt model `ay_emul/Starcpu.inc` + `mc68000.pas`/`atari.pas` expose, mirroring `z80_bus.h`'s role for the Z80 core. Because Musashi resolves its memory-access hooks at link time as fixed-name functions (not a runtime callback struct), this adapter holds one process-wide "active bus" pointer activated via `m68k_bus_activate()`. Scope for this milestone is the bare 68000 core plus a generic memory-region mechanism; MFP timers, DMA sound, and YM2149 region wiring are separate files.

Ported from: Not a port of Pascal logic — it is a new adapter binding the third-party Musashi 68000 core to the memory/interrupt conventions described by ay_emul/Starcpu.inc, ay_emul/mc68000.pas, and ay_emul/atari.pas.

## mfp.h

Declares MFP68901 timer emulation exposed as an `m68k_bus.h` callback region at $FFFA00-$FFFA2F: the 4 delay-mode timers (A-D) driving SNDH playback timing and the register read/write semantics real tunes exercise. Event-count/pulse (AER-driven) mode is not ported, matching the original (which only implements delay mode).

Ported from: ay_emul/atari.pas (MFP_Registers, TMFP_DelayTimer, EmulateTimer, SetMFPRegister, mfp_readbyte/mfp_writebyte).

## z80_bus.h

Adapter binding superzazu/z80 (`engine/third_party/z80`) to the memory/port model `ay_emul/Z80.pas`'s live (non-ASM) code exercises: flat 64K memory, ZX-Spectrum-style and Amstrad-CPC-style AY port-decode protocols with mid-stream auto-detect-and-switch, and the once-per-frame maskable-interrupt acceptance window.

Ported from: Not a direct port of Pascal logic — it is a new adapter, but it faithfully reproduces behavior from ay_emul/Z80.pas's live `{$else Z80Emu_noASM}` branch (the `{$ifdef Z80Emu_ASM}` branch is confirmed dead code and not replicated).
