/* See gui/include/gui/alsa_mixer.h's own file comment for the full
 * rationale/platform-substitution notes. Same _GNU_SOURCE-before-any-
 * include requirement as tools/ay_player/src/alsa_output.c (asoundlib's
 * global.h redeclares `struct timespec` under strict -std=c11 without
 * it) - see that file's own comment for the full explanation, unchanged
 * here. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <alsa/asoundlib.h>

#include "gui/alsa_mixer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct gui_alsa_mixer {
  snd_mixer_t* handle;
  snd_mixer_elem_t* elem;
  char* card;       /* g_strdup/strdup'd copy of the attach target */
  char* selem_name; /* g_strdup/strdup'd copy of the control name */
  long vol_min, vol_max;
};

static bool has_usable_volume(snd_mixer_elem_t* elem, long* min, long* max) {
  if (!snd_mixer_selem_has_playback_volume(elem)) return false;
  if (snd_mixer_selem_get_playback_volume_range(elem, min, max) != 0)
    return false;
  return *max > *min; /* mixerctl.pas:387 - SelectMixerControl2's own guard */
}

/* Shared by gui_alsa_mixer_enumerate and gui_alsa_mixer_open's
 * auto-detect path - opens+attaches+loads a fresh handle on `card`,
 * hands the walk of its elements to `visit`, then always tears the
 * handle down again (callers that want to KEEP a handle open, i.e.
 * gui_alsa_mixer_open's explicit-name success path, reopen their own
 * separately below rather than threading an ownership handoff through
 * this helper - simpler than a second code path here). */
typedef void (*elem_visitor)(snd_mixer_elem_t* elem, void* userdata);

static bool walk_elems(const char* card, elem_visitor visit, void* userdata) {
  const char* c = (card && card[0]) ? card : "default";
  snd_mixer_t* handle = NULL;
  if (snd_mixer_open(&handle, 0) != 0) return false;
  bool ok = false;
  if (snd_mixer_attach(handle, c) == 0 &&
      snd_mixer_selem_register(handle, NULL, NULL) == 0 &&
      snd_mixer_load(handle) == 0) {
    ok = true;
    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(handle); elem;
         elem = snd_mixer_elem_next(elem)) {
      visit(elem, userdata);
    }
  }
  snd_mixer_close(handle); /* also detaches/frees - snd_mixer_close's own
                             * documented contract covers both */
  return ok;
}

struct enum_ctx {
  char** names;
  int count;
};

static void enum_visit(snd_mixer_elem_t* elem, void* userdata) {
  struct enum_ctx* ctx = (struct enum_ctx*)userdata;
  long min, max;
  if (!has_usable_volume(elem, &min, &max)) return;
  ctx->names = realloc(ctx->names, sizeof(char*) * (size_t)(ctx->count + 1));
  ctx->names[ctx->count] = strdup(snd_mixer_selem_get_name(elem));
  ctx->count++;
}

char** gui_alsa_mixer_enumerate(const char* card, int* out_count) {
  struct enum_ctx ctx = {.names = NULL, .count = 0};
  if (!walk_elems(card, enum_visit, &ctx)) {
    *out_count = 0;
    return NULL;
  }
  *out_count = ctx.count;
  return ctx.names;
}

void gui_alsa_mixer_free_names(char** names, int count) {
  if (!names) return;
  for (int i = 0; i < count; i++) free(names[i]);
  free(names);
}

struct detect_ctx {
  char* master;
  char* pcm;
  char* first;
};

static void detect_visit(snd_mixer_elem_t* elem, void* userdata) {
  struct detect_ctx* ctx = (struct detect_ctx*)userdata;
  long min, max;
  if (!has_usable_volume(elem, &min, &max)) return;
  const char* name = snd_mixer_selem_get_name(elem);
  if (!ctx->first) ctx->first = strdup(name);
  if (!ctx->master && strcmp(name, "Master") == 0) ctx->master = strdup(name);
  if (!ctx->pcm && strcmp(name, "PCM") == 0) ctx->pcm = strdup(name);
}

