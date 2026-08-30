"""Return, risk and trend statistics computed over a series of :class:`Bar`.

Everything here works on plain Python floats so the analytics (and their tests)
run without pandas, numpy or a network connection.
"""

from __future__ import annotations

import math
from statistics import fmean, stdev
from typing import Sequence

from stocks.models import Bar, Summary

#: Trading days in a calendar year, used to annualise daily statistics.
TRADING_DAYS = 252

#: Calendar days in a year, averaged over the leap cycle.
DAYS_PER_YEAR = 365.25


def closes(bars: Sequence[Bar]) -> list[float]:
    """Return the close of every bar, in order."""
    return [bar.close for bar in bars]


def simple_returns(bars: Sequence[Bar]) -> list[float]:
    """Period-over-period simple returns.

    The result has one fewer element than ``bars``: the first bar has no
    predecessor to compare against.
    """
    prices = closes(bars)
    out: list[float] = []
    for previous, current in zip(prices, prices[1:]):
        if previous == 0:
            raise ValueError("cannot compute a return from a zero close")
        out.append(current / previous - 1.0)
    return out


def log_returns(bars: Sequence[Bar]) -> list[float]:
    """Continuously compounded returns, which add across periods."""
    prices = closes(bars)
    out: list[float] = []
    for previous, current in zip(prices, prices[1:]):
        if previous <= 0 or current <= 0:
            raise ValueError("log returns need strictly positive prices")
        out.append(math.log(current / previous))
    return out


def total_return(bars: Sequence[Bar]) -> float:
    """Growth from the first close to the last, as a fraction (0.1 == +10%)."""
    if len(bars) < 2:
        return 0.0
    first, last = bars[0].close, bars[-1].close
    if first == 0:
        raise ValueError("cannot compute a return from a zero close")
    return last / first - 1.0


def cagr(bars: Sequence[Bar]) -> float:
    """Compound annual growth rate implied by the first and last bar.

    Returns ``0.0`` when the window is too short to annualise meaningfully
    (fewer than two bars, or both bars on the same day).
    """
    if len(bars) < 2:
        return 0.0
    years = (bars[-1].date - bars[0].date).days / DAYS_PER_YEAR
    if years <= 0:
        return 0.0
    first, last = bars[0].close, bars[-1].close
    if first <= 0 or last <= 0:
        raise ValueError("CAGR needs strictly positive prices")
    return (last / first) ** (1.0 / years) - 1.0


def volatility(returns: Sequence[float], annualise: bool = True) -> float:
    """Standard deviation of returns, annualised by default.

    Returns ``0.0`` for fewer than two observations, where the sample standard
    deviation is undefined.
    """
    if len(returns) < 2:
        return 0.0
    daily = stdev(returns)
    return daily * math.sqrt(TRADING_DAYS) if annualise else daily


def sharpe(returns: Sequence[float], risk_free: float = 0.0) -> float:
    """Annualised Sharpe ratio.

    ``risk_free`` is an annual rate (``0.04`` for 4%); it is spread evenly over
    the trading year before being subtracted from each observation. Returns
    ``0.0`` when the returns have no dispersion to divide by.
    """
    if len(returns) < 2:
        return 0.0
    daily_rf = risk_free / TRADING_DAYS
    excess = [r - daily_rf for r in returns]
    spread = stdev(excess)
    if spread == 0:
        return 0.0
    return fmean(excess) / spread * math.sqrt(TRADING_DAYS)


def max_drawdown(bars: Sequence[Bar]) -> float:
    """Deepest peak-to-trough fall in close price, as a negative fraction.

    ``-0.25`` means the series lost a quarter of its value from its running
    high before recovering (or before the window ended).
    """
    worst = 0.0
    peak = -math.inf
    for price in closes(bars):
        peak = max(peak, price)
        if peak > 0:
            worst = min(worst, price / peak - 1.0)
    return worst


def moving_average(values: Sequence[float], window: int) -> list[float | None]:
    """Trailing simple moving average, aligned to ``values``.

    The first ``window - 1`` entries are ``None`` because the window is not yet
    full. Computed with a running sum, so cost is linear in ``len(values)``.
    """
    if window < 1:
        raise ValueError(f"window must be at least 1, got {window}")
    out: list[float | None] = []
    running = 0.0
    for index, value in enumerate(values):
        running += value
        if index >= window:
            running -= values[index - window]
        out.append(running / window if index >= window - 1 else None)
    return out


def summarize(ticker: str, bars: Sequence[Bar]) -> Summary:
    """Bundle the headline statistics for one ticker into a :class:`Summary`."""
    if not bars:
        raise ValueError(f"{ticker}: no bars to summarise")
    returns = simple_returns(bars)
    return Summary(
        ticker=ticker,
        start=bars[0].date,
        end=bars[-1].date,
        bars=len(bars),
        first_close=bars[0].close,
        last_close=bars[-1].close,
        total_return=total_return(bars),
        cagr=cagr(bars),
        volatility=volatility(returns),
        sharpe=sharpe(returns),
        max_drawdown=max_drawdown(bars),
    )
