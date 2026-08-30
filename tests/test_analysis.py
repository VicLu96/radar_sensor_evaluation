"""Return, risk and trend statistics."""

import math
import unittest
from datetime import date

from stocks.analysis import (
    cagr,
    log_returns,
    max_drawdown,
    moving_average,
    sharpe,
    simple_returns,
    summarize,
    total_return,
    volatility,
)
from stocks.models import Bar
from tests.helpers import bars_from_closes


class ReturnsTest(unittest.TestCase):
    def test_simple_returns_are_one_shorter_than_the_input(self):
        bars = bars_from_closes([100.0, 110.0, 121.0])
        self.assertEqual(len(simple_returns(bars)), 2)

    def test_simple_returns_values(self):
        bars = bars_from_closes([100.0, 110.0, 121.0])
        for actual, expected in zip(simple_returns(bars), [0.1, 0.1]):
            self.assertAlmostEqual(actual, expected)

    def test_log_returns_add_up_to_the_total(self):
        bars = bars_from_closes([100.0, 110.0, 121.0])
        self.assertAlmostEqual(sum(log_returns(bars)), math.log(1.21))

    def test_log_returns_reject_non_positive_prices(self):
        bars = [
            Bar(date(2024, 1, 1), 1.0, 1.0, 0.0, 0.0, 1),
            Bar(date(2024, 1, 2), 1.0, 1.0, 1.0, 1.0, 1),
        ]
        with self.assertRaises(ValueError):
            log_returns(bars)

    def test_total_return(self):
        self.assertAlmostEqual(total_return(bars_from_closes([100.0, 121.0])), 0.21)

    def test_total_return_of_a_single_bar_is_zero(self):
        self.assertEqual(total_return(bars_from_closes([100.0])), 0.0)

    def test_total_return_can_be_negative(self):
        self.assertAlmostEqual(total_return(bars_from_closes([100.0, 80.0])), -0.2)


class CagrTest(unittest.TestCase):
    def test_doubling_over_a_year_is_about_one_hundred_percent(self):
        bars = [
            Bar(date(2024, 1, 1), 100.0, 100.0, 100.0, 100.0, 1),
            Bar(date(2025, 1, 1), 200.0, 200.0, 200.0, 200.0, 1),
        ]
        self.assertAlmostEqual(cagr(bars), 1.0, places=2)

    def test_flat_series_has_no_growth(self):
        bars = [
            Bar(date(2024, 1, 1), 100.0, 100.0, 100.0, 100.0, 1),
            Bar(date(2026, 1, 1), 100.0, 100.0, 100.0, 100.0, 1),
        ]
        self.assertAlmostEqual(cagr(bars), 0.0)

    def test_same_day_window_is_not_annualised(self):
        bars = [
            Bar(date(2024, 1, 1), 100.0, 100.0, 100.0, 100.0, 1),
            Bar(date(2024, 1, 1), 200.0, 200.0, 200.0, 200.0, 1),
        ]
        self.assertEqual(cagr(bars), 0.0)

    def test_single_bar_is_not_annualised(self):
        self.assertEqual(cagr(bars_from_closes([100.0])), 0.0)


class RiskTest(unittest.TestCase):
    def test_volatility_of_a_flat_series_is_zero(self):
        self.assertEqual(volatility([0.0, 0.0, 0.0]), 0.0)

    def test_volatility_annualises_by_root_252(self):
        returns = [0.01, -0.01, 0.02, -0.02]
        self.assertAlmostEqual(
            volatility(returns), volatility(returns, annualise=False) * math.sqrt(252)
        )

    def test_volatility_needs_two_observations(self):
        self.assertEqual(volatility([0.01]), 0.0)

    def test_sharpe_is_zero_without_dispersion(self):
        self.assertEqual(sharpe([0.01, 0.01, 0.01]), 0.0)

    def test_sharpe_falls_when_the_risk_free_rate_rises(self):
        returns = [0.01, -0.005, 0.02, 0.0]
        self.assertLess(sharpe(returns, risk_free=0.05), sharpe(returns, risk_free=0.0))

    def test_max_drawdown_finds_the_deepest_trough(self):
        bars = bars_from_closes([100.0, 120.0, 60.0, 90.0])
        self.assertAlmostEqual(max_drawdown(bars), -0.5)

    def test_max_drawdown_of_a_rising_series_is_zero(self):
        self.assertEqual(max_drawdown(bars_from_closes([1.0, 2.0, 3.0])), 0.0)


class MovingAverageTest(unittest.TestCase):
    def test_pads_until_the_window_is_full(self):
        self.assertEqual(moving_average([1.0, 2.0, 3.0], 3), [None, None, 2.0])

    def test_window_of_one_is_the_input(self):
        self.assertEqual(moving_average([1.0, 2.0], 1), [1.0, 2.0])

    def test_rolls_forward(self):
        result = moving_average([1.0, 2.0, 3.0, 4.0], 2)
        self.assertEqual(result, [None, 1.5, 2.5, 3.5])

    def test_running_sum_matches_a_naive_recomputation(self):
        values = [float(i % 7) + 0.5 for i in range(50)]
        window = 5
        expected = [
            None if i < window - 1 else sum(values[i - window + 1 : i + 1]) / window
            for i in range(len(values))
        ]
        for actual, want in zip(moving_average(values, window), expected):
            if want is None:
                self.assertIsNone(actual)
            else:
                self.assertAlmostEqual(actual, want)

    def test_rejects_a_non_positive_window(self):
        with self.assertRaises(ValueError):
            moving_average([1.0], 0)


class SummarizeTest(unittest.TestCase):
    def test_carries_the_window_edges(self):
        bars = bars_from_closes([100.0, 110.0, 121.0])
        summary = summarize("TEST", bars)
        self.assertEqual(summary.ticker, "TEST")
        self.assertEqual(summary.bars, 3)
        self.assertEqual(summary.first_close, 100.0)
        self.assertEqual(summary.last_close, 121.0)
        self.assertAlmostEqual(summary.total_return, 0.21)

    def test_rejects_an_empty_series(self):
        with self.assertRaises(ValueError):
            summarize("TEST", [])


if __name__ == "__main__":
    unittest.main()
