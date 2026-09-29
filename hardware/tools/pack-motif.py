#!/usr/bin/env python3
"""Pack any animation or image sequence into a best-quality Motif asset.

Examples:
  pack-motif.py art.gif
  pack-motif.py clip.mp4 --name launch-loop
  pack-motif.py frames/ --name spin --fps 30 --fit contain
  pack-motif.py render.webp --loop-ms 3000 --no-delta

The input is Lanczos-scaled to the native 480x480 panel (cover-crop by
default), rounded and Bayer-dithered to RGB565, stored as deterministic
difference frames, and trimmed to the longest loop that fits the 4 MiB
upload. The .motif plus device-exact <name>.webp / <name>-preview.png are
validated and published atomically next to the other gallery assets.
"""
from pathlib import Path
import argparse

from animation_output import (decode_raster, equalize_durations, image_durations,
                              pack_animation, video_info)

ROOT = Path(__file__).resolve().parents[2]

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('input', type=Path, help='animated image, video, or a folder of numbered frames')
parser.add_argument('--name', help='asset name (default: input file name without extension)')
parser.add_argument('--output', type=Path, default=ROOT / 'assets/device-animations')
parser.add_argument('--fps', type=float, help='frame rate for sources without timing (default: 30)')
parser.add_argument('--loop-ms', type=int, help='rescale the finished loop to exactly this length')
parser.add_argument('--fit', choices=('cover', 'contain'), default='cover', help='how non-square input fills the panel')
parser.add_argument('--preview-frames', type=int, default=40, help='cap on bundled preview frames')
parser.add_argument('--no-delta', action='store_true', help='store absolute frames instead of differences')
args = parser.parse_args()

name = args.name or args.input.stem


def animated(path):
    from PIL import Image
    with Image.open(path) as image:
        return getattr(image, 'n_frames', 1) > 1


frames = decode_raster(args.input, fit=args.fit)
if args.input.is_dir():
    fps = args.fps or 30
    indices, durations = equalize_durations(len(frames), 1000 / fps)
    frames = frames[indices]
elif animated(args.input):
    durations = image_durations(args.input)
    if len(durations) != len(frames):
        fps = args.fps or 30
        indices, durations = equalize_durations(len(frames), 1000 / fps)
        frames = frames[indices]
elif len(frames) == 1:
    durations = [1000]
else:
    fps, seconds = video_info(args.input)
    frame_ms = seconds * 1000 / len(frames) if seconds else 1000 / (args.fps or fps or 30)
    # A long clip cannot exceed 255 frames on the panel; keep the timing exact.
    if len(frames) > 480:
        step = len(frames) / 480
        keep = [int(index * step) for index in range(480)]
        frames = frames[keep]
        frame_ms *= step
    indices, durations = equalize_durations(len(frames), frame_ms)
    frames = frames[indices]

if args.loop_ms:
    durations = [round((index + 1) * args.loop_ms / len(frames)) - round(index * args.loop_ms / len(frames))
                 for index in range(len(frames))]

count, source_count, size = pack_animation(name, frames, durations, args.output,
                                           delta=not args.no_delta, preview_frames=args.preview_frames)
print(f'PACKED {name}: {count}/{source_count} frames, {sum(durations)} ms loop, {size:,} bytes -> {args.output / (name + ".motif")}')
