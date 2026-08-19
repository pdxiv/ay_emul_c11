/* Must precede every #include (even our own headers, which transitively
 * pull in <stdint.h> -> <features.h> and lock in glibc's own lower
 * default first otherwise, triggering a harmless but noisy
 * "_POSIX_C_SOURCE redefined" warning). */
#define _POSIX_C_SOURCE 200809L /* for strcasecmp under strict -std=c11 */
#include "gui/playlist.h"

#include <ctype.h>
#include <dirent.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "ay_engine/player.h"

/* Same 18 formats engine/include/ay_engine/player.h dispatches - not a
 * separate/duplicated detection table, just the set of extensions worth
 * probing during a directory scan (player_load_song itself is still the
 * real, final arbiter of playability - see gui_playlist_add_file). */
static const char* const EXTENSIONS[] = {
    ".ay",  ".ym",  ".pt3", ".sndh", ".vtx", ".pt1", ".gtr", ".fls",
    ".stc", ".stp", ".pt2", ".fxm",  ".psm", ".asc", ".as0", ".ftc",
    ".psc", ".sqt",
};
#define NUM_EXTENSIONS (int)(sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]))

static bool has_recognized_extension(const char* path) {
  const char* dot = strrchr(path, '.');
  if (!dot) return false;
  for (int i = 0; i < NUM_EXTENSIONS; i++) {
    if (strcasecmp(dot, EXTENSIONS[i]) == 0) return true;
  }
  return false;
}

static const char* basename_of(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/* Truncation here (a source string >= cap bytes) is intentional and
 * safe - dst is always explicitly NUL-terminated on the next line
 * regardless - silencing the compiler's (accurate but unhelpful here)
 * truncation warning for just this function, same precedent as
 * format_scroll_string below. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
static void copy_bounded(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}
#pragma GCC diagnostic pop

/* CP1251->UTF-8 - see gui/src/playback.c's identical helper for the
 * full rationale (duplicated rather than shared, same precedent as
 * basename_of above: both files are small and self-contained). */
static void cp1251_to_utf8(const char* raw, char* out, size_t cap) {
  out[0] = '\0';
  if (!raw || raw[0] == '\0' || cap == 0) return;
  gchar* converted = g_convert(raw, -1, "UTF-8", "CP1251", NULL, NULL, NULL);
  if (converted) {
    strncpy(out, converted, cap - 1);
    out[cap - 1] = '\0';
    g_free(converted);
  }
}

/* PlayList.pas: FormatScrollString (2475-2493) - "Author - Title" when
 * both are known, else whichever one is, else the filename. A pathological
 * combined author+title longer than `cap` (gui_playlist_entry::display is
 * 300 bytes; author/title are each capped at 256) is intentionally
 * truncated by snprintf - fine for a display label, not a correctness
 * issue, hence silencing the compiler's (accurate but unhelpful here)
 * truncation warning for just this function. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void format_scroll_string(const char* author, const char* title,
                                  const char* path, char* out, size_t cap) {
  if (author[0] && title[0]) {
    snprintf(out, cap, "%s - %s", author, title);
  } else if (author[0]) {
    snprintf(out, cap, "%s", author);
  } else if (title[0]) {
    snprintf(out, cap, "%s", title);
  } else {
    snprintf(out, cap, "%s", basename_of(path));
  }
}
#pragma GCC diagnostic pop

void gui_playlist_entry_refresh_display(gui_playlist_entry* e) {
  const char* author =
      e->overrides.author[0] ? e->overrides.author : e->author;
  const char* title = e->overrides.title[0] ? e->overrides.title : e->title;
  format_scroll_string(author, title, e->path, e->display,
                        sizeof(e->display));
  if (e->song_count > 1) {
    size_t len = strlen(e->display);
    snprintf(e->display + len, sizeof(e->display) - len, " (song %d/%d)",
              e->song_index + 1, e->song_count);
  }
  /* MIG-0112: PlayList.pas's GetPlayListFileType appends 'x2' to the
   * file-type column for a Turbosound-paired item whose pair plays the
   * SAME file (the only case this port's own .ayl support produces -
   * see gui_playlist_entry's own comment). This port has no separate
   * file-type column to append that suffix to (playlist_win.c only
   * ever renders `display`), so the indicator is folded into `display`
   * itself instead - same signal (pairing status is visible somewhere
   * in the playlist), relocated to the one place this port actually
   * shows per-item text. */
  if (e->has_ts_pair) {
    size_t len = strlen(e->display);
    snprintf(e->display + len, sizeof(e->display) - len, " [x2]");
  }
}

/* PlayList.pas:902-906's own PLDef_* initial values. */
void gui_playlist_defaults_init(gui_playlist_defaults* d) {
  memset(d, 0, sizeof(*d));
  d->channel_mode = -1;
  d->ay_freq = -1;
  d->int_freq = -1;
}

void gui_playlist_init(gui_playlist* pl) {
  memset(pl, 0, sizeof(*pl));
  pl->current = -1;
}

void gui_playlist_clear(gui_playlist* pl) {
  pl->count = 0;
  pl->current = -1;
}

void gui_playlist_free(gui_playlist* pl) {
  free(pl->items);
  memset(pl, 0, sizeof(*pl));
  pl->current = -1;
}

/* PlayList.pas: Deduplicate1Click - see playlist.h's own comment for
 * the (path, song_index) key this uses in place of the original's
 * (FileName, FormatSpec, Offset) triple. */
