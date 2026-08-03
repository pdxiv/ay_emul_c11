/* Unified dispatch over the file formats engine/libayengine.a can load
 * and render. This is a NEW, narrower detector than tools/identify_ay_file/
 * (which classifies ~19 formats but doesn't link against engine/ at all,
 * by design - see its own Makefile comment). ay_player only needs to tell
 * its supported formats apart, so it re-derives their signatures directly
 * from Ay_Emul.fmt/Players.pas (cited per check below) rather than
 * depending on identify_ay_file's code.
 *
 * Content-signature-based detection (checked first, in this order -
 * arbitrary but fixed, these signatures don't overlap in practice):
 *  - "ZXAY" + TypeID "EMUL" @4 -> AY (Players.pas:7148, TAYFileHeader).
 *    Other ZXAY TypeIDs (AMAD/ST11/other) are real formats but not ones
 *    engine/ay_file.c supports (see ay_file.h's file comment) - rejected.
 *  - "YM2!"/"YM3!"/"YM3b"/"YM5!LeOnArD!"/"YM6!LeOnArD!" @0, or "-lh5-" @2
 *    (LHA-wrapped) -> YM (Ay_Emul.fmt [format+ YM2/YM3/YM3b/YM5/YM6/YM]).
 *    engine/ym_file.c only actually plays the LHA-wrapped extended-YM5
 *    case; ym_file_load itself reports YM_FILE_ERR_UNSUPPORTED_TYPE for
 *    anything else, so this detector doesn't need to pre-filter that.
 *  - "ProTracker 3."@0+" compilation of "@14+" by "@62, or "Vortex
 *    Tracker II 1.0 module: "@0+" by "@62 -> PT3 (Ay_Emul.fmt
 *    [format+ PT3]).
 *  - "ICE!"@0 or "SNDH"@12 -> SNDH (Ay_Emul.fmt [format+ SNDH]).
 *    engine/sndh_file.c rejects "ICE!" (SNDH_FILE_ERR_ICE_COMPRESSED, not
 *    ported - see sndh_file.h) but the detector still recognizes it as
 *    SNDH so the caller gets a clear "unsupported", not "unrecognized".
 *  - 2-letter id "ay"/"ym"/"AY"/"YM" @0 + mode byte 0..6 @2 -> VTX
 *    (Ay_Emul.fmt [format+ VTX]).
 *
 * Extension-based fallback (only reached if no signature above matches):
 * some formats (PT1, GTR, FLS, and others as they're added) have NO byte
 * signature at all in Ay_Emul.fmt - real Pascal recognizes them purely by
 * a recognised file extension (Players.pas:8100-8103's Tier A, trusted
 * outright with no content check at all - see tools/identify_ay_file's
 * own dispatch.c for the fuller citation of this same real behavior).
 * player_load mirrors that: `.pt1` -> PT1, `.gtr` -> GTR, `.fls` -> FLS,
 * `.stc` -> STC, `.fxm` -> FXM, etc. Note FXM actually HAS an
 * Ay_Emul.fmt byte signature ("FXSM"@0), but real Pascal's own
 * Module_Detector never checks for it (confirmed by
 * tools/identify_ay_file's own detect_signature_trackers.c comment,
 * itself citing a full read of Players.pas:6901-7002) - FXM is only
 * ever reached via Tier A (a recognised .fxm extension) or a ZXAY+AMAD
 * container's TypeID, so this detector deliberately does NOT content-
 * sniff "FXSM" either, to match real playback behavior exactly rather
 * than being more permissive than it. PSM has the same situation
 * ("psm1"@8 is a real Ay_Emul.fmt signature, also never checked by
 * Module_Detector per the same identify_ay_file comment) - `.psm` -> PSM
 * is extension-only detection too. ASC has no signature at all; `.asc`
 * -> ASC, `.as0` -> ASC0 (the older, LoopingPosition-less on-disk
 * variant - see asc_file.h for the load-time shift/fixup this needs).
 * FTC is detected via extension too (`.ftc`): real Pascal actually
 * reaches FTC through a structural heuristic validator (FoundFTC,
 * Players.pas:6973), not a simple byte signature - identify_ay_file's
 * own "Module: "+";Fast Tracker v1.00" signature is an approximation
 * that does NOT match every real file (confirmed empirically:
 * test_corpus_76/RE-TRIGG.ftc has a different trailing version string
 * and no leading ";" at that offset) - porting FoundFTC itself is out of
 * scope here, so this detector uses the reliable extension-based path
 * instead of an approximate, sometimes-wrong content signature. PSC is
 * the same situation one level further: real Pascal reaches it purely
 * via FoundPSC (a structural heuristic, Players.pas:5969), and
 * tools/identify_ay_file doesn't even attempt an approximate byte
 * signature for it - `.psc` -> PSC is extension-only detection. SQT is
 * the same again: reached purely via FoundSQT (a structural heuristic,
 * tools/identify_ay_file's own detect_sqt_structural reimplements it
 * rather than using a simple signature) - `.sqt` -> SQT is
 * extension-only detection too. */
