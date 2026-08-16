# tools/identify_ay_file/src/

Source for `identify_ay_file`, a standalone tool that identifies AY/YM/tracker
chiptune file formats by extension/magic bytes/structural checks, mirroring
`ay_emul/Players.pas`'s `AddFile` Tier A/B/C dispatch logic (see
`dispatch.c`'s top comment for the full citation), without implementing any
playback/emulation. Detector-specific Pascal source line citations live in
each file's own header comment; see `tools/identify_ay_file/include/identify/
README.md` for the per-header summary of the same mapping.

## bufio.c

File loading and bounds-checked byte access shared by every detector:
`read_whole_file` (caps reads at `MAX_READ_SIZE`), `has_at`/`str_at`/
`in_bounds`/`byte_at`/`be16_at`/`le16_at` (endian-explicit field readers —
`le16_at` documented as matching x86 Pascal native-endian `word`/`PWord`
fields used by ST3/ASC/STP/PT1/PT2/SQT/FLS's on-disk pointer tables), and
`find_bytes` (whole-file substring search for Tier C fallbacks).

Not a port — original C infrastructure implementing the `identify/common.h`
API; no single Pascal file corresponds to it.

## detect_container.c

Implements `detect_ay_container`, `detect_aym`, `detect_psg`.

Ported from: `Players.pas` — `detect_ay_container` mirrors `OpenAYFile`
(`Players.pas:7132-7236`), including the verified-against-a-real-build detail
that a `ZXAY`+`AMAD` container loads as an ordinary FXM file and `ZXAY`+
`ST11` as an ordinary ST1 file (`Players.pas:7195,7216,7229`), not as
distinct AYAMAD/ST11 formats the way `Ay_Emul.fmt`'s naming alone would
suggest — confirmed via a dedicated `OracleHarness.pas` scenario
(`tests/oracle_diff`) after an earlier implementation got this wrong.
`detect_aym` mirrors `OpenAYMFile` (`Players.pas:7026-7038`), including its
hard revision-byte requirement. `detect_psg` mirrors the PSG/EPSG
content-sniff check at `Players.pas:8130`, including the quirk that EPSG
content is dispatched as plain `FT.PSG` even in Pascal's own real logic.

## detect_fls.c

Implements `detect_fls_structural`, Flash Tracker's pointer/table structural
validator.

Ported from: `Players.pas` — mirrors `FoundFLS` (`Players.pas:6332-6544`,
the current/faster variant; the older commented-out `FoundFLS` at
`Players.pas:6547`+ is dead code and intentionally not ported), minus the
final `IntegrityCheck` step (see `detect_st_family.h`'s documented
approximation, which applies here too).

## detect_pt3.c

Implements `detect_pt3` (PT3 compiler-stamp text signature check, version/
turbosound/tone-table field extraction) and `scan_whole_file_for_pt3` (Tier C
substring-search fallback).

Ported from: `Players.pas` — mirrors the `Ay_Emul.fmt` PT3 signature (two OR'd
alternatives), the `ModTypes` PT3 `MusicName` layout (`Players.pas:136`),
version digit read (`Players.pas:2580-2581`), turbosound marker
(`Players.pas:2662`), and `TonTableId` offset, cross-checked against
`engine/src/pt3_file.c`. `scan_whole_file_for_pt3` is a documented
approximation (plain substring search) of Pascal's real sliding structural
`Module_Detector` re-check, not a byte-for-byte port.

## detect_pt_asc_family.c

Implements `detect_asc1_structural`, `detect_asc0_structural`,
`detect_pt2_structural`, `detect_pt1_structural`, `detect_sqt_structural`.

Ported from: `Players.pas` — mirrors `FoundASC1`/`FoundASC0`
(`Players.pas:5299-5406`), `FoundPT2` (`Players.pas:5785-5878`), `FoundPT1`
(`Players.pas:6262-6330`), and `FoundSQT` (`Players.pas:6182-6260`), each
against its `ModTypes` layout variant comment citation, minus the final
`IntegrityCheck` step (same documented approximation as
`detect_st_family.h`).

## detect_signature_trackers.c

Implements `detect_stc`, `detect_psc`, `detect_ftc`, `detect_gtr`,
`detect_fxm`, `detect_psm` (fixed-offset byte-signature trackers) and
`scan_whole_file_for_signature_trackers` (Tier C fallback for
STC/PSC/FTC/GTR only — FXM/PSM are deliberately excluded there since
`Module_Detector`'s real if/elseif chain never calls `FoundFXM`/`FoundPSM`).

Mirrors: `Ay_Emul.fmt`'s `[format+ STC/PSC/FTC/GTR/FXM/PSM]` match entries,
cross-referenced against `Players.pas:6901-7002`'s `Module_Detector` body to
determine real Tier-C reachability. These are direct signature checks, not
ports of a `FoundXXX` structural routine.

## detect_sndh.c

Implements `detect_sndh`: ICE-compressed or plain SNDH header detection.

Mirrors: `Ay_Emul.fmt`'s `[format+ SNDH]` entries and `sndh.pas`'s TSNDHTag
format, restricted to the two structural properties visible in the fixed
16-byte header (see `detect_sndh.h`); the full tag-chain walk is out of
scope here (already ported for playback in `engine/src/sndh_file.c`).

## detect_st_family.c

Implements `detect_st1_structural`, `detect_st3_structural`,
`detect_stp_structural`.

Ported from: `Players.pas` — mirrors `FoundST1` (`Players.pas:4922-5065`),
`FoundST3` (`Players.pas:5175-5297`), and `FoundSTP`
(`Players.pas:5555-5624`), each against its `ModTypes` layout variant. Omits
the final `IntegrityCheck` confirmation step (a documented, deliberate
approximation covering multiple detectors in this directory — see the file's
own comments and `detect_st_family.h`); also documents that ST3/STP's
KsaId-prefixed secondary-confirmation sub-case is approximated by using only
the more common non-KsaId pointer base.

## detect_stf.c

Implements the STF (Sound Tracker Pro, uncompiled) depacker and structural
validator.

Ported from: `Players.pas` — ports `STFDepackInit`/`STFDepackBytes`
(`Players.pas:1187-1329`, including `MoveBufBytes` at
`Players.pas:1222-1240`) as a single-pass depacker (documented in the file's
top comment as provably equivalent to Pascal's incremental per-checkpoint
calls), then re-runs every structural check from `FoundSTF`
(`Players.pas:5408-5553`) against the depacked buffer, again omitting the
final `IntegrityCheck` step.

## detect_vtx.c

Implements `detect_vtx`.

Ported from: `Players.pas` — mirrors `Ay_Emul.fmt`'s `[format+ VTX]` entry
plus the mode-byte range check and AY/YM chip-type distinction from
`Players.pas:8135-8140` and `Chip_Type` (`Players.pas:7674-7677`).

## detect_ym.c

Implements `detect_ym_body` and `detect_ym`.

Ported from: `Players.pas` — mirrors the YM2!/YM3!/YM3b/YM5!/YM6! signature
checks (`Ay_Emul.fmt`, `Players.pas:8132-8134`), `TYM5FileHeader`
(`Players.pas:274-283`), and the extended-interleaved `Song_Attr` bit
(`Players.pas:2812-2870`) used to select `YM5i`/`YM6i` vs. plain
`YM5`/`YM6` register decoding.

## dispatch.c

Implements `identify`, the top-level dispatcher, plus its extension table,
named-detector lookup, and `Module_Detector`-order Tier C fallback chain.

Mirrors: `Players.pas`'s `AddFile` (`~Players.pas:8047` onward) — the file's
extensive top-of-file comment documents the exact Tier A (extension trust,
`Players.pas:8100-8103`), Tier B (ambiguous `.ay`/`.ym`/`.psg` extensions,
`Players.pas:8104-8114`), and Tier C (content sniffing then
`Module_Detector`, `Players.pas:8115-8146` and `6830-7024`) logic, plus two
explicitly documented approximations: (a) STC/PSC/FTC/PT3/GTR are re-tested
via multi-anchor substring search rather than Pascal's fuller structural
check, and (b) the ten signature-less structural trackers are only checked
anchored at file offset 0, not at every offset the way Pascal's real sliding
scan does. This is a reimplementation of `AddFile`'s dispatch decision logic,
not a line-by-line port of it.

## main.c

CLI entry point: reads the target file (bounded by `MAX_READ_SIZE`, with a
truncation warning to stderr if larger), calls `identify`, and prints one
`key=value ...` line to stdout via the `outline` builder, with malformed
content reported as `malformed=yes` data rather than a hard error.

Not a port — new CLI/tool infrastructure. It drives the ported dispatch/
detector logic in this directory but has no direct Pascal counterpart; the
one-line `key=value` output format is this tool's own design (documented in
`identify_ay_file.md`), not a Pascal UI/output format being reproduced.

## outline.c

Implements the `outline` output-line builder (`out_init`/`out_kv`/
`out_kv_int`/`out_kv_bool`) with its own escaping convention (backslash,
space, and non-printable bytes escaped so the whole line stays splittable on
unescaped whitespace without quoting).

Not a port — new C infrastructure implementing this tool's own output format,
as documented in `identify_ay_file.md`'s "Escaping" section; there is no
Pascal source it mirrors.
