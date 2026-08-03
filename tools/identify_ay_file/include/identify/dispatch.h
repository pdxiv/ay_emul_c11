#ifndef IDENTIFY_DISPATCH_H
#define IDENTIFY_DISPATCH_H

#include "identify/common.h"

/* Top-level dispatch, mirroring Players.pas AddFile's Tier A/B/C exactly -
 * see dispatch.c's top comment for the full citation. */
void identify(const filebuf* f, const char* path, detection* d);

#endif /* IDENTIFY_DISPATCH_H */
