"""Plain-text table rendering and number formatting for the CLI."""

from __future__ import annotations

from typing import Sequence


def render_table(headers: Sequence[str], rows: Sequence[Sequence[str]]) -> str:
    """Render an aligned text table.

    The first column is left-aligned (it holds labels such as tickers and
    dates); every other column is right-aligned so figures line up on the
    decimal point.
    """
    if not rows:
        return "(no rows)"

    widths = [len(header) for header in headers]
    for row in rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(cell))

    def line(cells: Sequence[str]) -> str:
        parts = [
            cell.ljust(widths[index]) if index == 0 else cell.rjust(widths[index])
            for index, cell in enumerate(cells)
        ]
        return "  ".join(parts).rstrip()

    rule = "  ".join("-" * width for width in widths)
    return "\n".join([line(headers), rule, *(line(row) for row in rows)])


def money(value: float) -> str:
    """Format a price with two decimals."""
    return f"{value:,.2f}"


def percent(value: float, decimals: int = 2) -> str:
    """Format a fraction as a signed percentage (``0.1`` -> ``+10.00%``)."""
    return f"{value * 100:+.{decimals}f}%"


def ratio(value: float) -> str:
    """Format a unitless ratio such as a Sharpe."""
    return f"{value:.2f}"


def count(value: int) -> str:
    """Format a whole number with thousands separators."""
    return f"{value:,}"
