#!/usr/bin/env python3
"""Side-by-side audit of the annotation tutorial renders (#125 v2) against the validated Paper boards.

  python tests/onboarding/annotation_compare.py <renders dir> [--pairs <out dir>] [--dpi 96|120] [--welcome <base onboarding renders dir>]

Paper exports: design/annotation-onboarding-evidence/paper-v2/<board>@96.png (1x) and @120.png (1.25x) of « #125 v2 —
Tutoriel annotation (validé) »: A1 … A7 (cyan), A2-violet … A4-violet, E1b (the browser frame with the card), E2 (the
welcome window's step 6, compared with the base onboarding's board 6 render, --welcome).
The exports are tagged « Display P3 Gamut with sRGB Transfer » (#0099A0 is stored as 67, 151, 158): they are brought to
sRGB through their own profile first. Only regions whose pixels do not depend on the instant of the capture are measured
(the GrainGradient behind the top of the boards is animated, Paper caught it at an arbitrary frame): text block, footer,
progress, and the opaque surfaces of each step. Mean absolute difference per channel, 0..255. With --pairs it writes
Paper | render | diff x4 images (whole boards, and 3x crops of each region).
"""
import io
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageCms, ImageStat

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design/annotation-onboarding-evidence/paper-v2'
COMMON = {'title + body': (100, 410, 620, 500), 'footer left': (19, 531, 140, 563), 'footer right': (560, 531, 701, 563), 'progress': (310, 540, 410, 554)}
SITE = {'page header': (152, 66, 568, 99), 'page form': (166, 110, 554, 222)}
REGIONS = {
    'A1': {**COMMON, 'picked button': (81, 179, 321, 223), 'note card': (335, 177, 636, 277)},
    'A2': {**COMMON, **SITE, 'framed button': (160, 204, 332, 270), 'coach pill': (350, 228, 520, 264)},
    'A3': {**COMMON, 'page header': (112, 66, 528, 99), 'framed button': (120, 204, 292, 270), 'note': (306, 198, 607, 278)},
    'A4': {**COMMON, **SITE, 'wide button': (166, 228, 426, 264), 'measure': (166, 270, 426, 286), 'done pill': (438, 228, 512, 264)},
    'A5': {**COMMON, 'local row': (122, 86, 598, 130), 'file row': (122, 148, 598, 192), 'tooltip': (334, 272, 598, 320)},
    'A6': {**COMMON, 'card': (182, 114, 538, 250)},
    'A7': {**COMMON, 'keys': (230, 120, 490, 176), 'status pill': (278, 196, 442, 228)},
    'A2-violet': {**COMMON, **SITE, 'framed button': (160, 204, 332, 270), 'coach pill': (350, 228, 520, 264)},
    'A3-violet': {**COMMON, 'page header': (112, 66, 528, 99), 'framed button': (120, 204, 292, 270), 'note': (306, 198, 607, 278)},
    'A4-violet': {**COMMON, **SITE, 'wide button': (166, 228, 426, 264), 'measure': (166, 270, 426, 286), 'done pill': (438, 228, 512, 264)},
}
CARD = (744, 80, 1060, 258)   # E1b: the card in Paper's 1180 x 815 frame


def mean(img):
    return sum(ImageStat.Stat(img).mean[:3]) / 3


def srgb(path):
    img = Image.open(path); icc = img.info.get('icc_profile'); img = img.convert('RGB')
    if icc:
        img = ImageCms.profileToProfile(img, ImageCms.ImageCmsProfile(io.BytesIO(icc)), ImageCms.createProfile('sRGB'), outputMode='RGB')
    return img


