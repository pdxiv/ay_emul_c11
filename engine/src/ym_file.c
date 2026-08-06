#include "ay_engine/ym_file.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/lh5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint16_t be16u(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32u(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
/* TLZHFileHeader's own multi-byte fields (CompSize/UCompSize) are the raw
 * LZH archive format's native little-endian, unlike YM/AY header fields -
 * see ym_file.h's file comment and the exploration note this mirrors. */
static int32_t le32s(const uint8_t* p) {
  return (int32_t)(((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* Players.pas:17144 - YM6SinusTable[i,j] := round(sin(j*2*pi/8)*i/2+i/2). */
static uint8_t ym6_sinus_table[16][8];
static bool ym6_sinus_table_ready = false;
static void ym6_sinus_table_init(void) {
  int i, j;
  if (ym6_sinus_table_ready) return;
  for (i = 0; i < 16; i++) {
    for (j = 0; j < 8; j++) {
      double v = sin(j * 2.0 * M_PI / 8.0) * i / 2.0 + i / 2.0;
      ym6_sinus_table[i][j] = (uint8_t)llround(v);
    }
  }
  ym6_sinus_table_ready = true;
}

ym_file_status ym_file_load(ym_file* f, const uint8_t* data, size_t size,
                             int sample_rate) {
  const uint8_t* payload;
  size_t payload_size;
  uint8_t* unpacked = NULL;
  int32_t unpacked_size;
  uint32_t magic;
  uint32_t chip_frq;
  uint16_t inter_frq, num_of_dig, add_size;
  uint32_t num_of_tiks, loop;
  int32_t k;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 6) return YM_FILE_ERR_BAD_HEADER;

  if (memcmp(data + 2, "-lh5-", 5) == 0) {
    /* TLZHFileHeader (22 bytes): HSize(1) ChkSum(1) Method(5) CompSize(4,
     * LE) UCompSize(4, LE) Dos_DT(4) Attr(2) FileNameLen(1). */
    uint8_t hsize;
    int32_t comp_size, ucomp_size;
    int64_t comp_off;

    if (size < 22) return YM_FILE_ERR_TRUNCATED;
    hsize = data[0];
    comp_size = le32s(data + 7);
    ucomp_size = le32s(data + 11);
    comp_off = (int64_t)hsize + 2;
    if (comp_size <= 0 || ucomp_size <= 0 ||
        (uint64_t)comp_off >= size)
      return YM_FILE_ERR_LZH_INVALID;
    if ((uint64_t)comp_off + (uint32_t)comp_size > size)
      return YM_FILE_ERR_TRUNCATED;

    unpacked_size = ucomp_size;
    unpacked = (uint8_t*)malloc((size_t)unpacked_size);
    if (!unpacked) return YM_FILE_ERR_TRUNCATED;
    if (!lh5_decompress(data + comp_off, comp_size, unpacked,
                         unpacked_size)) {
      free(unpacked);
      return YM_FILE_ERR_LZH_INVALID;
    }
    payload = unpacked;
    payload_size = (size_t)unpacked_size;
  } else {
    /* Not LHA-compressed (a raw YM5!/YM6! file) - still copy into an
     * owned buffer so ym_file_free's ownership model stays uniform. */
    unpacked_size = (int32_t)size;
    unpacked = (uint8_t*)malloc((size_t)unpacked_size);
    if (!unpacked) return YM_FILE_ERR_TRUNCATED;
    memcpy(unpacked, data, (size_t)unpacked_size);
    payload = unpacked;
    payload_size = size;
  }

  if (payload_size < 4) {
    free(unpacked);
    return YM_FILE_ERR_TRUNCATED;
  }
  magic = be32u(payload); /* compared big-endian-read against the ASCII
                            * bytes directly, e.g. "YM5!" == 0x594D3521 */
  if (magic != 0x594D3521u /* "YM5!" */ && magic != 0x594D3621u /* "YM6!" */) {
    free(unpacked);
    return YM_FILE_ERR_UNSUPPORTED_TYPE;
  }
  f->is_ym6 = (magic == 0x594D3621u);

  /* TYM5FileHeader (34 bytes): Id(4) Leo(8) Num_of_tiks(4) Song_Attr(4)
   * Num_of_Dig(2) ChipFrq(4) InterFrq(2) Loop(4) Add_Size(2). All BE. */
  if (payload_size < 34) {
    free(unpacked);
    return YM_FILE_ERR_TRUNCATED;
  }
  num_of_tiks = be32u(payload + 12);
  /* Song_Attr's only bit this milestone reads is bit0 of its last byte
   * (payload[19] & 1, checked below) - the rest (incl. bits 25-26,
   * digidrum sample-format selector) is only used by the digidrum
   * sample-format conversion this milestone doesn't port, see
   * ym_file.h's file comment. */
  num_of_dig = be16u(payload + 20);
  chip_frq = be32u(payload + 22);
  inter_frq = be16u(payload + 26);
  loop = be32u(payload + 28);
  add_size = be16u(payload + 32);

  if (!(payload[19] & 1)) {
    /* Non-extended YM5/YM6 variant (YM5_Get_Registers/YM6_Get_Registers) -
     * not ported this milestone, see ym_file.h. */
    free(unpacked);
    return YM_FILE_ERR_UNSUPPORTED_TYPE;
  }

  f->data = unpacked;
  f->data_size = unpacked_size;
  f->number_of_vbls = (int32_t)num_of_tiks;
  f->loop_vbl = (int32_t)loop;
  if (f->loop_vbl < 0) f->loop_vbl = 0;

  /* Digidrum descriptor table (Players.pas:2817-2832): k starts right
   * after the 34-byte header plus Add_Size extra bytes, each descriptor
   * is a 4-byte BE length followed by that many raw sample bytes. */
  k = 34 + (int32_t)add_size;
  if (num_of_dig > 0) {
    f->digidrums = (ym_digidrum*)calloc(num_of_dig, sizeof(ym_digidrum));
    f->digidrum_count = num_of_dig;
    for (i = 0; i < (int)num_of_dig; i++) {
      int32_t len;
      if ((size_t)k + 4 > payload_size) {
        ym_file_free(f);
        return YM_FILE_ERR_TRUNCATED;
      }
      len = (int32_t)be32u(payload + k);
      f->digidrums[i].offset = k + 4;
      f->digidrums[i].length = len;
      k += 4 + len;
      if ((size_t)k > payload_size) {
        ym_file_free(f);
        return YM_FILE_ERR_TRUNCATED;
      }
    }
    /* Players.pas:2833-2852's digidrum sample-format conversion
     * (YMizeSample / linear-to-YM-amplitude-table lookup) is NOT ported -
     * see ym_file.h. Samples are used as raw bytes, matching the layout
     * for pre-converted/YM-native-encoded samples only. */
  }

  /* Title/Author/Comment: 3 null-terminated strings, in that order
   * (Players.pas:7791-7809); k ends up at VTX_Offset. */
  {
    char* const dests[3] = {f->title, f->author, f->comment};
    const size_t dest_caps[3] = {sizeof(f->title), sizeof(f->author),
                                  sizeof(f->comment)};
    int str;
    for (str = 0; str < 3; str++) {
      size_t n = 0;
      for (;;) {
        k++;
        if ((size_t)k > payload_size) {
          ym_file_free(f);
          return YM_FILE_ERR_TRUNCATED;
        }
        uint8_t ch = payload[k - 1];
        if (ch == 0) break;
        if (n + 1 < dest_caps[str]) dests[str][n++] = (char)ch;
      }
      dests[str][n] = '\0';
    }
  }
  f->vtx_offset = k;
  if ((size_t)f->vtx_offset + (size_t)f->number_of_vbls * 16 > payload_size) {
    ym_file_free(f);
    return YM_FILE_ERR_TRUNCATED;
  }

  /* MainWin.pas:1544-1548/1570/2070-2072's arithmetic cores (see
   * ym_file.h) - AY_Freq/Interrupt_Freq from the header, Delay_In_Tiks/
   * MFPTimerFrq/YM6TiksOnInt derived exactly as Set_Chip_Frq/
   * Set_Player_Frq/Set_MFP_Frq(0,0) would. */
  f->ay_freq = (double)chip_frq;
  f->interrupt_freq = (double)inter_frq * 1000.0;
  f->mfp_timer_frq = trunc(f->ay_freq * 16.0 / 13.0 + 0.5);
  f->ym6_tiks_on_int = f->ay_freq / (f->interrupt_freq / 1000.0 * 8.0);

  ay_engine_init(&f->ay);
  f->ay.chip_type = AY_CHIP_TYPE_YM; /* see ym_file.h's Chip_Type note */
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * f->ay_freq + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:4009-4025 (PrepareToPlay's shared FT.YM2/YM5/YM6 branch;
   * only FT.YM5 gets these two fixed defaults - for FT.YM6,
   * AtariSE1Type/AtariSE2Type are left at their Pascal-global zero
   * default, matching this struct's own memset above, and get properly
   * assigned by ym6i_get_registers's first real frame). */
  if (!f->is_ym6) {
    f->atari_se1_type = 0;
    f->atari_se2_type = 1;
  }
  f->position_in_vtx = 0;
  f->atari_se1_channel = 0;
  f->atari_se2_channel = 0;
  f->atari_timer_counter1 = 0;
  f->atari_timer_counter2 = 0;
  f->ym6_cur_tik = f->ym6_tiks_on_int;
  f->atari_v1 = 0;
  f->atari_v2 = 0;
  f->ym6_sinus_pos1 = 0;
  f->ym6_sinus_pos2 = 0;

  f->global_tick_counter = 0;
  f->global_tick_max = f->number_of_vbls;
  f->do_loop = false;
  f->real_end_all = false;

  ym6_sinus_table_init();

  return YM_FILE_OK;
}

void ym_file_free(ym_file* f) {
  free(f->data);
  f->data = NULL;
  free(f->digidrums);
  f->digidrums = NULL;
}

/* Players.pas:13653-13819 YM5i_Get_Registers. */
static void ym5i_get_registers(ym_file* f) {
  ay_chip* chip = &f->ay.chip;
  int32_t k = f->position_in_vtx + f->vtx_offset;
  const uint8_t* d = f->data;
  int32_t vbl = f->number_of_vbls;
  uint8_t b, la, lb, lc, mx;
  uint8_t dd, se1tc, se2tc;
  double frq;

  ay_chip_set_ay_register_fast(chip, 0, d[k]);

  k += vbl;
  b = d[k];
  ay_chip_set_ay_register_fast(chip, 1, (uint8_t)(b & 15));
  f->atari_se1_channel = (b & 0x30) >> 4;
  if (b & 0x40) f->atari_timer_counter1 = 0;

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 2, d[k]);

  k += vbl;
  dd = d[k];
  ay_chip_set_ay_register_fast(chip, 3, (uint8_t)(dd & 15));
  dd = (uint8_t)((dd & 0x30) >> 4);

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 4, d[k]);

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 5, (uint8_t)(d[k] & 15));

  k += vbl;
  b = d[k];
  ay_chip_set_ay_register_fast(chip, 6, (uint8_t)(b & 31));
  f->atari_se1_tp = b >> 5;

  k += vbl;
  mx = (uint8_t)(d[k] & 63);

  k += vbl;
  la = d[k];
  f->atari_se2_tp = la >> 5;

  k += vbl;
  lb = d[k];

  k += vbl;
  lc = d[k];

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 11, d[k]);

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 12, d[k]);

  k += vbl;
  b = d[k];
  if (b != 255) ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(b & 15));

  k += vbl;
  se1tc = d[k];

  k += vbl;
  se2tc = d[k];

  if (se1tc != 0 && f->atari_se1_tp != 0 && f->atari_se1_channel != 0) {
    switch (f->atari_se1_channel) {
      case 1:
        f->atari_param1 = la & 15;
        chip->envelope_en_a = true;
        break;
      case 2:
        f->atari_param1 = lb & 15;
        chip->envelope_en_b = true;
        break;
      case 3:
        f->atari_param1 = lc & 15;
        chip->envelope_en_c = true;
        break;
    }
    frq = 1.0 / (f->mfp_timer_frq / se1tc / (f->ay_freq / 8.0));
    switch (f->atari_se1_tp) {
      case 1: f->atari_timer_period1 = frq * 4; break;
      case 2: f->atari_timer_period1 = frq * 10; break;
      case 3: f->atari_timer_period1 = frq * 16; break;
      case 4: f->atari_timer_period1 = frq * 50; break;
      case 5: f->atari_timer_period1 = frq * 64; break;
      case 6: f->atari_timer_period1 = frq * 100; break;
      case 7: f->atari_timer_period1 = frq * 200; break;
    }
    if (f->atari_timer_counter1 >= f->atari_timer_period1)
      f->atari_timer_counter1 = 0;
  } else {
    f->atari_se1_channel = 0;
    f->atari_timer_counter1 = 0;
    f->atari_v1 = 0;
  }

  if (se2tc != 0 && f->atari_se2_tp != 0 && dd != 0) {
    switch (dd) {
      case 1:
        f->atari_param2 = la & 15;
        chip->envelope_en_a = true;
        break;
      case 2:
        f->atari_param2 = lb & 15;
        chip->envelope_en_b = true;
        break;
      case 3:
        f->atari_param2 = lc & 15;
        chip->envelope_en_c = true;
        break;
    }
    if (f->atari_param2 >= f->digidrum_count) dd = 0;
    f->atari_se2_channel = dd;
    f->atari_se2_pos = 0;
    frq = 1.0 / (f->mfp_timer_frq / se2tc / (f->ay_freq / 8.0));
    switch (f->atari_se2_tp) {
      case 1: f->atari_timer_period2 = frq * 4; break;
      case 2: f->atari_timer_period2 = frq * 10; break;
      case 3: f->atari_timer_period2 = frq * 16; break;
      case 4: f->atari_timer_period2 = frq * 50; break;
      case 5: f->atari_timer_period2 = frq * 64; break;
      case 6: f->atari_timer_period2 = frq * 100; break;
      case 7: f->atari_timer_period2 = frq * 200; break;
    }
    if (f->atari_timer_counter2 >= f->atari_timer_period2)
      f->atari_timer_counter2 = 0;
  } else {
    switch (f->atari_se2_channel) {
      case 0: f->atari_timer_counter2 = 0; break;
      case 1: if ((mx & 9) != 9) f->atari_se2_channel = 0; break;
      case 2: if ((mx & 18) != 18) f->atari_se2_channel = 0; break;
      case 3: if ((mx & 36) != 36) f->atari_se2_channel = 0; break;
    }
  }

  switch (f->atari_se2_channel) {
    case 1: mx |= 9; break;
    case 2: mx |= 18; break;
    case 3: mx |= 36; break;
  }
  ay_chip_set_ay_register_fast(chip, 7, mx);

  if (f->atari_se1_channel != 1 && f->atari_se2_channel != 1)
    ay_chip_set_ay_register_fast(chip, 8, (uint8_t)(la & 31));
  if (f->atari_se1_channel != 2 && f->atari_se2_channel != 2)
    ay_chip_set_ay_register_fast(chip, 9, (uint8_t)(lb & 31));
  if (f->atari_se1_channel != 3 && f->atari_se2_channel != 3)
    ay_chip_set_ay_register_fast(chip, 10, (uint8_t)(lc & 31));

  f->global_tick_counter++;
  f->position_in_vtx++;
  if (f->position_in_vtx == f->number_of_vbls)
    f->position_in_vtx = f->loop_vbl;
}

