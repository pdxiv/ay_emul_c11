# identify_ay_file

A standalone C11 command-line utility that identifies AY/YM chiptune music
file formats and their relevant variants, reproducing the identification
logic of the original `ay_emul` Object Pascal program without any of its
playback/emulation code.

The source is split into one file per format family under `src/`, with
shared types/helpers in `include/identify/` (each file is kept under 600
lines; see `src/dispatch.c` for the top-level Tier A/B/C dispatch that
ties every detector together).

Build:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude -o identify_ay_file src/*.c
# or, from this directory:
make
```

Usage:

```sh
./identify_ay_file <music-file>
find ./songs -type f -exec ./identify_ay_file '{}' \; | sort
find ./songs -type f -exec ./identify_ay_file '{}' \; | grep 'turbo_sound=yes'
find ./songs -type f -exec ./identify_ay_file '{}' \; | sort -u -k2
```

Exit status: `0` on success (one line printed to stdout, even for
unrecognised or malformed content - the file itself was successfully
opened and read). `1` for file-access errors (cannot open/seek/read). `2`
for usage errors (wrong number of arguments). Diagnostics for both go to
stderr; a `malformed=yes` result also prints one explanatory line to
stderr in addition to its normal stdout line.

## Where the real detection logic comes from

Contrary to first appearances, `ay_emul/filetypes.pas` does not contain the
format table itself - `load_formats` loads it at runtime from an embedded
`RCDATA` resource (`Ay_Emul.lpi`: `Resource_18` -> `Ay_Emul.fmt`,
`ResourceName="FILETYPES"`). `Ay_Emul.fmt` is therefore the authoritative,
data-driven source of every format's canonical name, extension(s), and
(where one exists) magic-byte signature, and is quoted verbatim throughout
`identify_ay_file.c`'s comments.

The real top-level file-open dispatcher is `Players.pas`'s `AddFile`
(~line 8047). It runs in three tiers, reproduced here in the same order:

- **Tier A** - extension lookup (`GetFileTypeFromFNExt`, driven by
  `Ay_Emul.fmt`'s `[fnext ...]` sections). If the extension maps to exactly
  one format, Pascal uses it **with no content verification at all**
  (`Players.pas:8100-8103`).
- **Tier B** - a hand-written special case for the three extensions that
  map to more than one format (`.ay`, `.ym`, `.psg`, `Players.pas:8104-8114`).
  Pascal still does not check magic bytes here either; it hard-codes
  `.ay`->`OpenAYFile`, `.ym`->`FT.YM`, `.psg`->`FT.PSG` and discovers the
  real sub-variant only once the file is actually opened for playback.
- **Tier C** - content sniffing (`Players.pas:8115-8146`), used only when
  the extension is unrecognised or absent. Checks `ZXAY`+TypeID,
  `PSG`/`EPSG`, the four-byte `YM2!`/`YM3!`/`YM3b`/`YM5!`/`YM6!` prefixes,
  the VTX 2-letter+version-byte pattern, and `-lh5-` at offset 2, in that
  order; falling through to `Module_Detector` (`Players.pas:6830-7024`), a
  whole-file sliding-window scan calling, in this exact order, `FoundST1`,
  `FoundST3`, `FoundSTC`, `FoundASC1`, `FoundASC0`, `FoundSTF`, `FoundSTP`,
  `FoundPT2`, `FoundPT3`, `FoundPSC`, `FoundFTC`, `FoundPT1`, `FoundGTR`,
  `FoundSQT`, `FoundFLS` - first match wins. (`FXM`/`PSM` have
  `Ay_Emul.fmt` byte signatures but are *not* part of this chain - a full
  read of `Module_Detector`'s body confirms it never calls
  `FoundFXM`/`FoundPSM`; they are Tier-A-only in the real program.)

`identify_ay_file` reproduces Tier A and B exactly, reproduces every Tier C
magic check exactly, and reproduces the *entire* `Module_Detector` fallback
chain, in order, for extensionless/unrecognised-extension input, with two
documented approximations (see "Known limitations" below).

## Format/property inventory (from Ay_Emul.fmt + Players.pas)

All formats below share `type` &isin; {`AY`, `AYR`, `AYRS`, `AYEMUL`} in
`Ay_Emul.fmt` - the AY-chip-native format family. Formats of other `type`s
(`WAV`, `MOD`, `CDA`, `MIDI`, `PL`, `Skin` - i.e. MP3/WAV/FLAC/MOD/CDA/MIDI/
playlists/skins, all decoded by the BASS library rather than the AY chip
core) are out of scope per the task ("AY/YM music-file formats") and are
reported as `format=unknown` by this tool, even though `Ay_Emul.fmt` does
technically define signatures for some of them (MP3, WAV, RMI, XMI, AYL,
AYS). This is a scope judgement call, not a detection gap.

| Canonical name | `Ay_Emul.fmt` type | Extension(s) | Signature | Notes |
|---|---|---|---|---|
"none (ext only)" below means: no `Ay_Emul.fmt` byte signature exists, so
Tier A/B never content-verifies it (matching Pascal exactly) - but this
tool's Tier C fallback for extensionless input now runs a full structural
port of the corresponding `FoundXXX` detector (see "Detector-to-Pascal-
source mapping" and MIG-0023), unlike a plain "extension-only" gap.

| `OUT`   | AYRS   | out         | none (ext only) | Z80 emulator output log |
| `ZXAY`  | AYRS   | zxay        | `ZXAY` @0 | register-dump stream; also the fallback when a `ZXAY`-prefixed file's TypeID is none of EMUL/AMAD/ST11 |
| `AY`    | AYEMUL | ay (ambiguous) | `ZXAYEMUL` @0 | the real Z80-driven container (Sergey Bulba `.ay`) |
| `AYAMAD`| AY     | ay (ambiguous) | `ZXAYAMAD` @0 | Fuxoft AY Language variant of the same container |
| (ST11)  | -      | ay (ambiguous) | `ZXAY`+`ST11` @0/4 | a third TypeID `OpenAYFile` accepts; no distinct `Ay_Emul.fmt` name - reported as `format=AY subtype=ST11` |
| `AYM`   | AYEMUL | aym         | `AYM0` @0 | RDOSPLAY Z80 snapshot; `Rev` must be `'0'` |
| `EPSG`  | AYRS   | psg (ambiguous) | `EPSG\x1A` @0, 6 zero bytes @10 | **dead in practice** - `Players.pas:8130` dispatches EPSG content as `FT.PSG` too; reported as `format=PSG subtype=EPSG` |
| `PSG`   | AYR    | psg (ambiguous) | `PSG\x1A` @0 | register dump |
| `ST1`   | AY     | st1  | none (ext only); Tier C: full `FoundST1` port | Sound Tracker 1, uncompiled |
| `STC`   | AY     | stc, zxs | 10 alternative strings @7 | Sound Tracker, compiled |
| `ST3`   | AY     | st3  | none (ext only); Tier C: full `FoundST3` port | S.T. Music's Recompiler |
| `ASC`   | AY     | asc  | none (ext only); Tier C: full `FoundASC1` port | ASC Sound Master |
| `ASC0`  | AY     | as0  | none (ext only); Tier C: full `FoundASC0` port | ASC Sound Master v0 |
| `STF`   | AY     | stf  | none (ext only); Tier C: full `FoundSTF` port incl. its custom depacker | Sound Tracker Pro, uncompiled |
| `STP`   | AY     | stp  | none (ext only); Tier C: full `FoundSTP` port | Sound Tracker Pro, compiled |
| `PSC`   | AY     | psc  | `PSC V1.0`@0 + ` COMPILATION OF `@9 + ` BY `@45 (all required) | Pro Sound Creator |
| `FLS`   | AY     | fls  | none (ext only); Tier C: full `FoundFLS` port | Flash Tracker |
| `FTC`   | AY     | ftc  | `Module: `@0 + `;Fast Tracker v1.00`@50 (both required) | Fast Tracker |
| `PT1`   | AY     | pt1  | none (ext only); Tier C: full `FoundPT1` port | Pro Tracker 1 |
| `PT2`   | AY     | pt2  | none (ext only); Tier C: full `FoundPT2` port | Pro Tracker 2 |
| `PT3`   | AY     | pt3  | `ProTracker 3.`@0+...+` by `@62, OR `Vortex Tracker II 1.0 module: `@0+` by `@62 | version digit @13, Turbosound marker @98 (version&ge;7 only), tone-table id @99 |
| `SQT`   | AY     | sqt  | none (ext only); Tier C: full `FoundSQT` port | SQ-Tracker |
| `GTR`   | AY     | gtr  | `GTR\n` @1 | Global Tracker |
| `FXM`   | AY     | fxm  | `FXSM` @0 | Fuxoft AY Language |
| `PSM`   | AY     | psm  | `psm1` @8 | Pro Sound Maker |
| `TS`    | AY     | ts   | none (ext only) | Turbo Sound container, always 2 chips |
| `VTX`   | AYR    | vtx  | 2-letter id (`ay`/`ym`/`AY`/`YM`) @0 + mode byte 0-6 @2 | lowercase = long header; chip type = AY or YM from the id itself |
| `YM`    | AYR    | ym (ambiguous) | `YM2!`/`YM3!`/`YM3b`/`YM5!LeOnArD!`/`YM6!LeOnArD!` @0, or `-lh5-` @2 | subtype = YM2/YM3/YM3b/YM5/YM6; YM5/YM6 additionally carry the "extended interleaved" flag (`Song_Attr` bit 0) and a digidrum count |
| `SNDH`  | AYEMUL | sndh, snd | `ICE!` @0 (compressed) OR `SNDH` @12 | Atari ST, MC68000-driven |

Chip count (`chips=`) is `1` for all single-AY formats, `2` for `TS`
containers and for `PT3` files whose version&ge;7 Turbosound marker byte
(offset 98) is non-space (`Players.pas:2660-2665`).

## Output schema

One line per file, to stdout, space-separated `key=value` pairs, in this
fixed order:

```
file=<path> format=<FORMAT> subtype=<value|none> version=<n|unknown> chips=<n|unknown> turbo_sound=<yes|no|unknown> digi_drum=<yes|no|unknown> compressed=<none|lha|ice|unknown> [<format-specific-key>=<value>] malformed=<yes|no> confidence=<definite|probable|unknown>
```

- `file`/`format` are always the first two fields, per the task spec.
- `format=unknown` for files that were opened successfully but matched no
  known signature by any tier (Tier C's `Module_Detector` fallback
  notwithstanding - see "Known limitations").
- `subtype`, when applicable, further distinguishes variants Pascal itself
  distinguishes (e.g. `YM5` vs `YM6`, `long_header` vs `short_header` for
  VTX, `EPSG` vs `plain` for PSG). `none` when the schema property doesn't
  apply to that format.
- `confidence`:
  - `definite` - a real content signature matched (Tier B/C, or Tier A
    with a signature that also happened to match).
  - `probable` - the format came from a trusted-but-unverified extension
    (Tier A/B) and either has no content signature to check at all, or its
    signature check could not be run to completion.
  - `unknown` - `format=unknown`.
- `malformed=yes` marks files that were successfully read but whose content
  contradicts a genuine *structural* requirement of the format (truncated
  `AY`/`AYM` header, bad `AYM` revision byte, non-zero `EPSG` reserved
  field, an extension-trusted `AY`/`AYM`/`PSG`/`YM`/`VTX`/`SNDH` file whose
  content doesn't match its structural signature at all); the reason is
  also printed to stderr. It is deliberately **not** set merely because an
  extension-trusted `STC`/`PSC`/`FTC`/`GTR`/`PT3`/`FXM`/`PSM` file's
  compiler-stamp text doesn't match one of `Ay_Emul.fmt`'s known
  signatures - real Pascal never checks that text at Tier A either (see
  `format_has_structural_signature` in `dispatch.c`), and a real-world
  corpus run showed hundreds of perfectly valid files (different
  compiler/tool versions than the fixed signature list happens to cover)
  that would otherwise have been wrongly flagged. Such files still get
  `confidence=probable`, just without `malformed=yes`.
- Extra format-specific fields (`songs=`, `tone_table_id=`, `chip_type=`,
  `extended=`) appear after `compressed` and before `malformed` when
  applicable, so the first eight fields' positions never shift between
  formats - only whether a ninth field is present varies.

### Escaping

Values are never quoted. Within a value: `\` -> `\\`, space -> `\ `, and
any byte `< 0x20` or `>= 0x7f` -> `\xHH` (two uppercase hex digits). This
keeps the whole line splittable on unescaped whitespace, so
`grep 'key=value'`, `awk '{for(i=1;i<=NF;i++) ...}'`, and `sort -k2` all
work unmodified on raw output - no shell-style unquoting is needed, only
literal `\`-sequence substitution if a consumer wants the original bytes
back.

## Detector-to-Pascal-source mapping

| Detector | File | Pascal source |
|---|---|---|
| `detect_ay_container` | `detect_container.c` | `Ay_Emul.fmt` `[format+ AY]`/`[format+ AYAMAD]`; `Players.pas:8118-8126` (TypeID branch); `TAYFileHeader`, `Players.pas:220-230` |
| `detect_aym` | `detect_container.c` | `Ay_Emul.fmt` `[format+ AYM]`; `OpenAYMFile`, `Players.pas:7026-7038`; `TAYMFileHeader`, `Players.pas:245-254` |
| `detect_psg` | `detect_container.c` | `Ay_Emul.fmt` `[format+ PSG]`/`[format+ EPSG]`; `Players.pas:8130` (EPSG-as-PSG dispatch quirk) |
| `detect_ym_body`/`detect_ym` | `detect_ym.c` | `Ay_Emul.fmt` `[format+ YM2/YM3/YM3b/YM5/YM6/YM]`; `Players.pas:8132-8146`; `TYM5FileHeader`, `Players.pas:274-283`; extended-variant bit, `Players.pas:2812-2870` |
| `detect_vtx` | `detect_vtx.c` | `Ay_Emul.fmt` `[format+ VTX]`; `Players.pas:8135-8140`; `TVTXFileHeader`, `Players.pas:261-269`; Chip_Type, `Players.pas:7674-7677` |
| `detect_pt3`/`scan_whole_file_for_pt3` | `detect_pt3.c` | `Ay_Emul.fmt` `[format+ PT3]`; version digit, `Players.pas:2580-2581`; Turbosound marker, `Players.pas:2662-2665`; `ModTypes`, `Players.pas:136` |
| `detect_stc`/`detect_psc`/`detect_ftc`/`detect_gtr`/`detect_fxm`/`detect_psm`/`scan_whole_file_for_signature_trackers` | `detect_signature_trackers.c` | `Ay_Emul.fmt` `[format+ STC/PSC/FTC/GTR/FXM/PSM]` (each cited inline); Tier C scan restricted to STC/PSC/FTC/GTR only, matching `Module_Detector`'s real chain (FXM/PSM are Tier-A-only, see below) |
| `detect_sndh` | `detect_sndh.c` | `Ay_Emul.fmt` `[format+ SNDH]`; `OpenSNDHFile`, `Players.pas:7065-7080` |
| `detect_st1_structural`/`detect_st3_structural`/`detect_stp_structural` | `detect_st_family.c` | `FoundST1`, `Players.pas:4922-5065`; `FoundST3`, `Players.pas:5175-5297`; `FoundSTP`, `Players.pas:5555-5624`; `ModTypes` variants 14/16/4, `Players.pas:112-201` |
| `detect_asc1_structural`/`detect_asc0_structural`/`detect_pt2_structural`/`detect_pt1_structural`/`detect_sqt_structural` | `detect_pt_asc_family.c` | `FoundASC1`, `Players.pas:5299-5352`; `FoundASC0`, `Players.pas:5354-5406`; `FoundPT2`, `Players.pas:5785-5878`; `FoundPT1`, `Players.pas:6262-6330`; `FoundSQT`, `Players.pas:6182-6260` |
| `detect_stf_structural` (+ its depacker) | `detect_stf.c` | `FoundSTF`, `Players.pas:5408-5553`; `STFDepackInit`/`STFDepackBytes`, `Players.pas:1187-1329`; `ModTypes` variant 15, `Players.pas:202-210` |
| `detect_fls_structural` | `detect_fls.c` | `FoundFLS` (current variant, not the commented-out older one), `Players.pas:6332-6544`; `ModTypes` variant 10, `Players.pas:171-176` |
| extension table (`EXT_TABLE`) | `dispatch.c` | `Ay_Emul.fmt`'s `[fnext ...]` sections; `GetFileTypeFromFNExt` |
| Tier A/B/C dispatch (`identify`) + `scan_whole_file_module_detector` | `dispatch.c` | `Players.pas` `AddFile`, `Players.pas:8047-8146`; `Module_Detector`, `Players.pas:6830-7024` |

## Known limitations / differences from Pascal behaviour

- **The ten structural-only tracker detectors (`ST1`, `ST3`, `ASC`,
  `ASC0`, `STF`, `STP`, `PT1`, `PT2`, `SQT`, `FLS`) only check the
  candidate window anchored at file offset 0**, not Players.pas's true
  sliding scan across every byte offset in the file. `Module_Detector`'s
  real sliding scan exists to find tracker data embedded anywhere in a
  file (e.g. inside a TRD/SCL disk image, or after a BASIC loader);
  reproducing that for these ten substantially heavier structural checks
  (one of which - `STF` - runs a full custom depacker) would mean
  re-running each of them at every byte offset of a potentially large
  file, which is disproportionate for an identification tool. Standalone
  extensionless tracker files (the common real case) start their data at
  offset 0, so this still covers the vast majority of cases; the
  "embedded inside a disk image" sub-case is not detected. Tracked in
  `migration_debt.yaml` as `MIG-0023` (`state: translated` - ported and
  fuzz-tested for safety, but not oracle-diff-validated against the real
  binary. `test_corpus_76/` now contains real sample files for 7 of these
  10 formats, making that re-verification newly possible, but it has not
  been done - see MIG-0023's rationale for the current state).
- **These same ten detectors also skip the final `IntegrityCheck`
  confirmation step** Pascal performs after every structural check passes
  (a `LoadTrackerModule`+`GetTimeXXX` call that computes a playable
  duration and rejects the candidate if it comes back zero) - this
  requires the full tracker-loading/playback engine this tool
  deliberately does not implement. A small number of files that pass
  every structural check here but would still be rejected by that final
  step may be reported as detected where real Pascal (extensionless input
  only) would not.
- **`ST3`'s "Id"-prefixed variant and `STP`'s `Init_Id=0` branch skip a
  secondary tag-string comparison** (`KsaId`/`StcId`) Pascal performs in
  addition to the pointer-arithmetic checks - noted inline in
  `detect_st_family.c`. The primary structural gate (which does the actual
  discriminating work) is otherwise ported in full.
- **`Module_Detector`'s fallback for STC/PSC/FTC/GTR (when the extension is
  missing/unrecognised) is a best-effort approximation**, not a byte-for-
  byte port: the real Pascal function slides a window over the whole file
  re-running the full structural `FoundXXX` check at every offset; this
  tool instead searches directly for each signature string and checks the
  surrounding fixed-offset fields relative to where it was found. For
  every real-world case tested this is equivalent (both are looking for
  the same fixed-relative-offset byte pattern occurring anywhere in the
  file), but it is not a proven identical algorithm, so such matches are
  reported with `confidence=probable`, not `definite`. Note `PT3` gets the
  same treatment via `scan_whole_file_for_pt3` in `detect_pt3.c`.
- **`FXM` and `PSM` are Tier-A-only** (reachable only via a recognised
  `.fxm`/`.psm` extension), even though `Ay_Emul.fmt` gives them byte
  signatures: a full read of `Module_Detector`'s body confirms it never
  calls `FoundFXM`/`FoundPSM`, so the real program never content-detects
  them for extensionless input either.
- **EPSG is only ever reported as a `PSG` subtype**, matching the real
  dispatcher's actual (if arguably buggy) behaviour of never selecting
  `Ay_Emul.fmt`'s separately-named `EPSG` format at runtime - see
  `Players.pas:8130`.
- **Compressed payloads (LHA for YM/VTX, ICE for SNDH) are not
  decompressed.** Only the outer signature bytes are inspected; properties
  that live inside the compressed body (YM sub-variant when LHA-wrapped,
  SNDH's tag chain when ICE-compressed) are reported as `unknown` rather
  than guessed. This is intentional per the task's "identification and
  classification only" scope, not an oversight.
- **SNDH's tag chain (title/author/song count/replay frequency/etc.) is
  not walked.** Only the two structural signature checks (`ICE!` vs the
  fixed-offset `SNDH` magic) are performed; the variable-length tag scan
  that extracts those properties (`sndh_ExtractTextInfo`) is a
  substantially larger piece of logic already ported for playback in
  `engine/src/sndh_file.c` but deliberately not duplicated here.
- **AYM's song-index-in-filename URL syntax** (`Players.pas:8058-8068`,
  `filename:N` selecting one track of a multi-track container) is a
  playlist-UI feature, not a file-format property, and is out of scope.
- The `songs=` field for `AY`/`AYM` reports total embedded songs from the
  header count field; it is informational and not one of the task's
  required baseline fields.

## Oracle-diff verification against the real ay_emul binary

Beyond the fuzz/crash-safety testing above, this tool has been
differentially compared against a real compiled `ay_emul` via a new
`ay_emul/OracleHarness.pas` scenario (`identify_file`, gated by
`AY_EMUL_ORACLE=identify_file` - submodule-local only, never pushed, per
`PORTING_TO_C11_LINUX.md` §8.1) that drives the real `Add_Songs_From_File`
dispatcher directly and reports the resulting `PlayListItems[0]^.FileType`
name back via `GetFileType`. 78 real files (one per distinct output
permutation found in a large real-world corpus scan) were run through
both; 70/78 matched exactly, and all 8 remaining differences trace to
specific, already-documented approximations above (LHA-wrapped `.ym`
sub-variant resolution, the Tier C fallback's known imprecision for
`STC`/`PSC`/`FTC`/`GTR`/`PT3`, and the missing `IntegrityCheck` step) - see
`migration_debt.yaml` `MIG-0024` for the full breakdown, including one
real bug this comparison found and fixed: `OpenAYFile` (Players.pas:
7192-7230) does not classify a `ZXAY`+`AMAD`/`ST11` container as a
distinct `AYAMAD` format or an `AY` subtype at all - it reassigns them to
`FT.FXM`/`FT.ST1` respectively, so `detect_ay_container` now reports
`format=FXM`/`format=ST1` for those TypeIDs instead of the originally
assumed (and Ay_Emul.fmt-mime-type-driven, but wrong) `AYAMAD`/`AY;
subtype=ST11`.
