/* C11/GTK2 port of mxhelper.pas's TFrmMxHlp - a stereo channel-mode
 * preset picker, launched from the Mixer window's "Presets..." button
 * (Mixer.pas: SBHelper/SBHelperClick). mxhelper.pas itself has NO
 * logic at all (a bare form declaration, empty implementation) - all
 * behavior lives in the caller, TFrmMixer.SBHelperClick, which this
 * file ports.
 *
 * Ported: the 13-way channel-allocation radio group (AY/YM x ABC/ACB/
 * BAC/BCA/CAB/CBA Stereo, plus Mono) and MainWin.pas's CalcModeCoefs
 * (1953-2027), the pure arithmetic that turns a (Mode, ChType) pair
 * into the six index_al/ar/bl/br/cl/cr values - matching
 * MainWin.pas's own Set_Mode (2034-2041), which applies a preset via
 * this exact same arithmetic and, notably, discards CalcModeCoefs's
 * own BeeperMax/Atari_DMAMax out-params into throwaway locals rather
 * than writing them back. Both of those ARE real, ported Mixer
 * controls now (beeper_max/TBAmpBpr - MIG-0106; atari_dma_max/
 * TBAmpDMA - MIG-0123, see gui/include/gui/mixer_win.h) - this
 * dialog simply doesn't touch either, matching Set_Mode's own real
 * behavior exactly, not a gap.
 *
 * NOT ported: the TSDMAChG check-group (ZX Turbo-Sound / STe DMA-Sound
 * "leave space for additional device" options) and the PreAmp-search
 * loop that follows it in Mixer.pas's SBHelperClick (601-634,
 * decrementing PreAmp and recalculating level tables until no
 * overflow indicator would be shown) - unlike Set_Mode above,
 * SBHelperClick's own real implementation DOES call CalcModeCoefs
 * with the TSDMAChG checkboxes' live state and DOES write the
 * resulting BeeperMax/Atari_DMAMax back to the real fields, then
 * auto-tunes PreAmp to avoid clipping. That headroom-reservation/
 * auto-tune behavior is a genuinely separate, smaller convenience
 * feature layered on top of the same preset arithmetic - not blocked
 * on any missing engine plumbing (player_ay_engine already exposes
 * atari_dma_max/pre_amp directly), just not implemented: a real user
 * can reach the same end state manually via the Mixer window's own
 * DMA/PreAmp controls. See migration_debt.yaml MIG-0123 for the full
 * citation trail.
 */
#ifndef GUI_DIALOGS_MXHELPER_H
#define GUI_DIALOGS_MXHELPER_H

#include <gtk/gtk.h>
#include <stdint.h>

#include "gui/playback.h"

/* No-op if `playback` isn't loaded. Otherwise shows a modal dialog with
 * the 13 preset radio buttons and Set/Cancel; on Set, applies the
 * selected preset's channel amplitude/pan values and chip type via
 * player_ay_engine() + ay_engine_calculate_level_tables() (the exact
 * same primitives gui/src/mixer_win.c's own sliders use). */
void gui_mxhelper_show(GtkWindow* parent, gui_playback* playback);

/* MainWin.pas: CalcModeCoefs (1953-2027), exposed for
 * ItemEdit.pas's per-item channel_mode override (MIG-0088) to reuse
 * the same preset arithmetic this dialog's own apply_preset uses
 * internally - matches Set_Mode's own six-index-only output (see
 * this header's own file comment for why BeeperMax/Atari_DMAMax are
 * correctly left untouched by this path, and what's still genuinely
 * unported: SBHelperClick's own TSDMAChG/PreAmp-search headroom
 * reservation). `mode` is 0 (Mono) - 6; `echo` is 85 for AY_Chip, 13
 * for YM_Chip. */
void mxhelper_calc_mode_coefs(int mode, int echo, uint8_t* al, uint8_t* ar,
                               uint8_t* bl, uint8_t* br, uint8_t* cl,
                               uint8_t* cr);

#endif /* GUI_DIALOGS_MXHELPER_H */
