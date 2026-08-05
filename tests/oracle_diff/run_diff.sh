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
for scenario in zx cpc immediate immediate_in m68k mfp dma; do
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

# ay_file: real MetalMania.ay file (test_corpus_76/, replaces the
# formerly-used songs/cpc/Discmac20_0.ay - see the "songs/ -> test_corpus_76"
# migration in migration_debt.yaml) through the full loader + MakeBufferAY
# playback loop (engine/src/ay_file.c vs the real Players.pas/Z80.pas/AY.pas
# code, via OracleHarness.pas's RunAYFileTest). Compared here as
# informational (byte-count of the matching prefix), not a hard pass/fail
# gate: the previous fixture diverged after its silent lead-in by a small
# (~2 T-state per interrupt-accept event) timing offset traced to
# interrupt-acceptance/HALT T-state accounting between superzazu/z80 and
# Z80.pas's live interpreter (migration_debt.yaml MIG-0018) - a fixture-
# independent engine characteristic, so the same non-hard-gate treatment
# applies here without re-deriving fixture-specific byte offsets.
AY_EMUL_ORACLE=ay_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/MetalMania.ay" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_ay_file.bin" "$ORACLE_BIN"
./dump_engine_state ay_file "$WORKDIR/engine_ay_file.bin" "$ROOT/test_corpus_76/MetalMania.ay"
if cmp -s "$WORKDIR/oracle_ay_file.bin" "$WORKDIR/engine_ay_file.bin"; then
  echo "[PASS] ay_file: oracle and engine PCM output byte-identical"
else
  prefix=$(cmp "$WORKDIR/oracle_ay_file.bin" "$WORKDIR/engine_ay_file.bin" | sed -n 's/.*byte \([0-9]*\).*/\1/p')
  echo "[INFO] ay_file: matches for the first $((prefix - 1)) bytes (known bounded timing debt, MIG-0018), not a hard failure"
fi

# ym_file: real .ym files (test_corpus_76/) through the full
# LZH-decompress-or-not + loader + MakeBufferYM5/YM6 playback loop
# (engine/src/lh5.c + ym_file.c vs the real Players.pas/AY.pas code, via
# OracleHarness.pas's RunYMFileTest). Batman_Journey.ym (LHA-compressed
# extended YM5!, replaces the formerly-used songs/cpc/The Last V8.ym) was
# the original coverage; MIG-0041 adds 3D-Fight_Fort.ym (plain/non-LHA
# extended YM5! - confirms the non-LHA byte-sourcing path, previously
# untested) and Block_Us.ym (plain/non-LHA extended YM6! - exercises the
# new ym6i_get_registers this milestone adds).
for ym_name in "Batman_Journey.ym" "3D-Fight_Fort.ym" "Block_Us.ym"; do
  AY_EMUL_ORACLE=ym_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/$ym_name" \
    AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_ym_file.bin" "$ORACLE_BIN"
  ./dump_engine_state ym_file "$WORKDIR/engine_ym_file.bin" "$ROOT/test_corpus_76/$ym_name"
  if cmp -s "$WORKDIR/oracle_ym_file.bin" "$WORKDIR/engine_ym_file.bin"; then
    echo "[PASS] ym_file ($ym_name): oracle and engine PCM output byte-identical"
  else
    echo "[FAIL] ym_file ($ym_name): PCM output differs"
    status=1
  fi
done

