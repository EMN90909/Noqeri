#!/usr/bin/env python3
"""Unit tests for fail-closed timing quantiles."""
import importlib.util
from pathlib import Path
import unittest

S = Path(__file__).resolve().parent.parent / "scripts/native-perf-evidence.py"
SPEC = importlib.util.spec_from_file_location("native_perf_evidence", S)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class TestQuantiles(unittest.TestCase):
    def test_nearest_rank(self):
        values = list(range(1, 21))
        result = MOD.percentiles(values)
        self.assertEqual(result["p50_ms"], 10.5)
        self.assertEqual(result["p95_ms"], 19)
        self.assertEqual(result["count"], 20)

    def test_missing_samples_fails_closed(self):
        with self.assertRaises(ValueError):
            MOD.percentiles([])
        with self.assertRaises(ValueError):
            MOD.percentiles([1, 2, 3, 4])

    def test_invalid_sample_fails_closed(self):
        with self.assertRaises(ValueError):
            MOD.percentiles([1, 2, 3, 4, float("nan")])
        with self.assertRaises(ValueError):
            MOD.percentiles([1, 2, -1, 3, 4])


if __name__ == "__main__":
    unittest.main()
