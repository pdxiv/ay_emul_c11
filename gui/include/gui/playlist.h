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

#include "ay_engine/hw/ay.h"
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

  /* ItemEdit.pas: Number_Of_Channels field (GBNChans/RBNStereo/RBNMono/
   * RBNDef, SetPlayItems/GetPlayItems ~144-147/224-226) - 0 = unset/use
   * the playlist-wide PLDef_Number_Of_Channels fallback (RBNDef), 1 =
   * mono, 2 = stereo. Matches Pascal's own tri-state convention exactly
   * (unlike channel_mode/ay_freq/int_freq's -1 sentinel, Number_Of_
   * Channels genuinely uses 0 as its own "unset" value - see PlayList.
   * pas:902's `PLDef_Number_Of_Channels := 0`). Gated at load time by
   * Mixer's CBChLst checkbox (gui_mixer_win_use_channel_count_list) -
   * see PlayList.pas's PlayItem ~748-759. */
  int number_of_channels;

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

/* PlayList.pas: PLDef_Chip_Type/PLDef_Number_Of_Channels/PLDef_
 * SoundChip_Frq/PLDef_Player_Frq/PLDef_Channel_Mode/PLDef_AL../PLDef_CR
 * (declared ~284-289, defaulted ~902-906) - playlist-WIDE fallback
 * overrides (distinct from any individual gui_playlist_entry::overrides
 * above), consulted by PlayItem (~751-823) whenever an item's own
 * override is unset. ItemEdit.pas's "Load Defaults"/"Save as Defaults"
 * buttons (BDefLoadClick/BDefSaveClick, ~198-213) read/write exactly
 * these fields via the dialog's own SetPlayItems/GetPlayItems. Session-
 * lifetime only in this port's GENERAL playback use (see gui_playlist_
 * win's own `defaults` field comment - PlayItem's own play-time fallback
 * consultation isn't wired into gui/src/playback.c, a separate, pre-
 * existing gap this entry doesn't touch). PLDef_* playlist-FILE
 * persistence IS now implemented (MIG-0118): gui_playlist_save_ayl
 * writes a leading `<...>` block from its own `defaults` argument
 * (SaveAYL:1711-1728's exact per-field write conditions - Channels if
 * >0, ChannelsAllocation if <>-1, ChipFrequency/PlayerFrequency if
 * >=0, ChipType if <>No_Chip) and diffs each per-item ChipType/
 * ChannelsAllocation/ChipFrequency/PlayerFrequency override against it
 * before writing (SavePLItem:1635-1653's own `<> PLDef_X` guards) -
 * gui_playlist_load_ayl parses a leading block the same way and returns
 * it via its own `out_defaults` argument (NULL if the caller doesn't
 * need it) rather than discarding it. Same sentinel conventions as
 * gui_playlist_overrides throughout. */
typedef struct gui_playlist_defaults {
  bool has_chip_type;
  ay_chip_type chip_type;
  int number_of_channels; /* 0 = "use built-in default" (2/stereo), 1 =
                            * mono, 2 = stereo - PLDef_Number_Of_Channels
                            * itself defaults to 0 */
  int channel_mode; /* -1/-2/0-6, PLDef_Channel_Mode - defaults to -1 */
  uint8_t al, ar, bl, br, cl, cr;
  int ay_freq;  /* -1 = unset, PLDef_SoundChip_Frq - defaults to -1 */
  int int_freq; /* -1 = unset, PLDef_Player_Frq - defaults to -1 */
} gui_playlist_defaults;

/* PlayList.pas:902-906's own PLDef_* initial values - the caller
 * (gui_playlist_win_create) must call this since a plain memset leaves
 * channel_mode/ay_freq/int_freq at 0, not their real -1 "unset"
 * sentinel. */
