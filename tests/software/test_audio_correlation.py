from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AudioCorrelationTests(unittest.TestCase):
    def test_recovers_delay_and_rejects_silence(self):
        with tempfile.TemporaryDirectory(prefix="dspet-correlation-") as directory:
            executable = str(Path(directory) / "correlation-test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "tests/hardware"),
                str(ROOT / "tests/hardware/DSPetAudioCorrelation.c"),
                str(ROOT / "tests/software/audio_correlation_test.c"),
                "-lm", "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
