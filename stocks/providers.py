"""Sources of price history.

Two providers ship with the package:

``csv``
    Reads ``<TICKER>.csv`` out of a directory. Offline and deterministic, so it
    is what the tests and the bundled sample data use.
``yfinance``
    Live daily bars from Yahoo Finance. Needs the optional ``yfinance``
    dependency and network access.
"""

from __future__ import annotations

import csv
from abc import ABC, abstractmethod
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Iterable

from stocks.models import Bar

#: Header names accepted for each :class:`Bar` field, matched case-insensitively.
_CSV_ALIASES = {
    "date": ("date", "timestamp", "day"),
    "open": ("open", "o"),
    "high": ("high", "h"),
    "low": ("low", "l"),
    "close": ("close", "adj close", "adj_close", "c"),
    "volume": ("volume", "vol", "v"),
}


class ProviderError(RuntimeError):
    """Raised when a provider cannot deliver the requested history."""


class Provider(ABC):
    """Fetches daily bars for a ticker."""

    name: str

    @abstractmethod
    def history(
        self,
        ticker: str,
        start: date | None = None,
        end: date | None = None,
    ) -> list[Bar]:
        """Return daily bars for ``ticker``, oldest first.

        ``start`` and ``end`` are both inclusive; ``None`` means unbounded on
        that side.
        """

    def latest(self, ticker: str) -> Bar:
        """Return the most recent bar available for ``ticker``."""
        bars = self.history(ticker)
        if not bars:
            raise ProviderError(f"{ticker}: no bars available")
        return bars[-1]


def _window(bars: Iterable[Bar], start: date | None, end: date | None) -> list[Bar]:
    """Keep the bars falling inside the inclusive ``[start, end]`` window."""
    return [
        bar
        for bar in bars
        if (start is None or bar.date >= start) and (end is None or bar.date <= end)
    ]


class CsvProvider(Provider):
    """Reads price history from ``<directory>/<TICKER>.csv``.

    The header is matched case-insensitively against a handful of common
    spellings, so files exported from Yahoo Finance or Stooq load unchanged.
    """

    name = "csv"

    def __init__(self, directory: str | Path) -> None:
        self.directory = Path(directory)

    def path_for(self, ticker: str) -> Path:
        return self.directory / f"{ticker.upper()}.csv"

    def available(self) -> list[str]:
        """Tickers this provider can serve, sorted."""
        if not self.directory.is_dir():
            return []
        return sorted(path.stem.upper() for path in self.directory.glob("*.csv"))

    def history(
        self,
        ticker: str,
        start: date | None = None,
        end: date | None = None,
    ) -> list[Bar]:
        path = self.path_for(ticker)
        if not path.is_file():
            known = ", ".join(self.available()) or "none"
            raise ProviderError(f"{ticker}: no CSV at {path} (available: {known})")

        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            if reader.fieldnames is None:
                raise ProviderError(f"{path}: file is empty")
            columns = _resolve_columns(reader.fieldnames, path)
            bars = [_bar_from_row(row, columns, path, line) for line, row in enumerate(reader, 2)]

        bars.sort(key=lambda bar: bar.date)
        return _window(bars, start, end)


def _resolve_columns(fieldnames: Iterable[str], path: Path) -> dict[str, str]:
    """Map each Bar field to the header that supplies it."""
    lookup = {name.strip().lower(): name for name in fieldnames}
    columns: dict[str, str] = {}
    for field, aliases in _CSV_ALIASES.items():
        for alias in aliases:
            if alias in lookup:
                columns[field] = lookup[alias]
                break
        else:
            raise ProviderError(f"{path}: missing a '{field}' column (found: {', '.join(lookup)})")
    return columns


def _bar_from_row(row: dict[str, str], columns: dict[str, str], path: Path, line: int) -> Bar:
    """Build one :class:`Bar`, reporting the file and line on bad input."""
    try:
        return Bar(
            date=_parse_date(row[columns["date"]]),
            open=float(row[columns["open"]]),
            high=float(row[columns["high"]]),
            low=float(row[columns["low"]]),
            close=float(row[columns["close"]]),
            volume=int(float(row[columns["volume"]])),
        )
    except (TypeError, ValueError) as exc:
        raise ProviderError(f"{path}:{line}: {exc}") from exc


def _parse_date(text: str) -> date:
    """Parse an ISO date, tolerating a trailing time component."""
    text = text.strip()
    try:
        return date.fromisoformat(text)
    except ValueError:
        pass
    try:
        return datetime.fromisoformat(text.replace("Z", "+00:00")).date()
    except ValueError as exc:
        raise ValueError(f"unrecognised date {text!r}") from exc


class YFinanceProvider(Provider):
    """Daily bars from Yahoo Finance via the optional ``yfinance`` package.

    Prices are split- and dividend-adjusted (``auto_adjust=True``), so returns
    computed from them are total returns rather than price-only returns.
    """

    name = "yfinance"

    def __init__(self, default_period: str = "1y") -> None:
        self.default_period = default_period

    def history(
        self,
        ticker: str,
        start: date | None = None,
        end: date | None = None,
    ) -> list[Bar]:
        yf = _import_yfinance()
        # yfinance treats `end` as exclusive; step past it so our inclusive
        # contract holds for both providers.
        kwargs: dict[str, object] = {"auto_adjust": True, "actions": False}
        if start is None and end is None:
            kwargs["period"] = self.default_period
        else:
            if start is not None:
                kwargs["start"] = start.isoformat()
            if end is not None:
                kwargs["end"] = (end + timedelta(days=1)).isoformat()

        try:
            frame = yf.Ticker(ticker).history(**kwargs)
        except Exception as exc:  # noqa: BLE001 - yfinance raises bare Exceptions
            raise ProviderError(f"{ticker}: download failed ({exc})") from exc

        if frame is None or frame.empty:
            raise ProviderError(f"{ticker}: Yahoo Finance returned no rows")
        return [_bar_from_frame_row(index, row) for index, row in frame.iterrows()]


def _bar_from_frame_row(index, row) -> Bar:
    """Convert one yfinance DataFrame row into a :class:`Bar`."""
    open_, high, low, close = (
        float(row["Open"]),
        float(row["High"]),
        float(row["Low"]),
        float(row["Close"]),
    )
    # Adjusted prices are rounded independently, which can nudge the open or
    # close a fraction outside the high/low band. Widen the band rather than
    # rejecting an otherwise usable bar.
    return Bar(
        date=index.date(),
        open=open_,
        high=max(high, open_, close),
        low=min(low, open_, close),
        close=close,
        volume=int(row["Volume"]),
    )


def _import_yfinance():
    try:
        import yfinance as yf
    except ImportError as exc:
        raise ProviderError(
            "the yfinance provider needs the 'yfinance' package "
            "(pip install -r requirements.txt), or use --source csv"
        ) from exc
    return yf


def get_provider(name: str, csv_dir: str | Path = "data", period: str = "1y") -> Provider:
    """Build a provider by name (``csv`` or ``yfinance``)."""
    if name == "csv":
        return CsvProvider(csv_dir)
    if name == "yfinance":
        return YFinanceProvider(default_period=period)
    raise ProviderError(f"unknown source {name!r} (expected 'csv' or 'yfinance')")
