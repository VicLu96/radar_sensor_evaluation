"""Bar validation rules."""

import unittest
from datetime import date

from stocks.models import Bar


class BarValidationTest(unittest.TestCase):
    def test_accepts_a_consistent_bar(self):
        bar = Bar(date(2024, 1, 1), open=10.0, high=11.0, low=9.0, close=10.5, volume=100)
        self.assertEqual(bar.close, 10.5)

    def test_rejects_high_below_low(self):
        with self.assertRaisesRegex(ValueError, "below low"):
            Bar(date(2024, 1, 1), open=10.0, high=9.0, low=11.0, close=10.0, volume=100)

    def test_rejects_close_outside_the_band(self):
        with self.assertRaisesRegex(ValueError, "close"):
            Bar(date(2024, 1, 1), open=10.0, high=11.0, low=9.0, close=12.0, volume=100)

    def test_rejects_open_outside_the_band(self):
        with self.assertRaisesRegex(ValueError, "open"):
            Bar(date(2024, 1, 1), open=8.0, high=11.0, low=9.0, close=10.0, volume=100)

    def test_rejects_negative_volume(self):
        with self.assertRaisesRegex(ValueError, "negative volume"):
            Bar(date(2024, 1, 1), open=10.0, high=11.0, low=9.0, close=10.0, volume=-1)


if __name__ == "__main__":
    unittest.main()
