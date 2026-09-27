#!/usr/bin/env python3
"""Unit tests for the external corpus regression runner."""

import stat
import tempfile
import unittest
from pathlib import Path

import run_m68000_corpus_regression as regression


ADAPTER = """\
import sys
from pathlib import Path
output = Path(sys.argv[sys.argv.index('-o') + 1])
output.write_text('{}\\n')
"""


class RegressionRunnerTests(unittest.TestCase):
    def make_tools(self, directory: Path, result: str, exit_code: int = 0):
        adapter = directory / "adapter.py"
        adapter.write_text(ADAPTER)
        harness = directory / "harness"
        harness.write_text(
            f"#!/bin/sh\nprintf '%s\\n' '{result}'\nexit {exit_code}\n"
        )
        harness.chmod(harness.stat().st_mode | stat.S_IXUSR)
        return adapter, harness

    def test_successful_family_result_is_parsed(self):
        result = (
            "UAE_CPU_CORPUS_RESULT total=12 passed=10 skipped=2 "
            "malformed=0 PASS"
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            adapter, harness = self.make_tools(directory, result)
            corpus = directory / "NOP.json"
            corpus.write_text("[]")
            passed, summary, details = regression.run_family(
                adapter, harness, directory, directory, "NOP", 12)

        self.assertTrue(passed)
        self.assertIn("passed=10", summary)
        self.assertEqual(details["total"], 12)
        self.assertEqual(details["skipped"], 2)

    def test_failed_family_result_propagates_failure(self):
        result = (
            "UAE_CPU_CORPUS_RESULT total=12 passed=9 skipped=2 "
            "malformed=1 FAIL"
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            adapter, harness = self.make_tools(directory, result, 1)
            corpus = directory / "NOP.json"
            corpus.write_text("[]")
            passed, _, details = regression.run_family(
                adapter, harness, directory, directory, "NOP", 12)

        self.assertFalse(passed)
        self.assertEqual(details["malformed"], 1)
        self.assertEqual(details["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