gui_alsa_mixer* gui_alsa_mixer_open(const char* card, const char* selem_name) {
  const char* c = (card && card[0]) ? card : "default";
  char* chosen = NULL;

  if (selem_name && selem_name[0]) {
    chosen = strdup(selem_name);
  } else {
    /* alsa_mixer.h: gui_alsa_mixer_open's own auto-detect preference
     * order (Master, then PCM, then first-found) - see its own comment
     * for how this closes mixerctl.pas's own "todo real detect" gap. */
    struct detect_ctx ctx = {0};
    walk_elems(c, detect_visit, &ctx);
    chosen = ctx.master ? ctx.master : (ctx.pcm ? ctx.pcm : ctx.first);
    if (ctx.master && chosen != ctx.master) free(ctx.master);
    if (ctx.pcm && chosen != ctx.pcm) free(ctx.pcm);
    if (ctx.first && chosen != ctx.first) free(ctx.first);
    if (!chosen) return NULL;
  }

  snd_mixer_t* handle = NULL;
  if (snd_mixer_open(&handle, 0) != 0) {
    free(chosen);
    return NULL;
  }
  if (snd_mixer_attach(handle, c) != 0 ||
      snd_mixer_selem_register(handle, NULL, NULL) != 0 ||
      snd_mixer_load(handle) != 0) {
    snd_mixer_close(handle);
    free(chosen);
    return NULL;
  }

  snd_mixer_elem_t* found = NULL;
  long min = 0, max = 0;
  for (snd_mixer_elem_t* elem = snd_mixer_first_elem(handle); elem;
       elem = snd_mixer_elem_next(elem)) {
    if (strcmp(snd_mixer_selem_get_name(elem), chosen) != 0) continue;
    if (!has_usable_volume(elem, &min, &max)) continue;
    found = elem;
    break;
  }
  if (!found) {
    snd_mixer_close(handle);
    free(chosen);
    return NULL;
  }

  gui_alsa_mixer* m = calloc(1, sizeof(*m));
  m->handle = handle;
  m->elem = found;
  m->card = strdup(c);
  m->selem_name = chosen;
  m->vol_min = min;
  m->vol_max = max;
  return m;
}

void gui_alsa_mixer_close(gui_alsa_mixer* m) {
  if (!m) return;
  if (m->handle) snd_mixer_close(m->handle);
  free(m->card);
  free(m->selem_name);
  free(m);
}

const char* gui_alsa_mixer_selem_name(const gui_alsa_mixer* m) {
  return m->selem_name;
}

/* MainWin.pas:4233-4247 SetSysVolume's exact two curves, mapping the
 * skinned slider's own [0,1] fraction to the mixer's real [0,1]
 * fraction (this module then further rescales that onto vol_min..
 * vol_max, which SetSysVolume's own mixerctl_setvolume(v:single) did
 * internally on the Pascal side). */
static double ui_to_mixer_fraction(double v, bool linear) {
  if (v < 0.0) v = 0.0;
  if (v > 1.0) v = 1.0;
  if (linear) return v;
  return (exp(v) - 1.0) / (exp(1.0) - 1.0);
}

/* MainWin.pas:4204-4231 GetSysVolume's inverse of the curve above. */
static double mixer_to_ui_fraction(double v, bool linear) {
  if (v < 0.0) v = 0.0;
  if (v > 1.0) v = 1.0;
  if (linear) return v;
  return log(v * (exp(1.0) - 1.0) + 1.0);
}

bool gui_alsa_mixer_set_volume(gui_alsa_mixer* m, double v, bool linear) {
  double frac = ui_to_mixer_fraction(v, linear);
  long raw = m->vol_min + (long)lround(frac * (double)(m->vol_max - m->vol_min));
  if (raw < m->vol_min) raw = m->vol_min;
  if (raw > m->vol_max) raw = m->vol_max;
  return snd_mixer_selem_set_playback_volume_all(m->elem, raw) == 0;
}

bool gui_alsa_mixer_get_volume(gui_alsa_mixer* m, double* v, bool linear) {
  long raw = 0;
  /* Reads the first channel that reports a value - mixerctl.pas's own
   * GetSysVolume/mixerctl_getvolume averages/uses one representative
   * channel too (its own UpdateBalans logic is about a separate stereo-
   * balance side effect this port doesn't track); front-left is always
   * present on every real control this module accepts (has_usable_
   * volume already required at least playback volume support). */
  if (snd_mixer_selem_get_playback_volume(m->elem, SND_MIXER_SCHN_FRONT_LEFT,
                                           &raw) != 0)
    return false;
  double frac = (m->vol_max > m->vol_min)
                    ? (double)(raw - m->vol_min) / (double)(m->vol_max - m->vol_min)
                    : 0.0;
  *v = mixer_to_ui_fraction(frac, linear);
  return true;
}
