#!/usr/bin/env python3
"""Render a seamless, stationary Toyota emblem loop at native display resolution."""

from pathlib import Path
import math
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SIZE = 480
SCALE = 2
FPS = 30
FRAMES = 90
OUT = HERE / "frames"
OUT.mkdir(exist_ok=True)

# Keep the official emblem's path unchanged. rsvg-convert provides antialiased
# alpha at twice the panel resolution; only color and scene lighting vary.
svg = HERE / "toyota.svg"
emblem_png = HERE / "emblem-mask.png"
subprocess.run(
    ["rsvg-convert", "-w", "740", "-h", "512", str(svg), "-o", str(emblem_png)],
    check=True,
)
mask = Image.open(emblem_png).convert("RGBA").getchannel("A")
mask = mask.resize((650, 448), Image.Resampling.LANCZOS)

yy, xx = np.mgrid[0 : SIZE * SCALE, 0 : SIZE * SCALE]
cx = cy = SIZE * SCALE / 2
radius = np.sqrt(((xx - cx) / SCALE) ** 2 + ((yy - cy) / SCALE) ** 2)

for frame in range(FRAMES):
    phase = 2 * math.pi * frame / FRAMES
    # Small ambient changes, never a scale/opacity reveal of the emblem.
    red_aura = np.exp(-((radius - 160) / 77) ** 2) * (0.74 + 0.16 * math.sin(phase))
    center_aura = np.exp(-(radius / 190) ** 2) * 0.28
    rgb = np.zeros((SIZE * SCALE, SIZE * SCALE, 3), dtype=np.uint8)
    rgb[:, :, 0] = np.clip(3 + 25 * red_aura + 6 * center_aura, 0, 255)
    rgb[:, :, 1] = np.clip(4 + 3 * red_aura, 0, 255)
    rgb[:, :, 2] = np.clip(6 + 3 * red_aura, 0, 255)
    image = Image.fromarray(rgb, "RGB").convert("RGBA")

    # Two slender, offset arc traces imply motion without moving the badge.
    arcs = Image.new("RGBA", image.size)
    draw = ImageDraw.Draw(arcs)
    box = (94 * SCALE, 94 * SCALE, 386 * SCALE, 386 * SCALE)
    angle = frame * 360 / FRAMES
    draw.arc(box, angle - 23, angle + 23, fill=(210, 30, 29, 125), width=2 * SCALE)
    draw.arc(box, angle + 157, angle + 184, fill=(130, 27, 28, 75), width=SCALE)
    glow = arcs.filter(ImageFilter.GaussianBlur(9 * SCALE))
    image = Image.alpha_composite(image, glow)
    image = Image.alpha_composite(image, arcs)

    # Emblem remains fixed and fully visible for every frame.
    badge = Image.new("RGBA", mask.size, (231, 233, 234, 255))
    badge.putalpha(mask)
    px = (image.width - badge.width) // 2
    py = (image.height - badge.height) // 2
    shadow = Image.new("RGBA", image.size)
    shadow.paste((173, 12, 15, 255), (px, py), mask)
    shadow = shadow.filter(ImageFilter.GaussianBlur(13 * SCALE))
    image = Image.alpha_composite(image, shadow)
    image.alpha_composite(badge, (px, py))

    # Glancing light crosses the static metal once per seamless loop.
    sweep_center = -200 + 880 * frame / FRAMES
    local_x = (xx / SCALE) - sweep_center
    stripe = np.exp(-((local_x + (yy / SCALE - 240) * 0.22) / 23) ** 2)
    stripe_image = Image.new("RGBA", image.size, (255, 255, 255, 0))
    stripe_image.putalpha(Image.fromarray(np.uint8(stripe * 53), "L"))
    badge_alpha = Image.new("L", image.size)
    badge_alpha.paste(mask, (px, py))
    stripe_image.putalpha(Image.fromarray(np.minimum(np.array(stripe_image.getchannel("A")), np.array(badge_alpha)), "L"))
    image = Image.alpha_composite(image, stripe_image)

    image.resize((SIZE, SIZE), Image.Resampling.LANCZOS).convert("RGB").save(
        OUT / f"{frame:03d}.png", optimize=True
    )

subprocess.run(
    ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", str(FPS),
     "-i", str(OUT / "%03d.png"), "-c:v", "libx264", "-preset", "slow", "-crf", "16",
     "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(HERE / "toyota-ember.mp4")],
    check=True,
)
subprocess.run(
    ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", str(FPS),
     "-i", str(OUT / "%03d.png"), "-vf", "fps=15,split[a][b];[a]palettegen[p];[b][p]paletteuse",
     "-loop", "0", str(HERE / "toyota-ember.gif")],
    check=True,
)
preview_frames = [Image.open(path).convert("RGB") for path in sorted(OUT.glob("[0-9][0-9][0-9].png"))]
preview_frames[0].save(
    HERE / "toyota-ember.webp", format="WEBP", save_all=True,
    append_images=preview_frames[1:],
    duration=[34 if index % 3 == 2 else 33 for index in range(FRAMES)],
    loop=0, lossless=True, method=4,
)
emblem_png.unlink()
