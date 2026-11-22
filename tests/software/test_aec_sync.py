from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AecSyncTests(unittest.TestCase):
    def test_aligns_shared_clock_streams_and_rejects_gap(self):
        with tempfile.TemporaryDirectory(prefix="dspet-aec-sync-") as directory:
            executable = str(Path(directory) / "aec-sync-test")
            service = ROOT / "main/services/audio"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(service), str(service / "DSPetAecSync.c"),
                str(ROOT / "tests/software/aec_sync_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
