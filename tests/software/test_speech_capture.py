from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SpeechCaptureTests(unittest.TestCase):
    def test_preroll_trigger_confirmation_and_endpoint(self):
        with tempfile.TemporaryDirectory(prefix="dspet-speech-capture-") as directory:
            executable = str(Path(directory) / "speech-capture-test")
            service = ROOT / "main/services/audio"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(service), str(service / "DSPetSpeechCapture.c"),
                str(ROOT / "tests/software/speech_capture_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
