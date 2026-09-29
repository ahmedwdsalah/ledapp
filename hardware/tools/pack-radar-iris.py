#!/usr/bin/env python3
"""Render circular radar and procedural iris loops at native display resolution."""
from pathlib import Path
from animation_output import pack_animation
import math

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'assets/device-animations'
SIZE, SCALE = 480, 2
axis = (np.arange(SIZE * SCALE, dtype=np.float32) + .5) / SCALE - SIZE / 2
x, y = np.meshgrid(axis, axis)
r = np.hypot(x, y)
a = np.arctan2(y, x)
mask = np.clip((235-r)*2, 0, 1)


def line(value, width):
    return np.exp(-np.square(value / width))


def finish(rgb):
    return Image.fromarray(np.clip(rgb * mask[:, :, None], 0, 255).astype(np.uint8)).resize(
        (SIZE, SIZE), Image.Resampling.LANCZOS)


def radar(t):
    phase = math.tau * t
    behind = (phase - a) % math.tau
    sweep = np.exp(-behind / .43) * (r < 222)
    beam = line(np.minimum(behind, math.tau-behind), .009) * (r < 222)
    grid = sum(line(r-radius, .65) for radius in (55, 110, 165, 221))
    spoke = line(np.sin(a*6)*r, .55) * (r < 221)
    tick = line(np.sin(a*60), .15) * (r > 214) * (r < 221)
    major = line(np.sin(a*12), .09) * (r > 209) * (r < 222)
    edge = line(r-230, 1.0)
    energy = .28*grid + .075*spoke + .22*tick + .34*major + .18*edge + .55*sweep + .85*beam
    rgb = np.stack([energy*36, energy*220, energy*105], axis=-1)
    for radius, angle in ((96,-.7),(155,1.8),(187,-2.4),(65,2.8)):
        age = (phase-angle) % math.tau
        visibility = math.exp(-age/1.4)
        distance = np.hypot(x-radius*math.cos(angle),y-radius*math.sin(angle))
        dot = line(distance,2.7)*visibility
        halo = line(distance,7)*visibility
        ping = line(distance-(5+age*7),.8)*math.exp(-age*2)
        rgb += (dot*.95 + halo*.28 + ping*.3)[:,:,None]*np.array([100,255,160])
    core = line(r,2.5)
    rgb += core[:,:,None]*np.array([180,255,210])
    return finish(rgb)


# Deterministic radial fibers; no frame-to-frame noise or texture flicker.
rng = np.random.default_rng(714)
fibers = np.zeros_like(r)
fine = np.zeros_like(r)
for frequency in range(23, 290, 17):
    offset = rng.uniform(0,math.tau)
    fibers += np.sin(a*frequency + offset + 1.3*np.sin(r/37+offset)) / 12
for frequency in (311,433,577):
    fine += np.sin(a*frequency + .8*np.sin(r/14)) / 3
angular = .5 + .5*np.sin(a*91+1.4*np.sin(a*17))


def iris(t):
    phase = math.tau*t
    pupil = 67 + 5*math.sin(phase)
    u = np.clip((r-pupil)/(228-pupil),0,1)
    detail = .53 + .42*fibers + .095*fine
    folds = .5+.5*np.sin(40*u + 3*fibers + np.sin(a*37))
    furrows = np.clip((angular-.30)*2,0,1) * np.power(np.clip(np.sin(np.pi*u),0,1),.55)
    texture = np.clip(detail + .17*folds - .27*furrows,0,1)
    gold = np.exp(-np.square((u-.17)/.19))
    outer = np.clip((1-u)*7,0,1)
    light = .77+.23*np.cos(a+1.4)
    red = (95 + 88*gold)*texture*outer*light
    green = (53 + 59*gold)*texture*outer*light
    blue = (172 - 62*gold)*texture*outer*light
    rgb = np.stack((red,green,blue),axis=-1)
    # Fine collarette and dark pupil remain attached as the pupil breathes.
    collarette = line(u-(.20+.024*np.sin(a*43)), .012)
    rgb += collarette[:,:,None]*np.array([40,25,28])
    pupil_edge = np.clip((r-pupil)*1.3,0,1)
    rgb *= pupil_edge[:,:,None]
    rgb *= np.clip((230-r)/7,0,1)[:,:,None]
    # Corneal reflections drift slowly, without rotating the eye texture.
    gx = -77 + 4*math.sin(phase)
    gy = -100 + 3*math.cos(phase)
    reflection = np.exp(-(((x-gx)/26)**6+((y-gy)/38)**6))* .63
    small = line(np.hypot(x-57,y+82),5)*.72
    reflection *= np.clip((r-pupil-5)/12,0,1)*(r<219)
    rgb += (reflection+small)[:,:,None]*np.array([165,190,230])
    return finish(rgb)


def pack(name, renderer, count, duration):
    # Test periodicity independently of the frame indexing.
    assert np.max(np.abs(np.asarray(renderer(0),dtype=int)-np.asarray(renderer(1),dtype=int))) <= 1
    frames = [np.asarray(renderer(i/count), dtype=np.uint8) for i in range(count)]
    durations = [duration] * count
    length, source_count, size = pack_animation(name, frames, durations, TARGET)
    print(f'PASS {name}: {length}/{source_count} frames, {count*duration} ms, {size:,} bytes', flush=True)


if __name__ == '__main__':
    pack('emerald-radar',radar,60,50)
    pack('violet-iris',iris,36,90)
