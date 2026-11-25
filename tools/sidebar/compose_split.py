"""Split view in the column (#127): the column's tab list from tests/ui/sidebar_render_test.cpp next to the Paper board's
frames (« Proposition — Vue partagée dans la colonne (panneau + compact) » 2473-0, exported to
design/127-sidebar-evidence/paper/split-*.png).

  python tools/sidebar/compose_split.py build/sidebar-qa/renders design/127-sidebar-evidence

Panel: the board's column is 248 wide with a 1 DIP border and padding 8 (its rows 230 wide from x 9); the app's rows are
232 wide from x 8. The render is cropped so the first rows coincide (9 above and left of it), then code | Paper |
difference x4. Compact: the board draws a 48 DIP column with 32 DIP cells, the app's is 40 wide with 40 x 36 rows, so
the two are only shown side by side (the render's column from 5 above its first row). Everything is scaled x3.
"""
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

renders, out = Path(sys.argv[1]), Path(sys.argv[2])
paper = out / 'paper'
SCALE = 3
for line in (renders / 'split-boxes.txt').read_text(encoding='utf-8').splitlines():
    p = line.split()
    name, (left, top, right, bottom) = p[0], (float(v) for v in p[1:5])
    ref = paper / f'{name}.png'
    if not ref.exists():
        continue
    board = Image.open(ref).convert('RGB')
    w, h = board.size
    src = Image.open(renders / f'{name}.png').convert('RGB')
    compact = 'compact' in name
    if compact:
        crop = src.crop((0, int(top) - 5, 40, int(top) - 5 + h))
    else:
        crop = src.crop((int(left) - 9, int(top) - 9, int(left) - 9 + w, int(top) - 9 + h))
    panes = [crop, board]
    if not compact:
        panes.append(ImageChops.difference(crop, board).point(lambda v: min(255, v * 4)))
    big = [im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST) for im in panes]
    sheet = Image.new('RGB', (max(760, sum(im.width for im in big) + 20 * (len(big) + 1)), max(im.height for im in big) + 50), (40, 40, 40))
    x = 20
    for im in big:
        sheet.paste(im, (x, 40))
        x += im.width + 20
    label = 'code (left) | Paper (middle) | difference x4 (right)' if not compact else 'code: 40 DIP column, 40 x 36 rows (left) | Paper: 48 DIP board, 32 DIP cells (right)'
    ImageDraw.Draw(sheet).text((20, 12), f'{name}: {label}, x{SCALE}', fill=(220, 220, 220))
    sheet.save(out / f'{name}.png')
    print('wrote', out / f'{name}.png')
