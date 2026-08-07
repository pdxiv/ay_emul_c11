/* Adapter binding superzazu/z80 to ay_emul's memory/port model - see
 * engine/include/ay_engine/z80_bus.h for the ported contract and scope. */
#include "ay_engine/hw/z80_bus.h"

#include <string.h>

/* Z80.pas:31-32 (live noASM branch shares these constants with the dead
 * ASM branch - they're plain `const`, not conditionally compiled). */
#define PORT_MASK 0xc002u
#define CPC_PORT_MASK 11u

static uint8_t rb(void* userdata, uint16_t addr) {
  z80_bus* bus = (z80_bus*)userdata;
  return bus->ram[addr];
}

static void wb(void* userdata, uint16_t addr, uint8_t val) {
  z80_bus* bus = (z80_bus*)userdata;
  bus->ram[addr] = val;
}

/* superzazu/z80's port_in/port_out callbacks only pass the low 8 bits of
 * the port (z->c for the `IN r,(C)`/`OUT (C),r` family, or the immediate
 * operand byte for `IN A,(n)`/`OUT (n),A` - see z80.c:565,574,590,1200,1206,
 * 1635-1643). Z80.pas's own port decode needs the full 16-bit port (e.g.
 * PortMask=$c002 distinguishes $FFFD from $BFFD by a bit that lives in the
 * *high* byte - confirmed by direct computation: 0xFFFD & 0xc002 = 0xc000,
 * 0xBFFD & 0xc002 = 0x8000, differing in bit 14, which is part of B, not
 * C). superzazu/z80 exposes the full CPU register file to the callback
 * (the z80* itself is the first argument), so we reconstruct the missing
 * high byte from z->b (BC-indirect form) or z->a (immediate form) -
 * whichever the about-to-execute opcode actually uses. Which form applies
 * is pre-decoded once per z80_bus_step() into next_port_op_is_bc_form,
 * since the callback itself has no opcode-class parameter. */
static uint16_t reconstruct_port(const z80_bus* bus, uint8_t low_byte) {
  uint16_t high = bus->next_port_op_is_bc_form ? bus->cpu.b : bus->cpu.a;
  return (uint16_t)((high << 8) | low_byte);
}

/* Z80.pas:11001-11024 (live), CPCCheckPIO - "true if register value is set". */
static bool cpc_check_pio(z80_bus* bus) {
  switch (bus->cpc_switch) {
    case 0xC0: /* address */
      bus->ay_cur_reg = bus->cpc_data;
      return false;
    case 0x80: /* write */
      if (bus->ay_cur_reg < 14) {
        if (bus->on_ay_write != NULL) {
          bus->on_ay_write(bus->ay_write_userdata, bus->ay_cur_reg,
                            bus->cpc_data);
        }
        return true;
      }
      return false;
    default:
      return false;
  }
}

/* Z80.pas:11026-11040 (live), CPCOutProc. */
static void cpc_out(z80_bus* bus, uint16_t port, uint8_t dat) {
  switch ((port >> 8) & CPC_PORT_MASK) {
    case (0xF4 & CPC_PORT_MASK):
      bus->cpc_data = dat;
      cpc_check_pio(bus);
      break;
    case (0xF6 & CPC_PORT_MASK):
      bus->cpc_switch = (uint8_t)(dat & 0xC0);
      cpc_check_pio(bus);
      break;
    default:
      break;
  }
}

/* Z80.pas:10986-10990 (live), CPCInProc - marked //todo in the original
 * itself; always returns 255. */
static uint8_t cpc_in(z80_bus* bus, uint16_t port) {
  (void)bus;
  (void)port;
  return 255;
}

/* Z80.pas:10992-10998 (live), ZXInProc. */
static uint8_t zx_in(z80_bus* bus, uint16_t port) {
  if ((port & PORT_MASK) == (65533u & PORT_MASK)) {
    if (bus->ay_cur_reg < 14 && bus->on_ay_read != NULL) {
      return bus->on_ay_read(bus->ay_read_userdata, bus->ay_cur_reg);
    }
  }
  return 255;
}

