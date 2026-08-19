# engine/include/ay_engine/util/

Small standalone utility headers used by the format loaders and hardware core: a compression codec needed to unpack certain file formats, and an optional diagnostic tracing facility used to cross-check this port's behavior against the original Pascal oracle.

## lh5.h

Declares `lh5_decompress`, a single whole-buffer-to-whole-buffer LZH/LHA "-lh5-" decompressor. The original Pascal decoder is a streaming/incremental design built around a handle+reader-callback abstraction, but since its only real caller (the YM/VTX loaders) always requests the entire unpacked size in one call, this port collapses it to a plain one-shot decompress function per the approved porting plan. Decompression only - the original's compression path (`Encode_Buffer_To_File`) is never needed since this project only ever reads files, never writes them.

Ported from: ay_emul/lh5.pas (LZHDepacker/InitLZHDepacker, 26-30 for the streaming API this simplifies away).

## trace_log.h

Declares four opt-in diagnostic logging functions (`trace_log_ay`, `trace_log_irq`, `trace_log_step`, `trace_log_mfp`), each gated by its own environment variable (`AY_ENGINE_AY_TRACE`, `AY_ENGINE_IRQ_TRACE`, `AY_ENGINE_STEP_TRACE`, `AY_ENGINE_MFP_TRACE`) and zero-cost (beyond one cached `getenv()` check) when unset. Used to cross-check this port's AY register writes, VBL/MFP interrupt request/coalesce/service events, atari_emulate_step's cycle-budget accounting, and MFP register writes against the Pascal oracle's own trace output during the Atari/SNDH emulation debugging work (MIG-0045 through MIG-0055). The header notes one deliberate asymmetry: the C11 side can log a "service" event (an interrupt actually dispatched, via Musashi's int_ack callback) that the Pascal side's Starscream 68000 core has no equivalent hook for, so ay_emul/TraceLog.pas can only ever log "assert"/"coalesce" events, never "service" - a real difference between what each side can observe, not a bug in either log.

Ported from: ay_emul/TraceLog.pas (kept as this module's Pascal-side sibling, log line formats intentionally kept in sync for direct diff-tool comparison rather than mechanically transcribed function-by-function).
