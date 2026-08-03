/* Sound Tracker Pro, uncompiled (STF) structural detector - no Ay_Emul.fmt
 * byte signature exists for this format. Unlike every other tracker
 * format here, STF modules are themselves stored compressed with a
 * small custom LZ77-like scheme (Players.pas:1187-1329's
 * STFDepackInit/STFDepackBytes) - FoundSTF (Players.pas:5408-5553) depacks
 * incrementally, validating each structural field as soon as enough bytes
 * exist for it. This file ports the depacker once, decoding the whole
 * candidate window in a single pass (provably equivalent to Pascal's
 * incremental calls - see the .c file's top comment for why), then
 * re-checks every field Pascal's incremental calls would have checked.
 * The final IntegrityCheck confirmation step is not performed - see
 * detect_st_family.h's file comment for the same rationale. */
#ifndef IDENTIFY_DETECT_STF_H
#define IDENTIFY_DETECT_STF_H

#include "identify/common.h"

bool detect_stf_structural(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_STF_H */
