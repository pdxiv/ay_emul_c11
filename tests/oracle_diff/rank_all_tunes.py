#!/usr/bin/env python3
"""Post-processor for all_tunes_sweep.sh's CSV report.

The raw CSV (one row per file) is too large to read directly - this
ranks it by divergence severity so the worst cases can be singled out
for focused investigation, per the request that produced it.

Usage:
    rank_all_tunes.py <report.csv> [--top N] [--format EXT] [--summary]
"""
import argparse
import csv
import sys
from collections import defaultdict


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def to_float(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("--top", type=int, default=50,
                     help="how many worst-ranked rows to print (default 50)")
    ap.add_argument("--format", default=None,
                     help="restrict to one extension (e.g. sndh)")
    ap.add_argument("--summary", action="store_true",
                     help="print per-format/per-status summary instead of a ranked list")
    ap.add_argument("--min-compared", type=int, default=0,
                     help="ignore rows where compared_bytes is below this (default 0)")
    args = ap.parse_args()

    rows = load(args.csv_path)
    if args.format:
        rows = [r for r in rows if r["ext"].lower() == args.format.lower()]

    if args.summary:
        by_ext = defaultdict(lambda: {
            "total": 0, "ok_both": 0, "exact": 0, "diverged": 0,
            "unsupported": 0, "oracle_fail": 0, "c11_fail": 0,
            "diff_sum": 0.0,
        })
        for r in rows:
            b = by_ext[r["ext"]]
            b["total"] += 1
            if r["oracle_status"] == "unsupported_ext":
                b["unsupported"] += 1
                continue
            if r["oracle_status"] != "ok":
                b["oracle_fail"] += 1
                continue
            if r["c11_status"] != "ok":
                b["c11_fail"] += 1
                continue
            b["ok_both"] += 1
            frac = to_float(r["diff_fraction"])
            b["diff_sum"] += frac
            if frac == 0.0:
                b["exact"] += 1
            else:
                b["diverged"] += 1
        print(f"{'ext':6s} {'total':>7s} {'ok_both':>8s} {'exact':>7s} "
              f"{'diverged':>9s} {'unsupp':>7s} {'oracle_fail':>11s} "
              f"{'c11_fail':>9s} {'avg_diff_frac':>14s}")
        for ext, b in sorted(by_ext.items(), key=lambda kv: -kv[1]["total"]):
            avg = b["diff_sum"] / b["ok_both"] if b["ok_both"] else 0.0
            print(f"{ext:6s} {b['total']:>7d} {b['ok_both']:>8d} {b['exact']:>7d} "
                  f"{b['diverged']:>9d} {b['unsupported']:>7d} "
                  f"{b['oracle_fail']:>11d} {b['c11_fail']:>9d} {avg:>14.6f}")
        return

    # Ranked worst-first list. ASYMMETRIC failures (one side renders, the
    # other doesn't) rank highest - a real port-vs-oracle behavioral gap.
    # Both-sides-reject-identically (e.g. an unsupported YM sub-variant
    # neither implementation handles) is NOT a divergence - both agree,
    # deprioritized to the bottom rather than mixed in with real mismatches.
    # Among files both sides successfully rendered, sort by diff_fraction.
    def classify(r):
        ok_o = r["oracle_status"] == "ok"
        ok_c = r["c11_status"] == "ok"
        if ok_o != ok_c:
            return 3  # asymmetric - most concerning
        if ok_o and ok_c:
            return 2  # both rendered - rank by diff_fraction
        return 0  # both failed identically (or unsupported ext) - benign

    def sort_key(r):
        cls = classify(r)
        if cls == 2 and to_float(r.get("compared_bytes") or 0) < args.min_compared:
            cls = 1  # rendered but too little data to compare meaningfully
        return (cls, to_float(r["diff_fraction"]) if cls == 2 else 0.0)

    ranked = sorted(rows, key=sort_key, reverse=True)

    print(f"{'rank':>5s} {'ext':5s} {'diff_frac':>10s} {'first_diff':>11s} "
          f"{'oracle_st':>10s} {'c11_st':>10s} path")
    for i, r in enumerate(ranked[: args.top], 1):
        print(f"{i:>5d} {r['ext']:5s} {to_float(r['diff_fraction']):>10.6f} "
              f"{r['first_diff_byte']:>11s} {r['oracle_status']:>10s} "
              f"{r['c11_status']:>10s} {r['path']}")


if __name__ == "__main__":
    main()
