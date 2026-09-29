"""Validate generated assets before atomically replacing Metro-visible files."""
from contextlib import contextmanager
from pathlib import Path
import os
import struct
import tempfile
import zlib

from PIL import Image


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
        if magic != b'MOTF' or version != 1 or flags or (width, height) not in ((240,240),(480,480)) or not 1 <= count <= 255:
            raise ValueError(f'{path.name}: invalid animation header')
        offset = 12
        for _ in range(count):
            if offset + 6 > len(data):
                raise ValueError(f'{path.name}: truncated frame header')
            duration, length = struct.unpack_from('<HI', data, offset)
            offset += 6
            if not 20 <= duration <= 1000 or not 6 <= length <= width * height * 2 + 1024 or offset + length > len(data):
                raise ValueError(f'{path.name}: invalid frame')
            decoder = zlib.decompressobj()
            pixels = decoder.decompress(data[offset:offset+length], width * height * 2 + 1)
            if len(pixels) != width * height * 2 or not decoder.eof:
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
