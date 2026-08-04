# Motorola MC68901 Multi-Function Peripheral (MFP)

Active-low signals are written with a leading slash, e.g. `/CS`, `/DS`,
`/IRQ`.

## Contents

- [1. Overview](#1-overview)
- [2. Signal Description](#2-signal-description)
  - [2.1 Signal Summary Table](#21-signal-summary-table)
  - [2.2 Bus Operation](#22-bus-operation)
- [3. General-Purpose I/O Port](#3-general-purpose-io-port)
- [4. Interrupt Controller](#4-interrupt-controller)
- [5. Timers](#5-timers)
- [6. USART](#6-usart)
- [7. Register Map](#7-register-map)
- [8. Reset Behavior](#8-reset-behavior)
- [9. Electrical Characteristics](#9-electrical-characteristics)
- [10. Mechanical Data and Ordering Information](#10-mechanical-data-and-ordering-information)

## 1. Overview

The MC68901 Multi-Function Peripheral (MFP) is an NMOS large-scale
integration peripheral device introduced by Motorola for the M68000
family of microprocessors. It interfaces directly to the MC68000's
asynchronous bus, including the vectored interrupt-acknowledge cycle, and
integrates four functional blocks commonly required in an M68000-based
system into a single 48-pin package:

- An 8-bit parallel general-purpose I/O port (GPIP), individually
  programmable pin-by-pin, with per-pin interrupt capability.
- A 16-source interrupt controller with individual source enabling,
  masking, unique vector generation per source, and both polled and
  vectored operation. Handshake lines (`/IEI`/`/IEO`) allow multiple MFPs
  to be daisy-chained at a single CPU interrupt priority level.
- Four 8-bit timers (A, B, C, D), each with an independent programmable
  prescaler; Timers A and B are full-function and can additionally
  operate as event counters or in a pulse-width measurement mode, while
  Timers C and D support delay (free-running) mode only. Any timer's
  output can serve as a baud-rate clock for the on-chip serial channel.
- A full-duplex USART supporting asynchronous (start/stop) operation and,
  with its built-in polynomial generator/checker, byte-synchronous
  operation, with a wide range of programmable character formats.
  Receiver-ready/transmitter-ready (`/RR`/`/TR`) handshake lines allow the
  serial channel to be driven by a DMA controller instead of by
  interrupts.

The device occupies 24 addressable byte-wide registers in the host CPU's
address space, all directly addressable, which simplifies programming.

## 2. Signal Description

| Pin(s) | Direction | Function |
|---|---|---|
| D0–D7 | I/O | Data bus. Bidirectional; carries register data during normal read/write cycles and the interrupt vector number during an `/IACK` cycle. Because the vector number must appear in the low byte of the 16-bit data word, D0–D7 must be wired to the CPU's low-order eight data lines if vectored interrupts are used, which places the MFP's registers at odd addresses. |
| R/W | Input | Read/write select: high selects a read, low selects a write. |
| RS1–RS5 | Input | Register select; addresses the 24 internal registers. `RS1` is conventionally wired to CPU address line `A1`, `RS2` to `A2`, and so on, so registers land on odd byte addresses on a 16-bit bus. |
| /CS | Input | Chip select; activates the MFP for internal register access. |
| /DS | Input | Data strobe; qualifies a bus cycle and, together with `/CS`, gates register access and interrupt-acknowledge decoding. Must be connected to the CPU's lower data strobe (`LDS` on the MC68000, or `/DS` on the MC68008) if vectored interrupts are used. Data is latched on the rising edge for writes. |
| /DTACK | Output | Data transfer acknowledge; asserted by the MFP to terminate an asynchronous bus cycle once a read is valid or a write has been accepted. Driven only when the MFP has recognized `/CS`, or `/IACK` together with `/IEI`. |
| /IACK | Input | Interrupt acknowledge; asserted by the CPU, together with `/DS`, during an interrupt-acknowledge cycle addressed to the MFP's priority level. |
| /IEI | Input | Interrupt enable in. Indicates that no higher-priority device in a daisy chain is requesting service; the highest-priority device in a chain ties `/IEI` low. During `/IACK`, a device may not drive a vector onto the bus until `/IEI` is asserted. Tie low on all MFPs when daisy-chaining is not used. |
| /IEO | Output | Interrupt enable out. Signals lower-priority devices that neither this device nor any higher-priority device is requesting service; connects to the next device's `/IEI`. Left unconnected on the lowest-priority device (or on all devices if daisy-chaining is unused). |
| /IRQ | Output | Interrupt request, open-drain, to the CPU. Negated when all interrupt pending bits are clear, when all mask bits are clear, or as a result of an `/IACK` cycle (unless other interrupts remain pending). |
| /RESET | Input | Hardware reset; must be held low for a minimum of 2 µs. |
| CLK | Input | Single-phase, TTL-compatible system clock used for internal bus timing; must not be gated off, and need not match the host bus clock in frequency or phase. |
| XTAL1/XTAL2 | Input | Timer clock crystal or external oscillator input, independent of CLK, driving the four timer prescalers and (indirectly, via a timer output) the baud-rate generator. A crystal may be connected across XTAL1/XTAL2, or XTAL1 driven directly with a TTL-level clock with XTAL2 left unconnected. |
| TAI | Input | Timer A auxiliary input, used in event-count and pulse-width measurement modes. |
| TBI | Input | Timer B auxiliary input, analogous to TAI. |
| TAO | Output | Timer A output (toggles on time-out; forced low by `/RESET`, and separately clearable by the Reset bit in TACR). |
| TBO | Output | Timer B output, analogous to TAO, clearable via TBCR. |
| TCO | Output | Timer C output. |
| TDO | Output | Timer D output. |
| SI | Input | Serial input (USART receiver data); not used in loopback mode. |
| SO | Output | Serial output (USART transmitter data); driven high during reset. |
| RC | Input | Receiver clock; strobes the bit rate of the receiver. May come from a timer output or an external TTL clock. Not used in loopback mode. |
| TC | Input | Transmitter clock; strobes the bit rate of the transmitter. May come from a timer output or an external TTL clock. |
| /RR | Output | Receiver ready; reflects receive-buffer-full status for DMA transfers. |
| /TR | Output | Transmitter ready; reflects transmit-buffer-empty status for DMA transfers. |
| GPIP0–GPIP7 (I0–I7) | I/O | 8-bit general-purpose I/O port, individually direction- and edge-programmable, each bit capable of generating an interrupt on a selected transition (see §3). |
| VCC, GND | Power | +5 V supply and ground. |

Two of the general-purpose interrupt channels are shared with the timer
auxiliary inputs: when Timer A or B is placed in pulse-width-measurement
or event-count mode, the interrupt channel ordinarily associated with
GPIP4 (channel 6) responds instead to transitions on TAI, and the channel
ordinarily associated with GPIP3 (channel 3) responds instead to
transitions on TBI. GPIP4 and GPIP3 remain usable as plain I/O pins in
this situation — they simply lose their own interrupt-generating
capability for as long as the corresponding timer occupies that channel.
(TAI/TBI are separate physical pins from GPIP4/GPIP3; only the interrupt
*channel* is shared, not the pin.) GPIP7, GPIP6, and GPIP5 are
general-purpose only, with no auxiliary peripheral function.

### 2.1 Signal Summary Table

| Signal | Mnemonic | Direction | Active level |
|---|---|---|---|
| Power input | VCC | Input | High |
| Ground | GND | Input | Low |
| Clock | CLK | Input | N/A |
| Chip select | /CS | Input | Low |
| Data strobe | /DS | Input | Low |
| Read/write | R/W | Input | Read: high; write: low |
| Data transfer acknowledge | /DTACK | Output | Low |
| Register select bus | RS1–RS5 | Input | N/A |
| Data bus | D0–D7 | I/O | N/A |
| Reset | /RESET | Input | Low |
| Interrupt request | /IRQ | Output | Low |
| Interrupt acknowledge | /IACK | Input | Low |
| Interrupt enable in | /IEI | Input | Low |
| Interrupt enable out | /IEO | Output | Low |
| General-purpose I/O/interrupt lines | I0–I7 (GPIP0–GPIP7) | I/O | N/A |
| Timer clock | XTAL1, XTAL2 | Input | High |
| Timer inputs | TAI, TBI | Input | N/A |
| Timer outputs | TAO, TBO, TCO, TDO | Output | N/A |
| Serial input | SI | Input | N/A |
| Serial output | SO | Output | N/A |
| Receiver clock | RC | Input | N/A |
| Transmitter clock | TC | Input | N/A |
| Receiver ready | /RR | Output | Low |
| Transmitter ready | /TR | Output | Low |

### 2.2 Bus Operation

The register-select bus (RS1–RS5) and data bus (D0–D7) are separate
parallel buses used with an asynchronous handshake. The bus master is
responsible for deskewing the signals it drives at both the start and end
of a cycle, and for deskewing the acknowledge and data signals returned
by the MFP.

**Read cycle.** With `/CS` and `/DS` asserted and R/W high, the MFP
drives the addressed register's contents onto D0–D7 and asserts
`/DTACK`. After the processor has latched the data it negates `/DS`;
negation of either `/CS` or `/DS` terminates the cycle, and the MFP
returns `/DTACK` and the data bus to the high-impedance state.

**Write cycle.** With `/CS` and `/DS` asserted and R/W low, the MFP
decodes the addressed register, loads it from D0–D7, and asserts
`/DTACK`. The cycle terminates, and `/DTACK` returns to high-impedance,
when either `/CS` or `/DS` is negated.

**Interrupt-acknowledge cycle.** When the CPU services a pending MFP
interrupt it asserts `/IACK` together with `/DS` at the MFP's programmed
priority level. An MFP whose `/IEI` is asserted and which has a pending
interrupt responds by driving an 8-bit vector number for the
highest-priority pending channel onto D0–D7 and asserting `/DTACK`; an
MFP with no pending interrupt instead asserts `/IEO` (without driving the
bus or `/DTACK`) so that the next, lower-priority device in the daisy
chain may respond. The cycle terminates, returning `/DTACK` and the data
bus to high-impedance, when either `/DS` or `/IACK` is negated. `/IRQ` is
negated as a result of the cycle unless further interrupts remain
pending. If no channel actually qualifies at acknowledge time (e.g. the
request was withdrawn between assertion and acknowledge), the MFP does
not respond with `/DTACK`, and the CPU's own spurious-interrupt handling
(autovectoring after a bus-error timeout) takes over.

## 3. General-Purpose I/O Port

The 8-bit bidirectional GPIP (I0–I7) is controlled by three registers:

- **GPDR** (General Purpose Data Register). Writing GPDR drives the
  output-configured pins; pins configured as inputs remain
  high-impedance and are unaffected by the write. Reading GPDR returns,
  for output-configured pins, the last value written, and for
  input-configured pins, the instantaneous level from the input buffer.
- **DDR** (Data Direction Register). Each bit selects whether the
  corresponding GPIP pin is a push-pull output (1) or a high-impedance
  input (0, the reset default, so all GPIP pins power up as inputs).
- **AER** (Active Edge Register). For each GPIP bit, selects which
  transition is recognized as the interrupt-generating "active" edge on
  that pin. The transition detector is implemented as an exclusive-OR of
  the AER bit and the input buffer's state, so writing AER can itself
  trigger a transition and set a pending bit depending on the current
  input level; consequently AER should be configured *before* interrupts
  for that channel are enabled via IERA/IERB, and changing an edge bit
  while its interrupt is enabled may generate a spurious interrupt.

Each GPIP line configured as an input generates an interrupt on the edge
selected by its AER bit, setting the associated pending bit in IPRA/IPRB
(subject to that channel's enable in IERA/IERB — see §4). Since
interrupts are enabled bit-by-bit, a subset of GPIP can be used as
handshake lines while the remainder serve as a general interrupt-driven
input port — including, notably, as a way to attach non-vectoring
peripherals (e.g. M6800-family devices, whose `/IRQ` output does not
supply a vector during acknowledge) to the GPIP so that each gets its own
unique MFP-generated vector number, reducing interrupt latency compared
with a polled autovector handler.

## 4. Interrupt Controller

The MFP multiplexes sixteen interrupt sources — eight GPIP pins, four
timers, and the USART's four conditions (receiver buffer full, receiver
error, transmitter buffer empty, transmitter error/underrun) — onto a
single `/IRQ` output, using a fully vectored, prioritized scheme built
from five register pairs, each split into an "A" half (channels 8–15)
and a "B" half (channels 0–7):

| Register pair | Function |
|---|---|
| IERA / IERB | Interrupt Enable Register. A cleared bit disables the channel entirely: interrupts on that channel are ignored, and writing a 0 also clears any pending bit already latched for that channel (terminating any outstanding request) — though it does *not* clear a bit already latched in the in-service register. Readable at any time. |
| IPRA / IPRB | Interrupt Pending Register. Set when an enabled channel's interrupt condition occurs. In a vectored scheme, cleared automatically when the CPU acknowledges the channel; in a polled scheme, must be cleared by the handler. A single bit is cleared in software by writing 0 to that bit and 1 to all others — writing all 1s has no effect. |
| ISRA / ISRB | Interrupt In-Service Register (software end-of-interrupt mode only). Set for a channel when its vector is returned during `/IACK`. Cleared by writing 0 to that bit (and 1 to all others), or automatically in automatic end-of-interrupt mode, where the ISR bits are forced to 0 at all times. Readable at any time. |
| IMRA / IMRB | Interrupt Mask Register. Gates whether a pending, enabled channel may actually assert `/IRQ`; masking (clearing the bit) does not stop the channel's pending bit from being set on a qualifying event, it only withholds the resulting bus request. If a currently-requesting channel is masked, the request ceases and `/IRQ` is negated unless another channel is still requesting; when unmasked again, any interrupt that became pending in the meantime is serviced at its normal priority. Readable at any time. |
| VR | Vector Register. Bits 7–4 form the user-programmable upper nibble of the 8-bit vector returned to the CPU; bit 3 (S) selects automatic (0) or software (1) end-of-interrupt mode; bits 2–0 are unused. |

### 4.1 Channel assignment and priority

The sixteen channels have a fixed hardware priority, channel 15 (`1111`)
highest down to channel 0 (`0000`) lowest:

| Channel | Binary | Source |
|---|---|---|
| 15 | 1111 | GPIP7 (I7) |
| 14 | 1110 | GPIP6 (I6) |
| 13 | 1101 | Timer A |
| 12 | 1100 | Receiver Buffer Full |
| 11 | 1011 | Receiver Error |
| 10 | 1010 | Transmitter Buffer Empty |
| 9 | 1001 | Transmitter Error (Underrun/End) |
| 8 | 1000 | Timer B |
| 7 | 0111 | GPIP5 (I5) |
| 6 | 0110 | GPIP4 (I4) / TAI (see §2) |
| 5 | 0101 | Timer C |
| 4 | 0100 | Timer D |
| 3 | 0011 | GPIP3 (I3) / TBI (see §2) |
| 2 | 0010 | GPIP2 (I2) |
| 1 | 0001 | GPIP1 (I1) |
| 0 | 0000 | GPIP0 (I0) |

The low-order 4 bits of the interrupt vector delivered during `/IACK`
are exactly this binary channel number; the upper 4 bits come from VR
bits 7–4. Selective masking (§ IMR above) lets software effectively
re-prioritize channels, since a masked channel cannot assert `/IRQ`
regardless of its hardware priority.

The highest-priority channel that is simultaneously pending, enabled and
unmasked, and (in software end-of-interrupt mode) whose priority exceeds
that of any channel currently marked in-service, drives `/IRQ` active.

### 4.2 Daisy-chain expansion

The MFP supports 8 external (GPIP) and 8 internal interrupt sources per
device. When a system needs more than eight external sources at one CPU
priority level, additional MFPs can be daisy-chained via `/IEI`/`/IEO`:
the highest-priority device's `/IEI` is tied low, and each device's
`/IEO` feeds the next lower-priority device's `/IEI`, with the
lowest-priority device's `/IEO` left unconnected. All devices in a chain
share a common `/IACK`; during an acknowledge cycle, only the device
electrically closest to a pending request in the chain drives the vector,
resolving inter-device priority without extra logic.

### 4.3 Nesting and end-of-interrupt modes

In an M68000 vectored system, the MFP is assigned one of seven interrupt
priority levels; once the CPU recognizes an interrupt at that level, the
CPU itself masks further interrupts at that level or below, so
same-level MFP interrupts cannot nest unless the handler explicitly
lowers the CPU's interrupt mask. The MFP's end-of-interrupt mode (S bit
of VR) controls how such nesting behaves; it has no effect in a polled
(non-vectored) scheme.

- **Automatic End-of-Interrupt (S = 0).** A channel's pending bit is
  cleared as soon as its vector is returned; the in-service registers
  are forced to 0 and no record of "currently servicing" is kept. Any
  subsequent interrupt on any channel — including a lower-priority one —
  will generate a fresh request as soon as the CPU's own mask permits,
  since the MFP retains no memory of which channel is mid-service.
- **Software End-of-Interrupt (S = 1).** Returning a channel's vector
  clears its pending bit and sets its in-service bit. While that bit
  remains set, only a *higher*-priority channel may request service and
  be acknowledged; the channel can still receive and latch further
  pending interrupts, but issues no new request until its handler
  explicitly clears the in-service bit (write 0 to that bit, 1 to the
  rest — writing all 1s has no effect). This provides full
  priority-based nesting: the handler can lower the CPU's mask and
  accept lower-priority MFP interrupts before finishing the current one.
  Transitioning the S bit from 1 to 0 clears both ISR registers.

Disabling a channel via IER immediately clears its pending bit but,
notably, does *not* clear an already-latched in-service bit — in
software end-of-interrupt mode that bit must still be cleared explicitly
by the handler even after the channel is disabled.

## 5. Timers

The MFP contains four independent 8-bit down-counting timers (A, B, C,
D), each built from a data register (TADR/TBDR/TCDR/TDDR) holding the
reload/count value, and a control register (TACR, TBCR, or the shared
TCDCR for C/D) selecting operating mode and prescale ratio. All four
share a common prescaler clock input, XTAL1/XTAL2, independent of the
CLK bus-timing input — so the timers (and, via a timer output, the
USART baud-rate clock) can run from a different, often more precise,
frequency reference than the host bus. Each timer's output pin toggles
whenever its main counter counts through `01` (hex), regardless of mode;
in delay mode this produces a square wave at half the time-out
frequency. TAO and TBO can additionally be forced low at any time by
writing a 1 to the Reset bit of TACR/TBCR, and a device reset drives all
four timer outputs low.

### 5.1 Control register format

Timer A and Timer B each have a dedicated 8-bit control register:

| Bits | Field | Description |
|---|---|---|
| 7–5 | unused | Reserved, read as 0. |
| 4 | Reset | One-shot strobe: writing a 1, while the timer is in a delay-related mode, forces TAO/TBO low. |
| 3 | Event | Selects delay mode (0) or event/pulse mode (1) — see §5.2. |
| 2–0 | Control | Selects the prescaler divide ratio, or stops the timer — see table below. |

Timer C and Timer D share one control register, TCDCR, split into two
4-bit nibbles (bits 7–4 for Timer C, bits 3–0 for Timer D), each with a
1-bit unused field and a 3-bit prescale-control field. Timers C and D
support delay mode only — they have no dedicated auxiliary input pin and
so no event/pulse-width capability.

| Control value | Prescale divisor |
|---|---|
| 000 | Timer stopped (main counter still directly readable/writable) |
| 001 | ÷4 |
| 010 | ÷10 |
| 011 | ÷16 |
| 100 | ÷50 |
| 101 | ÷64 |
| 110 | ÷100 |
| 111 | ÷200 |

### 5.2 Operating modes

**Delay mode** (Event bit = 0; all four timers). The prescaler divides
the XTAL1 clock by the selected ratio, and each resulting pulse
decrements the 8-bit main counter by one. When the counter counts through
`01` (hex), it is reloaded from the data register (a value of `00`
reloads as 256, the maximum count), a time-out pulse is produced, the
timer's interrupt-pending bit is set (if that channel is enabled), and
the output pin toggles. This free-running, auto-reload behavior is the
usual way to generate a periodic interrupt of programmable period, such
as a system tick.

Example: with divide-by-10 selected and the data register loaded with
100 (decimal), the counter decrements once every 10 timer-clock cycles,
producing a time-out every 1,000 timer clocks; the output line completes
one full period every 2,000 timer-clock cycles.

**Event-count mode** (Event bit = 1, Control field = 000; Timers A and B
only). The prescaler is bypassed; the counter instead decrements once
per active transition on the timer's auxiliary input (TAI for A, TBI for
B), where the active edge is the one selected by the AER bit normally
associated with GPIP4 (for TAI) or GPIP3 (for TBI). This lets Timer A or
B count external events (pulses, revolutions, an external real-time-clock
tick) and interrupt after a programmable number of them. To be counted
reliably, the input may transition no faster than once every four timer
clock periods (i.e. its maximum frequency is one-fourth of the timer
clock). As in delay mode, the active transition also generates an
interrupt on the associated channel if that channel is enabled — though
typically it is left disabled, since the timer itself is already
counting the transitions.

**Pulse-width measurement mode** (Event bit = 1, Control field ≠ 000;
Timers A and B only). The timer behaves as in delay mode — prescaler
active, main counter decrementing — but only while the level on TAI/TBI
is in its "active" state, as defined by the associated AER edge bit
(edge bit = 1 → active-high; edge bit = 0 → active-low). This measures
the width of the active pulse in units of timer-clock periods. Uniquely
in this mode, the interrupt normally triggered on the auxiliary input's
AER-selected edge instead fires on the *opposite* transition — e.g. with
the edge bit set to 1 (active-high), the timer runs while TAI is high,
and the interrupt fires when TAI falls, at the moment the measured pulse
ends and its width becomes available in the data register. After
reading the counter, the data register must be rewritten to allow the
next pulse to be measured correctly; writing it while a pulse is
transitioning to the active state may load an indeterminate value.

### 5.3 Reading and writing the timers

The data register can be read at any time; the value returned is the
counter's value as of the last low-to-high transition of `/DS`. Writing
the data register while the timer is stopped (control field = 000)
loads both the data register and the main counter immediately. Writing
while the timer is running loads only the data register; the new value
takes effect the next time the counter counts through `01` (hex),
without disturbing the countdown in progress — writing during the actual
`01`-to-reload transition may load an indeterminate value into the main
counter. To force an immediate reload with a new count, software
typically stops the timer, writes the data register, then restarts it.

Changing a running timer's prescale value does not immediately
resynchronize the counter: per the device's AC timing specification, the
first time-out pulse after such a change may occur at an indeterminate
point no less than 1 and no more than 200 timer-clock periods later,
after which subsequent time-outs resume the newly selected, correct
interval.

## 6. USART

The MFP integrates a full-duplex, double-buffered USART supporting
asynchronous start/stop communication and, with the aid of an on-chip
polynomial generator/checker, byte-synchronous communication, with
independently clocked and independently interrupting receive and
transmit sections. Five registers control and report USART state:

| Register | Function |
|---|---|
| UCR | USART Control Register — clock mode, character length, format (stop bits / synchronous mode), parity. |
| RSR | Receiver Status Register — receiver enable and status/error flags. |
| TSR | Transmitter Status Register — transmitter enable, output control, and status/control flags. |
| SCR | Synchronous Character Register — the sync/address character used in synchronous mode. |
| UDR | USART Data Register — the single-byte transmit/receive data buffer. |

### 6.1 Clock modes and character formats

UCR bit 7 selects the receive/transmit clock-to-data-rate ratio: **1×**
(RC/TC clock the bit rate directly; used when synchronization is handled
externally, or in synchronous mode) or **16×** (RC/TC run at sixteen
times the bit rate, the conventional asynchronous oversampling ratio).
In 16× mode, data is sampled at mid-bit time for improved noise
rejection, and a resynchronization state machine increases the channel's
tolerance to clock skew: a valid transition resets an internal counter
to state 0, transition-checking is inhibited until state 4, and at state
8 the transition-checker's prior state is clocked into the receive shift
register.

**Asynchronous format.** Word length (5–8 data bits), stop-bit count (1,
1.5, or 2), and parity (odd, even, or none) are independently
programmable via UCR. For character lengths under 8 bits, the assembled
character occupies the low-order bits with the unused high-order bit
positions zero-filled, followed by the parity bit if enabled. Start-bit
detection is always active; a new character does not begin shifting in
until a 0 (space) bit is seen. In 16× mode, false-start-bit rejection is
also active: a transition must be stable for three RC clock edges to be
considered valid, and no further 0-to-1 transition may occur for at
least eight more clock edges.

**Synchronous format.** The 8-bit sync character in SCR is compared
against incoming data until a match establishes synchronization; the
sync word is retransmitted continuously during an underrun. Received
sync characters can optionally be stripped from the data delivered to
UDR. The sync character is normally written *after* the word length is
selected, since unused bit positions in SCR are zeroed; when parity is
enabled and the word length is 8, the MFP computes and appends the
parity bit for the sync word automatically, but for shorter word
lengths the software must compute the sync word's parity itself and
include it in the value written to SCR.

### 6.2 USART Control Register (UCR)

| Bits | Field | Description |
|---|---|---|
| 7 | Clock mode | 1× or 16× (see §6.1). |
| 6–5 | Character length | 8, 7, 6, or 5 data bits. |
| 4–3 | Format | Asynchronous: selects 1, 1.5, or 2 stop bits; one encoding instead selects synchronous mode, in which SCR governs character synchronization and no start/stop bits are used. |
| 2 | Parity enable | Enables parity generation/checking. |
| 1 | Parity | Odd or even, when enabled. |
| 0 | unused | Reserved. |

### 6.3 Receiver

Serial data on SI is clocked into an internal 8-bit shift register until
a full character is assembled, then transferred to the receive buffer
(UDR) — provided the previous buffer contents have already been read —
generating a Buffer Full interrupt. Each transfer also latches fresh
status into RSR; RSR is not updated again until UDR has been read. To
keep data and status correctly paired, software should read RSR *before*
UDR: if UDR is read first, a new character could complete and overwrite
RSR before the status for the first character is retrieved.

RSR bit 0 (Receiver Enable) must be set for the receiver to run, and it
otherwise reports:

- **Buffer Full** — a character is ready in UDR; cleared when UDR is
  read.
- **Overrun Error (OE)** — a new character completed before the previous
  one was read, so data was lost.
- **Parity Error (PE)** — received parity does not match the sense
  selected by UCR.
- **Frame Error** — no stop bit found at the expected position
  (asynchronous mode).
- **Found/Search or Break (F/S, B)** — break-condition detection (a
  continuous space held longer than a full character time), or, in
  synchronous mode, whether byte synchronization has been achieved.
- **Match / Character-in-Progress** — in synchronous mode, whether the
  assembled character matches the SCR sync/address character; in
  asynchronous mode, indicates a character is currently being assembled.
- **Synchronous Strip Enable** — when set (synchronous mode), strips
  matched sync characters from the data delivered to UDR.

The receive section has two interrupt channels: one for the Buffer Full
condition, one for error conditions (overrun, parity, frame/break). If
the error channel is enabled, an error interrupt fires only on that
channel; if it is disabled, an error instead produces an interrupt on
the Buffer Full channel alongside normal buffer-full interrupts. Either
way, RSR must be read to determine the actual cause. The status flags
themselves latch and behave identically whether or not their interrupt
channel is enabled.

Two overrun/break edge cases: a break received while the buffer is
already full does *not* by itself produce an overrun — only the B flag
is set once the buffer is read; but if a *new character* (not just a
break) is received while the buffer is full, and a break follows before
the buffer is read, both B and OE are set together.

### 6.4 Transmitter

Writing UDR loads the transmit buffer; its contents move into the
internal shift register once the previous word finishes shifting out,
producing a Buffer Empty condition. If the shift register empties
before a new word is written, an Underrun error occurs (particularly
relevant in synchronous mode, where continuous character flow is
expected). Between enabling the transmitter and the first bit going out
there is a fixed delay, during which SO should be programmed (via TSR)
to the desired idle state (high, low, or high-impedance); a 1 bit is
always transmitted immediately ahead of the first real word when the
transmitter is first enabled. In asynchronous mode, an idle transmitter
sends continuous marks until UDR is written; in synchronous mode, it
continuously repeats the sync character.

Disabling the transmitter lets any word currently in flight finish
normally, but a word waiting in the buffer is *not* sent and remains
buffered (no Buffer Empty results); if the buffer was already empty at
disable time, that Buffer Empty condition persists but no Underrun is
raised when the in-flight word completes. If nothing is being
transmitted at disable time, the transmitter stops at the next shift
clock edge.

TSR can also command a **Break**: transmission of a continuous space,
starting once the character in the shift register finishes (or
immediately, if the shift register is empty), and continuing until the
break command is cleared. An End interrupt fires at each character
boundary during a break, to help time its duration. A character already
in the transmit buffer when a break begins is sent once the break ends;
if the buffer was empty at the start of the break, it may be written
during the break, and if it is still empty when the break ends, an
underrun results. Disabling the transmitter mid-break truncates the
break at the end of the current character (no stop bit is appended) and
raises neither Buffer Empty nor Underrun, regardless of buffer state.

The transmit section similarly has two interrupt channels: Buffer Empty,
and Underrun/End combined.

**Transmitter Status Register (TSR) fields:**

- **Buffer Empty** — UDR has emptied into the shift register; drives the
  Transmitter Buffer Empty interrupt channel.
- **Underrun Error (UE)** — shift register emptied with no new UDR data
  available; drives the Transmitter Error channel.
- **End** — marks transmission of the final character, used with break
  generation for correct frame termination.
- **Break** — forces continuous transmission of a space condition
  regardless of UDR contents.
- **Auto Turnaround** — when enabled, automatically switches SO to
  high-impedance and re-enables the receiver as soon as the transmitter
  empties, for CPU-transparent half-duplex/multidrop/modem-control links.
- **Output control** — a 2-bit field selecting SO's idle-state behavior
  when the transmitter is disabled: high-impedance, forced low, forced
  high, or loopback (SO routed internally back to the receiver input,
  for diagnostic self-test; SI is not used in this mode).

### 6.5 DMA operation

The USART's `/RR` (receiver ready) and `/TR` (transmitter ready) outputs
let a DMA controller drive block transfers directly, bypassing the
per-character interrupt channels. Because USART error flags are valid
only at each character boundary, block transfers under DMA must
accumulate errors across the block: the error channel (receiver or
transmitter) is enabled but its interrupt is masked during the transfer,
and once the block completes, software reads IPRA — any pending
receiver- or transmitter-error bit indicates that an error occurred
somewhere in the block.

### 6.6 Synchronous Character Register (SCR)

Holds the sync/address character (bit 7 is a control bit distinct from
the sync-character data) that the receiver's synchronization logic
searches for to establish or re-establish byte alignment, and which the
transmitter automatically prefixes to outgoing synchronous frames (see
§6.1 and §6.3).

## 7. Register Map

| Hex offset | RS5 | RS4 | RS3 | RS2 | RS1 | Register | Name |
|---:|:---:|:---:|:---:|:---:|:---:|---|---|
| 01 | 0 | 0 | 0 | 0 | 0 | GPDR (GPIP) | General Purpose I/O Data Register |
| 03 | 0 | 0 | 0 | 0 | 1 | AER | Active Edge Register |
| 05 | 0 | 0 | 0 | 1 | 0 | DDR | Data Direction Register |
| 07 | 0 | 0 | 0 | 1 | 1 | IERA | Interrupt Enable Register A |
| 09 | 0 | 0 | 1 | 0 | 0 | IERB | Interrupt Enable Register B |
| 0B | 0 | 0 | 1 | 0 | 1 | IPRA | Interrupt Pending Register A |
| 0D | 0 | 0 | 1 | 1 | 0 | IPRB | Interrupt Pending Register B |
| 0F | 0 | 0 | 1 | 1 | 1 | ISRA | Interrupt In-Service Register A |
| 11 | 0 | 1 | 0 | 0 | 0 | ISRB | Interrupt In-Service Register B |
| 13 | 0 | 1 | 0 | 0 | 1 | IMRA | Interrupt Mask Register A |
| 15 | 0 | 1 | 0 | 1 | 0 | IMRB | Interrupt Mask Register B |
| 17 | 0 | 1 | 0 | 1 | 1 | VR | Vector Register |
| 19 | 0 | 1 | 1 | 0 | 0 | TACR | Timer A Control Register |
| 1B | 0 | 1 | 1 | 0 | 1 | TBCR | Timer B Control Register |
| 1D | 0 | 1 | 1 | 1 | 0 | TCDCR | Timers C and D Control Register |
| 1F | 0 | 1 | 1 | 1 | 1 | TADR | Timer A Data Register |
| 21 | 1 | 0 | 0 | 0 | 0 | TBDR | Timer B Data Register |
| 23 | 1 | 0 | 0 | 0 | 1 | TCDR | Timer C Data Register |
| 25 | 1 | 0 | 0 | 1 | 0 | TDDR | Timer D Data Register |
| 27 | 1 | 0 | 0 | 1 | 1 | SCR | Synchronous Character Register |
| 29 | 1 | 0 | 1 | 0 | 0 | UCR | USART Control Register |
| 2B | 1 | 0 | 1 | 0 | 1 | RSR | Receiver Status Register |
| 2D | 1 | 0 | 1 | 1 | 0 | TSR | Transmitter Status Register |
| 2F | 1 | 0 | 1 | 1 | 1 | UDR | USART Data Register |

All 24 registers fall on odd byte addresses, given RS1↔A1…RS5↔A5 wiring
with `/DS` tied to the CPU's lower data strobe (required for vectored
interrupts, see §2). Logically they form one contiguous 24-register set.

## 8. Reset Behavior

`/RESET` must be held low for a minimum of 2 µs. On reset:

- All internal registers are cleared to zero **except**: the four timer
  data registers (TADR, TBDR, TCDR, TDDR), the USART data register
  (UDR), the Transmitter Status Register (TSR), and the Vector Register,
  which is instead forced to `$0F` (upper nibble 0, S = 1, i.e.
  software end-of-interrupt selected) rather than cleared.
- All four timers are stopped (control fields read as the "stopped"
  code) and their outputs (TAO–TDO) are driven low.
- All interrupt channels are disabled, and any pending or in-service
  interrupts are cleared.
- The USART receiver and transmitter are disabled.
- The GPIP lines are placed in the high-impedance input state (DDR = 0
  for all bits).
- SO is driven high; other external MFP outputs are negated.

The device remains in this quiescent state until initialized by
software, which typically programs the vector base and end-of-interrupt
mode, enables the desired interrupt channels, sets GPIP direction and
edge sense, starts the required timers, and configures and enables the
USART, in whatever order suits the target system.

## 9. Electrical Characteristics

### 9.1 Maximum Ratings

| Rating | Symbol | Value | Unit |
|---|---|---:|---|
| Supply voltage | VCC | −0.3 to 7.0 | V |
| Input voltage | Vin | −0.3 to 7.0 | V |
| Operating temperature range | TA | 0 to 70 | °C |
| Storage temperature | Tstg | −65 to 150 | °C |
| Power dissipation | PD | 1.5 | W |

This device contains input protection circuitry against static
discharge and field-induced damage; normal precautions against exceeding
these maximum ratings should still be observed, and reliability is
improved by tying unused inputs to an appropriate logic level (VCC or
GND) rather than leaving them floating.

### 9.2 Thermal Characteristics

| Characteristic | Symbol | Value | Unit |
|---|---|---:|---|
| Thermal resistance, ceramic package | θJA | 40 | °C/W |
| Thermal resistance, plastic package | θJA | TBD (not specified at time of publication) | °C/W |

### 9.3 Power Considerations

Average chip-junction temperature `TJ` (°C):

```
TJ = TA + (PD × θJA)                                           (1)
```

where `TA` is ambient temperature (°C), `θJA` is junction-to-ambient
thermal resistance (°C/W), and `PD = PINT + PI/O`, with `PINT = ICC ×
VCC` (chip internal power) and `PI/O` the user-determined power
dissipated in the input/output pins. For most applications `PI/O <
PINT` and can be neglected.

An approximate relation between `PD` and `TJ`, neglecting `PI/O`:

```
PD = K ÷ (TJ + 273 °C)                                          (2)
```

Solving (1) and (2) for the part-specific constant `K`:

```
K = PD × (TA + 273 °C) + θJA × PD²                              (3)
```

`K` can be found from (3) by measuring `PD` at thermal equilibrium for a
known `TA`; `PD` and `TJ` at any other `TA` then follow by iterating (1)
and (2).

### 9.4 DC Electrical Characteristics

Conditions: TA = 0 °C to 70 °C, VCC = 5 V ±5%, unless otherwise noted.

| Characteristic | Symbol | Min | Max | Unit |
|---|---|---:|---:|---|
| Input high voltage | VIH | 2.0 | VCC + 0.3 | V |
| Input low voltage | VIL | −0.3 | 0.8 | V |
| Output high voltage, except /DTACK (IOH = −120 µA) | VOH | 2.4 | — | V |
| Output low voltage, except /DTACK (IOL = 2.0 mA) | VOL | — | 0.5 | V |
| Power-supply current, outputs open | ICC | — | 180 | mA |
| Input leakage current (Vin = 0 to VCC) | ILI | — | 10 | µA |
| High-impedance output leakage current, floating high (Vout = 2.4 to VCC) | ILOH | — | 10 | µA |
| High-impedance output leakage current, floating low (Vout = 0.5 V) | ILOL | — | −10 | µA |
| /DTACK output source current (Vout = 2.4 V) | IOH | — | −400 | µA |
| /DTACK output sink current (Vout = 0.5 V) | IOL | — | 5.3 | mA |

### 9.5 Capacitance

Conditions: TA = 25 °C, f = 1 MHz, unmeasured pins grounded.

| Characteristic | Symbol | Min | Max | Unit |
|---|---|---:|---:|---|
| Input capacitance | Cin | — | 10 | pF |
| High-impedance output capacitance | Cout | — | 10 | pF |

### 9.6 Clock Timing

| Characteristic | Symbol | Min | Max | Unit |
|---|---|---:|---:|---|
| Frequency of operation | f | 1.0 | 4.0 | MHz |
| Cycle time | tCYC | 250 | 1000 | ns |
| Clock pulse width | tCL, tCH | 110 | 250 | ns |
| Rise and fall times | tCr, tCf | — | 15 | ns |

Suggested crystal parameters for XTAL1/XTAL2: parallel-resonance,
fundamental-mode AT-cut, HC6 or HC33 holder; frequency tolerance (18 pF
load) 0.1%; drive level 10 µW; shunt capacitance ≤7 pF; series
resistance `RS ≤ 300 Ω` for 2.0–2.7 MHz, `RS ≤ 150 Ω` for 2.8–4.0 MHz.

### 9.7 AC Electrical Characteristics

Conditions: VCC = 5.0 V ±5%, VSS = 0 V, TA = 0 °C to 70 °C, unless
otherwise noted.

| No. | Characteristic | Min | Max | Unit |
|---:|---|---:|---:|---|
| 1 | /CS, /IACK, /DS width high | 50 | — | ns |
| 2 | Address valid to falling /CS setup time | 30 | — | ns |
| 3 | Data valid prior to rising /DS setup time | 280 | — | ns |
| 4¹ | /CS, /IACK valid to falling clock setup time | 50 | — | ns |
| 5 | Clock low to /DTACK low | — | 220 | ns |
| 6 | /CS or /DS high to /DTACK high | — | 60 | ns |
| 7 | /CS or /DS high to /DTACK high-impedance | — | 100 | ns |
| 8 | /CS or /DS high to data-invalid hold time | 0 | — | ns |
| 9 | /CS or /DS high to data high-impedance | — | 50 | ns |
| 10 | /CS or /DS high to address-invalid hold time | 0 | — | ns |
| 11 | Data valid from /CS low | — | 250 | ns |
| 12 | Read data valid to /DTACK low setup time | 50 | — | ns |
| 13 | /DTACK low to /DS or /CS high hold time | 0 | — | ns |
| 14 | /IEI low to clock-falling setup time | 50 | — | ns |
| 15 | /IEO valid from clock-low delay time | — | 220 | ns |
| 16 | Data valid from clock-low delay time | — | 300 | ns |
| 17 | /IEO invalid from /IACK-high delay time | — | 100 | ns |
| 18 | /DTACK low from clock-high delay time | — | 220 | ns |
| 19 | /IEO valid from /IEI-low delay time | — | 140 | ns |
| 20 | Data valid from /IEI-low delay time | — | 200 | ns |
| 21 | Clock cycle time | 250 | 1000 | ns |
| 22 | Clock width low | 110 | — | ns |
| 23 | Clock width high | 110 | — | ns |
| 24² | /CS, /IACK inactive to rising-clock setup time | 100 | — | ns |
| 25 | I/O minimum active pulse width | 100 | — | ns |
| 26 | I/O minimum time between active edges | 100 | — | ns |
| 27 | I/O data valid from rising /CS or /DS | — | 500 | ns |
| 28 | Receiver-ready delay from rising RC | — | 240 | ns |
| 29 | Transmitter-ready delay from rising TC | — | 295 | ns |
| 30 | Timer output low from rising edge of /CS or /DS, timers A and B (reset-output time) | — | 500 | ns |
| 31 | Output valid from internal time-out | — | 2 tCLK + 300 | ns |
| 32 | Timer clock low time | 110 | — | ns |
| 33 | Timer clock high time | 110 | — | ns |
| 34 | Timer clock cycle time | 250 | 1000 | ns |
| 35 | /RESET low time | 2 | — | µs |
| 36 | Delay to falling /IRQ from external-interrupt active transition | — | 380 | ns |
| 37 | Transmitter internal delay from rising/falling edge of TC | 550 | — | ns |
| 38 | Receiver-buffer-full interrupt transition delay from rising edge of RC | 750 | — | ns |
| 39 | Receiver-error interrupt transition delay from falling edge of RC | 750 | — | ns |
| 40 | Serial-input setup time from rising edge of RC (÷1 mode only) | 80 | — | ns |
| 41 | Data hold time from rising edge of RC (÷1 mode only) | 350 | — | ns |
| 42 | Serial-output data valid from falling edge of TC | — | 390 | ns |
| 43 | Transmitter clock low time | 500 | — | ns |
| 44 | Transmitter clock high time | 500 | — | ns |
| 45 | Transmitter clock cycle time | 1.05 | — | µs |
| 46 | Receiver clock low time | 500 | — | ns |
| 47 | Receiver clock high time | 500 | — | ns |
| 48 | Receiver clock cycle time | 1.05 | — | µs |
| 49 | /CS, /IACK, /DS width low | — | 80 | tCLK |
| 50 | Serial-output data valid from falling edge of TC (÷16 mode) | — | 490 | ns |

1. If the setup time is not met, `/CS` will not be recognized until the
   next falling clock edge.
2. If this setup time is met on consecutive cycles, the minimum
   hold-off is one clock cycle; otherwise it is two clock cycles.

### 9.8 Timer AC Characteristics

Definitions: `Error = indicated time value − actual time value`;
`tPSC = tCLK × prescale value`.

**Internal timer mode**

| Characteristic | Error/requirement |
|---|---|
| Single interval error, free running | ±100 ns |
| Cumulative internal error | 0 |
| Error between two timer reads | ±(tPSC − 4 tCLK) |
| Start timer to stop timer error | 2 tCLK + 100 ns to −(tPSC + 6 tCLK + 100 ns) |
| Start timer to read timer error | 0 to −(tPSC + 6 tCLK + 400 ns) |
| Start timer to interrupt-request error | −2 tCLK to −(4 tCLK + 800 ns) |

**Pulse-width measurement mode**

| Characteristic | Error/requirement |
|---|---|
| Measurement accuracy | 2 tCLK to −(tPSC + 4 tCLK) |
| Minimum pulse width | 4 tCLK |

**Event-counter mode**

| Characteristic | Requirement |
|---|---|
| Minimum active time of TAI and TBI | 4 tCLK |
| Minimum inactive time of TAI and TBI | 4 tCLK |

Notes: error may be cumulative if measured repetitively; error is
relative to TOUT or `/IRQ` where noted; the interrupt-request figures
assume the timer can request service immediately.

## 10. Mechanical Data and Ordering Information

The MC68901 is packaged in a 48-pin dual-in-line package (ceramic or
plastic).

### Ordering Information

| Package type | Maximum clock frequency | Temperature range | Order number |
|---|---:|---:|---|
| Ceramic, L suffix | 4.0 MHz | 0 °C to 70 °C | `MC68901L` |
| Plastic, P suffix | 4.0 MHz | 0 °C to 70 °C | `MC68901P` |
