/* C11 port of ay_emul/Players.pas + atari.pas's SNDH file loading/playback
 * (FT.SNDH) - built entirely on the already-ported and oracle-validated
 * Atari hardware layer (engine/atari_emulate.h, engine/mfp.h,
 * engine/dma_sound.h, engine/m68k_bus.h - MIG-0013/0014/0015). This
 * milestone's own new work is: (1) minimal SNDH tag parsing to extract
 * PlayFreq/NumberOfSongs/CurrentSong (sndh.pas's sndh_ExtractTextInfo,
 * scoped to just the playback-relevant fields - see below), and (2) the
 * "boot a minimal Atari TOS environment" memory layout a real SNDH player
 * expects (atari.pas's Atari_PrepMem, 1094-1315): exception vector table,
 * a hand-assembled VBL interrupt handler, a TRAP#1 (Super) stub, a Cookie
 * Jar, and a "start point" trampoline that calls the SNDH's own INIT
 * routine once and then installs its PLAY routine into the VBL queue.
 *
 * MIG-0045: the hand-assembled 16-bit instruction words in that memory
 * layout were, until this entry, systematically byte-swapped - Pascal's
 * WPtr(@bank0[addr])^:=$XXXX is a raw native (little-endian) pointer
 * write, so the ORIGINAL source constants are themselves deliberately
 * pre-swapped (WPtr(...)^:=$734E, labelled {RTE}, really is RTE -
 * Musashi confirms the true opcode is 0x4e73 - only because the native
 * write reverses it back). This port's put16() is an explicit, correct
 * big-endian writer, the opposite convention, so every such constant
 * needed un-swapping before use and none were - corrupting the entire
 * bootstrap. Before the fix: garbage/CPU runaway into unmapped memory
 * after enough cycles, not just silence. After the fix: real, plausible
 * audio, but NOT yet byte-identical to the real Pascal engine (a second,
 * smaller, not-yet-root-caused divergence in exactly when audio starts -
 * see migration_debt.yaml MIG-0045 for what's confirmed vs still open).
 *
 * Ports:
 *  - sndh_UnpackFile in full (sndh.pas:318-596), both branches: the plain
 *    raw-copy path for a non-compressed file, AND (MIG-0016) the actual
 *    "ICE!" depacker (sndh.pas:319-496, a hand-written LZ-style bitstream
 *    decompressor ported from 68000 assembly - see sndh_ice_unpack's own
 *    comment in sndh_file.c for the full port notes, including its one
 *    genuinely-unvalidated sub-path). Real-file validation for the ICE
 *    branch: test_corpus_76/megaintr.snd (a real, user-supplied
 *    ICE-compressed SNDH file, "Mega Intro" by Paradox) - see
 *    migration_debt.yaml MIG-0016.
 *  - sndh_ExtractTextInfo's tag scan (sndh.pas:598-835), narrowed to the
 *    tags that affect playback: VBL/TA/TB/TC/TD (PlayFreq - PlayGen itself
 *    is read but never acted on, matching the original's own comment at
 *    Players.pas:7068 "ignored, always used VBL for the moment"), "##"
 *    (NumberOfSongs), "!#"/"#!" (CurrentSong), and TIME (per-song
 *    duration, sndh.pas:GetTunesTime - MIG-0100: originally scanned-
 *    past-but-discarded like the string tags below, on the mistaken
 *    assumption it was UI-only; it's actually load-bearing for
 *    Players.pas's own RerollMusic/Atari_SeekTo seek support and for
 *    the song's natural end - both wired up now, see sndh_file.h's own
 *    struct/function comments). COMM/TITL/RIPP/CONV/YEAR (author/
 *    title/etc strings) and nSN/nST (song name table) are still
 *    skipped over (their bytes are scanned past correctly, matching
 *    HdPos bookkeeping, so later tags still parse right) but their
 *    *content* is discarded - genuinely playlist/UI metadata only.
 *  - Atari_PrepMem's full memory layout (atari.pas:1094-1315) except
 *    InitSTMem (atari.pas:198-207, a GEMDOS memory-pool allocator
 *    bookkeeping call - irrelevant unless the SNDH program itself calls
 *    GEMDOS Malloc, which simple music-only programs don't) and
 *    IntelizeMemory (a Starscream-specific word-swap optimization with no
 *    equivalent needed here - see engine/atari_emulate.h's file comment
 *    and the earlier m68k_bus.c milestone's finding).
 *  - Atari_InitEmu's MFP/DMA-sound-state reset (atari.pas:1317-1385) - a
 *    subset already covered by atari_emulate_init calling mfp_init/
 *    dma_sound_init/m68k_bus_reset internally; only the SNDH-specific
 *    extra (D0 = CurrentSong, matching Atari_PrepEmu's `s68000context.
 *    dreg[0] := CurrentSong`) is added on top here.
 *  - MakeBufferSNDH's playback loop (Players.pas:14049-14074): drives
 *    atari_emulate_step (already ported) with the same 150000-cycle-or-
 *    end AY-mixer-flush threshold.
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - Multi-song sub-tune selection beyond D0=CurrentSong (no per-song
 *    playtime lookup, no song-switching UI) - our real test file has
 *    exactly one song.
 *  - TA/TB/TC/TD (non-VBL) interrupt-driven PLAY dispatch - the original
 *    itself never implements these either (Atari_PrepMem's GenTA..GenTD
 *    case is dead/commented-out code, confirmed by direct reading), so
 *    this isn't a simplification relative to the real app's own actual
 *    behavior, just a shared limitation.
 *  - InitSTMem / GEMDOS memory-pool emulation, SNDHTimeDB lookup (a
 *    separately-loaded duration database used only to override playlist
 *    display times) - UI-only.
 */
