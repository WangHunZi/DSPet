import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SetupFormTest(unittest.TestCase):
    def test_setup_form(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "setup-form"
            subprocess.run(
                [
                    os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "main/services/setup"),
                    str(ROOT / "main/services/setup/DSPetSetupForm.c"),
                    str(ROOT / "tests/software/setup_form_test.c"), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([binary], check=True)
