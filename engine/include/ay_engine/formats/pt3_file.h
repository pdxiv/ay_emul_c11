/* C11 port of ay_emul/Players.pas's Pro Tracker 3 (PT3) module format
 * support (FT.PT3) - a genuine event/tracker engine (pattern/sample/
 * ornament interpretation with note-pitch tables and effect commands),
 * not a register-dump format like AY/YM/PSG. Built on the existing
 * engine/ay.h mixer (PT3_Get_Registers only ever calls
 * ay_chip_set_ampl_a/b/c / ay_chip_set_mixer_register /
 * ay_chip_set_envelope_register / raw register writes - no Z80
 * execution, matches the shape of every other non-Z80 format ported so
 * far).
 *
 * Scope: standalone .pt3 files loaded directly (not embedded inside
 * another container format) - confirmed via LoadTrackerModule
 * (Players.pas:2265-2312) that the pointer-rebase-by-MAddr logic only
 * runs for embedded/non-.pt3-extension modules, which is dead code for
 * both real test files (songs/pt3/ARTe_ST1.pt3,
 * songs/turbo_sound/Gasman_-_dynamite.pt3 - both have a real .pt3
 * extension and are loaded standalone) - so this port skips that rebase
 * entirely and reads the file's own absolute-from-buffer-start pointers
 * as-is.
 *
 * Ports:
 *  - The file-into-RAM load (Players.pas:2248-2264 - just a raw byte copy
 *    into a flat 65536-byte buffer, zero-padded).
 *  - PT3_Get_Registers in full (Players.pas:12302-12767): GetNoteFreq
 *    (pitch-table select - only PT3NoteTable_ST is ported, see below),
 *    PatternInterpreter (the per-channel opcode-stream walker: note-on,
 *    sample/ornament select, envelope, volume, and the 9 numbered effect
 *    commands), ChangeRegisters (per-tick tone/amplitude/noise/envelope
 *    derivation), and the main per-frame body (delay gate,
 *    position/pattern advance, final AY register writes).
 *  - CheckLoopAndStop (Players.pas:8732-8746) - simplified: this
 *    milestone doesn't port GetTimePT3 (the pattern-walking simulation
 *    that computes a song's real duration/loop point up front, needed
 *    only for UI seek/scrub and duration display - Players.pas:15315-
 *    15645), so there's no real Global_Tick_Max to check against; see
 *    pt3_file_make_buffer's contract below.
 *  - The InitTrackerModule FT.PT3 branch (Players.pas:3717-3806) and the
 *    CaseTrModules FT.PT3 branch's Version derivation (Players.pas:
 *    2577-2582).
 *  - GetNoteFreq's full 4-way PT3_TonTableId dispatch (Players.pas:
 *    12304-12322), i.e. all 6 PT3NoteTable_* variants (PT_33_34r/PT_34_35/
 *    ST/ASM_34r/ASM_34_35/REAL_34r/REAL_34_35, each version-gated except
 *    ST) - MIG-0040 closes the "only TonTableId=1 supported" gap MIG-0020
 *    originally left open.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - Turbosound/TSMode self-pairing (dual-chip PT3, marker-byte-triggered)
 *    IS now ported (MIG-0109, closing the PT3 half of MIG-0007) - see
 *    ts_mode/ts_byte/voice[2] below. GetTimePT3's own `vars[1]`/second-
 *    chip duration computation (used only when the real byte at file
 *    offset 98 makes a file self-pair) is ALSO now ported in pt3_get_time
 *    - the "TS = $20 case only" framing that used to be true here no
 *    longer is once ts_mode can be true. Still out of scope for this
 *    phase: playlist-level dual-FILE Turbosound pairing (two separate,
 *    possibly different-format files chained via a .ayl playlist's Next
 *    pointer, Players.pas:2677-2686's `else if PlayListItems[Index]^.Next
 *    <> nil` branch) - that's Phase B, a deliberate, separate follow-up
 *    (playlist/.ayl/GUI, out of scope here per this phase's own approved
 *    plan).
 *  - GetTimePT3 (duration/loop-point precompute) - MIG-0101: ported
 *    (pt3_get_time in pt3_file.c), transcribed 1:1 from Players.pas:
 *    15333-15662, INCLUDING its own `vars[1]` dual-voice half as of
 *    MIG-0109 (previously skipped, "equivalent to TS=$20" - no longer an
 *    accurate description now that ts_mode is reachable). Global_Tick_Max
 *    (what GetTimePT3 computes) is what CheckLoopAndStop uses to decide
 *    when a PT3 song naturally ends with Do_Loop off (Players.pas:
 *    8732-8746), so this remains a real playback-correctness dependency,
 *    not just a UI one; see migration_debt.yaml MIG-0101/MIG-0109.
 *  - Embedded/rebased-pointer PT3 loading (see Scope above) - MAddr is
 *    always 0 for standalone files, so the rebase branch never runs for
 *    either real test file.
 */
