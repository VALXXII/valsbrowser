"""Pixel comparison of a capture with its Paper board export (#121 crashed-tab page, screen-share popover, frozen page).

    python tests/crash-screenshare/compare.py <capture.png> <paper.png> [--out diff.png] [--max-mean 6] [--max-box 2]
        [--surface X,Y] [--inset N] [--name-band I:left|center ...] [--skip-band I ...] [--crop-height N]

Same size required (crop or capture at the board's scale first). Reports, and checks:
  * mean absolute difference per channel (0-255), and the share of pixels off by more than 24;
  * the bounding box of the « ink » (pixels more than 12 away from the background colour: the pixel at --surface, by
    default (2, 2); a transparent Paper export is laid over the capture's colour at that spot first) in both images,
    and each edge's offset: the geometry (positions, sizes) must agree within --max-box pixels;
  * the same boxes per horizontal band of ink (icon, title, text lines, buttons), so a shifted row is named.
--inset N leaves N pixels at each edge out of the ink (a native window's 1 px border and rounded corners).
--name-band I:left|center: band I holds the SITE'S NAME, which differs between a test page (127.0.0.1:5207) and the
board (lemonde.fr, meet.google.com): only its top and its anchor (left edge, or centre for centred text) are checked,
its width and bottom are free (another name, other letters and descenders), and its rows are left out of the mean.
Writes a diff image (red = capture brighter, cyan = Paper brighter) when --out is given. Exit 1 when a check fails.
Needs Pillow (run with plain `python`, not `python -I`: Pillow lives in the user site-packages).
"""
from __future__ import annotations
import argparse
import sys
from PIL import Image, ImageChops


def ink_mask(img: Image.Image, bg, inset: int = 0, tol: int = 12) -> Image.Image:
    rgb = img.convert('RGB')
    diff = ImageChops.difference(rgb, Image.new('RGB', rgb.size, bg)).convert('L')
    mask = diff.point(lambda v: 255 if v > tol else 0)
    if inset:
        w, h = mask.size
        inner = Image.new('L', mask.size, 0); inner.paste(mask.crop((inset, inset, w - inset, h - inset)), (inset, inset)); mask = inner
    return mask


def bands(mask: Image.Image) -> list[tuple[int, int, int, int]]:
    """Horizontal bands of ink separated by empty rows: (left, top, right, bottom) each."""
    w, h = mask.size
    px = mask.load()
    rows = [any(px[x, y] for x in range(w)) for y in range(h)]
    out, y = [], 0
    while y < h:
        if not rows[y]:
            y += 1; continue
        top = y
        while y < h and rows[y]:
            y += 1
        box = mask.crop((0, top, w, y)).getbbox()
        if box:
            out.append((box[0], top, box[2], y))
    return out


