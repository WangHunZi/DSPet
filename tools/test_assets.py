"""Host checks for the pixel format and shipped animation pack.

Run: python3 -m unittest discover -s tools -p 'test_*.py'
"""

import hashlib
import json
from pathlib import Path
import unittest

from PIL import Image
from convert_animation import rgb565_be

ROOT = Path(__file__).resolve().parents[1]


class AssetTests(unittest.TestCase):
    def test_known_rgb565_wire_colors(self):
        image = Image.new("RGB", (5, 1))
        image.putdata([(255, 0, 0), (0, 255, 0), (0, 0, 255),
                       (255, 255, 255), (0, 0, 0)])
        self.assertEqual(rgb565_be(image), bytes.fromhex("f80007e0001fffff0000"))

    def test_source_is_the_pinned_upstream_clip(self):
        metadata = json.loads((ROOT / "assets/source/idle.json").read_text())
        source = (ROOT / "assets/source/idle.webm").read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(), metadata["sha256"])

    def test_pack_size_and_checksum(self):
        metadata = json.loads((ROOT / "assets/generated/idle.json").read_text())
        pixels = (ROOT / "assets/generated/idle.rgb565").read_bytes()
        self.assertEqual(metadata["format"], "RGB565_BE")
        self.assertEqual((metadata["width"], metadata["height"]), (240, 240))
        self.assertGreater(metadata["frame_count"], 1)
        self.assertEqual(len(pixels), metadata["frame_count"] * 240 * 240 * 2)
        self.assertEqual(hashlib.sha256(pixels).hexdigest(), metadata["sha256"])

    def test_preview_matches_quantized_first_frame(self):
        with Image.open(ROOT / "assets/generated/idle-preview.png") as preview:
            first = rgb565_be(preview)
        pixels = (ROOT / "assets/generated/idle.rgb565").read_bytes()
        self.assertEqual(first, pixels[:240 * 240 * 2])


if __name__ == "__main__":
    unittest.main()
