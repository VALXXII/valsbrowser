"""Mode Focus fidelity audit: measurements on the captures of fid_run.py.

    python design/focus-mode-fidelity/tools/fid_measure.py design/focus-mode-fidelity/raw  > design/focus-mode-fidelity/measurements.json

Every value is read from the screen captures (device px) and converted to DIP. Ideal references (corners) are
computed analytically by supersampling the Paper geometry: rounded rect, radius r, 1 px #FFFFFF14 border.
"""
import json, math, sys
from pathlib import Path
from PIL import Image, ImageChops

RAW = Path(sys.argv[1]); CROPS = RAW.parent / 'crops'; CROPS.mkdir(exist_ok=True)
CANVAS, BORDER_A = 0x17, 0x14 / 255
def lum(p): return p[0]
def S(v, s): return int(math.floor(v * s + .5))
def load(tag, name):
    p = RAW / tag / f'{name}.png'
    return Image.open(p).convert('RGB') if p.exists() else None
def meta(tag): return json.loads((RAW / tag / 'run.json').read_text(encoding='utf-8'))

def diff_stats(a, b, box=None):
    if box: a, b = a.crop(box), b.crop(box)
    d = ImageChops.difference(a, b).convert('L'); hist = d.histogram(); n = sum(hist[1:]); mx = max((i for i, c in enumerate(hist) if c), default=0)
    return {'pixels_differing': n, 'max_channel_diff': mx, 'bbox': d.point(lambda v: 255 if v else 0).getbbox()}

def clusters(a, b, gap=6):
    """Column clusters of differing pixels (x ranges) — to name which element differs."""
    d = ImageChops.difference(a, b).convert('L'); w, h = d.size; px = d.load()
    cols = [x for x in range(w) if any(px[x, y] for y in range(h))]
    out = []
    for x in cols:
        if out and x - out[-1][1] <= gap: out[-1][1] = x
        else: out.append([x, x])
    res = []
    for x0, x1 in out:
        sub = d.crop((x0, 0, x1 + 1, h)); bb = sub.point(lambda v: 255 if v else 0).getbbox(); hist = sub.histogram()
        res.append({'x': [x0, x1], 'y': [bb[1], bb[3] - 1] if bb else None, 'n': sum(hist[1:]), 'max': max(i for i, c in enumerate(hist) if c)})
    return res

# ---- ideal corner: a rounded rect (corner at origin, the shape toward +x,+y), radius r, border bw, all in px ----
def ideal_corner(n, r, bw, fill, border_over_fill, bg, ss=16):
    img = Image.new('RGB', (n, n)); px = img.load()
    for j in range(n):
        for i in range(n):
            cin = cfill = 0
            for sj in range(ss):
                for si in range(ss):
                    x, y = i + (si + .5) / ss, j + (sj + .5) / ss
                    def inside(x, y, r):
                        if r <= 0: return x >= 0 and y >= 0
                        if x < r and y < r: return (x - r) ** 2 + (y - r) ** 2 <= r * r
                        return x >= 0 and y >= 0
                    if inside(x, y, r):
                        cin += 1
                        if bw and inside(x - bw, y - bw, max(r - bw, 0)): cfill += 1
            a_in, a_fill = cin / ss / ss, cfill / ss / ss
            a_border = a_in - a_fill if bw else 0
            if not bw: a_fill = a_in
            v = tuple(round(bg[k] * (1 - a_in) + fill[k] * a_fill + border_over_fill[k] * a_border) for k in range(3))
            px[i, j] = v
    return img
def corner_crop(img, x, y, n, fx, fy):
    """crop an n x n corner whose outer pixel corner is at (x, y), oriented so that the shape goes toward +x/+y."""
    box = (x if fx > 0 else x - n, y if fy > 0 else y - n, (x + n) if fx > 0 else x, (y + n) if fy > 0 else y)
    c = img.crop(box)
    if fx < 0: c = c.transpose(Image.FLIP_LEFT_RIGHT)
    if fy < 0: c = c.transpose(Image.FLIP_TOP_BOTTOM)
    return c
def compare_corner(c, ideal):
    d = ImageChops.difference(c, ideal).convert('L'); h = d.histogram()
    return {'max_err': max(i for i, v in enumerate(h) if v), 'mean_err': round(sum(i * v for i, v in enumerate(h)) / max(1, sum(h)), 2), 'px_err_gt_24': sum(h[25:])}
