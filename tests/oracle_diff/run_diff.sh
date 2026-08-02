#!/bin/sh
# Differential validation of engine/z80_bus.c + engine/ay.c against the
# ORIGINAL Pascal Z80.pas/AY.pas (migration_debt.yaml MIG-0003/0005/0006b),
# and engine/m68k_bus.c (Musashi) against the original's precompiled
# Starscream 68000 core (MIG-0012), via ay_emul/OracleHarness.pas. Builds
# the real ay_emul binary with lazbuild, runs each synthetic scenario
# through both implementations, and byte-compares the results. The
# Starscream core is used here strictly as a local test oracle - see
# PORTING_TO_C11_LINUX.md §8.1 - never linked into or shipped with the
# C11 port itself.
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
ORACLE_BIN="$ROOT/ay_emul/lib/x86_64-linux/Ay_Emul"

echo "== Building ay_emul (oracle) =="
(cd "$ROOT/ay_emul" && lazbuild --build-mode=release Ay_Emul.lpi >/tmp/oracle_build.log 2>&1) \
  || { echo "oracle build failed, see /tmp/oracle_build.log"; exit 1; }

echo "== Building engine/ + dump_engine_state =="
(cd "$ROOT/engine" && make >/dev/null)
gcc -std=c11 -Wall -Wextra -O2 \
  -I"$ROOT/engine/include" -I"$ROOT/engine/third_party/z80" -I"$ROOT/engine/third_party/musashi" \
  dump_engine_state.c "$ROOT/engine/libayengine.a" -o dump_engine_state -lm

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

status=0
for scenario in zx cpc immediate m68k; do
  AY_EMUL_ORACLE="$scenario" AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_$scenario.txt" "$ORACLE_BIN"
  ./dump_engine_state "$scenario" "$WORKDIR/engine_$scenario.txt"
  if cmp -s "$WORKDIR/oracle_$scenario.txt" "$WORKDIR/engine_$scenario.txt"; then
    echo "[PASS] $scenario: oracle and engine registers match"
  else
    echo "[FAIL] $scenario: registers differ"
    diff "$WORKDIR/oracle_$scenario.txt" "$WORKDIR/engine_$scenario.txt" || true
    status=1
  fi
done

for pcm_scenario in pcm pcm_filtered pcm8; do
  AY_EMUL_ORACLE="$pcm_scenario" AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_$pcm_scenario.bin" "$ORACLE_BIN"
  ./dump_engine_state "$pcm_scenario" "$WORKDIR/engine_$pcm_scenario.bin"
  if cmp -s "$WORKDIR/oracle_$pcm_scenario.bin" "$WORKDIR/engine_$pcm_scenario.bin"; then
    echo "[PASS] $pcm_scenario: oracle and engine PCM output byte-identical"
  else
    echo "[FAIL] $pcm_scenario: PCM output differs"
    status=1
  fi
done

exit $status
