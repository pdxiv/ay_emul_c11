/* ST-Sound YM2/YM3/YM3b/YM5/YM6 register-dump family. */
#ifndef IDENTIFY_DETECT_YM_H
#define IDENTIFY_DETECT_YM_H

#include "identify/common.h"

/* Checks the YM2!/YM3!/YM3b/YM5!.../YM6!... magic at file offset `base`
 * (used directly for uncompressed files, and would apply after LHA
 * decompression - which this tool does not perform - for wrapped ones). */
bool detect_ym_body(const filebuf* f, size_t base, detection* d);

/* Full entry point: also checks the "-lh5-" LHA wrapper at offset 2. */
bool detect_ym(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_YM_H */
