#!/usr/bin/env python3
# =============================================================================
# make_sample_data.py
# Generate SYNTHETIC OHLC bar data for demonstrating tools/otf_backtest.cpp.
#
#   python3 tools/make_sample_data.py --bars 6000 --out tools/sample_data.csv
#
# WARNING: This is randomly generated data with alternating trend/chop regimes.
# It is ONLY for exercising the backtester mechanics. Numbers produced from it
# say NOTHING about real strategy performance. Use real exported market data for
# any meaningful evaluation.
# =============================================================================
import argparse
import csv
import math
import random
from datetime import datetime, timedelta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bars", type=int, default=6000)
    ap.add_argument("--out", default="tools/sample_data.csv")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--tick", type=float, default=0.25)
    ap.add_argument("--start", type=float, default=5000.0)
    args = ap.parse_args()

    random.seed(args.seed)
    price = args.start
    t = datetime(2026, 1, 2, 9, 30, 0)
    rows = []

    # Regime switches between trending (drift) and choppy (no drift).
    drift = 0.0
    regime_left = 0
    for _ in range(args.bars):
        if regime_left <= 0:
            regime_left = random.randint(80, 260)
            if random.random() < 0.55:                     # trending regime
                drift = random.choice([-1, 1]) * random.uniform(0.04, 0.14)
            else:                                          # choppy regime
                drift = 0.0
        regime_left -= 1

        vol = random.uniform(0.6, 1.6)
        o = price
        c = o + drift + random.gauss(0.0, vol)
        hi = max(o, c) + abs(random.gauss(0.0, vol * 0.6))
        lo = min(o, c) - abs(random.gauss(0.0, vol * 0.6))

        # snap to tick grid
        def snap(x):
            return round(round(x / args.tick) * args.tick, 4)

        o, hi, lo, c = snap(o), snap(hi), snap(lo), snap(c)
        hi = max(hi, o, c)
        lo = min(lo, o, c)

        rows.append((t.strftime("%Y-%m-%d %H:%M:%S"), o, hi, lo, c, random.randint(100, 5000)))
        price = c
        t += timedelta(minutes=1)

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["DateTime", "Open", "High", "Low", "Close", "Volume"])
        w.writerows(rows)

    print(f"Wrote {len(rows)} synthetic bars to {args.out}")


if __name__ == "__main__":
    main()