def zoom_pair(a, b, name, k=12, label=None):
    w, h = a.size; z = Image.new('RGB', (w * k * 2 + 12, h * k), (255, 0, 0))
    z.paste(a.resize((w * k, h * k), Image.NEAREST), (0, 0)); z.paste(b.resize((w * k, h * k), Image.NEAREST), (w * k + 12, 0)); z.save(CROPS / name)

def scan(img, x0, y0, dx, dy, pred, limit=400):
    x, y = x0, y0
    for k in range(limit):
        if pred(img.getpixel((x, y))): return k
        x += dx; y += dy
    return None

def ease(u, p1=(0.23, 1), p2=(0.32, 1)):
    if u <= 0: return 0.0
    if u >= 1: return 1.0
    lo, hi = 0.0, 1.0
    for _ in range(40):
        t = (lo + hi) / 2; x = 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0] + t ** 3
        lo, hi = (t, hi) if x < u else (lo, t)
    t = (lo + hi) / 2; return 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1] + t ** 3
def fit(samples, rising):
    """samples: (ms, progress 0..1) for frames in motion; returns best (t0, D, rms)."""
    best = None
    for D in range(100, 361, 4):
        for t0 in range(-60, 400, 2):
            e = 0
            for t, p in samples:
                q = ease((t - t0) / D); q = q if rising else 1 - q; e += (q - p) ** 2
            if best is None or e < best[2]: best = (t0, D, e)
    return best[0], best[1], round(math.sqrt(best[2] / max(1, len(samples))), 4)

