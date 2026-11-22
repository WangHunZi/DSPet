from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class IndexedFrameTests(unittest.TestCase):
    def test_in_place_expansion_and_capacity(self):
        with tempfile.TemporaryDirectory(prefix="dspet-indexed-") as directory:
            executable = str(Path(directory) / "frame-test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "main/services/animation"),
                str(ROOT / "main/services/animation/DSPetIndexedFrame.c"),
                str(ROOT / "tests/software/indexed_frame_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
