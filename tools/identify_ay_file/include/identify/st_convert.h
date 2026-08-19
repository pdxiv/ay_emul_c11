/* Recompiler-style converters that turn an older tracker file layout
 * into a newer, already-ported one, so identify_ay_file can confirm the
 * match via a real IntegrityCheck step by reusing engine/'s existing,
 * oracle-validated loader for the newer format, exactly the way
 * LoadTrackerModule itself does (Players.pas:2547-2558):
 *   FT.ST1 -> ST12STC -> plays as FT.STC
 *   FT.ST3 -> ST32STC -> plays as FT.STC
 *   FT.STF -> STFDepack, then STF2STP -> plays as FT.STP
 *
 * IMPORTANT ported-but-unvalidated-by-oracle caveat: there are zero real
 * .st1/.st3/.stf sample files anywhere in this repo's test corpus, so
 * unlike every other format ported in this project, this trio has NOT
 * been checked byte-for-byte against a real file run through the
 * original Pascal program. Validated instead by: (a) hand-tracing the
 * Pascal literally into C, (b) cross-checking the produced STC/STP-layout
 * byte meanings against engine/'s already-validated
 * STC_Get_Registers/STP_Get_Registers ports and Players.pas's own
 * FoundSTC/FoundSTP, and (c) round-tripping hand-constructed synthetic
 * ST1/ST3/STF files through their existing, oracle-cross-checked
 * structural detectors (detect_st1_structural/detect_st3_structural/
 * detect_stf_structural) -> the converters here -> stc_file_load/
 * stp_file_load, confirming a plausible, hand-computed nonzero
 * global_tick_max (not just "nonzero"). Treat as translated/
 * behaviorally-plausible, not independently validated - see
 * migration_debt.yaml. */
#ifndef IDENTIFY_ST_CONVERT_H
#define IDENTIFY_ST_CONVERT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Players.pas:1766-2049 ST12STC and Players.pas:2051-2216 ST32STC.
 * `data`/`size` are the candidate window bytes (the same "windowed view
 * starting at the candidate offset" convention used by every detect_*
 * function's filebuf, just passed as a raw pointer+length pair here,
 * matching integrity_check.h's other functions). On success, `out`
 * (caller-owned, 65536 bytes) is filled with the produced STC-layout
 * module and `*out_size` is set to its length (Pascal's ST_Size/`n`'s
 * final value) - i.e. exactly what stc_file_load(out, *out_size, ...)
 * expects. Note Pascal's own signature takes an explicit `msize`
 * parameter that FoundST1/FoundST3 always pass as the exact
 * structurally-derived module length, not the raw remaining-window
 * size - both functions re-derive (or, for ST3, prove equivalent to
 * using the raw window size directly - see st_convert.c) that length
 * internally here, since our C signature only gets the raw window. */
bool st1_to_stc(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size);
bool st3_to_stc(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size);

/* Players.pas:1346-1764 STF2STP, operating on an ALREADY-DEPACKED STF
 * buffer (LoadTrackerModule always runs STFDepack immediately before
 * STF2STP - Players.pas:2555-2558). This wrapper does both steps: it
 * first depacks `data`/`size` via stf_depack_all (identify/detect_stf.h)
 * and then runs the STF2STP port on the depacked bytes, producing a
 * buffer in STP's own on-disk layout (ModTypes variant 4) that
 * stp_file_load can already parse. Returns false on any structural
 * violation the original Pascal would also have bailed out on (every
 * Exit(False) site), or if depacking itself fails. `out` must be a
 * caller-owned 65536-byte buffer; `*out_size` receives the number of
 * valid bytes written to it on success. */
bool stf_to_stp(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size);

#endif /* IDENTIFY_ST_CONVERT_H */
