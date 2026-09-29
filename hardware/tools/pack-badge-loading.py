#!/usr/bin/env python3
"""Render an antialiased radial spinner and pack native RGB565 playback frames."""
from pathlib import Path
from animation_output import pack_animation
import math

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'assets/device-animations'
SIZE, SCALE, SEGMENTS, FRAMES = 480, 4, 16, 48

# Rounded spokes occupy 90% of the circular display, with no baked-in bezel.
masks = []
for segment in range(SEGMENTS):
    angle = segment * math.tau / SEGMENTS - math.pi / 2
    mask = Image.new('L', (SIZE * SCALE, SIZE * SCALE))
    draw = ImageDraw.Draw(mask)
    points = [((240 + radius * math.cos(angle)) * SCALE,
               (240 + radius * math.sin(angle)) * SCALE) for radius in (130, 204)]
    width = 22 * SCALE
    draw.line(points, fill=255, width=width)
    for x, y in points:
        draw.ellipse((x-width/2, y-width/2, x+width/2, y+width/2), fill=255)
    masks.append(np.asarray(mask.resize((SIZE, SIZE), Image.Resampling.LANCZOS), dtype=np.float32))
masks = np.stack(masks)


def render(index):
    phase = index * SEGMENTS / FRAMES
    # Circular distance makes the fade continuous across the loop boundary.
    distance = np.abs((np.arange(SEGMENTS) - phase + SEGMENTS / 2) % SEGMENTS - SEGMENTS / 2)
    level = 0.20 + 0.80 * np.exp(-0.5 * (distance / 2.5) ** 2)
    gray = np.clip(np.sum(masks * level[:, None, None], axis=0), 0, 255).round().astype(np.uint8)
    return Image.fromarray(gray).convert('RGB')


assert np.array_equal(np.asarray(render(0)), np.asarray(render(FRAMES)))
frames = [np.asarray(render(index), dtype=np.uint8) for index in range(FRAMES)]
durations = [34 if index % 3 == 2 else 33 for index in range(FRAMES)]
count, source_count, size = pack_animation('badge-loading', frames, durations, TARGET)
print(f'PASS badge-loading: {count}/{source_count} frames, {sum(durations)} ms seamless loop, {size:,} bytes, native {SIZE}×{SIZE}')
