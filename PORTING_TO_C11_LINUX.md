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
etc.) — GTK2 is EOL upstream; **GTK3 or GTK4 in C** is the realistic
target for new code, not a GTK2 C rewrite. This is a full UI redesign, not
a mechanical translation — see §5 for scope.

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
separately and deciding deliberately** — e.g. shipping a correct,
well-tested headless/CLI player first (which validates the entire engine
port against real files) and treating the skinned GTK UI as its own
follow-on project, possibly even with a deliberately simpler (non-skinned)
first-cut UI rather than reproducing the exact skin-rendering engine.

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

0. **Run the Z80 fidelity gate** (§7.1) before writing any engine code:
   confirm superzazu/z80 — already decided on — passes ZEXALL/ZEXDOC and
   matches the T-state/callback-hook shape this codebase needs. (The 68000
   core is likewise already decided — Musashi, §3.2/§7.1.) Both CPU-core
   choices are made; this step is verification, not selection, and doing
   it first avoids rework — it determines the shape of the C static
   library step 1 builds.
1. **Core CPU/sound emulation** (Z80 via the library chosen in step 0, AY
   likewise per §7.1, 68000 via Musashi) as a standalone C static library
   with no GTK/audio-output dependency. Get correctness nailed down early
   via differential testing against the existing, working Pascal binary
   (this repo already builds and runs — see `BUILDING_UBUNTU20.04.md` —
   making it a ready-made test oracle: run both on the same input files and
   diff the generated PCM/register traces).
2. **Format parsers** (`Players.pas` and friends, §7.5 — this part stays a
   hand-port, there's no library for it) on top of that library, validated
   the same way — decode a corpus of real files with both old and new
   binaries and byte-compare output.
3. **ALSA output** (§4.2) — genuinely simpler in C than the current
   binding-layer approach.
4. **A minimal CLI/headless player** that exercises 1–3 end-to-end, plus
   WAV export (§4.3/§7.3 — trivial, no library: hand-write a WAV header
   and stream the same PCM the player already produces). This is the
   first point at which "the port works" becomes objectively
   demonstrable, and it's achievable without touching the GUI at all. No
   separate "codec integration" step is needed — playback is limited to
   the chiptune formats `Players.pas` already covers, and BASS (and any
   open replacement for it) is out of scope entirely, so this port never
   has a step for MP3/OGG/etc. playback or export to begin with.
5. **GUI**, last, and treated as its own scoped effort per §5 — likely
   worth a separate design decision (reproduce the skinned UI exactly, or
   ship a simpler first-cut with standard GTK widgets) rather than
   bundling it into the same estimate as 1–4. For the ~14 standard
   dialogs, start with the `.lfm`-to-C11-skeleton generator (§5.1) to get
   the widget tree and signal stubs in place before hand-porting handler
   bodies; `MainWin.pas` and `PlayList.pas` get no benefit from the
   generator and should be scoped as hand-design work from the start.

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
  a mismatch, the presumption is that the C11 port has a bug (or, per §7's
  policy discussion elsewhere in this document, that a deliberate,
  documented, and separately-justified fidelity improvement was made and
  recorded as migration debt) — never that the original Pascal behavior
  should be edited to close the gap. Doing so would silently destroy the
  oracle's value for every future comparison, and defeats the entire
  purpose of differential testing.

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
a machine-checkable ledger (`migration_debt.yaml` / `porting_ledger.json`)
using the `translated` / `behaviorally_incomplete` / `validated` states,
with a `MIG-####` id for every stub, approximated behavior, or deferred
subsystem (the GUI, if deferred per §8, would itself be one or more
open `MIG-####` entries against a headless-only build) — not silently
implied by "the CLI player works." `timedb.inc.h`'s open GPLv3 licensing
question (§4.6) is exactly the kind of thing that ledger exists for: the
file is being kept in the port now, with the license question explicitly
unresolved rather than either blocking on it or quietly shipping it as if
it were clean — that's a `MIG-####` entry, not a footnote to forget about.
This document is the *feasibility survey* that such a ledger would be
scoped from; it does not itself constitute porting progress, and no code
has been changed as part of producing it.
