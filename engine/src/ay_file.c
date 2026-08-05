#include "ay_engine/ay_file.h"

#include <string.h>

#include "ay_engine/trace_log.h"

/* All multi-byte header fields in a real .ay file are big-endian (see
 * Players.pas's pervasive SwapEndian() calls on header fields - the
 * packed records are read raw off disk into little-endian Pascal
 * variables, then explicitly byte-swapped before use). */
static uint16_t be16u(const uint8_t* p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
static int16_t be16s(const uint8_t* p) {
  return (int16_t)be16u(p);
}

static const uint8_t DUMP_IM1[13] = {0xF3, 0xCD, 0, 0,   0xED, 0x56, 0xFB,
                                      0x76, 0xCD, 0, 0,   0x18, 0xF7};
static const uint8_t DUMP_IM2[10] = {0xF3, 0xCD, 0,    0,    0xED,
                                      0x5E, 0xFB, 0x76, 0x18, 0xFA};

/* MainWin.pas:1544-1546 - the pure-arithmetic core of Set_Chip_Frq, without
 * the GUI/filter/label side effects (see ay_file.h's file comment). Also
 * resets Tik.Re := Delay_In_Tiks (MainWin.pas:1546) - required whenever
 * this is called mid-song (the on_chip_freq_change path below), not just
 * at load time, otherwise tik_re keeps accumulating against the OLD
 * delay_in_tiks and desyncs the resampler the instant the AY clock
 * changes. */
static void ay_file_set_chip_freq(ay_file* f, int ay_freq, int sample_rate) {
  f->ay.delay_in_tiks = (uint32_t)(8192.0 / sample_rate * ay_freq + 0.5);
  f->ay.frq_ay_by_frq_z80 =
      (int64_t)((double)ay_freq / f->frq_z80 / 8.0 * 4294967296.0 + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
}

static void on_chip_freq_change(void* ud, unsigned long hz) {
  ay_file* f = (ay_file*)ud;
  /* Z80.pas:11112 / PlayList.pas:813-814: only the AY chip clock changes
   * here (Set_Chip_Frq(1000000) on CPC-protocol auto-detect); the Z80
   * clock (frq_z80) is untouched. */
  ay_file_set_chip_freq(f, (int)hz, f->sample_rate);
}

static void on_ay_write(void* ud, uint8_t reg, uint8_t data) {
  ay_file* f = (ay_file*)ud;
  /* Z80.pas's InitialOutProc/ZXOutProc/CPCOutProc: "if not Flg then
   * SynthesizerAY" then SetAYRegister - the Flg (double-flush-within-one-
   * OUT) bookkeeping is z80_bus.c's own internal concern (it only invokes
   * this callback once per logical register write); this callback's job
   * is just the single flush-then-apply the contract in z80_bus.h
   * documents. */
  ay_synthesizer_ay(&f->ay, f->bus.current_tact);
  trace_log_ay(f->bus.current_tact, "write_apply", reg, data);
  ay_chip_set_ay_register(&f->ay.chip, reg, data);
}

/* Z80.pas's ZXInProc/InitialInProc/CPCInProc (live): `Dat := SoundChip[0].
 * RegisterAY.Index[AY_CurReg]` when AY_CurReg < 14 - a plain read of the
 * last value SetAYRegister stored, no synthesizer flush needed (unlike
 * on_ay_write, reading back doesn't change chip state). z80_bus.c's
 * zx_in/cpc_in already gate this call on ay_cur_reg < 14 themselves, so
 * this callback can read c->reg[reg] unconditionally. */
static uint8_t on_ay_read(void* ud, uint8_t reg) {
  ay_file* f = (ay_file*)ud;
  return f->ay.chip.reg[reg];
}

static void on_beeper_change(void* ud, int level) {
  ay_file* f = (ay_file*)ud;
  ay_synthesizer_ay(&f->ay, f->bus.current_tact);
  trace_log_ay(f->bus.current_tact, "beeper_toggle", -1, level);
  f->ay.beeper = level;
}

/* Players.pas:2915-2949 + 7132-7236 (OpenAYFile's relative-pointer walk,
 * specialized to the FT.AY/TypeID=EMUL case only). Returns the absolute
 * file offset of the song's TSongData block, or (size_t)-1 with *status
 * set on error. */
static size_t locate_song_data(const uint8_t* data, size_t size,
                                int song_index, ay_file_status* status) {
  int64_t songs_structure_ptr, i;
  uint16_t w;

  if (size < 20) {
    *status = AY_FILE_ERR_TRUNCATED;
    return (size_t)-1;
  }
  if (memcmp(data, "ZXAY", 4) != 0) {
    *status = AY_FILE_ERR_BAD_HEADER;
    return (size_t)-1;
  }
  if (memcmp(data + 4, "EMUL", 4) != 0) {
    *status = AY_FILE_ERR_UNSUPPORTED_TYPE;
    return (size_t)-1;
  }

  /* offset 16: NumOfSongs (byte), 17: FirstSong (byte) - see Players.pas:
   * 2919-2928's byte(w) use, which extracts only the raw NumOfSongs byte
   * despite reading it via a 2-byte word. */
  if (song_index < 0 || song_index > data[16]) {
    *status = AY_FILE_ERR_BAD_SONG_INDEX;
    return (size_t)-1;
  }

  songs_structure_ptr = be16s(data + 18); /* offset 18-19: PSongsStructure */
  /* Players.pas:2930: i := PSongsStructure + 18 + FormatSpec*4 + 2 - lands
   * exactly on the song's TSongStructure.PSongData field. */
  i = songs_structure_ptr + 18 + (int64_t)song_index * 4 + 2;
  if (i < 0 || (size_t)i + 2 > size) {
    *status = AY_FILE_ERR_TRUNCATED;
    return (size_t)-1;
  }
  w = be16u(data + i);
  i += (int16_t)w; /* pointer relative to its own field position */
  if (i < 0 || (size_t)i + 14 > size) {
    *status = AY_FILE_ERR_TRUNCATED;
    return (size_t)-1;
  }

  *status = AY_FILE_OK;
  return (size_t)i;
}

ay_file_status ay_file_load(ay_file* f, const uint8_t* data, size_t size,
                             int song_index, int ay_freq, int frq_z80,
                             int sample_rate) {
  ay_file_status status;
  size_t song_data_pos, fpos, points_pos;
  uint16_t song_length_raw;
  int16_t p_points_raw, p_addresses_raw;
  uint8_t hi_reg, lo_reg;
  uint16_t stek, init, inter;
  int64_t ay_blocks;

  song_data_pos = locate_song_data(data, size, song_index, &status);
  if (status != AY_FILE_OK) return status;

  /* TSongData (14 bytes): ChanA,ChanB,ChanC,Noise (unused - see
   * Players.pas:3926-4008, never read there), SongLength, FadeLength
   * (unused - see ay_file.h), HiReg, LoReg, PPoints, PAddresses. */
  song_length_raw = be16u(data + song_data_pos + 4);
  hi_reg = data[song_data_pos + 8];
  lo_reg = data[song_data_pos + 9];
  p_points_raw = be16s(data + song_data_pos + 10);
  p_addresses_raw = be16s(data + song_data_pos + 12);
  fpos = song_data_pos + 14;

  /* Players.pas:2938-2939/2944: pointers relative to their own field's
   * position (fpos-4 for PPoints, fpos-2 for PAddresses). */
  points_pos = fpos - 4 + (size_t)(int64_t)p_points_raw;
  if ((int64_t)points_pos < 0 || points_pos + 6 > size)
    return AY_FILE_ERR_TRUNCATED;
  stek = be16u(data + points_pos);
  init = be16u(data + points_pos + 2);
  inter = be16u(data + points_pos + 4);

  ay_blocks = (int64_t)fpos - 2 + p_addresses_raw;
  if (ay_blocks < 0 || (size_t)ay_blocks > size) return AY_FILE_ERR_TRUNCATED;

  z80_bus_init(&f->bus, AY_FILE_MAX_TSTATES_DEF);
  ay_engine_init(&f->ay);

  f->frq_z80 = frq_z80;
  f->sample_rate = sample_rate;
  ay_file_set_chip_freq(f, ay_freq, sample_rate);

  f->bus.machine = Z80_BUS_MACHINE_INITIAL;
  f->bus.ay_file_enable_auto_switch = true; /* PlayList.pas:811-814 */
  f->bus.on_ay_write = on_ay_write;
  f->bus.ay_write_userdata = f;
  f->bus.on_ay_read = on_ay_read;
  f->bus.ay_read_userdata = f;
  f->bus.on_beeper_change = on_beeper_change;
  f->bus.beeper_userdata = f;
  f->bus.on_chip_freq_change = on_chip_freq_change;
  f->bus.chip_freq_userdata = f;

  ay_engine_calculate_level_tables(&f->ay);
  /* AY.pas: ZXOutProc/InitialOutProc read the global BeeperLevel directly;
   * z80_bus.h's beeper_on_level is the C11 bridge for that same value -
   * this caller-supply step was missing, leaving beeper_on_level at its
   * zero-init default and silently dropping the beeper/digidrum channel
   * entirely (MIG-0057). Was left disabled pending MIG-0061 (T21.ay's
   * spurious beeper-port toggle, root-caused to on_ay_read never being
   * wired up - IN A,(C) always returned 0xFF instead of the real
   * register value) - now fixed, safe to enable. */
  f->bus.beeper_on_level = f->ay.beeper_level;
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3944-3948. */
  memset(f->bus.ram + 10, 0xC9, 255 - 10 + 1);
  memset(f->bus.ram + 256, 0xFF, 16383 - 256 + 1);
  memset(f->bus.ram + 16384, 0x00, 65535 - 16384 + 1);
  f->bus.ram[56] = 0xFB;

  if (inter != 0) {
    memcpy(f->bus.ram, DUMP_IM1, sizeof(DUMP_IM1));
    f->bus.ram[9] = (uint8_t)(inter & 0xFF);
    f->bus.ram[10] = (uint8_t)(inter >> 8);
  } else {
    memcpy(f->bus.ram, DUMP_IM2, sizeof(DUMP_IM2));
  }
  f->bus.ram[2] = (uint8_t)(init & 0xFF);
  f->bus.ram[3] = (uint8_t)(init >> 8);

  /* superzazu/z80 keeps the main register set's flags as discrete
   * bitfields (sf/zf/yf/hf/xf/pf/nf/cf) rather than a packed F byte, and
   * ix/iy as plain 16-bit words rather than separate hi/lo bytes - decode
   * LoReg (-> F) / recombine HiReg:LoReg (-> IX/IY) accordingly. The
   * alternate set keeps a raw f_ byte (never decoded into bits), matching
   * the original's AFAlt.LoByte being a plain byte field too. */
  f->bus.cpu.sp = stek;
  f->bus.cpu.a = hi_reg;
  f->bus.cpu.sf = (lo_reg & 0x80) != 0;
  f->bus.cpu.zf = (lo_reg & 0x40) != 0;
  f->bus.cpu.yf = (lo_reg & 0x20) != 0;
  f->bus.cpu.hf = (lo_reg & 0x10) != 0;
  f->bus.cpu.xf = (lo_reg & 0x08) != 0;
  f->bus.cpu.pf = (lo_reg & 0x04) != 0;
  f->bus.cpu.nf = (lo_reg & 0x02) != 0;
  f->bus.cpu.cf = (lo_reg & 0x01) != 0;
  f->bus.cpu.a_ = hi_reg;
  f->bus.cpu.f_ = lo_reg;
  f->bus.cpu.h = hi_reg;
  f->bus.cpu.l = lo_reg;
  f->bus.cpu.d = hi_reg;
  f->bus.cpu.e = lo_reg;
  f->bus.cpu.b = hi_reg;
  f->bus.cpu.c = lo_reg;
  f->bus.cpu.h_ = hi_reg;
  f->bus.cpu.l_ = lo_reg;
  f->bus.cpu.d_ = hi_reg;
  f->bus.cpu.e_ = lo_reg;
  f->bus.cpu.b_ = hi_reg;
  f->bus.cpu.c_ = lo_reg;
  f->bus.cpu.ix = (uint16_t)((hi_reg << 8) | lo_reg);
  f->bus.cpu.iy = (uint16_t)((hi_reg << 8) | lo_reg);
  f->bus.cpu.i = 0x03;
  f->bus.cpu.r = 0x00;
  f->bus.cpu.pc = 0;
  f->bus.cpu.iff1 = 0;
  f->bus.cpu.iff2 = 0;
  f->bus.cpu.interrupt_mode = 0;

  /* Players.pas:3984-4007: (dest_addr,length,rel_offset) triples,
   * dest_addr==0 terminates. */
  {
    size_t pos = (size_t)ay_blocks;
    uint16_t dest_addr;

    if (pos + 2 > size) return AY_FILE_ERR_TRUNCATED;
    dest_addr = be16u(data + pos);
    pos += 2;
    while (dest_addr != 0) {
      uint16_t block_len;
      int16_t rel_off;
      int64_t src_pos;
      size_t after_triple;

      if (init == 0) {
        f->bus.ram[2] = (uint8_t)(dest_addr & 0xFF);
        f->bus.ram[3] = (uint8_t)(dest_addr >> 8);
        init = dest_addr;
      }

      if (pos + 4 > size) return AY_FILE_ERR_TRUNCATED;
      block_len = be16u(data + pos);
      pos += 2;
      if ((uint32_t)block_len + dest_addr > 65536)
        block_len = (uint16_t)(65536 - dest_addr);
      rel_off = be16s(data + pos);
      pos += 2;
      /* rel_offset relative to its own field position (now `pos`, having
       * just been read). */
      src_pos = (int64_t)pos + rel_off - 2;
      if (src_pos < 0) return AY_FILE_ERR_TRUNCATED;
      if ((uint64_t)src_pos + block_len > size)
        block_len = (uint16_t)(size - (size_t)src_pos);
      after_triple = pos;

      if ((size_t)dest_addr + block_len > 0x10000) return AY_FILE_ERR_TRUNCATED;
      memcpy(f->bus.ram + dest_addr, data + src_pos, block_len);

      pos = after_triple;
      if (pos + 2 > size) return AY_FILE_ERR_TRUNCATED;
      dest_addr = be16u(data + pos);
      pos += 2;
    }
  }

  f->global_tick_counter = 0;
  f->global_tick_max = song_length_raw != 0 ? song_length_raw : 15000;
  f->do_loop = false;
  f->real_end_all = false;

  return AY_FILE_OK;
}

int ay_file_make_buffer(ay_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->chip_type = AY_CHIP_TYPE_AY;
  ay->number_of_channels = 2;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay_synthesizer_ay(ay, f->bus.current_tact);
    if (ay->int_flag) return ay->buf_len;
    if (ay->int_beeper) {
      ay->int_beeper = false;
      ay->beeper = ay->beeper_next;
    }
    if (ay->int_ay) {
      ay->int_ay = false;
      ay_chip_set_ay_register(&ay->chip, ay->reg_num_next, ay->dat_next);
    }
  }

  while (ay->buf_len < buffer_length) {
    int64_t tact_before = f->bus.current_tact;
    z80_bus_step(&f->bus);
    if (f->bus.current_tact < tact_before) {
      /* Frame rollover - Players.pas:14012-14026. z80_bus_step already
       * decremented current_tact by max_tstates (see z80_bus.c); mirror
       * MakeBufferAY's matching Previous_Tact decrement before flushing. */
      ay->previous_tact -= f->bus.max_tstates;
      if (ay->buf_len < buffer_length) {
        ay_synthesizer_ay(ay, f->bus.current_tact);
      }
      f->global_tick_counter++;
      if (f->global_tick_counter >= f->global_tick_max) {
        if (f->do_loop) {
          f->global_tick_counter = f->global_tick_max;
        } else {
          f->real_end_all = true;
          return ay->buf_len;
        }
      }
    }
  }
  return ay->buf_len;
}
