#!/usr/bin/env python3
"""Pack the gallery's clean GIF previews into display-ready RGB565 frame streams."""
from pathlib import Path
import struct
import subprocess
import zlib
from PIL import Image, ImageSequence

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/device-gifs'
OUTPUT = ROOT / 'assets/device-animations'
FRAME_BYTES = 240 * 240 * 2
MAX_BYTES = 4 * 1024 * 1024
OUTPUT.mkdir(exist_ok=True)

for source in sorted(SOURCE.glob('*.gif')):
    with Image.open(source) as image:
        durations = [max(20, min(1000, int(frame.info.get('duration', 70)))) for frame in ImageSequence.Iterator(image)]
    raw = subprocess.run(
        ['ffmpeg', '-v', 'error', '-i', str(source), '-f', 'rawvideo', '-pix_fmt', 'rgb565le', '-'],
        capture_output=True, check=True,
    ).stdout
    if len(raw) != len(durations) * FRAME_BYTES or not 1 <= len(durations) <= 255:
        raise ValueError(f'{source.name}: frame count or dimensions changed during decoding')
    output = bytearray(struct.pack('<4sBBHHH', b'MOTF', 1, 0, 240, 240, len(durations)))
    for index, duration in enumerate(durations):
        compressed = zlib.compress(raw[index * FRAME_BYTES:(index + 1) * FRAME_BYTES], 1)
        output.extend(struct.pack('<HI', duration, len(compressed)))
        output.extend(compressed)
    if len(output) > MAX_BYTES:
        raise ValueError(f'{source.name}: {len(output)} bytes exceeds the display limit')
    (OUTPUT / f'{source.stem}.motif').write_bytes(output)

print(f'Packed {len(list(OUTPUT.glob("*.motif")))} animations')
