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

#include "ay_engine/formats/ay_file.h"
#include "ay_engine/formats/fls_file.h"
#include "ay_engine/formats/gtr_file.h"
#include "ay_engine/formats/pt1_file.h"
#include "ay_engine/formats/pt2_file.h"
#include "ay_engine/formats/pt3_file.h"
#include "ay_engine/formats/fxm_file.h"
#include "ay_engine/formats/psm_file.h"
#include "ay_engine/formats/asc_file.h"
#include "ay_engine/formats/ftc_file.h"
#include "ay_engine/formats/psc_file.h"
#include "ay_engine/formats/sqt_file.h"
#include "ay_engine/formats/stc_file.h"
#include "ay_engine/formats/stp_file.h"
#include "ay_engine/formats/sndh_file.h"
#include "ay_engine/formats/vtx_file.h"
#include "ay_engine/formats/ym_file.h"
#include "ay_engine/formats/out_file.h"
#include "ay_engine/formats/epsg_file.h"

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
  PLAYER_FORMAT_OUT,  /* MIG-0010 update: raw ZX Spectrum port-write trace */
  PLAYER_FORMAT_EPSG, /* MIG-0010 update: register-write log w/ 16-byte header */
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
    out_file out;
    epsg_file epsg;
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
 * change.
 *
 * is_ste (MIG-0121): SNDH-only (see sndh_file_load's own comment); a
 * no-op flag for every other format, passed straight through - callers
 * with no reason to care should pass true, matching player_load's own
 * behavior and every pre-existing caller's exact prior default. */
player_status player_load_song(player* p, const char* path,
                                const uint8_t* data, size_t size,
                                int sample_rate, int song_index,
                                bool is_ste);

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
 * Returns true (with real values) for ALL 18 formats: AY/YM/VTX/SNDH
 * (SNDH's is atari_emulate::tick_count/tick_count_max, populated from
 * its own TIME tag by sndh_file_load - MIG-0100), PT3 (pt3_file's own
 * global_tick_counter/global_tick_max, populated by a faithful port of
 * GetTimePT3 - MIG-0101, see pt3_file.h), and the remaining 13
 * PT1/PT2/GTR/FLS/STC/STP/FXM/PSM/ASC/ASC0/FTC/PSC/SQT (each format's
 * own GetTimeXXX ported by MIG-0103/MIG-0104, wired into this dispatch
 * by a later session pass, and cross-checked exact-tick-for-tick
 * against the real Pascal oracle by MIG-0111, which also fixed a real
 * bug in pt3_get_time found via that same full-corpus sweep). This
 * comment previously said the other 13 formats always returned false/0
 * - that was accurate when written but went stale once the work above
 * landed and was never updated; do not trust a "still open" framing in
 * an old comment over migration_debt.yaml's actual entry states. */
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

/* MIG-0017 update: Atari_SeekTo's real fast-forward algorithm
 * (atari.pas:1684-1705), for SNDH only - see sndh_file.h's own
 * sndh_file_seek_fast_forward for the full citation and mixer-
 * reentrancy-safety notes. Every other format has no such shortcut in
 * the original either (RerollMusic's own IsZ80EmuFileType/VTX branches
 * just decode-and-discard through the normal player_make_buffer path,
 * same as gui/src/playback.c's do_seek already does generically) - this
 * returns false (no-op) for all of them, meaning callers should fall
 * back to that generic path. Only valid for a FORWARD seek (target_tick
 * >= the format's own current tick, per player_get_tick_position) -
 * returns false for a backward seek too (a full reload is still needed
 * either way, same as before this entry). */
bool player_seek_fast_forward(player* p, int64_t target_tick);

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

/* settings.pas: AY_FreqDef - the standard AY-3-8910/YM2149 clock every
 * format's own _file_load defaults to (see e.g. STC_FILE_AY_FREQ_DEF and
 * its siblings, all this same value) unless overridden. */
#define PLAYER_AY_FREQ_DEF 1773400

/* Read-side counterpart to player_set_chip_freq: the AY chip clock
 * CURRENTLY in effect for the loaded file, in Hz. VTX/YM store their own
 * file-derived clock (Players.pas reads it straight from the header, no
 * fixed default applies); every other format has no persistent per-file
 * ay_freq concept at all (see player_set_chip_freq's own comment) and is
 * always PLAYER_AY_FREQ_DEF unless a caller already called
 * player_set_chip_freq with something else. Used by gui/'s Mixer window
 * to re-establish filtering (ay_engine_set_filter, via
 * player_set_chip_freq) with the file's real clock right after a fresh
 * load, without needing every one of the 18 format loaders to expose
 * their own ay_freq. */
int player_get_ay_freq(const player* p);

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

/* MainWin.pas: Set_Z80_Frq (1601-1634) - a LOAD-TIME-ONLY Z80 clock
 * override (MIG-0120; matches this port's own established convention of
 * applying overrides once, right after a fresh load, not live
 * mid-playback - see gui/src/mixer_win.c's own doc comment on why).
 * Silently ignored if `frq_z80` is outside MainWin.pas:1605's own
 * [1000000, 8000000] guard range. A no-op for every format except AY
 * (PLAYER_FORMAT_AY) - matches the original: only Z80-driven .ay/.zxay/
 * .aym/.epsg-family files ever read FrqZ80 at all, and this port
 * currently only has a real .ay loader among that family. Updates
 * f->as.ay.frq_z80 and recomputes frq_ay_by_frq_z80 with the exact same
 * formula player_set_chip_freq's own AY case uses (MainWin.pas:1627). */
void player_set_frq_z80(player* p, int frq_z80);

/* MainWin.pas: Set_MC68K_Frq (1636-1650) - a LOAD-TIME-ONLY 68000 clock
 * override (MIG-0120, same rationale as player_set_frq_z80 above). A
 * no-op for every format except SNDH (the only format driving
 * atari_emulate in this port). Thin dispatcher onto atari_emulate_set_
 * mc68000_freq - see atari_emulate.h for the full formula citation. */
void player_set_mc68000_freq(player* p, double freq);

/* MainWin.pas: Set_MFP_Frq (1563-1585) - a LOAD-TIME-ONLY MFP timer
 * frequency override (MIG-0120, same rationale as player_set_frq_z80
 * above). A no-op for every format except SNDH. `mode` 0 = Auto:
 * computes MainWin.pas:1570's `Trunc(AY_Freq*16/13+0.5)` from THIS
 * player's own player_get_ay_freq and passes the result to atari_
 * emulate_set_mfp_freq; `freq` is ignored in this mode, matching
 * Set_MFP_Frq's own Md=0 branch which never reads its Fr parameter
 * either. `mode` 1 = manual: `freq` must be in MainWin.pas:1573's own
 * [1000000, 4365292] range (validated by atari_emulate_set_mfp_freq
 * itself). */
void player_set_mfp_freq(player* p, int mode, int freq);

/* Read-side counterparts to player_set_frq_z80/player_set_mc68000_freq/
 * player_set_mfp_freq, for gui/'s Mixer window "current value" readouts
 * (Mixer.lfm's EZ80FrqCur/EMCFrqCur/EMFPFrqCur, MIG-0120). 0/0.0 for any
 * format the corresponding setter is a no-op for (AY-only for frq_z80,
 * SNDH-only for the other two) - a caller displaying these should treat
 * 0 as "not applicable to this file", never as a real clock value. */
int player_get_frq_z80(const player* p);
double player_get_mc68000_freq(const player* p);
double player_get_mfp_freq(const player* p);

/* MainWin.pas: Set_N_Tact/Set_N_Tact2/Set_N_TactS (1670-1712, MIG-0131) -
 * a "TStates per frame" override (MaxTStates). UNLIKE player_set_frq_z80/
 * player_set_mc68000_freq/player_set_mfp_freq above, this one has no
 * `if IsPlaying then exit` guard in the original at all - it's a LIVE,
 * mid-playback-applicable override (same timing category as player_set_
 * chip_freq), not a load-time-only one. Silently ignored outside
 * MainWin.pas:1672's own (9999, 200000] guard range. A no-op for every
 * format except AY - matches the original: MaxTStates/FrqZ80 is only a
 * real per-tick timing concept for Z80-driven .ay/.zxay/.aym/.epsg-
 * family files (player_get_seconds_per_tick's own doc comment), and this
 * port currently only has a real .ay loader among that family. Writes
 * p->as.ay.bus.max_tstates directly - the same field real playback
 * timing already reads (z80_bus.c's own current_tact wraparound check).
 * Set_N_Tact's own Time_ms/ProgrMax/VProgrPos proportional-rescale side
 * effects are progress-bar bookkeeping this port's playback thread has
 * no equivalent state for - out of scope, same as player_set_player_
 * freq's own doc comment on digsoundloop_catch. */
void player_set_n_tact(player* p, int n_tact);

/* Read-side counterpart to player_set_n_tact, same shape/caveats as
 * player_get_frq_z80 (0 for any non-AY format). */
int player_get_n_tact(const player* p);

/* settings.pas: Interrupt_FreqDef = 50000 - *1000-prescaled (50Hz), the
 * standard VBL/interrupt rate every format defaults to unless overridden
 * (matches PT3_FILE_INTERRUPT_FREQ_DEF and its siblings, all this same
 * value). */
#define PLAYER_INTERRUPT_FREQ_DEF 50000

/* Read-side counterpart to player_set_player_freq, same shape as
 * player_get_ay_freq: the interrupt/VBL frequency CURRENTLY in effect
 * for the loaded file (Hz*1000-prescaled, matching player_set_player_
 * freq's own freq_hz_x1000 convention). VTX/YM store their own file-
 * derived rate; every other format has no persistent per-file
 * interrupt_freq concept (AY uses MaxTStates/FrqZ80 instead) and is
 * always PLAYER_INTERRUPT_FREQ_DEF unless a caller already called
 * player_set_player_freq with something else. Added for gui/'s Mixer
 * window (GBIntFrq's "current" live readout, Mixer.lfm's EIntFrqCur). */
int player_get_int_freq(const player* p);

/* MainWin.pas: Set_Stereo (~1793-1798: `if IsPlaying then exit;
 * NumberOfChannels := St; SetSynthesizer;`) - a LOAD-TIME-ONLY output
 * channel-count change (the original itself only allows this while not
 * playing; this port mirrors that by only ever calling it right after a
 * fresh load, same as the chip-type/frequency overrides in player_set_
 * chip_freq/player_set_player_freq - see gui/src/mainwin.c's apply_item_
 * overrides). `channels` must be 1 or 2 (MainWin.pas:1795's `St in [1,
 * 2]` guard) - silently ignored otherwise. Sets the loaded format's
 * ay_engine.number_of_channels (universal across all 18 formats, via
 * player_ay_engine) and recomputes the level tables (SetSynthesizer's
 * own Calculate_Level_Tables2 call, AY.pas:1759) since Calculate_Level_
 * Tables' Index_A/B/C arithmetic itself branches on number_of_channels
 * (AY.pas:956-976) - a stale level table would otherwise silently keep
 * producing the OLD channel count's mix ratios even after this call.
 * player_make_buffer's underlying ay_synthesizer_ay dispatch (ay.c)
 * already picks ay_synthesizer_{stereo,mono}{16,8} based on this same
 * field, so no further plumbing is needed for the actual PCM shape to
 * change - see ay_synthesizer_mono16/mono8 (ay.h/ay.c), already ported
 * and bugfixed (see migration_debt.yaml's tick-accumulator entry) but
 * previously unreachable from any live caller until this function. */
void player_set_number_of_channels(player* p, int channels);

/* Mixer.pas: RBBt16Click/RBBt8Click -> MainWin.pas's Set_Sample_Bit
 * (~1774-1779: `if IsPlaying then exit; SampleBit := SB;
 * SetSynthesizer;`) - MIG-0130, same LOAD-TIME-ONLY convention as
 * player_set_number_of_channels above (real Pascal only allows this
 * while not playing; this port mirrors that by only ever calling it
 * right after a fresh load). `bits` must be 8 or 16 - silently ignored
 * otherwise. Sets ay_engine.sample_bits and recomputes the level
 * tables (ay_engine_calculate_level_tables's own `r = (sample_bits==8)
 * ? 127 : 32767` full-scale branch, AY.pas:982) - ay_synthesizer_
 * dispatch already picks ay_synthesizer_{stereo,mono}8 based on this
 * same field (confirmed already correctly wired, ay.c), so no further
 * engine plumbing is needed for real 8-bit-WIDTH PCM output; the
 * caller (gui/src/playback.c) is what needs to know the resulting
 * player_make_buffer output is now packed uint8_t samples in the same
 * buffer, not int16_t - see ay_synthesizer_stereo16's own doc comment
 * for the exact aliasing convention. */
void player_set_sample_bits(player* p, int bits);

/* Dispatches to the matching X_file_make_buffer. Same contract as the
 * underlying format functions: writes up to `buffer_length` frames to
 * `buf` - STEREO16 (2 int16 samples per frame) if the loaded format's
 * ay_engine.number_of_channels is 2 (the default, see ay_engine_init),
 * MONO16 (1 int16 sample per frame) if 1 (see player_set_number_of_
 * channels) - returns frames actually written. Callers must size `buf`
 * and interpret its contents according to whichever channel count was
 * last set (player_ay_engine(p)->number_of_channels), same as this
 * port's ALSA output layer already does generically (tools/ay_player/
 * src/alsa_output.c). */
int player_make_buffer(player* p, int16_t* buf, int buffer_length);

/* True once the loaded format's own real_end_all flag is set - AY/YM/
 * SNDH/VTX/PT3 (MIG-0101), plus the remaining 13 tracker formats'
 * CheckLoopAndStop ports (MIG-0108). Every one of the 18 formats has
 * this wired now; ay_player's --seconds bound exists for the
 * do_loop=true (--ignore-end) case, not because any format lacks
 * real_end_all. */
bool player_real_end_all(const player* p);

/* Sets the loaded format's own do_loop flag - Players.pas: Do_Loop
 * (settings.pas, default false).
 * When true, CheckLoopAndStop-equivalent logic (see e.g. pt3_file.c's
 * pt3_file_make_buffer, MIG-0101) clamps the tick counter at its max
 * instead of ever setting real_end_all, so playback never stops on its
 * own - used by ay_player's --ignore-end (MIG-0101's own PT3 test
 * fix) and tests/oracle_diff/dump_engine_state.c's pt3_file scenario,
 * both matching OracleHarness.pas's RunPT3FileTest's own sentinel-
 * Global_Tick_Max bypass (see main.c's --ignore-end comment for the
 * full why). gui/src/playback.c never calls this - real interactive
 * playback keeps do_loop at its real default (false), matching the
 * original's own default and this port's separate, GUI-level Loop
 * button (MainWin.pas: ButLoop) implementation. */
void player_set_do_loop(player* p, bool do_loop);

/* MIG-0114: sets the loaded format's own force_loop flag - Players.pas/
 * Tools.pas's Force_Loop (the CBForceLoop checkbox, MIG-0095's own Tools-
 * window scope note previously excluded this specific control). Unlike
 * do_loop, Force_Loop still lets real_end_all become true once this
 * format's own tick budget is reached - it only keeps register
 * generation (and so audible pattern-position looping) going PAST that
 * point instead of freezing on the last frame's frozen register values,
 * per CheckLoopAndStop's exact semantics (Players.pas:8730-8746, see
 * e.g. pt3_file.c's pt3_check_loop_and_stop for the fullest citation).
 * The real Pascal use case this exists for: a mismatched-length
 * Turbosound pair's SHORTER voice keeps audibly looping instead of
 * going silent/frozen while the longer voice keeps playing - see
 * player_pair_set_force_loop. A no-op for any format
 * !player_supports_pairing(p) (AY/YM/VTX/SNDH/UNKNOWN never call
 * CheckLoopAndStop at all - see player_supports_pairing's own comment),
 * matching Force_Loop's real Pascal scope exactly (it's only ever
 * consulted from within that one tracker-specific function). */
void player_set_force_loop(player* p, bool force_loop);

/* Frees any owned allocation for formats that have one (YM/SNDH/VTX);
 * a no-op for AY/PT3 (which own none) and for PLAYER_FORMAT_UNKNOWN. */
void player_free(player* p);

const char* player_format_name(player_format fmt);

/* MIG-0112: playlist-level Turbosound (dual-chip) pairing of two
 * SEPARATE, independently-loaded files - Players.pas's TrModLoaded
 * (2643-2691) `else if (TSMode = False) and (PlayListItems[Index]^.Next
 * <> nil) then ...` branch, the one piece MIG-0109's own Phase A/PT3
 * self-pairing work deliberately left open (see migration_debt.yaml
 * MIG-0007's UPDATE note). Distinct from PT3's OWN self-pairing
 * (MIG-0109, a single .pt3 file with a byte-98 tag driving both voices
 * from data already in its own private ay_engine): this is TWO
 * DIFFERENT playlist items - possibly two different tracker formats
 * entirely (Players.pas's own TrModLoaded branch has no format-match
 * requirement) - chained via a `.ayl` playlist's "ts" `<` marker
 * (gui/include/gui/playlist.h) or (for API callers that construct a
 * pairing directly) simply two paths passed to player_pair_load_song.
 *
 * True for exactly the 14 tracker formats real Pascal's TrModLoaded/
 * CaseTrModules dispatch drives via All_GetRegisters[CNum] (PT1, PT2,
 * PT3, STC, STP, PSC, FLS, FTC, SQT, GTR, FXM, PSM, ASC, ASC0) -
 * matching MIG-0101's own "IsAYNativeFileType" audit exactly. False for
 * AY/YM/VTX/SNDH/UNKNOWN: those four are loaded through Players.pas's
 * OWN separate AYFileLoaded/YMFileLoaded/VTXFileLoaded/SNDHFileLoaded
 * functions, none of which ever consult PlayListItems[Index]^.Next -
 * TSMode is structurally unreachable from any of them in the original,
 * so pairing either of them is not a "not yet ported" gap, it's simply
 * not a real Pascal behavior to reproduce. */
bool player_supports_pairing(const player* p);

/* Advances ONE interrupt frame's worth of register writes into `target`
 * - Players.pas's All_GetRegisters[CNum](CNum), generalized as an
 * explicit ay_chip* (this port has no single shared SoundChip[0..1]
 * array the way AY.pas does; every format struct owns its own private
 * ay_engine instead - see ay.h's own file comment) rather than an index
 * into one. Dispatches to the matching <fmt>_file_step_registers
 * (engine/include/ay_engine/formats/<fmt>_file.h, MIG-0112) - the SAME
 * function each format's own X_file_make_buffer now calls internally
 * for standalone playback (always with `&p->as.<fmt>.ay.chip` there),
 * so standalone and paired playback share one code path per format, not
 * two to keep in sync. Returns false (a no-op call) once this player's
 * own natural end was already reached (player_real_end_all(p) is then
 * true) - matching CheckLoopAndStop's own `if CheckLoopAndStop(CNum)
 * then Exit` early-return, reproduced as an idempotent no-op rather
 * than requiring the caller to stop calling this once a side has ended
 * (see player_pair_make_buffer, which calls both sides unconditionally
 * every frame exactly like MakeBufferTracker's own unconditional
 * `All_GetRegisters[0](0)` does). Always false (a no-op) for AY/YM/VTX/
 * SNDH/UNKNOWN - see player_supports_pairing. */
bool player_step_registers(player* p, ay_chip* target);

/* MIG-0010 update: generalizes player_step_registers to ALL 18 formats,
 * including the four (AY/YM/VTX/SNDH) that one always no-ops on -
 * TSMode pairing is structurally unreachable for those four (that's
 * genuinely why player_step_registers excludes them), but each one
 * still has its own real per-tick register generator in Players.pas,
 * reached by Convs.pas's VBL2PSG/VBL2VTX export converters exactly like
 * the 14 tracker formats are - see engine/src/player.c's own definition
 * for the full citation. Steps the format's OWN internal chip in place
 * rather than an explicit external target (no pairing-style redirection
 * exists for these four in the original either); read player_ay_engine
 * (p)->chip afterward for the resulting register state. Returns false
 * once real_end_all is set (an idempotent no-op after that point,
 * matching player_step_registers' own contract) or for
 * PLAYER_FORMAT_UNKNOWN. */
bool player_step_registers_any(player* p);

/* Holds two independently-loaded players sharing ONE ay_engine (the
 * primary's own) for playlist-level Turbosound pairing (MIG-0112, see
 * player_supports_pairing's own comment for how this differs from PT3's
 * self-pairing). `secondary`'s own private ay_engine is set up by its
 * normal per-format loader but never actually driven through its own
 * synth path when `active` - only its register-generation side
 * (player_step_registers) runs, writing into the PRIMARY's ay_engine's
 * chip2 instead of secondary's own chip - exactly mirroring how PT3's
 * self-pairing writes voice 1's registers into the SAME struct's own
 * chip2 rather than a separate engine (MIG-0109), just across two
 * player instances instead of one. */
typedef struct player_pair {
  player primary;
  player secondary;
  bool active; /* true iff secondary is genuinely loaded and paired -
                * see player_pair_load_song's own comment for when this
                * ends up false despite a secondary path being given. */
  bool secondary_loaded; /* true iff secondary was successfully loaded,
                           * REGARDLESS of whether pairing ended up
                           * active (e.g. it loaded fine but primary had
                           * already self-paired) - tracked separately
                           * from `active` purely so player_pair_free
                           * knows whether secondary owns an allocation
                           * that needs releasing. */
} player_pair;

/* Loads `primary` via player_load_song exactly as player_load_song
 * itself would. If `secondary_data` is non-NULL, ALSO loads `secondary`
 * the same way and, only if ALL of the following hold (matching
 * TrModLoaded's own `(TSMode = False) and (PlayListItems[Index]^.Next
 * <> nil)` guard plus LoadTrackerModule's own implicit "second file
 * must itself load OK" requirement), activates pairing:
 *  - primary loaded successfully and player_supports_pairing(primary)
 *  - secondary loaded successfully and player_supports_pairing(secondary)
 *  - primary did NOT already self-pair (e.g. a byte-98-tagged .pt3 -
 *    MIG-0109's own ts_mode) - TrModLoaded only ever consults `Next`
 *    `if TSMode = False`, so a self-pairing PT3 file's OWN internal
 *    partner always wins over any playlist-level Next chain.
 * Otherwise `pair->active` is left false and `pair` plays as `primary`
 * alone - matching TrModLoaded's own graceful "Next failed to load,
 * TSMode just never gets set" fallback (Players.pas has no separate
 * error path for a bad Next item; it simply doesn't pair). Returns
 * primary's own player_load_song status; secondary's success/failure is
 * not surfaced beyond `pair->active`, matching the original exactly. */
/* is_ste (MIG-0121): applies to `primary` only (SNDH never participates
 * in Turbosound pairing, so `secondary` - if any - is always loaded with
 * is_ste=true, matching every pre-existing caller's exact prior
 * default). */
player_status player_pair_load_song(player_pair* pair, const char* primary_path,
                                     const uint8_t* primary_data,
                                     size_t primary_size,
                                     const char* secondary_path,
                                     const uint8_t* secondary_data,
                                     size_t secondary_size, int sample_rate,
                                     int song_index, bool is_ste);

/* Players.pas: MakeBufferTracker (12301-12317) - the shared cadence loop
 * every Turbosound-paired (or unpaired) tracker plays through. Calls
 * player_step_registers on `primary` (into its own chip) every frame
 * unconditionally, and - only when `pair->active` - on `secondary` too
 * (into primary's chip2), then synthesizes/mixes exactly once via
 * primary's own ay_engine (already ts_mode-aware per MIG-0109) UNLESS
 * BOTH have reached their own natural end (`Real_End_All := Real_End[0]
 * and Real_End[1]` when TSMode, matching CheckLoopAndStop's per-CNum
 * accumulation exactly - a still-playing secondary keeps the pair going
 * even after primary's own file ends, and vice versa). When
 * `pair->active` is false this reduces to plain player_make_buffer
 * (single call, no pairing overhead) - callers can use this
 * unconditionally instead of branching on `active` themselves. */
int player_pair_make_buffer(player_pair* pair, int16_t* buf,
                             int buffer_length);

/* True once BOTH primary and secondary (when active) have reached their
 * own natural end - Players.pas's Real_End_All, the AND of Real_End[0]
 * and (if TSMode) Real_End[1]. Equivalent to plain player_real_end_all
 * when `pair->active` is false. */
bool player_pair_real_end_all(const player_pair* pair);

/* Sets do_loop on both primary and secondary (a no-op on secondary when
 * not active) - Players.pas's Do_Loop is a single global flag consulted
 * identically by CheckLoopAndStop(0) and CheckLoopAndStop(1), so both
 * sides must agree here too. */
void player_pair_set_do_loop(player_pair* pair, bool do_loop);

/* MIG-0114: sets force_loop on both primary and secondary, same rationale
 * as player_pair_set_do_loop - Force_Loop is Players.pas's own single
 * global too (Tools.pas's CBForceLoop, not per-voice), consulted
 * identically by both CheckLoopAndStop(0)/(1) calls within one
 * MakeBufferTracker iteration. This is genuinely the intended real-world
 * use of Force_Loop (a mismatched-length pair's shorter voice keeps
 * looping instead of going silent) - see player_set_force_loop's own
 * comment for the full mechanism. */
void player_pair_set_force_loop(player_pair* pair, bool force_loop);

/* Frees primary, and secondary too if it was ever loaded (regardless of
 * whether pairing ended up active - a secondary that failed
 * player_supports_pairing but still loaded successfully still owns
 * whatever allocation player_free would need to release). Safe to call
 * on a pair whose player_pair_load_song never fully succeeded. */
void player_pair_free(player_pair* pair);

#endif /* PLAYER_FORMAT_H */
