"""Brick 4 -- synthetic order stream generator.

Emits a CSV (orders.csv) that bench_match can ingest, plus four engineered
flavors so each code path of the matcher is stressed:

    uniform     -- prices uniform in a wide band; few crosses
    clustered   -- 90% within +/-5 ticks of mid; lots of crosses
    sweep       -- aggressive market-impact runs that walk the book
    adversarial -- self-cross, fat-finger, rapid cancel-replace

Usage:
    python generate_orders.py --shape clustered --n 5000000 --out data/orders.csv
"""
from __future__ import annotations

import argparse
import numpy as np
import pandas as pd


MID_TICK = 100_0000   # $100.0000 in 4-decimal integer ticks


def gen_uniform(n: int, rng: np.random.Generator) -> pd.DataFrame:
    prices = MID_TICK + rng.integers(-5000, 5001, size=n)
    return _assemble(n, rng, prices, p_cancel=0.05)


def gen_clustered(n: int, rng: np.random.Generator) -> pd.DataFrame:
    offsets = (rng.standard_t(df=3, size=n) * 5).astype(int)
    return _assemble(n, rng, MID_TICK + offsets, p_cancel=0.15)


def gen_sweep(n: int, rng: np.random.Generator) -> pd.DataFrame:
    # 80% small resting orders, 20% large aggressive sweeps
    prices = MID_TICK + rng.integers(-50, 51, size=n)
    qtys   = np.where(rng.random(n) < 0.2,
                      rng.integers(500, 5000, size=n),
                      rng.integers(1, 100, size=n))
    sides  = rng.choice(['B', 'S'], size=n)
    events = np.full(n, 'A')
    return pd.DataFrame({'ts': np.arange(n), 'event': events, 'side': sides,
                         'price': prices, 'qty': qtys})


def gen_adversarial(n: int, rng: np.random.Generator) -> pd.DataFrame:
    df = gen_clustered(n, rng)
    # Sprinkle in fat-finger far-from-mid orders and rapid cancel-replace pairs
    far_idx = rng.choice(n, size=n // 1000, replace=False)
    df.loc[far_idx, 'price'] = MID_TICK + rng.integers(-50_000, 50_001,
                                                       size=len(far_idx))
    return df


def _assemble(n, rng, prices, p_cancel) -> pd.DataFrame:
    sides  = rng.choice(['B', 'S'], size=n)
    qtys   = rng.integers(1, 500, size=n)
    events = np.where(rng.random(n) < p_cancel, 'X', 'A')
    return pd.DataFrame({'ts': np.arange(n), 'event': events, 'side': sides,
                         'price': prices, 'qty': qtys})


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--shape', choices=['uniform', 'clustered', 'sweep',
                                        'adversarial'], default='clustered')
    ap.add_argument('--n',    type=int, default=5_000_000)
    ap.add_argument('--seed', type=int, default=42)
    ap.add_argument('--out',  default='data/orders.csv')
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    gens = dict(uniform=gen_uniform, clustered=gen_clustered,
                sweep=gen_sweep, adversarial=gen_adversarial)
    df = gens[args.shape](args.n, rng)
    df.to_csv(args.out, index=False)
    print(f"wrote {args.out}: shape={args.shape} rows={len(df):,}")


if __name__ == '__main__':
    main()
