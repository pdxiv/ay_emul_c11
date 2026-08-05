#!/bin/sh
# Ad hoc, large-scale (tens of thousands of files) corpus divergence sweep
# against an external directory of AY/tracker music files - NOT part of the
# tracked, permanent regression suite (unlike corpus_wav_md5_sweep.sh, which
# targets test_corpus_76/ and IS committed). Produces a CSV report (one row
# per file) rather than PASS/FAIL lines, so the results can be sorted/
# ranked by divergence severity afterward rather than read directly - see
# rank_all_tunes.py in this same directory.
#
# Usage: all_tunes_sweep.sh <input-dir> <output-csv> [frames]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ORACLE_BIN="$ROOT/ay_emul/lib/x86_64-linux/Ay_Emul"
PLAYER_BIN="$ROOT/tools/ay_player/ay_player"
IDENTIFY_BIN="$ROOT/tools/identify_ay_file/identify_ay_file"

INPUT_DIR="${1:?usage: all_tunes_sweep.sh <input-dir> <output-csv> [frames]}"
OUT_CSV="${2:?usage: all_tunes_sweep.sh <input-dir> <output-csv> [frames]}"
FRAMES="${3:-480000}"   # 10s at 48kHz by default

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

scenario_for_ext() {
  case "$1" in
    pt3) echo wav_export_pt3 ;;
    ym) echo wav_export_ym ;;
    vtx|VTX) echo wav_export ;;
    ay) echo wav_export_ay ;;
    sndh) echo wav_export_sndh ;;
    pt1) echo wav_export_pt1 ;;
    gtr) echo wav_export_gtr ;;
    fls) echo wav_export_fls ;;
    stc) echo wav_export_stc ;;
    stp) echo wav_export_stp ;;
    pt2) echo wav_export_pt2 ;;
    fxm) echo wav_export_fxm ;;
    psm) echo wav_export_psm ;;
    asc) echo wav_export_asc ;;
    as0) echo wav_export_asc0 ;;
    ftc) echo wav_export_ftc ;;
    psc) echo wav_export_psc ;;
    sqt) echo wav_export_sqt ;;
    *) echo "" ;;
  esac
}

# WAV_HEADER_BYTES: the fixed 44-byte RIFF/WAVE/fmt/data header both sides
# emit identically (verified: only the size fields inside it differ when
# the two renders have different total lengths - the oracle's per-format
# RunXXXWAVExportTest scenarios each have their OWN hardcoded internal
# buffer count, unrelated to this script's requested FRAMES, so oracle and
# c11 output are frequently different total lengths). Comparing raw bytes
# including the header would spuriously "differ" at the header's own size
# field before any real PCM content is even reached - skip it on both
# sides and compare only the overlapping PCM region, capped at the
# requested FRAMES*4 bytes (16-bit stereo), recording how many bytes were
# actually compared (compared_bytes) since it's sometimes LESS than the
# full 10s if the oracle's own hardcoded scenario length is shorter.
WAV_HEADER_BYTES=44
TARGET_PCM_BYTES=$((FRAMES * 4))

echo "path,ext,scenario,oracle_status,c11_status,oracle_bytes,c11_bytes,compared_bytes,diff_bytes,diff_fraction,first_diff_byte,oracle_md5,c11_md5,elapsed_ms" > "$OUT_CSV"

