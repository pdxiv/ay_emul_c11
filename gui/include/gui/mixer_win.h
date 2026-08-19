/* C11/GTK2 port of Mixer.pas's TFrmMixer - scoped to the controls that
 * affect actual audio output (see PHASE5_GUI_PROGRESS.md's own note on
 * why): the six per-channel pan/level trackbars (TBAmpAL/AR/BL/BR/CL/
 * CR), the beeper/digidrum amplitude trackbar (TBAmpBpr - MIG-0106,
 * originally scoped out by MIG-0078 but brought in-scope on explicit
 * user request), the Atari DMA-sound headroom amplitude trackbar
 * (TBAmpDMA - MIG-0123, same origin, previously miscategorized as an
 * Atari-only feature out of scope - see mixer_win.c's own comment for
 * why it isn't), the preamp trackbar (TBPreAmp - MIG-0131, found via
 * tools/lfm_analyze/lfm_analyze.py's ground-truth pass against Mixer.
 * lfm/Mixer.pas after this port's own GUI turned out to have never
 * exposed it despite the engine already fully supporting it), plus a
 * typed exact-value entry paired with every one of these 9 sliders
 * (EAmpAL etc, MIG-0131), the AY/YM chip-type radio buttons (RBChTypeAY/
 * RBChTypeYM), the resampler quality radio buttons (RBResamAvg/
 * RBResamFIR - MIG-0106, same origin as the beeper slider), and
 * GBChFrq/GBIntFrq's own read-only "current value" displays (EChFrqCur/
 * EIntFrqCur - not overrides, ItemEdit.pas's own MIG-0088 override
 * mechanism is unchanged) AND, now, their real LIVE override sub-panels
 * (RBChFrqZX/Pnt/ST/CPC/Other, RBEIntFrqZX/RBIntFrqPnt/RBIntFrqOther -
 * MIG-0124), and (MIG-0120/MIG-0121) the Atari MFP/Z80/68000 clock
 * overrides (GBZ80Frq/GBMCFrq/GBMFPFrq) plus the SNDH ST/STe/mono-
 * default controls (GBSNDH). Everything else in the real Mixer.pas -
 * BASS/proxy/network settings - is NOT ported (see migration_debt.yaml):
 * out of this port's scope.
 *
 * Built with idiomatic GTK2 widgets (GtkVScale/GtkRadioButton in a
 * GtkHBox/GtkVBox layout), not generated from Mixer.lfm - same
 * rationale as gui/src/playlist_win.c (Mixer.pas isn't skin-rendered,
 * and Mixer.lfm's generated skeleton is far too large - 1,592 lines -
 * to use as a hand-completion starting point, see MIG-0078).
 *
 * TAB STRUCTURE (MIG-0129/MIG-0130): real Mixer.pas is a TPageControl
 * with 5 tabs, real captions (Mixer.lfm) - "AY Emulation" (AYEmuSheet),
 * "Digital Sound" (WOSheet - NOT "Wave-Out"; a MIG-0129-era comment
 * here mislabeled it, corrected by MIG-0130's own research pass -
 * confirmed on Linux this port's real digsound.pas already targets
 * ALSA directly, not a Windows WaveOut/DirectSound abstraction, so its
 * GBDevice control is a genuine ALSA PCM device picker, directly
 * portable), "MIDIOut" (MOSheet), "Global Volume Control" (VolumeSheet),
 * "BASS v2.4 (optional)" (BASSSheet). MOSheet/BASSSheet stay unported
 * (no MIDI/BASS equivalent attempted, out of scope entirely). This
 * window has a real GtkNotebook with 3 tabs matching the 3 real tabs
 * that ARE in scope:
 *  - "AY Emulation": AYEmuSheet's own content, laid out in two columns
 *    (a GtkHBox of two GtkVBoxes) approximating AYEmuSheet's real
 *    2-column groupbox grid, not its exact pixel coordinates (same
 *    "idiomatic GTK2, faithful in spirit not literal pixels" precedent
 *    as the rest of this file).
 *  - "Digital Sound": WOSheet's GBSRate/GBChans/GBBRate/GBBuffs/
 *    GBDevice/SBStop (MIG-0130) - real ALSA output-device configuration
 *    (sample rate/channels/bit depth/buffer size/device), previously
 *    100% hardcoded (48000Hz/16-bit/stereo/ALSA "default"/200ms fixed
 *    latency) with zero user-facing configurability anywhere in this
 *    port. CheckBox6/CheckBox7 (WOSheet's own read-only "currently
 *    playing in mono/stereo" status lamps, MainWin.pas:1829-1836) are
 *    NOT ported - purely cosmetic dual-indicator redundant with the
 *    RBChStereo/RBChMono radios' own state, see migration_debt.yaml.
 *  - "Global Volume Control" (labeled "Volume" here for brevity):
 *    VolumeSheet, MOVED here from gui/src/tools_win.c (MIG-0129, which
 *    had held it since first implemented, explicitly flagged at the
 *    time as "a session-specific file-ownership split, not a claim
 *    that this is where the real Pascal form lives").
 *
 * Like gui/src/playlist_win.c, this window is created once and
 * persists for the process lifetime; "closing" it (the window-manager
 * X, or ButMixer again) just hides it.
 */
