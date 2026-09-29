#!/usr/bin/env python3
"""Pack Toyota Ember's lossless 480px masters for current Motif transport."""

from pathlib import Path
from animation_output import publish_assets
import struct
import zlib

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/animation-masters/toyota-ember/frames"
TARGET = ROOT / "assets/device-animations/toyota-ember.motif"
WIDTH = HEIGHT = 480
MAX_BYTES = 4 * 1024 * 1024

frames = sorted(SOURCE.glob("[0-9][0-9][0-9].png"))
if len(frames) != 90:
    raise ValueError(f"Expected 90 source frames, found {len(frames)}")

output = bytearray(struct.pack("<4sBBHHH", b"MOTF", 1, 0, WIDTH, HEIGHT, len(frames)))
for index, path in enumerate(frames):
    with Image.open(path) as image:
        if image.size != (WIDTH, HEIGHT):
            raise ValueError(f"{path.name} has wrong dimensions")
        rgb = np.asarray(image.convert("RGB"), dtype=np.uint16)
    pixels = ((rgb[:, :, 0] >> 3) << 11) | ((rgb[:, :, 1] >> 2) << 5) | (rgb[:, :, 2] >> 3)
    compressed = zlib.compress(pixels.astype("<u2").tobytes(), 9)
    # 30 fps averages 33⅓ ms; repeating 33, 33, 34 prevents drift.
    duration = 34 if index % 3 == 2 else 33
    output.extend(struct.pack("<HI", duration, len(compressed)))
    output.extend(compressed)

if len(output) > MAX_BYTES:
    raise ValueError(f"Toyota animation is {len(output)} bytes; limit is {MAX_BYTES}")
with publish_assets(TARGET.parent) as staging:
    (staging / TARGET.name).write_bytes(output)
print(f"Packed {len(frames)} native 480×480 frames in {len(output)} bytes")
