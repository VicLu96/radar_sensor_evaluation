"""CSV parsing, windowing and provider selection."""

import unittest
from datetime import date

from stocks.providers import CsvProvider, ProviderError, YFinanceProvider, get_provider
from tests.helpers import FIXTURES


class CsvProviderTest(unittest.TestCase):
    def setUp(self):
        self.provider = CsvProvider(FIXTURES)

    def test_sorts_rows_by_date(self):
        bars = self.provider.history("TEST")
        self.assertEqual([bar.date.day for bar in bars], [1, 2, 3])

    def test_reads_prices(self):
        bars = self.provider.history("TEST")
        self.assertEqual([bar.close for bar in bars], [100.0, 110.0, 121.0])

    def test_ticker_lookup_is_case_insensitive(self):
        self.assertEqual(len(self.provider.history("test")), 3)

    def test_start_is_inclusive(self):
        bars = self.provider.history("TEST", start=date(2024, 1, 2))
        self.assertEqual([bar.date.day for bar in bars], [2, 3])

    def test_end_is_inclusive(self):
        bars = self.provider.history("TEST", end=date(2024, 1, 2))
        self.assertEqual([bar.date.day for bar in bars], [1, 2])

    def test_window_can_be_empty(self):
        self.assertEqual(self.provider.history("TEST", start=date(2030, 1, 1)), [])

    def test_latest_returns_the_last_bar(self):
        self.assertEqual(self.provider.latest("TEST").close, 121.0)

    def test_accepts_alternative_header_spellings(self):
        bars = self.provider.history("ALIAS")
        self.assertEqual([bar.close for bar in bars], [10.25, 10.75])

    def test_parses_dates_carrying_a_time(self):
        bars = self.provider.history("ALIAS")
        self.assertEqual(bars[0].date, date(2024, 2, 1))
        self.assertEqual(bars[1].date, date(2024, 2, 2))

    def test_missing_file_names_the_available_tickers(self):
        with self.assertRaisesRegex(ProviderError, "available: "):
            self.provider.history("NOPE")

    def test_missing_column_is_reported(self):
        with self.assertRaisesRegex(ProviderError, "missing a 'close' column"):
            self.provider.history("NOCLOSE")

    def test_bad_row_reports_the_line_number(self):
        with self.assertRaisesRegex(ProviderError, r"BROKEN\.csv:3"):
            self.provider.history("BROKEN")

    def test_available_lists_fixture_tickers(self):
        self.assertIn("TEST", self.provider.available())

    def test_available_is_empty_for_a_missing_directory(self):
        self.assertEqual(CsvProvider(FIXTURES / "nope").available(), [])


class SampleDataTest(unittest.TestCase):
    """The bundled data/ directory should stay loadable."""

    def test_sample_tickers_load(self):
        provider = CsvProvider("data")
        tickers = provider.available()
        self.assertIn("SPY", tickers)
        for ticker in tickers:
            self.assertGreater(len(provider.history(ticker)), 0)


class GetProviderTest(unittest.TestCase):
    def test_builds_a_csv_provider(self):
        self.assertIsInstance(get_provider("csv", csv_dir=FIXTURES), CsvProvider)

    def test_builds_a_yfinance_provider(self):
        self.assertIsInstance(get_provider("yfinance"), YFinanceProvider)

    def test_rejects_an_unknown_source(self):
        with self.assertRaisesRegex(ProviderError, "unknown source"):
            get_provider("bloomberg")


if __name__ == "__main__":
    unittest.main()
