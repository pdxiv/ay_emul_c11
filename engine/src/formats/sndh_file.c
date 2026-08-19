#include "ay_engine/formats/sndh_file.h"

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

/* MIG-0016: sndh.pas:319-496's ICE depacker ("based on Ice.exe by
 * S.V.Bulba", itself converted from Anders Eriksson/Odd Skancke's
 * 68000-assembly 'ice_unpa.s') - a hand-written LZ-style bitstream
 * decoder that decompresses BACKWARDS, starting from the last byte of
 * the packed stream and the last byte of the unpacked buffer, walking
 * down to offset 0. This is a faithful, opcode-for-opcode port of
 * normal_bytes/get_1_bit/get_N_bits - variable names below intentionally
 * mirror the Pascal ones (PI/UI/CB/BitGroup) rather than being renamed
 * to something more "C-idiomatic", to keep this comparable line-by-line
 * against the original during review.
 *
 * `error` is set (and every helper below becomes a no-op returning 0)
 * the moment PI would underflow past the start of the packed buffer, or
 * UI/j would step outside the unpacked buffer - the original has no
 * such guard (a genuinely malformed packed stream would corrupt memory
 * or raise a Pascal range-check exception depending on build settings);
 * this port fails loudly instead, per this workspace's own porting
 * invariants, without changing behavior for any well-formed input (real
 * ICE streams never hit these paths - confirmed via test_corpus_76's
 * megaintr.snd, see MIG-0016's migration_debt.yaml entry). */
typedef struct {
  const uint8_t* packed;
  size_t pi;   /* Pascal: PI - countdown index into `packed` */
  uint8_t cb;  /* Pascal: CB - bit shift register */
  bool error;
} sndh_ice_reader;

static void sndh_ice_reload(sndh_ice_reader* r) {
  if (r->pi == 0) {
    r->error = true;
    r->cb = 0;
    return;
  }
  r->pi--;
  r->cb = r->packed[r->pi];
}

static int sndh_ice_get_1_bit(sndh_ice_reader* r) {
  int result = ((int8_t)r->cb < 0) ? 1 : 0;
  r->cb = (uint8_t)(r->cb + r->cb);
  if (r->cb == 0) {
    sndh_ice_reload(r);
    if (!r->error) {
      int a = result;
      result = ((int8_t)r->cb < 0) ? 1 : 0;
      r->cb = (uint8_t)(r->cb + r->cb + a);
    }
  }
  return result;
}

static int16_t sndh_ice_get_n_bits(sndh_ice_reader* r, int n) {
  int16_t bit_group = 0;
  int i;
  for (i = 0; i < n; i++) {
    int a = ((int8_t)r->cb < 0) ? 1 : 0;
    r->cb = (uint8_t)(r->cb + r->cb);
    if (r->cb == 0) {
      sndh_ice_reload(r);
      if (r->error) return bit_group;
      int b = a;
      a = ((int8_t)r->cb < 0) ? 1 : 0;
      r->cb = (uint8_t)(r->cb + r->cb + b);
    }
    bit_group = (int16_t)(bit_group + bit_group + a);
  }
  return bit_group;
}

/* Pascal: direct_tab (sndh.pas:340-346). */
static const struct {
  int bits;
  int16_t max_value;
  int16_t addition;
} sndh_ice_direct_tab[5] = {
    {15, 0x7fff, 270}, {8, 0x00ff, 15}, {3, 0x0007, 8},
    {2, 0x0003, 5},    {2, 0x0003, 2},
};
/* Pascal: length_tab (sndh.pas:347). */
static const int8_t sndh_ice_length_tab[10] = {10, 2, 1, -1, -1, 8, 4, 2, 1, 0};
/* Pascal: more_offset/more_offset2 (sndh.pas:348-349). */
static const int8_t sndh_ice_more_offset[3] = {12, 5, 8};
static const int16_t sndh_ice_more_offset2[3] = {0x11f, -1, 31};

/* Pascal: normal_bytes (sndh.pas:403-484). `unpacked`/`ui` and
 * `r->packed`/`r->pi` are walked DOWNWARD together; `ui` reaching 0
 * ends the loop (matching Pascal's `until UI <= 0`, which - like all
 * Pascal repeat/until loops - always runs the body at least once). */
