/* Unified dispatch over the file formats engine/libayengine.a can load
 * and render. This is a NEW, narrower detector than tools/identify_ay_file/
 * (which classifies ~19 formats but doesn't link against engine/ at all,
 * by design - see its own Makefile comment). Originally lived under
 * tools/ay_player/include/player/format.h (the CLI player's own private
 * header); promoted here at the Phase 5 GUI kickoff since gui/ needs the
 * exact same dispatch and it belongs in the shared engine layer, not a
 * CLI-tool-specific one - moved as-is (mechanical rename, no dispatch-
 * logic change), with player_song_count()/player_load_song() added below
 * for the GUI's benefit. ay_player only needs to tell its supported
 * formats apart, so it re-derives their signatures directly from
 * Ay_Emul.fmt/Players.pas (cited per check below) rather than depending
 * on identify_ay_file's code.
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
#ifndef AY_ENGINE_PLAYER_H
#define AY_ENGINE_PLAYER_H

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

/* Same as player_load, but with an explicit song_index for formats that
 * support multi-song selection (currently only AY - see ay_file_load's
 * own song_index parameter). Ignored (song 0 always loads) for every
 * other format, since none of them support subsong selection yet -
 * added for the Phase 5 GUI, which needs to offer song choice for
 * multi-song .ay files; player_load is unchanged and still just calls
 * this with song_index=0, so nothing already using player_load needs to
 * change. */
player_status player_load_song(player* p, const char* path,
                                const uint8_t* data, size_t size,
                                int sample_rate, int song_index);

/* Number of selectable songs in the loaded file (1 for every format
 * except multi-song AY files, where it's TAYFileHeader.NumOfSongs+1 -
 * see ay_file.h's song_count field). Call after a successful
 * player_load/player_load_song. Added for the Phase 5 GUI (song
 * selection); not a concept the CLI player has ever needed. */
int player_song_count(const player* p);

/* Which AY-3-8910-family chip variant the loaded format renders through
 * (AY_CHIP_TYPE_AY or AY_CHIP_TYPE_YM - see ay.h's AY.pas: ChType). Every
 * format struct embeds its own `ay_engine ay` (the shared sound-chip
 * core all 18 formats render audio through), so this is a simple
 * per-format field read, no new engine logic. Added for the Phase 5 GUI
 * (Led_AY/Led_YM real wiring, MainWin.pas's own Led_AY/Led_YM reflect
 * exactly this same AY.pas ChType value). Call after a successful
 * player_load/player_load_song. */
ay_chip_type player_chip_type(const player* p);

/* Raw (untranscoded CP1251) author/title/comment strings - see
 * ay_file.h's own comment on why these aren't converted to UTF-8 here
 * (a GUI-layer concern, see gui/src/playback.c). Author+Title+Comment:
 * .ay (Players.pas's OpenAYFile), YM5/YM6 (ym_file.h). Title+Author (no
 * Comment): PT3, PSC (MIG-0082), ASC, ASC0 (MIG-0083 - both share ONE
 * check/offset pair post-normalization, see asc_file.c's own comment).
 * Title only (no Author, no Comment): GTR, FTC, PT1, PT2 (MIG-0082,
 * Players.pas's AddTrackerModule, 7345-7402 - each a fixed-offset,
 * fixed-width, space-padded field), STP (MIG-0083 - only present if a
 * 28-byte KSA signature matches at a fixed offset), PSM (MIG-0083 - a
 * variable-length remark field, with an optional stripped "psm1\0"
 * prefix convention). Every other format (VTX/SNDH/FLS/STC/FXM/SQT)
 * writes empty strings to all three outputs - matching each of their
 * own headers' still-accurate "no metadata extraction" note (STC's own
 * title logic exists in Players.pas but needs legacy compressor-tag
 * detection, KSA/STCompressor signature parsing at a computed rather
 * than fixed offset, a materially bigger unit than the formats done so
 * far - not attempted here; FLS/SQT have no title-extraction logic in
 * Players.pas at all, confirmed by direct grep across the full title-
 * extraction section - not a gap, the original simply never reads
 * metadata for those two).
 * `cap` includes the NUL terminator; a NULL out-pointer or 0 cap skips
 * that particular string. Call after a successful player_load/
 * player_load_song. */
void player_get_metadata_raw(const player* p, char* author,
                              size_t author_cap, char* title,
                              size_t title_cap, char* comment,
                              size_t comment_cap);

/* Returns a mutable pointer to the loaded format's shared ay_engine -
 * every one of the 18 formats embeds its own `ay_engine ay` (see
 * player_chip_type's own comment for the same pattern). Lets a caller
 * adjust live mixer parameters - chip_type, and the six per-channel
 * pan/level weights index_al/ar/bl/br/cl/cr (AY.pas: Index_AL etc,
 * MainWin.pas's Set_Mode_Manual) - then call
 * ay_engine_calculate_level_tables() (ay.h) to apply them; this mirrors
 * Set_Mode_Manual's own "assign fields, then Calculate_Level_Tables2"
 * shape exactly, just generalized across all 18 formats instead of
 * being main-window-specific. Added for the Phase 5 GUI's Mixer window
 * (MIG-0078) - the CLI player has never needed live parameter changes.
 * Never NULL for a successfully-loaded player. */
