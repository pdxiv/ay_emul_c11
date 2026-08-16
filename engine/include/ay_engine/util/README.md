# engine/include/ay_engine/util/

Public headers for small utility subsystems shared across the engine: LZH decompression and optional diagnostic tracing.

## lh5.h

Declares an LZH/LHA "-lh5-" decompressor, decompression-only (the original's compression path, Encode_Buffer_To_File, is never needed since this project only reads `.ym` files, never writes them). Unlike the original's streaming/incremental handle+reader-callback design, this port is a plain "decompress whole compressed buffer to whole output buffer" function, since the only caller (the YM loader) always requests the entire unpacked size at once.

Ported from: ay_emul/lh5.pas.

## trace_log.h

Declares optional, opt-in diagnostic tracing (AY register writes, IRQ events, step events, MFP events) for cross-checking this port's behavior against the Pascal oracle. Disabled by default (a cached getenv() check) and enabled per-subsystem via environment variables (e.g. `AY_ENGINE_AY_TRACE`); log line formats are kept in sync with the Pascal-side sibling so a diff tool can compare them directly.

Ported from: ay_emul/TraceLog.pas (as a log-format sibling to keep in sync with, not a line-for-line translation — this is new C11 tracing infrastructure).
