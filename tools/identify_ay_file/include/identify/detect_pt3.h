#ifndef IDENTIFY_DETECT_PT3_H
#define IDENTIFY_DETECT_PT3_H

#include "identify/common.h"

bool detect_pt3(const filebuf* f, detection* d);

/* Best-effort Tier C Module_Detector fallback: PT3's signature re-tested
 * as a whole-file substring search rather than Pascal's real sliding
 * structural FoundPT3 check - see detect_signature_trackers.c's
 * scan_whole_file_for_signature_trackers for why this is equivalent for
 * simple fixed-relative-offset signatures but not proven identical. */
bool scan_whole_file_for_pt3(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_PT3_H */