int gui_playlist_dedup(gui_playlist* pl) {
  int removed = 0;
  for (int i = 0; i < pl->count - 1; i++) {
    for (int j = i + 1; j < pl->count;) {
      if (strcmp(pl->items[i].path, pl->items[j].path) == 0 &&
          pl->items[i].song_index == pl->items[j].song_index) {
        gui_playlist_remove(pl, j);
        removed++;
      } else {
        j++;
      }
    }
  }
  return removed;
}

void gui_playlist_remove(gui_playlist* pl, int index) {
  if (index < 0 || index >= pl->count) return;
  memmove(&pl->items[index], &pl->items[index + 1],
          (size_t)(pl->count - index - 1) * sizeof(gui_playlist_entry));
  pl->count--;
  if (pl->current == index) {
    pl->current = -1;
  } else if (pl->current > index) {
    pl->current--;
  }
}

static gui_playlist_entry* push_entry(gui_playlist* pl) {
  if (pl->count == pl->capacity) {
    int new_cap = pl->capacity ? pl->capacity * 2 : 64;
    gui_playlist_entry* items = (gui_playlist_entry*)realloc(
        pl->items, (size_t)new_cap * sizeof(gui_playlist_entry));
    if (!items) return NULL;
    pl->items = items;
    pl->capacity = new_cap;
  }
  gui_playlist_entry* e = &pl->items[pl->count++];
  memset(e, 0, sizeof(*e));
  e->overrides.channel_mode = -1;
  e->overrides.ay_freq = -1;
  e->overrides.int_freq = -1;
  e->id = pl->next_id++;
  return e;
}

/* Loads `path`+`song_index` via the real player.h dispatch (the same
 * one playback uses - so "was this added to the playlist" and "will
 * this actually play" never disagree) and fills in song_count plus
 * UTF-8-converted author/title/comment for that specific song. Returns
 * false if the file/song can't be loaded. `data`/`size` are the
 * already-read file bytes (read once by the caller, reused across every
 * subsong - see gui_playlist_add_file). */
static bool probe_song(const uint8_t* data, size_t size, const char* path,
                        int song_index, int* out_song_count,
                        char* out_author, char* out_title,
                        char* out_comment, player_format* out_format,
                        double* out_duration_seconds) {
  player p;
  player_status st =
      player_load_song(&p, path, data, size, 48000, song_index, true);
  if (st != PLAYER_OK) return false;
  *out_song_count = player_song_count(&p);
  if (out_format) *out_format = p.format;

  char raw_author[256], raw_title[256], raw_comment[256];
  player_get_metadata_raw(&p, raw_author, sizeof(raw_author), raw_title,
                           sizeof(raw_title), raw_comment,
                           sizeof(raw_comment));
  cp1251_to_utf8(raw_author, out_author, 256);
  cp1251_to_utf8(raw_title, out_title, 256);
  cp1251_to_utf8(raw_comment, out_comment, 256);

  if (out_duration_seconds) {
    int64_t counter, max;
    *out_duration_seconds =
        (player_get_tick_position(&p, &counter, &max) && max > 0)
            ? (double)max * player_get_seconds_per_tick(&p)
            : 0.0;
  }

  player_free(&p);
  return true;
}

int gui_playlist_add_file(gui_playlist* pl, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) { fclose(f); return 0; }
  size_t read = fread(data, 1, (size_t)size, f);
  fclose(f);
  if (read != (size_t)size) { free(data); return 0; }

  int song_count = 0;
  char author[256], title[256], comment[256];
  player_format format;
  double duration0;
  if (!probe_song(data, read, path, 0, &song_count, author, title, comment,
                   &format, &duration0)) {
    free(data);
    return 0;
  }

  /* Author/comment are read once, outside the per-song loop, matching
   * Players.pas:7154-7167's OpenAYFile exactly (AuthorString/
   * MiscString are computed before the song loop; only SongName/Title
   * varies per subsong - refetched below for every song_index > 0). */
  for (int i = 0; i < song_count; i++) {
    char this_title[256];
    double this_duration;
    if (i == 0) {
      strncpy(this_title, title, sizeof(this_title) - 1);
      this_title[sizeof(this_title) - 1] = '\0';
      this_duration = duration0;
    } else {
      int unused_count;
      char unused_author[256], unused_comment[256];
      if (!probe_song(data, read, path, i, &unused_count, unused_author,
                       this_title, unused_comment, NULL, &this_duration)) {
        this_title[0] = '\0';
        this_duration = 0.0;
      }
    }

    gui_playlist_entry* e = push_entry(pl);
    if (!e) { free(data); return i; } /* OOM - keep what's added so far */
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->song_index = i;
    e->song_count = song_count;
    e->format = format;
    e->duration_seconds = this_duration;
    copy_bounded(e->author, sizeof(e->author), author);
    copy_bounded(e->title, sizeof(e->title), this_title);
    gui_playlist_entry_refresh_display(e);
  }
  free(data);
  return song_count;
}

/* MIG-0118: adds exactly ONE subsong (song_index) of `path` without
 * expanding every other subsong the file may have - used by
 * gui_playlist_load_ayl when reloading a `FormatSpec=N` line (a
 * multi-song .ay entry this port's own .ayl writer produced, one line
 * per subsong - see that function's own comment) instead of the full
 * gui_playlist_add_file expansion every other add path uses. Mirrors
 * gui_playlist_add_file's own per-song body (author probed once at
 * song_index 0, matching Players.pas's OpenAYFile: AuthorString is
 * file-wide, only SongName/Title varies per subsong) without its outer
 * loop. Returns false if the file/song can't be loaded or song_index is
 * out of range. */