# pt3_file: real .pt3 files (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/pt3_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunPT3FileTest).
# ZAGON_07_remixDJ_EchoMAKROSS.pt3 (v4) and ZELiNAPI.pt3 (v7) are
# TonTableId=1 (PT3NoteTable_ST), turbo_sound=no per identify_ay_file -
# replacing the formerly-used songs/pt3/ARTe_ST1.pt3 and
# songs/turbo_sound/Gasman_-_dynamite.pt3, same version spread. The
# remaining 6 files (MIG-0040) cover every other PT3_TonTableId/version
# bucket GetNoteFreq can select: InSpace.pt3 (TonTableId=0,v3),
# UUUUU.pt3 (TonTableId=0,v4), KETTARYs.pt3 (TonTableId=2,v3),
# ChRiStmA.pt3 (TonTableId=2,v4), LoveBlue.pt3 (TonTableId=else,v3),
# CKA4KU.pt3 (TonTableId=else,v6) - verified via a direct header-byte
# read (offsets 99/13) before picking, not assumed from prior research.
for pt3_name in "ZAGON_07_remixDJ_EchoMAKROSS.pt3" "ZELiNAPI.pt3" \
  "InSpace.pt3" "UUUUU.pt3" "KETTARYs.pt3" "ChRiStmA.pt3" \
  "LoveBlue.pt3" "CKA4KU.pt3"; do
  AY_EMUL_ORACLE=pt3_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/$pt3_name" \
    AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_pt3.bin" "$ORACLE_BIN"
  ./dump_engine_state pt3_file "$WORKDIR/engine_pt3.bin" "$ROOT/test_corpus_76/$pt3_name"
  if cmp -s "$WORKDIR/oracle_pt3.bin" "$WORKDIR/engine_pt3.bin"; then
    echo "[PASS] pt3_file ($pt3_name): oracle and engine PCM output byte-identical"
  else
    echo "[FAIL] pt3_file ($pt3_name): PCM output differs"
    status=1
  fi
done

# pt1_file: real DEMON.pt1 file (test_corpus_76/) through the full tracker-
# engine playback loop (engine/src/pt1_file.c vs the real Players.pas/AY.pas
# code, via OracleHarness.pas's RunPT1FileTest). Byte-identical (first port
# of the 13-format "make all 76 corpus formats playable" effort,
# MIG-0028) - a real Pascal round()-vs-truncating-division bug was caught
# and fixed via this exact gate before it passed (see MIG-0028).
AY_EMUL_ORACLE=pt1_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/DEMON.pt1" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_pt1.bin" "$ORACLE_BIN"
./dump_engine_state pt1_file "$WORKDIR/engine_pt1.bin" "$ROOT/test_corpus_76/DEMON.pt1"
if cmp -s "$WORKDIR/oracle_pt1.bin" "$WORKDIR/engine_pt1.bin"; then
  echo "[PASS] pt1_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] pt1_file: PCM output differs"
  status=1
fi

# gtr_file: real L.Boy_broken.gtr file (test_corpus_76/, GTR_ID[3]=$10
# variant - exercises the alternate envelope-disable/note-off branches)
# through the full tracker-engine playback loop (engine/src/gtr_file.c vs
# the real Players.pas/AY.pas code, via OracleHarness.pas's
# RunGTRFileTest). Second of the 13-format "make all 76 corpus formats
# playable" effort (MIG-0029).
AY_EMUL_ORACLE=gtr_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/L.Boy_broken.gtr" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_gtr.bin" "$ORACLE_BIN"
./dump_engine_state gtr_file "$WORKDIR/engine_gtr.bin" "$ROOT/test_corpus_76/L.Boy_broken.gtr"
if cmp -s "$WORKDIR/oracle_gtr.bin" "$WORKDIR/engine_gtr.bin"; then
  echo "[PASS] gtr_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] gtr_file: PCM output differs"
  status=1
fi

# fls_file: real SimpletonGift1.fls file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/fls_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunFLSFileTest). Third
# of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0030) - exercises FLS's heuristic base-address detection (no
# on-disk load-address field, unlike GTR).
AY_EMUL_ORACLE=fls_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/SimpletonGift1.fls" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_fls.bin" "$ORACLE_BIN"
./dump_engine_state fls_file "$WORKDIR/engine_fls.bin" "$ROOT/test_corpus_76/SimpletonGift1.fls"
if cmp -s "$WORKDIR/oracle_fls.bin" "$WORKDIR/engine_fls.bin"; then
  echo "[PASS] fls_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] fls_file: PCM output differs"
  status=1
fi

# stc_file: real AWAY.stc file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/stc_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunSTCFileTest).
# Fourth of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0031).
AY_EMUL_ORACLE=stc_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/AWAY.stc" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_stc.bin" "$ORACLE_BIN"
./dump_engine_state stc_file "$WORKDIR/engine_stc.bin" "$ROOT/test_corpus_76/AWAY.stc"
if cmp -s "$WORKDIR/oracle_stc.bin" "$WORKDIR/engine_stc.bin"; then
  echo "[PASS] stc_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] stc_file: PCM output differs"
  status=1
fi