/* Players.pas:13121-13385 YM6i_Get_Registers. Structurally a sibling of
 * ym5i_get_registers (same 14 register-plane reads at the same relative
 * offsets), but NOT a small diff of it - real differences:
 *  - Register-1/3's top 2 bits (`b shr 6`) are the effect TYPE (SE1Typ/
 *    SE2Typ: 0=square, 1=digidrum, 2=sinus, 3=explicit envelope) here,
 *    not YM5i's single "reset timer counter" flag bit - there is no
 *    timer-counter-reset check in YM6i at all.
 *  - AtariSE1Type/AtariSE2Type are reassigned from SE1Typ/SE2Typ every
 *    frame (Players.pas:13238,13322) - YM6's actual "switch effect type
 *    mid-song" feature - instead of being fixed at load time.
 *  - Per-type setup dispatch: types 0/2 both just capture a 4-bit
 *    parameter and set Envelope_En{A,B,C}; type 3 captures the same
 *    4-bit parameter AND immediately calls SetAmplX(x and 16); the
 *    else/digidrum case (type 1) captures a 5-bit parameter and ALSO
 *    sets Envelope_En{A,B,C} (yes, even for digidrum - replicated
 *    literally, not "fixed").
 *  - The digidrum-index-bounds check happens here too (redundant with,
 *    but in addition to, the one already in ym6_extra_get_registers).
 *  - The mixer force-open logic is gated on type=1 per slot
 *    independently (two separate checks), unlike YM5i's unconditional
 *    per-channel force (YM5i hardcodes SE2=digidrum always). */
