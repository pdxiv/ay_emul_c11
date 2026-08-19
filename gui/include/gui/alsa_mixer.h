/* Thin wrapper directly over ALSA's asoundlib.h SIMPLE-MIXER API
 * (snd_mixer_open/attach/selem_register/load/find_selem/
 * selem_get_playback_volume_range/selem_set_playback_volume_all) - the
 * genuinely new subsystem this session adds (Part A): before this file,
 * grep confirms ZERO snd_mixer_* code existed anywhere in this
 * codebase. A completely different ALSA API family from tools/ay_player/
 * src/alsa_output.c's snd_pcm_* code (PCM playback) sitting right next
 * to it in gui/Makefile's SRC wildcard - that file's own header comment
 * ("a thin wrapper directly over ALSA's asoundlib.h... replaces the
 * original's large hand-maintained fpalsa Pascal binding") is this
 * file's own model, just for the hardware MIXER instead of the PCM
 * device.
 *
 * Real Pascal: mixerctl.pas's own Linux branch (guarded `{$ifdef
 * Unix}`-equivalent in that file) already uses this exact same ALSA
 * simple-mixer API 1:1 (SelectMixerControl2 at mixerctl.pas:345-421) -
 * this is a real, not merely analogous, ALSA-idiom match, just ported
 * from FPC's alsa binding units to plain C directly against
 * <alsa/asoundlib.h>.
 *
 * PLATFORM SUBSTITUTION (documented, not silent - same standard as this
 * port's own existing tray-icon substitution, MIG-0073's "a generic
 * freedesktop icon-theme name is used instead of the app's own embedded
 * Windows ICON00-ICON99 resources"): Tools.pas/Mixer.pas's VolumeSheet
 * (BVolCtrlSelect/BVolCtrlDetect/EVolCtrl) lets the user pick a mixer
 * "device -> subdevice -> control" 3-level path (mixerctl.pas's own
 * Tmixerctl_list tree - a Windows MMSYSTEM mixerLine-enumeration shape
 * that the Linux backend above ALSO builds, purely to keep one shared
 * UI dialog working on both platforms, even though ALSA itself has no
 * such 3-level concept: a simple mixer's controls are just a flat list
 * on the default card). This module exposes that same flat list
 * directly (gui_alsa_mixer_enumerate) instead of replicating the
 * artificial 3-level tree/dialog - the closest faithful equivalent
 * given the platform difference, matching this project's established
 * norm for platform-appropriate substitution over literal structural
 * mimicry.
 */
#ifndef GUI_ALSA_MIXER_H
#define GUI_ALSA_MIXER_H

#include <stdbool.h>

typedef struct gui_alsa_mixer gui_alsa_mixer;

/* Lists every simple-mixer control on `card` (NULL/"" -> "default",
 * matching mixerctl.pas's own DetectMixerControl/mixerctl_enumerate
 * default attach target) that actually has a playback-volume range
 * (snd_mixer_selem_has_playback_volume + a non-empty min..max range,
 * same guard SelectMixerControl2 itself applies at mixerctl.pas:384-389
 * before accepting a control) - so read-only/capture-only/switch-only
 * elements (e.g. "Auto-Mute Mode", "Internal Mic Boost") never show up
 * as selectable volume controls.
 *
 * Returns a newly g_strdup'd/g_new'd NULL-free array of `*out_count`
 * name strings (free with gui_alsa_mixer_free_names), or NULL with
 * *out_count = 0 if the mixer device itself couldn't be opened at all
 * (no error detail beyond that - matching mixerctl_enumerate's own
 * plain integer-or-zero return contract). */
char** gui_alsa_mixer_enumerate(const char* card, int* out_count);
void gui_alsa_mixer_free_names(char** names, int count);

/* Opens `card`'s (NULL/"" -> "default") `selem_name` simple-mixer
 * control (mixerctl.pas: SelectMixerControl2's Path1/Path3 - Path2, the
 * artificial middle tree level from the header comment above, has no
 * equivalent here). If `selem_name` is NULL/"", auto-detects: tries
 * "Master" first, then "PCM", then the first control gui_alsa_mixer_
 * enumerate would have listed - a real (if simpler) equivalent of
 * BVolCtrlDetectClick/DetectMixerControl (mixerctl.pas:425-440, whose
 * own comment reads `//todo real detect (ctls->Master, PCM, etc) like
 * in gstalsamixer.c` - this preference order is exactly that
 * still-open todo, finished here since a real preference order costs
 * nothing extra to add). Returns NULL on failure (device missing, no
 * such control, or the found control has no usable volume range). */
gui_alsa_mixer* gui_alsa_mixer_open(const char* card, const char* selem_name);
void gui_alsa_mixer_close(gui_alsa_mixer* m);

/* The actual control name in use (never NULL once open succeeded) -
 * Mixer.pas: EVolCtrl.Text := s (mixerctl_title). */
const char* gui_alsa_mixer_selem_name(const gui_alsa_mixer* m);

/* v in [0,1] UI-slider space. `linear` selects the exact same curve
 * MainWin.pas's SetSysVolume/GetSysVolume (MainWin.pas:4233-4247/
 * 4204-4231) apply between VolumeCtrl (the skinned slider's own pixel
 * position, this module's `v`) and the real 0..1 mixer fraction:
 *   linear:      mixer_fraction = v
 *   logarithmic: mixer_fraction = (exp(v) - 1) / (exp(1) - 1)
 * (CBLnScale unchecked - the original's default/non-linear branch,
 * chosen because a mixer's own native range is perceptually
 * logarithmic, so this curve makes slider MOTION feel linear to the
 * ear even though the underlying hardware range isn't - VolLinear
 * itself defaults true in MainWin.pas, see gui_alsa_mixer_open's own
 * caller in tools_win.c for this port's matching default). Returns
 * false if the underlying snd_mixer_selem_set_playback_volume_all call
 * fails. */
bool gui_alsa_mixer_set_volume(gui_alsa_mixer* m, double v, bool linear);

/* Inverse of the curve above, reading the control's current hardware
 * level back into UI-slider space - MainWin.pas: GetSysVolume. Returns
 * false if the read itself failed (leaves *v untouched). */
bool gui_alsa_mixer_get_volume(gui_alsa_mixer* m, double* v, bool linear);

#endif /* GUI_ALSA_MIXER_H */