static bool add_single_song_entry(gui_playlist* pl, const char* path,
                                   int song_index) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) { fclose(f); return false; }
  size_t read = fread(data, 1, (size_t)size, f);
  fclose(f);
  if (read != (size_t)size) { free(data); return false; }

  int song_count = 0;
  char author[256], unused_title[256], comment[256];
  player_format format;
  double duration0;
  if (!probe_song(data, read, path, 0, &song_count, author, unused_title,
                   comment, &format, &duration0)) {
    free(data);
    return false;
  }
  if (song_index < 0 || song_index >= song_count) {
    free(data);
    return false;
  }

  char title[256];
  double duration;
  int unused_count;
  char unused_author[256], unused_comment[256];
  if (song_index == 0) {
    strncpy(title, unused_title, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    duration = duration0;
  } else if (!probe_song(data, read, path, song_index, &unused_count,
                          unused_author, title, unused_comment, NULL,
                          &duration)) {
    title[0] = '\0';
    duration = 0.0;
  }
  free(data);

  gui_playlist_entry* e = push_entry(pl);
  if (!e) return false;
  copy_bounded(e->path, sizeof(e->path), path);
  e->song_index = song_index;
  e->song_count = song_count;
  e->format = format;
  e->duration_seconds = duration;
  copy_bounded(e->author, sizeof(e->author), author);
  copy_bounded(e->title, sizeof(e->title), title);
  gui_playlist_entry_refresh_display(e);
  return true;
}

int gui_playlist_add_directory(gui_playlist* pl, const char* dir_path,
                                bool recurse,
                                gui_playlist_progress_cb on_progress,
                                void* userdata) {
  DIR* d = opendir(dir_path);
  if (!d) return 0;

  int added = 0;
  int examined = 0;
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;

    char child[1024];
    snprintf(child, sizeof(child), "%s/%s", dir_path, ent->d_name);

    struct stat st;
    if (stat(child, &st) != 0) continue;

    if (S_ISDIR(st.st_mode)) {
      if (recurse)
        added +=
            gui_playlist_add_directory(pl, child, true, on_progress, userdata);
    } else if (S_ISREG(st.st_mode) && has_recognized_extension(child)) {
      added += gui_playlist_add_file(pl, child);
    }

    examined++;
    if (on_progress && !on_progress(examined, userdata)) {
      closedir(d);
      return added; /* aborted */
    }
  }
  closedir(d);
  return added;
}

/* ASCII-only case-insensitive substring search (strstr on lowercased
 * copies) - a documented narrowing of FindPLItem.pas's UTF8LowerCase
 * (full-Unicode case folding); correct for every-day ASCII author/title
 * text, but a non-ASCII needle or haystack byte is compared as-is
 * rather than Unicode-case-folded. `hay`/`needle` are truncated to 511
 * bytes for this comparison - ample for author/title/filename fields
 * (all capped at 256 bytes themselves). */
static bool ascii_ci_contains(const char* hay, const char* needle) {
  if (!needle[0]) return true; /* empty search string matches everything,
                                 * matching Pos('', s) > 0 being false in
                                 * Pascal - see gui_playlist_find's own
                                 * caller, which never calls this with an
                                 * empty needle in the first place (mirrors
                                 * FindItem2's real behavior indirectly:
                                 * empty search text on the original just
                                 * never matches Pos(...)>0, but an empty
                                 * search is a degenerate no-op case not
                                 * worth replicating exactly here) */
  char h[512], n[512];
  size_t i;
  for (i = 0; hay[i] && i < sizeof(h) - 1; i++)
    h[i] = (char)tolower((unsigned char)hay[i]);
  h[i] = '\0';
  for (i = 0; needle[i] && i < sizeof(n) - 1; i++)
    n[i] = (char)tolower((unsigned char)needle[i]);
  n[i] = '\0';
  return strstr(h, n) != NULL;
}

static bool entry_matches(const gui_playlist_entry* e,
                           gui_playlist_find_mode mode, const char* needle) {
  switch (mode) {
    case GUI_PLAYLIST_FIND_AUTHOR:
      return ascii_ci_contains(e->author, needle);
    case GUI_PLAYLIST_FIND_TITLE:
      return ascii_ci_contains(e->title, needle);
    case GUI_PLAYLIST_FIND_FILENAME:
      return ascii_ci_contains(basename_of(e->path), needle);
    case GUI_PLAYLIST_FIND_ANYWHERE:
    default:
      return ascii_ci_contains(e->author, needle) ||
             ascii_ci_contains(e->title, needle) ||
             ascii_ci_contains(basename_of(e->path), needle);
  }
}

int gui_playlist_find(const gui_playlist* pl, int from,
                       gui_playlist_find_mode mode, const char* needle) {
  if (!needle[0]) return -1; /* FindItem2: Pos('', s) is never > 0 */
  if (from < 0) from = 0;
  for (int i = from; i < pl->count; i++) {
    if (entry_matches(&pl->items[i], mode, needle)) return i;
  }
  return -1;
}