static void ym6i_get_registers(ym_file* f) {
  ay_chip* chip = &f->ay.chip;
  int32_t k = f->position_in_vtx + f->vtx_offset;
  const uint8_t* d = f->data;
  int32_t vbl = f->number_of_vbls;
  uint8_t b, mx, la, lb, lc;
  int se1ch, se2ch, se1typ, se2typ;
  uint8_t se1tc, se2tc;
  double frq;

  ay_chip_set_ay_register_fast(chip, 0, d[k]);

  k += vbl;
  b = d[k];
  ay_chip_set_ay_register_fast(chip, 1, (uint8_t)(b & 15));
  se1ch = (b & 0x30) >> 4;
  se1typ = b >> 6;

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 2, d[k]);

  k += vbl;
  b = d[k];
  ay_chip_set_ay_register_fast(chip, 3, (uint8_t)(b & 15));
  se2ch = (b & 0x30) >> 4;
  se2typ = b >> 6;

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 4, d[k]);

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 5, (uint8_t)(d[k] & 15));

  k += vbl;
  b = d[k];
  ay_chip_set_ay_register_fast(chip, 6, (uint8_t)(b & 31));
  f->atari_se1_tp = b >> 5;

  k += vbl;
  mx = (uint8_t)(d[k] & 63);

  k += vbl;
  la = d[k];
  f->atari_se2_tp = la >> 5;

  k += vbl;
  lb = d[k];

  k += vbl;
  lc = d[k];

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 11, d[k]);

  k += vbl;
  ay_chip_set_ay_register_fast(chip, 12, d[k]);

  k += vbl;
  b = d[k];
  if (b != 255) ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(b & 15));

  k += vbl;
  se1tc = d[k];

  k += vbl;
  se2tc = d[k];

  if (se1tc != 0 && f->atari_se1_tp != 0 && se1ch != 0) {
    switch (se1ch) {
      case 1:
        if (se1typ == 0 || se1typ == 2) {
          f->atari_param1 = la & 15;
          chip->envelope_en_a = true;
        } else if (se1typ == 3) {
          f->atari_param1 = la & 15;
          ay_chip_set_ay_register_fast(chip, 8, (uint8_t)(la & 16));
        } else {
          f->atari_param1 = la & 31;
          chip->envelope_en_a = true;
        }
        break;
      case 2:
        if (se1typ == 0 || se1typ == 2) {
          f->atari_param1 = lb & 15;
          chip->envelope_en_b = true;
        } else if (se1typ == 3) {
          f->atari_param1 = lb & 15;
          ay_chip_set_ay_register_fast(chip, 9, (uint8_t)(lb & 16));
        } else {
          f->atari_param1 = lb & 31;
          chip->envelope_en_b = true;
        }
        break;
      case 3:
        if (se1typ == 0 || se1typ == 2) {
          f->atari_param1 = lc & 15;
          chip->envelope_en_c = true;
        } else if (se1typ == 3) {
          f->atari_param1 = lc & 15;
          ay_chip_set_ay_register_fast(chip, 10, (uint8_t)(lc & 16));
        } else {
          f->atari_param1 = lc & 31;
          chip->envelope_en_c = true;
        }
        break;
    }
    if (se1typ == 1 && f->atari_param1 >= f->digidrum_count) se1ch = 0;
    f->atari_se1_type = se1typ;
    f->atari_se1_channel = se1ch;
    f->atari_se1_pos = 0;
    frq = 1.0 / (f->mfp_timer_frq / se1tc / (f->ay_freq / 8.0));
    switch (f->atari_se1_tp) {
      case 1: f->atari_timer_period1 = frq * 4; break;
      case 2: f->atari_timer_period1 = frq * 10; break;
      case 3: f->atari_timer_period1 = frq * 16; break;
      case 4: f->atari_timer_period1 = frq * 50; break;
      case 5: f->atari_timer_period1 = frq * 64; break;
      case 6: f->atari_timer_period1 = frq * 100; break;
      case 7: f->atari_timer_period1 = frq * 200; break;
    }
    if (f->atari_timer_counter1 >= f->atari_timer_period1)
      f->atari_timer_counter1 = 0;
  } else {
    if (f->atari_se1_channel != 0 && f->atari_se1_type == 1) {
      switch (f->atari_se1_channel) {
        case 1: if ((mx & 9) != 9) f->atari_se1_channel = 0; break;
        case 2: if ((mx & 18) != 18) f->atari_se1_channel = 0; break;
        case 3: if ((mx & 36) != 36) f->atari_se1_channel = 0; break;
      }
    } else {
      f->atari_se1_channel = 0;
      f->atari_timer_counter1 = 0;
      f->atari_v1 = 0;
    }
  }

  if (se2tc != 0 && f->atari_se2_tp != 0 && se2ch != 0) {
    switch (se2ch) {
      case 1:
        if (se2typ == 0 || se2typ == 2) {
          f->atari_param2 = la & 15;
          chip->envelope_en_a = true;
        } else if (se2typ == 3) {
          f->atari_param2 = la & 15;
          ay_chip_set_ay_register_fast(chip, 8, (uint8_t)(la & 16));
        } else {
          f->atari_param2 = la & 31;
          chip->envelope_en_a = true;
        }
        break;
      case 2:
        if (se2typ == 0 || se2typ == 2) {
          f->atari_param2 = lb & 15;
          chip->envelope_en_b = true;
        } else if (se2typ == 3) {
          f->atari_param2 = lb & 15;
          ay_chip_set_ay_register_fast(chip, 9, (uint8_t)(lb & 16));
        } else {
          f->atari_param2 = lb & 31;
          chip->envelope_en_b = true;
        }
        break;
      case 3:
        if (se2typ == 0 || se2typ == 2) {
          f->atari_param2 = lc & 15;
          chip->envelope_en_c = true;
        } else if (se2typ == 3) {
          f->atari_param2 = lc & 15;
          ay_chip_set_ay_register_fast(chip, 10, (uint8_t)(lc & 16));
        } else {
          f->atari_param2 = lc & 31;
          chip->envelope_en_c = true;
        }
        break;
    }
    if (se2typ == 1 && f->atari_param2 >= f->digidrum_count) se2ch = 0;
    f->atari_se2_type = se2typ;
    f->atari_se2_channel = se2ch;
    f->atari_se2_pos = 0;
    frq = 1.0 / (f->mfp_timer_frq / se2tc / (f->ay_freq / 8.0));
    switch (f->atari_se2_tp) {
      case 1: f->atari_timer_period2 = frq * 4; break;
      case 2: f->atari_timer_period2 = frq * 10; break;
      case 3: f->atari_timer_period2 = frq * 16; break;
      case 4: f->atari_timer_period2 = frq * 50; break;
      case 5: f->atari_timer_period2 = frq * 64; break;
      case 6: f->atari_timer_period2 = frq * 100; break;
      case 7: f->atari_timer_period2 = frq * 200; break;
    }
    if (f->atari_timer_counter2 >= f->atari_timer_period2)
      f->atari_timer_counter2 = 0;
  } else {
    if (f->atari_se2_channel != 0 && f->atari_se2_type == 1) {
      switch (f->atari_se2_channel) {
        case 1: if ((mx & 9) != 9) f->atari_se2_channel = 0; break;
        case 2: if ((mx & 18) != 18) f->atari_se2_channel = 0; break;
        case 3: if ((mx & 36) != 36) f->atari_se2_channel = 0; break;
      }
    } else {
      f->atari_se2_channel = 0;
      f->atari_timer_counter2 = 0;
      f->atari_v2 = 0;
    }
  }

  if (f->atari_se1_type == 1) {
    switch (f->atari_se1_channel) {
      case 1: mx |= 9; break;
      case 2: mx |= 18; break;
      case 3: mx |= 36; break;
    }
  }
  if (f->atari_se2_type == 1) {
    switch (f->atari_se2_channel) {
      case 1: mx |= 9; break;
      case 2: mx |= 18; break;
      case 3: mx |= 36; break;
    }
  }

  ay_chip_set_ay_register_fast(chip, 7, mx);

  if (f->atari_se1_channel != 1 && f->atari_se2_channel != 1)
    ay_chip_set_ay_register_fast(chip, 8, (uint8_t)(la & 31));
  if (f->atari_se1_channel != 2 && f->atari_se2_channel != 2)
    ay_chip_set_ay_register_fast(chip, 9, (uint8_t)(lb & 31));
  if (f->atari_se1_channel != 3 && f->atari_se2_channel != 3)
    ay_chip_set_ay_register_fast(chip, 10, (uint8_t)(lc & 31));

  f->global_tick_counter++;
  f->position_in_vtx++;
  if (f->position_in_vtx == f->number_of_vbls)
    f->position_in_vtx = f->loop_vbl;
}

