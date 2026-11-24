"""Side-by-side of the column's off-screen renders (tests/ui/sidebar_render_test.cpp) and the Paper boards (#127).

  python tools/sidebar/compose.py build/sidebar-qa/renders design/127-sidebar-evidence

The render is the client area (1178 x 738); the Paper board is the window with its 1 DIP border (1180 x 740). The render
gets the same border and, where the app shows a WebView2 page, the page card (white, radius 8) at the page box the chrome
reported (boxes.txt), so the two read alike. Paper's page content (article text) and its letter-tile favicons are not
reproduced. Output: <out>/<name>.png = render | Paper | difference (amplified x4, the page box masked).
"""
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

renders, out = Path(sys.argv[1]), Path(sys.argv[2])
paper = out / 'paper'
boxes = {}
for line in (renders / 'boxes.txt').read_text(encoding='utf-8').splitlines():
    p = line.split()
    pop = tuple(float(v) for v in p[11:15]) if len(p) >= 15 else (0, 0, 0, 0)
    boxes[p[0]] = (tuple(float(v) for v in p[2:6]), p[7] == '1', float(p[9]), pop)
DARK_PAGE = {'06-nouvel-onglet'}  # the new tab page is the blank #171717 page

for name, (page, overlay, column, pop) in boxes.items():
    ref = paper / f'{name}.png'
    if not ref.exists():
        continue
    src = Image.open(renders / f'{name}.png').convert('RGB')
    win = Image.new('RGB', (1180, 740), (23, 23, 23))
    win.paste(src, (1, 1))
    d = ImageDraw.Draw(win, 'RGBA')
    l, t, r, b = page
    if name not in DARK_PAGE:
        card = Image.new('RGBA', (1180, 740), (0, 0, 0, 0))
        cd = ImageDraw.Draw(card)
        cd.rounded_rectangle((l + 1, t + 1, r + 1 - 1, b + 1 - 1), radius=8, fill=(255, 255, 255, 255))
        if overlay:  # the column over the page hides the page under it
            cd.rectangle((0, 0, column + 1, 740), fill=(0, 0, 0, 0))
        win = Image.alpha_composite(win.convert('RGBA'), card).convert('RGB')
        if overlay:
            over = src.crop((0, 0, int(column) + 1, 738))
            win.paste(over, (1, 1))
        if pop[2] > pop[0]:  # the compact « » » popover over the page (drawn by the chrome, a hole in the page)
            pl, pt, pr, pb = (int(v) for v in pop)
            win.paste(src.crop((pl, pt, pr + 1, pb + 1)), (pl + 1, pt + 1))
        d = ImageDraw.Draw(win, 'RGBA')
    d.rounded_rectangle((0, 0, 1179, 739), radius=8, outline=(255, 255, 255, 0x14), width=1)
    p = Image.open(ref).convert('RGB').resize((1180, 740))
    diff = ImageChops.difference(win, p).point(lambda v: min(255, v * 4))
    # The page's content (Paper's article) is not ours: mask it in the difference.
    dd = ImageDraw.Draw(diff)
    if name not in DARK_PAGE:
        dd.rectangle((max(l, column if overlay else l) + 1 + 8, t + 1 + 8, r - 8, b - 8), fill=(0, 0, 0))
    sheet = Image.new('RGB', (1180 * 3 + 40, 740 + 40), (40, 40, 40))
    for i, img in enumerate((win, p, diff)):
        sheet.paste(img, (10 + i * 1190, 30))
    ImageDraw.Draw(sheet).text((10, 8), f'{name}: code (left) | Paper (middle) | difference x4 (right)', fill=(220, 220, 220))
    sheet.save(out / f'{name}.png')
    print('wrote', out / f'{name}.png')