void gui_playlist_defaults_init(gui_playlist_defaults* d);

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

  double duration_seconds; /* PlayList.pas: PlayListItems[n]^.Time (via
                             * GetTime/GetPlayListTime) - MIG-0126.
                             * Extracted eagerly at add-time from the
                             * SAME probe_song load every entry already
                             * pays for (player_get_tick_position's
                             * max * player_get_seconds_per_tick, all
                             * 18 formats), unlike real Pascal's own
                             * lazy on-demand NeedGetTime/PollGetTime
                             * Request queue - a real behavioral
                             * simplification (every duration is known
                             * immediately, there's no "+" partial-total
                             * state gui_playlist_win's own total-
                             * duration label ever needs to show), not
                             * a narrowing: nothing is less accurate,
                             * just computed sooner. 0.0 if the format
                             * has no known duration (player_get_tick_
                             * position returned max<=0). */
  bool load_error; /* PlayList.pas: PlayListItems[n]^.Error<>FileNoError
                     * (MIG-0126) - true when a LOAD (not add/probe,
                     * which already fails closed - see gui_playlist_
                     * add_file's own comment) of this entry failed via
                     * gui/src/mainwin.c's do_load_song. Drives gui/src/
                     * playlist_win.c's PLColorErr/PLColorErrSel
                     * (RedrawItemRealy's own Err branch) - cleared
                     * again the next time this same entry loads
                     * successfully, matching RedrawItemRealy reading
                     * Error fresh every redraw rather than latching
                     * permanently. */

  /* MIG-0112: playlist-level Turbosound pairing (PlayList.pas's "ts" `<`
   * chain, TPlayListItem.Next). Traced end-to-end against LoadAYL/
   * SavePLItem (PlayList.pas:1512-1522/1690-1740): the paired "Next"
   * item is NOT a second file - LoadPLItem's very first statement is
   * `FileName := String1`, and the ts-branch reuses `String1 :=
   * PLItemWork.FileName` (the item JUST loaded, i.e. THIS SAME path)
   * before calling LoadPLItem again, and SaveAYL's own writer confirms
   * this symmetrically (`Writeln(m3uf, FileName); SavePLItem(...); if
   * ...Next<>nil then SavePLItem(...Next)` - ONE path line, then TWO
   * `<...>` override blocks, never a second path). So this is "the same
   * file driven on both AY chips simultaneously, each side with its own
   * independent overrides" (e.g. a different ChipFrequency/pan per
   * side for a chorus/detune effect) - not two different tracker files
   * paired together, despite Players.pas's own TrModLoaded loading
   * `Next` as a nominally-independent PPlayListItem. engine/player.h's
   * player_pair API is more general than this (it accepts any second
   * path/data), but this port's own .ayl support only ever exercises
   * the real, same-file-twice case, matching the original exactly. */
  bool has_ts_pair;
  gui_playlist_overrides ts_pair_overrides;
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
 * gui_playlist_save_ayl instead for multi-song playlists (MIG-0118: it
 * round-trips each subsong via its own FormatSpec token instead of
 * relying on M3U's own re-expansion - see that function's own comment).
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
 * both directions (see migration_debt.yaml, MIG-0091/MIG-0118, for the
 * full list of gaps) rather than a byte-for-byte reimplementation of
 * the original's full token/versioning grammar:
 *  - Written/read tokens: Name, Author, Program, Tracker, Computer,
 *    Date, Comment, ChipType, ChannelsAllocation, ChipFrequency,
 *    PlayerFrequency, FormatSpec (MIG-0118) - exactly the fields
 *    gui_playlist_overrides/entry already models, plus the per-item
 *    subsong index (see the multi-song bullet below). Every other real
 *    token (Channels [item-level; the PLDef-level Channels token below
 *    IS written/read], Offset, Length, Address, Loop, Time, Original,
 *    Type, ams_andsix) is recognized-and-skipped on load (so a real
 *    .ayl file loads without erroring) but never written.
 *  - PLDef global-defaults header block IS now read/written (MIG-0118):
 *    `defaults`/`out_defaults` below round-trip exactly SaveAYL:1711-
 *    1728's own 5-token subset (Channels/ChannelsAllocation/
 *    ChipFrequency/PlayerFrequency/ChipType - PLDef has no Author/
 *    Title/etc. concept in the original either). Per-item ChipType/
 *    ChannelsAllocation/ChipFrequency/PlayerFrequency overrides are
 *    diffed against `defaults` on save and OMITTED when they match
 *    (SavePLItem:1635-1653's own `<> PLDef_X` guards, including the
 *    original's own simple `Channel_Mode <> PLDef_Channel_Mode` integer
 *    compare - two different custom [-2] pan setups that happen to
 *    both use mode -2 are treated as "equal" and both omitted, exactly
 *    matching a real quirk of the original, not fixed here). PLDef is
 *    NOT baked into loaded items (FillDefPlayListItem's own defaults
 *    ARE the "unset" sentinels, confirmed by direct trace - PLDef is a
 *    separate, session-lifetime fallback PlayItem would consult at
 *    PLAY time in the original, matching gui_playlist_defaults's own
 *    role in THIS port already, see that struct's own comment).
 *  - "ts" Next-linked subitem chains are ported (MIG-0112, closing
 *    this gap and MIG-0007's own playlist-pairing follow-up): a second
 *    immediate `<...>` block right after an item's own block is parsed
 *    into that entry's own ts_pair_overrides and has_ts_pair is set -
 *    see gui_playlist_entry's own comment for why this is "the same
 *    file on both AY chips, each side with its own overrides" rather
 *    than two different files (traced from LoadPLItem/SavePLItem
 *    directly, not guessed). No FormatSpec token is written for a
 *    ts-pair's own block (ts_pair_overrides has no independent
 *    song_index concept - the paired voice always plays the SAME
 *    subsong as the primary, matching the original's own same-file-
 *    twice semantics).
 *  - Multi-song `.ay` entries ARE now round-tripped (MIG-0118): every
 *    subsong gets its OWN path line + `<...>` block (SaveAYL:1734-1741's
 *    own unconditional per-item loop - there is no "only write song 0"
 *    special case in the original), with a `FormatSpec=<song_index>`
 *    token for song_index > 0 (SavePLItem:1685-1692's own condition for
 *    Z80-emulated formats - `(FormatSpec > 0) and IsZ80EmuFileType(...)`,
 *    which for this port only ever applies to FT.AY, the only format
 *    whose song_count can exceed 1 here - see player_song_count's own
 *    comment). On load, a `FormatSpec=N` (N>0) token loads exactly that
 *    ONE subsong directly (not the file's full subsong expansion every
 *    OTHER add path uses) so N saved lines round-trip to exactly N
 *    entries, not N*song_count.
 * Returns the number of entries added (gui_playlist_load_ayl) or false
 * on a file-open failure (gui_playlist_save_ayl). `defaults` may be
 * NULL (nothing written/no diffing happens); `out_defaults` may be NULL
 * (a leading PLDef block, if present, is still skipped past correctly,
 * just not returned to the caller). */
bool gui_playlist_save_ayl(const gui_playlist* pl, const char* path,
                            const gui_playlist_defaults* defaults);
int gui_playlist_load_ayl(gui_playlist* pl, const char* path,
                           gui_playlist_defaults* out_defaults);

#endif /* GUI_PLAYLIST_H */
