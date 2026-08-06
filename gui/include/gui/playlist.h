/* C11 port of PlayList.pas's core data model (TPlayListItem/PlayListItems
 * + Add_File's directory-scan path) - scoped narrower than the original
 * on purpose, matching this port's existing scope boundaries (see
 * README.md's Scope section: playback + WAV export only, no BASS/CD/
 * MP3/OGG/FLAC/streaming). What's carried over:
 *
 *  - One entry per playable file, expanded to one entry PER SUBSONG for
 *    multi-song .ay files (mirrors Players.pas's OpenAYFile, which
 *    iterates 0..NumOfSongs when building playlist entries - see
 *    engine/include/ay_engine/player.h's player_song_count comment).
 *  - Add a single file, add a directory (recursive scan).
 *  - Next/previous/clear.
 *
 * NOT carried over (see migration_debt.yaml for the full list):
 * .ayl/.cue/.m3u playlist-file load/save, deduplication, and every
 * conversion-menu feature (WAV/VTX/YM6/PSG/ZXAY/MP3/OGG/FLAC/OPUS export
 * from the playlist) - conversion beyond WAV is already out of this
 * port's scope entirely, and WAV conversion itself is a CLI-only
 * feature (tools/ay_player) not exposed from the playlist in this
 * milestone. Per-item metadata, per-item channel/volume overrides,
 * "find item", and sorting (see gui_playlist_sort below, MIG-0089) ARE
 * now carried over - this comment predates those landing.
 */
#ifndef GUI_PLAYLIST_H
#define GUI_PLAYLIST_H

#include <stdbool.h>
#include <stdint.h>

#include "ay_engine/ay.h"
#include "ay_engine/player.h"

/* ItemEdit.pas's real per-item overrides (MIG-0088 - see its own
 * migration_debt.yaml entry for the full end-to-end trace this is
 * based on: PlayList.pas:1009's MenuItemAdjustingClick reads/writes
 * these exact fields on the selected playlist item, and PlayItem
 * (623-825ish) applies them at play time, each gated behind its own
 * Mixer "use per-item list" checkbox - see gui_mainwin's own
 * use_chip_type_list/use_channel_mode_list/use_ay_freq_list/
 * use_int_freq_list fields).
 *
 * Sentinels match the original's own "not set" convention: chip_type
 * uses `has_chip_type` (Pascal has a real No_Chip enum value to use as
 * a sentinel directly; this port's ay_chip_type has no equivalent
 * unused value, hence the separate bool) with channel_mode/ay_freq/
 * int_freq's -1 mirroring Pascal's own -1 sentinels directly. Empty
 * string = unset for every text field. */
typedef struct gui_playlist_overrides {
  bool has_chip_type;
  ay_chip_type chip_type;

  int channel_mode; /* -1 = none; 0-6 = Set_Mode preset (see
                      * mxhelper.c's calc_mode_coefs); -2 = manual
                      * al/ar/bl/br/cl/cr below (Set_Mode_Manual) */
  uint8_t al, ar, bl, br, cl, cr;

  int ay_freq;  /* -1 = none, else a live Hz value for
                 * player_set_chip_freq() */
  int int_freq; /* -1 = none, else Hz*1000 for player_set_player_freq()
                 * (only meaningful for YM/VTX - see that function's
                 * own comment) */

  /* Empty = use the read-only extracted title/author (MIG-0071/0072/
   * 0075/0082/0083) for title/author specifically; Program/Tracker/
   * Computer/Date/Comment have no extracted counterpart at all, empty
   * just means unset. Year (a separate numeric field in ItemEdit.pas)
   * is folded into `date` as free text here - a documented
   * simplification, see gui/dialogs/itemedit.c's own comment. */
  char title[256];
  char author[256];
  char program[256];
  char tracker[256];
  char computer[256];
  char date[256];
  char comment[512];
} gui_playlist_overrides;

