"""Shared helpers for building test fixtures."""

from __future__ import annotations

from datetime import date, timedelta
from pathlib import Path
from typing import Sequence

from stocks.models import Bar

FIXTURES = Path(__file__).resolve().parent / "fixtures"


def bars_from_closes(closes: Sequence[float], start: date = date(2024, 1, 1)) -> list[Bar]:
    """Build one bar per close on consecutive days, with a plausible OHLC band."""
    return [
        Bar(
            date=start + timedelta(days=offset),
            open=close,
            high=close * 1.01,
            low=close * 0.99,
            close=close,
            volume=1_000 + offset,
        )
        for offset, close in enumerate(closes)
    ]
