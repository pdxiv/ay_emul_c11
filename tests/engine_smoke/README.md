# Engine smoke tests

This milestone's scope (standalone Z80/AY C library, no Players.pas) means
there is no real render loop yet to run actual `.psg`/AY-format files
through and byte-compare against a built `ay_emul` binary end-to-end - that
full differential validation is tracked as **MIG-0005** and belongs to the
Players.pas-hand-port milestone that builds on top of this one.

What this directory validates instead, standalone:

1. **Port-decode correctness** (`test_z80_bus.c`): synthetic Z80 machine-code
   sequences exercising the ZX-style (`OUT (C),A` to `$FFFD`/`$BFFD`) and
   CPC-style (PPI/PSG-select protocol via `$F4`/`$F6`) AY port protocols
   ported from `Z80.pas`'s live `ZXOutProc`/`CPCOutProc`/`InitialOutProc`
   (see `engine/src/z80_bus.c`), confirming the right register/value pairs
   reach the `on_ay_write` callback, and that `Z80_BUS_MACHINE_INITIAL`
   auto-detects and locks in the CPC protocol exactly as the original does.
2. **AY register/mixer correctness** (`test_ay.c`): confirms
   `ay_chip_set_ay_register` reproduces `AY.pas`'s per-register masking and
   side effects (e.g. register 8 sets amplitude + the envelope-enable flag),
   and that a short synthetic tune run through `ay_synthesizer_stereo16`
   produces a non-silent, finite PCM buffer (a basic regression net, not a
   fidelity claim).

Build and run:

```sh
make
./test_z80_bus && ./test_ay
```
