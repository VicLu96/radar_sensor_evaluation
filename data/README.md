# Sample data

These CSVs are **synthetic** — a seeded geometric random walk, not real market
history. The tickers are borrowed only so the examples read naturally; the
prices never happened and must not be used for anything but exercising the code.

Regenerate them with:

```sh
python3 scripts/generate_sample_data.py
```

The seed is fixed, so the output is byte-for-byte reproducible. For real
numbers use `--source yfinance`, or drop your own `<TICKER>.csv` exports in
this directory.