/* PlayList.pas: CompareFileNames/CompareTitles/CompareAuthors/
 * CompareTypes (~3565-3603) - ASCII-range case-insensitive (see
 * ascii_ci_contains's own comment on the same narrowing of the
 * original's UTF8CompareText), each falling back to the next-more-
 * specific comparator on a tie exactly as the original chains do.
 * Title/Author use the effective (override-if-set) value, matching
 * PlayList.pas's own Title/Author fields being directly overridable by
 * ItemEdit.pas in the first place (this port's read-only-extracted-vs-
 * override split is its own addition - see gui_playlist_overrides's
 * comment - sorting by the value actually shown/used elsewhere keeps
 * that split consistent). */
static int cmp_filename(const gui_playlist_entry* a,
                         const gui_playlist_entry* b) {
  return strcasecmp(basename_of(a->path), basename_of(b->path));
}

static int cmp_title(const gui_playlist_entry* a,
                      const gui_playlist_entry* b) {
  const char* ta = a->overrides.title[0] ? a->overrides.title : a->title;
  const char* tb = b->overrides.title[0] ? b->overrides.title : b->title;
  int r = strcasecmp(ta, tb);
  return r != 0 ? r : cmp_filename(a, b);
}

static int cmp_author(const gui_playlist_entry* a,
                       const gui_playlist_entry* b) {
  const char* aa = a->overrides.author[0] ? a->overrides.author : a->author;
  const char* ab = b->overrides.author[0] ? b->overrides.author : b->author;
  int r = strcasecmp(aa, ab);
  return r != 0 ? r : cmp_title(a, b);
}

static int cmp_filetype(const gui_playlist_entry* a,
                         const gui_playlist_entry* b) {
  int r = strcasecmp(player_format_name(a->format),
                      player_format_name(b->format));
  return r != 0 ? r : cmp_author(a, b);
}

typedef int (*sort_cmp_fn)(const gui_playlist_entry*,
                            const gui_playlist_entry*);

/* Set only for the duration of one qsort() call below - GTK is
 * single-threaded here (main thread only touches the playlist model),
 * so this plain global is sufficient without needing glibc's qsort_r
 * extension just to pass context through. */
static const gui_playlist_entry* g_sort_base;
static sort_cmp_fn g_sort_cmp;

static int index_cmp(const void* pa, const void* pb) {
  int ia = *(const int*)pa, ib = *(const int*)pb;
  return g_sort_cmp(&g_sort_base[ia], &g_sort_base[ib]);
}

void gui_playlist_sort(gui_playlist* pl, gui_playlist_sort_mode mode) {
  if (pl->count < 2) return; /* RandomSortClick/MyQuickSort's own guards */

  uint64_t current_id =
      (pl->current >= 0) ? pl->items[pl->current].id : (uint64_t)-1;

  if (mode == GUI_PLAYLIST_SORT_RANDOM) {
    /* RandomSortClick (~3431-3468): swap-based Fisher-Yates over the
     * whole array - re-implemented as the standard algorithm rather
     * than the original's odd "pick two indices with Tag=0, swap them,
     * repeat count/2 times" scheme, but with the same net effect (a
     * uniformly random permutation), identity tracked via id like
     * every other mode here. */
    for (int i = pl->count - 1; i > 0; i--) {
      int j = rand() % (i + 1);
      gui_playlist_entry tmp = pl->items[i];
      pl->items[i] = pl->items[j];
      pl->items[j] = tmp;
    }
  } else {
    sort_cmp_fn cmp;
    switch (mode) {
      case GUI_PLAYLIST_SORT_AUTHOR: cmp = cmp_author; break;
      case GUI_PLAYLIST_SORT_TITLE: cmp = cmp_title; break;
      case GUI_PLAYLIST_SORT_FILENAME: cmp = cmp_filename; break;
      case GUI_PLAYLIST_SORT_FILETYPE: cmp = cmp_filetype; break;
      default: return;
    }

    /* Sorts an index permutation rather than the (multi-KB, due to
     * gui_playlist_overrides's several fixed text buffers)
     * gui_playlist_entry array directly - qsort() itself may move
     * elements through an O(n log n) number of swaps, and each swap of
     * full entries here would be far more memmove traffic than sorting
     * plain ints and reordering once at the end. */
    int* order = (int*)malloc((size_t)pl->count * sizeof(int));
    gui_playlist_entry* sorted =
        (gui_playlist_entry*)malloc((size_t)pl->count * sizeof(gui_playlist_entry));
    if (!order || !sorted) {
      free(order);
      free(sorted);
      return; /* OOM - leave the playlist unsorted rather than crash */
    }
    for (int i = 0; i < pl->count; i++) order[i] = i;

    g_sort_base = pl->items;
    g_sort_cmp = cmp;
    qsort(order, (size_t)pl->count, sizeof(int), index_cmp);
    g_sort_base = NULL;
    g_sort_cmp = NULL;

    for (int i = 0; i < pl->count; i++) sorted[i] = pl->items[order[i]];
    memcpy(pl->items, sorted, (size_t)pl->count * sizeof(gui_playlist_entry));
    free(order);
    free(sorted);
  }

  pl->current = -1;
  if (current_id != (uint64_t)-1) {
    for (int i = 0; i < pl->count; i++) {
      if (pl->items[i].id == current_id) {
        pl->current = i;
        break;
      }
    }
  }
}

static void dir_of(const char* path, char* out, size_t cap) {
  const char* slash = strrchr(path, '/');
  if (!slash) {
    out[0] = '\0';
    return;
  }
  size_t len = (size_t)(slash - path);
  if (len >= cap) len = cap - 1;
  memcpy(out, path, len);
  out[len] = '\0';
}

