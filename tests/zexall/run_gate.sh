#!/bin/sh
# Z80 fidelity gate (PORTING_TO_C11_LINUX.md §7.1 / §8 step 0).
#
# Builds and runs the ZEXALL/ZEXDOC test suite that ships with the vendored
# engine/third_party/z80 (superzazu/z80) submodule against its own test ROMs
# (roms/zexall.cim, roms/zexdoc.cim, roms/prelim.com). Must pass with zero
# cycle-count diff before any engine code is built on top of this core.
set -e
cd "$(dirname "$0")/../../engine/third_party/z80"
make clean >/dev/null 2>&1 || true
make
./z80_tests
