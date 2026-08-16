# engine/src/formats/

Implementations of each chiptune module/register-dump file format's load, detect, and register-generation logic, paired with the headers in `engine/include/ay_engine/formats/`. All are C11 ports of format-specific code from `ay_emul/Players.pas`, except `sndh_file.c` which also draws on `ay_emul/atari.pas`.

## asc_file.c

Implements ASC/ASC0 tracker playback: loads the module, converts the older ASC0 on-disk layout to ASC1's at load time, and interprets patterns/samples/ornaments to generate AY register writes each tick, using the ASM_TABLE note-frequency table.

Ported from: ay_emul/Players.pas (ASM_Table, ASC_Get_Registers, InitTrackerModule's FT.ASC/FT.ASC0 branch, LoadTrackerModule's FT.ASC0 branch).

## ay_file.c

Implements real `.ay` file loading and Z80 setup: parses the big-endian header/song table via relative pointers, fills the ZRAM/register regions, injects the DumpIM1/DumpIM2 player-stub trampoline, and sets up the Z80 register file from each song's Init/Inter addresses.

Ported from: ay_emul/Players.pas (OpenAYFile's relative-pointer walk, the header/song-table lookup, and the ZRAM/register setup).

## fls_file.c

Implements FLS tracker playback, including heuristic base-address detection at load time and pattern interpretation against the ST_TABLE note table (distinct from PT3's PT3NoteTable_ST).

Ported from: ay_emul/Players.pas (ST_Table, FLS_Get_Registers, InitTrackerModule's FT.FLS branch, LoadTrackerModule's FT.FLS branch).

## ftc_file.c

Implements FTC tracker playback, including Version-selection logic folded into the load step (a load-time-derived constant), using the shared PT3NoteTable_ST note table.

Ported from: ay_emul/Players.pas (PT3NoteTable_ST, FTC_Get_Registers, InitTrackerModule's FT.FTC branch, the Version-selection logic).

## fxm_file.c

Implements FXM tracker playback, including the 6-byte on-disk load-address prefix handling (skipped and applied as a copy-destination offset) and its hardcoded FormatSpec value of 31, using the FXM_TABLE note table.

Ported from: ay_emul/Players.pas (FXM_Table, FXM_Get_Registers, InitTrackerModule's FT.FXM branch, LoadTrackerModule's FT.FXM-specific handling).

## gtr_file.c

Implements GTR tracker playback, including load-time pointer normalization, using the shared PT3NoteTable_ST note table.

Ported from: ay_emul/Players.pas (PT3NoteTable_ST, GTR_Get_Registers, InitTrackerModule's FT.GTR branch, LoadTrackerModule's FT.GTR pointer-normalization branch).

## psc_file.c

Implements PSC tracker playback, including version-gated opcode behavior ($7A envelope-set, $7B noise-base-set) and load-time Version-selection logic, using the shared ASM_TABLE note table.

Ported from: ay_emul/Players.pas (ASM_Table, PSC_Get_Registers, InitTrackerModule's FT.PSC branch, the Version-selection logic).

## psm_file.c

Implements PSM tracker playback using its own PSM_TABLE note table (distinct from PT3's and FLS/STC/STP's tables); no load-time pointer relocation needed.

Ported from: ay_emul/Players.pas (PSM_Table, PSM_Get_Registers, InitTrackerModule's FT.PSM branch).

## pt1_file.c

Implements PT1 tracker playback: pattern interpretation and register generation against a flat 65536-byte ZRAM buffer using the fixed-tick-budget cadence, using the shared PT3NoteTable_ST note table.

Ported from: ay_emul/Players.pas (PT3NoteTable_ST, the file-into-RAM load shared with PT3, and PT1's register-generation logic, PT1_Get_Registers).

## pt2_file.c

Implements PT2 tracker playback, including re-deriving PT2_NumberOfPositions at load time (scanning for values <128) rather than trusting the on-disk value, using the shared PT3NoteTable_ST note table.

Ported from: ay_emul/Players.pas (PT3NoteTable_ST, PT2_Get_Registers, InitTrackerModule's FT.PT2 branch, LoadTrackerModule's FT.PT2 branch).

## pt3_file.c

Implements PT3 tracker playback: full pattern/sample/ornament interpretation with note-pitch tables and effect commands, driving the AY mixer directly (ay_chip_set_ampl_a/b/c, ay_chip_set_mixer_register, ay_chip_set_envelope_register) with no Z80 execution.

Ported from: ay_emul/Players.pas (PT3NoteTable_ST and PT3's register-generation/file-load logic).

## sndh_file.c

Implements SNDH loading/playback on top of the ported Atari hardware layer (m68k core via Musashi, MFP, DMA sound): parses SNDH tags, constructs the minimal Atari TOS boot memory image (exception vectors, hand-assembled VBL handler, TRAP#1 stub, Cookie Jar, start-point trampoline), and drives the emulated 68000 to call the tune's INIT/PLAY routines.

Ported from: ay_emul/Players.pas and ay_emul/atari.pas (Atari_PrepMem), with tag parsing derived from ay_emul/sndh.pas (sndh_ExtractTextInfo).

## sqt_file.c

Implements SQT tracker playback: its PatternInterpreter is modeled as a set of plain functions taking an explicit `uint16_t* ptr` cursor parameter, replacing the original's nested-procedure closure design, using its own SQT_TABLE note table.

Ported from: ay_emul/Players.pas (SQT_Table, SQT_Get_Registers and its Version-independent setup logic).

## stc_file.c

Implements STC tracker playback (FT.STC only) using the ST_Table note table shared with FLS/STP.

Ported from: ay_emul/Players.pas (ST_Table, STC_Get_Registers, InitTrackerModule's shared FT.STC/ST1/ST3 branch).

## stp_file.c

Implements STP tracker playback (FT.STP only), including the pattern-pointer relocation logic (a no-op in this project's always-MAddr=0 loading convention), using the ST_Table note table shared with FLS/STC.

Ported from: ay_emul/Players.pas (ST_Table, STP_Get_Registers, InitTrackerModule's shared FT.STP/FT.STF branch, LoadTrackerModule's FT.STP branch).

## vtx_file.c

Implements VTX register-dump decoding: parses the little-endian VTX header (unlike AY/YM's big-endian convention) and lh5-decompresses the register-plane data (via `util/lh5.c`), with no separately-encoded compressed-size field.

Ported from: ay_emul/Players.pas (the VTX header parse and register-plane decode).

## ym_file.c

Implements YM5/YM6 register-dump decoding: detects and parses the LHA container/TLZHFileHeader, parses the YM5/YM6 header including the digidrum descriptor table, and scans for the Title/Author/Comment strings to locate the register-data offset.

Ported from: ay_emul/Players.pas (the LHA container/TLZHFileHeader parse, TYM5FileHeader, and the corresponding per-play setup).
