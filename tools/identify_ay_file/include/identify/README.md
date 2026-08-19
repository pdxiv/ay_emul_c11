# include/identify/

Public headers for `identify_ay_file`'s detector modules: shared types/helpers
(`common.h`), one header per format family's detection logic, the top-level
dispatcher, and the `IntegrityCheck`/`st_convert` support used to confirm a
structural match against a real playable duration. Each header cites the
exact `Ay_Emul.fmt`/`Players.pas` source its detector(s) port; see
`../../identify_ay_file.md` for the overall Tier A/B/C design and the full
format inventory.

## common.h

Declares `filebuf` (loaded-file buffer), `detection` (the accumulated
per-file result struct), `outline` (the output-line builder), and the
bounds-checked byte-access helpers (`has_at`, `str_at`, `in_bounds`,
`byte_at`, `be16_at`, `le16_at`, `find_bytes`) every detector is built on.
Not itself a port of one Pascal file - `le16_at` documents that on-disk
`word` pointer fields are native-endian (little-endian) x86 Pascal values,
matching every ST3/ASC/STP/PT1/PT2/SQT/FLS on-disk pointer table.

## detect_container.h

Declares `detect_ay_container`, `detect_aym`, and `detect_psg` - the
AY-chip-native container family: the real Z80-driven `.ay`/`ZXAYEMUL`
container (and its `AYAMAD`/`ST11` TypeID siblings), the `AYM0`
RDOSPLAY snapshot, and `PSG`/`EPSG` register dumps.

Ported from: ay_emul/Players.pas (`OpenAYFile` ~7132-7236, `TAYFileHeader`
220-230, `OpenAYMFile` 7026-7038, `TAYMFileHeader` 245-254, and the
`AddFile` PSG/EPSG dispatch quirk at 8130); ay_emul/Ay_Emul.fmt
(`[format+ AY]`/`[format+ AYAMAD]`/`[format+ AYM]`/`[format+ PSG]`/
`[format+ EPSG]`).

## detect_fls.h

Declares `detect_fls_structural` - Flash Tracker (FLS), a format with no
`Ay_Emul.fmt` byte signature; only recognised via the pointer/table-walk
structural check ported here, minus the final `IntegrityCheck` step (see
`integrity_check.h`).

Ported from: ay_emul/Players.pas (`FoundFLS`, the current/faster variant,
6332-6544 - the older, commented-out `FoundFLS` is dead code and not
ported; `ModTypes` variant 10, 171-176).

## detect_pt3.h

Declares `detect_pt3` (the `Ay_Emul.fmt` text-signature check),
`detect_pt3_structural` (the real `Module_Detector` structural port,
tried with both a loader-relative and an absolute-from-0 pointer base),
and `scan_whole_file_for_pt3` (a signature-substring fallback kept only
for reference/tests, no longer used by dispatch).

Ported from: ay_emul/Players.pas (`FoundPT3(DetectAdr)`, 5880-5967;
version digit and Turbosound-marker logic, 2580-2665; `ModTypes` variant 6,
136-144); ay_emul/Ay_Emul.fmt (`[format+ PT3]`).

## detect_pt_asc_family.h

Declares the structural detectors for five formats with no `Ay_Emul.fmt`
byte signature - ASC/ASC0 (ASC Sound Master), PT1/PT2 (Pro Tracker 1/2),
SQT (SQ-Tracker) - plus `detect_psc_structural` for Pro Sound Creator's
own pointer/table structural check (grouped here since PSC's detection is
the same pointer-following style as its siblings, not a signature match).

Ported from: ay_emul/Players.pas (`FoundASC1` 5299-5352, `FoundASC0`
5354-5406, `FoundPT2` 5785-5878, `FoundPT1` 6262-6330, `FoundSQT`
6182-6260, `FoundPSC(PSC1_00)` 5969-6088).

## detect_signature_trackers.h

Declares the six formats whose `Ay_Emul.fmt` signature is a simple
fixed-offset text match - `detect_stc`, `detect_psc`, `detect_ftc`,
`detect_gtr`, `detect_fxm`, `detect_psm` - plus the legacy
`scan_whole_file_for_signature_trackers` substring fallback (kept for
reference/tests only) and the real `Module_Detector` structural ports
`detect_stc_structural`, `detect_gtr_structural`, `detect_ftc_structural`.

Ported from: ay_emul/Ay_Emul.fmt (`[format+ STC/PSC/FTC/GTR/FXM/PSM]`);
ay_emul/Players.pas (`FoundSTC` 5067-5173, `FoundGTR` 4841-4920, `FoundFTC`
6090-6180).

## detect_sndh.h

Declares `detect_sndh` - the Atari ST/MC68000-driven SNDH container,
detected either by the ICE-compressed wrapper's `ICE!` prefix or the
fixed-offset `SNDH` magic after the 12-byte jump-vector table.

Ported from: ay_emul/Players.pas (`OpenSNDHFile`, 7065-7080);
ay_emul/Ay_Emul.fmt (`[format+ SNDH]`).

## detect_st_family.h

