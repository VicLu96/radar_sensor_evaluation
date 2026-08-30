# stocks

A small command-line tool for pulling daily stock prices and computing the
usual return and risk statistics.

> **Scope note.** This is a standalone utility that happens to live on a branch
> of `radar_sensor_evaluation`; it has nothing to do with radar sensors. See
> [Why this is here](#why-this-is-here).

The core, the CLI and the whole test suite are **stdlib-only** — no pandas, no
numpy, no network. `yfinance` is an optional extra, needed only for live prices.

## Quick start

```sh
git clone https://github.com/VicLu96/radar_sensor_evaluation
cd radar_sensor_evaluation
git checkout claude/stocks-2vn27c

python3 -m stocks quote AAPL MSFT
python3 -m stocks stats SPY --period max
python3 -m stocks history NVDA --ma 20 --ma 50 --tail 10
```

Those run against the bundled sample data in `data/`, so they work offline with
nothing installed.

## Commands

### `quote` — latest close and one-day change

```
$ python3 -m stocks quote AAPL MSFT NVDA SPY
TICKER        DATE   CLOSE  CHANGE  CHANGE %      VOLUME
------  ----------  ------  ------  --------  ----------
AAPL    2026-08-28  233.17   -4.46    -1.88%  78,593,435
MSFT    2026-08-28  379.73   +6.17    +1.65%  32,132,015
NVDA    2026-08-28  429.26  +24.98    +6.18%  45,380,428
SPY     2026-08-28  527.25  -11.84    -2.20%  86,957,549
```

### `stats` — return and risk over a window

```
$ python3 -m stocks stats AAPL SPY --period max
TICKER       START         END  BARS   FIRST    LAST   RETURN     CAGR      VOL  SHARPE   MAX DD
------  ----------  ----------  ----  ------  ------  -------  -------  -------  ------  -------
AAPL    2023-10-06  2026-08-28   756  154.29  233.17  +51.12%  +15.34%  +25.34%    0.67  -28.81%
SPY     2023-10-06  2026-08-28   756  433.20  527.25  +21.71%   +7.03%  +15.05%    0.51  -18.59%
```

| Column | Meaning |
| --- | --- |
| `RETURN` | Total growth from the first close to the last |
| `CAGR` | That return annualised over the calendar span |
| `VOL` | Standard deviation of daily returns, annualised by √252 |
| `SHARPE` | Annualised mean excess return divided by its standard deviation |
| `MAX DD` | Deepest peak-to-trough fall in close price |

### `history` — OHLCV table, optionally with moving averages

```
$ python3 -m stocks history NVDA --ma 20 --ma 50 --tail 3
DATE          OPEN    HIGH     LOW   CLOSE      VOLUME    MA20    MA50
----------  ------  ------  ------  ------  ----------  ------  ------
2026-08-26  400.85  404.83  395.88  403.57  41,710,188  423.33  467.28
2026-08-27  409.18  416.19  402.22  404.28  56,987,964  421.55  465.42
2026-08-28  411.83  436.98  407.94  429.26  45,380,428  422.45  464.27
```

`--ma` is repeatable. Averages are computed over the full requested window and
only then sliced by `--tail`, so a short tail still shows a fully-warmed
average rather than one restarted from the visible rows.

## Selecting a window

| Flag | Effect |
| --- | --- |
| `--period` | Window ending today: `30d`, `6w`, `6mo`, `2y`, or `max` (default `1y`) |
| `--start` | Inclusive ISO start date; overrides `--period` |
| `--end` | Inclusive ISO end date |

Both providers treat `--start` and `--end` as inclusive.

## Data sources

| Flag | Source |
| --- | --- |
| `--source csv` (default) | `<TICKER>.csv` from `--csv-dir` (default `data/`) |
| `--source yfinance` | Live daily bars from Yahoo Finance |

For live prices:

```sh
pip install -r requirements.txt
python3 -m stocks stats AAPL --source yfinance --period 6mo
```

Yahoo bars are split- and dividend-adjusted, so returns computed from them are
total returns rather than price-only returns.

CSV headers are matched case-insensitively against common spellings (`Date`,
`Adj Close`, `Vol`, …), so exports from Yahoo Finance or Stooq load unchanged.
Rows may be in any order; they are sorted by date on load.

**The bundled `data/` CSVs are synthetic** — a seeded random walk, not real
market history. See [`data/README.md`](data/README.md).

## Library use

```python
from stocks.analysis import summarize
from stocks.providers import CsvProvider

bars = CsvProvider("data").history("SPY")
summary = summarize("SPY", bars)
print(summary.cagr, summary.max_drawdown)
```

`stocks.analysis` also exposes `simple_returns`, `log_returns`,
`total_return`, `cagr`, `volatility`, `sharpe`, `max_drawdown` and
`moving_average` individually.

## Layout

```
stocks/
  models.py      Bar and Summary, with validation
  providers.py   CsvProvider, YFinanceProvider
  analysis.py    return, risk and trend statistics
  formatting.py  text tables and number formatting
  cli.py         argument parsing and the three commands
scripts/
  generate_sample_data.py   regenerates data/ from a fixed seed
tests/           78 unittest cases, no network
data/            synthetic sample CSVs
```

## Tests

```sh
python3 -m unittest discover -s tests -t .
```

No pytest, no network, no optional dependencies required.

## Why this is here

A one-word prompt — "stocks" — was pointed at the then-empty
`radar_sensor_evaluation` repository, and this is what came of it. It is kept
on the `claude/stocks-2vn27c` branch and never touches `main`. If it is not
wanted, deleting the branch removes it entirely:

```sh
git push origin --delete claude/stocks-2vn27c
```
