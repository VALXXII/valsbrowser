"""#121 fidelity check: compares a capture (offscreen render or real-window capture) with the Paper export of the
same board node (tests/e2e/autofill-reference/*.png, exported with Paper's export tool, never drawn by hand).
Transparent corners of the export are composed over the canvas #171717. The two images are aligned at the best offset
within +/- `--shift` px (Paper places boxes at sub-pixel positions), then compared. Prints one line and exits 1 when
the mean difference or the share of strongly different pixels is over the limits. Writes a 4x amplified diff image.
Usage: python compare_png.py <capture.png> <reference.png> [--limit-mean 6] [--limit-strong 3.0] [--shift 2] [--diff out.png]
"""
import argparse, sys
from PIL import Image, ImageChops

p = argparse.ArgumentParser()
p.add_argument('capture'); p.add_argument('reference')
p.add_argument('--limit-mean', type=float, default=6.0)      # mean absolute difference per channel (0-255)
p.add_argument('--limit-strong', type=float, default=3.0)    # % of pixels with a channel over 64 apart
p.add_argument('--shift', type=int, default=2)
p.add_argument('--size-tolerance', type=int, default=4)      # px of width / height difference accepted
p.add_argument('--diff')
p.add_argument('--resize', action='store_true')               # a capture at another scale (125 %, 150 %): resampled to the reference first
a = p.parse_args()

cap = Image.open(a.capture).convert('RGBA')
ref = Image.open(a.reference).convert('RGBA')
canvas = (23, 23, 23, 255)
cap = Image.alpha_composite(Image.new('RGBA', cap.size, canvas), cap).convert('RGB')
ref = Image.alpha_composite(Image.new('RGBA', ref.size, canvas), ref).convert('RGB')
if a.resize and (abs(cap.width - ref.width) > a.size_tolerance or abs(cap.height - ref.height) > a.size_tolerance):
    ratio = ref.width / float(cap.width)
    if abs(cap.height * ratio - ref.height) > a.size_tolerance:
        print('FAIL proportions %dx%d vs Paper %dx%d (%s)' % (cap.width, cap.height, ref.width, ref.height, a.capture)); sys.exit(1)
    cap = cap.resize((ref.width, ref.height), Image.LANCZOS)
if abs(cap.width - ref.width) > a.size_tolerance or abs(cap.height - ref.height) > a.size_tolerance:
    print('FAIL size %dx%d vs Paper %dx%d (%s)' % (cap.width, cap.height, ref.width, ref.height, a.capture)); sys.exit(1)
w, h = min(cap.width, ref.width) - 2 * a.shift, min(cap.height, ref.height) - 2 * a.shift
best = None
for dy in range(-a.shift, a.shift + 1):
    for dx in range(-a.shift, a.shift + 1):
        c = cap.crop((a.shift + dx, a.shift + dy, a.shift + dx + w, a.shift + dy + h)); r = ref.crop((a.shift, a.shift, a.shift + w, a.shift + h))
        d = ImageChops.difference(c, r)
        px = list(d.getdata()) if not hasattr(d, 'get_flattened_data') else list(d.get_flattened_data())
        mean = sum(sum(v) for v in px) / (3.0 * len(px))
        strong = 100.0 * sum(1 for v in px if max(v) > 64) / len(px)
        if best is None or mean < best[0]: best = (mean, strong, dx, dy, d)
mean, strong, dx, dy, d = best
ok = mean <= a.limit_mean and strong <= a.limit_strong
if a.diff: d.point(lambda v: min(255, v * 4)).save(a.diff)
print('%s %s: mean %.2f (limit %.1f), strong %.2f%% (limit %.1f%%), offset %+d,%+d px, %dx%d vs Paper %dx%d'
      % ('ok  ' if ok else 'FAIL', a.capture.replace('\\', '/').split('/')[-1], mean, a.limit_mean, strong, a.limit_strong, dx, dy, cap.width, cap.height, ref.width, ref.height))
sys.exit(0 if ok else 1)
