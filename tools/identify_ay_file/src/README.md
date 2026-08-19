# src/

Implementation of `identify_ay_file`'s file-loading, per-format detectors,
top-level dispatch, `IntegrityCheck` confirmation, and CLI entry point. Kept
one file per format family (each under 600 lines) so every detector's
top-of-file comment can cite the exact `Ay_Emul.fmt`/`Players.pas` source it
ports without competing for space; see `../identify_ay_file.md` for the
overall Tier A/B/C design and `../include/identify/` for the corresponding
headers.

## bufio.c

Implements `read_whole_file` (loads a file into a `filebuf`, capped at
`MAX_READ_SIZE`) and the bounds-checked byte-access helpers (`has_at`,
`str_at`, `in_bounds`, `byte_at`, `be16_at`, `le16_at`, `find_bytes`)
every detector calls. Original C infrastructure, not a port of one Pascal
routine - `le16_at`'s comment documents the native-endian (little-endian)
convention used for on-disk x86 Pascal `word` pointer fields.

## detect_container.c

Implements `detect_ay_container` (ZXAY + TypeID: `EMUL`→AY, `AMAD`→FXM,
`ST11`→ST1 - note `AMAD`/`ST11` are reassigned to FXM/ST1, not reported as
distinct AY subtypes, per `OpenAYFile`'s real dispatch, confirmed by
oracle-diff), `detect_aym` (`AYM0` header, `Rev` must be `'0'`), and
`detect_psg` (`PSG`/`EPSG` register dumps - `EPSG` is always reported as
`format=PSG;subtype=EPSG` since the real dispatcher never selects
`Ay_Emul.fmt`'s separately-named `EPSG` format at runtime).

Ported from: ay_emul/Players.pas (`OpenAYFile` 7132-7236 incl. the
AMAD/ST11 TypeID branch at 7192-7230, `TAYFileHeader` 220-230,
`OpenAYMFile` 7026-7038, `TAYMFileHeader` 245-254, `AddFile`'s PSG/EPSG
dispatch quirk at 8130). Migration debt: `MIG-0024` (validated - this
AMAD/ST11 reassignment was a real bug found and fixed via oracle-diff
against the real binary).

## detect_fls.c

Implements `detect_fls_structural`: validates the FLS pointer-table
layout (positions/ornaments/samples pointers, per-sample loop parameters,
pattern address walk) via `ValidSamOffset`/`ValidSamParams`-equivalent
helpers, minus the final `IntegrityCheck` step.

Ported from: ay_emul/Players.pas (`FoundFLS`, current variant, 6332-6544;
`ValidSamOffset`/`ValidSamParams` 6339-6371; `ModTypes` variant 10,
171-176).

## detect_pt3.c

Implements `detect_pt3` (the `Ay_Emul.fmt` text-signature check for
"ProTracker 3."/"Vortex Tracker II" stamps, plus version digit,
Turbosound marker, and tone-table id extraction), `detect_pt3_structural`
(the real pointer/table `Module_Detector` port, tried with both a
loader-relative and absolute pointer base per Pascal's two back-to-back
calls), and `scan_whole_file_for_pt3` (a signature-substring fallback,
retained for reference/tests but no longer called by dispatch).

Ported from: ay_emul/Players.pas (`FoundPT3(DetectAdr)` 5880-5967;
version digit 2580-2581; Turbosound marker 2662-2665; `ModTypes` variant
6, 136-144); ay_emul/Ay_Emul.fmt (`[format+ PT3]`).

## detect_pt_asc_family.c

Implements `detect_asc1_structural`, `detect_asc0_structural`,
`detect_pt2_structural`, `detect_pt1_structural`, `detect_sqt_structural`,
and `detect_psc_structural(psc1_00)` - each a direct pointer/table-walk
port of its `FoundXXX` counterpart, validating position/pattern/sample/
ornament pointer consistency and table bounds.

Ported from: ay_emul/Players.pas (`FoundASC1` 5299-5352, `FoundASC0`
5354-5406, `FoundPT2` 5785-5878, `FoundPT1` 6262-6330, `FoundSQT`
6182-6260, `FoundPSC(PSC1_00)` 5969-6088; `ModTypes` variants 2/3/5/9/7).

## detect_signature_trackers.c

Implements the six `Ay_Emul.fmt`-signature detectors (`detect_stc`'s ten
alternative compiler-stamp strings, `detect_psc`, `detect_ftc`,
`detect_gtr`, `detect_fxm`, `detect_psm`), the legacy
`scan_whole_file_for_signature_trackers` substring fallback (kept for
reference/tests, not used by dispatch), and the real `Module_Detector`
structural ports `detect_stc_structural`, `detect_gtr_structural`, and
`detect_ftc_structural`. Documents that FXM/PSM are Tier-A-only in the
real program (`Module_Detector` never calls `FoundFXM`/`FoundPSM`, despite
both having `Ay_Emul.fmt` signatures), and that the ST3/STP-style `Id`
secondary tag-string (`KsaId`/`StcId`) confirmation is skipped in the
structural ports.

Ported from: ay_emul/Ay_Emul.fmt (`[format+ STC/PSC/FTC/GTR/FXM/PSM]`);
ay_emul/Players.pas (`FoundSTC` 5067-5173, `FoundGTR` 4841-4920,
`FoundFTC` 6090-6180). Migration debt: `MIG-0023b` (open, `translated` -
documents the substring-search-to-structural-port correction).

## detect_sndh.c

Implements `detect_sndh`: `ICE!`-prefix (compressed) or fixed-offset
`SNDH` magic at offset 12 (after the 12-byte 68000 jump-vector table).
Only the two structural signature checks are performed - the variable-
length tag chain (title/author/song count/replay frequency) is
deliberately not walked here, though it is already ported for playback in
`engine/src/sndh_file.c`.

Ported from: ay_emul/Players.pas (`OpenSNDHFile`, 7065-7080);
ay_emul/Ay_Emul.fmt (`[format+ SNDH]`).

## detect_st_family.c

Implements `detect_st1_structural`, `detect_st3_structural`, and
`detect_stp_structural` - pointer/table validation for ST1's sample/
position/ornament/pattern tables, ST3's positions/samples/ornaments/
patterns pointer chain (with an `Id`-prefixed-variant branch), and STP's
pointer layout (with an `Init_Id=0` branch). All three skip the
`KsaId`/`StcId` secondary tag-string confirmation Pascal performs in
addition to the pointer checks, and all three skip the final
`IntegrityCheck` step at this layer (performed later in `dispatch.c`).

Ported from: ay_emul/Players.pas (`FoundST1` 4922-5065, `FoundST3`
5175-5297, `FoundSTP` 5555-5624; `ModTypes` variants 14/16/4). Migration
debt: `MIG-0105` (open, `behaviorally_incomplete` - no real `.st1`/`.st3`
sample file exists in this repo to validate against).

## detect_stf.c

Implements the STF depacker (`stf_depack_all` and its byte-level
fill/move-back/move-literal helpers, a small custom LZ77-like scheme) and
`detect_stf_structural`, which depacks the whole candidate window in one
pass (documented as provably equivalent to Pascal's incremental
per-checkpoint depack-then-validate calls) and then re-checks every
structural field `FoundSTF` would have validated as it went.

Ported from: ay_emul/Players.pas (`FoundSTF` 5408-5553; `STFDepackInit`/
`STFDepackBytes`/`MoveBufBytes` 1187-1329; `STF_AllowedChars`; `ModTypes`
variant 15, 202-210). Migration debt: `MIG-0105` (open,
`behaviorally_incomplete`).

## detect_vtx.c

Implements `detect_vtx`: 2-letter id (`ay`/`ym`/`AY`/`YM`) plus a mode
byte in 0..6 (the constraint beyond the bare `Ay_Emul.fmt` match that
prevents false positives on any file merely starting with those two
letters). Lowercase id selects a long header (with Year/Programm/Tracker/
Comment strings); uppercase selects a short header; the letter pair also
names the AY-chip vs YM-chip synthesis mode.

Ported from: ay_emul/Players.pas (`AddFile`'s inline VTX check
8135-8140; `TVTXFileHeader` 261-269; `Chip_Type` 7674-7677);
ay_emul/Ay_Emul.fmt (`[format+ VTX]`).

## detect_ym.c

Implements `detect_ym_body` (the YM2!/YM3!/YM3b/YM5!LeOnArD!/YM6!LeOnArD!
prefix check at a given base offset, plus YM5/YM6's digi-drum count and
"extended interleaved" `Song_Attr` bit extraction) and `detect_ym` (adds
the `-lh5-` LHA-wrapper check at offset 2; the wrapped payload's inner
sub-variant is not resolved since this tool does not decompress LHA
bodies).

Ported from: ay_emul/Players.pas (`AddFile`'s inline YM content-sniff
8132-8146; `TYM5FileHeader` 274-283; extended-interleaved bit 2812-2870;
`TLZHFileHeader` 286-296); ay_emul/Ay_Emul.fmt (`[format+
YM2/YM3/YM3b/YM5/YM6/YM]`). Migration debt: `MIG-0024` (validated - the
LHA-wrapped `.ym` sub-variant gap is one of the eight documented
oracle-diff mismatches).

## dispatch.c

Implements `identify`, the Tier A (extension trust via `EXT_TABLE`) /
Tier B (`.ay`/`.ym`/`.psg` ambiguous-extension special case) / Tier C
(content sniffing, falling through to `scan_whole_file_module_detector`)
dispatcher, plus `scan_whole_file_module_detector` - a genuine byte-by-byte
sliding scan re-running all seventeen structural detectors at every
candidate offset in Pascal's exact order, and `confirm()`/`confirm_asc()`,
which gate each structural match behind its `IntegrityCheck` port before
accepting it (upgrading `confidence` to `definite` on success, discarding
the candidate and continuing the scan on failure).

Ported from: ay_emul/Players.pas (`AddFile` ~8047-8146; `Module_Detector`
6830-7024, including its exact `FoundST1, FoundST3, FoundSTC, FoundASC1,
FoundASC0, FoundSTF, FoundSTP, FoundPT2, FoundPT3(x2), FoundPSC(x2),
FoundFTC, FoundPT1, FoundGTR, FoundSQT, FoundFLS` order); ay_emul/Ay_Emul.fmt
(`[fnext ...]` extension sections). Migration debt: `MIG-0023`/`MIG-0023b`
(open, `translated` - documents the correction from an Ay_Emul.fmt-signature
substring-search approximation to genuine structural `FoundXXX` ports, since
`match=` fields turned out to be read only by `filetypes.pas`'s desktop
mime-type XML writer, never by `AddFile`/`Module_Detector`).

## integrity_check.c

Implements one `integrity_check_<format>` function per structural-tracker
format by loading the candidate through `engine/`'s corresponding
`<format>_file_load` and checking `global_tick_max > 0` - reusing
`engine/`'s already oracle-validated duration precomputes rather than
re-deriving `GetTimeXXX`'s logic a second time. ST1/ST3/STF have no
`engine/` port of their own; they first convert via `st_convert.h`'s
`st1_to_stc`/`st3_to_stc`/`stf_to_stp` and then reuse `stc_file_load`/
`stp_file_load`. Links against `engine/libayengine.a`.

Ported from: ay_emul/Players.pas (every `FoundXXX`'s
`IntegrityCheck`/`LoadTrackerModule`/`GetTimeXXX` tail, e.g. `FoundSTC`
5159-5171). Migration debt: `MIG-0023b`/`MIG-0101`/`MIG-0103`/`MIG-0104`
(open, `translated`); `MIG-0105` (open, `behaviorally_incomplete` for
ST1/ST3/STF).

## main.c

CLI entry point: reads the one required argument, loads the file via
`read_whole_file`, calls `identify`, and prints the single `key=value`
output line to stdout per `identify_ay_file.md`'s output schema. Exit
status `2` for usage errors, `1` for file-access errors (both to stderr);
a successfully-opened file always produces exactly one stdout line, even
`format=unknown` or `malformed=yes` ones (with the malformed reason also
printed to stderr).

Not a port of one specific Pascal routine - the overall behaviour mirrors
`Players.pas`'s `AddFile` being driven once per file, reduced to a
non-interactive CLI without any playback/GUI integration.

## outline.c

Implements the `outline` output-line builder: `out_init`, `out_kv` (with
the `\\`/`\ `/`\xHH` escaping convention that keeps the whole line
splittable on unescaped whitespace without quoting), `out_kv_int`, and
`out_kv_bool` (tri-state `yes`/`no`/`unknown`).

Original C infrastructure implementing `identify_ay_file.md`'s
"Escaping" section; not a port of Pascal code.

## st_convert.c

Implements `st1_to_stc`, `st3_to_stc` (`ST12STC`/`ST32STC`: convert an
ST1/ST3 candidate window into STC's on-disk layout, including the
pattern-opcode-stream `AddPat`/`CalcEmpty` dedup logic), and `stf_to_stp`
(depacks via `detect_stf.h`'s `stf_depack_all`, expands the gapless
pattern region back out via `InsertUnusedPatterns`, then runs the
`STF2STP` conversion) - so `integrity_check.c` can confirm these three
formats by reusing `engine/`'s existing STC/STP loaders instead of a
dedicated ST1/ST3/STF playback engine, exactly mirroring
`LoadTrackerModule`'s own real dispatch.

Ported from: ay_emul/Players.pas (`ST12STC` 1766-2049 incl. `AddPat`
1770-1783 and `CalcEmpty` 1789-1803, `ST32STC` 2051-2216, `STF2STP`
1346-1764 incl. `InsertUnusedPatterns` 1362-1379 and its own `AddPat`/
`CalcEmpty` 1349-1397). Migration debt: `MIG-0105` (open,
`behaviorally_incomplete` - not oracle-validated, no real `.st1`/`.st3`/
`.stf` sample file exists in this repo).