static void sndh_ice_normal_bytes(sndh_ice_reader* r, uint8_t* unpacked,
                                   size_t unpacked_size, size_t* ui_io) {
  size_t ui = *ui_io;
  do {
    if (sndh_ice_get_1_bit(r) != 0) {
      int16_t bit_group = 1;
      if (sndh_ice_get_1_bit(r) != 0) {
        int j = 5;
        int i;
        for (i = 0; i <= 4; i++) {
          j--;
          bit_group = sndh_ice_get_n_bits(r, sndh_ice_direct_tab[j].bits);
          if (sndh_ice_direct_tab[j].max_value != bit_group) break;
        }
        bit_group = (int16_t)(bit_group + sndh_ice_direct_tab[j].addition);
      }
      /* copy_direct */
      if (r->error || bit_group < 0 || (size_t)bit_group > r->pi ||
          (size_t)bit_group > ui) {
        r->error = true;
        return;
      }
      r->pi -= (size_t)bit_group;
      ui -= (size_t)bit_group;
      memcpy(unpacked + ui, r->packed + r->pi, (size_t)bit_group);
    }
    /* test_if_end */
    if (ui > 0) {
      int16_t bit_group;
      int k; /* Pascal: normal_bytes's own local `k: integer` (NOT
              * smallint like BitGroup - a distinct, wider var). */
      int i = 4;
      for (;;) {
        if (sndh_ice_get_1_bit(r) == 0) break;
        i--;
        if (i == 0) break;
      }
      bit_group = 0;
      {
        int nbits = sndh_ice_length_tab[i];
        if (nbits >= 0) bit_group = sndh_ice_get_n_bits(r, nbits);
      }
      k = sndh_ice_length_tab[i + 5] + bit_group;
      if (k != 0) {
        i = 2;
        for (;;) {
          if (sndh_ice_get_1_bit(r) == 0) break;
          i--;
          if (i == 0) break;
        }
        bit_group = sndh_ice_get_n_bits(r, sndh_ice_more_offset[i]);
        bit_group = (int16_t)(bit_group + sndh_ice_more_offset2[i]);
        if (bit_group < 0) bit_group = (int16_t)(bit_group - k);
      } else {
        /* get_offset_2 */
        i = 6;
        int16_t add = -1;
        if (sndh_ice_get_1_bit(r) != 0) {
          i = 9;
          add = 0x3f;
        }
        bit_group = sndh_ice_get_n_bits(r, i);
        bit_group = (int16_t)(bit_group + add);
      }
      /* depack_bytes: copies (k+2) bytes total, byte-by-byte since the
       * source/dest ranges can legitimately overlap (this IS the LZ
       * back-reference mechanism - a memmove would be equally correct
       * here, but a byte loop matches the original's own explicit
       * per-byte copy exactly and keeps the direction unambiguous). */
      if (r->error) return;
      {
        long j = (long)ui + k + bit_group + 1;
        int idx;
        if (j < 0 || (size_t)j >= unpacked_size || ui == 0) {
          r->error = true;
          return;
        }
        ui--;
        unpacked[ui] = unpacked[j];
        for (idx = 0; idx <= k; idx++) {
          j--;
          if (j < 0 || ui == 0) {
            r->error = true;
            return;
          }
          ui--;
          unpacked[ui] = unpacked[j];
        }
      }
    }
  } while (ui > 0 && !r->error);
  *ui_io = ui;
}

/* Pascal: sndh_UnpackFile's tail (sndh.pas:530-587) - the "not tested"
 * post-processing transform, gated by one extra bit read right after
 * normal_bytes. The original author's own comment (sndh.pas:532-535)
 * says this path has never been exercised against a real file; ported
 * faithfully anyway (no unported/omitted opcode) rather than silently
 * dropped, but genuinely UNVALIDATED - see MIG-0016's migration_debt.yaml
 * entry for whether test_corpus_76's real ICE file happens to trigger
 * it (if not, this remains translated-but-unreachable-by-known-corpus,
 * same status the whole depacker had before that file was added). */