#ifndef GUI_MIXER_WIN_H
#define GUI_MIXER_WIN_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "gui/playback.h"

/* Opaque here - gui/src/mixer_win.c includes the real gui/mainwin.h for
 * the full definition; kept opaque to avoid a circular include (see
 * gui/include/gui/tools_win.h's own identical precedent - gui_mainwin
 * embeds a gui_mixer_win field). */
typedef struct gui_mainwin gui_mainwin;

typedef struct gui_mixer_win {
  GtkWidget* window;
  gui_mainwin* mw; /* not owned - must outlive this window (MIG-0129,
                     * needed for the Volume tab's own mw->sysvol/
                     * sysvol_linear/auto_save_volume_pos access, same
                     * pattern gui_tools_win already established) */
  gui_playback* playback; /* not owned - derived from mw->playback at
                            * create time (kept as its own field since
                            * every OTHER control in this file already
                            * used w->playback directly, long before
                            * `mw` existed here); controls are no-ops
                            * (silently, matching a disabled control
                            * rather than erroring) whenever playback->
                            * loaded is false */
  GtkWidget* scale_al, *scale_ar, *scale_bl, *scale_br, *scale_cl, *scale_cr;
  GtkWidget* scale_bpr; /* Mixer.pas: TBAmpBpr - beeper/digidrum amplitude
                          * (MIG-0106) */
  GtkWidget* scale_dma; /* Mixer.pas: TBAmpDMA - Atari DMA-sound headroom
                          * amplitude (MIG-0123), same GBChAmp groupbox,
                          * immediately after TBAmpBpr (MainWin.pas:3811-
                          * 3814's own AddBitmaps order) */
  GtkWidget* scale_preamp; /* Mixer.pas: TBPreAmp ("Preamp", MIG-0131) -
                             * see ay_engine::pre_amp/pre_amp_max's own doc
                             * comment; previously had zero GUI exposure
                             * despite the engine already fully supporting
                             * it. Same GBChAmp groupbox as AL..DMA above. */
  /* Mixer.pas: EAmpAL/AR/BL/BR/CL/CR/EAmpBpr/EAmpDMA/EPreAmp (MIG-0131) -
   * typed exact-value entry paired with each slider above (real Pascal:
   * `Val(EAmpAL.Text, A, Cde); if (Cde=0) and (A in [0..255]) then
   * FrmMain.SetChan2(A, 0)`), since a 120px vertical slider alone can't
   * address all 256 values precisely. One shared "activate" handler
   * (on_amp_entry_activate) looks up each entry's paired GtkRange via
   * g_object_set_data rather than 9 near-identical trampolines. */
  GtkWidget* entry_al, *entry_ar, *entry_bl, *entry_br, *entry_cl, *entry_cr;
  GtkWidget* entry_bpr, *entry_dma, *entry_preamp;
  /* Mixer.pas: LAYOvfl/LTSOvfl/LDMAOvfl (MIG-0131) - clip-warning labels,
   * shown/hidden per Calculate_Level_Tables2's own 3 conditions
   * (MainWin.pas:1799-1816, see on_sync_timer's own comment) - purely
   * advisory, no audio effect of their own. */
  GtkWidget* label_ay_ovfl, *label_ts_ovfl, *label_dma_ovfl;
  GtkWidget* rb_ay, *rb_ym;
  GtkWidget* rb_filt_avg, *rb_filt_fir; /* Mixer.pas: RBResamAvg/RBResamFIR
                                          * (MIG-0106) */

  /* Mixer.pas: GBChFrq/EChFrqCur and GBIntFrq/EIntFrqCur - read-only
   * "current value" displays (not overrides; the actual override
   * mechanism is ItemEdit.pas's, MIG-0088). Previously not reproduced
   * at all (this file's own header comment used to say so); added by
   * the interrupt-frequency-readout item of the Mixer/Tools/Playlist
   * parity pass - see player_get_ay_freq/player_get_int_freq. */
  GtkWidget* label_ay_freq_cur, *label_int_freq_cur;

  /* Mixer.pas: CBChTypeLst/CheckBox1/CBChFrqLst/CBIntFrqLst/CBChLst -
   * "use the playlist item's own override" checkboxes gating ItemEdit.
   * pas's per-item chip-type/channel-mode/AY-freq/interrupt-freq/
   * channel-count overrides (MIG-0088, channel-count added by MIG-0107).
   * Read directly by gui/src/mainwin.c's do_load_song when starting
   * playback of a playlist entry. */
  GtkWidget* cb_use_chip_type_list;
  GtkWidget* cb_use_channel_mode_list;
  GtkWidget* cb_use_ay_freq_list;
  GtkWidget* cb_use_int_freq_list;
  GtkWidget* cb_use_channel_count_list; /* Mixer.pas: CBChLst */

  /* Mixer.pas: GBZ80Frq (RBZ80FrqZX/RBZ80FrqPnt/RBZ80FrqOther/
   * EZ80FrqOther/EZ80FrqCur) - MIG-0120, a load-time-only Z80 clock
   * override for .ay files (player_set_frq_z80). */
  GtkWidget* rb_z80_zx, *rb_z80_pnt, *rb_z80_other;
  GtkWidget* entry_z80_other;
  GtkWidget* label_z80_cur;

  /* Mixer.pas: GBMCFrq (RBMCFrqST/RBMCFrqOther/EMCFrqOther/EMCFrqCur) -
   * MIG-0120, a load-time-only 68000 clock override for SNDH files
   * (player_set_mc68000_freq). */
  GtkWidget* rb_mc_st, *rb_mc_other;
  GtkWidget* entry_mc_other;
  GtkWidget* label_mc_cur;

  /* Mixer.pas: GBMFPFrq (RBMFPFrq1613/RBMFPFrqST/RBMFPFrqOther/
   * EMFPFrqOther/EMFPFrqCur) - MIG-0120, a load-time-only MFP timer
   * frequency override for SNDH files (player_set_mfp_freq). "1613"
   * matches Mixer.pas's own control name (the Auto formula's 16/13
   * ratio, MainWin.pas:1570) - relabeled "Auto" here for clarity, same
   * meaning. */
  GtkWidget* rb_mfp_auto, *rb_mfp_st, *rb_mfp_other;
  GtkWidget* entry_mfp_other;
  GtkWidget* label_mfp_cur;

  /* Mixer.pas: GBSNDH - STRB/STeRB (MIG-0121, is_ste - see player_load_
   * song's own comment; a LOAD-TIME-ONLY format-variant flag, read by
   * gui/src/mainwin.c's do_load_song via gui_mixer_win_is_ste, same
   * timing as the other overrides above), plus AtariMonoChk/
   * AtariYMMonoChk (SNDH-specific fallback defaults for this port's
   * existing mono-output/channel-mode mechanisms - PlayList.pas:761,798,
   * see gui/src/mainwin.c's do_load_song). */
  GtkWidget* rb_atari_st, *rb_atari_ste;
  GtkWidget* cb_atari_mono, *cb_atari_ym_mono;

  /* Mixer.pas: GBChFrq's own override sub-panel (RBChFrqZX/RBChFrqPnt/
   * RBChFrqST/RBChFrqCPC/RBChFrqOther/EChFrqOther) - MIG-0124, a LIVE
   * AY-chip clock override (player_set_chip_freq, MainWin.pas:1534-
   * 1552's own Set_Chip_Frq) - unlike GBZ80Frq/GBMCFrq/GBMFPFrq above,
   * this applies immediately during playback, not load-time-only,
   * matching Set_Chip_Frq's own real "LIVE" semantics (see player.h's
   * own doc comment). EChFrqCur (label_ay_freq_cur above, MIG-0088-era)
   * is unchanged - still the read-only current-value display, now
   * joined by this real override sub-panel. */
  GtkWidget* rb_chfrq_zx, *rb_chfrq_pnt, *rb_chfrq_st, *rb_chfrq_cpc,
      *rb_chfrq_other;
  GtkWidget* entry_chfrq_other;

  /* Mixer.pas: GBOUTZXAY ("OUT, ZXAY, AY, AYM", MIG-0131) - EOUTStPerFrm
   * ("TStates per frame", a LIVE MaxTStates override - see player_set_
   * n_tact's own doc comment for why this one, unlike GBZ80Frq/GBMCFrq/
   * GBMFPFrq above, has no load-time-only guard) and EIntOffs
   * ("Interrupt offset" - display-only in this port: real Pascal's
   * IntOffset only ever feeds an export/conversion total-time
   * calculation this port doesn't have, so entry_int_offset has no
   * backing player/mainwin state of its own, just clamped to [0,
   * n_tact) on edit, matching Set_N_Tact's own IntOffset>=MaxTStates
   * clamp). Neither is in MainWin.pas:1073-1080's disabled-during-
   * playback list (PlayCurrent), unlike GBSRate/GBBRate/GBBuffs/
   * GBDevice/RBChStereo/RBChMono. */
  GtkWidget* entry_n_tact, *entry_int_offset;

  /* Mixer.pas: GBIntFrq's own override sub-panel (RBEIntFrqZX/
   * RBIntFrqPnt/RBIntFrqOther/EIntFrqOther) - MIG-0124, a LIVE
   * VBL/interrupt-frequency override (player_set_player_freq,
   * MainWin.pas:2043-2062's own Set_Player_Frq2/Set_Player_Frq) -
   * YM/VTX only, a no-op for every other format (player_set_player_
   * freq's own doc comment). EIntFrqCur (label_int_freq_cur above) is
   * unchanged. `EIntFrqOther`'s real Pascal text is a fractional-kHz
   * display (`Interrupt_Freq/1000`, 3 decimals) - this port's own
   * EIntFrqCur/label_int_freq_cur already established a plain-integer
   * Hz-x1000 display instead (see this window's own "(Hz x1000)"
   * label text), so entry_intfrq_other follows that SAME established
   * convention rather than reintroducing the fractional-kHz format
   * for only this one new control. */
  GtkWidget* rb_intfrq_zx, *rb_intfrq_pnt, *rb_intfrq_other;
  GtkWidget* entry_intfrq_other;

  /* Mixer.pas: VolumeSheet (MIG-0129, MOVED here from gui/src/
   * tools_win.c - see this file's own header comment) - real ALSA
   * system-mixer volume control, per the platform-substitution
   * documented in gui/include/gui/alsa_mixer.h (flat ALSA control list
   * replacing the Windows-mixerLine-shaped 3-level picker). */
  GtkWidget* label_volctrl;   /* EVolCtrl - read-only, shows the currently
                                * open control's name (or "none"). */
  GtkWidget* combo_volctrl;   /* BVolCtrlSelect, ALSA-flat-list form -
                                * "changed" reopens mw->sysvol on the
                                * newly picked name. */
  GtkWidget* check_ln_scale;  /* CBLnScale */
  GtkWidget* check_sv_vol_pos; /* CBSvVolPos - read via accessor, same
                                 * established pattern as gui_tools_win_
                                 * force_loop's own lazily-read
                                 * checkboxes. */

  /* Mixer.pas: WOSheet ("Digital Sound" tab, MIG-0130) - GBSRate/
   * GBChans/GBBRate/GBBuffs/GBDevice/SBStop. All write directly into
   * mw->sample_rate/sample_bits/default_channels/buf_len_ms/
   * num_buffers/output_device (gui/include/gui/mainwin.h) - session-
   * wide, load-time-only state, same rationale as VolumeSheet's own
   * mw->sysvol* fields (MIG-0129). Disabled while genuinely playing
   * (gui_mixer_win.c's own digital_sound_locked, on_sync_timer),
   * matching PlayCurrent/RestoreControls' real enable/disable
   * lifecycle (MainWin.pas:1073-1080/1023-1030) - every underlying
   * setter also has its own load-time-only guard as a backstop (see
   * player_set_sample_bits/player_set_number_of_channels's own doc
   * comments), so this is a UX nicety, not a correctness requirement. */
  GtkWidget* ds_frame_srate, *ds_frame_chans, *ds_frame_brate,
      *ds_frame_buffs, *ds_frame_device;
  GtkWidget* rb_sr48k, *rb_sr44k, *rb_sr22k, *rb_sr11k, *rb_sr96k, *rb_sr192k,
      *rb_sr_other;
  GtkWidget* entry_sr_other;
  GtkWidget* rb_ds_stereo, *rb_ds_mono;
  GtkWidget* rb_ds_bit16, *rb_ds_bit8;
  GtkWidget* scale_buf_len, *scale_num_buf;
  GtkWidget* label_buf_len, *label_num_buf, *label_tot_len;
  GtkWidget* combo_device;
  GtkWidget* button_ds_stop;

  bool syncing; /* true while on_sync_timer is programmatically setting
                  * widget values - lets the change handlers skip
                  * apply_and_recalc for those calls without needing
                  * GLib's g_signal_handlers_block_by_func (which trips
                  * -Wpedantic: casting a function pointer to gpointer is
                  * non-conforming strict ISO C, even though GTK2/GLib
                  * itself relies on it internally) */
  guint sync_timer_id; /* periodically re-syncs the sliders/radio
                         * buttons to the currently-loaded file's actual
                         * values (each new file load resets ay_engine
                         * to its own defaults - see gui_mainwin.c's
                         * do_load_song - so the Mixer window's controls
                         * would otherwise silently go stale after
                         * Open/Next/Prev) */
} gui_mixer_win;

