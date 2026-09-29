#!/usr/bin/env python3
"""Readers must see complete old/new assets, including during failed encodes."""
from io import BytesIO
from pathlib import Path
import sys
import tempfile
import threading

from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from animation_output import publish_assets

with tempfile.TemporaryDirectory() as temp:
    target=Path(temp)
    with publish_assets(target) as staging:
        Image.new('RGB',(480,480),'red').save(staging/'preview.webp')
    original=(target/'preview.webp').read_bytes()
    for failure in ('exception','empty','corrupt'):
        try:
            with publish_assets(target) as staging:
                (staging/'preview.webp').write_bytes(b'' if failure=='empty' else b'broken')
                assert (target/'preview.webp').read_bytes()==original
                if failure=='exception':raise RuntimeError('Encoder interrupted')
        except (RuntimeError,ValueError,OSError):pass
        else:raise AssertionError('Invalid asset was published')
        assert (target/'preview.webp').read_bytes()==original
    stopped=threading.Event();errors=[];reads=[0]
    def reader():
        while not stopped.is_set():
            try:
                data=(target/'preview.webp').read_bytes()
                assert data
                with Image.open(BytesIO(data)) as image:image.load()
                reads[0]+=1
            except Exception as error:errors.append(error)
    thread=threading.Thread(target=reader);thread.start()
    try:
        for i in range(20):
            with publish_assets(target) as staging:
                Image.new('RGB',(480,480),(i*10,50,70)).save(staging/'preview.webp',lossless=True)
    finally:stopped.set();thread.join()
    assert not errors and reads[0]>0,errors
    print(f'PASS: failed/empty/corrupt writes preserve old asset; {reads[0]} concurrent valid reads')
