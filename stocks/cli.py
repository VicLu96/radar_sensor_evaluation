"""Command line entry point: ``python -m stocks ...``."""

from __future__ import annotations

import argparse
import re
import sys
from datetime import date, timedelta
from typing import Sequence

from stocks import __version__
from stocks.analysis import moving_average, summarize
from stocks.formatting import count, money, percent, ratio, render_table
from stocks.models import Bar
from stocks.providers import Provider, ProviderError, get_provider

#: Multipliers for the ``--period`` suffixes, in calendar days.
_PERIOD_UNITS = {"d": 1, "w": 7, "mo": 30, "m": 30, "y": 365}

_PERIOD_RE = re.compile(r"^(\d+)(d|w|mo|m|y)$", re.IGNORECASE)


def resolve_period(period: str, today: date | None = None) -> date | None:
    """Turn a period such as ``6mo`` into the start date it implies.

    ``max`` (or ``all``) returns ``None``, meaning "no lower bound".
    """
    today = today or date.today()
    normalised = period.strip().lower()
    if normalised in {"max", "all"}:
        return None
    match = _PERIOD_RE.match(normalised)
    if not match:
        raise ValueError(f"unrecognised period {period!r} (try 30d, 6mo, 2y or max)")
    amount, unit = int(match.group(1)), match.group(2)
    return today - timedelta(days=amount * _PERIOD_UNITS[unit])


def parse_date(text: str) -> date:
    """argparse type for ``--start``/``--end``."""
    try:
        return date.fromisoformat(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected an ISO date like 2024-01-31, got {text!r}") from exc


def _window(args: argparse.Namespace) -> tuple[date | None, date | None]:
    """Resolve the requested window; an explicit ``--start`` wins over ``--period``."""
    start = args.start if args.start else resolve_period(args.period)
    return start, args.end


def _provider(args: argparse.Namespace) -> Provider:
    return get_provider(args.source, csv_dir=args.csv_dir, period=args.period)


def cmd_quote(args: argparse.Namespace) -> int:
    """Print the latest close for each ticker, with its one-day change."""
    provider = _provider(args)
    start, end = _window(args)
    rows: list[list[str]] = []
    for ticker in args.tickers:
        bars = provider.history(ticker, start, end)
        if not bars:
            raise ProviderError(f"{ticker}: no bars in the requested window")
        last = bars[-1]
        previous = bars[-2].close if len(bars) > 1 else last.close
        change = last.close - previous
        pct = change / previous if previous else 0.0
        rows.append(
            [
                ticker.upper(),
                last.date.isoformat(),
                money(last.close),
                f"{change:+,.2f}",
                percent(pct),
                count(last.volume),
            ]
        )
    print(render_table(["TICKER", "DATE", "CLOSE", "CHANGE", "CHANGE %", "VOLUME"], rows))
    return 0


def cmd_history(args: argparse.Namespace) -> int:
    """Print the OHLCV table for one ticker, optionally with moving averages."""
    provider = _provider(args)
    start, end = _window(args)
    bars = provider.history(args.ticker, start, end)
    if not bars:
        raise ProviderError(f"{args.ticker}: no bars in the requested window")

    windows = sorted(set(args.ma))
    # Averages are computed over the full window, then sliced alongside the
    # bars, so a --tail view still shows a fully-warmed average.
    averages = {size: moving_average([bar.close for bar in bars], size) for size in windows}

    visible = range(len(bars))
    if args.tail:
        visible = range(max(0, len(bars) - args.tail), len(bars))

    headers = ["DATE", "OPEN", "HIGH", "LOW", "CLOSE", "VOLUME"]
    headers += [f"MA{size}" for size in windows]
    rows = [_history_row(bars[i], windows, averages, i) for i in visible]
    print(render_table(headers, rows))
    return 0


def _history_row(
    bar: Bar,
    windows: Sequence[int],
    averages: dict[int, list[float | None]],
    index: int,
) -> list[str]:
    row = [
        bar.date.isoformat(),
        money(bar.open),
        money(bar.high),
        money(bar.low),
        money(bar.close),
        count(bar.volume),
    ]
    for size in windows:
        value = averages[size][index]
        row.append(money(value) if value is not None else "-")
    return row


def cmd_stats(args: argparse.Namespace) -> int:
    """Print return and risk statistics for each ticker."""
    provider = _provider(args)
    start, end = _window(args)
    rows: list[list[str]] = []
    for ticker in args.tickers:
        bars = provider.history(ticker, start, end)
        if not bars:
            raise ProviderError(f"{ticker}: no bars in the requested window")
        summary = summarize(ticker.upper(), bars)
        rows.append(
            [
                summary.ticker,
                summary.start.isoformat(),
                summary.end.isoformat(),
                count(summary.bars),
                money(summary.first_close),
                money(summary.last_close),
                percent(summary.total_return),
                percent(summary.cagr),
                percent(summary.volatility),
                ratio(summary.sharpe),
                percent(summary.max_drawdown),
            ]
        )
    headers = [
        "TICKER", "START", "END", "BARS", "FIRST", "LAST",
        "RETURN", "CAGR", "VOL", "SHARPE", "MAX DD",
    ]
    print(render_table(headers, rows))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="stocks",
        description="Fetch and analyse daily stock prices.",
    )
    parser.add_argument("--version", action="version", version=f"stocks {__version__}")

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--source",
        choices=["csv", "yfinance"],
        default="csv",
        help="where prices come from (default: csv, the bundled sample data)",
    )
    common.add_argument(
        "--csv-dir",
        default="data",
        help="directory of <TICKER>.csv files for --source csv (default: data)",
    )
    common.add_argument(
        "--period",
        default="1y",
        help="window ending today, e.g. 30d, 6mo, 2y, max (default: 1y)",
    )
    common.add_argument("--start", type=parse_date, help="inclusive start date, overrides --period")
    common.add_argument("--end", type=parse_date, help="inclusive end date")

    subparsers = parser.add_subparsers(dest="command", required=True)

    quote = subparsers.add_parser("quote", parents=[common], help="latest close and one-day change")
    quote.add_argument("tickers", nargs="+", metavar="TICKER")
    quote.set_defaults(func=cmd_quote)

    history = subparsers.add_parser("history", parents=[common], help="OHLCV table for one ticker")
    history.add_argument("ticker", metavar="TICKER")
    history.add_argument(
        "--ma",
        type=int,
        action="append",
        default=[],
        metavar="N",
        help="add an N-day moving average column (repeatable)",
    )
    history.add_argument("--tail", type=int, metavar="N", help="show only the last N rows")
    history.set_defaults(func=cmd_history)

    stats = subparsers.add_parser("stats", parents=[common], help="return and risk statistics")
    stats.add_argument("tickers", nargs="+", metavar="TICKER")
    stats.set_defaults(func=cmd_stats)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ProviderError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
