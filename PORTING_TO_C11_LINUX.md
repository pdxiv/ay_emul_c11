# Porting Ay_Emul from Object Pascal to C11 (Linux-only)

This document assesses what it would take to port this codebase — currently
Object Pascal (FPC/Lazarus, GTK2 via the LCL) — to C11, targeting Linux only
(no Windows/macOS build targets to preserve). It's based on a direct
inspection of every source file (sizes, `uses` clauses, class/record/asm
usage, coupling between subsystems), not a general impression of the
project. All findings below are grep/read-verified against this checkout.

**Source location.** The full original Pascal program is available in this
repository as the `ay_emul` git submodule
(`git@github.com:pdxiv/ay_emul.git`) — run `git submodule update --init`
to fetch it. Every file path referenced below (`Z80.pas`, `Players.pas`,
etc.) lives under that submodule, not at the repository root.

**Scope: chiptune playback and WAV export only.** This port targets
playback of the chiptune/tracker formats `Players.pas` and friends already
emulate (AY/YM/SNDH/Atari-ST/etc.), plus exporting rendered audio to WAV.
It deliberately does **not** include playback of general audio formats
(MP3/OGG/WMA/FLAC/APE/WV/AC3/AAC/ALAC/DSD/Opus, or MOD/XM/S3M/IT tracker
modules via BASS), export to anything other than WAV, or internet-radio
streaming — all of that is BASS-only functionality in the original source
(§4.3) and is out of scope here, not deferred. This narrows the port
substantially: no third-party audio-codec dependency of any kind is
needed (§4.3, §7.3) — no BASS, and no open replacement for it either.
**MIDI playback/sequencing is out of scope too** (§4.4) — it's entirely
Windows-only in the original source (MCI/MMSystem-based), so it's not a
capability this Linux port has any obligation to reproduce, open-source or
otherwise. **So is physical Audio CD (CD-DA) playback** (§4.4) — also
entirely Windows-only in the original source (MCI-based, `CDviaMCI.pas`),
and not something this port takes on.

**Bottom line up front:** this is feasible, but large — realistically a
multi-month effort for one experienced C developer, done properly (with
tests), even scoped to Linux-only and to the reduced scope above. The
emulation/format-parsing core (~50K lines) is good news: it's
overwhelmingly procedural, not GUI-entangled, and maps onto C fairly
directly. The GUI (~13.5K lines) is the opposite: a hand-rolled,
pixel-level skinned UI built on Lazarus's RTTI-driven form system, which
has no C equivalent and would need to be redesigned, not mechanically
translated. See §8 for the recommended phased approach — build the engine
and a real CLI/headless player first, treat the GUI as a separate, later,
and possibly differently-scoped project.

**Default policy: prefer a mature third-party C library over hand-porting,
wherever it doesn't cost fidelity.** Treating the Pascal source as the
thing to transliterate everywhere is the wrong default for a
maintainability-focused rewrite: several subsystems here (the Z80 core
especially) are exactly the kind of
well-understood, extensively-reimplemented, test-suite-verifiable problem
that the wider C/emulation community has already solved multiple times
over, often better-tested than a fresh hand-port would be. §7 lays out
subsystem-by-subsystem recommendations; the one real tradeoff it runs into
is fidelity risk on the Z80/AY cores. Licensing risk on BASS specifically
isn't a tradeoff to manage — it's moot, since §4.3 drops BASS from scope
entirely rather than replacing it.

## 1. Codebase inventory

Total: **71,532 lines** of Pascal (`.pas`/`.inc`, including the
BASS-only files dropped per §4.3), excluding `.lfm` form resources
(7,812 lines across all 15 forms, 7,291 of them in-scope — declarative
GUI layout, discussed in §5) and `timedb.inc.h` (11,064 lines of data,
discussed in §4.6 — kept in scope, but with an open licensing question
that needs resolving before redistribution).

| Category | Lines | Files |
|---|---:|---|
| Core CPU/sound emulation (Z80, AY, 68000 glue) | 23,368 | `Z80.pas`, `AY.pas`, `mc68000.pas`, `Starcpu.inc` |
| Format parsers / players / decompression | 26,745 | `Players.pas`, `filetypes.pas`, `atari.pas`, `digidrum.pas`, `sndh.pas`, `sndhtimedb.pas`, `lh5.pas`, `UniReader.pas`, `Convs.pas`, `sometypes.pas` |
| ALSA / audio output / mixer | 1,854 | `digsound.pas`, `digsoundcode.pas`, `mixerctl.pas`, `fpalsa/src/asoundlib.pp` |
| GUI forms/widgets (GTK2 via LCL) | 13,531 | `MainWin.pas`, `PlayList.pas`, `Mixer.pas`, `mxhelper.pas`, `Tools.pas`, `About.pas`, `HeadEdit.pas`, `ItemEdit.pas`, `FindPLItem.pas`, `JmpTime.pas`, `ProgBox.pas`, `seldir.pas`, `SelVolCtrl.pas` |
| App infrastructure (settings, i18n, options, IPC) | 923 | `Languages.pas`, `settings.pas`, `options.pas`, `winversion.pas`, `Ay_Emul.lpr` |
| **Windows-only — drop entirely for Linux-only scope, including all MIDI and CD-Audio playback (§4.4)** | 2,385 | `Midi.pas`, `CDviaMCI.pas`, `SelectCDs.pas`, `assoc.pas` |
| **BASS wrapper + BASS-only support — drop entirely, out of scope (§4.3)** | 2,726 | `basslight.pas`, `basscode.pas`, `basstags.pas`, `encoptsedit.pas` |

The two biggest single files by far are `Players.pas` (17,151 lines — every
supported chiptune/tracker format's player logic) and `Z80.pas` (22,052
lines — the Z80 CPU core, containing almost all the project's inline
assembly, see §3.1).

Two files (`digidrum.pas`, `Languages.pas`) have no `uses` clause at all —
they depend only on the language's built-in `System` unit, i.e. they're
close to pure algorithmic/data code already.

`MainWin.pas`, `Players.pas`, `PlayList.pas`, and `Convs.pas` (all counted
as in-scope, in the rows above) each contain their own small
`{$IFDEF Windows}`-guarded MIDI-handling fragments referencing `Midi.pas`'s
types, and (along with `filetypes.pas`) similar guarded fragments
referencing `CDviaMCI.pas`'s CD-Audio-playback types — both already inert
on Linux, and neither ported along with the rest of those files; see §4.4.

`Convs.pas` (in the "format parsers" row above) is counted as in-scope in
full, but ~125 of its 1,382 lines (`BASS_Converter`,
`BASS_Encoder_Options_Editor`) are BASS-only and drop along with the rest
of §4.3 — the remainder (`WAV_Converter` and the native `PSG_Converter`/
`VTX_Converter`/`YM6_Converter`/`ZXAY_Converter` chiptune-format
converters, plus string/encoding helpers) is unrelated to BASS and stays
in scope.

## 2. Coupling: the engine is much less GUI-entangled than the `uses` clauses suggest

