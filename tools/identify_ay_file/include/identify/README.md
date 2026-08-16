# tools/identify_ay_file/include/identify/

Public headers for `identify_ay_file`, a standalone tool that identifies
AY/YM/tracker chiptune file formats by extension and/or magic bytes, mirroring
`ay_emul/Players.pas`'s `AddFile` Tier A/B/C dispatch logic (see `dispatch.h`/
`dispatch.c` for the full citation) without any playback/emulation code. Most
detectors' own header comments cite the exact `Players.pas` source ported;
where a header is undocumented, see the paired `.c` file's comments.

## common.h

Shared types and small bounds-checked helpers used by every detector module:
the `filebuf` (loaded file bytes + sizes) and `detection` (identification
result) structs, `read_whole_file`, byte-access helpers (`has_at`, `str_at`,
`in_bounds`, `byte_at`, `be16_at`, `le16_at`, `find_bytes`), and the `outline`
output-line builder (`out_init`/`out_kv`/`out_kv_int`/`out_kv_bool`).

Not a port — original C infrastructure with no single Pascal counterpart; it
underpins the ports of the individual `FoundXXX`/format-check routines listed
below by giving them safe, endian-explicit byte access into a loaded file
buffer.

## detect_container.h

Declares `detect_ay_container`, `detect_aym`, `detect_psg`: the AY-container
family (real Z80-driven `.ay`/AYAMAD/ST11 containers), AYM, and PSG/EPSG
register-dump detectors.

Ported from: `Players.pas` — `detect_ay_container` mirrors `OpenAYFile`
(`Players.pas:7132-7236`, `TAYFileHeader` at `Players.pas:220-230`);
`detect_aym` mirrors `OpenAYMFile` (`Players.pas:7026-7038`,
`TAYMFileHeader` at `Players.pas:245-254`); `detect_psg` mirrors the
`PSG`/`EPSG` content-sniff check at `Players.pas:8130`. All three also cite
`Ay_Emul.fmt`'s `[format+ ...]` signature entries as corroborating data.

## detect_fls.h

Declares `detect_fls_structural`: structural detector for Flash Tracker
(FLS), which has no `Ay_Emul.fmt` byte signature.

Ported from: `Players.pas` — mirrors `FoundFLS` (`Players.pas:6332-6544`,
the "current, faster and more reliable" variant; an older `FoundFLS` left
commented out at `Players.pas:6547` onward is dead code and not ported),
minus the final `IntegrityCheck` confirmation step (documented approximation,
see `detect_st_family.h`).

## detect_pt3.h

Declares `detect_pt3` (structural/signature check) and
`scan_whole_file_for_pt3` (best-effort Tier C whole-file substring fallback).

Mirrors: `Players.pas`'s `Ay_Emul.fmt`-driven PT3 signature check
(`ModTypes` PT3 variant at `Players.pas:136`, version/turbosound/TonTableId
field reads around `Players.pas:2580-2662`) and the corresponding
`Module_Detector` PT3 re-check. `scan_whole_file_for_pt3` is a documented
approximation (substring search rather than Pascal's real sliding structural
check), not a line-by-line port.

## detect_pt_asc_family.h

Declares `detect_asc1_structural`, `detect_asc0_structural`,
`detect_pt2_structural`, `detect_pt1_structural`, `detect_sqt_structural`:
structural detectors for ASC/ASC0 (ASM Sound Master), PT1/PT2 (Pro Tracker
1/2), and SQT (SQ-Tracker), none of which have an `Ay_Emul.fmt` byte
signature.

Ported from: `Players.pas` — mirrors `FoundASC1`/`FoundASC0`/`FoundPT2`/
`FoundPT1`/`FoundSQT` (`Players.pas:5299-5407, 5785-5878, 6182-6330`), minus
the final `IntegrityCheck` confirmation step (same documented approximation
as `detect_st_family.h`).

## detect_signature_trackers.h

Declares `detect_stc`, `detect_psc`, `detect_ftc`, `detect_gtr`, `detect_fxm`,
`detect_psm` (simple fixed-offset byte-signature trackers), plus
`scan_whole_file_for_signature_trackers`, a Tier C fallback restricted to
STC/PSC/FTC/GTR.

