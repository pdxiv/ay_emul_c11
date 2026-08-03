/* Flash Tracker (FLS) structural detector - no Ay_Emul.fmt byte signature
 * exists for this format. Ported from Players.pas:6332-6544's FoundFLS
 * (the "current, faster and more reliable" variant - an older, slower
 * FoundFLS is left commented out in the Pascal source at Players.pas:
 * 6547 onward and is not ported, since it is dead code even in the
 * original), minus the final IntegrityCheck confirmation step - see
 * detect_st_family.h's file comment for why. */
#ifndef IDENTIFY_DETECT_FLS_H
#define IDENTIFY_DETECT_FLS_H

#include "identify/common.h"

bool detect_fls_structural(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_FLS_H */
