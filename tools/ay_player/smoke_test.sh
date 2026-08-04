#!/bin/sh
# ay_player smoke test: builds the tool, then runs its WAV-export path
# (headless, no ALSA device required) against one real file per format
# from ../../test_corpus_76, confirming correct format detection and a
# well-formed, audible WAV file for each.
# Also checks the error paths (unrecognized content, missing file).
# See migration_debt.yaml MIG-0027.
set -e
cd "$(dirname "$0")"
make >/dev/null
CORPUS="../../test_corpus_76"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

status=0

check_format() {
  file="$1"; expect_format="$2"; expect_audible="$3"
  out="$WORKDIR/$(basename "$file").wav"
  stderr=$(./ay_player "$CORPUS/$file" --wav="$out" --seconds=2 2>&1 >/dev/null)
  if ! echo "$stderr" | grep -q "detected as $expect_format"; then
    echo "[FAIL] $file: expected format=$expect_format, got: $stderr"
    status=1
    return
  fi
  if [ ! -s "$out" ]; then
    echo "[FAIL] $file: no WAV output produced"
    status=1
    return
  fi
  if ! file "$out" | grep -qi "WAVE audio"; then
    echo "[FAIL] $file: output is not a valid WAV file"
    status=1
    return
  fi
  nonzero=$(python3 -c "
import sys
with open('$out','rb') as f:
    f.seek(44)
    data = f.read()
print('yes' if any(b != 0 for b in data) else 'no')
")
  if [ "$nonzero" != "$expect_audible" ]; then
    echo "[FAIL] $file: expected audible=$expect_audible, got audible=$nonzero"
    status=1
    return
  fi
  echo "[PASS] $file: detected as $expect_format, audible=$nonzero"
}

check_format "MetalMania.ay" "AY" "yes"
check_format "Batman_Journey.ym" "YM" "yes"
check_format "ZAGON_07_remixDJ_EchoMAKROSS.pt3" "PT3" "yes"
check_format "DIABOLIS_IN_MUSICA.pt3" "PT3" "yes" # detected via extension fallback - corrupted title byte defeats the content signature, matching real Pascal's FoundPT3 (never checks that text)
check_format "More_Short_Demos.sndh" "SNDH" "yes" # MIG-0045 fixed the byte-swap bug that caused this; not yet byte-identical to the real Pascal engine (MIG-0021/MIG-0045)
check_format "GB2_5.vtx" "VTX" "yes"
check_format "DEMON.pt1" "PT1" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "L.Boy_broken.gtr" "GTR" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "SimpletonGift1.fls" "FLS" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "AWAY.stc" "STC" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "Girls_of_Meladze.stp" "STP" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "NOR.MUS..pt2" "PT2" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "The_Last_V8.fxm" "FXM" "yes" # detected via extension fallback (real Pascal never content-sniffs FXSM either)
check_format "m16.psm" "PSM" "yes" # detected via extension fallback (real Pascal never content-sniffs psm1 either)
check_format "NEWDANCE.asc" "ASC" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "MISTERS_BOX.as0" "ASC0" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "RE-TRIGG.ftc" "FTC" "yes" # detected via extension fallback - no reliable Ay_Emul.fmt signature
check_format "Inbetween_remix.psc" "PSC" "yes" # detected via extension fallback - no Ay_Emul.fmt signature
check_format "MotorAnimation.sqt" "SQT" "yes" # detected via extension fallback - no Ay_Emul.fmt signature

echo "not a chiptune file" > "$WORKDIR/garbage.bin"
if ./ay_player "$WORKDIR/garbage.bin" 2>&1 | grep -q "not a recognized"; then
  echo "[PASS] unrecognized file rejected cleanly"
else
  echo "[FAIL] unrecognized file was not rejected as expected"
  status=1
fi

if ./ay_player "$WORKDIR/does_not_exist" >/dev/null 2>&1; then
  echo "[FAIL] missing file should have failed"
  status=1
else
  echo "[PASS] missing file rejected cleanly"
fi

exit $status
