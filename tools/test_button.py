"""Compile and run the same button filter used by the firmware on the host."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ButtonTests(unittest.TestCase):
    def test_bounce_hold_startup_and_clock_wrap(self):
        with tempfile.TemporaryDirectory(prefix="dspet-button-") as directory:
            executable = str(Path(directory) / "button-test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "main"), str(ROOT / "main/button_filter.c"),
                str(ROOT / "tests/button_filter_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
