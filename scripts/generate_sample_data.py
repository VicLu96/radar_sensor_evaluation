"""Regenerate the synthetic CSVs in ``data/``.

The series are a seeded geometric random walk, not real market data. They exist
so the CLI, the examples and the tests all have something to run against
without a network connection. Run with::

    python3 scripts/generate_sample_data.py
"""

from __future__ import annotations

import csv
import math
import random
from datetime import date, timedelta
from pathlib import Path

SEED = 20240617
END = date(2026, 8, 28)
TRADING_DAYS = 756  # roughly three years of weekdays

# ticker -> (starting price, annual drift, annual volatility, typical volume)
SERIES = {
    "AAPL": (150.0, 0.12, 0.26, 58_000_000),
    "MSFT": (330.0, 0.15, 0.24, 24_000_000),
    "NVDA": (280.0, 0.35, 0.48, 41_000_000),
    "SPY": (430.0, 0.09, 0.15, 76_000_000),
}


def trading_days(end: date, count: int) -> list[date]:
    """The ``count`` most recent weekdays ending on or before ``end``."""
    days: list[date] = []
    cursor = end
    while len(days) < count:
        if cursor.weekday() < 5:
            days.append(cursor)
        cursor -= timedelta(days=1)
    return sorted(days)


def build_rows(rng: random.Random, start_price: float, drift: float, vol: float,
               volume: int, days: list[date]) -> list[list[object]]:
    """Walk a price series forward and shape each step into an OHLCV row."""
    daily_drift = drift / 252 - 0.5 * (vol**2) / 252
    daily_vol = vol / math.sqrt(252)
    rows: list[list[object]] = []
    close = start_price
    for day in days:
        previous_close = close
        close = previous_close * math.exp(daily_drift + daily_vol * rng.gauss(0, 1))
        open_ = previous_close * (1 + rng.gauss(0, daily_vol * 0.3))
        high = max(open_, close) * (1 + abs(rng.gauss(0, daily_vol * 0.4)))
        low = min(open_, close) * (1 - abs(rng.gauss(0, daily_vol * 0.4)))
        rows.append(
            [
                day.isoformat(),
                round(open_, 2),
                round(high, 2),
                round(low, 2),
                round(close, 2),
                int(volume * rng.uniform(0.6, 1.6)),
            ]
        )
    return rows


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "data"
    out_dir.mkdir(exist_ok=True)
    days = trading_days(END, TRADING_DAYS)

    for ticker, (price, drift, vol, volume) in SERIES.items():
        rng = random.Random(f"{SEED}-{ticker}")
        rows = build_rows(rng, price, drift, vol, volume, days)
        path = out_dir / f"{ticker}.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["date", "open", "high", "low", "close", "volume"])
            writer.writerows(rows)
        print(f"wrote {path} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
