#!/usr/bin/env python3
"""Animate generated masters with FFmpeg displacement maps; pack native RGB565 loops."""
from pathlib import Path
from animation_output import publish_assets
import math
import struct
import subprocess
import tempfile
import zlib

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'assets/device-animations'
SIZE, RENDER, FRAMES, LOOP_MS = 480, 960, 40, 4000
axis = np.arange(RENDER, dtype=np.float32) / 2
x, y = np.meshgrid(axis, axis)


def coordinates(subject, phase):
    if subject == 'violet-iris':
        dx, dy = x-250, y-229
        radius = np.maximum(np.hypot(dx,dy), .001)
        pupil, outer = 84, 230
        expanded = pupil + 6*math.sin(phase)
        source_radius = np.where(radius < expanded, radius*pupil/expanded,
            pupil+(radius-expanded)*(outer-pupil)/(outer-expanded))
        source_radius = np.where(radius < outer, source_radius, radius)
        # Keep the corneal window reflection stationary above the moving iris.
        reflection = np.exp(-(((x-66)/47)**6+((y-173)/105)**6))
        shift = (source_radius/radius-1)*(1-reflection)
        sx, sy = x+dx*shift, y+dy*shift
    else:
        # Localized head tilt, breathing and ear movement; background stays still.
        weight = np.clip((y-95)/100,0,1)
        angle = .019*math.sin(phase)
        dx,dy = x-282,y-287
        sx = x + weight*(dx*(math.cos(angle)-1)+dy*math.sin(angle))
        sy = y + weight*(-dx*math.sin(angle)+dy*(math.cos(angle)-1)-1.8*math.sin(phase*2))
        nose = np.exp(-(((x-364)/65)**2+((y-383)/48)**2))
        sy += .9*math.sin(phase*3)*nose
        ear = np.exp(-(((x-167)/54)**2+((y-162)/47)**2))
        sx += 1.3*math.sin(phase*2)*ear
    outside = (x-240)**2+(y-240)**2 > 238**2
    return [np.where(outside,65535,np.clip(c*2,0,RENDER-1)).round().astype('<u2') for c in (sx,sy)]


def render(subject, destination):
    master = ROOT / f'assets/animation-masters/{subject}/master.png'
    start = coordinates(subject,0)
    end = coordinates(subject,math.tau)
    assert all(np.array_equal(a,b) for a,b in zip(start,end)), 'Loop mapping is not periodic'
    with tempfile.TemporaryDirectory() as temp:
        paths = [Path(temp)/'x.raw',Path(temp)/'y.raw']
        with paths[0].open('wb') as fx, paths[1].open('wb') as fy:
            for index in range(FRAMES):
                mx,my = coordinates(subject,math.tau*index/FRAMES)
                fx.write(mx.tobytes());fy.write(my.tobytes())
        cmd = ['ffmpeg','-v','error','-y','-loop','1','-framerate','10','-i',str(master)]
        for path in paths:
            cmd += ['-f','rawvideo','-pixel_format','gray16le','-video_size',f'{RENDER}x{RENDER}',
                    '-framerate','10','-i',str(path)]
        cmd += ['-filter_complex',f'[0:v]scale={RENDER}:{RENDER}:flags=lanczos,format=rgb24[s];'
                f'[s][1:v][2:v]remap=fill=black,scale={SIZE}:{SIZE}:flags=lanczos,format=rgb24[v]',
                '-map','[v]','-frames:v',str(FRAMES),'-f','rawvideo',str(destination)]
        subprocess.run(cmd,check=True)


def pack(subject,name):
    with tempfile.TemporaryDirectory() as temp:
        rawpath=Path(temp)/'frames.rgb'
        render(subject,rawpath)
        raw=rawpath.read_bytes()
    assert len(raw)==FRAMES*SIZE*SIZE*3
    rgb=np.frombuffer(raw,dtype=np.uint8).reshape(FRAMES,SIZE,SIZE,3)
    compressed=[]
    for frame in rgb:
        pixels=frame.astype(np.uint16)
        pixels=((pixels[:,:,0]>>3)<<11)|((pixels[:,:,1]>>2)<<5)|(pixels[:,:,2]>>3)
        data=pixels.astype('<u2').tobytes()
        payload=zlib.compress(data,9)
        assert zlib.decompress(payload)==data
        compressed.append(payload)
    # Preserve spatial and color detail; choose the most frames that fit the device.
    for count in range(FRAMES,1,-1):
        indices=[i*FRAMES//count for i in range(count)]
        if 12+sum(6+len(compressed[i]) for i in indices)<=4*1024*1024:
            break
    assert count>=12, 'Too few frames at native quality'
    durations=[round((i+1)*LOOP_MS/count)-round(i*LOOP_MS/count) for i in range(count)]
    output=bytearray(struct.pack('<4sBBHHH',b'MOTF',1,0,SIZE,SIZE,count))
    for index,duration in zip(indices,durations):
        payload=compressed[index]
        output.extend(struct.pack('<HI',duration,len(payload)));output.extend(payload)
    assert sum(durations)==LOOP_MS and len(output)<=4*1024*1024
    with publish_assets(TARGET) as staging:
        (staging/f'{name}.motif').write_bytes(output)
        # The app preview uses the exact frame selection and timing sent to the device.
        frames=[Image.frombytes('RGB',(SIZE,SIZE),rgb[index].tobytes()) for index in indices]
        frames[0].save(staging/f'{name}-preview.png')
        frames[0].save(staging/f'{name}.webp',save_all=True,append_images=frames[1:],
                       duration=durations,loop=0,lossless=True,method=4)
        with Image.open(staging/f'{name}.webp') as preview:
            assert 1 < preview.n_frames <= count
            preview_duration = 0
            for index in range(preview.n_frames):
                preview.seek(index); preview.load()
                preview_duration += preview.info["duration"]
            assert preview_duration == LOOP_MS
    print(f'PASS {name}: {count} matching device/preview frames, {LOOP_MS}ms, {len(output):,} bytes',flush=True)


if __name__=='__main__':
    pack('violet-iris','violet-iris-real')
    pack('raccoon','curious-raccoon')