#ifndef AY_ENGINE_PT3_FILE_H
#define AY_ENGINE_PT3_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  PT3_FILE_OK = 0,
  PT3_FILE_ERR_BAD_HEADER, /* never actually returned by pt3_file_load -
                            * PT3's "ProTracker 3.X compilation of..."/
                            * "Vortex Tracker II..." text signature is,
                            * per real Pascal's own Module_Detector, used
                            * ONLY for Tier C content-sniffing/OS mime-
                            * type registration, never checked by the
                            * real Tier A (extension-trusted) loader -
                            * confirmed via tools/identify_ay_file's own
                            * dispatch.c comment, which documents real
                            * corpus files (e.g. this project's own
                            * DIABOLIS_IN_MUSICA.pt3, deliberately
                            * scene-cracktro-corrupted at byte 0) that
                            * load and play correctly despite failing
                            * this signature entirely. A load-time
                            * signature check was added and then reverted
                            * here for exactly that reason - kept only
                            * for enum-shape symmetry with other formats'
                            * error sets (e.g. sndh_file's ICE!-compressed
                            * case), not because anything can trigger it. */
  PT3_FILE_ERR_TRUNCATED,
} pt3_file_status;

typedef struct pt3_channel {
  uint16_t address_in_pattern;
  uint16_t ornament_pointer;
  uint16_t sample_pointer;
  uint16_t ton;
  uint8_t loop_ornament_position;
  uint8_t ornament_length;
  uint8_t position_in_ornament;
  uint8_t loop_sample_position;
  uint8_t sample_length;
  uint8_t position_in_sample;
  uint8_t volume;
  uint8_t number_of_notes_to_skip;
  uint8_t note;
  uint8_t slide_to_note;
  uint8_t amplitude;
  bool envelope_enabled;
  bool enabled;
  bool simple_gliss;
  int16_t current_amplitude_sliding;
  int16_t ton_slide_count;
  int16_t current_onoff;
  int16_t onoff_delay;
  int16_t offon_delay;
  int16_t ton_slide_delay;
  int16_t current_ton_sliding;
  int16_t ton_accumulator;
  int16_t ton_slide_step;
  int16_t ton_delta;
  int8_t note_skip_counter;
  uint8_t current_noise_sliding;
  uint8_t current_envelope_sliding;
} pt3_channel;

/* Players.pas: PlParams[CNum].PT3 - the per-voice MUTABLE playback state
 * (tick/position cursor, envelope/noise accumulator state, one pt3_channel
 * per AY channel). Everything header-derived (version, ton_table_id,
 * patterns_pointer, samples/ornaments_pointers, number_of_positions,
 * loop_position, position_list_offset) lives on pt3_file directly instead,
 * SHARED between both voices - matching Players.pas's own
 * `PLConsts[1] := PLConsts[0]` whole-record copy (TrModLoaded,
 * Players.pas:2665-2668): both voices read pattern/sample/ornament data
 * from the SAME loaded file bytes, only .TS legitimately differs between
 * PLConsts[0] and PLConsts[1] (see pt3_file's ts_byte below), and this
 * port never duplicates read-only config that would only ever hold an
 * identical copy.
 *
 * `delay` here is Players.pas's `PlParams[CNum].PT3.Delay` - the
 * PER-VOICE, RUNTIME-MUTABLE current tempo (initialized from the file's
 * PT3_Delay header byte, but a pattern's effect-9 "set tempo" command can
 * change it afterwards, Players.pas:12584-12596) - NOT a second copy of
 * the header's own fixed byte. That effect can also cross-write the OTHER
 * voice's delay/delay_counter when ts_mode is active - see
 * pattern_interpreter's own Flag9 handling in pt3_file.c for the exact,
 * asymmetric (voice 0's DelayCounter but never voice 1's) replication of
 * `if TSMode and (PLConsts[1].TS <> $20) then ...` (Players.pas:
 * 12588-12594). MIG-0109 - this cross-voice sync was previously flagged
 * as unported debt (grep the old "TSMode paired-delay sync not ported"
 * comment history) and is now faithfully ported. */
typedef struct pt3_voice {
  pt3_channel chan_a, chan_b, chan_c;

  int16_t env_base;      /* PT3_Parameters: Env_Base.wrd */
  int16_t cur_env_slide;
  int16_t env_slide_add;
  int8_t cur_env_delay;
  int8_t env_delay;
  uint8_t noise_base;
  uint8_t delay;
  uint8_t add_to_noise;
  uint8_t delay_counter;
  uint8_t current_position;

  int64_t global_tick_counter; /* PlConsts[CNum].Global_Tick_Counter, see
                                * pt3_file's own top-level mirror field for
                                * why voice[0]'s copy is also duplicated
                                * there. */
  bool real_end;                /* Players.pas: Real_End[CNum] */
} pt3_voice;

