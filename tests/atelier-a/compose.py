"""Side-by-side evidence for Val's « Atelier » part A: each offscreen render of the app next to its Paper board.

Inputs:
  design/atelier-a-evidence/paper/*.png      Paper exports (read only: Paper « Atelier de Val - à ne pas toucher »)
  build/atelier-a-qa/switch/*.png            tests/settings_layout_switch_test.cpp
  build/atelier-a-qa/split-frames/*.png      tests/ui/split_frames_ui_test.cpp
Output: design/atelier-a-evidence/*.png  (Paper | app | difference x4, plus 3x zooms of the drawn area)

  python tests/atelier-a/compose.py
"""
from __future__ import annotations
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design/atelier-a-evidence/paper'
QA = ROOT / 'build/atelier-a-qa'
OUT = ROOT / 'design/atelier-a-evidence'
GAP, BG, INK = 16, (255, 255, 255), (40, 40, 40)


def label(img: Image.Image, text: str) -> Image.Image:
    out = Image.new('RGB', (img.width, img.height + 22), BG)
    out.paste(img, (0, 22)); ImageDraw.Draw(out).text((4, 4), text, fill=INK)
    return out


def row(images: list[Image.Image]) -> Image.Image:
    w = sum(i.width for i in images) + GAP * (len(images) - 1); h = max(i.height for i in images)
    out = Image.new('RGB', (w, h), BG); x = 0
    for i in images: out.paste(i, (x, 0)); x += i.width + GAP
    return out


def column(images: list[Image.Image]) -> Image.Image:
    w = max(i.width for i in images); h = sum(i.height for i in images) + GAP * (len(images) - 1)
    out = Image.new('RGB', (w, h), BG); y = 0
    for i in images: out.paste(i, (0, y)); y += i.height + GAP
    return out


def diff(a: Image.Image, b: Image.Image) -> Image.Image:
    d = ImageChops.difference(a.convert('RGB'), b.convert('RGB'))
    return d.point(lambda v: min(255, v * 4))


def pair(paper: Path, app: Path, name: str, crop: tuple[int, int, int, int] | None = None, zoom: int = 1) -> None:
    p, a = Image.open(paper).convert('RGB'), Image.open(app).convert('RGB')
    if a.size != p.size: a = a.resize(p.size, Image.LANCZOS)
    if crop: p, a = p.crop(crop), a.crop(crop)
    if zoom > 1: p, a = p.resize((p.width * zoom, p.height * zoom), Image.NEAREST), a.resize((a.width * zoom, a.height * zoom), Image.NEAREST)
    row([label(p, f'Paper  {paper.name}'), label(a, f'app  {app.name}'), label(diff(p, a), 'difference x4')]).save(OUT / name)
    print('wrote', OUT / name)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    states = [('26YE-rest', 'rest'), ('271F-hover-side', 'hover-side'), ('274F-hover-compact', 'hover-compact'), ('26V2-hover-classic', 'hover-classic')]
    for board, state in states:
        for scale in ('100', '150'):
            paper, app = PAPER / f'{board}-{scale}.png', QA / f'switch/switch-{state}-{scale}.png'
            if paper.exists() and app.exists():
                pair(paper, app, f'switch-{state}-{scale}.png')
                k = 1.5 if scale == '150' else 1
                pair(paper, app, f'switch-{state}-{scale}-footer-zoom.png', (0, int(600 * k), int(332 * k), int(731 * k)), 3 if scale == '100' else 2)
    for board, side in (('27E0-frames-right', 'right'), ('2793-frames-left', 'left')):
        paper, app = PAPER / f'{board}-100.png', QA / f'split-frames/frames-{side}-100.png'
        if paper.exists() and app.exists():
            pair(paper, app, f'frames-{side}-100.png', (0, 70, 1180, 130))
            x0 = 592 if side == 'right' else 4
            pair(paper, app, f'frames-{side}-100-corners-zoom.png', (x0, 76, x0 + 584, 112), 3)
    morph = [QA / f'split-frames/morph-{k}.png' for k in (0, 15, 30, 50, 70, 85)] + [QA / 'split-frames/frames-right-100.png']
    if all(m.exists() for m in morph):
        column([label(Image.open(m).convert('RGB').crop((560, 70, 1180, 116)), m.name + ('  (morph spring, % of its settle time)' if 'morph' in m.name else '  (in place)')) for m in morph]).save(OUT / 'frames-morph.png')
        print('wrote', OUT / 'frames-morph.png')
    extra = [QA / 'split-frames/reduced-50.png', QA / 'split-frames/hover-close.png', QA / 'split-frames/narrow.png']
    if all(m.exists() for m in extra):
        column([label(Image.open(m).convert('RGB').crop((0, 70, 1180, 116)), m.name) for m in extra]).save(OUT / 'frames-states.png')
        print('wrote', OUT / 'frames-states.png')


if __name__ == '__main__':
    main()
