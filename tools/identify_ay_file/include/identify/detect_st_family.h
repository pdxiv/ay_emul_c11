/* "Sound Tracker" lineage structural detectors: ST1 (uncompiled), ST3
 * (S.T. Music's Recompiler), STP (Sound Tracker Pro, compiled). Unlike
 * STC (ST1's compiled form, already covered by detect_signature_trackers.c
 * since it has a simple text signature), these three have NO byte
 * signature in Ay_Emul.fmt at all - Pascal only recognises them for
 * extensionless input via the structural pointer/table validation ported
 * here from Players.pas's FoundST1/FoundST3/FoundSTP (Players.pas:
 * 4922-5065, 5175-5297, 5555-5624).
 *
 * These are approximations of Pascal's detectors in one specific,
 * documented way: the final `IntegrityCheck` confirmation step (calling
 * LoadTrackerModule + GetTimeXXX to compute a playable duration, discarding
 * the candidate if it comes back zero) is NOT performed, since it requires
 * the full tracker-loading/playback engine this tool deliberately does not
 * implement (see identify_ay_file.md). Every other structural check
 * (pointer bounds, table-size arithmetic, per-field range validation) is
 * ported faithfully. This means a small number of files that pass every
 * structural check here but would still be rejected by the final
 * IntegrityCheck step may be reported as detected where real Pascal (in
 * the no-extension fallback path only) would not - tracked as part of
 * MIG-0023's confidence=probable caveat for this whole fallback tier. */
#ifndef IDENTIFY_DETECT_ST_FAMILY_H
#define IDENTIFY_DETECT_ST_FAMILY_H

#include "identify/common.h"

bool detect_st1_structural(const filebuf* f, detection* d);
bool detect_st3_structural(const filebuf* f, detection* d);
bool detect_stp_structural(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_ST_FAMILY_H */
