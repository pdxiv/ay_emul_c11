# engine/include/ay_engine/formats/

Public headers for each chiptune module/register-dump file format the engine can load and render. Each header declares that format's load/detect/register-generation API; the paired `.c` file (in `engine/src/formats/`) holds the implementation. All are C11 ports of format-specific logic from `ay_emul/Players.pas` (the monolithic Pascal player/format module), except `sndh_file.h` which also draws on `ay_emul/atari.pas` and `ay_emul/sndh.pas`.

## asc_file.h

ASC ("ASC Sound Master") tracker module format (FT.ASC / FT.ASC0), including the older ASC0 on-disk variant that gets converted to ASC1's layout at load time.

Ported from: ay_emul/Players.pas (ASC_Get_Registers, InitTrackerModule's FT.ASC/FT.ASC0 branch, LoadTrackerModule's FT.ASC0 branch).

## ay_file.h

Real Sergey Bulba/FUXOFT `.ay` container format (FT.AY, TypeID "EMUL" only — the unrelated "AMAD"/"ST11" container variants sharing the same "ZXAY" magic are rejected). Covers header/song-table lookup, ZRAM/register setup (player-stub trampoline injection, Z80 register file init), and the relative-pointer song walk.

Ported from: ay_emul/Players.pas (OpenAYFile, the header/song-table walk, and the ZRAM/register setup).

## fls_file.h

FLS ("Flash Tracker") module format (FT.FLS), including its heuristic base-address detection at load time (FLS carries no explicit on-disk load-address field, unlike GTR).

Ported from: ay_emul/Players.pas (FLS_Get_Registers, InitTrackerModule's FT.FLS branch, LoadTrackerModule's FT.FLS branch).

## ftc_file.h

FTC ("Fasttracker Compiled") module format (FT.FTC), including its load-time Version-selection logic that picks the note table used by GetNoteFreq.

Ported from: ay_emul/Players.pas (FTC_Get_Registers, InitTrackerModule's FT.FTC branch, the Version-selection logic).

## fxm_file.h

FXM ("Fuxoft AY Music") module format (FT.FXM), including its distinctive 6-byte on-disk load-address prefix (applied as a copy-destination offset, unlike other formats ported so far).

Ported from: ay_emul/Players.pas (FXM_Get_Registers, InitTrackerModule's FT.FXM branch, LoadTrackerModule's FT.FXM-specific handling).

## gtr_file.h

GTR (Sam Coupe/ZX "Globe Tracker", pattern tag FT.GTR) module format, including its load-time pointer-normalization step.

Ported from: ay_emul/Players.pas (GTR_Get_Registers, InitTrackerModule's FT.GTR branch, LoadTrackerModule's FT.GTR pointer-normalization branch).

## psc_file.h

PSC module format (FT.PSC tag; historical name undocumented in the Pascal source), including two opcodes ($7A envelope-set, $7B noise-base-set) whose effect is version-gated, and load-time Version-selection logic.

Ported from: ay_emul/Players.pas (PSC_Get_Registers, InitTrackerModule's FT.PSC branch, the Version-selection logic).

## psm_file.h

PSM ("Pro Sound Maker") module format (FT.PSM). No load-time pointer relocation is needed for this format.

Ported from: ay_emul/Players.pas (PSM_Get_Registers, InitTrackerModule's FT.PSM branch).

## pt1_file.h

Pro Tracker 1 (PT1) module format (FT.PT1) — the oldest/simplest ZX Spectrum tracker format ported, with no sample-length envelope-slide fields, no turbosound, and always using the PT3NoteTable_ST note table. Built on the shared `ay.h` mixer, same architecture as `pt3_file.h`. Scope limited to standalone `.pt1` files (no embedded/rebased-pointer loading).

Ported from: ay_emul/Players.pas (the file-into-RAM load shared with PT3, and PT1's register-generation logic).

## pt2_file.h

Pro Tracker 2 (PT2) module format (FT.PT2), including load-time re-derivation of PT2_NumberOfPositions (scanning the position list) in this project's headless-loading convention.

Ported from: ay_emul/Players.pas (PT2_Get_Registers, InitTrackerModule's FT.PT2 branch, LoadTrackerModule's FT.PT2 branch).

## pt3_file.h

Pro Tracker 3 (PT3) module format (FT.PT3) — a genuine pattern/sample/ornament tracker engine with note-pitch tables and effect commands, not a simple register-dump format. Built on the `ay.h` mixer (no Z80 execution). Scope limited to standalone `.pt3` files loaded directly.

Ported from: ay_emul/Players.pas (PT3's register-generation and file-load logic).

## sndh_file.h

SNDH file loading/playback (FT.SNDH) for Atari ST tunes, built entirely on the ported Atari hardware layer (`atari_emulate.h`, `mfp.h`, `dma_sound.h`, `m68k_bus.h`). Covers minimal SNDH tag parsing (PlayFreq/NumberOfSongs/CurrentSong) and constructing the minimal Atari TOS boot environment (exception vector table, hand-assembled VBL handler, TRAP#1 stub, Cookie Jar, start-point trampoline) a real SNDH player expects.

Ported from: ay_emul/Players.pas and ay_emul/atari.pas (Atari_PrepMem), with tag-parsing derived from ay_emul/sndh.pas (sndh_ExtractTextInfo).

## sqt_file.h

SQT ("SQ-Tracker") module format (FT.SQT) — structurally the most complex format ported, with its PatternInterpreter modeled as a small tree of named subroutines (mirroring the Pascal original's nested-procedure closures via an explicit cursor parameter instead).

Ported from: ay_emul/Players.pas (SQT_Get_Registers and its Version-independent setup logic).

## stc_file.h

STC (SoundTracker Compiled / "Pro Tracker" ancestor) module format (FT.STC only — the older FT.ST1/FT.ST3 on-disk variants that get converted to STC's layout at load time are out of scope, as the test corpus contains no such files).

Ported from: ay_emul/Players.pas (STC_Get_Registers, InitTrackerModule's shared FT.STC/ST1/ST3 branch).

## stp_file.h

STP ("SoundTrackerPro") module format (FT.STP only — the older FT.STF on-disk variant that gets converted to STP's layout at load time is out of scope, as the test corpus contains no such files).

Ported from: ay_emul/Players.pas (STP_Get_Registers, InitTrackerModule's shared FT.STP/FT.STF branch, LoadTrackerModule's FT.STP branch).

## vtx_file.h

VTX register-dump format (FT.VTX) — lh5/LZH-compressed (via `util/lh5.h`) but without the "-lh5-"-signature TLZHFileHeader wrapper YM files use. A pure register-plane data dump with no Z80 execution and no digidrum/sinus/envelope sub-timer engine.

Ported from: ay_emul/Players.pas (the VTX header parse and register-plane decode).

## ym_file.h

YM5/YM6 register-dump format (FT.YM5/FT.YM6), the LHA-wrapped "YM Archive" style files. Built on `util/lh5.h` for decompression and the `ay.h` mixer (pure data interpretation, no Z80 execution). Captures Title/Author/Comment string content in addition to the register data.

Ported from: ay_emul/Players.pas (the LHA container/TLZHFileHeader parse and the YM5/YM6 header parse, TYM5FileHeader).
