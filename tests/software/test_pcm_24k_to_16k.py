from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PcmRateTests(unittest.TestCase):
    def test_samples_survive_chunk_boundaries_and_keep_signed_range(self):
        with tempfile.TemporaryDirectory(prefix="dspet-pcm-rate-") as directory:
            audio = ROOT / "main/services/audio"
            executable = Path(directory) / "pcm-rate-test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(audio),
                str(audio / "DSPetPcm24kTo16k.c"), str(audio / "DSPetPcmWav.c"),
                str(ROOT / "tests/software/pcm_24k_to_16k_test.c"), "-o", str(executable),
            ], check=True)
            subprocess.run([executable], check=True)