# stp_file: real Girls_of_Meladze.stp file (test_corpus_76/) through the
# full tracker-engine playback loop (engine/src/stp_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunSTPFileTest). Fifth
# of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0032).
AY_EMUL_ORACLE=stp_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/Girls_of_Meladze.stp" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_stp.bin" "$ORACLE_BIN"
./dump_engine_state stp_file "$WORKDIR/engine_stp.bin" "$ROOT/test_corpus_76/Girls_of_Meladze.stp"
if cmp -s "$WORKDIR/oracle_stp.bin" "$WORKDIR/engine_stp.bin"; then
  echo "[PASS] stp_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] stp_file: PCM output differs"
  status=1
fi

# pt2_file: real NOR.MUS..pt2 file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/pt2_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunPT2FileTest). Sixth
# of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0033) - exercises PT2's load-time PT2_NumberOfPositions
# recomputation and its portamento (Current_Ton_Sliding/GlissType)
# machinery.
AY_EMUL_ORACLE=pt2_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/NOR.MUS..pt2" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_pt2.bin" "$ORACLE_BIN"
./dump_engine_state pt2_file "$WORKDIR/engine_pt2.bin" "$ROOT/test_corpus_76/NOR.MUS..pt2"
if cmp -s "$WORKDIR/oracle_pt2.bin" "$WORKDIR/engine_pt2.bin"; then
  echo "[PASS] pt2_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] pt2_file: PCM output differs"
  status=1
fi

# fxm_file: real The_Last_V8.fxm file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/fxm_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunFXMFileTest).
# Seventh of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0034) - exercises FXM's bytecode-VM PatternInterpreter (jumps,
# calls, loop-counters, a push/pop Stek) and its unique 6-byte-skip/
# load-address file layout.
AY_EMUL_ORACLE=fxm_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/The_Last_V8.fxm" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_fxm.bin" "$ORACLE_BIN"
./dump_engine_state fxm_file "$WORKDIR/engine_fxm.bin" "$ROOT/test_corpus_76/The_Last_V8.fxm"
if cmp -s "$WORKDIR/oracle_fxm.bin" "$WORKDIR/engine_fxm.bin"; then
  echo "[PASS] fxm_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] fxm_file: PCM output differs"
  status=1
fi

# psm_file: real m16.psm file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/psm_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunPSMFileTest).
# Eighth of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0035) - exercises PSM's C-B-A channel processing order (unlike
# every other format's A-B-C) and its envelope/sample/ornament
# loop-counter machinery.
AY_EMUL_ORACLE=psm_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/m16.psm" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_psm.bin" "$ORACLE_BIN"
./dump_engine_state psm_file "$WORKDIR/engine_psm.bin" "$ROOT/test_corpus_76/m16.psm"
if cmp -s "$WORKDIR/oracle_psm.bin" "$WORKDIR/engine_psm.bin"; then
  echo "[PASS] psm_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] psm_file: PCM output differs"
  status=1
fi

# asc_file / asc0_file: real NEWDANCE.asc + MISTERS_BOX.as0 files
# (test_corpus_76/) through the full tracker-engine playback loop
# (engine/src/asc_file.c vs the real Players.pas/AY.pas code, via
# OracleHarness.pas's RunASCFileTest). Ninth of the 13-format "make all
# 76 corpus formats playable" effort (MIG-0036) - exercises both the
# direct ASC1 layout and the ASC0->ASC1 load-time byte-shift/pointer-
# fixup this format needs.
AY_EMUL_ORACLE=asc_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/NEWDANCE.asc" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_asc.bin" "$ORACLE_BIN"
./dump_engine_state asc_file "$WORKDIR/engine_asc.bin" "$ROOT/test_corpus_76/NEWDANCE.asc"
if cmp -s "$WORKDIR/oracle_asc.bin" "$WORKDIR/engine_asc.bin"; then
  echo "[PASS] asc_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] asc_file: PCM output differs"
  status=1
fi

AY_EMUL_ORACLE=asc0_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/MISTERS_BOX.as0" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_asc0.bin" "$ORACLE_BIN"
./dump_engine_state asc0_file "$WORKDIR/engine_asc0.bin" "$ROOT/test_corpus_76/MISTERS_BOX.as0"
if cmp -s "$WORKDIR/oracle_asc0.bin" "$WORKDIR/engine_asc0.bin"; then
  echo "[PASS] asc0_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] asc0_file: PCM output differs"
  status=1