find "$INPUT_DIR" -type f | (
count=0
while IFS= read -r fpath; do
  count=$((count+1))
  if [ $((count % 200)) -eq 0 ]; then
    echo "progress: $count files processed" >&2
  fi
  fname=$(basename "$fpath")
  ext=$(printf '%s' "$fname" | sed 's/.*\.//' | tr 'A-Z' 'a-z')
  scenario=$(scenario_for_ext "$ext")

  t0=$(date +%s%N)

  if [ -z "$scenario" ]; then
    echo "\"$fpath\",$ext,,unsupported_ext,unsupported_ext,,,,,,,,,0" >> "$OUT_CSV"
    continue
  fi

  oracle_wav="$WORKDIR/oracle.wav"
  player_wav="$WORKDIR/player.wav"
  rm -f "$oracle_wav" "$player_wav"

  oracle_status="ok"
  timeout 20 env AY_EMUL_ORACLE="$scenario" AY_EMUL_ORACLE_FILE="$fpath" \
    AY_EMUL_ORACLE_OUT="$oracle_wav" "$ORACLE_BIN" >/dev/null 2>&1
  orc=$?
  if [ "$orc" = 124 ]; then
    oracle_status="timeout"
  elif [ ! -s "$oracle_wav" ]; then
    oracle_status="no_output_exit$orc"
  fi

  c11_status="ok"
  timeout 20 "$PLAYER_BIN" "$fpath" --wav="$player_wav" --frames="$FRAMES" >/dev/null 2>&1
  prc=$?
  if [ "$prc" = 124 ]; then
    c11_status="timeout"
  elif [ ! -s "$player_wav" ]; then
    c11_status="no_output_exit$prc"
  fi

  t1=$(date +%s%N)
  elapsed_ms=$(( (t1 - t0) / 1000000 ))

  if [ "$oracle_status" != "ok" ] || [ "$c11_status" != "ok" ]; then
    echo "\"$fpath\",$ext,$scenario,$oracle_status,$c11_status,,,,,,,,,$elapsed_ms" >> "$OUT_CSV"
    continue
  fi

  oracle_bytes=$(stat -c%s "$oracle_wav")
  player_bytes=$(stat -c%s "$player_wav")
  oracle_md5=$(md5sum "$oracle_wav" | cut -d' ' -f1)
  player_md5=$(md5sum "$player_wav" | cut -d' ' -f1)

  oracle_pcm=$((oracle_bytes - WAV_HEADER_BYTES))
  player_pcm=$((player_bytes - WAV_HEADER_BYTES))
  compared_bytes=$oracle_pcm
  [ "$player_pcm" -lt "$compared_bytes" ] && compared_bytes=$player_pcm
  [ "$TARGET_PCM_BYTES" -lt "$compared_bytes" ] && compared_bytes=$TARGET_PCM_BYTES

  if [ "$compared_bytes" -le 0 ]; then
    echo "\"$fpath\",$ext,$scenario,$oracle_status,$c11_status,$oracle_bytes,$player_bytes,0,,,,\"$oracle_md5\",\"$player_md5\",$elapsed_ms" >> "$OUT_CSV"
    continue
  fi

  if [ "$oracle_md5" = "$player_md5" ]; then
    diff_bytes=0
    first_diff="-1"
  else
    diff_bytes=$(cmp -l -i "${WAV_HEADER_BYTES}:${WAV_HEADER_BYTES}" -n "$compared_bytes" \
      "$oracle_wav" "$player_wav" 2>/dev/null | wc -l)
    first_diff=$(cmp -i "${WAV_HEADER_BYTES}:${WAV_HEADER_BYTES}" -n "$compared_bytes" \
      "$oracle_wav" "$player_wav" 2>/dev/null | sed -n 's/.*byte \([0-9]*\).*/\1/p')
    [ -z "$first_diff" ] && first_diff="-1"
  fi

  diff_fraction=$(awk -v d="$diff_bytes" -v t="$compared_bytes" 'BEGIN{ if(t>0) printf "%.6f", d/t; else print "0" }')

  echo "\"$fpath\",$ext,$scenario,$oracle_status,$c11_status,$oracle_bytes,$player_bytes,$compared_bytes,$diff_bytes,$diff_fraction,$first_diff,$oracle_md5,$player_md5,$elapsed_ms" >> "$OUT_CSV"
done
echo "progress: $count files processed (final)" >&2
)

echo "Done. Report at: $OUT_CSV" >&2
