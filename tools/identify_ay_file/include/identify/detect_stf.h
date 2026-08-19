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
 * The final IntegrityCheck confirmation step is now also performed
 * (dispatch.c's confirm()/integrity_check_stf): LoadTrackerModule's real
 * STF handling is itself STFDepack immediately followed by a conversion
 * into FT.STP's own on-disk layout (STF2STP) - see st_convert.h - so
 * this reuses engine/'s existing STP support rather than needing a
 * separate STF playback engine. Unlike every other IntegrityCheck-backed
 * format, this has NOT been checked against a real .stf file, since none
 * exists anywhere in this repo's test corpus - see st_convert.h's file
 * comment for the validation approach used instead. */
#ifndef IDENTIFY_DETECT_STF_H
#define IDENTIFY_DETECT_STF_H

#include "identify/common.h"

bool detect_stf_structural(const filebuf* f, detection* d);

/* Exposed so st_convert.c's STF2STP port can depack a candidate STF
 * window without duplicating this depacker - Pascal's own STF2STP
 * (Players.pas:1346-1764) always runs against an already-depacked
 * buffer (LoadTrackerModule calls STFDepack immediately before
 * STF2STP), so the conversion step's input is this decoder's output,
 * not the raw compressed file bytes. */
#define STF_PAT_SIZE 576        /* Players.pas: STF_PatSize = 9*64 */
#define STF_PRE_PATS_SIZE 0xBF9 /* Players.pas: STF_PrePatsSize */
#define STF_MAX_SIZE (STF_PRE_PATS_SIZE + STF_PAT_SIZE * 32)

typedef struct {
  const uint8_t* buf1; /* compressed input */
  size_t psize;
  size_t pos1;
  uint8_t buf2[STF_MAX_SIZE]; /* depacked output */
  size_t pos2;
  bool ended;
  bool ok;
} stf_depack_state;

/* Players.pas:1196-1329 STFDepackBytes, decoded in one continuous pass -
 * see detect_stf.c's top comment for why this is equivalent to Pascal's
 * incremental per-checkpoint calls. Caller sets buf1/psize/pos1(=1)/
 * pos2(=0) before calling; on return, s->ok indicates whether decoding
 * completed without error (s->pos2 bytes are valid in s->buf2 either
 * way - partial output on failure, matching how much Pascal would have
 * produced before hitting the same error). */
void stf_depack_all(stf_depack_state* s);

#endif /* IDENTIFY_DETECT_STF_H */
