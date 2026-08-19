/* MIG-0010: C11 port of ay_emul/Convs.pas's PSG_Converter (672-895),
 * the VBL2PSG branch only (778-809) - the generic per-tick All_
 * GetRegisters[CNum] dispatch, reached by every format EXCEPT the three
 * genuinely obscure raw-register-trace input formats this port has no
 * loader for at all: FT.OUT, FT.ZXAY, and FT.EPSG (NOT the same thing
 * as this port's own real .ay file support, PLAYER_FORMAT_AY/FT.AY,
 * despite the similar name - FT.ZXAY is a completely separate, distinct
 * type constant for a raw Z80-takt-timestamped register-write-log
 * format, confirmed by direct trace of filetypes.pas's own type list
 * and OUT_Get_Registers'/ZXAY_Get_Registers' file-stream-reading bodies,
 * neither of which has anything to do with Z80/68000 CPU emulation the
 * way a real tracker/AY/SNDH file's own playback does - see
 * migration_debt.yaml). Reaches all other 18 formats via player_step_
 * registers_any (the 14 Turbosound-pairing-eligible tracker formats via
 * player_step_registers, MIG-0112's own already-validated mechanism;
 * AY/YM/VTX/SNDH via their own per-format step_registers, MIG-0010
 * update - each has a genuine All_GetRegisters[CNum] entry in the real
 * Pascal too, just structurally unreachable for TSMode pairing, which
 * is a completely different question from "does it have a per-tick
 * register generator at all").
 *
 * PSG is uncompressed (a flat register-write-diff log with a simple
 * run-length "idle frames" encoding, PSG_Save_Registers/Psg_Save_
 * Ostatok) - no third-party dependency needed, unlike VTX (see
 * tools/ay_export/include/ay_export/vtx_export.h). */
#ifndef AY_ENGINE_PSG_EXPORT_H
#define AY_ENGINE_PSG_EXPORT_H

#include <stdbool.h>

#include "ay_engine/player.h"

/* Writes a .psg export of `p` (any format except PLAYER_FORMAT_UNKNOWN)
 * to `path`, driving playback forward from tick 0 to p's own natural
 * end (Global_Tick_Max) via player_step_registers_any, exactly matching
 * VBL2PSG's own `repeat All_GetRegisters[0](0); ... until (Global_Tick_
 * Counter >= Global_Tick_Max) or May_Quit` loop (there is no "May_Quit"/
 * user-cancel concept in this port - always runs to completion). Do_Loop
 * is implicitly false for the whole export (PSG_Converter's own
 * `Do_Loop := False` before converting - a natural-length export, not a
 * forced loop). Returns false for PLAYER_FORMAT_UNKNOWN or on a file-
 * write error. */
bool psg_export_write(const char* path, player* p);

/* Same, for a player_pair with TSMode active (pair->active) - writes
 * TWO files (`path` for the primary voice, `path2` for the secondary),
 * matching PSG_Converter's own VBL2PSG TSMode branch (which calls
 * PSG_Save_Registers(0) and PSG_Save_Registers(1) independently every
 * frame, each with its own FF_Counter/Prev_Regs state - i.e. two
 * completely independent PSG streams, not one interleaved one).
 * Force_Loop is honored (matching VBL2PSG's own `if not Real_End[n] or
 * Force_Loop then PSG_Save_Registers(n)` - a mismatched-length pair's
 * shorter voice keeps being recorded, looping its own pattern data,
 * past its own natural end if Force_Loop is set on it beforehand via
 * player_pair_set_force_loop; otherwise that voice's own file simply
 * stops recording once its side ends while the longer voice's file
 * keeps going). If !pair->active, writes only `path` from pair->primary
 * (identical to psg_export_write). */
bool psg_export_write_pair(const char* path, const char* path2,
                            player_pair* pair);

#endif /* AY_ENGINE_PSG_EXPORT_H */
