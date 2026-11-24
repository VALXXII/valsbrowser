#!/usr/bin/env python3
"""Side-by-side audit of the address field's « Local » pill (#127, Paper « Browser Design » › « #127 — Panneau latéral » ›
« Proposition — Adresse · pastille « Local » (validée) », the profile-coloured version) against the offscreen renders of
tests/ui/address_render_test.cpp.

    python tests/ui/local_pill_compare.py <renders dir> [--pairs <out dir>]

Paper export (1x): design/local-pill-evidence/paper/board.png (the 1540 x 440 board). Its « Barre du haut » fields
(250 x 32, #292929) are at x 160 (rest) and 430 (hover), y 156 (Personnel), 208 (Travail), 260 (Perso créa, violet). The
renders are the 1180-wide chrome at 96 DPI (address-local-<profile>[-hover]-96.png): the field starts at (121, 44). The
export is « Display P3 Gamut with sRGB Transfer », converted to sRGB first. Mean absolute difference per channel (0..255)
over the pill and over the pill + divider + address (the first 240 DIP of the field).
"""
from __future__ import annotations
import io
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageCms, ImageStat

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'design/local-pill-evidence/paper/board.png'
FIELD = (121, 44)                                   # the top bar's field in the 1180-wide render
ROWS = {'personnel': 156, 'travail': 208, 'violet': 260}
COLUMNS = {'': 160, '-hover': 430}
REGIONS = {'pill': (6, 6, 65, 26), 'pill + divider + address': (0, 0, 240, 32)}


def srgb(path: Path) -> Image.Image:
    img = Image.open(path); icc = img.info.get('icc_profile'); img = img.convert('RGB')
    if icc:
        img = ImageCms.profileToProfile(img, ImageCms.ImageCmsProfile(io.BytesIO(icc)), ImageCms.createProfile('sRGB'), outputMode='RGB')
    return img


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__); return 2
    renders = Path(sys.argv[1])
    pairs = Path(sys.argv[sys.argv.index('--pairs') + 1]) if '--pairs' in sys.argv else None
    board = srgb(BOARD)
    worst = 0.0
    sheets = []
    for profile, y in ROWS.items():
        for state, x in COLUMNS.items():
            paper = board.crop((x, y, x + 250, y + 32))
            full = Image.open(renders / f'address-local-{profile}{state}-96.png').convert('RGB')
            mine = full.crop((FIELD[0], FIELD[1], FIELD[0] + 250, FIELD[1] + 32))
            line = []
            for name, box in REGIONS.items():
                v = sum(ImageStat.Stat(ImageChops.difference(paper.crop(box), mine.crop(box))).mean[:3]) / 3
                worst = max(worst, v); line.append(f'{name} {v:.2f}')
            print(f'{profile}{state or "-rest"}: ' + ', '.join(line))
            sheets.append((paper, mine))
    if pairs:
        pairs.mkdir(parents=True, exist_ok=True)
        w, h, z = 240, 32, 3
        sheet = Image.new('RGB', (2 * w * z + 16, len(sheets) * (h * z + 8)), (255, 0, 255))
        for i, (paper, mine) in enumerate(sheets):
            sheet.paste(paper.crop((0, 0, w, h)).resize((w * z, h * z), Image.NEAREST), (0, i * (h * z + 8)))
            sheet.paste(mine.crop((0, 0, w, h)).resize((w * z, h * z), Image.NEAREST), (w * z + 16, i * (h * z + 8)))
        sheet.save(pairs / 'local-pill@96.png')   # left Paper, right the app; rows: Personnel, Travail, violet × rest, hover
    print(f'worst region mean difference: {worst:.2f} / 255')
    return 0


if __name__ == '__main__':
    sys.exit(main())
