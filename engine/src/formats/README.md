# engine/src/formats/

This directory holds one C source file per chiptune module/dump format supported by the engine: tracker pattern formats (ASC, FLS, FTC, FXM, GTR, PSC, PSM, PT1, PT2, PT3, SQT, STC, STP), register-dump/replay formats (VTX, YM), the Sergey Bulba/FUXOFT `.ay` container (AY), and the Atari ST SNDH format (68000-driven, unlike the Z80/pattern-VM formats). Every file follows the same shape: a loader that parses the header/pattern data into a `<fmt>_file` struct, a duration precompute (`GetTime<FMT>`-equivalent) run once at load time, a per-tick "get registers" routine that walks pattern/opcode data and writes AY chip registers, and a `<fmt>_file_make_buffer` entry point that drives the AY synthesizer to fill an output buffer.

## asc_file.c

Parses ASC Sound Master modules (both ASC1 and the older ASC0 layout, which is normalized to ASC1's field order in-place before decoding). Implements a byte-opcode pattern interpreter driving three channels' tone/amplitude/noise/envelope state each tick, plus a `GetTimeASC`-equivalent duration precompute that walks the position list once. Title/author are extracted via a `PatternsPointer - NumberOfPositions == 72` header-consistency check shared by both variants.

Ported from: ay_emul/Players.pas (`ASC_Get_Registers`, `GetTimeASC`, `PatternInterpreter`/`GetRegisters` (ASC instance), `InitTrackerModule`'s shared FT.ASC/FT.ASC0 branch, `"else if FType = FT.ASC"`/`"FT.ASC0"` load branches).

## ay_file.c

Loads real Sergey Bulba/FUXOFT `.ay` files (TypeID `"EMUL"` only; AMAD/ST11 are rejected as unsupported). Unlike every other file in this directory, it doesn't interpret its own pattern bytecode — it reconstructs a Z80 CPU/RAM image (register state, `DumpIM1`/`DumpIM2` trampoline injection, block-copy loader) and drives it through the shared `z80_bus`/superzazu-z80 core, with AY register writes and beeper toggles captured via callbacks.

Ported from: ay_emul/Players.pas (`OpenAYFile`'s header/song-table walk, the ZRAM/register setup and `DumpIM1`/`DumpIM2` injection, a `MakeBufferAY`-equivalent playback loop).

Open migration debt: MIG-0018 (a small register-write timing offset traced to a genuine Z80.pas IM1/IM2 opcode-timing quirk in the original, intentionally not replicated in the trusted Z80 core — "won't fix", not a port bug) and MIG-0057 (the `.ay`-file digidrum/beeper investigation: `beeper_on_level` wiring and register-8/9/10 amplitude masking were fixed and validated via the MIG-0058–0062 follow-ups, but one residual open item remains — the engine's other `*_file.c` tracker loaders that call `ay_chip_set_ay_register_fast` directly were never individually audited to confirm their own amplitude values are always pre-bounded to 0-31; the mixer-level mask is a believed-sufficient but unverified backstop).

## fls_file.c

Parses FLS (SoundTracker Pro-family) modules. Unlike GTR, FLS files carry no on-disk load address, so the loader brute-force searches for the base offset that makes the header's pointer fields internally consistent, then relocates them in place. Pattern interpretation and the duration precompute both walk the same single-position-pointer pattern stream (FLS's note-skip/tempo is a single shared value, not per-channel, unlike most other formats here).

Ported from: ay_emul/Players.pas (`FLS_Get_Registers`, `GetTimeFLS`, `PatternInterpreter`/`GetRegisters` (FLS instance), `LoadTrackerModule`'s FT.FLS base-address-detection branch, `InitTrackerModule`'s FT.FLS branch).

## ftc_file.c

Parses FTC (Fuxoft Tracker) modules. Selects between three note-frequency tables (`PT3NoteTable_ST`, `FTCNoteTable2`, `ST_Table`) depending on a version digit parsed from the header and a variant byte at offset 0x32. Implements the pattern interpreter/register generator including a retrig-imitation quirk that resets an AY tone counter to avoid rewriting an identical envelope shape (a documented hardware-quirk workaround, not a bug).

Ported from: ay_emul/Players.pas (`FTC_Get_Registers`, `GetTimeFTC`, `PatternInterpreter`/`GetRegisters` (FTC instance), `InitTrackerModule`'s FT.FTC branch, `"else if FType = FT.FTC"` load branch).

## fxm_file.c

Parses FXM modules, whose on-disk layout is unique in this directory: the first 6 bytes hold a little-endian load address, and everything from byte 6 onward is copied verbatim to that address in a simulated 64KB memory image — all internal pointers are then absolute offsets with the load address already baked in, unlike GTR/FLS/PT2/PT3/STC's relocation passes. Playback is a per-channel bytecode VM with jump/call/loop opcodes and its own small call stack; the C port adds defensive bounds guards around that stack that the original Pascal (protected by range-check traps) didn't need.

Ported from: ay_emul/Players.pas (`FXM_Get_Registers`, `GetTimeFXM`/`FXM_Loop_Found`, `PatternInterpreter`/`RealGetRegisters` (FXM instance), `InitTrackerModule`'s FT.FXM branch, `LoadTrackerModule`'s FT.FXM load-address/data-copy logic).

## gtr_file.c

Parses GTR modules (fixed header with load address, name, and 15/16/32-slot sample/ornament/pattern pointer tables, relocated by subtracting the stored load address at load time). A 4-byte format-ID field's last byte (`0x10`) selects a dual-mode variant where opcode `0xE0` does not terminate an opcode walk, checked consistently in both the pattern interpreter and the duration precompute.

Ported from: ay_emul/Players.pas (`GTR_Get_Registers`, `GetTimeGTR`, `PatternInterpreter`/`GetRegisters` (GTR instance), `InitTrackerModule`'s FT.GTR branch, `LoadTrackerModule`'s FT.GTR relocation branch).

## psc_file.c

Parses PSC modules. A version byte (defaulting to 7 when the header's digit-character field is absent) gates whether ornament/sample pointers get an extra base-offset added, and channel identity (A/B/C) gates two opcodes (`0x7A` envelope, `0x7B` noise) that only channel B acts on — replicating Pascal's pointer-identity checks (`@Chan = @PlParams[CNum].PSC_B`) via an explicit `chan_id` parameter.

Ported from: ay_emul/Players.pas (`PSC_Get_Registers`, `GetTimePSC`, `PatternInterpreter`/`GetRegisters` (PSC instance), `InitTrackerModule`'s FT.PSC branch, `"else if FType = FT.PSC"` load branch).

## psm_file.c

Parses PSM modules. Its header's optional remark field is inspected for a `"psm1\0"` prefix that gets stripped when present (or the whole string dropped if it's exactly the sentinel) before being used as the title. Uniquely among the formats here, its per-tick register routine processes channels in **C, B, A** order rather than A, B, C, since channel C alone drives the shared position/pattern advance; it also tracks a distinct `finished` flag independent of the loop/stop state.

Ported from: ay_emul/Players.pas (`PSM_Get_Registers`, `GetTimePSM`, `PatternInterpreter`/`ChangeRegisters` (PSM instance), `InitTrackerModule`'s FT.PSM branch, `LoadTrackerModule`'s FT.PSM branch).

## pt1_file.c

Parses ProTracker 1 (PT1) modules — the terminator byte for pattern/position data is `255` (unlike PT2's `0`, since `0` is a valid PT1 note). Reimplements Pascal's `round()` banker's-rounding (tie-to-even) via integer division rather than a naive float truncation, and replicates Pascal's unchecked 8-bit wraparound on a note-plus-ornament-offset computation before clamping — both were root causes of real corpus divergences (see migration debt below), not stylistic choices.

Ported from: ay_emul/Players.pas (`PT1_Get_Registers`, `GetTimePT1`, `PatternInterpreter`/`GetRegisters` (PT1 instance), `InitTrackerModule`'s FT.PT1 branch, `"else if FType = FT.PT1"` load branch).

## pt2_file.c

Parses ProTracker 2 (PT2) modules — terminator byte `0` (opposite of PT1). The on-disk position count is discarded in favor of re-deriving it by scanning the position list for the first byte `>= 128`, matching Pascal's headless-loading (`PPLItem = nil`) convention rather than trusting a possibly-stale header field. Implements two glissando slide types, including an "alternative Ton_Delta" variant that derives its delta from a note-table difference and flips sign by slide direction.

Ported from: ay_emul/Players.pas (`PT2_Get_Registers`, `GetTimePT2`, `PatternInterpreter`/`GetRegisters` (PT2 instance), `InitTrackerModule`'s FT.PT2 branch, `"else if FType = FT.PT2"` load branch).

## pt3_file.c

Parses ProTracker 3 (PT3) modules, the largest and most complex format here. A version digit and a `TonTableId` byte together select among six note-frequency tables and two volume tables (values seen in real-world files span far more than a clean enum, so `TonTableId` is treated as a 4-way 0/1/2/else case, not a strict range check). Version-7+ files may carry an active "TS" (TurboSound) byte enabling dual-AY playback: a second independent voice/pattern set is loaded and driven against a second AY chip, with both the register generator and the duration precompute parametrized by voice index and, for duration, taking the shorter of the two voices' natural end.

Ported from: ay_emul/Players.pas (`PT3_Get_Registers`, `GetTimePT3`/`PatInt`, `GetNoteFreq`, `ChangeRegisters` (PT3 instance), `TrModLoaded` (TS-mode detection), `InitTrackerModule`'s FT.PT3 branch, `"else if FType = FT.PT3"` load branch).

Open migration debt: MIG-0007 (Turbosound/dual-chip mixing itself lives in engine/ay.c, not here — PT3 is the only format that actually sets `ts_mode` and drives a second chip, per MIG-0109's "Phase A" closure of the engine+PT3 half; full dual-chip *mixing* support for the general case remains tracked there, not in this file). MIG-0111 (validated, not open): a full-corpus oracle sweep of `pt3_get_time` against the real `GetTimePT3` found and fixed two real bugs — voice 0's `a11`/`a22`/`a33` step-size state was wrongly reset every position instead of persisting across the whole song, and `dl_catcher`'s 65536-row safety budget was wrongly reset per position instead of spanning the whole file — both now match `Players.pas:15613-15662`'s once-only initialization exactly.

## sndh_file.c

Loads Atari ST SNDH files: verifies the `"SNDH"` magic, rejects `"ICE!"`-packed files up front (fails loudly with a dedicated error code rather than mishandling them), and scans a bounded tag list (`TIME` per-song duration table, song-name tables, and 2-byte numeric tags for song count/current song/play frequency) up to the `"HDNS"` sentinel. Unlike every Z80-tracker format in this directory, playback hand-assembles a real 68000 machine-code image (vectors, VBL handler, TRAP #1 stub, Cookie Jar, startup trampoline) into a simulated Atari memory space and drives it tick-by-tick through the shared `atari_emulate` 68000 core, periodically flushing deferred AY register writes and dispatching the AY synthesizer on the same odometer-based cycle threshold (150000) as the original.

Ported from: ay_emul/sndh.pas (`GetNumberZ`, `GetTunesTime`, `GetTunesName`, the SNDH tag-scan/ICE-check body), ay_emul/atari.pas (`Atari_PrepEmu`, `Atari_SetDefault`, `SynthesizerSNDH`, the hand-assembled VBL/TRAP#1/Cookie-Jar setup), ay_emul/Players.pas (`OpenSNDHFile`, `MakeBufferSNDH`).

Open migration debt: MIG-0016 (the SNDH ICE depacker itself, ~180 lines of LZ-style decompression in `sndh.pas`, is not ported — detected and rejected rather than silently mishandled; no ICE-compressed file exists in the current test corpus) and MIG-0021 (umbrella entry for SNDH loading/playback — the original "AY registers never written" bug and melody/percussion timing issues are long since fixed via MIG-0045 through MIG-0055, but a small residual audio-timing jitter against the Pascal oracle remains open).

## sqt_file.c

Parses SQT modules, using heuristic base-address detection and bulk pointer relocation at load time (no on-disk load address field, similar in spirit to FLS's brute-force search). Channel processing order is **C, B, A**, unlike every other tracker format here. Its opcode dispatch is built from several small `Call_LCxxxx`-named Pascal subroutines with genuinely irregular early-exit placement, ported using literal early `break`s rather than this codebase's usual flattened if/else-with-a-`quit`-flag style, specifically to avoid silently dropping one of the original's exits. Its duration precompute is the most complex in this directory, sharing one C helper across six near-duplicate Pascal opcode-scan blocks.

Ported from: ay_emul/Players.pas (`SQT_Get_Registers`, `GetTimeSQT`, `Call_LC1D1`/`Call_LC2A8`/`Call_LC2D9`/`Call_LC283`/`Call_LC191`, `PatternInterpreter`/`GetRegisters` (SQT instance), `InitTrackerModule`'s FT.SQT branch).

## stc_file.c

Parses SoundTracker/STC modules — the oldest, simplest format here, with only a delay byte plus three pointer fields and an 18-byte name in its header, and (unlike GTR/FLS/FXM) no load-time pointer relocation needed. Pattern-ID lookup is a brute-force linear scan over fixed 7-byte pattern-table slots, both at load time and during playback position advance. Unlike most other tracker formats, its duration precompute reports no loop point (`GetTimeSTC`'s own Pascal signature has no `Lp` output parameter).

Ported from: ay_emul/Players.pas (`STC_Get_Registers`, `GetTimeSTC`, `PatternInterpreter`/`GetRegisters` (STC instance), `InitTrackerModule`'s shared FT.STC/FT.ST1/FT.ST3 branch).

## stp_file.c

Parses STP (Soundtracker Pro) modules. A `"KSA SOFTWARE COMPILATION OF "` signature check at a fixed offset gates whether a title string is read at all. Its pattern interpreter and register generator otherwise follow the same shared 96-entry `ST_Table` note-table convention used by FLS/FTC/STC.

Ported from: ay_emul/Players.pas (`STP_Get_Registers`, `GetTimeSTP`, `PatternInterpreter`/`GetRegisters` (STP instance), `InitTrackerModule`'s shared FT.STP/FT.STF branch, the KSA-signature title-read branch).

## vtx_file.c

Parses VTX files, a raw AY/YM chip-register-dump/replay format (not a pattern tracker) with short (`"AY"`/`"YM"`) and long (`"ay"`/`"ym"`) header variants, little-endian throughout (unlike the AY/YM tracker-container formats' big-endian conventions), followed by an LH5/LZH-compressed payload of 14-byte-per-tick, column-major register planes. Playback has no pattern interpretation at all: each tick it reads register `i`'s byte directly from the decompressed plane and writes it (masked per-register) to the AY chip, skipping register 13 (envelope shape) when its byte is the sentinel value 255.

Ported from: ay_emul/Players.pas (`VTX_YM3_YM3b_Get_Registers`, the VTX header/string loader body, `TVTXFileHeader`), ay_emul/AY.pas (`SynthesizerZX50`).

## ym_file.c

Parses YM files (Atari ST "YM5!"/"YM6!" chip-register-dump formats), optionally LH5/LZH-compressed. After a 34-byte big-endian header (chip/interrupt frequency, tick count, loop point, digidrum descriptor sizing) and a digidrum sample-descriptor table plus title/author/comment strings, it reads 16-byte-per-tick column-major register planes like VTX, but only supports the "extended" YM5/YM6 variant (Song_Attr bit 0 set) that adds two Atari-ST special-effect timer slots (`SE1`/`SE2`) capable of injecting square-wave, digidrum-sample, sinusoidal, or explicit-envelope modulation between register frames at a finer sub-tick rate than the register stream itself. The plain (non-extended) YM5/YM6 path and digidrum linear-to-YM-amplitude sample conversion are explicitly not ported (documented in ym_file.h as known, intentional gaps rather than silent omissions) — files needing either fail with `YM_FILE_ERR_UNSUPPORTED_TYPE`.

Ported from: ay_emul/Players.pas (`YM5i_Get_Registers`, `YM6i_Get_Registers`, `YM6_Extra_GetRegisters`, the digidrum-descriptor/string-parsing loader body), ay_emul/AY.pas (`SynthesizerYM6`), ay_emul/MainWin.pas (`Set_Chip_Frq`/`Set_Player_Frq`/`Set_MFP_Frq` frequency-derivation formulas).