typedef struct gui_playlist_entry {
  char path[1024];
  int song_index; /* 0-based; >0 only for multi-song .ay expansion */
  int song_count;
  char author[256]; /* UTF-8, empty if the format/file has none - same
                      * data format_scroll_string() already folds into
                      * `display`, kept separately too so FindPLItem.pas-
                      * style field-scoped search (gui_playlist_find) can
                      * match against just one field. This is the
                      * READ-ONLY EXTRACTED value - overrides.author, if
                      * non-empty, takes precedence for display (see
                      * format_scroll_string's own comment). */
  char title[256];
  char display[300]; /* PlayList.pas: FormatScrollString's convention -
                       * "Author - Title" when both are known (currently
                       * only .ay files carry this - see
                       * player_get_metadata_raw's own comment),
                       * falling back to just Author or just Title, then
                       * to the filename [+ "- song N/M" for multi-song
                       * files without per-song titles] */
  gui_playlist_overrides overrides;
  player_format format; /* set at add-file time from the real player_load_
                          * song probe (MIG-0089) - used by gui_playlist_
                          * sort's "by file type" mode (PlayList.pas:
                          * CompareTypes/GetFileType), not otherwise
                          * displayed. */
  uint64_t id; /* assigned uniquely at add-time (gui_playlist::next_id) -
                * exists solely so gui_playlist_sort can track which
                * entry `current` should point to after a reorder by
                * identity, the same way PlayList.pas's own pointer-
                * based PlaylistItems array does (MyQuickSort/
                * RandomSortClick swap *pointers*, so PlayingItem/
                * Item_Displayed/Scroll_Distination stay meaningful
                * across a sort without any special-casing there - this
                * port's array holds values, not pointers, so an
                * explicit id is the equivalent). */
} gui_playlist_entry;

/* Recomputes `display` from author/title/overrides (FormatScrollString,
 * matching gui_playlist_add_file's own internal use of the same
 * formula) - call after changing an entry's overrides.title/author so
 * the playlist view reflects the edit immediately. */
void gui_playlist_entry_refresh_display(gui_playlist_entry* e);

typedef struct gui_playlist {
  gui_playlist_entry* items;
  int count;
  int capacity;
  int current; /* index of the currently-loaded entry, -1 if none */
  uint64_t next_id; /* next gui_playlist_entry::id to assign */
} gui_playlist;

void gui_playlist_init(gui_playlist* pl);
void gui_playlist_clear(gui_playlist* pl);
void gui_playlist_free(gui_playlist* pl);

/* PlayList.pas: DeletePlayListItem(n) - removes entry `index`, shifting
 * later entries down by one. Adjusts `current`: unaffected if it was
 * before `index`, decremented if it was after, or set to -1 if `index`
 * was the currently-playing entry itself (matching
 * DeletePlayListItemAndReprepare's "playing item removed" case - the
 * caller, gui/src/playlist_win.c, does not itself stop playback, only
 * the model's own bookkeeping is updated here). No-op if `index` is out
 * of range. */
void gui_playlist_remove(gui_playlist* pl, int index);

/* FindPLItem.pas: FindItem2's field-selection modes (RadioGroup1.
 * ItemIndex - "Anywhere"/"Author name"/"Music title"/"File name"). Mode
 * 0 ("Anywhere") here only checks author/title/filename, matching this
 * struct's own narrower field set (the original's mode-0 fallback also
 * checks Programm/Tracker/Computer/Date/Comment, none of which this
 * port extracts for any format - see migration_debt.yaml). */
typedef enum {
  GUI_PLAYLIST_FIND_ANYWHERE = 0,
  GUI_PLAYLIST_FIND_AUTHOR = 1,
  GUI_PLAYLIST_FIND_TITLE = 2,
  GUI_PLAYLIST_FIND_FILENAME = 3,
} gui_playlist_find_mode;

