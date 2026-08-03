/* C11 port of ay_emul/lh5.pas's LZH/LHA "-lh5-" decompressor
 * (decompression only - the compression path, Encode_Buffer_To_File, is
 * never needed since this project only ever reads .ym files, never
 * writes them).
 *
 * The original is a streaming/incremental decoder built around a
 * handle+reader-callback abstraction (LZHDepacker/InitLZHDepacker,
 * lh5.pas:26-30), but Players.pas's only caller (the YM loader,
 * Players.pas:2775) always requests the entire unpacked size in one
 * UniRead call - so this port is a plain "decompress whole compressed
 * buffer to whole output buffer" function, per the approved plan.
 */
#ifndef AY_ENGINE_LH5_H
#define AY_ENGINE_LH5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decompresses `comp`/`comp_size` (the "-lh5-"-compressed payload,
 * i.e. TLZHFileHeader.CompSize bytes starting at HSize+2 - the header
 * itself is NOT part of `comp`) into `out`, which must be exactly
 * `out_size` bytes (TLZHFileHeader.UCompSize). Returns false if the
 * compressed stream is malformed (mirrors lh5.pas's InvalidLZH
 * exception) or requests more output than out_size can hold. */
bool lh5_decompress(const uint8_t* comp, int32_t comp_size, uint8_t* out,
                     int32_t out_size);

#endif /* AY_ENGINE_LH5_H */
