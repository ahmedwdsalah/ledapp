#!/usr/bin/env python3
"""Pack a full-screen premium BMW roundel loop for the Motif display."""

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from animation_output import pack_animation

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "assets/device-animations"

WIDTH = HEIGHT = 480
SS = 2
S = WIDTH * SS
FRAMES = 90
DURATION = 40

FONT_PATH = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

LIGHT_BLUE = np.array([0, 102, 177], dtype=np.float32)
DARK_BLUE = np.array([22, 88, 142], dtype=np.float32)
M_RED = np.array([227, 33, 25], dtype=np.float32)

yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
cx = cy = (S - 1) / 2.0
xg = xx - cx
yg = yy - cy
radius = np.hypot(xg, yg)
angle = np.arctan2(yg, xg)
turn = np.mod(angle, 2 * np.pi)

OUTER = 390.0
BEZEL_IN = 316.0
RING_IN = 236.0

# background: deep navy radial + vignette
base = np.array([4, 5, 10], dtype=np.float32)
deep = np.array([13, 21, 38], dtype=np.float32)
falloff = np.exp(-((radius / (S * 0.58)) ** 2))
bg = base + (deep - base)[None, None, :] * falloff[..., None] * 0.95
vignette = 1.0 - 0.38 * np.clip((radius / (S * 0.72)) ** 2, 0, 1)
bg = bg * vignette[..., None]

# ambient blue glow behind the emblem
glow = np.exp(-(((radius - OUTER * 0.55) / (S * 0.16)) ** 2))
glow_layer = np.array([24, 96, 168], dtype=np.float32)[None, None, :] * glow[..., None] * 0.30

# roundel
roundel = np.zeros((S, S, 3), dtype=np.float32)
bezel_mask = (radius >= BEZEL_IN) & (radius <= OUTER)
shade = 0.70 + 0.30 * np.cos(angle + 2.4)
metal = np.stack([np.full_like(radius, 178), np.full_like(radius, 182), np.full_like(radius, 190)], -1) * shade[..., None]
marks = 0.055 * (np.sin(turn * 96.0) > 0.72)
metal = metal * (1.0 + marks[..., None])
roundel[bezel_mask] = metal[bezel_mask]
roundel[(radius >= OUTER - 3.0) & (radius <= OUTER)] = np.array([214, 217, 222], dtype=np.float32)
roundel[(radius >= BEZEL_IN) & (radius <= BEZEL_IN + 2.5)] = np.array([70, 72, 78], dtype=np.float32)

black_mask = (radius >= RING_IN) & (radius < BEZEL_IN)
bevel = 1.0 - 0.22 * np.clip((BEZEL_IN - radius) / (BEZEL_IN - RING_IN), 0, 1)
ring_black = np.array([11, 12, 15], dtype=np.float32)[None, None, :] * bevel[..., None]
roundel[black_mask] = ring_black[black_mask]

inner_mask = radius < RING_IN
glass = np.clip(0.88 + 0.12 * np.clip(-(xg + yg) / (2 * RING_IN), 0, 1), 0, 1.2)
disc = np.array([242, 243, 245], dtype=np.float32)[None, None, :] * glass[..., None]
blue_quad = ((xg < 0) & (yg < 0)) | ((xg > 0) & (yg > 0))
blue_disc = LIGHT_BLUE[None, None, :] * (0.90 + 0.10 * (1 - radius / RING_IN))[..., None]
disc = np.where((blue_quad & inner_mask)[..., None], blue_disc, disc)
roundel[inner_mask] = disc[inner_mask]

for r_line, color, width in ((RING_IN, (10, 11, 13), 2.5), (BEZEL_IN, (46, 48, 54), 2.0), (OUTER, (12, 13, 16), 2.5)):
    roundel[np.abs(radius - r_line) <= width / 2] = np.array(color, dtype=np.float32)