static void resolve_relative(const char* base_dir, const char* rel, char* out,
                              size_t cap) {
  if (rel[0] == '/' || !base_dir[0]) {
    copy_bounded(out, cap, rel);
  } else {
    snprintf(out, cap, "%s/%s", base_dir, rel);
  }
}

/* PlayList.pas:315-320, CheckPath (the real Lazarus/FPC Linux build's
 * OWN backslash-to-forward-slash path-delimiter fixup, `{$IFNDEF
 * Windows}`-guarded - real .ayl playlists are commonly authored/shared
 * from the Windows build and carry Windows-style `\` separators, e.g.
 * `..\..\Authors\Foo\bar.stc`). Tries the path AS GIVEN first
 * (`if FileExists(path) then Exit`) - only rewrites backslashes if that
 * fails, so a path that happens to already work (already uses `/`, or a
 * filename that coincidentally contains a literal `\`) is left alone.
 * gui_playlist_load_ayl calls this instead of resolve_relative directly
 * at every path-line site (MIG-0112, found via a real .ayl file -
 * test_corpus_76/Cmnd.ayl - that only loads correctly with this). */
static void ayl_check_path(const char* base_dir, const char* raw, char* out,
                            size_t cap) {
  resolve_relative(base_dir, raw, out, cap);
  FILE* probe = fopen(out, "rb");
  if (probe) {
    fclose(probe);
    return;
  }
  char fixed[1024];
  size_t oi = 0;
  for (size_t i = 0; raw[i] && oi + 1 < sizeof(fixed); i++) {
    fixed[oi++] = (raw[i] == '\\') ? '/' : raw[i];
  }
  fixed[oi] = '\0';
  resolve_relative(base_dir, fixed, out, cap);
}

bool gui_playlist_save_m3u(const gui_playlist* pl, const char* path) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  for (int i = 0; i < pl->count; i++) fprintf(f, "%s\n", pl->items[i].path);
  fclose(f);
  return true;
}

int gui_playlist_load_m3u(gui_playlist* pl, const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return -1;
  char base_dir[1024];
  dir_of(path, base_dir, sizeof(base_dir));

  int added = 0;
  char line[1024];
  bool first = true;
  while (fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (first) {
      first = false;
      if (strcmp(line, "#EXTM3U") == 0) continue;
    }
    if (line[0] == '\0' || line[0] == '#') continue;
    char resolved[1024];
    resolve_relative(base_dir, line, resolved, sizeof(resolved));
    added += gui_playlist_add_file(pl, resolved);
  }
  fclose(f);
  return added;
}

#define AYL_VERSION_STRING "ZX Spectrum Sound Chip Emulator Play List File v1."

static const char* const AYL_CHAN_ALLOC[7] = {
    "Mono", "ABC", "ACB", "BAC", "BCA", "CAB", "CBA",
};

/* PlayList.pas: SaveAYL's ConvCR - '\' -> "\\", CR(+LF) -> "\n" (see
 * playlist.h's own comment on why a lone LF, this port's own comment-
 * text newline convention, is treated the same as a CRLF pair here). */
static void ayl_encode_comment(const char* s, char* out, size_t cap) {
  size_t oi = 0;
  for (size_t i = 0; s[i] && oi + 2 < cap; i++) {
    char c = s[i];
    if (c == '\\') {
      out[oi++] = '\\';
      out[oi++] = '\\';
    } else if (c == '\r' || c == '\n') {
      out[oi++] = '\\';
      out[oi++] = 'n';
      if (c == '\r' && s[i + 1] == '\n') i++;
    } else {
      out[oi++] = c;
    }
  }
  out[oi] = '\0';
}

static void ayl_decode_comment(const char* s, char* out, size_t cap) {
  size_t oi = 0;
  for (size_t i = 0; s[i] && oi + 1 < cap; i++) {
    if (s[i] == '\\' && s[i + 1]) {
      i++;
      out[oi++] = (s[i] == 'n') ? '\n' : s[i];
    } else {
      out[oi++] = s[i];
    }
  }
  out[oi] = '\0';
}

static bool ayl_readline(FILE* f, char* out, size_t cap) {
  if (!fgets(out, (int)cap, f)) return false;
  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
    out[--len] = '\0';
  return true;
}

/* Writes one `<...>` override block (Players.pas: SavePLItem's own
 * token-writing half) for `ov`, using `title`/`author` as the already-
 * resolved display fallbacks. `def` (may be NULL) is diffed against
 * ChipType/ChannelsAllocation/ChipFrequency/PlayerFrequency exactly
 * like SavePLItem:1635-1653's own `<> PLDef_X` guards - a field that
 * matches `def` is OMITTED (see playlist.h's own comment on the
 * ChannelsAllocation=-2 "same mode, different pan values" quirk this
 * intentionally preserves). `format_spec` (0 = don't write) is the
 * MIG-0118 subsong-index token, SavePLItem:1685-1692's own condition
 * for FT.AY (the only format this port's song_count ever exceeds 1
 * for). Returns true iff anything was written (i.e. whether the
 * `<`/`>` wrapper was needed at all) - MIG-0112 factored this out of
 * gui_playlist_save_ayl so the ts-pair block doesn't duplicate this
 * ~40-line token list a second time. */