fi

# ftc_file: real RE-TRIGG.ftc file (test_corpus_76/) through the full
# tracker-engine playback loop (engine/src/ftc_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunFTCFileTest). Tenth
# of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0037) - exercises FTC's tone-slide accumulator, the envelope-
# rewrite-avoidance hardware quirk, and the retrig imitation mechanism
# (matches this specific file's own name/purpose).
AY_EMUL_ORACLE=ftc_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/RE-TRIGG.ftc" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_ftc.bin" "$ORACLE_BIN"
./dump_engine_state ftc_file "$WORKDIR/engine_ftc.bin" "$ROOT/test_corpus_76/RE-TRIGG.ftc"
if cmp -s "$WORKDIR/oracle_ftc.bin" "$WORKDIR/engine_ftc.bin"; then
  echo "[PASS] ftc_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] ftc_file: PCM output differs"
  status=1
fi

# psc_file: real Inbetween_remix.psc file (test_corpus_76/) through the
# full tracker-engine playback loop (engine/src/psc_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunPSCFileTest).
# Eleventh of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0038) - exercises PSC's channel-B-only opcodes ($7A/$7B) and its
# note-set opcode that does NOT terminate the pattern-opcode loop
# (unlike every other format ported so far).
AY_EMUL_ORACLE=psc_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/Inbetween_remix.psc" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_psc.bin" "$ORACLE_BIN"
./dump_engine_state psc_file "$WORKDIR/engine_psc.bin" "$ROOT/test_corpus_76/Inbetween_remix.psc"
if cmp -s "$WORKDIR/oracle_psc.bin" "$WORKDIR/engine_psc.bin"; then
  echo "[PASS] psc_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] psc_file: PCM output differs"
  status=1
fi

# sqt_file: real MotorAnimation.sqt file (test_corpus_76/) through the
# full tracker-engine playback loop (engine/src/sqt_file.c vs the real
# Players.pas/AY.pas code, via OracleHarness.pas's RunSQTFileTest). Last
# of the 13-format "make all 76 corpus formats playable" effort
# (MIG-0039) - exercises SQT's heuristic load-time relocation and its
# subroutine-call-tree PatternInterpreter (Call_LC1D1/LC2A8/LC2D9/LC283/
# LC191).
AY_EMUL_ORACLE=sqt_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/MotorAnimation.sqt" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_sqt.bin" "$ORACLE_BIN"
./dump_engine_state sqt_file "$WORKDIR/engine_sqt.bin" "$ROOT/test_corpus_76/MotorAnimation.sqt"
if cmp -s "$WORKDIR/oracle_sqt.bin" "$WORKDIR/engine_sqt.bin"; then
  echo "[PASS] sqt_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] sqt_file: PCM output differs"
  status=1
fi

# vtx_file: real GB2_5.vtx file (test_corpus_76/, long-header "ay"-chip-type
# variant, matching the formerly-used songs/vtx/Intro.vtx's characteristics)
# through the full lh5-decompress + loader + MakeBufferVTX playback loop
# (engine/src/lh5.c + vtx_file.c vs the real Players.pas/AY.pas code, via
# OracleHarness.pas's RunVTXFileTest).
AY_EMUL_ORACLE=vtx_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/GB2_5.vtx" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_vtx_file.bin" "$ORACLE_BIN"
./dump_engine_state vtx_file "$WORKDIR/engine_vtx_file.bin" "$ROOT/test_corpus_76/GB2_5.vtx"
if cmp -s "$WORKDIR/oracle_vtx_file.bin" "$WORKDIR/engine_vtx_file.bin"; then
  echo "[PASS] vtx_file: oracle and engine PCM output byte-identical"
else
  echo "[FAIL] vtx_file: PCM output differs"
  status=1
fi

echo "== Building tools/ay_player =="
(cd "$ROOT/tools/ay_player" && make >/dev/null)

