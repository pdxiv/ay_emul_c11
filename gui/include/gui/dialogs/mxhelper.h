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
 * into the six index_al/ar/bl/br/cl/cr values.
 *
 * NOT ported: the TSDMAChG check-group (ZX Turbo-Sound / STe DMA-Sound
 * "leave space for additional device" options) and the PreAmp-search
 * loop that follows it in SBHelperClick (decrementing PreAmp and
 * recalculating level tables until no overflow indicator would be
 * shown) - both are about Atari DMA-sound/TurboSound channel
 * headroom, a feature this port's Mixer window doesn't have at all
 * (MIG-0078 scoped Mixer to the core AY channels only, see gui/
 * include/gui/mixer_win.h's own file comment for the itemized list of
 * what's out of scope there) - matching that same scope decision here.
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
 * internally - see mxhelper.c's own comment on what's dropped
 * (DMA/TS/BeeperMax, PreAmp search). `mode` is 0 (Mono) - 6; `echo` is
 * 85 for AY_Chip, 13 for YM_Chip. */
void mxhelper_calc_mode_coefs(int mode, int echo, uint8_t* al, uint8_t* ar,
                               uint8_t* bl, uint8_t* br, uint8_t* cl,
                               uint8_t* cr);

#endif /* GUI_DIALOGS_MXHELPER_H */