/* `mw` must outlive the mixer window (MIG-0129 - changed from a bare
 * `gui_playback*` to the owning gui_mainwin, matching gui_tools_win_
 * create's own established signature, needed for the new Volume tab's
 * mw->sysvol access - w->playback is still derived internally as
 * &mw->playback, unchanged for every pre-existing caller in this
 * file). */
void gui_mixer_win_create(gui_mixer_win* w, GtkWindow* parent,
                           gui_mainwin* mw);
void gui_mixer_win_toggle_visible(gui_mixer_win* w);
void gui_mixer_win_destroy(gui_mixer_win* w);

/* Convenience accessors for gui/src/mainwin.c's do_load_song, so it
 * doesn't need to reach into GtkToggleButton internals directly. */
bool gui_mixer_win_use_chip_type_list(const gui_mixer_win* w);
bool gui_mixer_win_use_channel_mode_list(const gui_mixer_win* w);
bool gui_mixer_win_use_ay_freq_list(const gui_mixer_win* w);
bool gui_mixer_win_use_int_freq_list(const gui_mixer_win* w);
bool gui_mixer_win_use_channel_count_list(const gui_mixer_win* w);

/* Mixer.pas: GBSNDH's STRB/STeRB radio pair (MIG-0121) - true (STe,
 * default matching this port's own pre-existing unconditional-DMA-sound
 * behavior) unless the plain-ST radio is selected. Read by gui/src/
 * mainwin.c's do_load_song, same load-time-only timing as the other
 * gui_mixer_win_use_*_list accessors above. */
bool gui_mixer_win_is_ste(const gui_mixer_win* w);

/* Mixer.pas: GBSNDH's AtariMonoChk/AtariYMMonoChk (default unchecked,
 * matching Mixer.lfm - neither control has a Checked=True attribute).
 * Read by gui/src/mainwin.c's do_load_song (PlayList.pas:761,798's own
 * SNDH-specific fallback-default precedence). */
bool gui_mixer_win_atari_mono(const gui_mixer_win* w);
bool gui_mixer_win_atari_ym_mono(const gui_mixer_win* w);

/* Mixer.pas: CBSvVolPos/AutoSaveVolumePos (MIG-0129 - MOVED here from
 * gui_tools_win_auto_save_volume_pos, along with the rest of
 * VolumeSheet) - same lazy-read contract, read by gui/src/mainwin.c's
 * gui_mainwin_save_settings at save time. */
bool gui_mixer_win_auto_save_volume_pos(const gui_mixer_win* w);

#endif /* GUI_MIXER_WIN_H */