Declares `detect_st1_structural`, `detect_st3_structural`, and
`detect_stp_structural` - the "Sound Tracker" lineage formats with no
`Ay_Emul.fmt` byte signature at all (ST1 uncompiled, ST3/S.T. Music's
Recompiler, STP/Sound Tracker Pro compiled), recognised only via
pointer/table structural validation. Documents that all three now also get
the `IntegrityCheck` confirmation step, and that ST1/ST3 (unlike STP) have
no `engine/` port of their own - they reuse STC's via `st_convert.h` - and
have NOT been oracle-diff-validated against a real file, since none exists
in this repo's corpus.

Ported from: ay_emul/Players.pas (`FoundST1` 4922-5065, `FoundST3`
5175-5297, `FoundSTP` 5555-5624). Migration debt: `MIG-0105` (open,
`behaviorally_incomplete` - ST1/ST3/STF verification gap).

## detect_stf.h

Declares `detect_stf_structural` plus the exposed `stf_depack_all` helper
(and its `stf_depack_state` type) - Sound Tracker Pro uncompiled (STF), a
format with no `Ay_Emul.fmt` signature whose modules are themselves stored
compressed with a small custom LZ77-like scheme. The depacker is exposed so
`st_convert.c`'s STF-to-STP converter can reuse it without duplicating the
decode logic.

Ported from: ay_emul/Players.pas (`FoundSTF` 5408-5553; `STFDepackInit`/
`STFDepackBytes` 1187-1329; `ModTypes` variant 15, 202-210).

## detect_vtx.h

Declares `detect_vtx` - the VTX register-dump container, identified by its
2-letter id (`ay`/`ym`/`AY`/`YM`) plus a mode byte in 0..6.

Ported from: ay_emul/Players.pas (`AddFile`'s inline VTX check 8135-8140;
`TVTXFileHeader` 261-269; `Chip_Type` 7674-7677); ay_emul/Ay_Emul.fmt
(`[format+ VTX]`).

## detect_ym.h

Declares `detect_ym_body` (checks the YM2!/YM3!/YM3b/YM5!.../YM6!...
magic at a given offset - used directly, and would apply post-LHA-decompression
for wrapped files, which this tool does not perform) and `detect_ym` (the
full entry point, also checking the `-lh5-` LHA wrapper at offset 2).

Ported from: ay_emul/Players.pas (`AddFile`'s inline YM content-sniff
8132-8146; `TYM5FileHeader` 274-283; extended-interleaved-variant bit,
2812-2870); ay_emul/Ay_Emul.fmt (`[format+ YM2/YM3/YM3b/YM5/YM6/YM]`).

## dispatch.h

Declares `identify`, the single top-level entry point mirroring
`AddFile`'s Tier A (extension trust) / Tier B (ambiguous-extension special
case) / Tier C (content sniffing, falling through to a full
`Module_Detector` sliding-window scan) exactly, in that order.

Ported from: ay_emul/Players.pas (`AddFile` ~8047-8146; `Module_Detector`
6830-7024). Migration debt: `MIG-0023`/`MIG-0023b` (open, `translated`).

## integrity_check.h

Declares one `integrity_check_<format>` function per structural-tracker
format (fifteen total), each mirroring a `FoundXXX`'s own tail (`if
IntegrityCheck then ... if TimeLength = 0 then exit`) by loading the
candidate through `engine/`'s already oracle-validated per-format loader
and rejecting it unless the computed `global_tick_max` duration is
nonzero. Twelve formats reuse their own `engine/` port directly; ST1/ST3/STF
have none of their own and instead convert to STC's/STP's layout first via
`st_convert.h`. FXM/PSM's `engine/` `GetTimeXXX` ports exist but are
deliberately not exposed here, since `Module_Detector` never structurally
matches either format (both are Tier-A-only).

Ported from: ay_emul/Players.pas (every `FoundXXX`'s `IntegrityCheck`/
`LoadTrackerModule`/`GetTimeXXX` tail, e.g. `FoundSTC` 5159-5171).
Migration debt: `MIG-0023b`/`MIG-0101`/`MIG-0103`/`MIG-0104` (open,
`translated`); `MIG-0105` (open, `behaviorally_incomplete` for the
ST1/ST3/STF trio specifically).

## st_convert.h

Declares `st1_to_stc`, `st3_to_stc`, and `stf_to_stp` - recompiler-style
converters that turn an ST1/ST3/STF candidate window into an already-ported
newer format's on-disk layout (STC or STP), so `integrity_check.h` can
confirm the match by reusing `engine/`'s existing STC/STP loader instead of
needing a dedicated ST1/ST3/STF playback engine, exactly mirroring
`LoadTrackerModule`'s own real dispatch. Documents explicitly that this
trio has NOT been validated against a real `.st1`/`.st3`/`.stf` file, since
none exists anywhere in this repo's test corpus - validated instead by
hand-tracing, cross-checking against `engine/`'s already-validated
STC/STP register-getters, and round-tripping hand-constructed synthetic
files.

Ported from: ay_emul/Players.pas (`ST12STC` 1766-2049, `ST32STC`
2051-2216, `STF2STP` 1346-1764). Migration debt: `MIG-0105` (open,
`behaviorally_incomplete`).
