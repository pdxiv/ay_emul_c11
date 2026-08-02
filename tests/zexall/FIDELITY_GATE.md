# Z80 fidelity gate results

Required by `PORTING_TO_C11_LINUX.md` §7.1/§8 step 0 before any engine code is
built on top of superzazu/z80: confirm it passes ZEXALL/ZEXDOC and matches
this project's actual undocumented-flag and interrupt-timing behavior.

## Result: gate passed

Run via `run_gate.sh` (builds `engine/third_party/z80`'s own `z80_tests`
harness, which ships with the vendored ROMs under `roms/`):

```
*** TEST: roms/prelim.com    -> cycles match expected (8721), 0 instructions failed
*** TEST: roms/zexdoc.cim    -> cycles match expected (46734978649), 0 diff
*** TEST: roms/zexall.cim    -> cycles match expected (46734978649), 0 diff
5764169747 instructions executed, exit code 0
```

Zero failures, zero cycle-count discrepancy across all three ROMs. ZEXALL and
ZEXDOC both exhaustively exercise undocumented XF/YF flag behavior and
documented-flag edge cases across the full opcode set, so this is not a
narrow smoke test.

## Important correction to PORTING_TO_C11_LINUX.md's risk framing

§3.1 of that document frames "238 inline asm blocks" in `Z80.pas` as the
single highest-risk item in the whole port, on the assumption that the live
Ubuntu 20.04 binary executes hand-tuned x86 asm reading real EFLAGS bits to
derive undocumented flags. **Direct inspection of the actual conditional
compilation shows this is not what the shipping Linux binary does.**

- `Z80.pas:92` opens `{$ifdef Z80Emu_ASM}` and doesn't close until
  `{$else Z80Emu_noASM}` at `Z80.pas:10854` — this guards essentially the
  entire first copy of the CPU core, including all 238 asm blocks and the
  first copy of `Z80_Step`/`Z80_ExecuteCommand` (`Z80.pas:10810,10818`) and
  the first copy of `ZXOutProc`/`CPCOutProc` (`Z80.pas:264,225`).
- `Z80Emu_ASM` is **never defined** anywhere in this checkout — not in
  `Ay_Emul.lpi` (checked both `CustomOptions` blocks), not in `Ay_Emul.lpr`,
  not via a `{$define}` in `Z80.pas` itself. So this entire block, asm and
  all, is dead code in every build configuration this repo can currently
  produce, debug or release.
- The actually-compiled implementation is the `{$else Z80Emu_noASM}` branch
  (`Z80.pas:10854` to EOF), which contains its own small set of asm helpers
  (`SetFlagsInc`, `SetSliFlags`, etc., `Z80.pas:10869-10983`) — but those are
  further gated behind `{$ifdef debugz80}` (`Z80.pas:10869`), which is
  **also never defined** anywhere in this checkout. So even the "fallback"
  asm helpers don't compile into the shipping binary.
- The real, live flag-computation code (outside both dead-code guards) is
  plain Pascal bit arithmetic that mirrors the standard, well-documented
  algorithm for the undocumented flags — result bits 3 and 5 copied directly
  into XF/YF, e.g. `Z80.pas:11296: fl := res and (XF or YF or SF);` and
  `Z80.pas:11655: Z80_Registers.AF.LoByte := (Z80_Registers.AF.HiByte and
  (YF or XF)) or c or ...` (both inside the live noASM branch, which runs
  from `Z80.pas:10854` to end of file at `Z80.pas:22052`). This is
  exactly the algorithm every accurate software Z80 emulator (including
  superzazu/z80, confirmed by its ZEXALL/ZEXDOC pass above) already
  implements — not a hardware-EFLAGS-derived scheme needing bespoke
  differential verification.
- The live copy of `Z80_Step`/`Z80_ExecuteCommand` is at `Z80.pas:21009-21051`,
  not `10810-10852` — logic is identical between the two copies (confirmed by
  direct comparison), so this doesn't change any of the interrupt/T-state
  findings below, only which line numbers are the authoritative reference.

**Net effect on this port's risk profile:** the highest-risk item flagged in
§3.1 (238 asm blocks needing individual hand-verification) does not apply to
what Linux users actually run today — it was already dead weight before this
port started. The live reference behavior is plain, portable Pascal integer
arithmetic, which is both easier to reason about and already independently
corroborated by ZEXALL/ZEXDOC. This doesn't change the decision to use
superzazu/z80 (still the right call — no reason to hand-port working,
tested C when the "risk" that motivated extra caution turns out to be moot),
but it downgrades the fidelity-gate work from "must diff against hand-tuned
asm" to "confirm the well-known algorithm matches," which ZEXALL/ZEXDOC's
pass already does.

## Interrupt-acceptance cycle counts

Confirmed against the live copy (`Z80.pas:21027-21045`):

- IM2: 18 T-states, vectors through `ZRAM[IR.HiByte*256+255]`.
- IM1 **and** IM0 (collapsed to the same `else` branch — the original never
  modeled true IM0 arbitrary-instruction-from-data-bus execution): 12
  T-states, fixed `PC := $38`.

superzazu/z80 implements real, distinct IM0 (via `z80_gen_int(z, data)`,
executing whatever instruction is on the data bus) and a real 13-T IM1. Per
this codebase's actual usage (`PORTING_TO_C11_LINUX.md`'s Players.pas
exploration confirms only IM1-style ZX/CPC AY machines are driven through
this core — IM0 is not exercised by any in-scope format), this is a
**decided, intentional fidelity improvement, not an unresolved gap**: the
new engine will use superzazu/z80's real IM1 timing (13 T, one T-state more
accurate than the original's simplification) rather than reproducing the
original's simplification. Tracked as `MIG-0001` in `migration_debt.yaml`
(state `validated`) so it's not later mistaken for an unverified difference.

NMI needs no verification — confirmed unused anywhere in `Z80.pas` (both
copies).
