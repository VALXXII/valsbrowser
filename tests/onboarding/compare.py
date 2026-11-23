#!/usr/bin/env python3
"""Side-by-side audit of the onboarding renders against the Paper screenshots.

  python tests/onboarding/compare.py <renders dir> [--pairs <out dir>] [--suffix @96.png]

For each board (1, 2, 3, 4, 5, 5.1, 6, 7) it prints the mean absolute difference per channel (0..255) over
the whole frame and over the regions that do not depend on a clock (the animated shaders of boards 4 – 6 are
captured by Paper at an arbitrary instant), and the « ink mass » of a few text runs (how heavy the type reads:
sum of luminance over the background) as a ratio render / Paper. With --pairs it writes Paper | render | diff x4
images. Paper screenshots: design/onboarding-evidence/paper/<board>.jpg (720 x 580, from get_screenshot).
"""
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design/onboarding-evidence/paper'
BOARDS = ['1', '2', '3', '4', '5', '5.1', '6', '7']
# Regions whose pixels depend on the instant of the capture (animated shader nodes).
CLOCK = {'4': [(495, 456, 554, 513)], '5': [(495, 456, 554, 513)], '5.1': [(495, 456, 554, 513)], '6': [(0, 0, 720, 410)]}
TEXT = {
    '1': {'title 32/500': (190, 428, 530, 464), 'body 14/400': (200, 470, 520, 490), 'button 14/500': (28, 536, 152, 556)},
    '2': {'title 16/600': (310, 16, 432, 34), 'label 14/500': (296, 448, 352, 468), 'button 14/600': (28, 536, 80, 556)},
    '3': {'select 14/500': (188, 446, 292, 468), 'check 14/500': (258, 492, 378, 512)},
    '6': {'title 32/500': (240, 417, 480, 453), 'body 14/400': (110, 458, 610, 498)},
}


def mean_diff(a, b, skip=()):
    d = ImageChops.difference(a, b)
    if skip:
        d = d.copy()
        for box in skip:
            d.paste((0, 0, 0), box)
    return sum(ImageStat.Stat(d).mean) / 3


def ink(img, box):
    crop = img.crop(box).convert('L')
    px = list(crop.getdata())
    bg = sorted(px)[len(px) // 10]          # the dark background level
    return sum(max(0, p - bg) for p in px)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__); return 2
    renders = Path(sys.argv[1])
    pairs = Path(sys.argv[sys.argv.index('--pairs') + 1]) if '--pairs' in sys.argv else None
    suffix = sys.argv[sys.argv.index('--suffix') + 1] if '--suffix' in sys.argv else '@96.png'
    if pairs:
        pairs.mkdir(parents=True, exist_ok=True)
    worst = 0.0
    for board in BOARDS:
        paper = Image.open(PAPER / f'{board}.jpg').convert('RGB')
        mine = Image.open(renders / f'{board}{suffix}').convert('RGB')
        if mine.size != paper.size:
            mine = mine.resize(paper.size, Image.LANCZOS)
        whole, steady = mean_diff(paper, mine), mean_diff(paper, mine, CLOCK.get(board, ()))
        worst = max(worst, steady)
        line = f'board {board:>3}: mean diff {whole:5.2f} / 255, without clocked shaders {steady:5.2f}'
        for name, box in TEXT.get(board, {}).items():
            p, m = ink(paper, box), ink(mine, box)
            line += f' | {name}: {m / p:4.2f}' if p else ''
        print(line)
        if pairs:
            diff = ImageChops.difference(paper, mine).point(lambda v: min(255, v * 4))
            sheet = Image.new('RGB', (720 * 3 + 20, 580), (120, 0, 0))
            sheet.paste(paper, (0, 0)); sheet.paste(mine, (730, 0)); sheet.paste(diff, (1460, 0))
            sheet.save(pairs / f'{board}.png')
    print(f'worst steady difference: {worst:.2f} / 255')
    return 0


if __name__ == '__main__':
    sys.exit(main())