typedef struct pt3_file {
  ay_engine ay;

  uint8_t data[65536]; /* Players.pas: Module.Index (ZRAM) */

  /* [0] = chip 0 / PLConsts[0] / PlParams[0].PT3 (always active).
   * [1] = chip 1 / PLConsts[1] / PlParams[1].PT3 - only initialized and
   * driven when ts_mode is true (Turbosound self-pairing, MIG-0109,
   * closing the PT3 half of MIG-0007). Writes to ay.chip and ay.chip2
   * respectively - see pt3_get_registers_voice in pt3_file.c. */
  pt3_voice voice[2];

  bool ts_mode;   /* AY.pas/Players.pas: TSMode, this-file-only self-pair */
  uint8_t ts_byte; /* Players.pas: PLConsts[1].TS - the raw byte read from
                    * file offset 98 (`Ord(ZRAM.PT3_MusicName[98])`,
                    * confirmed via Players.pas's ModTypes variant record:
                    * every FT.* variant overlays the SAME `Index[0..65536]`
                    * base, so PT3_MusicName[N] literally IS file byte N -
                    * PT3_MusicName[13] already matches this port's own
                    * f->data[13] version-digit read, PT3_MusicName[$1E]/
                    * [$42] already match the existing title/author byte
                    * offsets, and PT3_MusicName[98] sits immediately
                    * before PT3_TonTableId at byte 99, i.e. f->data[99],
                    * both already-verified anchors either side of it).
                    * 0x20 (space) means "not a TS-tagged file" - ts_mode
                    * is only ever true when this is some other value.
                    * Used only by voice[1]'s own position-index formula
                    * (`i := TS*3-3-i`, Players.pas:3733-3734/12722-12723)
                    * - voice[0]'s own PLConsts[0].TS is unconditionally
                    * $20 in the original (TrModLoaded, Players.pas:2651),
                    * so that formula never fires for voice[0]. */

  int version;               /* PlConsts[n].Version */
  uint8_t ton_table_id;      /* PT3_TonTableId - selects among 6 note
                              * tables via GetNoteFreq, see pt3_file.c's
                              * get_note_freq() */
  uint16_t patterns_pointer; /* PT3_PatternsPointer */
  uint16_t samples_pointers[32];
  uint16_t ornaments_pointers[16];
  uint8_t number_of_positions;
  uint8_t loop_position;
  uint32_t position_list_offset; /* byte offset of PT3_PositionList[0] */

  int64_t global_tick_counter; /* Mirrors voice[0].global_tick_counter -
                                * kept as a top-level field (updated every
                                * pt3_file_make_buffer iteration) purely
                                * for player.c's existing external
                                * progress-display contract (player_get_
                                * duration_ticks), which - like the
                                * original's own UI/seek code - only ever
                                * surfaces PLConsts[0]'s counter, never
                                * PLConsts[1]'s, even when TSMode is
                                * active. voice[1]'s own counter (used
                                * internally for the real AND-of-both-
                                * voices end condition, see real_end_all
                                * below) lives only on voice[1] itself. */
  int64_t global_tick_max;     /* PlConsts[CNum].Global_Tick_Max - shared
                                * between both voices via Players.pas's own
                                * `PLConsts[1] := PLConsts[0]` whole-record
                                * copy (TrModLoaded). MIG-0101: computed by
                                * pt3_file_load via a faithful port of
                                * GetTimePT3 (Players.pas:15333-15662), a
                                * pattern-opcode-only simulation (no audio
                                * synthesis) that walks the position list
                                * exactly once - MIG-0109 extended this to
                                * also walk voice[1]'s own pattern data
                                * (GetTimePT3's `vars[1]`) when ts_mode is
                                * set, matching the original's own TS-aware
                                * duration computation exactly (the walk
                                * stops as soon as EITHER voice's pattern
                                * data signals end-of-position, not just
                                * voice 0's - see pt3_get_time). 0 only if
                                * the file's position list is somehow
                                * degenerate (empty) - real files always
                                * get a real value. */
  int64_t loop_tick;           /* GetTimePT3's `Lp` output - the tick at
                                 * which PT3_LoopPosition is reached;
                                 * informational only in this port (no
                                 * caller currently reads it - Pascal's own
                                 * RerollMusic doesn't use Lp for
                                 * IsAYNativeFileType seeking either).
                                 * Force_Loop (MIG-0114, see do_loop/
                                 * force_loop below) doesn't need this
                                 * either - CheckLoopAndStop only ever
                                 * touches Global_Tick_Counter/Real_End,
                                 * never LoopPosition; the actual pattern-
                                 * position wraparound Force_Loop lets
                                 * keep audibly happening is already
                                 * driven entirely by each format's own
                                 * GetRegisters position-list walk,
                                 * independent of this field. */
  bool do_loop;                 /* settings.pas: Do_Loop (default false) -
                                  * see CheckLoopAndStop, MIG-0101. A
                                  * single shared setting in the original
                                  * too (not per-voice) - both voices'
                                  * CheckLoopAndStop(CNum) read the same
                                  * global Do_Loop. */
  bool force_loop;              /* MIG-0114: settings.pas/Tools.pas's
                                  * Force_Loop (CBForceLoop checkbox) - now
                                  * ported (see pt3_check_loop_and_stop's
                                  * own updated comment); the "Force_Loop
                                  * gap" this struct's own loop_tick field
                                  * comment above referred to is resolved.
                                  * Also a single shared setting, same as
                                  * do_loop. */
  bool real_end_all;            /* Players.pas: Real_End_All - MIG-0101.
                                 * MIG-0109: when ts_mode is set, this is
                                 * the real AND of voice[0].real_end and
                                 * voice[1].real_end (Players.pas:
                                 * `Real_End_All := Real_End_All and
                                 * Real_End[0]; if TSMode then ... and
                                 * Real_End[1]`, MakeBufferTracker) -
                                 * both voices' own natural-end condition
                                 * genuinely tracked and required, not
                                 * silently collapsed to voice 0's alone.
                                 * Force_Loop (the "continue playing a
                                 * shorter TS-pair module" case) isn't
                                 * modeled, matching this port's existing
                                 * do_loop-only contract - out of scope,
                                 * not silently different behavior: with
                                 * Force_Loop never set (this port has no
                                 * caller that could set it), CheckLoopAndStop
                                 * behaves identically to the ported
                                 * version. */

  /* Raw (untranscoded CP1251), space-trimmed title/author - Players.pas:
   * "else if FType = FT.PT3" (7405-7427): fixed 32-byte fields at file
   * offsets 0x1E/0x42 - added for the Phase 5 GUI, MIG-0082. The
   * KsaId2[1]/[2] one-byte reads in that same branch (offsets 13 and
   * 98) only affect PLItem.FormatSpec's PT-v3.7-TS-format flag, a
   * playlist/conversion detail out of this port's scope, not ported. */
  char title[64];
  char author[64];
} pt3_file;