At first glance the core emulation units look tangled up with the GUI:
`Z80.pas` and `AY.pas` both `uses MainWin` (the main GTK form's unit), and
`Players.pas`, `PlayList.pas` all pull in `Controls`/`Forms`/LCL units.

Checked directly, though, actual GUI *API calls* from the engine files are
minimal:

| File | Direct GUI API calls (`FrmMain.`, `Application.`, `ShowMessage`, `TForm`/`TButton`/etc., `Canvas.`) |
|---|---:|
| `Z80.pas` (22,052 lines) | 4 |
| `AY.pas` (1,210 lines) | 0 |
| `Players.pas` (17,151 lines) | 2 |
| `atari.pas` (1,729 lines) | 1 |
| `mc68000.pas`, `digidrum.pas`, `sndh.pas`, `basstags.pas`, `lh5.pas`, `UniReader.pas` | 0 each |
| `Convs.pas` (1,382 lines) | 17 — all `Application.ProcessMessages` (pumping the GTK event loop during long-running format conversions, so the UI doesn't freeze) |

In other words: `uses MainWin` in the CPU/sound core is there for a couple
of incidental global flags/callbacks (e.g. `Z80.pas` calls
`FrmMain.Set_Chip_Frq(...)` once), not deep entanglement. This is the most
important finding for scoping the port: **the emulation and format-parsing
core can realistically be extracted into a GUI-free C library**, with the
handful of real touch points (the `Application.ProcessMessages` calls in
`Convs.pas`, the couple of `FrmMain.` calls) replaced by a callback/hook
interface. The main custom-painted window (`MainWin.pas`) is a different
story — see §5.

## 3. Language-level porting concerns

### 3.1 Inline assembly — concentrated, not spread out

```
Z80.pas:  238 inline `asm` blocks   (x86, `{$asmmode intel}`)
AY.pas:     3 inline `asm` blocks
(zero elsewhere)
```

This is almost entirely in the Z80 CPU core's instruction dispatch, which
is written for speed using hand-tuned x86 assembly. This is the highest
*technical-risk* single item in the port:

- Transliterated by hand, these need to become either (a) equivalent C11
  doing the same bit manipulation and letting GCC/Clang optimize it, or
  (b) GCC/Clang extended inline `asm` blocks with the same logic carried
  over. Either way, every one of the 238 blocks needs individual attention
  and correctness testing (flag behavior, cycle counts) — not mechanical,
  and the single most labor- and risk-intensive item in this entire port
  if done this way.
- **Decided instead: don't hand-port this file — replace it with
  [superzazu/z80](https://github.com/superzazu/z80)** (single-file C,
  MIT-licensed). This is exactly the kind of well-understood,
  exhaustively-reimplemented problem (ZEXALL/ZEXDOC test suites exist
  specifically to pin down Z80 behavior, including undocumented
  flags/opcodes) where a fresh hand-port is more likely to introduce subtle
  bugs than to avoid them. See §7.1 for the fidelity/compatibility gate
  that must still be run before integration, and why this preference
  doesn't apply equally to every subsystem.

### 3.2 The precompiled 68000 core: technically free to reuse, but we're replacing it with Musashi

`Starcpu.inc` declares `external` functions (`s68000init`, `s68000exec`,
`s68000GetContext`, etc.) backed by prebuilt object files already checked
into the repo: `Starcpu64L.o` (Linux/x86_64), `Starcpu32L.o`,
`Starcpu64W.o`/`Starcpu32W.o` (Windows, irrelevant here). This is the
**Starscream 68000 core**, originally written in x86 assembly, used for
Atari ST emulation (`atari.pas`, `sndh.pas`).

Checked with `nm`: the object file exports both underscore-prefixed and
plain symbol names (`_s68000init`, `s68000init`, `s68000init_`), i.e. it's
already a stable C-callable ABI, and reusing `Starcpu64L.o` unchanged would
technically be free — no license file for it ships in this repo, though
(only an author credit in `credits.txt`), and that's disqualifying for a
rewrite intended to have clean, verifiable dependency licensing.

**Decision for this port: don't reuse Starscream — replace it with
[Musashi](https://github.com/kstenerud/Musashi)** (Karl Stenerud's 68000
core, plain C, unambiguously MIT-licensed, widely used and actively
maintained). This costs the real integration work that reusing the `.o`
would have avoided, but removes the license-provenance question entirely
instead of leaving it as an open risk. See §7.1 for the full comparison
and what the Musashi integration needs to preserve from `Starcpu.inc`'s
existing interface.

### 3.3 Very little "real" OOP — good news for a structural C port

Only **30 `class` types** are declared in the entire 71K-line codebase, and
they break down as:

- **17** are `TForm` descendants — i.e. one per GUI dialog, mechanically
  tied to a `.lfm` resource (see §5). Not really "business" classes.
- **3** are `TThread` descendants (`digsoundcode.pas`, `mixerctl.pas`,
  `winversion.pas` — the fourth, in `Midi.pas`, is Windows-only) —
  translate directly to `pthread_create` + a worker function.
- **4** (`TSensZone`, `TButtZone`, `TLedZone`, `TMoveZone`, all in
  `MainWin.pas`) are plain `TObject` helper classes for the skinned UI's
  mouse-hit-testing zones — straightforward to flatten into C structs +
  functions.
- **1** (`TPlayList`, in `PlayList.pas`) is a custom `TPanel` descendant —
  the hand-drawn playlist grid control. This is real custom-widget logic
  and needs real design work in the GTK/Cairo port (§5).
- The remaining **5** are `Exception` descendants (`EBASSError`,
  `ERegistryError`, `EInvalidCompressedData`, `EMultiMediaError`,
  `EFileStructureError`, `EAddFileError` — trivially become error-code
  enums).

There is no real inheritance depth, no interfaces (`= interface` appears
nowhere in the codebase), no operator overloading, and generics are used
in exactly one place and are dead code in practice (`TFPIntList =
TFPList;//specialize TFPGList<Integer>;` — the generic form is commented
out). **This is a procedural, global-state-driven codebase wearing a thin
layer of Pascal OOP, not an OOP codebase** — which is the main reason a C
port is realistic at all rather than a ground-up rewrite.

### 3.4 Records, dynamic arrays, sets — direct C equivalents

- **79 `record` type declarations** — map ~1:1 onto C `struct`s (Pascal
  records and C structs have the same layout model; variant/`case` records,
  if any turn up during the actual port, become C `union`s).
- **60 `array of T` (dynamic array) declarations** — become
  pointer + explicit length (or a tiny growable-array helper used
  consistently), same pattern C code uses anyway.
- **Only 3 `set of` declarations** in the whole codebase — trivially become
  bitmask `unsigned` values with `enum`-based bit constants.
- **`TStringList`/`TStrings` usage: 11 occurrences**, confined to 4 files
  (`digsound.pas`, `filetypes.pas`, `PlayList.pas`, `MainWin.pas`) — narrow
  enough to replace with a small custom string-list helper (or GLib's
  `GPtrArray`/`GStrv` if the port already depends on GLib via GTK) rather
  than needing a general-purpose port of FPC's `TStrings` API surface.

### 3.5 Strings: predominantly UTF-8/Ansi, not deep Unicode-API coupling

`WideString`/`UnicodeString` usage totals **22 occurrences across 11
files** — none of them dense. The codebase is written FPC-native-Linux
style: `UTF8String`/`AnsiString` with `lazutf8` helpers for UTF-8-aware
string ops (`UTF8Decode`, etc.), which is exactly the encoding model a C11
Linux port would also use (raw UTF-8 byte buffers, no wide-char layer
needed). This significantly de-risks the string-handling part of the port
relative to what you'd expect from a Windows-first codebase.

What *does* need real design work: FPC's `AnsiString`/`UTF8String` are
reference-counted, copy-on-write, and garbage-collected by the compiler
(implicit `_AnsiStrAssign` / `_AnsiStrDecRef` calls the current source
never has to think about). C11 has none of that. A C port needs an explicit
ownership convention for every string (a small ref-counted string type,
or — simpler and more idiomatic for a from-scratch C port — consistent
caller-owns/callee-copies conventions with explicit `free()`s). This is a
cross-cutting decision that affects nearly every function signature in the
port and is worth settling *before* translating any of the ~27K lines of
format-parser code that constantly slice/concatenate strings (filenames,
tags, metadata).

### 3.6 Exceptions → error codes

`try`/`except`/`finally` is used **~200 times**, concentrated in
`Players.pas` (53), `PlayList.pas` (32), `MainWin.pas` (29), `Convs.pas`
(12) — mostly wrapping file I/O and format parsing (expected: this app
opens a lot of untrusted, often malformed, music files, and instead of
validating up front it leans on Pascal exceptions for "this file is
garbage, bail out"). C11 has no exceptions. Options, roughly in order of
how well they fit this codebase:

- **Explicit error-code returns + goto-based cleanup** (idiomatic C,
  matches the existing "bail out partway through parsing" control flow
  most directly, but is the most mechanical, line-by-line translation
  work — every one of the ~200 sites needs to become an `if (err) goto
  cleanup;`).
- **`setjmp`/`longjmp`** to approximate the original "throw from deep in a
  parser, catch at the top" structure with less line-by-line rewriting,
  at the cost of C code that's harder to reason about (no RAII, manual
  unwind bookkeeping) and generally discouraged in modern C — only worth
  it if the goto-based rewrite proves too invasive for the parser-heavy
  27K-line format-support layer specifically.

Either way, this touches a large fraction of the format-parsing code and
should be settled as a project-wide convention up front, same as §3.5.

## 4. External / native dependencies

### 4.1 GTK2 (via the LCL) — no direct C path, full redesign needed

The current GUI is built on Lazarus's LCL, which is not just "GTK2
bindings" — it's a whole RTTI-driven component/property/streaming system
(published properties, `.lfm` resources deserialized at runtime, see §5).
There's no C library that provides this model. A C port means writing
direct GTK C API calls (`gtk_button_new()`, manual signal connection,
etc.).

**Decision: GTK2, matching the original, not GTK3/4.** GTK2 is EOL
upstream, which would ordinarily push new code toward GTK3/4 — but this
port's Phase 5 decision (§5/§8) is to reproduce `MainWin.pas`'s exact
skinned UI (custom `.ays`/`.bmc` skin format, non-rectangular window
shaping, hand-painted hit-testing), and Ubuntu 20.04's `libgtk2.0-dev`
is the same toolkit generation the original binary already links
against and that this port's build environment already has installed.
Matching GTK2 keeps the skin/region/hit-testing code a direct,
mechanical translation of the original's own GDK/GTK2 calls rather than
an extra translation layer on top of a differently-shaped API (GTK3's
widget/CSS model, or GTK4's, differ enough from GTK2 to make that
translation non-trivial for no real benefit here). This is a full UI
redesign relative to the LCL's RTTI/streaming model either way, not a
mechanical translation — see §5 for scope — the GTK-version choice only
affects which flavor of that redesign work looks like.

### 4.2 ALSA — a straightforward, arguably *easier* port

`digsound.pas`/`digsoundcode.pas`/`mixerctl.pas` currently go through
`fpalsa` (a hand-maintained FPC binding to `asoundlib.h`, checked into
this repo under `fpalsa/src/`, 1,854 lines including the binding itself).
In C, you'd `#include <alsa/asoundlib.h>` directly and link `-lasound` —
no binding layer needed at all. This is one of the few places where the C
port is *simpler* than the Pascal original.

### 4.3 BASS audio library — dropped entirely, out of scope for this port

**Decision: this port does not use BASS, and doesn't need a replacement for
it either.** Traced through `basslight.pas`, `basscode.pas`, `Convs.pas`,
`encoptsedit.pas`, and `filetypes.pas` to confirm exactly what BASS does in
this app, and it breaks down into two jobs, neither of which the core
emulator touches (confirmed — no BASS references from `Players.pas`, the
native engine goes straight to ALSA):

1. **Playback of non-chiptune formats** — MP3/OGG/WMA/FLAC/APE/WV/AC3/AAC/
   ALAC/DSD/Opus decode, plus MOD/XM/S3M/IT tracker-module playback via
   `BASS_MusicLoad`, plus internet-radio URL streaming. `filetypes.pas` has
   a dedicated `TFTCBASS` file-type category for exactly this — it's a
   bolt-on "also play regular music files" feature, layered on top of, not
   underneath, the chiptune engine.
2. **Exporting rendered chiptune audio to MP3/OGG/FLAC/Opus** —
   `Convs.pas`'s `BASS_Converter` (~110 lines) and
   `BASS_Encoder_Options_Editor` (~15 lines), driving BASSENC.

**This C11/Linux port only needs (a) playback of the chiptune formats this
project itself emulates (AY/YM/SNDH/Atari-ST/etc. — the whole point of the
`Players.pas` decoders) and (b) export of rendered audio to WAV.** Neither
of those needs BASS:

- Chiptune playback was never routed through BASS to begin with (§4.3
  above, confirmed) — nothing to replace here.
- WAV export already doesn't use BASS in the original source either:
  `Convs.pas`'s `WAV_Converter` (verified by reading its body) writes a
  `WaveFileHeader` struct and raw PCM buffers directly via plain buffered
  file I/O (`AssignFile`/`BlockWrite`) — it's self-contained, zero
  external dependencies, pulling PCM straight from the emulator's own
  render loop (`MakeBuffer`). The C port can do exactly the same thing:
  hand-roll a ~44-byte WAV header and stream PCM bytes after it. No
  library needed for this at all, not even something as small as
  `libsndfile` — WAV is a trivial enough container that a dependency would
  be overkill.

Given that, MP3/OGG/WMA/FLAC/APE/WV/AC3/AAC/ALAC/DSD/Opus/tracker-module
**playback**, and MP3/OGG/FLAC/Opus **export**, are all explicitly
**out of scope** for this port — not deferred, not migration debt, just
not something this Linux/C11 rewrite is trying to do. That also means
none of BASS's replacement candidates apply here either: no FFmpeg, no
`libopenmpt`, no LAME/`libvorbisenc`/`libFLAC`/`libopusenc`. The closed-source
licensing concern that would otherwise apply to BASS (confirmed via
`bass.credits.txt` — every BASS component is "all rights reserved,"
commercial redistribution requires a paid license) is moot: the dependency
is simply not carried into the port, not replaced with something
open-source equivalent.

**Source files dropped from scope as a result:** `basslight.pas` (627
lines), `basscode.pas` (874 lines) — the entire BASS wrapper — and
`encoptsedit.pas` (647 lines — the GUI dialog for configuring BASS encoder
options, meaningless without BASS). Within `Convs.pas` (1,382 lines,
otherwise in scope — see §7.5, it also holds the native `PSG_Converter`/
`VTX_Converter`/`YM6_Converter`/`ZXAY_Converter` chiptune-format converters
and general string/encoding helpers, none of which touch BASS), only
`BASS_Converter` and `BASS_Encoder_Options_Editor` (~125 lines combined)
are dropped.

### 4.4 The Windows-only files are dead weight for this port — and MIDI and CD playback are out of scope entirely

`Midi.pas`, `CDviaMCI.pas`, `SelectCDs.pas`, `assoc.pas` (2,385 lines
total) are guarded by `{$IFDEF Windows}` in `Ay_Emul.lpr` and nowhere else
in the app — confirmed they're excluded from a Linux build already. For a
Linux-only C port, don't even look at these; drop them.

**MIDI specifically is worth calling out on its own, because it isn't
confined to `Midi.pas`.** `TMIDIParams`/`PMIDIParams` and `load_midi` are
declared in `Midi.pas` (Windows-only, built on `MMSystem`/the Windows MIDI
mapper — there is no ALSA/Linux implementation of MIDI output or
sequencing anywhere in this codebase), but code that *references* them —
file-type detection, seek handling, playlist integration, UI hooks — is
threaded through `MainWin.pas` (22 references), `Players.pas` (14,
including the `load_midi` call itself), `PlayList.pas` (3), and
`Convs.pas` (5). Checked directly: every one of these is wrapped in its
own `{$IFDEF Windows}`/`{$ENDIF Windows}` block in the host file (verified
e.g. around `Players.pas`'s `load_midi` call, guarded by an `{$IFDEF
Windows}` opened earlier in the same procedure) — so a Linux build,
including the one already built in this session, already compiles with
zero MIDI code active; this isn't a new exclusion decision, just making
explicit that it's already fully inert on Linux. **For this C11 port, MIDI
playback/sequencing is out of scope**, same as the general-audio-format
BASS features (§4.3): not a gap to fill with an open MIDI library (e.g.
ALSA sequencer / `libasound`'s own MIDI API would be the natural Linux
equivalent if this were ever revisited), just not something this port is
trying to do. When translating `MainWin.pas`/`Players.pas`/`PlayList.pas`/
`Convs.pas`, the `{$IFDEF Windows}` MIDI branches in those files can be
skipped/deleted along with the rest of the Windows-only code, not ported.

**Physical Audio CD (CD-DA) playback follows the exact same pattern, and
is also out of scope.** `CDviaMCI.pas` ("Simple MCI interface... Based on
Multimedia Programmer's Reference" per its own header) implements CD
playback entirely on top of Windows's MCI API — no Linux/ALSA equivalent
exists anywhere in this codebase. As with MIDI, references to it
(`IsCDFileType`, `CDSetPosition`, `StartCD`, `CurCDNum`, etc.) are threaded
through `Players.pas` (20 references), `MainWin.pas` (13), `PlayList.pas`
(10), `Convs.pas` (5), and `filetypes.pas` (3) — checked directly, e.g. the
`IsCDFileType` check in `MainWin.pas` is wrapped in its own `{$IFDEF
Windows}` block, same as the MIDI case. So, again, a Linux build already
compiles with zero CD-playback code active, and **CD Audio playback is out
of scope for this C11 port** — not a gap needing an open equivalent (e.g.
a raw-CDDA `libcdio`-based reader), just not something this port is
trying to do. The `{$IFDEF Windows}` CD-playback branches in those four
files get skipped/deleted the same way the MIDI branches do.

### 4.5 No hidden dependency on zlib or other compression libs

`lh5.pas` (1,070 lines) is a **self-contained** LZH/LHA decompressor (used
for compressed module formats) — no external library calls, pure
algorithmic Pascal. Grepped the whole codebase for `zlib`/`libz`: zero
hits. This unit ports like any other algorithmic code — mechanical,
low-risk, easy to differentially test against the original (decompress the
same file with both, byte-compare).

### 4.6 `timedb.inc.h` is already ~C to port, but carries an open licensing question

`sndhtimedb.pas` includes `timedb.inc.h` (11,064 lines) — a table of
known-track-length data for SNDH files. Despite living in a `.pas`-adjacent
file, its actual content is already C-flavored data:

```c
 TIMEDB_ENTRY( 003cbe93,  1,     59278,                  YM), /* Unknown - OLD JOE CLARK'S BOOGIE [3144] */
```

with C-style `/* ... */` comments and a macro-call syntax. Mechanically,
porting this is close to zero-cost: write a small `TIMEDB_ENTRY(...)` C
macro (or a tiny script to reformat into a static
`struct { ... } table[] = { ... };` initializer) and reuse the file's data
lines verbatim. This single file is 15% of the codebase's line count and,
purely as a translation task, among the least of the porting concerns.

**Licensing is a separate matter, and it's not clean.** `credits.txt`
credits this file as sourced from `timedb.inc.h` in the **sc68** project
(`sourceforge.net/p/sc68`, by Benjamin Gerard). Checked directly against
sc68's project metadata: sc68 is licensed **GPLv3**. That makes this the
one *confirmed* copyleft dependency found anywhere in this codebase (in
contrast to Starscream, §3.2, where the concern is an *absence* of license
information, not a confirmed copyleft one) — carrying this file into the
port as-is means carrying GPLv3 obligations for at least this component,
which may not be compatible with how the rest of a C rewrite is intended
to be licensed.

**Decision for this port: keep `timedb.inc.h` anyway, with the licensing
question left open as a known, tracked issue to resolve later** — not
resolved now, and not silently ignored either. Concretely, that means:
before this port is released or redistributed under terms that assume
permissive/non-copyleft licensing, someone needs to either (a) get a
relicense/permission grant from Benjamin Gerard, (b) rebuild an equivalent
duration database independently from the underlying facts (track lengths
aren't themselves copyrightable; sc68's specific curated/expressed file is
what's GPL'd), or (c) accept GPLv3 for this file and whatever that implies
for the rest of the binary it's linked into. `credits.txt` itself already
calls this database "optional," which is what makes "keep it for now,
resolve the license question later" a viable stance rather than a blocking
one — the feature can be compiled out without losing core chiptune
playback if the licensing question doesn't get resolved before it matters.

## 5. The GUI is the real project-shaped risk, not the engine

Two things push the GUI from "big mechanical translation" into "redesign
territory":

1. **RTTI-driven form loading.** Every dialog's `.lfm` file (7,812 lines
   total across 15 files) is a declarative property list
   (`Left`/`Top`/`Width`/`Caption`/event-handler-name/...) deserialized at
   runtime against the Pascal class's *published* properties — this is
   precisely the mechanism that's been breaking across Lazarus versions
   during this project's Ubuntu 20.04 build work (see
   `BUILDING_UBUNTU20.04.md` §3.3 for two real examples hit in this
   session). There is no C equivalent of this streaming system. Porting a
   form means manually writing the GTK widget-construction/layout code
   that the `.lfm` currently expresses declaratively — for the ~14 in-scope
   forms (`encoptsedit.lfm` drops with the rest of the BASS encoder-options
   dialog, §4.3), by hand, referring to the `.lfm` as a spec rather than
   translating it. `Tools.lfm` (1,612 lines) and `Mixer.lfm` (2,938 lines)
   are the biggest.

2. **`MainWin.pas` is a hand-painted, skinned UI, not a standard-widgets
   window.** Confirmed 120 `Canvas.` drawing calls in this one file, plus
   a custom `.ays`/`.bmc` skin-file format loaded at runtime
   (`LoadSkin`, embedded as the `DEFAULTSKIN` resource from `Ay_Emul2.ays`
   — see `Ay_Emul.lpi`'s resource list) and four dedicated classes
   (`TSensZone`/`TButtZone`/`TLedZone`/`TMoveZone`) implementing
   mouse-hit-testing against irregularly-shaped skin regions (see also
   `rgn.inc`/`rgn2.inc`, region-mask helpers). This is a bespoke
   Winamp-classic-style skinned-UI rendering engine. Porting it means
   reimplementing bitmap compositing and hit-testing with Cairo/GTK
   drawing primitives while preserving the `.ays`/`.bmc` skin file format
   byte-for-byte (existing skins in the wild must keep working) — real
   design and testing work, not translation.

3. `PlayList.pas`'s `TPlayList` custom control (a hand-drawn playlist grid
   with drag-drop, in-place editing, etc.) is the same category of problem
   at smaller scale (15 `Canvas.` calls).

Given all that, **the GUI is the part of this port most worth scoping
separately and deciding deliberately** — which is exactly what happened:
the headless/CLI player shipped first (phases 0-4, validating the entire
engine port against real files), and the skinned GTK UI was deliberately
scoped as its own follow-on project.

**Decision (Phase 5 kickoff): reproduce the exact skin-rendering engine,
not a simplified non-skinned first cut.** The `.ays`/`.bmc` skin format,
non-rectangular window shaping (`rgn.inc`/`rgn2.inc`), and the
`TSensZone`/`TButtZone`/`TLedZone`/`TMoveZone` hit-testing classes all get
real C11/GTK2/Cairo equivalents — see `gui/` and the Phase 5 migration-debt
entries for what's landed and what's still outstanding. The GTK-version
question this implies (§4.1) is also decided: GTK2, matching the
original, not GTK3/4.

### 5.1 A `.lfm`-to-C11-skeleton generator, for the standard dialogs specifically

For the ~14 in-scope standard dialogs (everything under point 1 above
*except* `MainWin.pas` and `PlayList.pas`'s custom control), **there should
be a Python script, run at build/port time, that parses each `.lfm` file
and emits skeleton C11 source** — this is worth doing, not just
theoretically possible. `.lfm` is a plain-text, well-structured, nested
`object`/property grammar (confirmed by direct inspection — e.g.
`ItemEdit.lfm`'s `object GBInfo: TGroupBox` / `AnchorSideLeft.Control =
Owner` / `Caption = 'Information'` blocks), not something that requires
the Lazarus RTTI machinery to *read*, only to *load at runtime*. That makes
it tractable for a small parser, with a close analogue in existing
practice (Glade `.ui` XML → C).

What the generator should be scoped to produce, per dialog:

- A `gtk_*_new()` call per component, keyed off its `.lfm` type
  (`TLabel`→`gtk_label_new`, `TEdit`→`gtk_entry_new`, `TGroupBox`→
  `gtk_frame_new`, etc.), building the widget tree in the same nesting the
  `.lfm` declares.
- Direct property-setter calls for the directly-mappable properties
  (`Caption`, `Hint`, `TabOrder`→focus chain, and similar).
- A `g_signal_connect(widget, "...", G_CALLBACK(FormCreate), ...)` stub for
  every `On*` handler name found in the `.lfm` (`OnCreate`, `OnClick`,
  etc.), with an empty matching C function generated alongside it so the
  handler bodies can be filled in by hand afterward.

What it should **not** be expected to solve, so this doesn't get oversold
as more automated than it is:

- **Layout.** `.lfm`'s positioning model is absolute `Left`/`Top`/`Width`/
  `Height` plus an anchor/`BorderSpacing` overlay (LCL's own constraint
  system) — there is no 1:1 GTK target. The generator needs an explicit,
  deliberate choice here: either emit `GtkFixed`/`GtkLayout` placement
  using the literal `.lfm` coordinates (mechanical, but non-idiomatic and
  fragile under GTK theming/DPI scaling), or translate the anchor graph
  into `GtkGrid`/`GtkBox` packing (more idiomatic, but a real translation
  problem — the anchor graph is arbitrary, not a strict row/column
  structure — not a lookup-table job). This choice should be made
  explicitly before writing the generator, not discovered mid-implementation.
  **Decided for the Phase 5 kickoff generator: `GtkFixed` with the
  `.lfm`'s literal coordinates** — simplest, and consistent with the
  skinned app's own absolute-positioning aesthetic; idiomatic
  `GtkGrid`/`GtkBox` translation remains a possible future improvement,
  not required for the dialogs landed so far.
- **Handler bodies.** `.lfm` only records handler *names*; the logic lives
  in the corresponding `.pas` file and still has to be hand-ported into the
  generated stub, same as any other application code.
- **`MainWin.pas` and `PlayList.pas`.** Neither has `.lfm`-describable
  structure to generate from — `MainWin.pas` is hand-painted via `Canvas.`
  calls against a runtime-loaded skin, and `TPlayList` is a custom-drawn
  grid control. These remain fully hand-designed work per the rest of §5;
  the generator does not reduce that scope.

Net effect: this turns phase 5's easier ~14 dialogs from "manually write
GTK construction code, using the `.lfm` as a spec" into "generate the
widget-tree-and-signal-stub skeleton, then hand-fill callback bodies and
make the layout-container decision" — a meaningful reduction of the
mechanical fraction of that work, without changing the conclusion that
`MainWin`/`PlayList` are redesign work.

## 6. Pascal → C11 construct mapping (reference table)

| Pascal construct | Prevalence | C11 equivalent |
|---|---|---|
| `unit`/`interface`/`implementation` | 43 units | `.h`/`.c` pairs |
| `class(TForm)` | 17 | Hand-written GTK widget tree + a plain `struct` for instance state |
| `class(TThread)` | 3 | `pthread_create` + worker function + `struct` for thread args |
| `class(TObject)` (plain helper classes) | 4 | `struct` + free functions |
| `class(Exception)` | 5 | `enum` error codes |
| `record` | 79 | `struct` (or `union` for variant records, if found during translation) |
| `array of T` | 60 | pointer + explicit length, or a small growable-array helper |
| `set of T` | 3 | bitmask `unsigned` + `enum` bit constants |
| `try`/`except`/`finally` | ~200 | error-code returns + `goto cleanup;`, or `setjmp`/`longjmp` for deep-parser-abort cases |
| `AnsiString`/`UTF8String` (ref-counted, GC'd) | pervasive | UTF-8 `char*` buffers via GLib `GString`/`gchar*` (see §7.4) instead of a custom ownership convention |
| `WideString`/`UnicodeString` | 22, thin | not needed as a general layer — isolate the few real Windows/Unicode-API touch points instead |
| `TStringList`/`TStrings` | 11, in 4 files | GLib `GPtrArray`/`GStrv` (see §7.4) instead of a custom string-list type |
| inline `asm` (x86, Intel syntax) | 241 (238 in `Z80.pas`) | **decided: replace `Z80.pas`'s dispatch core with [superzazu/z80](https://github.com/superzazu/z80)** (§7.1) rather than hand-transliterating; hand-porting to C/inline `asm` is the fallback if the fidelity gate fails, and the highest-risk item in the port if it comes to that |
| `external name '...'` + linked `.o` | `Starcpu*.o` (Starscream 68000 core) | Not reused — replaced with **Musashi** (MIT-licensed C 68000 core, §3.2/§7.1); Starscream's `.o` is technically C-ABI-reusable but ships with no confirmed license |
| generics (`specialize TFPGList<T>`) | 1, effectively dead code | not needed |
| `set of TShiftState` etc. (LCL-specific) | GUI-layer only | GTK/GDK modifier-mask equivalents |
| `.lfm` RTTI-streamed form resources | 7,291 lines, 14 in-scope forms (`encoptsedit.lfm` excluded, §4.3) | for the standard dialogs (all but `MainWin`/`PlayList`): a Python script generates a GTK widget-tree-and-signal-stub C skeleton from each `.lfm` (§5.1); handler bodies and the layout-container decision are still hand-work. `MainWin`/`PlayList` get no benefit from this and stay fully hand-written GTK code, using the `.lfm` as a spec. |

## 7. Prefer mature third-party C libraries over hand-porting, by default

The default assumption in §3–§6 above is "translate the Pascal into
equivalent C." For long-term maintainability that's the wrong default in
several places: this codebase reimplements a handful of extremely
well-trodden problems (a Z80 CPU, an AY/YM sound chip, LHA decompression,
several audio codecs) that the wider open-source community has already
solved, tested, and kept maintained independently of this project. A
hand-port inherits this project's bugs and gains none of that external
maintenance; a library swap gets community-maintained correctness for
free, at the cost of a compatibility/fidelity check up front. The
recommendation below is per-subsystem, not blanket, because the
fidelity-risk/payoff tradeoff is genuinely different in each case.

### 7.1 CPU cores — the highest-payoff swaps, with a real fidelity gate

| Core | Current | Recommended C library | License | Notes |
|---|---|---|---|---|
| Z80 | `Z80.pas`, 238 asm blocks (§3.1) | **Decided: [superzazu/z80](https://github.com/superzazu/z80)** (single-file C, MIT-licensed) | MIT | Its `z80_step()`-returns-cycles-taken API, and separate memory-callback / port-callback design, match this codebase's existing per-step, callback-driven model closely (`Z80_Step` already accumulates T-states per instruction into `CurrentTact`, and already swaps `ZXInProc`/`ZXOutProc`/`CPCInProc`/`CPCOutProc` per machine variant) — an opaque "run for N cycles" API would not have been a drop-in fit. **Gate to run before integration (not yet done):** pass ZEXALL/ZEXDOC, and differentially verify undocumented-flag (XF/YF) behavior against this project's own ASM-derived reference (`Z80.pas`'s flag-setting routines read real x86 EFLAGS bits to derive them — this app's demoscene-era chiptunes rely on that precision), plus confirm IM 0/1/2 interrupt-acceptance cycle counts match what `Z80_Step` currently hardcodes. NMI is unused by this codebase and needs no verification. |
| MC68000 (Atari ST) | `Starcpu*.o` — precompiled **Starscream** core (Neill Corlett, modified by Stéphane Dallongeville/Carsten Elton Sørensen — credited in `credits.txt`) | **Decided: [Musashi](https://github.com/kstenerud/Musashi)** (Karl Stenerud's 68000 core, plain C, actively maintained) | Musashi: unambiguously MIT. Starscream: ⚠️ no license file ships in this repo — only an author credit | Starscream's `.o` is technically a zero-cost, C-ABI-compatible reuse (confirmed via `nm` — see §3.2), but with no confirmed redistribution license it's not an acceptable dependency for this rewrite, so we're not using it. Musashi costs real integration work Starscream would have avoided (writing the memory/interrupt glue Musashi expects, rather than reusing `Starcpu.inc`'s existing `external` declarations almost verbatim), but that's the tradeoff for clean licensing. |
| AY/YM sound chip | `AY.pas` (`TSoundChip`, only 3 asm blocks) | `ayumi` (MIT) or `libayemu` (LGPL) | permissive | Much lower risk than the Z80 swap (small surface, no timing-critical dispatch loop) — but §3 flags a point that still applies here: this project's `Amplitudes_AY` DAC table is explicitly credited to "Hacker KAY" and is a deliberate, specific tuning choice. Adopting a library's own table changes the audio output; treat that as a conscious fidelity decision, not a transparent substitution, and consider keeping this project's table plugged into the library if it exposes that as a parameter. |

### 7.1.1 When a CPU core's raw timing disagrees with the original: the C library wins, not the oracle

This port's general rule (§8.1) is that `ay_emul` is the oracle and any
disagreement is presumed a bug in the port, not the original. **Raw CPU
instruction/interrupt timing is the one deliberate exception**, decided
explicitly during the port (not a default to assume elsewhere): superzazu/
z80 and Musashi are both mature, widely-used, independently-validated
implementations of a real, physical, precisely-documented CPU
(superzazu/z80 passes ZEXALL/ZEXDOC per §7.1's gate; Musashi is the 68000
core behind MAME's own extensive test suite). If either of Z80.pas's live
interpreter or Starscream — the two original Pascal/asm CPU cores this
port replaces — disagrees with the corresponding C library on a
standard, well-documented timing value (an opcode's cycle cost, an
interrupt-acknowledge cycle count, etc.), the presumption inverts for
that specific question: **the original Pascal CPU core is bugged, not
this port**, and the C11 side should keep the C library's correct value
rather than replicate the original's error.

Two confirmed real examples, both found by direct source comparison
rather than assumed:

- **Z80 interrupt-accept timing** (`MIG-0018`): Z80.pas's live `Z80_Step`
  charges 12 T-states for an IM1 interrupt accept and 18 for IM2;
  superzazu/z80 charges the standard, Zilog-documented 13/19. A one-line
  fix to match Z80.pas's 12/18 was written, verified to close part of a
  real oracle-diff gap, and then deliberately reverted - matching
  Z80.pas here would make this port's Z80 timing non-standard on purpose.
- **68000 opcode costs** (`MIG-0053`): Starscream's own cycle model
  charges `ADDA.L <ea>,An` (register-source) 8 cycles and Musashi charges
  the standard 6; conversely Musashi charges `CLR.W`-to-memory and
  `ADD.W`-immediate 2 cycles more than Starscream's model. Musashi's
  values match the well-known, published Motorola 68000 instruction-timing
  table; Starscream's don't, for these specific cases.

This exception is intentionally narrow: it applies to raw CPU core
timing questions only (opcode cycle costs, interrupt-acknowledge
timing, and similar CPU-internal bus-cycle accounting), decided by
checking a well-documented external reference (the Zilog/Motorola
timing tables, or the C library's own well-tested cycle tables), not a
license to second-guess `ay_emul`'s behavior generally. Every other
"the original does X, is X right?" question in this port still defaults
to matching `ay_emul` exactly, bug-for-bug if necessary (see §8.1) -
Atari MFP scheduling, AY register masking, VBL timing, and everything
else this project has spent real effort matching byte-for-byte were
each investigated and found to need matching `ay_emul`, not overriding
it. Don't extend this CPU-timing exception to other subsystems without
the same standard-reference cross-check that justified it here.

**Vendored core files themselves are never edited (explicit user
directive, still standing): `engine/third_party/musashi`'s and
`engine/third_party/z80`'s actual CPU-emulation source files
(`m68k_in.c`/`m68kops.c`/`m68kcpu.c`, `z80.c`) are never patched, full
stop** - "they're correct, and they should work." `MIG-0054` hit this
directly: a 1.34%-over-30-seconds cumulative Musashi-vs-Starscream
clock-rate drift (far larger than MIG-0053's original ~0.07% sample
suggested) was found and, at the time, left permanently unpatched at
the vendored-cycle-table level for this reason.

**Revised, narrower scope (explicit user directive, reversing the
original blanket "never override CPU timing toward Starscream" framing
- `MIG-0056`): hook-based, opt-in corrections that leave the vendored
files completely untouched are permitted.** Musashi ships a genuine,
documented integration point for exactly this - `M68K_INSTRUCTION_HOOK`
(an `m68kconf.h` config flag, not a core-logic change) plus
`m68k_set_instr_hook_callback()`, which fires once per instruction from
*inside* Musashi's normal batched `m68k_execute()` loop with no
measurable performance cost (benchmarked: ~300-310ms for a 30s render
with the hook registered and doing real per-instruction work, versus
~320-336ms unmodified - both trivial compared to the ~10s that forced
single-instruction stepping costs for the same render, MIG-0054's
earlier finding). `engine/m68k_bus.c`'s `m68k_bus_enable_starscream_
timing_override()` (off by default; `MIG-0056`) uses this hook to
correct this port's own `cycle_count` ledger - not Musashi's internal
dispatch/timing, which stays exactly as-is - toward Starscream's
declared cost for a small, explicitly enumerated set of opcode+
addressing-mode patterns already known (`MIG-0053`) to differ.

Building this actually produced a useful, humbling result: measured on
Temple_of_Asherah.sndh, the 3 known-mismatched patterns fire only 2,650
times out of 2,773,601 total instructions (0.096%) - accounting for at
most ~10,000 of the ~3,205,130-cycle gap (~0.3% of it). Comparing both
the corrected and uncorrected results against the *physically-correct*
baseline (exactly `30s * 8,000,000 Hz` cycles, not just against the
oracle) showed the oracle itself lands within +0.018% of that exact
value, while this port sits at +1.353% *before* the override and
+1.358% *after* - the override made the overcounting marginally worse,
not better. This falsifies "aggregate per-opcode cost bias" as the
drift's dominant mechanism; something else in this port's own
cycle-accounting is the real, still-unidentified cause (see
`MIG-0056`'s full writeup). The takeaway for future work: **a port-side
scheduling/accounting bug, not a CPU-core cost difference, is now the
leading suspect** - exactly the category of fix `MIG-0054`'s own two
real bugs (missing timeslice-release, stale IRQ-level snapshot) already
came from, found and fixed without touching Musashi at all.

**Cycle-accounting model and its verified invariant.** A follow-up
investigation (`MIG-0056`) traced the full cycle-budget lifecycle in
`engine/atari_emulate.c` to rule out a bookkeeping bug as the drift's
source. The model is deliberately simple: `atari_emulate.cycle_count`
(`int64_t`) is incremented in exactly two places, both in
`atari_emulate_step` - `a->cycle_count += used;` (Musashi's own honest,
unpadded `m68k_bus_exec` return value) and `a->cycle_count +=
m68k_bus_take_timing_correction();` (MIG-0056's opt-in override, always
0 unless explicitly enabled). Unlike `atari.pas`'s odometer (which
periodically rebases itself downward to dodge 32-bit overflow, per
`atari.pas:294-295`/`TraceLog.pas`'s `AbsCycle` compensation), this port
uses a plain 64-bit accumulator with no realistic overflow risk and
never resets it. **The invariant this must always satisfy**: summing
`used` across every `atari_emulate_step` call for a render must equal
that render's final `cycle_count` exactly, with zero unaccounted
remainder - verified empirically (not just by inspection) on a 30s
Temple_of_Asherah.sndh capture: `sum(used)` over 302,832 steps =
243,241,138, identically matching that run's actual final `cycle_count`.
Any future change to the scheduling loop must preserve this - if it
ever stops holding, that itself would indicate a genuine double-count
or lost-carry bug (which, per this investigation, is not currently
present).

Given that invariant holds, the ~1.34%/30s drift is **not a ledger bug**
- every cycle in the final total corresponds to real, honestly-reported
Musashi execution. The excess must come from this port genuinely doing
more real work (more instructions, more expensive ones, or more
interrupt services) than `ay_emul` does for equivalent content. MIG-0056
ruled out, by direct source comparison (not assumption) with
`atari.pas`, several strong candidates: VBL period computation (bit-
exact), the DMA-sound subsystem (provably inactive for this test file),
and both of `mfp.c`'s timer-period formulas (`get_mfp_delay`,
`calc_timer_cnt`/`set_timer_delay_mode`) including their truncation
convention (both engines truncate, neither rounds - verified against
`atari.pas:389-432` line-by-line). It also empirically quantified two
small, non-dominant real contributors: MIG-0053's `min=1` dispatch-
guarantee cap (~2.2% of the gap, confirmed via a direct A/B rebuild-and-
measure test) and the known opcode-cost-pattern mismatches (~0.3% of the
gap, from MIG-0056's own override experiment). MFP Timer A services 3.1%
more often on this port than the oracle for identical content (113,874
vs 110,472 over 30s, with assert count exactly equal to service count -
no residual coalesce/backlog) - since the ledger is proven honest, this
represents genuinely extra real interrupt-handler execution, not
phantom accounting.

**Verified Timer A state model** (MIG-0056 follow-up, using new
`AY_ENGINE_TIMERA_TRACE`/`AY_EMUL_TIMERA_TRACE` instrumentation - kept
as permanent, disabled-by-default diagnostic infrastructure alongside
this project's other trace types). Confirmed identical to `atari.pas:
394-432`, source-level: writing the data register (TAD) while the timer
is STOPPED (DM=0) immediately reloads the active counter; writing it
while RUNNING (DM<>0) only updates the value picked up on the timer's
own next natural reload - the in-flight countdown is untouched.
Changing the delay mode (control register) while already running, to a
different nonzero mode, rebases the timer's phase by the exact ratio of
old/new prescaler coefficients rather than resetting it. Transitioning
DM to/from 0 (stop/start) does reset phase. All three confirmed both by
direct source comparison and by matching live trace behavior on both
engines.

Given that model is correct, MIG-0056 partitioned a full 30s render into
Timer A "configuration epochs" (a new epoch begins whenever a register
write or post-expiration reload changes DM/DR) and compared per-epoch
expiration counts between engines, aligned by sequence order. Within
steady, unchanging-configuration epochs, per-epoch counts matched within
+/-1 and the cumulative difference across 400+ epochs stayed small and
bounded (-18..+2) - **not** growing toward the 3,402-event gap. This
rules out a steady-state per-expiration rate defect: given a fixed
configuration, this port generates expirations at exactly the oracle's
rate. The gap is instead concentrated in the 68000 program's own
periodic full stop/restart of Timer A (its digidrum-loop re-trigger,
roughly once per second) - this port completes 77 such restart cycles
in 30s versus the oracle's 74, and 3 extra cycles at ~1,451 expirations
each (~4,354 predicted) accounts for nearly all of the measured 4,320-
expiration excess confined to those spans (a much smaller ~918-event
offsetting effect exists elsewhere, unexplored). **This is a downstream
symptom, not an MFP defect**: the program's own restart-trigger logic
fires a few extra times simply because this port's overall cycle-count
reaches whatever condition it checks measurably sooner than the oracle
does per unit of real audio time - the same still-unresolved general
drift question above. Future work on the remaining gap should look
for that upstream cause (what specifically makes this port's clock run
measurably ahead - not yet found despite VBL/DMA/MFP-formula/opcode-
cost/dispatch-cap rule-outs), not at Timer A's own reload/reconfiguration
logic, which is now confirmed correct.

**Invariant this establishes for future changes**: Timer A's (and by
extension the other three MFP timers', which share the same
`set_timer_delay_mode`/`set_timer_data_register`/`calc_timer_cnt` code
path) per-configuration expiration rate must continue to match
`atari.pas` epoch-for-epoch. If a future change to `mfp.c` regresses
this, it would show up as the per-epoch cumulative drift test above
growing unboundedly rather than staying small and bounded - that
specific signature (bounded per-epoch drift vs. unbounded per-epoch
drift) is the fastest way to distinguish "a genuine new MFP timing bug"
from "the still-open upstream clock-rate question" if this investigation
is resumed.

**Upstream scheduler/timebase follow-up (MIG-0056) - the drift is system-
wide, not MFP-specific.** Tested the leading hypothesis - uncarried
instruction-boundary overshoot ("scheduler debt") - directly rather than
by inspection alone. Structurally, `atari_emulate_step` cannot lose
overshoot: every deadline (`min = base_vbl + vbl_period - od`, and each
MFP timer's `t->base + t->delay - od`) is recomputed fresh from live
`a->cycle_count` at the top of every call, and `cycle_count` is never
reset or tracked as a separate "budget" that could be clamped - any
prior overshoot is automatically folded into the next deadline for
free, unlike a `budget -= consumed; clamp at 0` pattern. Measured (not
assumed) the actual overshoot distribution across 302,832 steps of a 30s
render: positive overshoot (a call finishing its in-flight instruction
past budget) sums to 2,715,982 cycles - superficially close in order of
magnitude to the ~3.2M-cycle gap. But MIG-0053's `min=1` dispatch
guarantee - which accounts for the single largest source of small,
overshoot-prone calls (54.8% of all steps) - was already A/B tested
directly (disable it, rebuild, measure): the total changes by only
70,531 cycles (2.2% of the gap). If per-call overshoot were the dominant
mechanism, removing most of the small-budget calls should have removed
most of the 2.7M; it didn't. Overshoot is real but not dominant.

**The decisive new finding: VBL shows the identical excess rate as
Timer A and the overall cycle count.** VBL is a completely independent
subsystem from MFP Timer A - a fixed-modulus counter with no program-
visible state or opcode execution involved in its own periodicity, and
its cycle-domain period (40,000 cycles = 200Hz) is already confirmed
bit-exact. Measured its assert rate per real rendered second on a 30s
capture: oracle 5,994 asserts / 1,440,256 frames = 199.77 Hz (landing
almost exactly on the true 200Hz); this port 6,074 asserts / 1,440,000
frames = 202.47 Hz - a +1.35% excess, matching the whole-render cycle-
count excess (+1.353%) and Timer A's own service excess (+3.1%, same
direction) closely. Two structurally unrelated subsystems (VBL, MFP
Timer A) showing the same-direction, same-order-of-magnitude excess
rules out a defect specific to either one - **the excess cycle-count-
per-rendered-second is a uniform, system-wide property**, not a Timer-A/
digidrum-specific bug and not a pure per-opcode-cost artifact (which
would not manifest in VBL's own periodicity at all, since nothing about
VBL's timing depends on which opcodes ran). Per this investigation's own
divergence classification, this is a "timing-only" signature: the
machine reaches equivalent audio output at a systematically higher
elapsed-cycle cost, with no evidence of differing register/memory state
driving it.

**This points at the audio-generation mechanism (`ay_synthesizer_ay`'s
tick accumulator, `engine/ay.c`) as the most likely remaining locus**,
since if cycle_count-to-VBL-count and cycle_count-to-Timer-A-count are
both exact, the only remaining place a fixed elapsed cycle_count could
correspond to a *different* number of rendered audio samples between
engines is in the cycle-to-sample conversion itself - despite its own
ratio constant (`frq_ay_by_frq_z80` = 134,217,728, an exact power-of-2
relationship) already being confirmed bit-identical between engines.
No specific defect was confirmed there in this pass; the accumulator's
own math (`tmp := number_of_tiks + (current_tact-previous_tact)*ratio`,
committing `previous_tact` forward only on a successful, non-early-exit
update) is a monotonic linear accumulation that should be mathematically
independent of call frequency for a fixed elapsed cycle span, and no
overflow risk was found (deltas are of order 10^3-10^5, ratio ~2^27,
products stay far below `int64` range). No specific defect was
identified by inspection alone - the fix (below) was found by building
the exact call-cadence-dependence test this section calls for, not by
further reading.

**Root cause found and fixed: a packed-variant-record partial-reset,
mistranslated as a full reset.** A standalone harness (no CPU/MFP/SNDH -
just `ay_synthesizer_ay` fed a fixed 240,000,000-cycle total, split
across different call partitions) proved the accumulator's own
behavior WAS call-partition-dependent: one giant call and uniform
1-cycle calls both produced 1,440,001 frames (correct), but uniform
1000-cycle calls produced only 1,428,481 (11,520 fewer) and a call
pattern matching real `atari_emulate_step` cadence produced only
1,380,429 (59,572 fewer) - for the identical total elapsed cycles.
Comparing `engine/ay.c`'s four `ay_synthesizer_{stereo,mono}{16,8}`
functions against `AY.pas`'s four equivalent sites (lines 708, 769,
854, 901) found the exact divergence: every Pascal site ends its whole-
tick processing loop with `Tmp := 0; Number_Of_Tiks.Hi := Tmp;`, never
`Number_Of_Tiks.Re := Tmp`. `Number_Of_Tiks` is declared (`AY.pas:
143-148`) as an explicit packed variant record - `case boolean of
False:(lo,hi:longword); True:(re:int64)` - so `.Hi`/`.Lo` are literally
the upper/lower 32 bits of `.Re`. `Number_Of_Tiks.Hi := 0` clears only
the just-consumed whole-tick count, leaving `.Lo` - the fixed-point
FRACTIONAL remainder carried from the last cycle-to-ticks conversion -
completely intact for the next call. The C11 port's `e->number_of_tiks
= 0` zeroed the entire 64-bit value every time, silently discarding
that fractional remainder on nearly every call (since the backstop call
in `sndh_file_make_buffer` fires on almost every `atari_emulate_step`).
Each loss was tiny, but at real production call cadence (~300,000 calls
in a 30s render) the losses compounded into needing measurably more
elapsed CPU cycles to reach any fixed audio-frame count - the exact
mechanism behind the system-wide (VBL, Timer A, and overall) ~1.35%
excess this section had been chasing.

**Fix**: all four sites changed from `e->number_of_tiks = 0;` to
`e->number_of_tiks &= 0xFFFFFFFFLL;` (mask to the low 32 bits, exactly
reproducing `Number_Of_Tiks.Hi := 0`'s effect on the packed layout).
The genuine full-reset at `ay_engine_reset_chip` (`e->number_of_tiks =
0`, matching `AY.pas:920`'s real `Number_Of_Tiks.Re := 0` on chip
reset, not a batch-completion checkpoint) was correctly left unchanged.

**Verified effect, re-running the harness**: all four partition
strategies now produce identical results (1,440,001 frames, identical
final accumulator state) regardless of call cadence - partition
independence restored. **On the real 30s `Temple_of_Asherah.sndh`
render**: final cycle count 243,248,236 (+1.3534% over the exact
240,000,000 baseline) -> 239,999,278 (**-0.0003%** - closer to exact
than the oracle's own +0.0180%); VBL rate 202.47Hz -> 199.73Hz (oracle
199.77Hz, true 200Hz); MFP Timer A assert count 113,874 -> 110,374
(oracle 110,472); Timer A restart-cycle count 77 -> 75 (oracle 74, a
small bounded residual, not chased further); whole-clip zero-lag WAV
correlation 0.1255 -> 0.5593 with **no lag/time-warp compensation
needed at all** (previously a large, growing lag correction was
required for any meaningful correlation); 50ms-envelope correlation
0.9802. `tests/oracle_diff/run_diff.sh` (60/60) and `tools/ay_player/
smoke_test.sh` (21/21) both still pass in full.

**Verified audio timebase contract, invariants future changes must
preserve**:
- `engine/ay.c`'s `ay_engine.number_of_tiks` is a 64-bit fixed-point
  accumulator mirroring `AY.pas`'s packed `Number_Of_Tiks` variant
  record exactly: the low 32 bits are a fractional cycle-to-tick
  remainder, the high 32 bits are the whole-tick count available to
  process. **Any code that clears only the "batch consumed" whole-tick
  count (matching Pascal's `.Hi := 0`) must use `&= 0xFFFFFFFFLL`, never
  a full-value assignment to `0`** - only a genuine full chip-reset
  (matching a real `Number_Of_Tiks.Re := 0` in the Pascal source) may
  zero the whole field.
- The accumulation itself (`ay_synthesizer_ay`: `tmp := number_of_tiks +
  (current_tact - previous_tact) * frq_ay_by_frq_z80`, committing
  `previous_tact` forward only when `tmp`'s high 32 bits are nonzero)
  must remain call-partition-independent: feeding the same total
  elapsed cycle delta through one call or many small calls must produce
  the same final accumulator state and the same total emitted frames.
  The standalone `synth_harness.c`-style test (feed a fixed cycle total
  through one_call/step1/step1000/production-cadence partitions, compare
  results) is the fastest way to catch a regression in this property.
- `frq_ay_by_frq_z80` (the CPU-cycle-to-AY-tick ratio) is confirmed an
  exact power-of-2 relationship (134,217,728) with no rounding
  ambiguity for this project's fixed clock constants - do not introduce
  floating-point imprecision into this specific constant's computation.

**A real measurement pitfall, found and fixed in this investigation:
always match render FRAME COUNT, not just duration in seconds, when
comparing this port against `ay_emul`'s oracle harness.**
`OracleHarness.pas`'s `RunSNDHWAVExportTest` renders a hardcoded
`NumBuffers * BufferLen = 2813 * 512 = 1,440,256` frames, always - not
exactly `30 * SampleRate`. Every earlier capture in this investigation
compared that against this port's `ay_player --seconds=30`
(`= 1,440,000` frames exactly), 256 frames short. That mismatch alone
was responsible for roughly two-thirds of a previously-reported "Timer
A fires 98 too many times" figure and all of a previously-reported
"VBL fires 2 too few times" figure - both artifacts vanished (VBL
became an exact match; Timer A's residual dropped from 98 to 33
events) once the port was driven with `--frames=1440256` to match the
oracle's actual render length. **When comparing against the oracle,
always drive this port to the SAME exact frame count the Pascal
harness renders, not the same nominal duration in seconds** - the two
are not always equal, and the difference is large enough to look like
a real timing defect if missed.

**The remaining, much smaller residual (30s matched-frame render):
cycle count -730 vs the oracle (240,042,376 vs 240,043,106); Timer A
assert count -33 (110,439 vs 110,472); Timer D assert count -1 (5,361
vs 5,362); Timer B and VBL are exact matches.** This was traced to a
specific, non-random signature, not left as unexplained "quantization
noise": partitioning Timer A's activity into its 74 restart epochs
(matching the oracle's own 74 exactly) shows an almost universally
consistent exactly-(-1)-expiration-per-epoch pattern across the back
half of the render (roughly 35 of 74 epochs), correlated with this
port's restart-epoch START times running a small, bounded, non-growing
42-84 cycles ahead of the oracle's throughout that same stretch.
Timer D's own single missing event sits at the same kind of boundary -
both engines' Timer D activity stops permanently around cycle 123.19M
(partway through the render), and the missing event is the last one in
that shared cutoff window. The demonstrated mechanism: a small residual
phase lag between the two engines (comparable in magnitude to the
overall -730-cycle figure) occasionally pushes a timer's STOP register
write to the opposite side of a ~663-cycle period boundary from where
the oracle's equivalent write lands, dropping exactly the final
expiration of that epoch on this port's side. **This was not root-
caused to a specific CPU instruction or exception class within this
investigation** - the mechanism (period-boundary-crossing at a
program-driven STOP write, given a small non-growing residual phase
lag) is established and evidence-backed, but the source of that
remaining tens-of-cycles lag itself was not pinned down further,
consistent with this project's rule against broad CPU-timing changes
without an identified instruction-level cause. Anyone resuming this
should build the full dual-domain CPU-execution/event trace this
investigation specified but did not complete, to locate the exact
instruction or ordering difference responsible for the residual lag.

**Where the phase lag is confirmed to originate, and why it likely
stops here.** VBL itself shows zero cumulative drift right up to the
last VBL before Timer A's very first restart (both engines land on the
exact same cycle, 56,000,000, after 1,392 independent VBL periods) -
proving the residual 42-84-cycle lag is not inherited from general
clock drift, but is newly introduced somewhere in the short (~8,000-
cycle) stretch of play-routine code between that VBL and the Timer A
control-register writes. Pinning it to a specific instruction was
attempted but blocked by a real tooling gap: this project's single-step
diagnostics (both the C11 scratch tool and `OracleHarness.pas`'s
`sndh_singlestep` scenario) drive normally (real interrupts processed)
only until reaching a requested start cycle, then switch to raw
single-stepping with **no further interrupt processing** - which fails
across a sleep/wake boundary like this one (the CPU is often still
idling at STOP when the switch happens, and raw stepping can't process
the VBL/MFP interrupt that would wake it). A reliable comparison here
needs a third single-step mode - interrupt-preserving single-stepping -
that doesn't exist yet. Given the residual's magnitude (42-84 cycles,
1-3 instructions' worth) matches the same character as MIG-0053's
already-accepted Musashi-vs-Starscream opcode-cost differences, and
this project's standing policy (above) is to never patch Musashi's
cycle tables regardless of what's found, completing that trace would
most likely confirm rather than change the outcome - lowering the
priority of building the missing tooling relative to documenting this
clearly for whoever picks it up next.

### 7.2 Decompression — small, low-risk win

`lh5.pas` (1,070 lines, self-contained LZH/LHA decoder, §4.5) could be
replaced with **`lhasa`** (Simon Howard, BSD-licensed, C, purpose-built for
LZH/LHA archives). Low payoff in isolation (the existing algorithm is
already self-contained and not asm-heavy) but low risk too, and it's one
less hand-ported algorithm to maintain and validate.

### 7.3 Proprietary format codec (BASS) — dropped, no replacement needed

`basslight.pas`/`basscode.pas`/`basstags.pas`/`encoptsedit.pas` (§4.3) wrap
**BASS**, a closed-source commercial library (Un4seen Developments —
confirmed via `bass.credits.txt`: every component, including the
format-specific plugins BASSFLAC/BASSWV/BASSAPE/BASSOPUS/etc., is "All
rights reserved" copyrighted, closed-source, and requires a paid license
for most commercial distribution).

**Decided: don't carry BASS into this port, and don't replace it with an
open equivalent either — its two jobs (playback of non-chiptune formats
like MP3/OGG/WMA/FLAC/tracker-modules, and export to MP3/OGG/FLAC/Opus)
are simply out of scope for this Linux/C11 rewrite.** This port's scope is
playback of the chiptune formats `Players.pas` and friends already emulate,
plus WAV export — both of which are already BASS-free in the original
source (§4.3 traces this in detail: the native engine never called into
BASS, and `Convs.pas`'s `WAV_Converter` already hand-writes a WAV header
and raw PCM with no library at all). So there's no codec library gap to
fill here: no FFmpeg, no `libopenmpt`, no `libsndfile`, no LAME/
`libvorbisenc`/`libFLAC`/`libopusenc`. If MP3/OGG/etc. playback or non-WAV
export is ever wanted in this C11 port down the line, that's new scope to
decide on deliberately at that point (and the open-library landscape for
it is well understood — FFmpeg's `libavformat`/`libavcodec` for decode,
`libopenmpt` for tracker modules, LAME/`libvorbisenc`/`libFLAC`/
`libopusenc` for encode — but none of it is needed for what this port is
actually trying to do).

### 7.4 Strings and containers — GLib, once GTK is already a dependency

§3.5 flagged that replacing FPC's ref-counted managed strings needs an
explicit ownership convention decided project-wide before translating the
~27K-line format-parser layer. Given §5 already commits this port to GTK
(and therefore to GLib as a transitive dependency regardless of what
happens with the GUI's own timeline), the pragmatic default is to use
GLib's `GString` (growable UTF-8 string buffer) and `GPtrArray`/`GStrv`
(dynamic arrays / string lists) throughout the engine and format-parser
code too, rather than a bespoke string/list type. This directly replaces
the 11 `TStringList`/`TStrings` use sites (§3.4) and gives every other
`array of T`/string site a single, well-tested, already-present dependency
to standardize on instead of inventing one.

### 7.5 Where hand-porting remains the right call

Not everything benefits from a library swap — noting this explicitly so
the "prefer libraries" policy doesn't get over-applied:

- **The format-parser logic itself** (`Players.pas`'s per-format decoders,
  `sndh.pas`, `atari.pas`, `digidrum.pas`) — these encode this project's
  own accumulated knowledge of dozens of obscure tracker formats. There's
  no general-purpose library that plays Pro Tracker 3.xx or ASC Sound
  Master modules; this is exactly the code that makes Ay_Emul what it is,
  and it has to be hand-ported. (`basstags.pas` doesn't belong on this
  list — it's ID3/APE/Shoutcast tag reading for BASS-played files, which
  is dropped entirely along with the rest of §4.3, not hand-ported.)
- **ALSA** (§4.2) and **settings/options storage** (`options.pas` — a
  ~250-line hand-rolled flat key=value text parser, not otherwise discussed
  above) are already small and low-risk enough that adopting a library
  (e.g. `inih` for the latter) would be a wash at best; only worth it if
  the config format itself is being redesigned anyway.
- **`timedb.inc.h`** (§4.6) needs no library — it's already near-C-syntax
  data. (Its open GPLv3 licensing question, also §4.6, is a separate,
  unresolved issue — not a library-choice question, and not blocking for
  this port, since the file is being kept in scope regardless.)

## 8. Recommended phased approach

Given §2's finding that the engine is largely GUI-decoupled, and §5's
finding that the GUI is the highest-risk, lowest-mechanical-translatability
part of the codebase, the port is much lower-risk done in this order:

**Status: phases 0–4 done (see `migration_debt.yaml` for still-open
MIG-#### entries and `migration_debt_validated.yaml` for closed-out
ones — §9 explains the split); phase 5 (GUI)'s kickoff milestone done
(skinned GTK2 playback window + `.lfm`-to-C11-skeleton generator proof
of concept — see MIG-0063 through MIG-0067 for exactly what landed vs.
what remains deferred), rest of phase 5 not started.**

0. **Run the Z80 fidelity gate** (§7.1) before writing any engine code:
   confirm superzazu/z80 — already decided on — passes ZEXALL/ZEXDOC and
   matches the T-state/callback-hook shape this codebase needs. (The 68000
   core is likewise already decided — Musashi, §3.2/§7.1.) Both CPU-core
   choices are made; this step is verification, not selection, and doing
   it first avoids rework — it determines the shape of the C static
   library step 1 builds. **Done** — `tests/zexall/run_gate.sh` passes,
   cycle-exact.
1. **Core CPU/sound emulation** (Z80 via the library chosen in step 0, AY
   likewise per §7.1, 68000 via Musashi) as a standalone C static library
   with no GTK/audio-output dependency. Get correctness nailed down early
   via differential testing against the existing, working Pascal binary
   (this repo already builds and runs — see `BUILDING_UBUNTU20.04.md` —
   making it a ready-made test oracle: run both on the same input files and
   diff the generated PCM/register traces). **Done** — `engine/` (Z80,
   68000/Musashi, AY-3-8910/12, Atari MFP/DMA-sound/scheduling), oracle-
   validated (MIG-0001..0017).
2. **Format parsers** (`Players.pas` and friends, §7.5 — this part stays a
   hand-port, there's no library for it) on top of that library, validated
   the same way — decode a corpus of real files with both old and new
   binaries and byte-compare output. **Done** for AY/YM/PT3/VTX (byte-
   identical, MIG-0018..0020/0022); SNDH loads and runs but has one known-
   incomplete gap (MIG-0021 — no audible output yet, root cause
   documented). The separate `tools/identify_ay_file/` utility (format
   *identification*, not playback — MIG-0023/0024) also lives here.
3. **ALSA output** (§4.2) — genuinely simpler in C than the current
   binding-layer approach. **Done** — `tools/ay_player/src/alsa_output.c`
   (MIG-0025); no library-equivalent byte-oracle exists for audio hardware
   output, so this is verified by code review, a graceful-no-device-
   failure check, and manual listening rather than a diff gate.
4. **A minimal CLI/headless player** that exercises 1–3 end-to-end, plus
   WAV export (§4.3/§7.3 — trivial, no library: hand-write a WAV header
   and stream the same PCM the player already produces). This is the
   first point at which "the port works" becomes objectively
   demonstrable, and it's achievable without touching the GUI at all. No
   separate "codec integration" step is needed — playback is limited to
   the chiptune formats `Players.pas` already covers, and BASS (and any
   open replacement for it) is out of scope entirely, so this port never
   has a step for MP3/OGG/etc. playback or export to begin with. **Done**
   — `tools/ay_player` (`ay_player <file> [--wav=<path>] [--seconds=N]`,
   MIG-0026/0027); WAV export is oracle-diff'd byte-identical against
   `Convs.pas`'s `WAV_Converter`.
5. **GUI**, last, and treated as its own scoped effort per §5. Decided:
   **GTK2, reproducing the skinned UI exactly** (not GTK3/4, not a
   simplified standard-widget first cut). For the ~14 standard dialogs,
   started with the `.lfm`-to-C11-skeleton generator (§5.1, `GtkFixed`
   layout) to get the widget tree and signal stubs in place before
   hand-porting handler bodies; `MainWin.pas` and `PlayList.pas` get no
   benefit from the generator and are scoped as hand-design work from the
   start. **In progress** — kickoff milestone: a working skinned playback
   window (skin loading/decompression, window shaping, core playback
   controls) plus the generator run against two proof-of-concept dialogs;
   see MIG-0063 through MIG-0067 for exact scope landed vs. still
   outstanding (metadata display, drag-drop, visualizer, tray/IPC,
   seeking, the remaining dialogs, `PlayList.pas`). Ongoing, feature-by-
   feature implementation status is tracked in `PHASE5_GUI_PROGRESS.md`,
   updated as work lands rather than once at the end.

### 8.1 The `ay_emul` submodule may be extended with oracle-harness diagnostics — never patched to match the port

Step 1 above (and every later differential-testing step) depends on the
`ay_emul` submodule being a trustworthy oracle: something the C11 port is
checked *against*, not something adjusted to agree with the port. Two rules
follow from that, and both matter enough to state explicitly rather than
leave as an assumption:

- **Permitted:** adding optional, self-contained diagnostic/test code to the
  `ay_emul` submodule purely to make differential validation possible — for
  example, a new unit that drives `Z80.pas`/`AY.pas`/etc. directly with
  synthetic inputs and dumps register state or PCM to a file, wired in
  behind an environment variable or command-line flag so normal use of the
  program is completely unaffected. This is legitimate porting-support work,
  not a compromise of the oracle: the original code paths being exercised
  are untouched, only a new, additive entry point is added. Such changes are
  committed to the submodule's own local git history so the work is
  reproducible, but are not pushed upstream without a separate, explicit
  decision to do so.
- **Not permitted, under any circumstance:** changing what `Z80.pas`,
  `AY.pas`, `Players.pas`, or any other original code path *computes*, in
  order to make it agree with the C11 port. If a differential test reveals
  a mismatch, the presumption is that the C11 port has a bug — with one
  narrow, explicitly-decided exception: raw CPU core timing (§7.1.1),
  where superzazu/z80 and Musashi are trusted over Z80.pas's/Starscream's
  own cycle accounting when they disagree on a standard, documented
  timing value. That exception changes which side of the C11 port is
  presumed correct (the C library, not a from-scratch reproduction of
  the original's number) — it never means editing the Pascal submodule
  itself to match. Outside that one case, every mismatch gets recorded as
  migration debt and investigated, never resolved by editing the
  original Pascal behavior to close the gap. Doing so would silently
  destroy the oracle's value for every future comparison, and defeats
  the entire purpose of differential testing.

### 8.2 Keep C11 source files under 600 lines — split by semantic boundary, not arbitrarily

No file in the C11 port (`engine/`, `tools/`, or anywhere else new C code is
added) should exceed 600 lines. This applies regardless of how directly a
single Pascal unit maps onto a single conceptual C module — a large
original `.pas` file (many run several thousand lines) is not a license for
an equally large `.c` file on the C11 side.

When a file would otherwise cross that threshold, split it along
**semantic boundaries** — one file per format, subsystem, detector family,
or clearly-separable concern — rather than by an arbitrary line count or by
extracting unrelated helper functions just to shrink a number. Shared
types, small bounds-checked helpers, or output-formatting code that
multiple format-specific files depend on belong in their own dedicated
file (with a matching header under `include/`), not duplicated or left
in whichever file happened to need them first.

This project's own `tools/identify_ay_file/` is a concrete example of the
pattern to follow: rather than one monolithic `identify_ay_file.c`, it's
organized as `include/identify/*.h` (shared types: `filebuf`, `detection`,
`outline`, plus small bounds-checked byte-access helpers) and
`src/*.c`, one file per format family (`detect_container.c` for AY/AYM/PSG,
`detect_ym.c`, `detect_vtx.c`, `detect_pt3.c`, `detect_st_family.c`,
`detect_pt_asc_family.c`, `detect_stf.c`, `detect_fls.c`,
`detect_signature_trackers.c`, `detect_sndh.c`), with `dispatch.c` tying
the per-format detectors together and `main.c` handling only argument
parsing and output. No file in that tool exceeds ~320 lines even though
the combined detection logic is substantial. `engine/` follows the same
general layout (`engine/include/ay_engine/*.h` + `engine/src/*.c`, one
file per subsystem/format: `z80_bus.c`, `ay.c`, `atari_emulate.c`,
`ay_file.c`, `ym_file.c`, `pt3_file.c`, `sndh_file.c`, `vtx_file.c`,
`lh5.c`, etc.), but predates this rule being written down explicitly:
`engine/src/ay.c` is currently 858 lines, over the limit, and should be
split by semantic boundary (e.g. register/mixer logic vs. filter/level-
table setup vs. envelope handling) the next time it's substantially
touched, rather than treated as a precedent for new files to match. Keep
new engine/ files under the limit from the start rather than letting this
become the pattern.

## 9. On tracking this as migration debt

Per the porting conventions this workspace uses for LLM-assisted ports: if
this port is actually undertaken, it should be tracked module-by-module in
a machine-checkable ledger using the `translated` / `behaviorally_incomplete`
/ `validated` states, with a `MIG-####` id for every stub, approximated
behavior, or deferred subsystem (the GUI, if deferred per §8, would itself
be one or more open `MIG-####` entries against a headless-only build) — not
silently implied by "the CLI player works." `timedb.inc.h`'s open GPLv3
licensing question (§4.6) is exactly the kind of thing that ledger exists
for: the file is being kept in the port now, with the license question
explicitly unresolved rather than either blocking on it or quietly shipping
it as if it were clean — that's a `MIG-####` entry, not a footnote to
forget about. This document is the *feasibility survey* that such a ledger
would be scoped from; it does not itself constitute porting progress, and
no code has been changed as part of producing it.

The ledger itself is split across two files, to keep each one a workable
size as the number of entries grows: **`migration_debt.yaml`** holds every
entry currently in the `translated` or `behaviorally_incomplete` state —
i.e. everything still open — and **`migration_debt_validated.yaml`** holds
entries once they've reached `state: validated`. Closing an entry means
*moving* it from the former file to the latter (not just editing its
`state` field in place), so `migration_debt.yaml` always reflects exactly
the current open debt, and a fresh session can survey it without wading
through everything already closed out. `porting_ledger.json` (mentioned
above as an alternative ledger format this workspace's conventions allow)
was not used for this port; both ledger files here are YAML.