/* Players.pas:12915-13004 YM6_Extra_GetRegisters. */
static void ym6_extra_get_registers(ym_file* f) {
  ay_chip* chip = &f->ay.chip;
  double t1, t2, t3;
  double ym6_tiks;

  t3 = f->ym6_tiks_on_int - f->ym6_cur_tik;
  t1 = t3;
  t2 = t3;

  if (f->atari_se1_channel != 0) {
    if (f->atari_timer_counter1 == 0) {
      /* Players.pas:12925-12955: cases 0/1/2 write RegisterAY.Index[...]
       * directly (bypassing SetAmplA/B/C's Envelope_EnA/B/C side effect) -
       * only case 3 goes through a real setter (SetEnvelopeRegister). */
      switch (f->atari_se1_type) {
        case 0:
          f->atari_v1 = (f->atari_v1 == 0) ? f->atari_param1 : 0;
          chip->reg[7 + f->atari_se1_channel] = (uint8_t)f->atari_v1;
          break;
        case 1:
          if (f->atari_param1 < f->digidrum_count) {
            ym_digidrum* dr = &f->digidrums[f->atari_param1];
            chip->reg[7 + f->atari_se1_channel] =
                f->data[dr->offset + f->atari_se1_pos];
            f->atari_se1_pos++;
            if (f->atari_se1_pos >= dr->length) f->atari_se1_channel = 0;
          } else {
            f->atari_se1_channel = 0;
          }
          break;
        case 2:
          chip->reg[7 + f->atari_se1_channel] =
              ym6_sinus_table[f->atari_param1][f->ym6_sinus_pos1];
          f->ym6_sinus_pos1 = (f->ym6_sinus_pos1 + 1) & 7;
          break;
        case 3:
          ay_chip_set_ay_register_fast(chip, 13, (uint8_t)f->atari_param1);
          break;
      }
    }
    t1 = f->atari_timer_period1 - f->atari_timer_counter1;
  }

  if (f->atari_se2_channel != 0) {
    if (f->atari_timer_counter2 == 0) {
      switch (f->atari_se2_type) {
        case 0:
          f->atari_v2 = (f->atari_v2 == 0) ? f->atari_param2 : 0;
          chip->reg[7 + f->atari_se2_channel] = (uint8_t)f->atari_v2;
          break;
        case 1:
          if (f->atari_param2 < f->digidrum_count) {
            ym_digidrum* dr = &f->digidrums[f->atari_param2];
            chip->reg[7 + f->atari_se2_channel] =
                (uint8_t)(f->data[dr->offset + f->atari_se2_pos] & 15);
            f->atari_se2_pos++;
            if (f->atari_se2_pos >= dr->length) f->atari_se2_channel = 0;
          } else {
            f->atari_se2_channel = 0;
          }
          break;
        case 2:
          chip->reg[7 + f->atari_se2_channel] =
              ym6_sinus_table[f->atari_param2][f->ym6_sinus_pos2];
          f->ym6_sinus_pos2 = (f->ym6_sinus_pos2 + 1) & 7;
          break;
        case 3:
          ay_chip_set_ay_register_fast(chip, 13, (uint8_t)f->atari_param2);
          break;
      }
    }
    t2 = f->atari_timer_period2 - f->atari_timer_counter2;
  }

  if (t2 < t1) t1 = t2;
  if (t3 < t1) t1 = t3;

  ym6_tiks = llround(t1 * 4294967296.0);

  if (f->atari_se1_channel != 0) {
    f->atari_timer_counter1 += t1;
    if (f->atari_timer_counter1 >= f->atari_timer_period1)
      f->atari_timer_counter1 = 0;
  }
  if (f->atari_se2_channel != 0) {
    f->atari_timer_counter2 += t1;
    if (f->atari_timer_counter2 >= f->atari_timer_period2)
      f->atari_timer_counter2 = 0;
  }
  f->ym6_cur_tik += t1;

  /* AY.pas:1117-1127 SynthesizerYM6, folded in here since ym6_tiks is a
   * purely local handoff in the original too (a unit-level global, but
   * only ever read by SynthesizerYM6 immediately after being written). */
  if (!f->ay.int_flag) {
    f->ay.number_of_tiks += (int64_t)ym6_tiks;
  } else {
    f->ay.int_flag = false;
  }
}

int ym_file_make_buffer(ym_file* f, int16_t* buf, int buffer_length) {
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

  if (f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop) {
      f->global_tick_counter = f->global_tick_max;
    } else {
      f->real_end_all = true;
      return ay->buf_len;
    }
  }

  while (!f->real_end_all && ay->buf_len < buffer_length) {
    if (f->ym6_cur_tik >= f->ym6_tiks_on_int) {
      f->ym6_cur_tik -= f->ym6_tiks_on_int;
      /* Players.pas:13006-13119 MakeBufferYM5/MakeBufferYM6 - identical
       * except for which *_Get_Registers variant they call. */
      if (f->is_ym6)
        ym6i_get_registers(f);
      else
        ym5i_get_registers(f);
    }
    ym6_extra_get_registers(f);
    if ((ay->number_of_tiks >> 32) != 0) {
      ay_synthesizer_stereo16(ay);
    }
    if (f->global_tick_counter >= f->global_tick_max && !ay->int_flag) {
      if (f->do_loop) {
        f->global_tick_counter = f->global_tick_max;
      } else {
        f->real_end_all = true;
        return ay->buf_len;
      }
    }
  }
  return ay->buf_len;
}
