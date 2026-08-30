"""End-to-end CLI behaviour, driven through main()."""

import io
import unittest
from contextlib import redirect_stderr, redirect_stdout
from datetime import date

from stocks.cli import main, resolve_period
from tests.helpers import FIXTURES

BASE = ["--source", "csv", "--csv-dir", str(FIXTURES), "--period", "max"]


def run(*argv) -> tuple[int, str, str]:
    """Invoke the CLI, returning (exit code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main(list(argv))
    return code, out.getvalue(), err.getvalue()


class ResolvePeriodTest(unittest.TestCase):
    TODAY = date(2024, 6, 15)

    def test_days(self):
        self.assertEqual(resolve_period("30d", self.TODAY), date(2024, 5, 16))

    def test_months(self):
        self.assertEqual(resolve_period("6mo", self.TODAY), date(2023, 12, 18))

    def test_years(self):
        self.assertEqual(resolve_period("1y", self.TODAY), date(2023, 6, 16))

    def test_max_means_unbounded(self):
        self.assertIsNone(resolve_period("max", self.TODAY))
        self.assertIsNone(resolve_period("ALL", self.TODAY))

    def test_is_case_insensitive(self):
        self.assertEqual(resolve_period("30D", self.TODAY), resolve_period("30d", self.TODAY))

    def test_rejects_nonsense(self):
        with self.assertRaisesRegex(ValueError, "unrecognised period"):
            resolve_period("soon", self.TODAY)


class QuoteTest(unittest.TestCase):
    def test_reports_the_last_close(self):
        code, out, _ = run("quote", "TEST", *BASE)
        self.assertEqual(code, 0)
        self.assertIn("121.00", out)

    def test_reports_the_one_day_change(self):
        code, out, _ = run("quote", "TEST", *BASE)
        self.assertEqual(code, 0)
        self.assertIn("+10.00%", out)

    def test_accepts_several_tickers(self):
        code, out, _ = run("quote", "TEST", "ALIAS", *BASE)
        self.assertEqual(code, 0)
        self.assertIn("TEST", out)
        self.assertIn("ALIAS", out)

    def test_unknown_ticker_exits_nonzero(self):
        code, _, err = run("quote", "NOPE", *BASE)
        self.assertEqual(code, 1)
        self.assertIn("error:", err)

    def test_empty_window_exits_nonzero(self):
        code, _, err = run(
            "quote", "TEST", "--source", "csv", "--csv-dir", str(FIXTURES), "--start", "2030-01-01"
        )
        self.assertEqual(code, 1)
        self.assertIn("no bars", err)


class HistoryTest(unittest.TestCase):
    def test_prints_every_bar(self):
        code, out, _ = run("history", "TEST", *BASE)
        self.assertEqual(code, 0)
        self.assertEqual(len(out.strip().splitlines()), 5)  # header + rule + 3 bars

    def test_tail_limits_the_rows(self):
        code, out, _ = run("history", "TEST", "--tail", "1", *BASE)
        self.assertEqual(code, 0)
        self.assertEqual(len(out.strip().splitlines()), 3)

    def test_moving_average_adds_a_column(self):
        code, out, _ = run("history", "TEST", "--ma", "2", *BASE)
        self.assertEqual(code, 0)
        self.assertIn("MA2", out)

    def test_moving_average_pads_the_first_row(self):
        code, out, _ = run("history", "TEST", "--ma", "2", *BASE)
        self.assertTrue(out.splitlines()[2].rstrip().endswith("-"))

    def test_averages_stay_warm_under_tail(self):
        # MA2 over the last bar is (110 + 121) / 2, which needs the bar that
        # --tail hides; the column must not restart from the visible slice.
        code, out, _ = run("history", "TEST", "--ma", "2", "--tail", "1", *BASE)
        self.assertIn("115.50", out)

    def test_repeated_ma_flags_are_deduplicated_and_sorted(self):
        code, out, _ = run("history", "TEST", "--ma", "3", "--ma", "2", "--ma", "3", *BASE)
        header = out.splitlines()[0]
        self.assertEqual(header.count("MA2"), 1)
        self.assertLess(header.index("MA2"), header.index("MA3"))


class StatsTest(unittest.TestCase):
    def test_reports_the_total_return(self):
        code, out, _ = run("stats", "TEST", *BASE)
        self.assertEqual(code, 0)
        self.assertIn("+21.00%", out)

    def test_covers_every_ticker(self):
        code, out, _ = run("stats", "TEST", "ALIAS", *BASE)
        self.assertEqual(code, 0)
        self.assertEqual(len(out.strip().splitlines()), 4)


class ArgumentTest(unittest.TestCase):
    def test_a_missing_subcommand_is_rejected(self):
        with self.assertRaises(SystemExit):
            run()

    def test_a_bad_date_is_rejected(self):
        with self.assertRaises(SystemExit):
            run("quote", "TEST", "--start", "yesterday")


if __name__ == "__main__":
    unittest.main()
