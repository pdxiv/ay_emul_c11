/* Structural detectors with no Ay_Emul.fmt byte signature: ASC/ASC0 (ASM
 * Sound Master), PT1/PT2 (Pro Tracker 1/2), SQT (SQ-Tracker). Ported from
 * Players.pas's FoundASC1/FoundASC0/FoundPT2/FoundPT1/FoundSQT
 * (Players.pas:5299-5407, 5785-5878, 6182-6330), minus the final
 * IntegrityCheck confirmation step - see detect_st_family.h's file
 * comment for why (same rationale applies here verbatim). */
#ifndef IDENTIFY_DETECT_PT_ASC_FAMILY_H
#define IDENTIFY_DETECT_PT_ASC_FAMILY_H

#include "identify/common.h"

bool detect_asc1_structural(const filebuf* f, detection* d);
bool detect_asc0_structural(const filebuf* f, detection* d);
bool detect_pt2_structural(const filebuf* f, detection* d);
bool detect_pt1_structural(const filebuf* f, detection* d);
bool detect_sqt_structural(const filebuf* f, detection* d);

/* Real Module_Detector structural port: Players.pas's FoundPSC(PSC1_00)
 * (5969). Grouped here (rather than detect_signature_trackers.c) since
 * it's a genuine pointer/table-following structural check, same style as
 * its siblings above - not a signature match. */
bool detect_psc_structural(const filebuf* f, bool psc1_00, detection* d);

#endif /* IDENTIFY_DETECT_PT_ASC_FAMILY_H */
