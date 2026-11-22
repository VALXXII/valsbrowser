#!/usr/bin/env python3
"""Builds assets/onboarding/* from the Paper file's images (Onboarding page p-R-0).

Inputs (a folder holding the Paper file-assets, fetched once from
https://app.paper.design/file-assets/01M6Q7YT14ZKMGET54ASPNGQEC/<name>):
  01M6Z96QMKEC39VR0XYRQZ4MZB.png   logo fed to the HalftoneDots shader (1080 x 1080)
  01M7ZGSD41ZF4SYR0C5FNNSN2S.png   browser mock, no bookmarks bar (4720 x 1760 = 1180 x 440 @4x)
  01M7ZJ5TN12GAN427ATN2VCKGT.png   browser mock with the bookmarks bar (5532 x 2064 = 1383 x 516 @4x)
  01M6Z8XPRACTPQS43PRSP6RK57.svg   logo silhouette fed to the GemSmoke shader
plus the noise texture of @paper-design/shaders 0.0.82 (dist/get-shader-noise-texture.js).

Outputs:
  halftone-logo.png    the logo, untouched
  mock-plain.png       left 2400 px of the first mock at full resolution (screens 2 and 3 only show that part)
  mock-bookmarks.png   left 2812 px of the second mock, halved (1406 x 1032 = 2x of the drawn size)
  gem-shape.png        GemSmoke's processed image: R = roundness (Poisson solve, as toProcessedGemSmoke),
                       G = shape alpha, B = 255. 1024 px wide instead of 4096 (the shader keeps the 4096 texel size).
  noise.png            the library's 128 x 128 noise texture (GrainGradient)

Usage: python tools/onboarding/make_assets.py <paper-assets-dir> <shaders-npm-dir>
"""
import base64
import re
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/onboarding'


