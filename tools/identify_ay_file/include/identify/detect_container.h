/* AY-container-family detectors: the real Z80-driven .ay container (and
 * its AYAMAD/ST11 siblings), AYM, and PSG/EPSG register dumps. */
#ifndef IDENTIFY_DETECT_CONTAINER_H
#define IDENTIFY_DETECT_CONTAINER_H

#include "identify/common.h"

bool detect_ay_container(const filebuf* f, detection* d);
bool detect_aym(const filebuf* f, detection* d);
bool detect_psg(const filebuf* f, detection* d);

#endif /* IDENTIFY_DETECT_CONTAINER_H */