static bool write_ayl_block(FILE* f, const gui_playlist_overrides* ov,
                             const char* title, const char* author,
                             const gui_playlist_defaults* def,
                             int format_spec) {
  bool open = false;
  if (author[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Author=%s\n", author);
  }
  if (title[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Name=%s\n", title);
  }
  if (ov->program[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Program=%s\n", ov->program);
  }
  if (ov->tracker[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Tracker=%s\n", ov->tracker);
  }
  if (ov->computer[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Computer=%s\n", ov->computer);
  }
  if (ov->date[0]) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Date=%s\n", ov->date);
  }
  if (ov->comment[0]) {
    char enc[1024];
    ayl_encode_comment(ov->comment, enc, sizeof(enc));
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Comment=%s\n", enc);
  }
  if (ov->has_chip_type &&
      !(def && def->has_chip_type && def->chip_type == ov->chip_type)) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "ChipType=%s\n",
            ov->chip_type == AY_CHIP_TYPE_AY ? "AY" : "YM");
  }
  if (ov->channel_mode != -1 &&
      !(def && def->channel_mode == ov->channel_mode)) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    if (ov->channel_mode >= 0 && ov->channel_mode <= 6) {
      fprintf(f, "ChannelsAllocation=%s\n",
              AYL_CHAN_ALLOC[ov->channel_mode]);
    } else {
      fprintf(f, "ChannelsAllocation=%d,%d,%d,%d,%d,%d\n", ov->al, ov->ar,
              ov->bl, ov->br, ov->cl, ov->cr);
    }
  }
  if (ov->ay_freq != -1 && !(def && def->ay_freq == ov->ay_freq)) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "ChipFrequency=%d\n", ov->ay_freq);
  }
  if (ov->int_freq != -1 && !(def && def->int_freq == ov->int_freq)) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "PlayerFrequency=%d\n", ov->int_freq);
  }
  if (format_spec > 0) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "FormatSpec=%d\n", format_spec);
  }
  if (open) fprintf(f, ">\n");
  return open;
}

/* PlayList.pas: SaveAYL:1711-1728's own leading PLDef block - the same
 * 5-token subset write_ayl_block diffs per-item fields against, just
 * unconditional here (PLDef has no "differs from itself" concept).
 * No-op if `defaults` is NULL. */
static void write_ayl_pldef_header(FILE* f, const gui_playlist_defaults* d) {
  bool open = false;
  if (!d) return;
  if (d->number_of_channels > 0) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "Channels=%s\n", d->number_of_channels == 1 ? "Mono" : "Stereo");
  }
  if (d->channel_mode != -1) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    if (d->channel_mode >= 0 && d->channel_mode <= 6) {
      fprintf(f, "ChannelsAllocation=%s\n", AYL_CHAN_ALLOC[d->channel_mode]);
    } else {
      fprintf(f, "ChannelsAllocation=%d,%d,%d,%d,%d,%d\n", d->al, d->ar,
              d->bl, d->br, d->cl, d->cr);
    }
  }
  if (d->ay_freq >= 0) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "ChipFrequency=%d\n", d->ay_freq);
  }
  if (d->int_freq >= 0) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "PlayerFrequency=%d\n", d->int_freq);
  }
  if (d->has_chip_type) {
    if (!open) { fprintf(f, "<\n"); open = true; }
    fprintf(f, "ChipType=%s\n", d->chip_type == AY_CHIP_TYPE_AY ? "AY" : "YM");
  }
  if (open) fprintf(f, ">\n");
}

bool gui_playlist_save_ayl(const gui_playlist* pl, const char* path,
                            const gui_playlist_defaults* defaults) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  fprintf(f, "%s6\n", AYL_VERSION_STRING);
  write_ayl_pldef_header(f, defaults);

  for (int i = 0; i < pl->count; i++) {
    const gui_playlist_entry* e = &pl->items[i];
    const gui_playlist_overrides* ov = &e->overrides;
    fprintf(f, "%s\n", e->path);

    const char* title = ov->title[0] ? ov->title : e->title;
    const char* author = ov->author[0] ? ov->author : e->author;
    write_ayl_block(f, ov, title, author, defaults, e->song_index);

    /* MIG-0112: PlayList.pas's SaveAYL - `if ...Next<>nil then
     * SavePLItem(...Next)` - a second `<...>` block for the ts-pair,
     * with NO second path line (see gui_playlist_entry's own comment on
     * why: the pair plays the SAME file, just its own override set).
     * SavePLItem's own token-writing has no read-only-metadata fallback
     * (that convention is this port's own gui_playlist_entry_refresh_
     * display concern, not SavePLItem's), so the ts-pair block passes
     * its title/author fields raw, not falling back to e->title/
     * e->author the way the primary block above does. No FormatSpec
     * here - see playlist.h's own comment on why. */
    if (e->has_ts_pair) {
      write_ayl_block(f, &e->ts_pair_overrides, e->ts_pair_overrides.title,
                       e->ts_pair_overrides.author, defaults, 0);
    }
  }

  fclose(f);
  return true;
}

static bool ayl_parse_token(const char* line, char* key, size_t key_cap,
                             char* val, size_t val_cap) {
  const char* eq = strchr(line, '=');
  if (!eq) return false;
  size_t klen = (size_t)(eq - line);
  if (klen >= key_cap) klen = key_cap - 1;
  memcpy(key, line, klen);
  key[klen] = '\0';
  copy_bounded(val, val_cap, eq + 1);
  return true;
}

