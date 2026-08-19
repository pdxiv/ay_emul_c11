/* MIG-0010: C11 port of ay_emul/Convs.pas's VTX_Converter (897-1064),
 * the VBL2VTX branch only (958-969) - same scope note as
 * engine/include/ay_engine/psg_export.h (the sibling PSG exporter):
 * reaches the 14 Turbosound-pairing-eligible tracker formats via
 * player_step_registers, not OUT2VTX/ZXAY2VTX/EPSG2VTX (FT.OUT/FT.ZXAY/
 * FT.EPSG input, out of scope - see migration_debt.yaml).
 *
 * Unlike PSG, VTX bodies are LZH ("-lh5-") compressed - kept OUT of
 * engine/libayengine.a (which has no third-party compression
 * dependency) and built here instead, linking engine/third_party/
 * lhassa's lh_compress_lh5 directly (see this project's own
 * investigation in migration_debt.yaml for why a vendored encoder was
 * used instead of hand-porting lh5.pas's own Encode_Buffer_To_File:
 * compression has no single "byte-exact" target the way playback does,
 * so a well-tested reference implementation is the right call here,
 * the same policy already applied to Musashi/superzazu-z80 for CPU
 * timing). Round-trip-validated against this port's OWN, already-
 * oracle-validated lh5_decompress before being wired in for real. */
#ifndef AY_EXPORT_VTX_EXPORT_H
#define AY_EXPORT_VTX_EXPORT_H

#include <stdbool.h>

#include "ay_engine/player.h"

/* Writes a .vtx export of `p` to `path`. `ay_freq` is the AY/YM chip
 * clock in Hz for the VTX header's own ChipFrq field - pass 0 to use
 * PLAYER_AY_FREQ_DEF (the standard clock every format defaults to
 * unless player_set_chip_freq was called; this port has no generic
 * "what clock is this player actually using" accessor - see this
 * function's own migration_debt.yaml entry). `title`/`author`/
 * `programm`/`tracker`/`comment` may each be NULL (written as empty
 * strings, matching a real conversion where the user left the VTX_
 * Header_Editor dialog's fields blank - this port has no generic
 * per-format song-metadata accessor to auto-fill `tracker` the way
 * Convs.pas's GetEditorString(CurFileType) does). `loop_vbl` is the
 * VTX header's own Loop field (VBL tick the player should seek back to
 * on natural end) - pass 0 for a non-looping export (the common case
 * for a tracker-sourced file, which has no native loop-point concept
 * this port threads through generically either). Returns false if `p`'s
 * format doesn't support player_step_registers, or on a write/
 * compression error. */
bool vtx_export_write(const char* path, player* p, int ay_freq,
                       const char* title, const char* author,
                       const char* programm, const char* tracker,
                       const char* comment, int loop_vbl);

/* Same, for a player_pair with TSMode active - writes ONE file (`path`)
 * containing the PRIMARY voice's own register stream. Convs.pas's
 * VTX_Converter itself has NO TSMode branch at all (confirmed by direct
 * reading of Convs.pas:897-1064 - unlike PSG_Converter, which does) -
 * VTX's own container format has no second-chip concept, so a real
 * Turbosound-paired VTX export in the original is genuinely just the
 * primary voice, matching this function's own behavior exactly (not a
 * simplification this port introduced). */
bool vtx_export_write_pair(const char* path, player_pair* pair, int ay_freq,
                            const char* title, const char* author,
                            const char* programm, const char* tracker,
                            const char* comment, int loop_vbl);

/* Oracle-diff support only (tests/oracle_diff/dump_engine_state.c) -
 * writes the raw, PRE-COMPRESSION column-major register buffer
 * (VTX_Save_Registers' own `p` array, VBL2VTX's real output before
 * Encode_Buffer_To_File ever runs) to `path` with no header/strings/
 * compression at all. This is the actual byte-exact validation target
 * for VTX export: the COMPRESSED bytes are not expected to match real
 * Pascal's own lh5.pas encoder output (compression has no single
 * "correct" byte-level result for a given input, unlike playback - see
 * migration_debt.yaml's own LhASsA investigation, the same reasoning
 * already applied to Musashi/superzazu-z80 for CPU timing), but the
 * register DATA feeding into compression absolutely does have one
 * correct answer, and is what this function isolates for comparison. */
bool vtx_export_debug_write_raw_regs(const char* path, player* p);

#endif /* AY_EXPORT_VTX_EXPORT_H */