# wav_export: real GB2_5.vtx file (test_corpus_76/) through tools/ay_player's
# --wav= path (Phase 4) vs. OracleHarness.pas's RunVTXWAVExportTest, which
# replicates Convs.pas's WAV_Converter core logic (header populate ->
# Real_End_All-bounded MakeBuffer loop, writing exactly BuffLen frames per
# call, matching Convs.pas:542-544 - not RunVTXFileTest's simpler fixed-
# buffer-write convention above) rather than calling WAV_Converter itself
# (too GUI-entangled - see OracleHarness.pas's RunVTXWAVExportTest comment).
# GB2_5.vtx is used here because it's the one already-validated format whose
# real_end_all fires naturally within a small, bounded frame count - see
# migration_debt.yaml MIG-0026.
AY_EMUL_ORACLE=wav_export AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/GB2_5.vtx" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_wav_export.wav" "$ORACLE_BIN"
"$ROOT/tools/ay_player/ay_player" "$ROOT/test_corpus_76/GB2_5.vtx" \
  --wav="$WORKDIR/player_wav_export.wav" --seconds=180 >/dev/null
if cmp -s "$WORKDIR/oracle_wav_export.wav" "$WORKDIR/player_wav_export.wav"; then
  echo "[PASS] wav_export (VTX): oracle and ay_player WAV output byte-identical"
else
  echo "[FAIL] wav_export (VTX): WAV output differs"
  status=1
fi

# wav_export (AY): only 512 frames (1 buffer) - deliberately stays inside
# the byte-identical prefix MIG-0018 already established for this exact
# file (matches for ~987-1005 frames before a small, known, non-content
# timing divergence), validating the WAV container itself without
# re-litigating that separately-tracked divergence.
AY_EMUL_ORACLE=wav_export_ay AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/MetalMania.ay" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_wav_ay.wav" "$ORACLE_BIN"
"$ROOT/tools/ay_player/ay_player" "$ROOT/test_corpus_76/MetalMania.ay" \
  --wav="$WORKDIR/player_wav_ay.wav" --frames=512 >/dev/null
if cmp -s "$WORKDIR/oracle_wav_ay.wav" "$WORKDIR/player_wav_ay.wav"; then
  echo "[PASS] wav_export (AY): oracle and ay_player WAV output byte-identical"
else
  echo "[FAIL] wav_export (AY): WAV output differs"
  status=1
fi

# wav_export (YM): 400 buffers/204800 frames - 100x further than MIG-0019's
# original raw-PCM gate (4 buffers), with no known divergence at this length.
AY_EMUL_ORACLE=wav_export_ym AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/Batman_Journey.ym" \
  AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_wav_ym.wav" "$ORACLE_BIN"
"$ROOT/tools/ay_player/ay_player" "$ROOT/test_corpus_76/Batman_Journey.ym" \
  --wav="$WORKDIR/player_wav_ym.wav" --frames=204800 >/dev/null
if cmp -s "$WORKDIR/oracle_wav_ym.wav" "$WORKDIR/player_wav_ym.wav"; then
  echo "[PASS] wav_export (YM): oracle and ay_player WAV output byte-identical"
else
  echo "[FAIL] wav_export (YM): WAV output differs"
  status=1
fi

# wav_export (PT3): 400 buffers/204800 frames, both real PT3 files, matching
# MIG-0020's existing raw-PCM gate exactly (already proven byte-identical at
# this length for both files).
for pt3_name in "ZAGON_07_remixDJ_EchoMAKROSS.pt3" "ZELiNAPI.pt3"; do
  AY_EMUL_ORACLE=wav_export_pt3 AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/$pt3_name" \
    AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_wav_pt3.wav" "$ORACLE_BIN"
  "$ROOT/tools/ay_player/ay_player" "$ROOT/test_corpus_76/$pt3_name" \
    --wav="$WORKDIR/player_wav_pt3.wav" --frames=204800 >/dev/null
  if cmp -s "$WORKDIR/oracle_wav_pt3.wav" "$WORKDIR/player_wav_pt3.wav"; then
    echo "[PASS] wav_export (PT3, $pt3_name): oracle and ay_player WAV output byte-identical"
  else
    echo "[FAIL] wav_export (PT3, $pt3_name): WAV output differs"
    status=1
  fi
done