#ifndef PLAYER_FORMAT_H
#define PLAYER_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/ay_file.h"
#include "ay_engine/fls_file.h"
#include "ay_engine/gtr_file.h"
#include "ay_engine/pt1_file.h"
#include "ay_engine/pt2_file.h"
#include "ay_engine/pt3_file.h"
#include "ay_engine/fxm_file.h"
#include "ay_engine/psm_file.h"
#include "ay_engine/asc_file.h"
#include "ay_engine/ftc_file.h"
#include "ay_engine/psc_file.h"
#include "ay_engine/sqt_file.h"
#include "ay_engine/stc_file.h"
#include "ay_engine/stp_file.h"
#include "ay_engine/sndh_file.h"
#include "ay_engine/vtx_file.h"
#include "ay_engine/ym_file.h"

typedef enum {
  PLAYER_FORMAT_UNKNOWN = 0,
  PLAYER_FORMAT_AY,
  PLAYER_FORMAT_YM,
  PLAYER_FORMAT_PT3,
  PLAYER_FORMAT_SNDH,
  PLAYER_FORMAT_VTX,
  PLAYER_FORMAT_PT1,
  PLAYER_FORMAT_GTR,
  PLAYER_FORMAT_FLS,
  PLAYER_FORMAT_STC,
  PLAYER_FORMAT_STP,
  PLAYER_FORMAT_PT2,
  PLAYER_FORMAT_FXM,
  PLAYER_FORMAT_PSM,
  PLAYER_FORMAT_ASC,
  PLAYER_FORMAT_ASC0,
  PLAYER_FORMAT_FTC,
  PLAYER_FORMAT_PSC,
  PLAYER_FORMAT_SQT,
} player_format;

typedef enum {
  PLAYER_OK = 0,
  PLAYER_ERR_UNRECOGNIZED, /* no signature matched and no known extension */
  PLAYER_ERR_LOAD_FAILED,  /* format recognized but its own _load failed
                            * (e.g. YM6!/non-extended-YM5, ICE!-compressed
                            * SNDH, embedded-pointer PT3 - see format.c) */
} player_status;

typedef struct player {
  player_format format;
  union {
    ay_file ay;
    ym_file ym;
    pt3_file pt3;
    sndh_file sndh;
    vtx_file vtx;
    pt1_file pt1;
    gtr_file gtr;
    fls_file fls;
    stc_file stc;
    stp_file stp;
    pt2_file pt2;
    fxm_file fxm;
    psm_file psm;
    asc_file asc; /* shared for both ASC (ASC1) and ASC0 - format tag
                   * distinguishes which, since both load into the same
                   * asc_file struct */
    ftc_file ftc;
    psc_file psc;
    sqt_file sqt;
  } as;
} player;

/* Detects the format of `data`/`size` (see file comment for the exact
 * rules - content signature first, then `path`'s extension as a fallback
 * for signature-less formats) and loads it via that format's own _load
 * function. `sample_rate` is passed straight through (pass a
 * X_FILE_SAMPLE_RATE_DEF of your choice - they're all 48000 by
 * convention). Returns PLAYER_ERR_UNRECOGNIZED if nothing matches, or
 * PLAYER_ERR_LOAD_FAILED if a signature/extension matched but the
 * format's own loader rejected the content (the specific X_FILE_ERR_* is
 * not surfaced here - a caller needing that detail should call the
 * specific X_file_load directly instead of going through this
 * dispatcher). */
player_status player_load(player* p, const char* path, const uint8_t* data,
                           size_t size, int sample_rate);

/* Dispatches to the matching X_file_make_buffer. Same contract as the
 * underlying format functions: writes up to `buffer_length` stereo16
 * frames to `buf`, returns frames actually written. */
int player_make_buffer(player* p, int16_t* buf, int buffer_length);

/* True once the loaded format's own real_end_all flag is set (AY/YM/
 * SNDH/VTX); PT3 has no such concept (see pt3_file.h) and this always
 * returns false for it - callers relying solely on this to stop PT3
 * playback will run forever, hence ay_player's --seconds bound. */
bool player_real_end_all(const player* p);

/* Frees any owned allocation for formats that have one (YM/SNDH/VTX);
 * a no-op for AY/PT3 (which own none) and for PLAYER_FORMAT_UNKNOWN. */
void player_free(player* p);

const char* player_format_name(player_format fmt);

#endif /* PLAYER_FORMAT_H */