#ifndef AY_ENGINE_SNDH_FILE_H
#define AY_ENGINE_SNDH_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/atari_emulate.h"
#include "ay_engine/hw/ay.h"

typedef enum {
  SNDH_FILE_OK = 0,
  SNDH_FILE_ERR_BAD_HEADER,
  /* MIG-0016: the "ICE!" magic was present but the depacker (see
   * sndh_ice_unpack in sndh_file.c) found the header/stream corrupt or
   * truncated - NOT "ICE-compressed files are unsupported" anymore
   * (that used to be this status's meaning before MIG-0016 ported the
   * real depacker; renamed from SNDH_FILE_ERR_ICE_COMPRESSED to make
   * that clear). A well-formed ICE-compressed file now loads and plays
   * normally, indistinguishable from an uncompressed one past this
   * point. */
  SNDH_FILE_ERR_ICE_CORRUPT,
  SNDH_FILE_ERR_TRUNCATED,
} sndh_file_status;

typedef struct sndh_file {
  atari_emulate atari;
  ay_engine ay;
  uint8_t* mem; /* owned flat 68000 RAM, freed by sndh_file_free */
  uint32_t mem_size;

  bool real_end_all; /* mirrors Players.pas: Real_End_All */
  int play_freq;      /* sndh.pas: PlayFreq (VBL tag, Hz) - the current
                        * song's tick rate; atari.tick_count/
                        * tick_count_max are counted in VBL ticks at
                        * this rate (MIG-0100). */
} sndh_file;

/* MIG-0016: Pascal's sndh_UnpackFile, "ICE!"-compressed branch only
 * (sndh.pas:319-496 depacker + 510-587 header/tail handling - the
 * non-compressed branch is just a raw copy, handled inline by
 * sndh_file_load itself rather than through this function). `data`/
 * `size` is the WHOLE file starting at the "ICE!" magic (already
 * confirmed present by the caller). On SNDH_FILE_OK, `*out_data` is a
 * freshly malloc'd buffer of `*out_size` bytes - caller owns it (free()
 * when done). Exposed (not static) specifically so tests/oracle_diff's
 * dump_engine_state can call it directly and byte-compare its output
 * against the real Pascal oracle without needing to run a full (slow -
 * see migration_debt.yaml MIG-0021) Atari CPU emulation pass just to
 * validate the depacker itself. */
sndh_file_status sndh_ice_unpack(const uint8_t* data, size_t size,
                                  uint8_t** out_data, size_t* out_size);

