# engine/include/ay_engine/formats/

Per-file-format headers for every chiptune module/register-dump format ay_engine can load and play back. Each header declares that format's own `X_file`/`X_channel` state structs, an `X_file_status` error enum, `X_file_load`, and `X_file_make_buffer` - all C11 ports of the matching `X_Get_Registers`/`InitTrackerModule`/`LoadTrackerModule` branch(es) in ay_emul/Players.pas (plus, for AY/SNDH, additional Pascal units for their own hardware-emulation cores). This is the largest and most format-specific layer of the engine; `player.h` one level up provides the shared dispatcher over all of them.

## asc_file.h

Declares `asc_file`/`asc_channel` for the "ASC Sound Master" format (both the current ASC1 on-disk layout and the older, LoopingPosition-less ASC0 variant, which `asc_file_load` shifts into ASC1's layout at load time before either is played identically). Carries `global_tick_max`/`loop_tick` (real duration precompute) and title/author metadata fields.

Ported from: ay_emul/Players.pas (ASC_Get_Registers 9709-10051; InitTrackerModule's FT.ASC/FT.ASC0 branch 3262-3367; LoadTrackerModule's FT.ASC0 shift-and-fixup branch 2383-2392).

## ay_file.h

Declares `ay_file` for real Sergey Bulba/FUXOFT `.ay` files (TypeID "EMUL" only - AMAD/ST11 containers sharing the same "ZXAY" magic are rejected). Unlike every other format here, this one drives a real embedded Z80 core (`z80_bus bus`) rather than a pattern-opcode interpreter, since `.ay` files store actual Z80 machine code/data to execute. Carries multi-song support (`song_count`, `song_index` load parameter) and author/title/comment metadata.

Ported from: ay_emul/Players.pas (header/song-table lookup 2915-2949 and OpenAYFile 7132-7236; ZRAM/register setup 3926-4008; MakeBufferAY/AY_Get_Registers 13989-14047; author/title/comment extraction 7154-7184). Deliberately not ported (see migration_debt.yaml): AMAD/ST11 TypeIDs, FadeLength-based fade-out at song end, and the AYFileEnableAutoSwitch-triggered GUI recalculation.

## fls_file.h

Declares `fls_file`/`fls_channel` for the "Flash Tracker" format (FT.FLS). Notably has no on-disk load-address field - `fls_file_load` must heuristically detect the base address (Players.pas's own base-address heuristic), unlike GTR's stored address.

Ported from: ay_emul/Players.pas (FLS_Get_Registers 11337-11521; InitTrackerModule's FT.FLS branch 3216-3260; LoadTrackerModule's FT.FLS heuristic base-address detection 2463-2532).

## ftc_file.h

Declares `ftc_file`/`ftc_channel` for the "Fasttracker Compiled" format (FT.FTC). Folds Players.pas's Version-selection logic (which note table `GetNoteFreq` uses) directly into `ftc_file_load` as a load-time-derived constant. Carries `global_tick_max`/`loop_tick` (MIG-0103) and a space-trimmed module title (MIG-0082).

Ported from: ay_emul/Players.pas (FTC_Get_Registers 10845-11174; InitTrackerModule's FT.FTC branch ~3368-3420; Version-selection 2611-2617; GetTimeFTC 15769-15887).

## fxm_file.h

Declares `fxm_file`/`fxm_channel` for "Fuxoft AY Music" (FT.FXM). Structurally distinct from the others: no per-file Delay/DelayCounter tempo gate - each channel's pattern interpreter is a small bytecode VM with jumps/calls/loop-counters and a push/pop stack (`stek[FXM_STEK_MAX]`, a fixed-size bound on Pascal's unbounded dynamic array, deliberate and documented rather than a silent truncation). Also reads a 6-byte on-disk load-address prefix that every other format here lacks.

Ported from: ay_emul/Players.pas (FXM_Get_Registers 11700-11972; InitTrackerModule's FT.FXM branch 3041-3088; LoadTrackerModule's FT.FXM load-address handling 2237,2249).

## gtr_file.h

Declares `gtr_file`/`gtr_channel` for the Sam Coupe/ZX "Globe Tracker" format (FT.GTR). Carries `global_tick_max`/`loop_tick` (MIG-0101) and a fixed-offset, space-padded module title (MIG-0082).

Ported from: ay_emul/Players.pas (GTR_Get_Registers 11523-11698; InitTrackerModule's FT.GTR branch 3090-3157; LoadTrackerModule's FT.GTR pointer-normalization branch 2534-2546; GetTimeGTR 14950-15001).

## psc_file.h

Declares `psc_file`/`psc_channel` for the "PSC" format (FT.PSC tag; historical full name not documented in Players.pas). Two behaviors called out as distinct from every other tracker format ported: opcodes $7A/$7B only take effect on channel B specifically (with an extra pattern-pointer-advance asymmetry for $7A on channel B), and the note-set opcode does NOT terminate the per-tick opcode loop (only the note-skip-count opcode does).

Ported from: ay_emul/Players.pas (PSC_Get_Registers 10053-10423; InitTrackerModule's FT.PSC branch 3516-3569; Version-selection 2600-2606; GetTimePSC 15664-15767).

## psm_file.h

Declares `psm_file`/`psm_channel` for "Pro Sound Maker" (FT.PSM). Carries a PSM-native `finished` flag (Players.pas: PSM_Parameters.Finished) which is distinct from `real_end_all` - a finished-but-not-real_end_all song keeps emitting silent frames until `global_tick_counter` reaches `global_tick_max`. Title extraction has PSM-specific "psm1\0" prefix/sentinel handling (MIG-0083).

Ported from: ay_emul/Players.pas (PSM_Get_Registers 11974-12281; InitTrackerModule's FT.PSM branch 3845-3871; GetTimePSM 16819-16881; title extraction 7532-7550).

## pt1_file.h

Declares `pt1_file`/`pt1_channel` for Pro Tracker 1 (FT.PT1), the oldest/simplest ZX tracker format ported here (no sample-length envelope-slide fields, no turbosound, always the ST note table). Scope is standalone `.pt1` files only - embedded/rebased-pointer loading is explicitly not ported (tracked debt).

Ported from: ay_emul/Players.pas (PT1_Get_Registers in full, including PatternInterpreter/GetRegisters, 11176-11335; InitTrackerModule's FT.PT1 branch 3570-3621; GetTimePT1 16679-16770; the shared file-into-RAM load 2248-2264).

## pt2_file.h

Declares `pt2_file`/`pt2_channel` for Pro Tracker 2 (FT.PT2). Notably, in this project's headless-loading convention (PPLItem=nil, MAddr=0 always), LoadTrackerModule's FT.PT2 branch always re-derives `number_of_positions` by scanning the position list itself, overwriting whatever value is on disk, while its pointer-relocation half is a no-op - both confirmed by reading the literal Pascal condition.

Ported from: ay_emul/Players.pas (PT2_Get_Registers 9091-9324; InitTrackerModule's FT.PT2 branch ~3622-3708; LoadTrackerModule's FT.PT2 branch 2313-2358; GetTimePT2 15216-15331).

## pt3_file.h

Declares `pt3_file`/`pt3_channel`/`pt3_voice` for Pro Tracker 3 (FT.PT3), the largest and most complex header in this directory. `pt3_voice` models Players.pas's `PlParams[CNum].PT3` per-voice mutable state (two voices, `voice[0]`/`voice[1]`, supporting Turbosound self-pairing dual-chip playback per MIG-0109); header-derived, voice-shared config (version, note-table id, pattern/sample/ornament pointers) lives directly on `pt3_file`, matching Players.pas's own whole-record `PLConsts[1] := PLConsts[0]` copy. Full duration precompute (`global_tick_max`/`loop_tick`, including the Turbosound-aware dual-voice walk) and natural end-of-song (`real_end_all`, the real AND of both voices' end state under Turbosound) are ported. Scope is standalone `.pt3` files only - embedded/rebased-pointer loading is out of scope since it's dead code for the real test corpus.

Ported from: ay_emul/Players.pas (PT3_Get_Registers in full 12302-12767; GetNoteFreq's 4-way/6-table dispatch 12304-12322; CheckLoopAndStop 8732-8746; InitTrackerModule's FT.PT3 branch 3717-3806; GetTimePT3 15333-15662; the file-into-RAM load 2248-2264). Deliberately not ported: playlist-level dual-FILE Turbosound pairing (two separate files chained via a .ayl playlist, Players.pas:2677-2686) - a separate, later phase per migration_debt.yaml MIG-0007.

## sndh_file.h

Declares `sndh_file` for Atari ST SNDH files (FT.SNDH), built on the separately-ported 68000/Atari hardware layer (`atari_emulate`, `mfp`, `dma_sound`, `m68k_bus`). This header's own new work is minimal SNDH tag parsing (PlayFreq/song count/TIME duration) plus the "boot a minimal Atari TOS environment" memory layout (exception vectors, a hand-assembled VBL handler, TRAP#1 stub, Cookie Jar, start trampoline) that a real SNDH player expects. `SNDH_FILE_ERR_ICE_COMPRESSED` is a real, reachable error: the ICE depacker itself is not ported (open migration debt, MIG-0016) so ICE-compressed SNDH files fail loudly rather than silently misplaying.

Ported from: ay_emul/Players.pas + ay_emul/atari.pas + ay_emul/sndh.pas (sndh_UnpackFile's non-ICE path, sndh.pas:497-509; sndh_ExtractTextInfo's playback-relevant tag scan, sndh.pas:598-835, incl. TIME per MIG-0100; Atari_PrepMem, atari.pas:1094-1315; Atari_InitEmu's MFP/DMA reset, atari.pas:1317-1385; MakeBufferSNDH, Players.pas:14049-14074).

## sqt_file.h

Declares `sqt_file`/`sqt_channel` for "SQ-Tracker" (FT.SQT), described in its own header comment as the structurally most complex format here: `SQT_Get_Registers`'s pattern interpreter is a small tree of named subroutines sharing a cursor via Pascal nested-procedure closures, modeled here as plain functions taking an explicit `uint16_t* ptr` parameter. Load-time pointer relocation is a genuine heuristic (like FLS's) based on a fixed `SQT_SamplesPointer - 10` offset.

Ported from: ay_emul/Players.pas (SQT_Get_Registers 10425-10843; LoadTrackerModule's FT.SQT pointer-relocation branch 2393-2423).

## stc_file.h

Declares `stc_file`/`stc_channel` for "SoundTracker Compiled" (FT.STC), an ancestor format of Pro Tracker. Only FT.STC's own layout is ported; the older FT.ST1/FT.ST3 on-disk variants that LoadTrackerModule converts to FT.STC's layout at load time (ST12STC/ST32STC) are out of scope since the test corpus has no .st1/.st3 files - tracked, not silently dropped.

Ported from: ay_emul/Players.pas (STC_Get_Registers 9325-9515; InitTrackerModule's shared FT.STC/ST1/ST3 branch 3160-3214; GetTimeSTC 15003-15040).

## stp_file.h

Declares `stp_file`/`stp_channel` for "SoundTrackerPro" (FT.STP). Only FT.STP's own layout is ported; the older FT.STF variant that LoadTrackerModule converts via STFDepack/STF2STP is out of scope (no .stf files in the test corpus). Title extraction requires a 28-byte "KSA SOFTWARE COMPILATION OF " signature match before a title is even present (MIG-0083).

Ported from: ay_emul/Players.pas (STP_Get_Registers 9517-9702; InitTrackerModule's shared FT.STP/FT.STF branch 3439-3488; LoadTrackerModule's FT.STP branch 2359-2382; GetTimeSTP 15184-15214).

## vtx_file.h

Declares `vtx_file` for the VTX register-dump format (FT.VTX) - lh5/LZH-compressed (via lh5.h) but without the "-lh5-" TLZHFileHeader wrapper YM files use; the compressed payload simply runs to end-of-file. The simplest register-plane format ported (no Z80 execution, no digidrum/sinus sub-timer engine). Supports both the short and long VTX header variants and AY/YM chip-type selection from the header's Id string.

Ported from: ay_emul/Players.pas (VTX header parse 7650-7707; lh5 decompression call 2736-2752; VTX_YM3_YM3b_Get_Registers 12798-12825 and MakeBufferVTX 12768-12796; PrepareToPlay's FT.VTX init and chip-type selection 4032, 7674-7677). Deliberately not ported: the sibling YM3/YM3b format sharing the same register reader but a different loader path (2790-2811) - no real test file exercises it.

## ym_file.h

Declares `ym_file` for the LHA-wrapped "YM Archive" formats YM5!/YM6! (extended-Song_Attr variant only), built on lh5.h for decompression. Includes the shared Atari-MFP-style sub-timer "extra effects" engine (`YM6_Extra_GetRegisters`, driving up to two simultaneous hardware-envelope/digidrum/sinus effect channels) and full Title/Author/Comment metadata capture (MIG-0075).

Ported from: ay_emul/Players.pas (LHA container detection + TLZHFileHeader/TYM5FileHeader parse 7708-7814; YM5i_Get_Registers 13653-13819; YM6_Extra_GetRegisters 12915-13004; SynthesizerYM6, ay_emul/AY.pas:1117-1127; YM6i_Get_Registers 13121-13385). Deliberately not ported (see migration_debt.yaml): the non-extended YM5_Get_Registers/YM6_Get_Registers variants, and digidrum sample format conversion (YMizeSample) - every real test file has Num_of_Dig=0, so the descriptor table is parsed but conversion is dead code for them (fails loudly rather than silently misplaying if ever exercised).
