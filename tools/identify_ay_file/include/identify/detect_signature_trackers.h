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

/* Best-effort Tier C Module_Detector fallback restricted to the six
 * formats above, whose signatures are simple enough to re-test as a
 * multi-anchor whole-file substring search (see the .c file's comment for
 * exactly how this differs from Players.pas's real Module_Detector). */
bool scan_whole_file_for_signature_trackers(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_SIGNATURE_TRACKERS_H */
