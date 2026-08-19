/* "Sound Tracker" lineage structural detectors: ST1 (uncompiled), ST3
 * (S.T. Music's Recompiler), STP (Sound Tracker Pro, compiled). Unlike
 * STC (ST1's compiled form, already covered by detect_signature_trackers.c
 * since it has a simple text signature), these three have NO byte
 * signature in Ay_Emul.fmt at all - Pascal only recognises them for
 * extensionless input via the structural pointer/table validation ported
 * here from Players.pas's FoundST1/FoundST3/FoundSTP (Players.pas:
 * 4922-5065, 5175-5297, 5555-5624).
 *
 * All three now also get the final `IntegrityCheck` confirmation step
 * (calling LoadTrackerModule + GetTimeXXX to compute a playable duration,
 * discarding the candidate if it comes back zero) - see dispatch.c's
 * confirm()/integrity_check.h. STP has its own engine/ port
 * (stp_file_load/stp_get_time, migration_debt.yaml MIG-0103) and is
 * oracle-validated the same way as every other IntegrityCheck-backed
 * format. ST1/ST3 have no engine/ port of their own - LoadTrackerModule's
 * real handling of them is itself just a conversion into FT.STC's own
 * on-disk layout (ST12STC/ST32STC), so identify_ay_file's IntegrityCheck
 * for these two reuses engine/'s existing STC support via st_convert.h
 * (MIG-0105) instead of needing a separate ST1/ST3 playback engine - and
 * unlike STP (or any other IntegrityCheck-backed format), this pair has
 * NOT been checked against a real .st1/.st3 file, since none exists
 * anywhere in this repo's test corpus; see st_convert.h's file comment
 * for the validation approach used instead. Every structural check here
 * (pointer bounds, table-size arithmetic, per-field range validation) is
 * otherwise ported faithfully for all three formats. */
#ifndef IDENTIFY_DETECT_ST_FAMILY_H
#define IDENTIFY_DETECT_ST_FAMILY_H

#include "identify/common.h"

bool detect_st1_structural(const filebuf* f, detection* d);
bool detect_st3_structural(const filebuf* f, detection* d);
bool detect_stp_structural(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_ST_FAMILY_H */
