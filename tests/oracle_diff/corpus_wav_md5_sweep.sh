#!/bin/sh
# For every playable file in test_corpus_76/, render a WAV snippet through
# both the real Pascal codebase (ay_emul/OracleHarness.pas's wav_export_*
# scenarios) and the C11 tools/ay_player, md5sum both, and report PASS/FAIL.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ORACLE_BIN="$ROOT/ay_emul/lib/x86_64-linux/Ay_Emul"
PLAYER_BIN="$ROOT/tools/ay_player/ay_player"
CORPUS="$ROOT/test_corpus_76"
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

status=0
pass=0
fail=0
skip=0

run_one() {
  fname="$1"
  scenario="$2"
  frames="$3"
  oracle_wav="$WORKDIR/oracle.wav"
  player_wav="$WORKDIR/player.wav"
  rm -f "$oracle_wav" "$player_wav"

  AY_EMUL_ORACLE="$scenario" AY_EMUL_ORACLE_FILE="$CORPUS/$fname" \
    AY_EMUL_ORACLE_OUT="$oracle_wav" "$ORACLE_BIN" >"$WORKDIR/oracle.log" 2>&1
  if [ ! -s "$oracle_wav" ]; then
    echo "[FAIL] $fname ($scenario): Pascal side produced no output"
    cat "$WORKDIR/oracle.log"
    fail=$((fail+1))
    status=1
    return
  fi

  "$PLAYER_BIN" "$CORPUS/$fname" --wav="$player_wav" --frames="$frames" \
    >"$WORKDIR/player.log" 2>&1
  if [ ! -s "$player_wav" ]; then
    echo "[FAIL] $fname ($scenario): ay_player produced no output"
    cat "$WORKDIR/player.log"
    fail=$((fail+1))
    status=1
    return
  fi

  oracle_md5=$(md5sum "$oracle_wav" | cut -d' ' -f1)
  player_md5=$(md5sum "$player_wav" | cut -d' ' -f1)
  if [ "$oracle_md5" = "$player_md5" ]; then
    echo "[PASS] $fname ($scenario): md5=$oracle_md5"
    pass=$((pass+1))
  else
    echo "[FAIL] $fname ($scenario): oracle md5=$oracle_md5 player md5=$player_md5"
    fail=$((fail+1))
    status=1
  fi
}

cd "$CORPUS" || exit 1
for f in *; do
  case "$f" in
    README.md)
      continue
      ;;
    *.pt3) run_one "$f" wav_export_pt3 204800 ;;
    *.ym) run_one "$f" wav_export_ym 204800 ;;
    *.vtx|*.VTX) run_one "$f" wav_export 204800 ;;
    *.ay) run_one "$f" wav_export_ay 512 ;;
    *.pt1) run_one "$f" wav_export_pt1 441344 ;;
    *.gtr) run_one "$f" wav_export_gtr 441344 ;;
    *.fls) run_one "$f" wav_export_fls 441344 ;;
    *.stc) run_one "$f" wav_export_stc 441344 ;;
    *.stp) run_one "$f" wav_export_stp 441344 ;;
    *.pt2) run_one "$f" wav_export_pt2 441344 ;;
    *.fxm) run_one "$f" wav_export_fxm 441344 ;;
    *.psm) run_one "$f" wav_export_psm 441344 ;;
    *.asc) run_one "$f" wav_export_asc 441344 ;;
    *.as0) run_one "$f" wav_export_asc0 441344 ;;
    *.ftc) run_one "$f" wav_export_ftc 441344 ;;
    *.psc) run_one "$f" wav_export_psc 441344 ;;
    *.sqt) run_one "$f" wav_export_sqt 441344 ;;
    *.sndh)
      echo "[SKIP] $f: SNDH WAV export not automatable (MIG-0021/MIG-0026 - Pascal-side MakeBufferSNDH is minutes-per-call slow)"
      skip=$((skip+1))
      ;;
    *)
      echo "[SKIP] $f: unrecognized extension"
      skip=$((skip+1))
      ;;
  esac
done

echo ""
echo "==== Summary: $pass passed, $fail failed, $skip skipped ===="
exit $status