static void sndh_ice_untested_transform(sndh_ice_reader* r, uint8_t* unpacked,
                                         size_t unpacked_size) {
  uint16_t k_count = 0xf9f;
  uint16_t kk;
  size_t ui = unpacked_size;
  if (sndh_ice_get_1_bit(r) != 0) {
    k_count = 0xf9f;
    if (sndh_ice_get_1_bit(r) != 0) {
      k_count = (uint16_t)sndh_ice_get_n_bits(r, 16);
    }
    for (kk = 0; kk <= k_count && !r->error; kk++) {
      uint16_t m1 = 0, m2 = 0, m3 = 0, m4 = 0;
      int i;
      if (ui < 8) {
        r->error = true;
        break;
      }
      for (i = 0; i < 4; i++) {
        int16_t l;
        int j;
        ui -= 2;
        /* Pascal: l := PSmallInt(@SNDHBuffer[UI])^ - a native (x86, LE)
         * pointer read, NOT the write side's deliberate big-endian
         * byte order below (see this function's own file comment on
         * m.h1/m.l1 for that asymmetry). */
        l = (int16_t)(unpacked[ui] | (unpacked[ui + 1] << 8));
        for (j = 0; j < 4; j++) {
          int a = (l < 0) ? 1 : 0;
          l = (int16_t)(l + l);
          m1 = (uint16_t)(m1 + m1 + a);
          a = (l < 0) ? 1 : 0;
          l = (int16_t)(l + l);
          m2 = (uint16_t)(m2 + m2 + a);
          a = (l < 0) ? 1 : 0;
          l = (int16_t)(l + l);
          m3 = (uint16_t)(m3 + m3 + a);
          a = (l < 0) ? 1 : 0;
          l = (int16_t)(l + l);
          m4 = (uint16_t)(m4 + m4 + a);
        }
      }
      unpacked[ui] = (uint8_t)(m1 >> 8);
      unpacked[ui + 1] = (uint8_t)m1;
      unpacked[ui + 2] = (uint8_t)(m2 >> 8);
      unpacked[ui + 3] = (uint8_t)m2;
      unpacked[ui + 4] = (uint8_t)(m3 >> 8);
      unpacked[ui + 5] = (uint8_t)m3;
      unpacked[ui + 6] = (uint8_t)(m4 >> 8);
      unpacked[ui + 7] = (uint8_t)m4;
    }
  }
}

/* Pascal: sndh_UnpackFile (sndh.pas:318-596), the "ICE!"-compressed
 * branch only (the non-compressed branch is just a raw copy, already
 * handled inline by sndh_file_load below). Returns a freshly malloc'd
 * buffer of exactly `*out_size` bytes via `*out_data` on success -
 * caller owns it (free() when done). `data`/`size` is the WHOLE file,
 * starting at the "ICE!" magic. */
