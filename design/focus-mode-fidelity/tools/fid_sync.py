"""D1 check: in every captured frame of a slide, is the hole (where the page starts) in step with the painted panel?

    python design/focus-mode-fidelity/tools/fid_sync.py <raw dir> [tag ...]

For each `slidein-*`, `slideout-*` and `reversal-*` frame of a white-page run (fid_run.py --page white):
  - b: the first page row (white) under the panel at x = 860 DIP;
  - the visible panel must be the panel at rest (`on-shown.png`) translated so that its bottom edge lands on b: every
    row above b is compared with the rest panel shifted by (b - rest bottom).
A frame « disagrees » when more than 0.5 % of those pixels differ by more than 24/255 (columns within 80 DIP of the
pointer are left out: hover feedback). A cut toolbar (the page over the panel's lower part) and a stale toolbar strip
under the panel both fail; a frame with the panel wholly above the page (b at the page top) is not counted.
Also reports the panel's bottom corners at rest against the analytic ideal (radius 4, 1 px border, over white).
"""
import json, math, sys
from pathlib import Path
from PIL import Image

RAW = Path(sys.argv[1]); TAGS = sys.argv[2:] or sorted(p.name for p in RAW.iterdir() if p.name.startswith('head-') and p.name.endswith('-white'))
CANVAS = 0x17; BORDER = round(CANVAS + (255 - CANVAS) * 20 / 255)
def S(v, s): return int(math.floor(v * s + .5))
def white(p): return p[0] >= 0xF8 and p[1] >= 0xF8 and p[2] >= 0xF8

def ideal_corner(n, r, bw, fill, border, bg, ss=16):
    """n x n bottom-left corner, row 0 = top row of the crop (the panel's bottom row is row n-1); the shape toward +x/-y."""
    out = []
    for y in range(n):
        row = []
        for x in range(n):
            acc = 0.0
            for j in range(ss):
                for i in range(ss):
                    sx, sy = x + (i + .5) / ss, (n - y - 1) + (j + .5) / ss   # sy measured up from the bottom edge
                    arc = sx < r and sy < r; d = math.hypot(r - sx, r - sy) if arc else 0
                    if arc and d > r: acc += bg; continue
                    inner = sx >= bw and sy >= bw and (not arc or d <= r - bw)
                    acc += fill if inner else border
            row.append(acc / (ss * ss))
        out.append(row)
    return out

R = {}
for tag in TAGS:
    d = RAW / tag; meta = json.loads((d / 'run.json').read_text(encoding='utf-8')); s = meta['scale']; band = meta['band']
    shown = Image.open(d / 'on-shown.png').convert('RGB'); W, H = shown.size; x860 = S(860, s)
    rest = next(y for y in range(S(8, s), H) if white(shown.getpixel((x860, y))))
    px = shown.load(); res = {'scale': s, 'rest_bottom_px': rest, 'frames': {}}
    pointer_x = S(700, s); keep = [x for x in range(S(20, s), W - S(20, s), 2) if abs(x - pointer_x) > S(80, s)]
    for kind in ('slidein', 'slideout', 'reversal'):
        rows = []
        for f in sorted(d.glob(f'{kind}-*.png')):
            t = float(f.stem.split('-', 2)[2][:-2]); img = Image.open(f).convert('RGB'); q = img.load()
            b = next((y for y in range(S(4, s), img.height) if white(q[x860, y])), None)
            if b is None or b <= S(4, s) + 1 or b >= img.height: rows.append({'ms': t, 'b': b, 'skip': True}); continue
            dy = b - rest; bad = n = 0
            for y in range(0, b):
                ry = y - dy
                if ry < 0 or ry >= H: continue
                for x in keep:
                    a, c = q[x, y], px[x, ry]; n += 1
                    if max(abs(a[0] - c[0]), abs(a[1] - c[1]), abs(a[2] - c[2])) > 24: bad += 1
            frac = bad / max(1, n); rows.append({'ms': t, 'b': b, 'shift_px': dy, 'bad_px': bad, 'bad_frac': round(frac, 4), 'disagree': frac > .005})
        res['frames'][kind] = rows
    res['disagreeing_frames'] = sum(1 for k in res['frames'].values() for r in k if r.get('disagree'))
    res['compared_frames'] = sum(1 for k in res['frames'].values() for r in k if not r.get('skip'))
    # D2: the panel's bottom corners at rest over the white page vs the analytic ideal
    n = S(7, s); r4, bw = 4 * s, 1 * s; ideal = ideal_corner(n, r4, bw, CANVAS, BORDER, 255)
    left, right = S(4, s), W - S(4, s); corners = {}
    for name, x0, flip in (('bl', left, False), ('br', right - n, True)):
        err = []; cnt = 0
        for y in range(n):
            for x in range(n):
                ix = (n - 1 - x) if flip else x
                v = px[x0 + x, rest - n + y][0]; e = abs(v - ideal[y][ix]); err.append(e)
                if e > 24: cnt += 1
        corners[name] = {'max_err': round(max(err)), 'px_err_gt_24': cnt}
    res['panel_bottom_corners_vs_ideal'] = corners
    R[tag] = res
    print(f"{tag}: {res['disagreeing_frames']} of {res['compared_frames']} slide frames disagree; bottom corners {corners}")
(RAW / 'sync.json').write_text(json.dumps(R, indent=1), encoding='utf-8')
