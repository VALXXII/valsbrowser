#!/usr/bin/env python3
"""Side-by-side audit of the annotation onboarding renders (#125) against the Paper boards.

  python tests/onboarding/annotation_compare.py <renders dir> [--pairs <out dir>] [--dpi 96|120]

Paper exports: design/annotation-onboarding-evidence/paper/A1@96.png … A4@120.png (export of the artboards at 1x and
1.25x). For each board it prints the mean absolute difference per channel (0..255) over regions whose pixels do not
depend on the instant of the capture: the GrainGradient behind the top of the boards is animated, Paper caught it at
an arbitrary frame, so only opaque parts are measured (text block, footer, the illustrations' opaque surfaces).
With --pairs it writes Paper | render | diff x4 images, whole boards and 3x crops of each region.
"""
import io
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageCms, ImageStat

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design/annotation-onboarding-evidence/paper'
COMMON = {'title + body': (100, 410, 620, 500), 'footer left': (19, 531, 140, 563), 'footer right': (560, 531, 701, 563), 'progress': (320, 540, 400, 554)}
REGIONS = {
    'A1': {**COMMON, 'picked button': (81, 179, 321, 223), 'note card': (335, 177, 636, 277)},
    'A2': {**COMMON, 'local row': (122, 106, 598, 150), 'tooltip': (344, 230, 598, 278)},
    'A3': {**COMMON, 'card': (182, 114, 538, 250)},
    'A4': {**COMMON, 'keys': (230, 120, 490, 176), 'status pill': (278, 196, 442, 228)},
}


def mean(img):
    return sum(ImageStat.Stat(img).mean[:3]) / 3


def srgb(path):
    """Paper exports are tagged « Display P3 Gamut with sRGB Transfer »: their pixel values are P3 values (#0099A0 is
    stored as 67, 151, 158). The renders are sRGB: the export is brought to sRGB through its own profile."""
    img = Image.open(path)
    icc = img.info.get('icc_profile')
    img = img.convert('RGB')
    if icc:
        img = ImageCms.profileToProfile(img, ImageCms.ImageCmsProfile(io.BytesIO(icc)), ImageCms.createProfile('sRGB'), outputMode='RGB')
    return img


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__); return 2
    renders = Path(sys.argv[1])
    pairs = Path(sys.argv[sys.argv.index('--pairs') + 1]) if '--pairs' in sys.argv else None
    dpi = sys.argv[sys.argv.index('--dpi') + 1] if '--dpi' in sys.argv else '96'
    k = 1.25 if dpi == '120' else 1.0
    if pairs:
        pairs.mkdir(parents=True, exist_ok=True)
    worst = 0.0
    for board, regions in REGIONS.items():
        paper = srgb(PAPER / f'{board}@{dpi}.png')
        mine = Image.open(renders / f'{board}@{dpi}.png').convert('RGB')
        if mine.size != paper.size:
            print(f'{board}: size {mine.size} vs Paper {paper.size}')
            mine = mine.resize(paper.size, Image.LANCZOS)
        diff = ImageChops.difference(paper, mine)
        parts = []
        for name, box in regions.items():
            b = tuple(round(v * k) for v in box)
            d = mean(diff.crop(b)); worst = max(worst, d); parts.append(f'{name} {d:.2f}')
            if pairs:
                w, h = b[2] - b[0], b[3] - b[1]; z = 3
                sheet = Image.new('RGB', (w * z, h * z * 3 + 8), (255, 0, 255))
                sheet.paste(paper.crop(b).resize((w * z, h * z), Image.NEAREST), (0, 0))
                sheet.paste(mine.crop(b).resize((w * z, h * z), Image.NEAREST), (0, h * z + 4))
                sheet.paste(diff.crop(b).point(lambda v: min(255, v * 4)).resize((w * z, h * z), Image.NEAREST), (0, 2 * h * z + 8))
                sheet.save(pairs / f'{board}@{dpi}-{name.replace(" ", "-").replace("+", "and")}.png')
        print(f'{board}@{dpi}: ' + ', '.join(parts))
        if pairs:
            w, h = paper.size
            sheet = Image.new('RGB', (w * 3 + 16, h), (255, 0, 255))
            sheet.paste(paper, (0, 0)); sheet.paste(mine, (w + 8, 0)); sheet.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0))
            sheet.save(pairs / f'{board}@{dpi}.png')
    print(f'worst region mean difference: {worst:.2f} / 255')
    return 0


if __name__ == '__main__':
    sys.exit(main())
