"""Core data types shared by the providers, the analytics and the CLI."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date


@dataclass(frozen=True)
class Bar:
    """A single daily OHLCV observation for one ticker."""

    date: date
    open: float
    high: float
    low: float
    close: float
    volume: int

    def __post_init__(self) -> None:
        if self.high < self.low:
            raise ValueError(f"{self.date}: high {self.high} is below low {self.low}")
        if not self.low <= self.close <= self.high:
            raise ValueError(f"{self.date}: close {self.close} outside [{self.low}, {self.high}]")
        if not self.low <= self.open <= self.high:
            raise ValueError(f"{self.date}: open {self.open} outside [{self.low}, {self.high}]")
        if self.volume < 0:
            raise ValueError(f"{self.date}: negative volume {self.volume}")


@dataclass(frozen=True)
class Summary:
    """Descriptive statistics for one ticker over a window of bars."""

    ticker: str
    start: date
    end: date
    bars: int
    first_close: float
    last_close: float
    total_return: float
    cagr: float
    volatility: float
    sharpe: float
    max_drawdown: float
