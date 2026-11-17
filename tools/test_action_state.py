from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ActionStateTests(unittest.TestCase):
    def test_automatic_manual_overlap_and_counter_wrap(self):
        with tempfile.TemporaryDirectory(prefix="dspet-action-") as directory:
            executable = str(Path(directory) / "action-test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "main"), str(ROOT / "main/action_state.c"),
                str(ROOT / "tests/action_state_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
