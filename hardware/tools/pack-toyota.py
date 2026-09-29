#!/usr/bin/env python3
"""Pack Toyota Ember's lossless 480px masters for current Motif transport."""

from pathlib import Path
from animation_output import pack_animation

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/animation-masters/toyota-ember/frames"
TARGET = ROOT / "assets/device-animations"
WIDTH = HEIGHT = 480

frames = sorted(SOURCE.glob("[0-9][0-9][0-9].png"))
if len(frames) != 90:
    raise ValueError(f"Expected 90 source frames, found {len(frames)}")

raw = []
for path in frames:
    with Image.open(path) as image:
        if image.size != (WIDTH, HEIGHT):
            raise ValueError(f"{path.name} has wrong dimensions")
        raw.append(np.asarray(image.convert("RGB")))
# 30 fps averages 33⅓ ms; repeating 33, 33, 34 prevents drift.
durations = [34 if index % 3 == 2 else 33 for index in range(len(frames))]
count, source_count, size = pack_animation("toyota-ember", raw, durations, TARGET, preview=False)
print(f"Packed {count}/{source_count} native 480×480 frames in {size} bytes")
