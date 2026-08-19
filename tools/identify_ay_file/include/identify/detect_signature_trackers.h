/* Tracker formats whose Ay_Emul.fmt signature is a simple fixed-offset
 * byte match: STC, PSC, FTC, GTR, FXM, PSM. */
#ifndef IDENTIFY_DETECT_SIGNATURE_TRACKERS_H
#define IDENTIFY_DETECT_SIGNATURE_TRACKERS_H

#include "identify/common.h"

bool detect_stc(const filebuf* f, detection* d);
bool detect_psc(const filebuf* f, detection* d);
bool detect_ftc(const filebuf* f, detection* d);
bool detect_gtr(const filebuf* f, detection* d);
bool detect_fxm(const filebuf* f, detection* d);
bool detect_psm(const filebuf* f, detection* d);

/* Retained for reference/tests; no longer used by dispatch.c's
 * Module_Detector fallback (see the real structural ports below and
 * dispatch.c's top comment - Ay_Emul.fmt's match= fields turn out to be
 * read only by filetypes.pas's desktop-integration XML writer, never by
 * AddFile/Module_Detector). */
bool scan_whole_file_for_signature_trackers(const filebuf* f, detection* d);

/* Real Module_Detector structural ports: Players.pas's FoundSTC (5067),
 * FoundGTR (4841), FoundFTC (6090) - see the .c file's comment above each
 * for the exact port notes and the Readen1/word-wraparound conventions
 * used throughout. */
bool detect_stc_structural(const filebuf* f, detection* d);
bool detect_gtr_structural(const filebuf* f, detection* d);
bool detect_ftc_structural(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_SIGNATURE_TRACKERS_H */