R = {}
heads = sorted(p.name for p in RAW.iterdir() if p.name.startswith('head-'))
for tag in heads:
    m = meta(tag); s = m['scale']; band = m['band']; page = m['page']; res = R.setdefault(tag, {'scale': s})
    pagec = (0x0E,) * 3 if page == 'dark' else (0xFF,) * 3
    is_page = (lambda p: p[0] <= 0x10) if page == 'dark' else (lambda p: p[0] >= 0xF8 and p[1] >= 0xF8)
    off = load(tag, 'off-1180'); hid = load(tag, 'on-hidden'); shown = load(tag, 'on-shown')
    W_, H_ = hid.size
    # 5. Désactivé vs main
    btag = f"base-{round(s * 100)}-dark" + ('-band' if band else '')
    base_off = load(btag, 'off-1180') if (RAW / btag).exists() else None
    if base_off and page == 'dark':
        top = S(121 if band else 81, s) + 2
        res['desactive_vs_main_chrome'] = diff_stats(off, base_off, (0, 0, W_, top))
        res['desactive_vs_main_full'] = diff_stats(off, base_off)
        res['desactive_vs_main_clusters'] = clusters(off.crop((0, 0, W_, top)), base_off.crop((0, 0, W_, top)))
        res['off_again_vs_off'] = diff_stats(load(tag, 'off-again'), off, (0, 0, W_, top))
    # 1. Activé, hidden
    ym, xm = H_ // 2, W_ // 2 + S(200, s)
    res['hidden_inset_px'] = {'left': scan(hid, 0, ym, 1, 0, is_page), 'right': scan(hid, W_ - 1, ym, -1, 0, is_page),
                              'top': scan(hid, xm, 0, 0, 1, is_page), 'bottom': scan(hid, xm, H_ - 1, 0, -1, is_page)}
    res['hidden_inset_dip'] = {k: (round(v / s, 2) if v is not None else None) for k, v in res['hidden_inset_px'].items()}
    canvas_px = [hid.getpixel((x, y)) for x in range(0, W_, 7) for y in list(range(S(4, s) - 1)) + list(range(H_ - S(4, s) + 1, H_))]
    canvas_px += [hid.getpixel((x, y)) for y in range(S(10, s), H_ - S(10, s), 7) for x in list(range(S(4, s) - 1)) + list(range(W_ - S(4, s) + 1, W_))]
    res['hidden_canvas_colours'] = sorted({'#%02X%02X%02X' % p for p in canvas_px})
    n = S(8, s); ins = S(4, s); r6 = 6 * s
    ideal = ideal_corner(n, r6, 0, pagec, pagec, (CANVAS,) * 3)
    cs = {}
    for name, (x, y, fx, fy) in {'tl': (ins, ins, 1, 1), 'tr': (W_ - ins, ins, -1, 1), 'bl': (ins, H_ - ins, 1, -1), 'br': (W_ - ins, H_ - ins, -1, -1)}.items():
        c = corner_crop(hid, x, y, n, fx, fy); cs[name] = compare_corner(c, ideal)
        if name == 'tl': zoom_pair(ideal, c, f'{tag}-page-corner-tl-ideal-vs-build.png')
    res['hidden_page_corners_vs_ideal_r6'] = cs
    # 2. hit box
    res['hitbox'] = {k: v['revealed'] for k, v in m.get('hitbox', {}).items()}; res['hitbox_edge_px'] = m.get('hitbox_edge_px')
    # 3. shown
    H = 124 if band else 84
    cx = S(600, s); gy = S(4 + 1 + 41, s)   # the gap between the tab row and the toolbar row (chrome-local y 41)
    col = [shown.getpixel((cx, y))[0] for y in range(0, S(H + 12, s))]
    row = [shown.getpixel((x, gy))[0] for x in range(W_)]
    res['shown_column_x600_first_rows'] = col[:S(8, s)]
    res['shown_column_x600_bottom_rows'] = {y: col[y] for y in range(S(H, s) - 2, S(H + 8, s))}
    res['shown_row_gap_left'] = row[:S(8, s)]; res['shown_row_gap_right'] = row[-S(8, s):]
    brd = lambda v: v > CANVAS + 6
    tb = [y for y in range(S(8, s)) if brd(col[y])]; bb = [y for y in range(S(H - 4, s), S(H + 8, s)) if brd(col[y]) and not is_page(shown.getpixel((cx, y)))]
    lb = [x for x in range(S(8, s)) if brd(row[x])]; rb = [x for x in range(W_ - S(8, s), W_) if brd(row[x])]
    res['panel_border_rows_px'] = {'top': tb, 'bottom': bb, 'left': lb, 'right': rb}
    first_page = next(y for y in range(S(H - 4, s), len(col)) if is_page(shown.getpixel((cx, y))))
    res['panel_edges_dip'] = {'top': round(tb[0] / s, 2) if tb else None, 'bottom_excl': round(first_page / s, 2),
                              'left': round(lb[0] / s, 2) if lb else None, 'right_excl': round((rb[-1] + 1) / s, 2) if rb else None, 'W': round(W_ / s, 2)}
    res['panel_height_dip'] = round((first_page - (tb[0] if tb else 0)) / s, 2)
    res['panel_border_pixels'] = {'top': shown.getpixel((cx, tb[0])) if tb else None, 'bottom': shown.getpixel((cx, bb[-1])) if bb else None,
                                  'left': shown.getpixel((lb[0], gy)) if lb else None, 'right': shown.getpixel((rb[-1], gy)) if rb else None}
    res['panel_fill_pixel'] = shown.getpixel((S(4 + 1 + 400, s), S(4 + 1 + 41, s)))
    # no hairline: chrome-local rows 77..82 (between toolbar bottom 76 and the border) at x 600
    res['panel_rows_below_toolbar'] = [shown.getpixel((cx, S(4 + 1 + yy, s)))[0] for yy in range(77, H - 1)] if not band else None
    # page does not move: rows below the panel, whole width
    yb = first_page + 2
    res['page_still'] = diff_stats(shown, hid, (0, yb, W_, H_))
    # panel corners vs ideal (r4, 1 px border; outside = canvas at the top, page at the bottom)
    n4 = S(7, s); ins4 = S(4, s); b1 = max(1, round(1 * s)) if False else 1 * s
    fill, bof = (CANVAS,) * 3, tuple(round(CANVAS + (255 - CANVAS) * BORDER_A) for _ in range(3))
    top_ideal = ideal_corner(n4, 4 * s, 1 * s, fill, bof, (CANVAS,) * 3)
    bot_ideal = ideal_corner(n4, 4 * s, 1 * s, fill, bof, pagec)
    pc = {}
    bottom_y = first_page
    for name, (x, y, fx, fy, idl) in {'tl': (ins4, tb[0] if tb else ins4, 1, 1, top_ideal), 'tr': (W_ - ins4, tb[0] if tb else ins4, -1, 1, top_ideal),
                                      'bl': (ins4, bottom_y, 1, -1, bot_ideal), 'br': (W_ - ins4, bottom_y, -1, -1, bot_ideal)}.items():
        c = corner_crop(shown, x, y, n4, fx, fy); pc[name] = compare_corner(c, idl)
        zoom_pair(idl, c, f'{tag}-panel-corner-{name}-ideal-vs-build.png')
    res['panel_corners_vs_ideal_r4'] = pc
    # rows inside the panel vs the normal chrome laid out on W - 10 DIP
    best = None
    for f in sorted((RAW / tag).glob('off-w*px.png')):
        ref = Image.open(f).convert('RGB'); rw = ref.width
        hgt = S((120 if band else 80), s)
        for oy in range(S(5, s) - 2, S(5, s) + 3):
            for ox in range(S(5, s) - 2, S(5, s) + 3):
                if ox + rw > W_: continue
                pnl = shown.crop((ox, oy, ox + rw, oy + hgt)); rf = ref.crop((0, 0, rw, hgt))
                st = diff_stats(pnl, rf, (S(4, s), 0, rw - S(4, s), hgt))
                if best is None or st['pixels_differing'] < best[0]['pixels_differing']: best = (st, ox, oy, f.name, pnl, rf)
    st, ox, oy, fname, pnl, rf = best
    res['rows_vs_normal'] = {'ref': fname, 'offset_px': (ox, oy), 'offset_dip': (round(ox / s, 3), round(oy / s, 3)), **st,
                             'clusters': clusters(pnl.crop((S(4, s), 0, pnl.width - S(4, s), pnl.height)), rf.crop((S(4, s), 0, rf.width - S(4, s), rf.height)))}
    pnl.save(CROPS / f'{tag}-panel-content.png'); rf.save(CROPS / f'{tag}-normal-chrome-W-10.png')
    # 7. drawer
    d = m.get('drawer_px')
    if d:
        res['drawer'] = {'top_dip': round(d['top'] / s, 2), 'right_gap_dip': round(d['right_gap'] / s, 2), 'bottom_gap_dip': round(d['bottom_gap'] / s, 2), 'px': d}
        dimg = load(tag, 'drawer')
        if dimg:
            dl, dt, dr = d['left'], d['top'], W_ - d['right_gap']
            inner = dimg.getpixel((dl + S(3, s), dt + S(10, s))) if False else dimg.getpixel((dl + S(8, s), dt + S(8, s)))
            n6 = S(9, s); idl = ideal_corner(n6, 6 * s, 0, inner, inner, pagec)
            for name, (x, fx) in {'tl': (dl, 1), 'tr': (dr, -1)}.items():
                c = corner_crop(dimg, x, dt, n6, fx, 1); res['drawer'][f'corner_{name}_vs_ideal_r6_solid'] = compare_corner(c, idl)
                zoom_pair(idl, c, f'{tag}-drawer-corner-{name}-ideal-vs-build.png')
            res['drawer']['inner_colour'] = inner
    # 6. slides
    for kind in ('slidein', 'slideout', 'reversal', 'ctrlL'):
        frames = sorted((RAW / tag).glob(f'{kind}-*.png'))
        if not frames: continue
        rows = []
        for f in frames:
            t = float(f.stem.split('-', 2)[2][:-2]); img = Image.open(f).convert('RGB')
            b = next((y for y in range(S(4, s), img.height) if is_page(img.getpixel((S(860, s), y)))), None)
            # flash: non-page pixels below the panel bottom inside the page box (away from the page's top corners)
            flash = 0
            if b is not None and page == 'white':
                for y in range(b, img.height, 1):
                    for x in range(S(14, s), W_ - S(14, s), 3):
                        if not is_page(img.getpixel((x, y))): flash += 1
            # content: the visible panel equals the panel at rest, shifted up (slide from above)
            shift_ok = None
            if b is not None and b > S(12, s):
                dy = first_page - b; a1 = img.crop((S(20, s), max(0, b - S(40, s)), W_ - S(20, s), b)); a2 = shown.crop((S(20, s), max(0, b - S(40, s)) + dy, W_ - S(20, s), b + dy))
                shift_ok = diff_stats(a1, a2)['pixels_differing']
            rows.append({'ms': t, 'bottom_px': b, 'bottom_dip': round(b / s, 2) if b is not None else None, 'flash_px': flash, 'content_vs_rest_shifted_diff': shift_ok})
        res[kind] = rows
        rest = first_page
        mv = [(r['ms'], r['bottom_px'] / S(4 + H, s)) for r in rows if r['bottom_px'] is not None and S(4, s) < r['bottom_px'] < rest]
        if kind in ('slidein', 'slideout') and len(mv) >= 3:
            t0, D, rms = fit(mv, kind == 'slidein'); res[f'{kind}_fit'] = {'t0_ms': t0, 'duration_ms': D, 'rms_progress': rms, 'n_moving_frames': len(mv)}
print(json.dumps(R, indent=1, default=str))