text_layer = Image.new("L", (S, S), 0)
draw = ImageDraw.Draw(text_layer)
font = ImageFont.truetype(FONT_PATH, 56)
label = "BMW"
boxes = [draw.textbbox((0, 0), ch, font=font) for ch in label]
widths = [b[2] - b[0] for b in boxes]
tracking = 18
total = sum(widths) + tracking * (len(label) - 1)
x0 = cx - total / 2
band_mid = (RING_IN + BEZEL_IN) / 2
for ch, box, w in zip(label, boxes, widths):
    draw.text((x0 - box[0], band_mid - (box[1] + box[3]) / 2), ch, 255, font=font)
    x0 += w + tracking
alpha = np.asarray(text_layer, dtype=np.float32)[..., None] / 255.0
roundel = roundel * (1 - alpha) + 255.0 * alpha

roundel_mask = np.clip((OUTER + 1.5 - radius) / 3.0, 0, 1)

# full-screen M stripe band (diagonal, drifts one period per loop)
period = 980
prof_xp = np.array([0, 30, 90, 120, 150, 180, 240, 270, 300, 322, 378, 400, period])
prof_a = np.array([0, 1, 1, 0, 0, 0.9, 0.9, 0, 0, 1, 1, 0, 0])
prof_index = np.arange(period)
stripe_alpha = np.interp(prof_index, prof_xp, prof_a).astype(np.float32)
stripe_color = np.zeros((period, 3), dtype=np.float32)
stripe_color[prof_index < 130] = LIGHT_BLUE
stripe_color[(prof_index >= 130) & (prof_index < 285)] = DARK_BLUE
stripe_color[(prof_index >= 285) & (prof_index < 420)] = M_RED
u0 = (xx + yy).astype(np.int32) % period
v = xx + yy

# M arc around the emblem (rotates once per loop with a soft comet head)
arc_mask = np.clip((radius - (OUTER + 6)) / 3.0, 0, 1) * np.clip(((OUTER + 22) - radius) / 3.0, 0, 1)
bins = (turn / (2 * np.pi) * 360).astype(np.int32) % 360
deg_xp = np.array([0, 115, 125, 235, 245, 355, 360], dtype=np.float32)
cone = np.stack([
    np.interp(np.arange(360), deg_xp, [0, 0, 22, 22, 227, 227, 0]),
    np.interp(np.arange(360), deg_xp, [102, 102, 88, 88, 33, 33, 102]),
    np.interp(np.arange(360), deg_xp, [177, 177, 142, 142, 25, 25, 177]),
], axis=-1).astype(np.float32)

sheen_sigma = 150.0
raw = []
for i in range(FRAMES):
    phase = i / FRAMES
    frame = bg.copy()
    shift = int(round(phase * period)) % period
    idx = (u0 + shift) % period
    frame += stripe_color[idx] * stripe_alpha[idx][..., None] * 0.26
    pulse = 0.5 + 0.5 * np.sin(2 * np.pi * 2 * phase - np.pi / 2)
    frame += glow_layer * (0.65 + 0.35 * pulse)
    frame = frame * (1 - roundel_mask[..., None]) + roundel * roundel_mask[..., None]
    rot = int(round(phase * 360)) % 360
    head = -np.pi / 2 + 2 * np.pi * phase
    d = np.mod(turn - head + np.pi, 2 * np.pi) - np.pi
    bump = 0.45 + 0.75 * np.exp(-(d * d) / (2 * 0.55 * 0.55))
    arc_col = np.roll(cone, rot, axis=0)[bins]
    frame += arc_col * bump[..., None] * arc_mask[..., None] * 0.85
    sheen_center = -400.0 + phase * 2800.0
    frame += 0.13 * np.exp(-((v - sheen_center) ** 2) / (2 * sheen_sigma * sheen_sigma))[..., None] * 255.0
    np.clip(frame, 0, 255, out=frame)
    small = Image.fromarray(frame.astype(np.uint8)).resize((WIDTH, HEIGHT), Image.LANCZOS)
    raw.append(np.asarray(small))

durations = [DURATION] * FRAMES
count, source_count, size = pack_animation("bmw-roundel", raw, durations, TARGET, preview=True)
print(f"Packed {count}/{source_count} 480x480 frames in {size} bytes")