/* Parses `data`/`size` (the whole .pt3 file's bytes - standalone files
 * only, see pt3_file.h's file comment) and sets up f->ay + f's tracker
 * state for playback from the beginning of the song. */
pt3_file_status pt3_file_load(pt3_file* f, const uint8_t* data, size_t size,
                               int sample_rate);

#define PT3_FILE_AY_FREQ_DEF 1773400  /* settings.pas: AY_FreqDef */
#define PT3_FILE_INTERRUPT_FREQ_DEF 50000 /* settings.pas: Interrupt_FreqDef */
#define PT3_FILE_SAMPLE_RATE_DEF 48000     /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferTracker (12283-12300) + SynthesizerZX50
 * (AY.pas:1075-1082), the PT3-only (TSMode always false) case, PLUS
 * CheckLoopAndStop (Players.pas:8732-8746, MIG-0101) - sets
 * f->real_end_all once global_tick_counter reaches global_tick_max
 * (when the caller isn't looping), the same "natural end of song"
 * contract ay_file/ym_file/vtx_file/sndh_file's own make_buffer
 * functions already have; looping within the pattern data itself
 * (CurrentPosition wrapping to LoopPosition) keeps working exactly as
 * before regardless. Returns the number of sample frames written -
 * `buffer_length` unless the song ends first (real_end_all become
 * true), same short-return contract as every other format. */
int pt3_file_make_buffer(pt3_file* f, int16_t* buf, int buffer_length);

/* MIG-0112: playlist-level Turbosound pairing's entry point - advances
 * voice 0 by one interrupt frame into an EXTERNAL `chip` (another
 * player's shared engine's chip2, when this PT3 file is playlist-paired
 * rather than self-pairing - see player.h's player_step_registers).
 * Returns false once this voice's own natural end is reached. Only
 * meaningful when f->ts_mode is false (self-pairing and playlist
 * pairing are mutually exclusive, matching TrModLoaded's own `TSMode =
 * False` guard - player_pair_load_song enforces this before ever
 * calling here). */
bool pt3_file_step_registers(pt3_file* f, ay_chip* chip);

#endif /* AY_ENGINE_PT3_FILE_H */
