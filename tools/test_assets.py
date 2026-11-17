"""Host checks for the pixel format and shipped animation pack.

Run: python3 -m unittest discover -s tools -p 'test_*.py'
"""

import hashlib
import json
from pathlib import Path
import unittest
import zlib

from PIL import Image
from convert_animation import rgb565_be, decode_indexed, restore_delta

ROOT = Path(__file__).resolve().parents[1]


class AssetTests(unittest.TestCase):
    def test_known_rgb565_wire_colors(self):
        image = Image.new("RGB", (5, 1))
        image.putdata([(255, 0, 0), (0, 255, 0), (0, 0, 255),
                       (255, 255, 255), (0, 0, 0)])
        self.assertEqual(rgb565_be(image), bytes.fromhex("f80007e0001fffff0000"))

    def test_source_is_the_pinned_upstream_clip(self):
        paths = list((ROOT / "assets/source").glob("*.json"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(clip=path.stem):
                metadata = json.loads(path.read_text())
                source = path.with_suffix(".webm").read_bytes()
                self.assertEqual(hashlib.sha256(source).hexdigest(), metadata["sha256"])

    def test_pack_size_and_checksum(self):
        paths = list((ROOT / "assets/generated").glob("*.json"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(clip=path.stem):
                metadata = json.loads(path.read_text())
                pixels = path.with_suffix(".rgb565z").read_bytes()
                self.assertEqual(metadata["format"], "INDEX8_DELTA_RGB565_ZLIB")
                self.assertEqual((metadata["width"], metadata["height"]), (240, 240))
                offsets = metadata["offsets"]
                self.assertEqual(len(offsets), metadata["frame_count"] + 1)
                self.assertEqual(offsets[0], 0)
                self.assertEqual(offsets[-1], len(pixels))
                digest = hashlib.sha256()
                previous = None
                for number, (start, end) in enumerate(zip(offsets, offsets[1:])):
                    self.assertLess(start, end)
                    payload = zlib.decompress(pixels[start:end])
                    self.assertEqual(len(payload), metadata["decoded_bytes_per_frame"])
                    payload, previous = restore_delta(payload, previous,
                        number % metadata["keyframe_interval"] == 0)
                    frame = decode_indexed(payload)
                    self.assertEqual(len(frame), 240 * 240 * 2)
                    digest.update(frame)
                self.assertEqual(digest.hexdigest(), metadata["uncompressed_sha256"])
                self.assertEqual(hashlib.sha256(pixels).hexdigest(), metadata["sha256"])

    def test_preview_matches_quantized_first_frame(self):
        for path in (ROOT / "assets/generated").glob("*.json"):
            with self.subTest(clip=path.stem):
                with Image.open(path.with_name(path.stem + "-preview.png")) as preview:
                    first = rgb565_be(preview)
                metadata = json.loads(path.read_text())
                pixels = path.with_suffix(".rgb565z").read_bytes()
                self.assertEqual(first, decode_indexed(zlib.decompress(pixels[:metadata["offsets"][1]])))

    def test_invalid_indexed_frame_is_rejected(self):
        with self.assertRaises(ValueError):
            decode_indexed(bytes(511))

    def test_delta_requires_a_predecessor(self):
        with self.assertRaises(ValueError):
            restore_delta(bytes(512 + 240 * 240), None, False)

    def test_keyframe_seek_matches_sequential_decode(self):
        path = ROOT / "assets/generated/wave.json"
        meta = json.loads(path.read_text())
        data = path.with_suffix(".rgb565z").read_bytes()
        previous = None
        sequential = []
        for frame in range(meta["frame_count"]):
            start, end = meta["offsets"][frame:frame + 2]
            restored, previous = restore_delta(zlib.decompress(data[start:end]), previous,
                frame % meta["keyframe_interval"] == 0)
            sequential.append(restored)
        for target in (0, 17, 24, 47, meta["frame_count"] - 1):
            first = target - target % meta["keyframe_interval"]
            previous = None
            for frame in range(first, target + 1):
                start, end = meta["offsets"][frame:frame + 2]
                restored, previous = restore_delta(zlib.decompress(data[start:end]), previous,
                    frame == first)
            self.assertEqual(restored, sequential[target])


if __name__ == "__main__":
    unittest.main()
