#ifndef IDENTIFY_DETECT_PT3_H
#define IDENTIFY_DETECT_PT3_H

#include "identify/common.h"

bool detect_pt3(const filebuf* f, detection* d);

/* Retained for reference/tests; no longer used by dispatch.c's
 * Module_Detector fallback - see detect_stc_structural's file comment in
 * detect_signature_trackers.c for why (Ay_Emul.fmt's match= fields are
 * dead code for identification in the real program). */
bool scan_whole_file_for_pt3(const filebuf* f, detection* d);

/* Real Module_Detector structural port: Players.pas's FoundPT3(DetectAdr)
 * (5880). See the .c file's comment above it for the exact port notes. */
bool detect_pt3_structural(const filebuf* f, bool detect_adr, detection* d);

#endif /* IDENTIFY_DETECT_PT3_H */