/* FindPLItem.pas: FindItem - case-insensitive (ASCII-range only, see
 * gui/src/playlist.c's own comment - a documented narrowing of the
 * original's full-Unicode UTF8LowerCase) substring search starting at
 * `from` (inclusive), NOT wrapping - the caller decides whether/how to
 * wrap (see gui_playlist_win_find_next). Returns the first matching
 * index >= from, or -1. */
int gui_playlist_find(const gui_playlist* pl, int from,
                       gui_playlist_find_mode mode, const char* needle);

/* PlayList.pas: RandomSort/ByauthorSort/BytitleSort/ByfilenameSort/
 * Byfiletype1 (~3431, 3604-3620) - MyQuickSort's five comparators
 * (CompareAuthors/CompareTitles/CompareFileNames/CompareTypes) plus the
 * separate Fisher-Yates-style RandomSortClick. Author/Title/FileType
 * each fall back to the next-more-specific comparator on a tie, exactly
 * matching the original's own CompareAuthors->CompareTitles->
 * CompareFileNames and CompareTypes->CompareAuthors chains. */
typedef enum {
  GUI_PLAYLIST_SORT_RANDOM = 0,
  GUI_PLAYLIST_SORT_AUTHOR = 1,
  GUI_PLAYLIST_SORT_TITLE = 2,
  GUI_PLAYLIST_SORT_FILENAME = 3,
  GUI_PLAYLIST_SORT_FILETYPE = 4,
} gui_playlist_sort_mode;

/* Sorts `pl->items` in place. Like MyQuickSort/RandomSortClick, tracks
 * `pl->current` through the reorder by identity (the entry that was
 * playing before the sort is still the one gui_playlist_win considers
 * "current" after it - PlayList.pas does the same for PlayingItem/
 * Item_Displayed/Scroll_Distination), not by index. A no-op for 0 or 1
 * entries (RandomSortClick's own `Length(PlaylistItems) < 2` guard;
 * MyQuickSort's `temp > 0` guard covers the deterministic modes
 * equivalently). */
void gui_playlist_sort(gui_playlist* pl, gui_playlist_sort_mode mode);

/* PlayList.pas: Deduplicate1Click (~3689-3720) - removes every entry
 * whose (FileName, FormatSpec, Offset) triple matches an earlier one,
 * keeping the first occurrence of each. FormatSpec is this port's
 * song_index (see player_get_metadata_raw's own comment on the
 * FormatSpec=song-index convention for multi-song files) and Offset is
 * always 0 for every entry this port's gui_playlist_add_file can add
 * (embedded-stream/CUE-track entries with nonzero Offset don't exist
 * here - see migration_debt.yaml's CUE entry) - so the equivalent,
 * exact-in-practice key here is (path, song_index). Returns the number
 * of entries removed. */
int gui_playlist_dedup(gui_playlist* pl);

int gui_playlist_add_file(gui_playlist* pl, const char* path);

/* Scans `dir_path` for files with a recognized extension (see
 * playlist.c's EXTENSIONS table - the same 18 formats player.h
 * dispatches, no separate/duplicated detection logic) and adds each via
 * gui_playlist_add_file. `recurse` matches seldir.pas's CBRecurse
 * checkbox (ChooseDirectory's cdoRecurse option) - false scans only
 * `dir_path` itself, true also descends into subdirectories. `on_
 * progress`, if non-NULL, is called after each file/subdirectory is
 * examined with (files_examined, userdata); return false from it to
 * abort the scan early (used to wire ProgBox's Abort button - see
 * gui/dialogs/progbox.c). Returns the number of entries added. */
typedef bool (*gui_playlist_progress_cb)(int files_examined, void* userdata);
int gui_playlist_add_directory(gui_playlist* pl, const char* dir_path,
                                bool recurse,
                                gui_playlist_progress_cb on_progress,
                                void* userdata);

