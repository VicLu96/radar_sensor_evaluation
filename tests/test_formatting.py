"""Table rendering and number formatting."""

import unittest

from stocks.formatting import count, money, percent, ratio, render_table


class RenderTableTest(unittest.TestCase):
    def test_reports_empty_input(self):
        self.assertEqual(render_table(["A"], []), "(no rows)")

    def test_includes_a_header_and_a_rule(self):
        lines = render_table(["A", "B"], [["1", "2"]]).splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(set(lines[1]) <= {"-", " "})

    def test_columns_line_up(self):
        lines = render_table(["TICKER", "X"], [["A", "1"], ["LONGER", "22"]]).splitlines()
        self.assertEqual(len({len(line) for line in lines}), 1)

    def test_first_column_is_left_aligned(self):
        lines = render_table(["T", "X"], [["A", "1"], ["LONG", "2"]]).splitlines()
        self.assertTrue(lines[2].startswith("A "))

    def test_widens_to_fit_the_header(self):
        lines = render_table(["VERYLONGHEADER"], [["1"]]).splitlines()
        self.assertEqual(lines[0], "VERYLONGHEADER")


class NumberFormatTest(unittest.TestCase):
    def test_money_uses_two_decimals_and_separators(self):
        self.assertEqual(money(1234.5), "1,234.50")

    def test_percent_is_signed(self):
        self.assertEqual(percent(0.1234), "+12.34%")
        self.assertEqual(percent(-0.5), "-50.00%")

    def test_ratio_uses_two_decimals(self):
        self.assertEqual(ratio(1.239), "1.24")

    def test_count_uses_separators(self):
        self.assertEqual(count(1234567), "1,234,567")


if __name__ == "__main__":
    unittest.main()