/* Beeper-edge handling shared by ZXOutProc/InitialOutProc (Z80.pas:
 * 11044-11060, 11080-11096): on a port write with bit 0 clear, resolve the
 * new beeper level from bit 4 of the data byte and notify on change. */
static void handle_beeper_edge(z80_bus* bus, uint16_t port, uint8_t dat) {
  if ((port & 1) == 0) {
    int next = (dat & 16) != 0 ? bus->beeper_on_level : 0;
    if (next != bus->beeper) {
      bus->beeper = next;
      if (bus->on_beeper_change != NULL) {
        bus->on_beeper_change(bus->beeper_userdata, next);
      }
    }
  }
}

/* Z80.pas:11042-11076 (live), ZXOutProc. */
static void zx_out(z80_bus* bus, uint16_t port, uint8_t dat) {
  handle_beeper_edge(bus, port, dat);
  if ((port & PORT_MASK) == (65533u & PORT_MASK)) {
    bus->ay_cur_reg = dat;
  } else if ((port & PORT_MASK) == (49149u & PORT_MASK)) {
    if (bus->ay_cur_reg < 14 && bus->on_ay_write != NULL) {
      bus->on_ay_write(bus->ay_write_userdata, bus->ay_cur_reg, dat);
    }
  }
}

/* Z80.pas:11078-11139 (live), InitialOutProc - auto-detects ZX vs CPC AY
 * port protocol from the first recognized write, then locks in that
 * machine's decoder for the rest of playback. */
static void initial_out(z80_bus* bus, uint16_t port, uint8_t dat) {
  handle_beeper_edge(bus, port, dat);
  switch ((port >> 8) & CPC_PORT_MASK) {
    case (0xF4 & CPC_PORT_MASK):
      bus->cpc_data = dat;
      cpc_check_pio(bus);
      return;
    case (0xF6 & CPC_PORT_MASK): {
      bus->cpc_switch = (uint8_t)(dat & 0xC0);
      if (cpc_check_pio(bus)) {
        bus->machine = Z80_BUS_MACHINE_CPC;
        if (bus->ay_file_enable_auto_switch && bus->on_chip_freq_change) {
          bus->on_chip_freq_change(bus->chip_freq_userdata, 1000000UL);
        }
        bus->beeper = 0;
      }
      return;
    }
    default:
      break;
  }
  if ((port & PORT_MASK) == (65533u & PORT_MASK)) {
    bus->machine = Z80_BUS_MACHINE_ZX;
    bus->ay_cur_reg = dat;
  } else if ((port & PORT_MASK) == (49149u & PORT_MASK)) {
    if (bus->ay_cur_reg < 14) {
      bus->machine = Z80_BUS_MACHINE_ZX;
      if (bus->on_ay_write != NULL) {
        bus->on_ay_write(bus->ay_write_userdata, bus->ay_cur_reg, dat);
      }
    }
  }
}

/* Z80.pas:11283-11289 (live), InitialInProc. */
static uint8_t initial_in(z80_bus* bus, uint16_t port) {
  if ((port & PORT_MASK) == (65533u & PORT_MASK)) {
    if (bus->ay_cur_reg < 14 && bus->on_ay_read != NULL) {
      return bus->on_ay_read(bus->ay_read_userdata, bus->ay_cur_reg);
    }
  }
  return 255;
}

static uint8_t port_in_trampoline(z80* const z, uint8_t port_low) {
  z80_bus* bus = (z80_bus*)z->userdata;
  uint16_t port = reconstruct_port(bus, port_low);
  switch (bus->machine) {
    case Z80_BUS_MACHINE_ZX: return zx_in(bus, port);
    case Z80_BUS_MACHINE_CPC: return cpc_in(bus, port);
    default: return initial_in(bus, port);
  }
}

static void port_out_trampoline(z80* const z, uint8_t port_low, uint8_t val) {
  z80_bus* bus = (z80_bus*)z->userdata;
  uint16_t port = reconstruct_port(bus, port_low);
  switch (bus->machine) {
    case Z80_BUS_MACHINE_ZX: zx_out(bus, port, val); break;
    case Z80_BUS_MACHINE_CPC: cpc_out(bus, port, val); break;
    default: initial_out(bus, port, val); break;
  }
}