def mean_of(diff: Image.Image, rows_out: list[tuple[int, int]]) -> tuple[float, float]:
    """Mean difference and share of pixels off by > 24 (%), rows in `rows_out` left out."""
    w, h = diff.size
    keep = [y for y in range(h) if not any(a <= y < b for a, b in rows_out)]
    if not keep:
        return 0.0, 0.0
    hist = [0] * 256
    for y in keep:
        for i, c in enumerate(diff.crop((0, y, w, y + 1)).histogram()):
            hist[i] += c
    n = w * len(keep)
    return sum(i * c for i, c in enumerate(hist)) / n, sum(hist[25:]) / n * 100


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('capture'); ap.add_argument('paper')
    ap.add_argument('--out'); ap.add_argument('--max-mean', type=float, default=6.0); ap.add_argument('--max-box', type=int, default=2)
    ap.add_argument('--skip-band', type=int, action='append', default=[], help='band index left out of the band checks (a known, decided difference)')
    ap.add_argument('--name-band', action='append', default=[], help="I:left or I:center, the band that holds the site's name")
    ap.add_argument('--surface', default='2,2', help='x,y of a background pixel (the page, or the window surface)')
    ap.add_argument('--inset', type=int, default=0, help='pixels left out of the ink at each edge (border, rounded corners)')
    ap.add_argument('--crop-height', type=int, default=0, help='compare only the top N pixel rows of both images')
    a = ap.parse_args()
    sx, sy = (int(v) for v in a.surface.split(','))
    names = {int(i): anchor for i, anchor in (s.split(':') for s in a.name_band)}
    cap = Image.open(a.capture).convert('RGB')
    ref = Image.open(a.paper)
    if ref.mode in ('RGBA', 'LA', 'P'):   # Paper exports a frame without its own fill as transparent: over the page's colour
        under = Image.new('RGBA', ref.size, cap.getpixel((2, 2)) + (255,)); under.alpha_composite(ref.convert('RGBA')); ref = under
    ref = ref.convert('RGB')
    if a.crop_height:
        cap, ref = cap.crop((0, 0, cap.size[0], min(a.crop_height, cap.size[1]))), ref.crop((0, 0, ref.size[0], min(a.crop_height, ref.size[1])))
        print(f'top {a.crop_height} rows only')
    failures = []
    if cap.size != ref.size:
        print(f'FAIL size: capture {cap.size} vs Paper {ref.size}')
        return 1
    bg_c, bg_r = cap.getpixel((sx, sy)), ref.getpixel((sx, sy))
    if max(abs(p - q) for p, q in zip(bg_c, bg_r)) > 2:
        failures.append(f'background at {sx},{sy}: capture {bg_c} vs Paper {bg_r}')
    mc, mr = ink_mask(cap, bg_c, a.inset), ink_mask(ref, bg_r, a.inset)
    rc, rr = bands(mc), bands(mr)
    print(f'bands: capture {len(rc)}, Paper {len(rr)}')
    name_rows = []
    for i, (x, y) in enumerate(zip(rc, rr)):
        if i in a.skip_band:
            print(f'  band {i}: capture {x} Paper {y} (left out: --skip-band)'); continue
        if i in names:
            name_rows.append((min(x[1], y[1]), max(x[3], y[3])))
            anchor = abs(x[0] - y[0]) if names[i] == 'left' else abs((x[0] + x[2]) - (y[0] + y[2])) / 2
            d = max(anchor, abs(x[1] - y[1]))
            print(f'  band {i}: capture {x} Paper {y} (site name: top and {names[i]} checked) {"ok" if d <= a.max_box else "OFF by " + str(d)}')
        else:
            d = max(abs(p - q) for p, q in zip(x, y))
            print(f'  band {i}: capture {x} Paper {y} {"ok" if d <= a.max_box else "OFF by " + str(d)}')
        if d > a.max_box:
            failures.append(f'band {i} off by {d} px')
    if len(rc) != len(rr):
        failures.append(f'{len(rc)} ink bands in the capture, {len(rr)} in Paper')
    if not names:
        bc, br = mc.getbbox(), mr.getbbox()
        print(f'ink box capture {bc} / Paper {br}')
        if not bc or not br or max(abs(x - y) for x, y in zip(bc, br)) > a.max_box:
            failures.append(f'ink box differs: {bc} vs {br}')
    diff = ImageChops.difference(cap, ref).convert('L')
    mean, off = mean_of(diff, name_rows)
    print(f'mean difference {mean:.2f} / 255, pixels off by > 24: {off:.2f} %' + (f' (rows of the site name left out: {name_rows})' if name_rows else ''))
    if mean > a.max_mean:
        failures.append(f'mean difference {mean:.2f} > {a.max_mean}')
    if a.out:
        brighter = ImageChops.subtract(cap.convert('L'), ref.convert('L')); darker = ImageChops.subtract(ref.convert('L'), cap.convert('L'))
        Image.merge('RGB', (brighter.point(lambda v: min(255, v * 4)), darker.point(lambda v: min(255, v * 4)), darker.point(lambda v: min(255, v * 4)))).save(a.out)
        print(f'diff image: {a.out}')
    for f in failures:
        print('FAIL ' + f)
    print('compare: ' + ('FAILED' if failures else 'OK'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
