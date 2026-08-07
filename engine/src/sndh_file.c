#include "ay_engine/sndh_file.h"

#include <stdlib.h>
#include <string.h>

#include "m68k.h"

#define PRG_START 0x2000u
#define MIN_MEM_SIZE 0x80000u

static void put32(uint8_t* mem, uint32_t addr, uint32_t v) {
  mem[addr] = (uint8_t)(v >> 24);
  mem[addr + 1] = (uint8_t)(v >> 16);
  mem[addr + 2] = (uint8_t)(v >> 8);
  mem[addr + 3] = (uint8_t)v;
}
static void put16(uint8_t* mem, uint32_t addr, uint16_t v) {
  mem[addr] = (uint8_t)(v >> 8);
  mem[addr + 1] = (uint8_t)v;
}
static uint16_t be16(const uint8_t* p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
/* sndh.pas's TSNDHTag.W1 is a raw in-memory word overlay over the first 2
 * bytes of a 4-byte tag read directly off disk into a native (x86, LE)
 * Pascal variable - i.e. word = byte0 + byte1*256, NOT the "read two
 * ASCII chars left to right" convention be16() implements above (which
 * happens to match Pascal's DW tag constants for 4-char tags like "SNDH"
 * purely by their own byte-reversal symmetry, but does NOT match the
 * 2-char numeric-tag constants sndh_TA/TB/TC/TD/VBL/etc - confirmed the
 * hard way: this mismatch was the actual root cause of SNDH's PlayFreq
 * never being read as anything but its 50 default, see migration_debt.yaml
 * MIG-0021). Pascal's own constants (sndh_TC=$4354 etc) are used verbatim
 * below specifically because this reads the same native-LE way. */
static uint16_t le16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

/* sndh.pas:598-835's tag scan, narrowed to playback-relevant fields -
 * see sndh_file.h's file comment for exactly what's skipped and why. */
#define SNDH_MAX_TIME_SONGS 64 /* real files have a handful of songs;
                                 * this just bounds storage, matching
                                 * SNDH_MAX_TIME_SONGS-or-fewer songs
                                 * being read - excess songs' TIME
                                 * entries are skipped over (bytes
                                 * still consumed correctly) but not
                                 * retained, same as this port's
                                 * existing "single/few-song" scope
                                 * (sndh_file.h's own file comment). */

typedef struct sndh_tags {
  int number_of_songs;
  int current_song;
  int play_freq;
  int time_raw[SNDH_MAX_TIME_SONGS]; /* sndh.pas: GetTunesTime's
                                       * PlayTimes[] - raw per-song
                                       * seconds from the TIME tag, 0
                                       * where absent/not retained. */
} sndh_tags;

static void parse_tags(const uint8_t* data, uint32_t size, sndh_tags* tags) {
  uint32_t hd_pos, max_header;
  const uint32_t MAX_HEADER = 200;

  tags->number_of_songs = 1;
  tags->current_song = 1;
  tags->play_freq = 50;
  memset(tags->time_raw, 0, sizeof(tags->time_raw));

  if (size < 32) return;
  if (be32(data + 12) != 0x534E4448u) return; /* "SNDH" */

  max_header = MAX_HEADER < size ? MAX_HEADER : size;
  hd_pos = 16;
  while (hd_pos + 4 <= max_header) {
    uint32_t tag_dw = be32(data + hd_pos);
    uint16_t tag_w1 = le16(data + hd_pos);
    hd_pos += 4;

    if (tag_dw == 0x48444E53u) break; /* "HDNS" - end of tag list */

    if (tag_dw == 0x434F4D4Du /* "COMM" */ ||
        tag_dw == 0x5449544Cu /* "TITL" */ ||
        tag_dw == 0x52495050u /* "RIPP" */ ||
        tag_dw == 0x434F4E56u /* "CONV" */ ||
        tag_dw == 0x59454152u /* "YEAR" */) {
      /* Skip the null-terminated string - content unused (UI-only). */
      while (hd_pos < max_header && data[hd_pos] != 0) hd_pos++;
      hd_pos++;
      continue;
    }
    if (tag_dw == 0x54494D45u /* "TIME" */) {
      /* Per-song duration table (2 bytes/song, big-endian raw seconds -
       * sndh.pas: GetTunesTime). Retained now (MIG-0100) - see
       * sndh_file.h's file comment for why this isn't UI-only after
       * all. */
      int n = tags->number_of_songs;
      int i;
      for (i = 0; i < n; i++) {
        uint32_t off = hd_pos + (uint32_t)i * 2;
        if (off + 2 > max_header) break;
        if (i < SNDH_MAX_TIME_SONGS) tags->time_raw[i] = be16(data + off);
      }
      hd_pos += (uint32_t)n * 2;
      continue;
    }
    if (tag_dw == 0x214E5323u /* "!NS#" (sndh__nSN) */ ||
        tag_dw == 0x4E532123u /* "NS!#" (sndh_n_SN) */ ||
        tag_dw == 0x21535423u /* "!ST#" (sndh__nST) */) {
      /* Song-name table: NumberOfSongs words, each a relative offset to a
       * null-terminated name; skip to the end of the whole table+names
       * block (sndh.pas:695-723's GetTunesName bookkeeping). */
      uint32_t maxofs = hd_pos + (uint32_t)tags->number_of_songs * 2 - 1;
      int i;
      for (i = 0; i < tags->number_of_songs; i++) {
        if (hd_pos + (uint32_t)i * 2 + 2 <= max_header) {
          uint32_t t = be16(data + hd_pos + (uint32_t)i * 2) + hd_pos - 4;
          uint32_t j = t;
          while (j < max_header && data[j] != 0) j++;
          if (maxofs < j) maxofs = j;
        }
      }
      hd_pos = maxofs + 1;
      continue;
    }

    /* Numeric tags: 2-byte tag id + ASCII-decimal digits + NUL, per
     * sndh.pas:639-656's GetNumberZ (the outer scan already consumed 4
     * bytes as `tag_dw`/hd_pos+4; back up 2 to re-read right after the
     * 2-byte tag id, matching the original's `Dec(HdPos,2)`). */
    if (tag_w1 == 0x2323u /* "##" */ || tag_w1 == 0x5621u /* "V!" */ ||
        tag_w1 == 0x4154u /* "AT" */ || tag_w1 == 0x4254u /* "BT" */ ||
        tag_w1 == 0x4354u /* "CT" */ || tag_w1 == 0x4454u /* "DT" */ ||
        tag_w1 == 0x2123u /* "!#" */ || tag_w1 == 0x2321u /* "#!" */) {
      uint32_t num_pos = hd_pos - 2;
      int value = 0;
      bool any_digit = false;
      while (num_pos < max_header && data[num_pos] != 0) {
        if (data[num_pos] >= '0' && data[num_pos] <= '9') {
          value = value * 10 + (data[num_pos] - '0');
          any_digit = true;
        }
        num_pos++;
      }
      hd_pos = num_pos + 1;
      if (any_digit && value != 0) {
        if (tag_w1 == 0x2323u) tags->number_of_songs = value;
        else if (tag_w1 == 0x2123u || tag_w1 == 0x2321u) tags->current_song = value;
        else tags->play_freq = value; /* V!/AT/BT/CT/DT - PlayGen ignored */
      }
      continue;
    }

    /* sndh_MuMo/sndh_FLAG and anything else: no playback effect. Original
     * retries at an odd offset (hd_pos-3) when nothing matches, since not
     * all files align tags on even addresses - mirror that exactly. */
    hd_pos -= 3;
  }

  if (tags->current_song > tags->number_of_songs)
    tags->current_song = tags->number_of_songs;
}

sndh_file_status sndh_file_load(sndh_file* f, const uint8_t* data,
                                 size_t size, int sample_rate) {
  sndh_tags tags;
  uint32_t sndh_len;
  uint32_t mem_size;
  double vbl_freq, mc68000_freq, ay_freq;
  int64_t vbl_period;

  memset(f, 0, sizeof(*f));

  if (size < 12) return SNDH_FILE_ERR_TRUNCATED;
  /* sndh.pas:500-501: reads the first 4 bytes as a native (LE) longword
   * and compares against sndh_ICE=$21454349 - i.e. the on-disk bytes are
   * 'I','C','E','!' in file order. */
  if (memcmp(data, "ICE!", 4) == 0) {
    return SNDH_FILE_ERR_ICE_COMPRESSED;
  }

  sndh_len = (uint32_t)size;
  parse_tags(data, sndh_len, &tags);

  /* atari.pas:1104-1114 - MemSize starts at MIN_MEM_SIZE and doubles until
   * big enough; our real test file (36628 bytes) never needs more than
   * MIN_MEM_SIZE, so only that common case is implemented - a file
   * needing more would be silently truncated below (bounded by the
   * PrgStart+0x3000 clamp already present, matching the original's own
   * "too big" clamp) rather than erroring, matching the original's own
   * behavior (it clamps too, see atari.pas:1295-1301). */
  mem_size = MIN_MEM_SIZE;
  while (mem_size < PRG_START + 0x3000u + sndh_len) mem_size <<= 1;

  f->mem = (uint8_t*)calloc(1, mem_size);
  if (!f->mem) return SNDH_FILE_ERR_TRUNCATED;
  f->mem_size = mem_size;

  /* atari.pas:1118-1119: exception vector table filled with a pointer to
   * $1076 (where an RTE instruction lives, written below). */
  {
    uint32_t l;
    for (l = 0; l * 4 <= 0x380u; l++) put32(f->mem, l * 4, 0x1076u);
  }
  put32(f->mem, 0, mem_size);      /* SP */
  put32(f->mem, 4, PRG_START);     /* PC */

  put32(f->mem, 0x070, 0x1076u);   /* VBL handler (overridden below) */
  put32(f->mem, 0x084, 0x1200u);   /* TRAP #1 emulation */
  put32(f->mem, 0x110, 0x1076u);   /* MFP Timer D */
  put32(f->mem, 0x114, 0x1076u);   /* MFP Timer C */
  put32(f->mem, 0x120, 0x1076u);   /* MFP Timer B */
  put32(f->mem, 0x134, 0x1076u);   /* MFP Timer A */
  /* atari.pas:1130-1132 writes these via a raw native-pointer WPtr
   * assignment (no conversion helper) - Pascal source constants are
   * therefore pre-byte-swapped relative to their true big-endian value
   * (WPtr(...)^:=$0100 -> native LE write -> true bytes read back as
   * $0001). put16 here is an explicit big-endian writer, so the TRUE
   * value must be passed directly, not the Pascal literal - MIG-0045. */
  put16(f->mem, 0x448, 0x0001u);
  put16(f->mem, 0x452, 0x0001u);
  put16(f->mem, 0x454, 0x0008u);
  put32(f->mem, 0x456, 0x04CEu);
  put32(f->mem, 0x0FF6, 8u + PRG_START + 0x1000u); /* PLAY entry address */

  /* GenVBL branch (always taken - PlayGen is read but never acted on,
   * matching Players.pas:7068's comment; see sndh_file.h). */
  vbl_freq = (double)tags.play_freq;
  put32(f->mem, 0x070, 0x1000u);
  if (vbl_freq != 50.0) put16(f->mem, 0x448, 0);
  put16(f->mem, 0x452, 0);

  /* atari.pas:1180-1217 - hand-assembled VBL interrupt handler. Every
   * put16 constant below is the byte-swap of the Pascal source's raw
   * WPtr(...)^:=$XXXX literal (see the 0x448/0x452/0x454 comment above
   * for why) - MIG-0045 fixed all of them; they were previously passed
   * through unswapped, corrupting every one of these hand-assembled
   * 16-bit instruction words. */
  put32(f->mem, 0x1000, 0x48E7FFFEu);
  put32(f->mem, 0x1004, 0x207C0000u);
  put16(f->mem, 0x1008, 0x0452u);
  put32(f->mem, 0x100A, 0x0C500000u);
  put32(f->mem, 0x100E, 0x66000062u);
  put32(f->mem, 0x1012, 0x207C0000u);
  put16(f->mem, 0x1016, 0x0454u);
  put32(f->mem, 0x1018, 0x4C900001u);
  put32(f->mem, 0x101C, 0x207C0000u);
  put16(f->mem, 0x1020, 0x0FFEu);
  put32(f->mem, 0x1022, 0x48900001u);
  put32(f->mem, 0x1026, 0x207C0000u);
  put16(f->mem, 0x102A, 0x0456u);
  put32(f->mem, 0x102C, 0x4CD00001u);
  put32(f->mem, 0x1030, 0x207C0000u);
  put16(f->mem, 0x1034, 0x0FFAu);
  put32(f->mem, 0x1036, 0x48D00001u);
  put32(f->mem, 0x103A, 0x207C0000u);
  put16(f->mem, 0x103E, 0x0FFEu);
  put32(f->mem, 0x1040, 0x0C500000u);
  put32(f->mem, 0x1044, 0x6700002Cu);
  put32(f->mem, 0x1048, 0x04500001u);
  put32(f->mem, 0x104C, 0x207C0000u);
  put16(f->mem, 0x1050, 0x0FFAu);
  put32(f->mem, 0x1052, 0x4CD00001u);
  put32(f->mem, 0x1056, 0x06900000u);
  put16(f->mem, 0x105A, 0x0004u);
  put16(f->mem, 0x105C, 0x2040u);
  put32(f->mem, 0x105E, 0x0C900000u);
  put16(f->mem, 0x1062, 0x0000u);
  put32(f->mem, 0x1064, 0x6700FFD4u);
  put32(f->mem, 0x1068, 0x4CD00100u);
  put16(f->mem, 0x106C, 0x4E90u);
  put32(f->mem, 0x106E, 0x6000FFCAu);
  put32(f->mem, 0x1072, 0x4CDF7FFFu);
  put16(f->mem, 0x1076, 0x4E73u); /* RTE */

  /* atari.pas:1234-1240 - TRAP #1 (Super) emulation. */
  put32(f->mem, 0x1200, 0x21CF11FCu);
  put32(f->mem, 0x1204, 0x13FC0001u);
  put32(f->mem, 0x1208, 0x00FFFF00u);
  put32(f->mem, 0x120C, 0x203811FCu);
  put32(f->mem, 0x1210, 0x21FC0000u);
  put32(f->mem, 0x1214, 0x000011FCu);
  put16(f->mem, 0x1218, 0x4E73u); /* RTE */

  /* atari.pas:1243-1257 - Cookie Jar (_CPU=0/68000, _SND=1/YM only,
   * _MCH=0/Atari ST - STe not ported, no real test file needs it). */
  put32(f->mem, 0x5A0, 0x1500u);
  memcpy(f->mem + 0x1500, "_CPU", 4);
  memcpy(f->mem + 0x1508, "_SND", 4);
  put32(f->mem, 0x150C, 1u);
  memcpy(f->mem + 0x1510, "_MCH", 4);
  put32(f->mem, 0x1514, 0u);

  /* atari.pas:1277-1292 - "start point" trampoline: BSR to the SNDH's own
   * INIT (offset 0 of the loaded buffer), then install the precomputed
   * PLAY address into the VBL queue, then idle. put16 constants here are
   * likewise the byte-swap of the Pascal source's raw WPtr literal - see
   * the 0x448 comment above (MIG-0045). */
  put32(f->mem, PRG_START + 0x00, 0x61000FFEu);
  put32(f->mem, PRG_START + 0x04, 0x207C0000u);
  put16(f->mem, PRG_START + 0x08, 0x0FF6u);
  put32(f->mem, PRG_START + 0x0A, 0x4CD00001u);
  put32(f->mem, PRG_START + 0x0E, 0x207C0000u);
  put16(f->mem, PRG_START + 0x12, 0x04CEu);
  put32(f->mem, PRG_START + 0x14, 0x48D00001u);
  put32(f->mem, PRG_START + 0x18, 0x4E722000u); /* STOP #$2000 */
  put32(f->mem, PRG_START + 0x1C, 0x6000FFFAu); /* BRA.W Loop */

  /* atari.pas:1294-1302 - load the SNDH program image, clamped exactly as
   * the original clamps it. */
  {
    uint32_t l = sndh_len;
    uint32_t max_l = mem_size - PRG_START - 0x3000u;
    if (l > max_l) l = max_l;
    memcpy(f->mem + PRG_START + 0x1000u, data, l);
  }

  /* MainWin.pas:1600-1608 / atari.pas's Atari_SetDefault - the fixed
   * Atari ST clock constants (already validated, MIG-0013/0014/0015). */
  mc68000_freq = 32000000.0 / 4.0;
  ay_freq = 32000000.0 / 16.0;
  vbl_period = (int64_t)(mc68000_freq / vbl_freq + 0.5);

  ay_engine_init(&f->ay);
  /* MainWin.pas:1544/2070's Delay_In_Tiks core (see ay_file.c's
   * ay_file_set_chip_freq for the same formula elsewhere) - without this,
   * delay_in_tiks/tik_re stay 0 and ay_averager16 divides by zero the
   * first time the mixer runs. */
  f->ay.delay_in_tiks = (uint32_t)(8192.0 / sample_rate * ay_freq + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
  /* atari.pas's Atari_SetDefault: FrqAyByFrqMC68000 :=
   * round(AyFreq/MC68000Freq/8*4294967296) - the cycle-to-tick ratio
   * SynthesizerSNDH (atari.pas:1717-1748) uses to convert elapsed 68000
   * cycles into AY-chip ticks before ever calling Synthesizer_Stereo16.
   * MIG-0046: previously never set (stayed 0), and sndh_file_make_buffer
   * called ay_synthesizer_stereo16 directly instead of through
   * ay_synthesizer_ay (which performs this exact accumulation) - meaning
   * sample-generation rate was driven by how many times
   * atari_emulate_step happened to be called rather than by elapsed
   * hardware cycles, silently "compensated" by an unrelated bug (Musashi's
   * STOP early-return) that inflated the call count until THAT bug was
   * fixed, at which point playback ran audibly too fast (confirmed:
   * tick_count/rendered-second went from the correct ~200Hz to ~1600-1900Hz
   * without this fix). */
  f->ay.frq_ay_by_frq_z80 =
      (int64_t)(ay_freq / mc68000_freq / 8.0 * 4294967296.0 + 0.5);
  ay_engine_calculate_level_tables(&f->ay);
  atari_emulate_init(&f->atari, f->mem, mem_size, &f->ay, mc68000_freq,
                      vbl_period, ay_freq);
  f->atari.do_loop = false;
  f->play_freq = tags.play_freq;
  {
    /* Players.pas:7112-7115 / sndh.pas:819-821 ("переводим в VBL" -
     * "convert to VBL"): PlayTimes[song] is raw seconds off the TIME
     * tag; multiply by PlayFreq to get the VBL tick count Global_Tick_
     * Max/tick_count_max is measured in, falling back to a 5-minute
     * default (`PlayFreq * 300`) when the tag is absent or the song's
     * own entry is 0 (MIG-0100). */
    int idx = tags.current_song - 1;
    int raw_seconds =
        (idx >= 0 && idx < SNDH_MAX_TIME_SONGS) ? tags.time_raw[idx] : 0;
    int64_t total_vbl = raw_seconds != 0
                             ? (int64_t)raw_seconds * tags.play_freq
                             : (int64_t)tags.play_freq * 300;
    f->atari.tick_count_max = total_vbl > 0 ? total_vbl : 0x7FFFFFFF;
  }

  /* atari.pas: Atari_PrepEmu's `s68000context.dreg[0] := CurrentSong;`
   * (D0 = 1-based current song index; PrepareToPlay's SNDH branch always
   * ends up with CurrentSong=1 for a single-song file like ours). */
  m68k_bus_set_reg(M68K_REG_D0, (uint32_t)tags.current_song);

  return SNDH_FILE_OK;
}

void sndh_file_free(sndh_file* f) {
  free(f->mem);
  f->mem = NULL;
}

int sndh_file_make_buffer(sndh_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->number_of_channels = 2;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_stereo16(ay);
  }
  if (ay->int_flag) return ay->buf_len;

  /* atari.pas: Atari_CheckOuts's AY-register half (MIG-0050) - applies any
   * writes that engine/atari_emulate.c's ym_region_write deferred onto
   * f->atari.pending_writes during the PREVIOUS render call (because the
   * output buffer was already full at that point), before CPU execution
   * resumes this call - matching the original's exact call ordering
   * (right after the top-of-function IntFlag check, before the main
   * Atari_Emulate loop). */
  atari_emulate_flush_pending_writes(&f->atari);

  while (ay->buf_len < buffer_length && !f->real_end_all) {
    atari_emulate_step(&f->atari);
    if (f->atari.real_end_all) f->real_end_all = true;
    if (ay->buf_len < buffer_length &&
        (f->atari.cycle_count >= 150000 || f->real_end_all)) {
      /* Players.pas:14071-14072's `s68000readOdometer >= 150000` check -
       * MIG-0050: ym_region_write now ALSO flushes reentrantly on every
       * AY register write (matching atari.pas's soundchip_writebyte), so
       * this is no longer the ONLY flush point - it's the same "flush at
       * least once per Atari_Emulate call regardless of writes" backstop
       * the original itself has (MakeBufferSNDH calls SynthesizerSNDH
       * here unconditionally past this cycle threshold, on top of
       * soundchip_writebyte's own reentrant calls - calling the
       * accumulator more often than strictly necessary is harmless, it
       * just processes a near-zero tick delta most of the time).
       * The original never resets its odometer at this checkpoint either
       * (it only wraps via Starscream's own 32-bit overflow, practically
       * never within one session) - so in BOTH implementations this
       * condition latches true within the first ~150000 cycles (~18.75ms
       * at 8MHz) and stays true, meaning this flush actually fires on
       * essentially every atari_emulate_step call thereafter. Not a bug -
       * confirmed to match the original's own real behavior, not merely
       * this port's simplification.
       *
       * MIG-0046: this call must go through ay_synthesizer_ay (atari.pas's
       * SynthesizerSNDH wrapper), NOT ay_synthesizer_stereo16 directly
       * (that's only correct for the int_flag branch above, matching
       * MakeBufferSNDH's own top-of-function Synthesizer(Buf) call, which
       * really is the raw, unwrapped Synthesizer). ay_synthesizer_ay
       * performs the actual elapsed-cycles-to-AY-ticks accumulation
       * (Number_Of_Tiks.Re += Odometer*FrqAyByFrqMC68000) BEFORE calling
       * ay_synthesizer_stereo16 internally - without it, sample count is
       * driven by call count instead of elapsed hardware cycles. */
      ay_synthesizer_ay(ay, f->atari.cycle_count);
    }
  }
  return ay->buf_len;
}