static int ayl_parse_chan_mode(const char* val, uint8_t* al, uint8_t* ar,
                                uint8_t* bl, uint8_t* br, uint8_t* cl,
                                uint8_t* cr) {
  for (int m = 0; m <= 6; m++) {
    if (strcmp(val, AYL_CHAN_ALLOC[m]) == 0) return m;
  }
  int a[6];
  if (sscanf(val, "%d,%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3], &a[4],
             &a[5]) == 6) {
    *al = (uint8_t)a[0];
    *ar = (uint8_t)a[1];
    *bl = (uint8_t)a[2];
    *br = (uint8_t)a[3];
    *cl = (uint8_t)a[4];
    *cr = (uint8_t)a[5];
    return -2;
  }
  return -1; /* unparseable - leave channel_mode unset */
}

/* A real, recognized token this port doesn't model (item-level
 * Channels/Offset/Length/Address/Loop/Time/Original/Type/ams_andsix -
 * FormatSpec IS modeled, but handled by the caller before this
 * function is reached, see gui_playlist_load_ayl) or anything wholly
 * unrecognized falls through every branch here and is silently skipped
 * rather than erroring, so a real .ayl file with these tokens still
 * loads (see playlist.h's own comment on this subset). */
static void ayl_apply_token(gui_playlist_overrides* ov, const char* key,
                             const char* val) {
  if (strcmp(key, "Author") == 0) {
    copy_bounded(ov->author, sizeof(ov->author), val);
  } else if (strcmp(key, "Name") == 0) {
    copy_bounded(ov->title, sizeof(ov->title), val);
  } else if (strcmp(key, "Program") == 0) {
    copy_bounded(ov->program, sizeof(ov->program), val);
  } else if (strcmp(key, "Tracker") == 0) {
    copy_bounded(ov->tracker, sizeof(ov->tracker), val);
  } else if (strcmp(key, "Computer") == 0) {
    copy_bounded(ov->computer, sizeof(ov->computer), val);
  } else if (strcmp(key, "Date") == 0) {
    copy_bounded(ov->date, sizeof(ov->date), val);
  } else if (strcmp(key, "Comment") == 0) {
    ayl_decode_comment(val, ov->comment, sizeof(ov->comment));
  } else if (strcmp(key, "ChipType") == 0) {
    if (strcmp(val, "AY") == 0) {
      ov->has_chip_type = true;
      ov->chip_type = AY_CHIP_TYPE_AY;
    } else if (strcmp(val, "YM") == 0) {
      ov->has_chip_type = true;
      ov->chip_type = AY_CHIP_TYPE_YM;
    }
  } else if (strcmp(key, "ChannelsAllocation") == 0) {
    int m = ayl_parse_chan_mode(val, &ov->al, &ov->ar, &ov->bl, &ov->br,
                                 &ov->cl, &ov->cr);
    if (m != -1) ov->channel_mode = m;
  } else if (strcmp(key, "ChipFrequency") == 0) {
    ov->ay_freq = atoi(val);
  } else if (strcmp(key, "PlayerFrequency") == 0) {
    ov->int_freq = atoi(val);
  }
}

/* PlayList.pas: LoadAYL:1429-1463's own leading-PLDef-block token loop -
 * the same 5-token subset write_ayl_pldef_header writes (ChipType/
 * Channels/ChannelsAllocation/ChipFrequency/PlayerFrequency); any other
 * token here is a real TokenError in the original (LoadAYL aborts the
 * whole load), but this port takes the same lenient "skip and keep
 * going" approach every per-item block already does rather than
 * rejecting an otherwise-fine file over one stray PLDef token. */
static void ayl_apply_pldef_token(gui_playlist_defaults* d, const char* key,
                                   const char* val) {
  if (strcmp(key, "ChipType") == 0) {
    if (strcmp(val, "AY") == 0) {
      d->has_chip_type = true;
      d->chip_type = AY_CHIP_TYPE_AY;
    } else if (strcmp(val, "YM") == 0) {
      d->has_chip_type = true;
      d->chip_type = AY_CHIP_TYPE_YM;
    }
  } else if (strcmp(key, "Channels") == 0) {
    if (strcmp(val, "Mono") == 0) d->number_of_channels = 1;
    else if (strcmp(val, "Stereo") == 0) d->number_of_channels = 2;
  } else if (strcmp(key, "ChannelsAllocation") == 0) {
    int m = ayl_parse_chan_mode(val, &d->al, &d->ar, &d->bl, &d->br, &d->cl,
                                 &d->cr);
    if (m != -1) d->channel_mode = m;
  } else if (strcmp(key, "ChipFrequency") == 0) {
    d->ay_freq = atoi(val);
  } else if (strcmp(key, "PlayerFrequency") == 0) {
    d->int_freq = atoi(val);
  }
}