/* Opcodes whose port operand comes from BC (ED-prefixed IN r,(C)/OUT (C),r)
 * vs. an immediate operand plus A (0xDB/0xD3) - see reconstruct_port(). */
static bool is_bc_form_ed_opcode(uint8_t op2) {
  switch (op2) {
    case 0x40: case 0x48: case 0x50: case 0x58:
    case 0x60: case 0x68: case 0x70: case 0x78: /* IN r,(C) */
    case 0x41: case 0x49: case 0x51: case 0x59:
    case 0x61: case 0x69: case 0x71: case 0x79: /* OUT (C),r */
      return true;
    default:
      return false;
  }
}

static void predecode_next_port_op(z80_bus* bus) {
  uint8_t op1 = bus->ram[bus->cpu.pc];
  if (op1 == 0xED) {
    uint8_t op2 = bus->ram[(uint16_t)(bus->cpu.pc + 1)];
    bus->next_port_op_is_bc_form = is_bc_form_ed_opcode(op2);
  } else {
    /* 0xDB (IN A,(n)) / 0xD3 (OUT (n),A) and everything else default to the
     * immediate form; harmless for non-port opcodes since the flag is only
     * consulted from within port_in/port_out, which won't fire this step
     * unless the opcode actually is a port instruction. */
    bus->next_port_op_is_bc_form = false;
  }
}

void z80_bus_init(z80_bus* bus, int64_t max_tstates) {
  memset(bus, 0, sizeof(*bus));
  z80_init(&bus->cpu);
  bus->cpu.read_byte = rb;
  bus->cpu.write_byte = wb;
  bus->cpu.port_in = port_in_trampoline;
  bus->cpu.port_out = port_out_trampoline;
  bus->cpu.userdata = bus;
  bus->machine = Z80_BUS_MACHINE_INITIAL;
  bus->max_tstates = max_tstates;
  bus->int_length = 32; /* Z80.pas:28, IntLength */
}

void z80_bus_step(z80_bus* bus) {
  int64_t before, delta;

  predecode_next_port_op(bus);

  before = (int64_t)bus->cpu.cyc;
  z80_step(&bus->cpu);
  delta = (int64_t)bus->cpu.cyc - before;
  bus->current_tact += delta;

  if (bus->int_pending_this_frame) {
    if (!bus->cpu.int_pending) {
      /* Accepted this step (superzazu/z80 cleared it in process_interrupts).
       * MIG-0018: Z80.pas's own live Z80_Step (the noASM branch past its
       * `{$else Z80Emu_noASM}`) charges 12 T-states for an IM1 interrupt
       * accept and 18 for IM2 - superzazu/z80 charges the standard 13/19
       * (z80.c's `z->cyc += 13`/`+= 19`, matching Zilog's own documented
       * timing and every other reputable Z80 core). Per explicit user
       * direction: superzazu/z80 is the correct reference here, not
       * Z80.pas - this is a genuine bug/quirk in the ORIGINAL Pascal
       * implementation, not something this port should replicate. Left
       * uncorrected deliberately (see migration_debt.yaml MIG-0018) -
       * this is the one place in the whole port where the original's
       * behavior is knowingly NOT what the C11 side matches. */
      bus->int_pending_this_frame = false;
    } else if (bus->current_tact >= bus->int_length) {
      /* Z80.pas only accepts the interrupt while CurrentTact < IntLength
       * right after the frame wraps (Z80.pas:21027) - the window has now
       * closed for this frame, so withdraw the request rather than let
       * superzazu/z80 fire it arbitrarily late once IFF1 next becomes
       * true (which would drift from the original's one-shot-per-frame
       * pulse model). */
      bus->cpu.int_pending = false;
      bus->int_pending_this_frame = false;
    }
  }

  if (bus->current_tact >= bus->max_tstates) {
    bus->current_tact -= bus->max_tstates;
    z80_gen_int(&bus->cpu, 0xFF); /* data byte unused in IM1/IM2 - MIG-0001 */
    bus->int_pending_this_frame = true;
  }
}
