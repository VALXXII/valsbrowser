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


def feedback() -> None:
    """Val's part A feedback (2026-11-26): the three points, one picture each."""
    import json
    # 1. « BETA » on the version line hovered / keyboard-focused.
    hov, foc = QA / 'switch/beta-version-hover-150.png', QA / 'switch/beta-version-focus-150.png'
    if hov.exists() and foc.exists():
        crops = [Image.open(f).convert('RGB').crop((0, 960, 498, 1097)) for f in (hov, foc)]
        crops = [c.resize((c.width * 2, c.height * 2), Image.NEAREST) for c in crops]
        column([label(crops[0], '1. version line hovered: BETA stays (150 %)'), label(crops[1], '1. version line keyboard-focused: BETA stays (150 %)')]).save(OUT / 'feedback-1-beta-hover-focus.png')
        print('wrote', OUT / 'feedback-1-beta-hover-focus.png')
    # 2. The drawer kept open across the layout switches (rectangles measured in the window, drawer painted by itself).
    data = QA / 'layout-keep/layout-keep.json'
    if data.exists():
        tiles = []
        for s in json.loads(data.read_text(encoding='utf-8')):
            w, h = int(s['client'][0]), int(s['client'][1]); k = .5
            img = Image.new('RGB', (int(w * k), int(h * k)), (23, 23, 23)); d = ImageDraw.Draw(img)
            p = s['page']; d.rounded_rectangle([p[0] * k, p[1] * k, p[2] * k, p[3] * k], radius=3, fill=(165, 165, 165))
            if s['column']: d.rectangle([0, s['page'][1] * k, max(0, s['column'] - 4) * k, h * k], fill=(33, 33, 33))
            dr = s['drawer']; pic = Image.open(QA / 'layout-keep' / s['png']).convert('RGB')
            pic = pic.resize((int((dr[2] - dr[0]) * k), int((dr[3] - dr[1]) * k)), Image.LANCZOS); img.paste(pic, (int(dr[0] * k), int(dr[1] * k)))
            d.rectangle([0, 0, w * k - 1, h * k - 1], outline=(90, 90, 90))
            tiles.append(label(img, f"switch #{s['step']} -> {s['layout']}: Reglages still open, docked {'left, against the column' if s['column'] else 'right'}"))
        rows = [row(tiles[i:i + 2]) for i in range(0, len(tiles), 2)]
        column(rows).save(OUT / 'feedback-2-drawer-stays-open.png')
        print('wrote', OUT / 'feedback-2-drawer-stays-open.png')
    # 3. No frame around the active pane (pointer and keyboard), the pages filled in grey as the boards draw them.
    shots = [QA / 'split-frames/no-active-ring-pointer.png', QA / 'split-frames/no-active-ring-keyboard.png']
    if all(s.exists() for s in shots):
        out = []
        for s in shots:
            img = Image.open(s).convert('RGB'); d = ImageDraw.Draw(img)
            for box in ((4, 81, 587, 810), (592, 81, 1175, 810)): d.rectangle(box, fill=(165, 165, 165))   # the pages (child windows, not in the chrome's picture)
            d.text((800, 420), 'active pane (Paper)', fill=(40, 40, 40)); d.text((200, 420), 'other pane (YouTube)', fill=(40, 40, 40))
            crop = img.crop((520, 60, 1180, 160)).resize((1320, 200), Image.NEAREST)
            out.append(label(crop, ('3. after a click' if 'pointer' in s.name else '3. after F6 / Tab') + ': no frame around the active pane (gap, page edge and window edge: canvas only)'))
        column(out).save(OUT / 'feedback-3-no-active-pane-frame.png')
        print('wrote', OUT / 'feedback-3-no-active-pane-frame.png')


if __name__ == '__main__':
    main()
    feedback()
