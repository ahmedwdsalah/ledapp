"""Shared asset pipeline: decode any source, quantize at best quality, publish atomically."""
from contextlib import contextmanager
from pathlib import Path
import math
import os
import struct
import subprocess
import tempfile
import zlib

import numpy as np
from PIL import Image, ImageSequence


# Ordered 8x8 Bayer matrix: deterministic, so equal pixels always quantize to
# equal values. That keeps difference frames compressible and flat colors exact.
BAYER8 = np.array([
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
], dtype=np.float32)


def quantize(rgb, dither=True):
    """Round 8-bit RGB to RGB565 with ordered dithering; returns uint16 pixels."""
    value = np.asarray(rgb, dtype=np.float32)
    height, width = value.shape[:2]
    if dither:
        matrix = BAYER8[np.arange(height)[:, None] % 8, np.arange(width)[None, :] % 8]
        offset = (matrix + .5) / 64 - .5
    else:
        offset = 0.0
    # Round to the nearest level while the dither offset redistributes only the
    # in-between values; exactly representable colors stay untouched.
    red = np.clip(np.floor(value[..., 0] * (31 / 255) + offset + .5), 0, 31)
    green = np.clip(np.floor(value[..., 1] * (63 / 255) + offset + .5), 0, 63)
    blue = np.clip(np.floor(value[..., 2] * (31 / 255) + offset + .5), 0, 31)
    red = red.astype(np.uint16)
    green = green.astype(np.uint16)
    blue = blue.astype(np.uint16)
    return (red << 11) | (green << 5) | blue