Mirrors: `Ay_Emul.fmt`'s `[format+ STC/PSC/FTC/GTR/FXM/PSM]` signature
entries, cross-referenced against `Players.pas:6901-7002`'s
`Module_Detector` chain to determine which of the six are actually
Tier-C-reachable in the real program (FXM/PSM are not, per the `.c` file's
comment). Simple signature matches, not structural ports of a `FoundXXX`
routine.

## detect_sndh.h

Declares `detect_sndh`: checks for ICE-compressed or plain SNDH headers.

Mirrors: `Ay_Emul.fmt`'s `[format+ SNDH]` signature entries
(`ICE!` / offset-12 `SNDH` magic) and `sndh.pas`'s `TSNDHTag`-based format,
restricted to the two structural properties visible in the fixed 16-byte
header; the full variable-length tag-chain walk (title/author/song-count) is
out of scope for identification (tracked in `migration_debt.yaml`).

## detect_st_family.h

Declares `detect_st1_structural`, `detect_st3_structural`,
`detect_stp_structural`: the "Sound Tracker" lineage (ST1 uncompiled, ST3,
STP compiled), none of which have an `Ay_Emul.fmt` byte signature.

Ported from: `Players.pas` — mirrors `FoundST1`/`FoundST3`/`FoundSTP`
(`Players.pas:4922-5065, 5175-5297, 5555-5624`). The header comment documents
one explicit, deliberate approximation shared by this and several other
detectors in this directory: the final `IntegrityCheck` confirmation step
(`LoadTrackerModule` + `GetTimeXXX`, discarding zero-duration candidates) is
not performed, since it requires the full tracker-loading/playback engine
this tool deliberately does not implement; every other structural check is
ported faithfully. Tracked as part of MIG-0023's `confidence=probable`
caveat in `migration_debt.yaml`.

## detect_stf.h

Declares `detect_stf_structural`: STF (Sound Tracker Pro, uncompiled), which
stores its module data compressed with a custom LZ77-like scheme.

Ported from: `Players.pas` — mirrors `FoundSTF` (`Players.pas:5408-5553`)
and depacks via a port of `STFDepackInit`/`STFDepackBytes`
(`Players.pas:1187-1329`), decoding the candidate window in one pass rather
than Pascal's incremental per-checkpoint calls (documented as provably
equivalent in the `.c` file). Same final-`IntegrityCheck`-omitted caveat as
`detect_st_family.h`.

## detect_vtx.h

Declares `detect_vtx`: VTX (ay/ym lowercase/uppercase 2-letter id + mode
byte) detector.

Ported from: `Players.pas` — mirrors `Ay_Emul.fmt`'s `[format+ VTX]` entry
plus the additional mode-byte range check from `Players.pas:8135-8140`, and
cites `Chip_Type` (`Players.pas:7674-7677`) for the AY-chip-vs-YM-chip and
long/short header distinction.

## detect_ym.h

Declares `detect_ym_body` (checks the YM2!/YM3!/YM3b/YM5!/YM6! magic at a
given offset) and `detect_ym` (full entry point, also checks the `-lh5-` LHA
wrapper at offset 2).

Ported from: `Players.pas` — mirrors `Ay_Emul.fmt`'s YM2/YM3/YM3b/YM5/YM6
signature entries and `Players.pas:8132-8134`'s Tier C sniff, `TYM5FileHeader`
(`Players.pas:274-283`), and the extended-interleaved `Song_Attr` bit check
cited against `Players.pas:2812-2870`.

## dispatch.h

Declares `identify`, the top-level entry point.

Mirrors: `Players.pas`'s `AddFile` (`Players.pas:~8047` onward) — see
`dispatch.c`'s top-of-file comment for the full Tier A/B/C citation. Not a
line-by-line port; it reimplements the three-tier dispatch decision logic
(extension trust, ambiguous-extension special case, and content-sniffing
fallback through `Module_Detector`) using the detectors declared in the
other headers in this directory.
