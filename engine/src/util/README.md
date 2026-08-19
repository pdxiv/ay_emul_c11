# engine/src/util/

Small, self-contained utility modules used by the format loaders and hardware core: an LZH decompressor needed to unpack `-lh5-`-wrapped YM files, and an opt-in diagnostic tracer used to cross-check this port's behavior cycle-for-cycle against the Pascal oracle.

## lh5.c

Decompresses a whole LZH/LHA "-lh5-" compressed buffer into a caller-supplied output buffer of the exact expected size, via `lh5_decompress`. Implements the classic LZSS+dynamic-Huffman scheme: `make_table` builds canonical Huffman decode tables (a flat lookup for short codes, an index-based binary-tree walk through `left[]`/`right[]` for longer ones), `read_pt_len`/`read_c_len` parse the per-block code-length tables, and `decode_c`/`decode_p`/`decode_buffer` decode literals and length/distance-coded back-references into an 8192-byte ring buffer (`DICSIZ`). Unlike the Pascal original's streaming, callback-driven decoder (built around a handle+reader abstraction for incremental reads), this port is a plain whole-buffer-in/whole-buffer-out function since the only caller (the YM loader) always requests the full unpacked size in one call.

Ported from: ay_emul/lh5.pas (`LZHDepacker`/`InitLZHDepacker` collapsed into `lh5_decompress`; `MakeTable`, `ReadPtLen`, `ReadCLen`, `DecodeC`, `DecodeP`, `DecodeBuffer` ported 1:1 by name). The compression path (`Encode_Buffer_To_File`) is not ported, since this project only ever reads `.ym` files.

## trace_log.c

Provides four zero-cost-when-disabled tracing functions (`trace_log_ay`, `trace_log_irq`, `trace_log_step`, `trace_log_mfp`) that each lazily open a line-buffered log file from an environment variable (`AY_ENGINE_AY_TRACE`, `AY_ENGINE_IRQ_TRACE`, `AY_ENGINE_STEP_TRACE`, `AY_ENGINE_MFP_TRACE`) on first use and write one text line per event, cycle-count-timestamped rather than wall-clock, so a diff tool can line them up against the Pascal-side oracle's own trace output. `trace_log_step` has no Pascal counterpart (a C11-only exec-budget diagnostic for the Musashi 68000 core), and `trace_log_irq`'s "service" event has no Pascal equivalent either, since Starscream (the Pascal side's 68000 core) exposes no interrupt-dispatch callback and so `TraceLog.pas` can only log request-time "assert"/"coalesce" events.

Ported from: ay_emul/TraceLog.pas (`TraceLogAY` -> `trace_log_ay`, `TraceLogIRQ` -> `trace_log_irq`; `TraceLogTimerA` is the closest Pascal sibling of `trace_log_mfp`, generalized in this port to the full MFP register set rather than just Timer A).