/* Parses `data`/`size` (the whole .sndh file's bytes) and sets up
 * f->atari/f->ay for playback. `f` takes ownership of a freshly allocated
 * 68000 memory image; call sndh_file_free when done. Also parses the
 * TIME tag (sndh.pas: GetTunesTime) for the selected song and sets
 * f->atari.tick_count_max to the real declared duration in VBL ticks
 * (Players.pas:7112-7115's own `if PlayTimes[CurrentSong-1] = 0 then
 * Time := PlayFreq * 300` 5-minute fallback when the tag is absent/
 * zero) - MIG-0100, previously left effectively unbounded
 * (0x7FFFFFFF), which also silently disabled seek support (see
 * player_get_tick_position's own SNDH case). is_ste (MIG-0121): true for
 * an Atari STe (default, matches every caller's exact behavior before
 * this parameter existed), false for a plain Atari ST, which has no
 * DMA-sound hardware at all - see atari_emulate.h's own is_ste comment
 * for the full atari.pas citation; mirrors asc_file_load's own is_asc0
 * parameter precedent (a load-time format-variant flag). */
sndh_file_status sndh_file_load(sndh_file* f, const uint8_t* data,
                                 size_t size, int sample_rate, bool is_ste);

#define SNDH_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */

void sndh_file_free(sndh_file* f);

/* Players.pas: MakeBufferSNDH (14049-14074). Same contract as
 * ay_file_make_buffer/ym_file_make_buffer: runs until `buffer_length`
 * stereo16 sample frames are written or the song ends
 * (f->real_end_all, now reachable via the real declared duration -
 * see sndh_file_load's own comment, MIG-0100). Callers wanting a
 * different bound (e.g. a fixed-length export) may still override
 * f->atari.tick_count_max/do_loop after loading, before the first
 * call. */
int sndh_file_make_buffer(sndh_file* f, int16_t* buf, int buffer_length);

/* MIG-0017 update: atari.pas's Atari_SeekTo (1684-1705), forward-seek
 * branch only - unlike the generic decode-and-discard seek every other
 * format uses (gui/src/playback.c's do_seek calling player_make_buffer
 * repeatedly and throwing away its output), the original's own SNDH
 * seek never calls Synthesizer/SynthesizerSNDH at all: it just drives
 * Atari_Emulate (this port's atari_emulate_step) forward on its own,
 * using DMASndSkipMC68000Takts to keep the DMA/digi-sample position
 * roughly in sync without generating any audio. `target_tick` must be
 * >= f->atari.tick_count (a backward seek needs a full reload instead,
 * same as every other format - see player.c's player_seek_fast_forward,
 * which enforces this before calling here). No-op if the song has
 * already ended. See sndh_file.c's own definition for the exact
 * mixer-reentrancy-safety mechanism and the DMA-catch-up caveat. */
void sndh_file_seek_fast_forward(sndh_file* f, int64_t target_tick);

/* MIG-0010 update: Players.pas's SNDH_Get_Registers (14094-14097),
 * which is literally `Atari_Emulate_One_VBL` - see migration_debt.yaml
 * MIG-0017's own correction of what that procedure actually is (SNDH's
 * own All_GetRegisters[0] entry, reached by Convs.pas's VBL2PSG/VBL2VTX
 * generic "else" branch like every other non-FT.OUT/FT.ZXAY/FT.EPSG
 * format - NOT a seek-only function, that was this project's own
 * earlier misreading, corrected once the real call graph was traced).
 * Advances the 68000 CPU/timers forward by exactly ONE VBL tick with NO
 * audio synthesis at all, reusing sndh_file_seek_fast_forward's own
 * mixer-reentrancy-safety mechanism (buf=NULL neutralization + per-step
 * pending-writes flush - see that function's own comment in sndh_
 * file.c). Returns true if a real frame was generated, false once
 * real_end_all is set (an idempotent no-op after that point, matching
 * every other format's own step_registers contract). */
bool sndh_file_step_registers(sndh_file* f);

#endif /* AY_ENGINE_SNDH_FILE_H */
