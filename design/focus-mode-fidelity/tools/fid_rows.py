"""Per-element comparison of the panel's rows with the normal chrome (normal mode laid out on W-10 DIP).
python fid_rows.py <raw/tag> : for each element box (chrome-local DIP), the best sub-search offset and its diff."""
import sys, json, math
from pathlib import Path
from PIL import Image, ImageChops
d = Path(sys.argv[1]); m = json.loads((d / 'run.json').read_text()); s = m['scale']; band = m['band']
shown = Image.open(d / 'on-shown.png').convert('RGB')
refs = {f.name: Image.open(f).convert('RGB') for f in sorted(d.glob('off-w*px.png'))}
def S(v): return int(math.floor(v * s + .5))
def boxes(Wd):  # chrome-local DIP boxes (x0, y0, x1, y1) of the elements; right-aligned ones from the right edge
    b = {'profile plate': (4, 2, 44, 38), 'tab': (44, 2, 216, 38), '+ button': (216, 4, 250, 36),
         'window buttons': (Wd - 118, 2, Wd, 38), 'back/forward': (4, 42, 80, 78), 'reload': (80, 42, 120, 78),
         'address field (left part)': (120, 42, 420, 78), 'address field (right end, link/star)': (Wd - 260, 42, Wd - 150, 78),
         'right icons + separator + gear': (Wd - 150, 42, Wd, 78), 'gap rows (y 38..42)': (4, 38, Wd - 4, 42), 'bottom padding (y 76..80)': (4, 77, Wd - 4, 80)}
    if band: b['bookmarks band row'] = (4, 80, Wd - 4, 120)
    return b
out = {}
for name, ref in refs.items():
    Wd = ref.width / s; res = {}
    for el, (x0, y0, x1, y1) in boxes(Wd).items():
        box = (S(x0), S(y0), min(ref.width, S(x1)), S(y1)); rc = ref.crop(box); best = None
        for oy in range(S(5) - 2, S(5) + 3):
            for ox in range(S(5) - 3, S(5) + 4):
                pc = shown.crop((box[0] + ox, box[1] + oy, box[2] + ox, box[3] + oy)); df = ImageChops.difference(pc, rc).convert('L'); h = df.histogram()
                n = sum(h[1:]); mx = max(i for i, c in enumerate(h) if c)
                if best is None or (n, mx) < (best[0], best[1]): best = (n, mx, ox, oy)
        res[el] = {'diff_px': best[0], 'max': best[1], 'offset_px': best[2:], 'offset_dip': (round(best[2] / s, 3), round(best[3] / s, 3))}
    out[name] = res
print(json.dumps(out, indent=1))