def sheet(paper, mine, out):
    diff = ImageChops.difference(paper, mine)
    w, h = paper.size; z = 3 if w < 700 else 1
    s = Image.new('RGB', (w * z, h * z * 3 + 8), (255, 0, 255))
    s.paste(paper.resize((w * z, h * z), Image.NEAREST), (0, 0)); s.paste(mine.resize((w * z, h * z), Image.NEAREST), (0, h * z + 4))
    s.paste(diff.point(lambda v: min(255, v * 4)).resize((w * z, h * z), Image.NEAREST), (0, 2 * h * z + 8)); s.save(out)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__); return 2
    renders = Path(sys.argv[1])
    arg = lambda name, default=None: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
    pairs = Path(arg('--pairs')) if arg('--pairs') else None
    dpi = arg('--dpi', '96'); k = 1.25 if dpi == '120' else 1.0
    welcome = Path(arg('--welcome')) if arg('--welcome') else None
    if pairs:
        pairs.mkdir(parents=True, exist_ok=True)
    worst = 0.0
    for board, regions in REGIONS.items():
        pp, mp = PAPER / f'{board}@{dpi}.png', renders / f'{board}@{dpi}.png'
        if not pp.exists() or not mp.exists():
            continue
        paper, mine = srgb(pp), Image.open(mp).convert('RGB')
        if mine.size != paper.size:
            mine = mine.resize(paper.size, Image.LANCZOS)
        diff = ImageChops.difference(paper, mine); parts = []
        for name, box in regions.items():
            b = tuple(round(v * k) for v in box)
            d = mean(diff.crop(b)); worst = max(worst, d); parts.append(f'{name} {d:.2f}')
            if pairs:
                sheet(paper.crop(b), mine.crop(b), pairs / f'{board}@{dpi}-{name.replace(" ", "-").replace("+", "and")}.png')
        print(f'{board}@{dpi}: ' + ', '.join(parts))
        if pairs:
            w, h = paper.size; s = Image.new('RGB', (w * 3 + 16, h), (255, 0, 255))
            s.paste(paper, (0, 0)); s.paste(mine, (w + 8, 0)); s.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0)); s.save(pairs / f'{board}@{dpi}.png')
    # E1b: the card alone.
    pp, mp = PAPER / f'E1b@{dpi}.png', renders / f'E1b-card@{dpi}.png'
    if pp.exists() and mp.exists():
        paper = srgb(pp).crop(tuple(round(v * k) for v in CARD)); mine = Image.open(mp).convert('RGB')
        if mine.size != paper.size:
            print(f'E1b card: size {mine.size} vs Paper {paper.size}'); mine = mine.resize(paper.size, Image.LANCZOS)
        inner = (round(2 * k), round(2 * k), paper.size[0] - round(2 * k), paper.size[1] - round(2 * k))   # inside the rounded edge (the browser page shows through Paper's corners)
        d = mean(ImageChops.difference(paper, mine).crop(inner)); worst = max(worst, d)
        print(f'E1b@{dpi}: card {d:.2f}')
        if pairs:
            sheet(paper, mine, pairs / f'E1b@{dpi}-card.png')
    # E2: the welcome window's step 6 (base onboarding render « 6 »).
    if welcome:
        pp, mp = PAPER / f'E2@{dpi}.png', welcome / f'6@{dpi}.png'
        if pp.exists() and mp.exists():
            paper, mine = srgb(pp), Image.open(mp).convert('RGB')
            regions = {**{n: b for n, b in COMMON.items() if n != 'progress'}, 'picked button': (81, 179, 321, 223), 'note card': (335, 177, 636, 277)}
            diff = ImageChops.difference(paper, mine); parts = []
            for name, box in regions.items():
                b = tuple(round(v * k) for v in box); dd = mean(diff.crop(b)); worst = max(worst, dd); parts.append(f'{name} {dd:.2f}')
                if pairs:
                    sheet(paper.crop(b), mine.crop(b), pairs / f'E2@{dpi}-{name.replace(" ", "-").replace("+", "and")}.png')
            print(f'E2@{dpi}: ' + ', '.join(parts))
            if pairs:
                w, h = paper.size; s = Image.new('RGB', (w * 3 + 16, h), (255, 0, 255))
                s.paste(paper, (0, 0)); s.paste(mine, (w + 8, 0)); s.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0)); s.save(pairs / f'E2@{dpi}.png')
    print(f'worst region mean difference: {worst:.2f} / 255')
    return 0


if __name__ == '__main__':
    sys.exit(main())