sndh_file_status sndh_ice_unpack(const uint8_t* data, size_t size,
                                  uint8_t** out_data, size_t* out_size) {
  uint32_t packed_size_field, unpacked_size_field;
  size_t packed_size;
  sndh_ice_reader r;
  uint8_t* unpacked;
  size_t ui;

  if (size < 12) return SNDH_FILE_ERR_TRUNCATED;
  /* Pascal: BlockRead(f,PackedSize,4); PackedSize:=SwapEndian(PackedSize) -
   * on-disk big-endian, includes this 12-byte header. */
  packed_size_field = be32(data + 4);
  unpacked_size_field = be32(data + 8);

  /* Defensive additions (see this function's own file comment above) -
   * Pascal's only header sanity check is `if PackedSize > FileSize(f)`;
   * the two checks below catch cases that check would miss (a
   * PackedSize field too small to even cover its own 12-byte header,
   * or a file truncated before the packed data it claims to contain)
   * before they could underflow/overrun, rather than relying on a
   * Pascal range-check exception that may or may not be enabled. */
  if (packed_size_field < 12) return SNDH_FILE_ERR_ICE_CORRUPT;
  if (packed_size_field > size) return SNDH_FILE_ERR_ICE_CORRUPT;
  if (unpacked_size_field == 0) return SNDH_FILE_ERR_ICE_CORRUPT;
  packed_size = (size_t)packed_size_field - 12;
  if (12 + packed_size > size) return SNDH_FILE_ERR_ICE_CORRUPT;

  unpacked = (uint8_t*)malloc(unpacked_size_field);
  if (!unpacked) return SNDH_FILE_ERR_TRUNCATED;

  r.packed = data + 12;
  r.pi = packed_size;
  r.error = false;
  /* Pascal: dec(PI); CB := PackedBuffer[PI] - prime the bit register
   * from the LAST packed byte before the first get_1_bit/get_N_bits
   * call (which would otherwise reload against an uninitialized CB). */
  if (r.pi == 0) {
    free(unpacked);
    return SNDH_FILE_ERR_ICE_CORRUPT;
  }
  r.pi--;
  r.cb = r.packed[r.pi];

  ui = unpacked_size_field;
  sndh_ice_normal_bytes(&r, unpacked, unpacked_size_field, &ui);
  if (!r.error) {
    sndh_ice_untested_transform(&r, unpacked, unpacked_size_field);
  }
  if (r.error) {
    free(unpacked);
    return SNDH_FILE_ERR_ICE_CORRUPT;
  }

  *out_data = unpacked;
  *out_size = unpacked_size_field;
  return SNDH_FILE_OK;
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
                                 size_t size, int sample_rate, bool is_ste) {
  sndh_tags tags;
  uint32_t sndh_len;
  uint32_t mem_size;
  double vbl_freq, mc68000_freq, ay_freq;
  int64_t vbl_period;
  uint8_t* unpacked = NULL; /* MIG-0016: owned when the ICE branch below
                              * runs; freed after its last use (the
                              * memcpy further down), or on any early
                              * return in between. */

  memset(f, 0, sizeof(*f));

  if (size < 12) return SNDH_FILE_ERR_TRUNCATED;
  /* sndh.pas:500-501: reads the first 4 bytes as a native (LE) longword
   * and compares against sndh_ICE=$21454349 - i.e. the on-disk bytes are
   * 'I','C','E','!' in file order. MIG-0016: this used to be a hard
   * rejection (SNDH_FILE_ERR_ICE_COMPRESSED); now `data`/`size` are
   * rebound to point at the freshly depacked buffer and everything below
   * proceeds exactly as it would for a naturally-uncompressed file. */
  if (memcmp(data, "ICE!", 4) == 0) {
    size_t unpacked_size;
    sndh_file_status ice_st =
        sndh_ice_unpack(data, size, &unpacked, &unpacked_size);
    if (ice_st != SNDH_FILE_OK) return ice_st;
    data = unpacked;
    size = unpacked_size;
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
  if (!f->mem) {
    free(unpacked);
    return SNDH_FILE_ERR_TRUNCATED;
  }
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
  free(unpacked); /* MIG-0016: last use of `data`/`unpacked` was above. */

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
                      vbl_period, ay_freq, vbl_freq, is_ste);
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
  /* See fxm_file.c's make_buffer for why number_of_channels is not
   * reset here (player_set_number_of_channels's load-time override
   * must persist across buffer-fill calls). */
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_dispatch(ay); /* MIG-0103: was hardcoded stereo16 */
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

void sndh_file_seek_fast_forward(sndh_file* f, int64_t target_tick) {
  if (target_tick < f->atari.tick_count || f->real_end_all) return;

  /* atari.pas: Atari_SeekTo's forward-seek branch calls `Atari_CheckOuts`
   * (apply any AY writes deferred from the previous render call) instead
   * of the backward branch's full `Atari_PrepEmu` reload; `IntFlag :=
   * False` follows unconditionally in both branches. */
  atari_emulate_flush_pending_writes(&f->atari);
  f->ay.int_flag = false;

  /* engine/atari_emulate.c has TWO reentrant ay_synthesizer_ay call
   * sites (the mid-instruction AY-register-write flush at the top of
   * this file's own comment on MIG-0050, and the per-step DMA-boundary
   * flush) - both need to be neutralized for the duration of this loop,
   * matching Atari_SeekTo never calling Synthesizer/SynthesizerSNDH at
   * all (SynthesizerSNDH's own `if Seeking then` branch skips real
   * mixing entirely - see this project's migration_debt.yaml MIG-0017
   * update for the full citation). ay_synthesizer_dispatch's own first
   * check (`if (e->buf == NULL) return;`) makes both call sites safe
   * unconditional no-ops regardless of buf_len/buffer_length - safer
   * than only adjusting buffer_length, since f->ay.buf may be a stale
   * pointer left over from the LAST real make_buffer call (already out
   * of the caller's scope) rather than something still safe to write
   * into. Restored after the loop; the next real sndh_file_make_buffer
   * call would reassign all three anyway, but restoring here keeps this
   * function's own effect on `f` fully self-contained. */
  void* saved_buf = f->ay.buf;
  int saved_buffer_length = f->ay.buffer_length;
  int saved_buf_len = f->ay.buf_len;
  f->ay.buf = NULL;

  while (f->atari.tick_count < target_tick && !f->real_end_all) {
    atari_emulate_step(&f->atari);
    if (f->atari.real_end_all) f->real_end_all = true;
    /* atari_emulate.h's own pending_writes queue is a FIXED 32-entry
     * buffer (see queue_write) - a register write that lands while the
     * reentrant flush at atari_emulate.c's ym_region_write sees
     * buf_len < buffer_length (using whichever stale values were saved
     * above) takes the "queue it" branch rather than applying directly,
     * since ay_synthesizer_dispatch returning immediately on buf==NULL
     * never gets a chance to reach the code that would apply it
     * otherwise. A seek spanning many thousands of ticks can easily
     * queue far more than 32 writes; queue_write silently DROPS
     * anything past that cap, permanently losing register state, not
     * just delaying it. Flushing every iteration (each step queues only
     * a handful of writes at most) keeps the queue nowhere near that
     * cap - found via direct A/B testing against the generic decode-
     * and-discard path: chip.reg[8]/9/10 were wrong (frozen at their
     * very-early-seek values) for any seek longer than ~32 register
     * writes without this, a real bug not a hypothetical one. */
    atari_emulate_flush_pending_writes(&f->atari);
  }

  f->ay.buf = saved_buf;
  f->ay.buffer_length = saved_buffer_length;
  f->ay.buf_len = saved_buf_len;

  /* atari.pas: Atari_SeekTo's `Number_Of_Tiks.re := 0;` - discard
   * whatever AY-tick backlog accumulated while ay_synthesizer_ay was
   * neutralized above, so normal playback doesn't try to "catch up" a
   * huge synthesis burst the instant it resumes. */
  f->ay.number_of_tiks = 0;

  /* Best-effort DMA/digi-sample position catch-up - NOT a direct,
   * line-for-line port of DMASndSkipMC68000Takts's own incremental
   * algorithm (atari.pas:344-380's exact PCurr<0 countdown scheme isn't
   * fully understood/validated against a real oracle scenario - none
   * exercises DMA-during-seek currently). Instead: dma_sound.c's own
   * pbase/pcurr are literal snapshots of atari.cycle_count at DMA-start
   * (see dma_sound_write_byte_at's Ctrl_DMASnd-equivalent assignment),
   * and pcurr is DESIGNED to track elapsed real cycles approximately
   * (advanced by one fixed real-cycle-equivalent step per audio sample
   * actually generated - dma_sound_mix's own `pcurr += mc68000_freq*8/
   * ay_freq`). Setting it directly to the current cycle_count is the
   * natural target state "as if" samples had kept being generated
   * throughout the skipped region - reasoned from that invariant, not
   * copied from the original's own code. Confirmed exercised by a real
   * file (test_corpus_76/megaintr.snd uses DMA sound), but NOT byte-
   * verified against the Pascal oracle - see migration_debt.yaml. */
  if (f->atari.dma.play) f->atari.dma.pcurr = (double)f->atari.cycle_count;
}

bool sndh_file_step_registers(sndh_file* f) {
  int64_t start_tick;
  void* saved_buf;
  int saved_buffer_length;
  int saved_buf_len;

  if (f->real_end_all) return false;
  start_tick = f->atari.tick_count;

  /* Same mixer-reentrancy-safety mechanism as sndh_file_seek_fast_
   * forward above - see its own comment for the full explanation of
   * why buf=NULL neutralization and a per-step pending-writes flush are
   * both needed (a stale f->ay.buf pointer, and atari_emulate.h's own
   * fixed 32-entry pending_writes queue silently overflowing on a long
   * export otherwise). */
  saved_buf = f->ay.buf;
  saved_buffer_length = f->ay.buffer_length;
  saved_buf_len = f->ay.buf_len;
  f->ay.buf = NULL;

  /* Players.pas: Atari_Emulate_One_VBL calls Atari_CheckOuts (this
   * port's atari_emulate_flush_pending_writes is its AY-write half)
   * exactly ONCE per call, at the very top only - Pascal's own AYOuts
   * queue (the direct analogue of atari_emulate.h's pending_writes) is
   * deliberately NOT drained again before this call returns, meaning
   * anything queued DURING this tick's own Atari_Emulate execution
   * stays invisible to chip.reg[] until the NEXT call's own top-of-
   * function flush - a real, one-VBL-tick write-visibility LAG the
   * original has by design (Atari_CheckOuts is only ever called once
   * per MakeBufferSNDH/Atari_Emulate_One_VBL invocation). Flushing more
   * eagerly (either mid-loop, matching sndh_file_seek_fast_forward's
   * own per-iteration flush - correct there, for its own much-longer-
   * running use case - or even just once more at THIS function's own
   * end) makes a queued write chip-visible sooner than real Pascal does,
   * which can change what a LATER 68000 instruction that reads AY
   * registers back observes, cascading into genuinely different
   * register output for the rest of the tick - confirmed by direct
   * oracle-diff testing against exactly this discrepancy (see
   * migration_debt.yaml). A single VBL tick's own write count is
   * bounded by what one interrupt frame can plausibly do (nowhere near
   * the pending_writes queue's 32-entry cap in practice), so the
   * queue-overflow risk that motivated seek_fast_forward's own eager
   * flushing doesn't apply here. */
  atari_emulate_flush_pending_writes(&f->atari);
  while (f->atari.tick_count == start_tick && !f->atari.real_end_all) {
    atari_emulate_step(&f->atari);
  }

  f->ay.buf = saved_buf;
  f->ay.buffer_length = saved_buffer_length;
  f->ay.buf_len = saved_buf_len;

  if (f->atari.real_end_all) f->real_end_all = true;
  return true;
}