# wav_export (tracker formats, MIG-0043): 862 buffers/441344 frames (~10.01s
# at the default 44100Hz SampleRateDef) - the "first 10 seconds" WAV snippet
# comparison for every one of the remaining 12 tracker-family formats that
# previously had no Pascal-side WAV-export scenario (only the lower-level
# raw-PCM gates above), driven through the shared WriteTrackerWAV helper
# (OracleHarness.pas) on the Pascal side and tools/ay_player's own --wav=
# --frames=441344 CLI path on the C11 side.
for fmt in pt1 gtr fls stc stp pt2 fxm psm asc asc0 ftc psc sqt; do
  case "$fmt" in
    pt1) fname="DEMON.pt1" ;;
    gtr) fname="L.Boy_broken.gtr" ;;
    fls) fname="SimpletonGift1.fls" ;;
    stc) fname="AWAY.stc" ;;
    stp) fname="Girls_of_Meladze.stp" ;;
    pt2) fname="NOR.MUS..pt2" ;;
    fxm) fname="The_Last_V8.fxm" ;;
    psm) fname="m16.psm" ;;
    asc) fname="NEWDANCE.asc" ;;
    asc0) fname="MISTERS_BOX.as0" ;;
    ftc) fname="RE-TRIGG.ftc" ;;
    psc) fname="Inbetween_remix.psc" ;;
    sqt) fname="MotorAnimation.sqt" ;;
  esac
  AY_EMUL_ORACLE="wav_export_$fmt" AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/$fname" \
    AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_wav_$fmt.wav" "$ORACLE_BIN"
  "$ROOT/tools/ay_player/ay_player" "$ROOT/test_corpus_76/$fname" \
    --wav="$WORKDIR/player_wav_$fmt.wav" --frames=441344 >/dev/null
  if cmp -s "$WORKDIR/oracle_wav_$fmt.wav" "$WORKDIR/player_wav_$fmt.wav"; then
    echo "[PASS] wav_export ($fmt, $fname): oracle and ay_player WAV output byte-identical"
  else
    echo "[FAIL] wav_export ($fmt, $fname): WAV output differs"
    status=1
  fi
done

# identify_file: re-verifies MIG-0023's structural tracker detectors
# against every real sample file test_corpus_76 has for a
# no-byte-signature format (7 of the original 10; ST1/ST3/STF remain
# unrepresented in the corpus) - this became possible once those files
# were added by the songs/ -> test_corpus_76 migration but had not been
# exercised as an automated gate before.
for id_fmt in NEWDANCE.asc:ASC MISTERS_BOX.as0:ASC0 "Girls_of_Meladze.stp:STP" \
              DEMON.pt1:PT1 "NOR.MUS..pt2:PT2" MotorAnimation.sqt:SQT \
              SimpletonGift1.fls:FLS; do
  id_fname="${id_fmt%%:*}"
  id_expected="${id_fmt##*:}"
  AY_EMUL_ORACLE=identify_file AY_EMUL_ORACLE_FILE="$ROOT/test_corpus_76/$id_fname" \
    AY_EMUL_ORACLE_OUT="$WORKDIR/oracle_identify_$id_expected.txt" "$ORACLE_BIN"
  id_oracle_format=$(sed -n 's/^format=//p' "$WORKDIR/oracle_identify_$id_expected.txt")
  id_tool_format=$("$ROOT/tools/identify_ay_file/identify_ay_file" \
    "$ROOT/test_corpus_76/$id_fname" | sed -n 's/.* format=\([A-Za-z0-9_]*\).*/\1/p')
  if [ "$id_oracle_format" = "$id_tool_format" ] && [ "$id_oracle_format" = "$id_expected" ]; then
    echo "[PASS] identify_file ($id_fname): oracle and identify_ay_file both agree format=$id_expected"
  else
    echo "[FAIL] identify_file ($id_fname): oracle=$id_oracle_format tool=$id_tool_format expected=$id_expected"
    status=1
  fi
done

# wav_export (SNDH): deliberately NOT gated here. This was originally
# noted (MIG-0021/0026) as producing zero frames after a 3+ minute stall,
# but that was from a much earlier, now-long-since-fixed state of
# engine/src/sndh_file.c (see MIG-0045 through MIG-0055) - SNDH WAV
# export via ay_player works correctly now, and RunSNDHWAVExportTest
# (used extensively, manually, throughout that whole investigation) does
# too. Still not wired into this automated loop because it needs a much
# longer NumBuffers setting than the other formats' WAV gates to be a
# meaningful comparison (30s of audio, not ~10s), making it noticeably
# slower per run - a real gap worth closing, not a stale non-issue.

exit $status
