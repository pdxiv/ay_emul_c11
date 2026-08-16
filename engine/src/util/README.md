# engine/src/util/

Implementations of small utility subsystems shared across the engine: LZH decompression and optional diagnostic tracing, paired with the headers in `engine/include/ay_engine/util/`.

## lh5.c

Implements the LZH/LHA "-lh5-" decompressor: the bit-buffer reader, Huffman/dictionary decode tables (DICBIT/DICSIZ/MATCHBIT/MAXMATCH/THRESHOLD constants), and whole-buffer-to-whole-buffer decompression used by the YM file loader.

Ported from: ay_emul/lh5.pas (decompression path only; the compression path, Encode_Buffer_To_File, is not ported since it is never exercised).

## trace_log.c

Implements the opt-in diagnostic loggers: lazily opens per-subsystem log files (AY register writes, IRQ, step, MFP) the first time each is used, gated by environment variables, and writes log lines whose format mirrors the Pascal-side TraceLog.pas sibling.

Ported from: ay_emul/TraceLog.pas (as a log-format sibling — new C11 infrastructure, not a translation of Pascal logic).
