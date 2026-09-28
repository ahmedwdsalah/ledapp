# Toyota Ember

Native 480 × 480, 30 fps, three-second seamless loop. Toyota emblem stays fixed and fully visible; only ambient red light and two fine arcs move. No reveal, closing, or opening animation.

`frames/` contains lossless source frames. `toyota-ember.mp4` is the full-frame-rate review copy. `toyota-ember.webp` is the full-frame-rate app preview. `toyota-ember.gif` is a reduced-frame-rate preview only; do not use it as the display master.

Regenerate with `python3 render.py`. Requires Pillow, NumPy, FFmpeg, and `rsvg-convert`.

Emblem source: [Toyota Europe DX Playbook](https://dx-playbook.toyota-europe.com/fundamentals/logos/). The saved SVG is used without changing its path geometry.

`python3 hardware/tools/pack-toyota.py` packs the frames into the 480 × 480 RGB565 `.motif` file uploaded by the app. The firmware decompresses each frame without upscaling. EAF remains a candidate for future benchmark; this asset uses the working device transport until an EAF player is proven on this board.