def flatten_path(d: str, steps: int = 48):
    """M / L / C / Z absolute commands only (the logo silhouette)."""
    tokens = re.findall(r'[MLCZ]|-?\d*\.?\d+(?:e-?\d+)?', d)
    pts, i, cur = [], 0, (0.0, 0.0)
    cmd = None
    def num():
        nonlocal i
        v = float(tokens[i]); i += 1
        return v
    while i < len(tokens):
        if tokens[i] in 'MLCZ':
            cmd = tokens[i]; i += 1
            if cmd == 'Z':
                continue
        if cmd == 'M' or cmd == 'L':
            cur = (num(), num()); pts.append(cur)
        elif cmd == 'C':
            p1 = (num(), num()); p2 = (num(), num()); p3 = (num(), num())
            for s in range(1, steps + 1):
                t = s / steps; u = 1 - t
                pts.append((u ** 3 * cur[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                            u ** 3 * cur[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
            cur = p3
        else:
            raise SystemExit('unsupported path command')
    return pts


def raster(points, vw, vh, width, height, pad_x, pad_y, ss=4):
    """Coverage (0..255) of the path drawn into (pad_x, pad_y, width - 2 pad_x, height - 2 pad_y)."""
    big = Image.new('L', (width * ss, height * ss), 0)
    sx = (width - 2 * pad_x) / vw; sy = (height - 2 * pad_y) / vh
    ImageDraw.Draw(big).polygon([((pad_x + x * sx) * ss, (pad_y + y * sy) * ss) for x, y in points], fill=255)
    return big.resize((width, height), Image.BOX)


def gem(svg_path: Path):
    text = svg_path.read_text(encoding='utf-8')
    vw, vh = (float(v) for v in re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', text).groups())
    points = flatten_path(re.search(r' d="([^"]+)"', text).group(1))
    # toProcessedGemSmoke: SVG scaled so its larger side is 4096, solved at min side 512.
    import math
    ow, oh = (4096.0, 4096.0 / (vw / vh)) if vw > vh else (4096.0 * (vw / vh), 4096.0)
    scale = 512 / min(ow, oh)
    w, h = round(ow * scale), round(oh * scale)
    pad_x, pad_y = math.ceil(w * .025), math.ceil(h * .025)
    cover = raster(points, vw, vh, w, h, pad_x, pad_y)
    mask = [1 if v else 0 for v in cover.getdata()]
    interior = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if not mask[i]:
                continue
            edge = x == 0 or x == w - 1 or y == 0 or y == h - 1
            if not edge:
                edge = not (mask[i - 1] and mask[i + 1] and mask[i - w] and mask[i + w] and mask[i - w - 1] and mask[i - w + 1] and mask[i + w - 1] and mask[i + w + 1])
            if not edge:
                interior.append(i)
    nb = {}
    for i in interior:
        x = i % w; y = i // w
        nb[i] = (i + 1 if x < w - 1 and mask[i + 1] else -1, i - 1 if x > 0 and mask[i - 1] else -1,
                 i - w if y > 0 and mask[i - w] else -1, i + w if y < h - 1 and mask[i + w] else -1)
    u = [0.0] * (w * h)
    red = [i for i in interior if ((i % w) + (i // w)) % 2 == 0]
    black = [i for i in interior if ((i % w) + (i // w)) % 2 == 1]
    omega, c = 1.9, 0.01
    for _ in range(32):
        for group in (red, black):
            for i in group:
                s = 0.0
                for n in nb[i]:
                    if n >= 0:
                        s += u[n]
                u[i] = omega * ((c + s) / 4) + (1 - omega) * u[i]
    for _ in range(3):
        tmp = u[:]
        for i in interior:
            s = 0.0; k = 0
            for n in nb[i]:
                if n >= 0:
                    s += tmp[n]; k += 1
            u[i] = (tmp[i] + s / k) * .5 if k else tmp[i]
    top = max(u[i] for i in interior)
    gray = Image.new('L', (w, h), 255)
    gray.putdata([round(255 * (1 - u[i] / top)) if mask[i] else 255 for i in range(w * h)])
    alpha_small = Image.new('L', (w, h), 0)
    alpha_small.putdata([255 if m else 0 for m in mask])
    # Output at a quarter of the library's 4096: canvas high-quality upscale ~ bicubic.
    fw, fh = 1024, round(1024 * oh / ow)
    r = gray.resize((fw, fh), Image.BICUBIC)
    up_alpha = alpha_small.resize((fw, fh), Image.BICUBIC)
    a = raster(points, vw, vh, fw, fh, math.ceil(ow * .025) * fw / ow, math.ceil(oh * .025) * fh / oh)
    rp, up, ap = r.load(), up_alpha.load(), a.load()
    out = Image.new('RGB', (fw, fh))
    op = out.load()
    for y in range(fh):
        for x in range(fw):
            if ap[x, y] == 0:
                op[x, y] = (255, 0, 255)
            else:
                op[x, y] = (0 if up[x, y] == 0 else rp[x, y], ap[x, y], 255)
    out.save(OUT / 'gem-shape.png', optimize=True)
    print('gem-shape.png', out.size, 'library size', round(ow), round(oh))


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__); return 2
    src, npm = Path(sys.argv[1]), Path(sys.argv[2])
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src / '01M6Z96QMKEC39VR0XYRQZ4MZB.png', OUT / 'halftone-logo.png')
    a = Image.open(src / '01M7ZGSD41ZF4SYR0C5FNNSN2S.png').convert('RGBA')
    a.crop((0, 0, 2400, 1760)).save(OUT / 'mock-plain.png', optimize=True)
    b = Image.open(src / '01M7ZJ5TN12GAN427ATN2VCKGT.png').convert('RGBA')
    b.crop((0, 0, 2812, 2064)).resize((1406, 1032), Image.LANCZOS).save(OUT / 'mock-bookmarks.png', optimize=True)
    js = (npm / 'package/dist/get-shader-noise-texture.js').read_text(encoding='utf-8')
    (OUT / 'noise.png').write_bytes(base64.b64decode(re.search(r'data:image/png;base64,([A-Za-z0-9+/=]+)', js).group(1)))
    print('noise.png', Image.open(OUT / 'noise.png').size, Image.open(OUT / 'noise.png').mode)
    gem(src / '01M6Z8XPRACTPQS43PRSP6RK57.svg')
    # The profile chip of the mocks (4x pixels): plate and glyph boxes, for design/onboarding-values.md.
    px = a.load()
    plate = [(x, y) for y in range(260) for x in range(260) if px[x, y][3] > 250 and abs(px[x, y][0] - 34) <= 2 and abs(px[x, y][1] - 51) <= 2 and abs(px[x, y][2] - 52) <= 2]
    glyph = [(x, y) for y in range(260) for x in range(260) if px[x, y][1] > 150 and px[x, y][0] < 140]
    for name, pts in (('plate', plate), ('glyph', glyph)):
        print(name, 'box @4x', min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts) + 1, max(p[1] for p in pts) + 1)
    print('plate centre colour', px[60, 60], 'glyph colour', px[94, 100], 'under the chip', px[20, 90], px[94, 12], px[94, 170])
    return 0


if __name__ == '__main__':
    sys.exit(main())