/* PlayList.pas: SBSaveClick's M3U branch (~2926-2938) - one file path
 * per line, no `#EXTM3U`/`#EXTINF` header at all (confirmed from the
 * real writer, which is exactly `for each item: Writeln(FileName)` -
 * no metadata is written by the original either). Note this means a
 * multi-song `.ay` file's path is written once per subsong (each
 * PlayListItem, subsong or not, gets its own line, matching the
 * original's own unconditional per-item loop) - reloading such an
 * .m3u then re-expands EACH of those lines back into the file's full
 * subsong set, multiplying entries (N subsong-lines x N subsongs) -
 * an inherited quirk of the original format/writer, not new to this
 * port, and not fixed here since the goal is faithful reproduction of
 * SBSaveClick's actual (simplistic) behavior, not an improved one. Use
 * gui_playlist_save_ayl instead for multi-song playlists (it collapses
 * to one line per unique path - see that function's own comment).
 * Returns false on a file-open failure. */
bool gui_playlist_save_m3u(const gui_playlist* pl, const char* path);

/* PlayList.pas: Add_File's M3U branch (~1792-1840) - reads one path per
 * line, skipping a leading `#EXTM3U` marker and any `#EXTINF:` lines
 * (their Title/Time hint is not used - this port's own real per-file
 * metadata extraction, MIG-0072/0075/0082/0083, is authoritative and
 * richer than an M3U-embedded hint anyway). Relative paths are resolved
 * against `path`'s own directory (SetCurrentDir(ExtractFileDir(FN)) in
 * the original). Not ported: `.m3u8`'s UTF-8-BOM/forced-UTF8 handling
 * and the original's UniDetectCharCode auto-detect - this port always
 * reads the file as UTF-8 (a documented narrowing, same category as
 * gui_playlist_find's own ASCII-range narrowing elsewhere in this
 * file). Returns the number of entries added. */
int gui_playlist_load_m3u(gui_playlist* pl, const char* path);

/* PlayList.pas: SaveAYL/LoadAYL (~1163-1728) - the native playlist
 * format. This port implements a SUBSET, real-syntax-compatible in
 * both directions (see migration_debt.yaml, MIG-0091, for the full
 * list of gaps) rather than a byte-for-byte reimplementation of the
 * original's full token/versioning grammar:
 *  - Written/read tokens: Name, Author, Program, Tracker, Computer,
 *    Date, Comment, ChipType, ChannelsAllocation, ChipFrequency,
 *    PlayerFrequency - exactly the fields gui_playlist_overrides/
 *    entry already models. Every other real token (Channels, Offset,
 *    Length, Address, Loop, Time, Original, Type, FormatSpec,
 *    ams_andsix) is recognized-and-skipped on load (so a real .ayl
 *    file loads without erroring) but never written.
 *  - No PLDef global-defaults header block on save; a leading one on
 *    load is skipped structurally (parsed and discarded, not applied -
 *    this port has no per-playlist "new item defaults" concept).
 *  - No "ts" Next-linked subitem chains (a rare feature whose exact
 *    purpose wasn't fully traced given the scope of this entry) - a
 *    second immediate `<...>` block after an item's own block is
 *    parsed-and-discarded, not attached to anything.
 *  - Multi-song `.ay` entries: only song_index 0 is written for each
 *    unique path (subsequent subsongs are skipped on save) - avoids
 *    the alternative failure mode of writing the same path N times and
 *    having it re-expand to N*song_count entries on reload, but does
 *    mean a saved-then-reloaded multi-song playlist collapses to just
 *    the first subsong of each file. Loading a real .ayl's own
 *    subsong-identifying tokens (which this port doesn't have a
 *    faithful equivalent for) is not attempted either - every loaded
 *    entry is song_index 0.
 * Returns the number of entries added (gui_playlist_load_ayl) or false
 * on a file-open failure (gui_playlist_save_ayl). */
bool gui_playlist_save_ayl(const gui_playlist* pl, const char* path);
int gui_playlist_load_ayl(gui_playlist* pl, const char* path);

#endif /* GUI_PLAYLIST_H */