ay_engine* player_ay_engine(player* p);

/* Players.pas's shared Global_Tick_Counter/Global_Tick_Max convention
 * (PlConsts[0] in the original - ticks played so far / total ticks the
 * file declares, e.g. .ay's TSongData.SongLength). This is exactly what
 * Players.pas's RerollMusic (14099-14283) reads to compute a seek
 * target (`SeekTo := round(newpos / maxpos * Global_Tick_Max)`) - see
 * gui/src/playback.c's gui_playback_request_seek for the C11 port of
 * RerollMusic's own decode-and-discard algorithm.
 *
 * Returns true (with real values) for only AY/YM/VTX - the sole three
 * formats whose C11 struct actually carries a global_tick_max field.
 * False (both outputs left at 0) for every other format: SNDH (a
 * separate Atari-VBL-based position model, not unified here - see
 * MIG-0017); PT1/PT2/PT3/GTR/FLS/STC/STP/FXM (each of these DOES have
 * a real declared song length in its own file format, e.g. PT3.pas's
 * GetTimePT3, but computing it was already a deliberate, separately-
 * recorded earlier scope decision - "UI-only, not needed [for audio
 * correctness]" - not something this entry revisits); PSM/ASC/ASC0/
 * FTC/PSC/SQT (these loop indefinitely via a position-based loop point
 * in the file, genuinely no fixed song length to have skipped). */
bool player_get_tick_position(const player* p, int64_t* counter,
                               int64_t* max);

/* Real-world seconds represented by ONE tick of Global_Tick_Counter -
 * needed to convert a real MM:SS time (JmpTime.pas) into a seek
 * fraction, on top of player_get_tick_position's own tick-count-only
 * view. Players.pas derives this per format: for AY (RerollMusic's
 * IsZ80EmuFileType branch, MainWin.pas:14277:
 * `BaseSample := trunc(l / FrqZ80 * MaxTStates * SampleRate + 0.5)`) it's
 * MaxTStates/FrqZ80 (one tick = one Z80 interrupt frame); for YM/VTX
 * (RerollMusic's FT.VTX/IsSTSoundFileType branch, MainWin.pas:14110-14111)
 * it's 1000/Interrupt_Freq (Interrupt_Freq is itself pre-scaled by 1000,
 * InterFrq*1000 - see ym_file.c/vtx_file.c's own interrupt_freq field
 * comments). Returns 0.0 for any format
 * player_get_tick_position doesn't support (see its own comment) -
 * there's no meaningful tick-to-seconds rate without a known tick_max
 * to relate it to either. */
double player_get_seconds_per_tick(const player* p);

/* MainWin.pas: Set_Chip_Frq (1534-1552) - a LIVE AY-chip clock
 * frequency change (`ay_freq` in Hz, MainWin.pas's own guard range is
 * [1000000, 3546800]; not enforced here, callers should clamp before
 * calling, matching ItemEdit.pas's own Val()+clamp pattern). Recomputes
 * delay_in_tiks/tik_re (every format - all 18 already set these at
 * load time from their own fixed AY-frequency default, so the concept
 * is universal) and the one Z80-specific derived field only AY has
 * (frq_ay_by_frq_z80 - matches ay_file_set_chip_freq, which this
 * generalizes; AY is the only format that runs a real Z80 core, see
 * player_chip_type's own file-scoped precedent for why AY is special-
 * cased here the same way). Also recomputes the two format-specific
 * "ticks per interrupt" derived fields Set_Chip_Frq's own body updates
 * (MainWin.pas:1547-1548): VTX's ay_tiks_in_interrupt, YM's
 * ym6_tiks_on_int - both were already stored per MIG-0080/MIG-0087.
 * A benign no-op recompute for every other format (delay_in_tiks/
 * tik_re still update, matching Set_Chip_Frq's own universal behavior,
 * even though this port doesn't yet have a per-item UI control exposing
 * it for those formats - see MIG-0087's own scope note). */
void player_set_chip_freq(player* p, int ay_freq, int sample_rate);

/* MainWin.pas: Set_Player_Frq (2043-2062, the arithmetic core only -
 * the digsoundloop_catch/Time_ms-rescale bookkeeping around it is a
 * BASS-streaming-specific concern, out of scope) - a LIVE VBL/
 * interrupt frequency change (`freq_hz_x1000`, matching Interrupt_
 * Freq's own storage convention of Hz*1000 - see player_get_seconds_
 * per_tick's own comment on why). Only meaningful for YM/VTX (the only
 * two formats with an interrupt_freq field at all - AY uses MaxTStates/
 * FrqZ80 instead, a different concept, see player_get_seconds_per_tick);
 * a no-op for every other format. Recomputes ay_tiks_in_interrupt/
 * ym6_tiks_on_int from the format's own already-stored ay_freq. */
void player_set_player_freq(player* p, int freq_hz_x1000);

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
