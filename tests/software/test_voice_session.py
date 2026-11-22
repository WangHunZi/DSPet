from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VoiceSessionTests(unittest.TestCase):
    def test_turns_barge_in_timeout_and_stale_replies(self):
        with tempfile.TemporaryDirectory(prefix="dspet-voice-") as directory:
            executable = str(Path(directory) / "voice-test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "main/app/voice"),
                str(ROOT / "main/app/voice/DSPetVoiceSession.c"),
                str(ROOT / "tests/software/voice_session_test.c"), "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True)
