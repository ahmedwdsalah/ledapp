#!/usr/bin/env python3
"""Render a seamless neon vortex for the native circular Motif display."""
from pathlib import Path
from animation_output import publish_assets
import math
import struct
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'assets/device-animations'
SIZE, SCALE, FRAMES = 480, 2, 48
COLORS = [(0, 220, 255), (255, 30, 150), (145, 65, 255)]


def point(radius, angle):
    return ((240 + radius * math.cos(angle)) * SCALE,
            (240 + radius * math.sin(angle)) * SCALE)


def render(index):
    phase = math.tau * (index % FRAMES) / FRAMES
    light = Image.new('RGB', (SIZE * SCALE, SIZE * SCALE))
    draw = ImageDraw.Draw(light)
    for blade in range(6):
        angle = blade * math.tau / 6 + phase
        outer, inner = [], []
        for step in range(65):
            t = step / 64
            radius = 42 + 159 * t
            twist = angle + 1.65 * (1 - t) + .13 * math.sin(phase * 2 + t * math.tau)
            width = .012 + .21 * math.sin(math.pi * t) ** 1.4
            outer.append(point(radius, twist + width))
            inner.append(point(radius, twist - width))
        color = COLORS[blade % 3]
        draw.polygon(outer + inner[::-1], fill=tuple(int(c * .48) for c in color))
        draw.line(outer, fill=color, width=3*SCALE)
        draw.line(inner, fill=tuple(int(c * .7) for c in color), width=2*SCALE)
        draw.line(outer[40:57], fill=(215, 245, 255), width=SCALE)
    # Counter-rotating broken halo and orbiting sparks.
    for segment in range(24):
        angle = segment * math.tau / 24 - phase / 2
        # Half-turn is identical for this repeating ring.
        points = [point(222, angle + t * .16 / 12) for t in range(13)]
        draw.line(points, fill=COLORS[segment % 3], width=2*SCALE)
    for ring in range(3):
        radius = 13 + ring * 8 + 2 * math.sin(phase * 2)
        points = [point(radius, phase * (-1 if ring % 2 else 1) + k * math.tau / 6) for k in range(7)]
        draw.line(points, fill=COLORS[ring], width=2*SCALE)
    draw.ellipse((237*SCALE,237*SCALE,243*SCALE,243*SCALE),fill=(245,255,255))
    glow = light.filter(ImageFilter.GaussianBlur(4*SCALE))
    rgb = np.minimum(np.asarray(light, dtype=np.uint16) + np.asarray(glow, dtype=np.uint16), 255).astype(np.uint8)
    return Image.fromarray(rgb).resize((SIZE, SIZE), Image.Resampling.LANCZOS)


assert np.array_equal(np.asarray(render(0)), np.asarray(render(FRAMES)))
frames = [render(i) for i in range(FRAMES)]
durations = [50] * FRAMES
output = bytearray(struct.pack('<4sBBHHH', b'MOTF', 1, 0, SIZE, SIZE, FRAMES))
for frame, duration in zip(frames, durations):
    rgb = np.asarray(frame, dtype=np.uint16)
    pixels = ((rgb[:, :, 0] >> 3) << 11) | ((rgb[:, :, 1] >> 2) << 5) | (rgb[:, :, 2] >> 3)
    raw = pixels.astype('<u2').tobytes()
    compressed = zlib.compress(raw, 9)
    assert len(raw) == SIZE * SIZE * 2 and zlib.decompress(compressed) == raw
    output.extend(struct.pack('<HI', duration, len(compressed)))
    output.extend(compressed)
assert len(output) <= 4 * 1024 * 1024, len(output)
with publish_assets(TARGET) as staging:
    (staging / 'prism-vortex.motif').write_bytes(output)
    frames[0].save(staging / 'prism-vortex.webp', save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, lossless=True, method=6)
    frames[0].save(staging / 'prism-vortex-preview.png')
    with Image.open(staging / 'prism-vortex.webp') as preview:
        assert preview.n_frames == FRAMES and preview.size == (SIZE, SIZE)
        for i in range(FRAMES):
            preview.seek(i)
            preview.load()
print(f'PASS: {FRAMES} frames, {sum(durations)} ms loop, {len(output):,} bytes, native {SIZE}×{SIZE}')
