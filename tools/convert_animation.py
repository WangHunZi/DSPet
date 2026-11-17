#!/usr/bin/env python3
"""Convert the pinned upstream idle clip to full-frame, big-endian RGB565.

The ESP32 reads raw frames from flash; VP9 decoding happens only on the host.
Run: python3 tools/convert_animation.py
Requires ffmpeg with libvpx-vp9 and tools/requirements.txt.
"""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageChops, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/source/idle.webm"
OUTPUT = ROOT / "assets/generated"
WIDTH, HEIGHT = 240, 240
FPS = 6
MARGIN = 10
BACKGROUND = (237, 246, 252)


def rgb565_be(image):
    data = bytearray()
    for red, green, blue in image.convert("RGB").get_flattened_data():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        data.extend((value >> 8, value & 255))
    return data


def main():
    source_info = json.loads(SOURCE.with_suffix(".json").read_text())
    source_hash = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    if source_hash != source_info["sha256"]:
        raise SystemExit("Source checksum mismatch; refusing to convert another clip")
    OUTPUT.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="dspet-frames-") as tmp:
        # ffmpeg's native VP9 decoder can silently discard alpha. Force libvpx.
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
            "-c:v", "libvpx-vp9", "-i", str(SOURCE),
            "-vf", f"fps={FPS}", "-pix_fmt", "rgba",
            str(Path(tmp) / "%04d.png"),
        ], check=True)
        paths = sorted(Path(tmp).glob("*.png"))
        if not paths:
            raise SystemExit("Decoder produced no frames")

        # One crop for the entire clip preserves breathing/motion; cropping
        # each frame independently would make the character jump and change size.
        union = None
        for path in paths:
            with Image.open(path) as image:
                if image.mode != "RGBA" or image.getchannel("A").getextrema()[0] != 0:
                    raise SystemExit("Missing transparency; check ffmpeg libvpx-vp9")
                mask = image.getchannel("A").point(lambda a: 255 if a >= 32 else 0)
                union = mask.copy() if union is None else ImageChops.lighter(union, mask)
        crop = union.getbbox()
        if crop is None:
            raise SystemExit("Source clip is entirely transparent")
        # Small transparent padding protects antialiased outlines at the crop edge.
        crop = (max(0, crop[0] - 3), max(0, crop[1] - 3),
                min(union.width, crop[2] + 3), min(union.height, crop[3] + 3))

        preview_frames = []
        binary = OUTPUT / "idle.rgb565"
        with binary.open("wb") as output:
            for path in paths:
                with Image.open(path) as source:
                    sprite = ImageOps.contain(source.crop(crop),
                        (WIDTH - 2 * MARGIN, HEIGHT - 2 * MARGIN), Image.Resampling.LANCZOS)
                canvas = Image.new("RGBA", (WIDTH, HEIGHT), BACKGROUND + (255,))
                canvas.alpha_composite(sprite,
                    ((WIDTH - sprite.width) // 2, (HEIGHT - sprite.height) // 2))
                frame = canvas.convert("RGB")
                output.write(rgb565_be(frame))
                preview_frames.append(frame)

        metadata = {
            "width": WIDTH, "height": HEIGHT, "fps": FPS,
            "frame_count": len(paths), "bytes_per_frame": WIDTH * HEIGHT * 2,
            "format": "RGB565_BE", "crop": crop, "margin": MARGIN,
            "background": list(BACKGROUND), "source_sha256": source_hash,
            "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        }
        (OUTPUT / "idle.json").write_text(json.dumps(metadata, indent=2) + "\n")
        preview_frames[0].save(OUTPUT / "idle-preview.png")
        # GIF has 10 ms time units; these durations sum to the original timing.
        durations = [round((i + 1) * 100 / FPS) * 10 - round(i * 100 / FPS) * 10
                     for i in range(len(paths))]
        preview_frames[0].save(OUTPUT / "idle-preview.gif", save_all=True,
            append_images=preview_frames[1:], duration=durations, loop=0, optimize=False)
        print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
