/* C11 port of ay_emul/Players.pas's VTX register-dump format (FT.VTX),
 * confirmed via the real test file (songs/vtx/Intro.vtx) to be lh5/LZH-
 * compressed (engine/lh5.h, already validated by MIG-0019) but WITHOUT
 * the "-lh5-"-signature TLZHFileHeader wrapper YM files use - the
 * compressed payload starts immediately after the VTX header+strings,
 * with its length simply being "whatever's left in the file" (no
 * separately-encoded CompSize field). Otherwise the simplest of all the
 * formats ported so far: a pure register-plane data dump (no Z80
 * execution, no digidrum/sinus/envelope sub-timer engine like YM5i - see
 * ym_file.h) - matches PSG/YM3/YM3b in shape.
 *
 * Ports:
 *  - The VTX header parse (Players.pas:7650-7707): both the short
 *    ("AY"/"YM", uppercase Id, no Year/Programm/Tracker/Comment fields)
 *    and long ("ay"/"ym", lowercase Id, with them) header variants -
 *    our real test file is the long "ay" variant. Programm/Tracker/
 *    Comment/Title/Author string *content* is skipped (UI-only), but
 *    their bytes are correctly walked past so F_Offset (where the
 *    compressed payload starts) lands right, matching the original's
 *    own HdPos-equivalent bookkeeping.
 *  - The lh5 decompression call (Players.pas:2736-2752).
 *  - VTX_YM3_YM3b_Get_Registers (Players.pas:12798-12825) and
 *    MakeBufferVTX (Players.pas:12768-12796) - both very short,
 *    register-plane-interleaved reads plus the same SynthesizerZX50
 *    (AY.pas:1075-1082) fixed-tick-budget cadence already ported for
 *    PT3 (MIG-0020).
 *  - The PrepareToPlay FT.VTX init (Players.pas:4032: `Position_In_VTX
 *    := 0`) and Chip_Type selection (Players.pas:7674-7677: AY_Chip for
 *    "ay"/"AY"-tagged files, YM_Chip for "ym"/"YM"-tagged ones - our
 *    real test file is "ay"-tagged).
 *
 * Deliberately not ported here (see migration_debt.yaml):
 *  - YM3/YM3b (a different, non-VTX-magic sibling format sharing the
 *    same VTX_YM3_YM3b_Get_Registers reader but a different loader path,
 *    Players.pas:2790-2811) - out of scope, no real test file exercises
 *    it (VTX's OWN loader, used here, is a different code path).
 */
#ifndef AY_ENGINE_VTX_FILE_H
#define AY_ENGINE_VTX_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"

typedef enum {
  VTX_FILE_OK = 0,
  VTX_FILE_ERR_BAD_HEADER,
  VTX_FILE_ERR_LZH_INVALID,
  VTX_FILE_ERR_TRUNCATED,
} vtx_file_status;

typedef struct vtx_file {
  ay_engine ay;

  uint8_t* data; /* owned, decompressed register-plane buffer */
  int32_t data_size;

  int32_t number_of_vbls; /* Players.pas: NumberOfVBLs */
  int32_t loop_vbl;       /* Players.pas: LoopVBL */
  int32_t position_in_vtx;

  int64_t global_tick_counter;
  int64_t global_tick_max;
  bool do_loop;
  bool real_end_all;

  int64_t ay_tiks_in_interrupt; /* AY.pas: AY_Tiks_In_Interrupt, from this
                                 * file's own ChipFrq/InterFrq header
                                 * fields - see MainWin.pas:2070. */
  double interrupt_freq; /* settings.pas: Interrupt_Freq (InterFrq*1000) -
                           * the same value already used to derive
                           * ay_tiks_in_interrupt above, stored directly
                           * too (added for the Phase 5 GUI's seek/
                           * duration support, MIG-0080 - one VTX "tick"
                           * is 1/interrupt_freq real seconds, same
                           * convention as ym_file.h's own field). */
  double ay_freq; /* settings.pas: AY_Freq, from the header's ChipFrq -
                    * previously only used transiently at load time to
                    * derive ay_tiks_in_interrupt, now stored (matching
                    * ym_file.h's own ay_freq field) so a live chip-
                    * frequency override (player_set_chip_freq, MIG-0087)
                    * can recompute ay_tiks_in_interrupt later too. */
} vtx_file;

/* Parses `data`/`size` (the whole .vtx file's bytes) and sets up f->ay
 * for playback. `f` takes ownership of a decompressed copy; call
 * vtx_file_free when done. */
vtx_file_status vtx_file_load(vtx_file* f, const uint8_t* data, size_t size,
                               int sample_rate);

void vtx_file_free(vtx_file* f);

#define VTX_FILE_SAMPLE_RATE_DEF 48000 /* settings.pas: SampleRateDef */

/* Players.pas: MakeBufferVTX (12768-12796). Same contract as
 * pt3_file_make_buffer: runs until `buffer_length` stereo16 sample
 * frames are written or the song ends (f->real_end_all). */
int vtx_file_make_buffer(vtx_file* f, int16_t* buf, int buffer_length);

/* MIG-0010 update: the register-generation half of vtx_file_make_
 * buffer's own inner loop body (VTX_YM3_YM3b_Get_Registers +
 * position_in_vtx loop-wraparound), WITHOUT the audio-synthesis call -
 * reached by Convs.pas's VBL2PSG/VBL2VTX generic "else" branch like
 * every other non-FT.OUT/FT.ZXAY/FT.EPSG format. Returns true if a real
 * frame was generated, false once real_end_all is set (an idempotent
 * no-op after that point). */
bool vtx_file_step_registers(vtx_file* f);

#endif /* AY_ENGINE_VTX_FILE_H */
