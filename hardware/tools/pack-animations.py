#!/usr/bin/env python3
"""Pack the gallery's GIF previews into native 480x480 device animations."""
from pathlib import Path
from animation_output import decode_raster, image_durations, pack_animation

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/device-gifs'
OUTPUT = ROOT / 'assets/device-animations'


def spread_ms(total, count):
    return [round((index + 1) * total / count) - round(index * total / count) for index in range(count)]


for source in sorted(SOURCE.glob('*.gif')):
    durations = image_durations(source)
    frames = decode_raster(source)
    if len(frames) != len(durations):
        # Frames can merge while decoding; keep the authored loop time instead.
        durations = spread_ms(sum(durations), len(frames))
    count, source_count, size = pack_animation(source.stem, frames, durations, OUTPUT, preview=False)
    print(f'PASS {source.stem}: {count}/{source_count} frames, native 480x480, {size:,} bytes', flush=True)

print(f'Packed {len(list(OUTPUT.glob("*.motif")))} animations')
