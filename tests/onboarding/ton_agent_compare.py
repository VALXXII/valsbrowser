#!/usr/bin/env python3
"""Side-by-side audit of the « Ton agent » renders (worker B) against the Paper boards.

  python tests/onboarding/ton_agent_compare.py [<renders dir>] [--pairs <out dir>]

Paper exports (1x): design/ton-agent-evidence/paper/<board>@96.png of « Onboarding agents — Ton agent (maquette prête) »:
Claude Code 1 Choisir … 4 Erreur, Codex 1 Choisir … 5 Erreur (720 × 580), and « Chat — premier message, rien n'est prêt »
(380 × 664, compared by tests/chat-panel/ton_agent_block.py). Renders: build/ton-agent-render (tests/onboarding/
run_ton_agent.py). The exports may carry a Display P3 profile: they are brought to sRGB first. Only regions whose pixels do
not depend on the instant of the capture are measured (the GrainGradient behind the top is animated): the card (inside its
rounded edge), the text block, the footer and the progress. Mean absolute difference per channel, 0..255. With --pairs:
Paper | render | diff x4 images.
"""
import io
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageCms, ImageStat

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design/ton-agent-evidence/paper'
COMMON = {'title + body': (100, 410, 620, 500), 'footer left': (19, 531, 140, 563), 'footer right': (530, 531, 701, 563), 'progress': (310, 540, 410, 554)}
def card(top, h):
    return {'card': (162, round(top) + 2, 558, round(top + h) - 2)}
BOARDS = {
    'claude-1-choisir': {**COMMON, **card(141.5, 157)},
    'claude-2-installation': {**COMMON, **card(123, 157)},
    'claude-3-pret': {**COMMON, **card(140, 160)},
    'claude-4-erreur': {**COMMON, **card(72, 228)},
    'codex-1-choisir': {**COMMON, **card(141.5, 157)},
    'codex-2-installation': {**COMMON, **card(123, 157)},
    'codex-3-a-connecter': {**COMMON, **card(141.5, 157)},
    'codex-4-pret': {**COMMON, **card(140, 160)},
    'codex-5-erreur': {**COMMON, **card(72, 228)},
}


def mean(img):
    return sum(ImageStat.Stat(img).mean[:3]) / 3


def srgb(path):
    img = Image.open(path); icc = img.info.get('icc_profile'); img = img.convert('RGB')
    if icc:
        img = ImageCms.profileToProfile(img, ImageCms.ImageCmsProfile(io.BytesIO(icc)), ImageCms.createProfile('sRGB'), outputMode='RGB')
    return img


def main() -> int:
    renders = Path(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else ROOT / 'build/ton-agent-render'
    pairs = Path(sys.argv[sys.argv.index('--pairs') + 1]) if '--pairs' in sys.argv else None
    if pairs:
        pairs.mkdir(parents=True, exist_ok=True)
    worst = 0.0
    for board, regions in BOARDS.items():
        pp, mp = PAPER / f'{board}@96.png', renders / f'{board}@96.png'
        if not pp.exists() or not mp.exists():
            print(f'{board}: missing {"Paper export" if not pp.exists() else "render"}'); continue
        paper, mine = srgb(pp), Image.open(mp).convert('RGB')
        if mine.size != paper.size:
            print(f'{board}: size {mine.size} vs Paper {paper.size}'); mine = mine.resize(paper.size, Image.LANCZOS)
        diff = ImageChops.difference(paper, mine); parts = []
        for name, box in regions.items():
            d = mean(diff.crop(box)); worst = max(worst, d); parts.append(f'{name} {d:.2f}')
        print(f'{board}: ' + ', '.join(parts))
        if pairs:
            w, h = paper.size; s = Image.new('RGB', (w * 3 + 16, h), (255, 0, 255))
            s.paste(paper, (0, 0)); s.paste(mine, (w + 8, 0)); s.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0)); s.save(pairs / f'{board}.png')
            b = regions['card']; z = 3; pc, mc = paper.crop(b), mine.crop(b); cw, ch = pc.size
            c = Image.new('RGB', (cw * z, ch * z * 3 + 8), (255, 0, 255))
            c.paste(pc.resize((cw * z, ch * z), Image.NEAREST), (0, 0)); c.paste(mc.resize((cw * z, ch * z), Image.NEAREST), (0, ch * z + 4))
            c.paste(ImageChops.difference(pc, mc).point(lambda v: min(255, v * 4)).resize((cw * z, ch * z), Image.NEAREST), (0, 2 * ch * z + 8)); c.save(pairs / f'{board}-card.png')
    print(f'worst region mean difference: {worst:.2f} / 255')
    return 0


if __name__ == '__main__':
    sys.exit(main())