int gui_playlist_load_ayl(gui_playlist* pl, const char* path,
                           gui_playlist_defaults* out_defaults) {
  FILE* f = fopen(path, "r");
  if (!f) return -1;
  char base_dir[1024];
  dir_of(path, base_dir, sizeof(base_dir));

  char line[1024];
  if (!ayl_readline(f, line, sizeof(line)) ||
      strncmp(line, AYL_VERSION_STRING, strlen(AYL_VERSION_STRING)) != 0) {
    fclose(f);
    return -1; /* not a recognized .ayl file */
  }

  int added_total = 0;
  char cur_buf[1024];
  if (!ayl_readline(f, cur_buf, sizeof(cur_buf))) {
    fclose(f);
    return added_total;
  }

  /* A leading PLDef global-defaults block, if present, is parsed into
   * *out_defaults (MIG-0118; left untouched if out_defaults is NULL -
   * "caller doesn't care", the block is still skipped past correctly). */
  char tok[1024];
  if (strcmp(cur_buf, "<") == 0) {
    gui_playlist_defaults pldef;
    gui_playlist_defaults_init(&pldef);
    while (ayl_readline(f, tok, sizeof(tok)) && strcmp(tok, ">") != 0) {
      char key[32], val[900];
      if (tok[0] && ayl_parse_token(tok, key, sizeof(key), val, sizeof(val))) {
        ayl_apply_pldef_token(&pldef, key, val);
      }
    }
    if (out_defaults) *out_defaults = pldef;
    if (!ayl_readline(f, cur_buf, sizeof(cur_buf))) {
      fclose(f);
      return added_total;
    }
  }

  bool have_cur = true;
  while (have_cur) {
    char next_buf[1024];
    if (!ayl_readline(f, next_buf, sizeof(next_buf))) {
      /* Last line in the file is a bare path with no block. */
      char resolved[1024];
      ayl_check_path(base_dir, cur_buf, resolved, sizeof(resolved));
      added_total += gui_playlist_add_file(pl, resolved);
      break;
    }

    if (strcmp(next_buf, "<") != 0) {
      /* `cur_buf` is a bare path (no block); `next_buf` becomes the
       * new current path candidate for the next iteration. */
      char resolved[1024];
      ayl_check_path(base_dir, cur_buf, resolved, sizeof(resolved));
      added_total += gui_playlist_add_file(pl, resolved);
      memcpy(cur_buf, next_buf, sizeof(cur_buf));
      continue;
    }

    /* `cur_buf` has a block - parse tokens until the closing '>'. A
     * FormatSpec token (MIG-0118) is this port's own multi-song .ay
     * subsong index, not a gui_playlist_overrides field - intercepted
     * here before ayl_apply_token sees it. */
    gui_playlist_overrides ov;
    memset(&ov, 0, sizeof(ov));
    ov.channel_mode = -1;
    ov.ay_freq = -1;
    ov.int_freq = -1;
    int format_spec = -1;
    while (ayl_readline(f, tok, sizeof(tok)) && strcmp(tok, ">") != 0) {
      char key[32], val[900];
      if (tok[0] && ayl_parse_token(tok, key, sizeof(key), val, sizeof(val))) {
        if (strcmp(key, "FormatSpec") == 0) {
          format_spec = atoi(val);
        } else {
          ayl_apply_token(&ov, key, val);
        }
      }
    }

    char resolved[1024];
    ayl_check_path(base_dir, cur_buf, resolved, sizeof(resolved));
    int before = pl->count;
    /* PlayList.pas: a path line WITH a `<...>` block always goes through
     * LoadPLItem+AddPlaylistItem - a single-entry add using FormatSpec
     * (defaulting to 0 if absent) directly as the subsong index, NEVER
     * the full-file subsong expansion Add_Songs_From_File/gui_playlist_
     * add_file does (that's reserved for a BARE path line with no block
     * at all - CheckAndAddFromPLFile's own path, matched above). Using
     * gui_playlist_add_file here unconditionally was the MIG-0118 bug:
     * a multi-song .ay's own song-0 line (no FormatSpec, since 0 is
     * never written) would silently re-expand ALL subsongs on top of
     * the ones each subsequent FormatSpec=N line adds individually. */
    int added = add_single_song_entry(pl, resolved,
                                       format_spec > 0 ? format_spec : 0)
                    ? 1
                    : 0;
    added_total += added;
    if (added > 0) {
      pl->items[before].overrides = ov;
      gui_playlist_entry_refresh_display(&pl->items[before]);
    }

    /* Read the path line for the NEXT item. A second immediate '<' here
     * is a "ts" Next-linked subitem (MIG-0112) - PlayList.pas's
     * LoadPLItem reuses the SAME path (String1 := PLItemWork.FileName)
     * rather than reading a new one, so this block's own tokens are
     * parsed into gui_playlist_entry::ts_pair_overrides on the entry
     * just added, not attached to any new playlist row - see
     * gui_playlist_entry's own comment for the full trace. */
    if (!ayl_readline(f, cur_buf, sizeof(cur_buf))) {
      have_cur = false;
      break;
    }
    if (strcmp(cur_buf, "<") == 0) {
      gui_playlist_overrides ts_ov;
      memset(&ts_ov, 0, sizeof(ts_ov));
      ts_ov.channel_mode = -1;
      ts_ov.ay_freq = -1;
      ts_ov.int_freq = -1;
      while (ayl_readline(f, tok, sizeof(tok)) && strcmp(tok, ">") != 0) {
        char key[32], val[900];
        if (tok[0] && ayl_parse_token(tok, key, sizeof(key), val, sizeof(val))) {
          ayl_apply_token(&ts_ov, key, val);
        }
      }
      if (added > 0) {
        pl->items[before].has_ts_pair = true;
        pl->items[before].ts_pair_overrides = ts_ov;
        gui_playlist_entry_refresh_display(&pl->items[before]);
      }
      if (!ayl_readline(f, cur_buf, sizeof(cur_buf))) {
        have_cur = false;
        break;
      }
    }
  }

  fclose(f);
  return added_total;
}