def dequantize(pixels):
    """Expand RGB565 pixels back to 8-bit RGB so previews show device-exact pixels."""
    pixels = np.asarray(pixels, dtype=np.uint16)
    red = ((pixels >> 11) & 0x1F).astype(np.uint16)
    green = ((pixels >> 5) & 0x3F).astype(np.uint16)
    blue = (pixels & 0x1F).astype(np.uint16)
    red = ((red * 255 + 15) // 31).astype(np.uint8)
    green = ((green * 255 + 31) // 63).astype(np.uint8)
    blue = ((blue * 255 + 15) // 31).astype(np.uint8)
    return np.stack((red, green, blue), axis=-1)


def preview_image(pixels):
    return Image.fromarray(dequantize(pixels))


def frame_payloads(frames, delta=False, split=False):
    payloads = []
    previous = None
    for index, frame in enumerate(frames):
        pixels = frame if not delta or index == 0 else (frame ^ previous)
        data = pixels.astype('<u2').tobytes()
        if split:
            # Two independent zlib halves let the display inflate both in parallel.
            half = len(data) // 2
            top = zlib.compress(data[:half], 9)
            bottom = zlib.compress(data[half:], 9)
            payloads.append(struct.pack('<I', len(top)) + top + bottom)
        else:
            payloads.append(zlib.compress(data, 9))
        previous = frame
    return payloads


def animation_flags(delta=False, split=False):
    return (1 if delta else 0) | (2 if split else 0)


def animation_bytes(frames, durations, delta=False, split=False):
    """Pack quantized uint16 frames into a Motif animation byte stream."""
    height, width = frames[0].shape
    output = bytearray(struct.pack('<4sBBHHH', b'MOTF', 1, animation_flags(delta, split), width, height, len(frames)))
    for duration, payload in zip(durations, frame_payloads(frames, delta, split)):
        output.extend(struct.pack('<HI', duration, len(payload)))
        output.extend(payload)
    return bytes(output)


def decode_raster(source, size=480, fps=None, fit='cover'):
    """Decode any GIF/WebP/APNG/video into RGB frames, Lanczos-scaled to the panel."""
    scale = f'scale={size}:{size}:flags=lanczos'
    if fit == 'cover':
        filters = f'{scale}:force_original_aspect_ratio=increase,crop={size}:{size},format=rgb24'
    else:
        filters = (f'{scale}:force_original_aspect_ratio=decrease,'
                   f'pad={size}:{size}:(ow-iw)/2:(oh-ih)/2:black,format=rgb24')
    command = ['ffmpeg', '-v', 'error']
    if Path(source).is_dir():
        command += ['-framerate', str(fps or 30), '-pattern_type', 'glob', '-i', str(Path(source) / '*')]
    else:
        command += ['-i', str(source)]
    command += ['-vf', filters, '-f', 'rawvideo', '-']
    raw = subprocess.run(command, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.uint8).reshape(-1, size, size, 3)


def image_durations(path):
    """Per-frame delays of an animated raster, clamped to the transport range."""
    with Image.open(path) as image:
        return [max(20, min(1000, int(frame.info.get('duration', 70) or 70)))
                for frame in ImageSequence.Iterator(image)]


def video_info(path):
    """(fps, duration_seconds or None) for a video source."""
    result = subprocess.run(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
         '-show_entries', 'stream=r_frame_rate:format=duration', '-of', 'default=nw=1:nk=1', str(path)],
        capture_output=True, check=True).stdout.decode().split()
    fps = 30.0
    duration = None
    for value in result:
        if '/' in value:
            numerator, denominator = value.split('/')
            if float(denominator): fps = float(numerator) / float(denominator)
        else:
            try: duration = float(value)
            except ValueError: pass
    return fps, duration


def equalize_durations(frame_count, frame_ms, minimum=20):
    """Group source frames so each kept frame lasts at least `minimum` ms."""
    if frame_ms <= 0: frame_ms = 1000 / 30
    group = max(1, math.ceil(minimum / frame_ms))
    indices = list(range(group - 1, frame_count, group))
    if not indices or indices[-1] != frame_count - 1:
        indices.append(frame_count - 1)
    durations = []
    for order, index in enumerate(indices):
        start = indices[order - 1] + 1 if order else 0
        durations.append(max(1, round(frame_ms * (index - start + 1))))
    return indices, durations


def pack_animation(name, frames, durations, destination, delta=True, split=None, preview_frames=40, preview=True):
    """Emit the best-quality .motif the transport allows, plus device-exact previews.

    Rounds and Bayer-dithers each frame to RGB565, keeps the longest loop that
    fits the 4 MiB upload (at most 255 frames; time of dropped frames merges
    into their keepers so the loop length survives), validates every written
    file, and only then replaces the live assets. With split=None the packer
    compares both layouts and keeps whichever holds more frames.
    """
    destination = Path(destination)
    frames = [np.asarray(frame, dtype=np.uint8) for frame in frames]
    assert frames and len(frames) == len(durations), 'frames and durations must line up'
    for frame in frames:
        assert frame.shape[:2] == (480, 480), 'frames must be 480x480'
    durations = [int(duration) for duration in durations]
    pixels = [quantize(frame) for frame in frames]
    source_count = len(pixels)
    limit = min(source_count, 255)
    capacity = 4 * 1024 * 1024

    def build(count, split_flag):
        indices = [i * source_count // count for i in range(count)]
        merged = []
        for order, index in enumerate(indices):
            start = indices[order - 1] + 1 if order else 0
            merged.append(min(1000, max(20, sum(durations[start:index + 1]))))
        return indices, merged, animation_bytes([pixels[i] for i in indices], merged, delta=delta, split=split_flag)

    def search(split_flag):
        count = limit
        indices, merged, output = build(count, split_flag)
        # Jump straight toward the fitting count instead of re-encoding every step.
        while len(output) > capacity and count > 1:
            count = min(count - 1, max(1, count * capacity // len(output)))
            indices, merged, output = build(count, split_flag)
        return count, indices, merged, output, split_flag

    best = search(split if split is not None else True)
    if split is None and best[0] < limit:
        plain = search(False)
        if plain[0] > best[0]:
            best = plain
    count, indices, merged, output, split_used = best
    assert len(output) <= capacity, f'{name}: not even one frame fits the transport'
    with publish_assets(destination) as staging:
        (staging / f'{name}.motif').write_bytes(output)
        if preview:
            step = max(1, (count + preview_frames - 1) // preview_frames)
            images = [preview_image(pixels[i]) for i in indices[::step]]
            delays = [sum(merged[k:k + step]) for k in range(0, count, step)]
            images[0].save(staging / f'{name}-preview.png')
            images[0].save(staging / f'{name}.webp', save_all=True, append_images=images[1:],
                           duration=delays, loop=0, lossless=True, method=4)
    return count, source_count, len(output)


def validate_asset(path):
    if path.stat().st_size == 0:
        raise ValueError(f'{path.name}: empty asset')
    if path.suffix in ('.png', '.webp'):
        with Image.open(path) as image:
            for index in range(getattr(image, 'n_frames', 1)):
                image.seek(index)
                image.load()
    elif path.suffix == '.motif':
        data = path.read_bytes()
        if not 18 <= len(data) <= 4 * 1024 * 1024:
            raise ValueError(f'{path.name}: invalid animation size')
        magic, version, flags, width, height, count = struct.unpack_from('<4sBBHHH', data)
        if magic != b'MOTF' or version != 1 or flags > 3 or (width, height) != (480, 480) or not 1 <= count <= 255:
            raise ValueError(f'{path.name}: invalid animation header')
        offset = 12
        for _ in range(count):
            if offset + 6 > len(data):
                raise ValueError(f'{path.name}: truncated frame header')
            duration, length = struct.unpack_from('<HI', data, offset)
            offset += 6
            if not 20 <= duration <= 1000 or not 6 <= length <= width * height * 2 + 65536 or offset + length > len(data):
                raise ValueError(f'{path.name}: invalid frame')
            frame_bytes = width * height * 2
            if flags & 2:
                payload = data[offset:offset + length]
                if length < 16:
                    raise ValueError(f'{path.name}: invalid split frame')
                top_length = struct.unpack_from('<I', payload)[0]
                if not 6 <= top_length <= length - 4 - 6:
                    raise ValueError(f'{path.name}: invalid split frame')
                streams = (payload[4:4 + top_length], payload[4 + top_length:])
            else:
                streams = (data[offset:offset + length],)
            for stream in streams:
                decoder = zlib.decompressobj()
                expected = frame_bytes // len(streams)
                pixels = decoder.decompress(stream, expected + 1)
                if len(pixels) != expected or not decoder.eof:
                    raise ValueError(f'{path.name}: invalid frame pixels')
            offset += length
        if offset != len(data):
            raise ValueError(f'{path.name}: trailing data')


@contextmanager
def publish_assets(destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    # Same filesystem makes os.replace atomic; failed encodes leave live assets untouched.
    with tempfile.TemporaryDirectory(prefix='.asset-build-', dir=destination) as temp:
        staging = Path(temp)
        yield staging
        files = list(staging.iterdir())
        for path in files:
            validate_asset(path)
        for path in files:
            os.replace(path, destination / path.name)
