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
 *  - sndh_UnpackFile's "not ICE-compressed" path only (sndh.pas:497-509):
 *    confirmed via the real test file (songs/sndh/Temple_of_Asherah.sndh)
 *    that it is NOT "ICE!"-compressed - the actual ICE depacker (sndh.pas:
 *    319-496, a hand-written LZ-style decompressor ported from 68000
 *    assembly) is NOT ported this milestone, see below.
 *  - sndh_ExtractTextInfo's tag scan (sndh.pas:598-835), narrowed to the
 *    tags that affect playback: VBL/TA/TB/TC/TD (PlayFreq - PlayGen itself
 *    is read but never acted on, matching the original's own comment at
 *    Players.pas:7068 "ignored, always used VBL for the moment"), "##"
 *    (NumberOfSongs), "!#"/"#!" (CurrentSong). COMM/TITL/RIPP/CONV/YEAR
 *    (author/title/etc strings), TIME (per-song duration), and
 *    nSN/nST (song name table) are skipped over (their bytes are scanned
 *    past correctly, matching HdPos bookkeeping, so later tags still
 *    parse right) but their *content* is discarded - all are playlist/UI
 *    metadata, irrelevant to correct audio playback.
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
 *  - The ICE depacker itself (sndh.pas:319-496) - our real test file
 *    doesn't need it; a future ICE-compressed test file would need this
 *    ported (sndh_file_load fails loudly - SNDH_FILE_ERR_ICE_COMPRESSED -
 *    rather than silently misplaying).
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

#include "ay_engine/atari_emulate.h"
#include "ay_engine/ay.h"

typedef enum {
  SNDH_FILE_OK = 0,
  SNDH_FILE_ERR_BAD_HEADER,
  SNDH_FILE_ERR_ICE_COMPRESSED, /* not ported - see sndh_file.h file comment */
  SNDH_FILE_ERR_TRUNCATED,
} sndh_file_status;

typedef struct sndh_file {
  atari_emulate atari;
  ay_engine ay;
  uint8_t* mem; /* owned flat 68000 RAM, freed by sndh_file_free */
  uint32_t mem_size;

  bool real_end_all; /* mirrors Players.pas: Real_End_All */
} sndh_file;

/* Parses `data`/`size` (the whole .sndh file's bytes) and sets up
 * f->atari/f->ay for playback. `f` takes ownership of a freshly allocated
 * 68000 memory image; call sndh_file_free when done. */
sndh_file_status sndh_file_load(sndh_file* f, const uint8_t* data,
                                 size_t size, int sample_rate);

#define SNDH_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */

void sndh_file_free(sndh_file* f);

/* Players.pas: MakeBufferSNDH (14049-14074). Same contract as
 * ay_file_make_buffer/ym_file_make_buffer: runs until `buffer_length`
 * stereo16 sample frames are written or the song ends
 * (f->real_end_all) - though as with pt3_file, no real end-of-song
 * duration is known for SNDH either (Players.pas relies on
 * atari_emulate's tick_count_max, which the caller should set via
 * f->atari.tick_count_max/do_loop before the first call if a bounded
 * run is wanted; left at atari_emulate_init's own default otherwise). */
int sndh_file_make_buffer(sndh_file* f, int16_t* buf, int buffer_length);

#endif /* AY_ENGINE_SNDH_FILE_H */
