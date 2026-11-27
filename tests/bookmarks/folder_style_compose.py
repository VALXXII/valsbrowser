"""#138 side-by-side sheets: each Paper board state (design/138-folders-evidence/paper, exported at 2x) next to the
production painters' offscreen renders at 192 DPI (tests/bookmarks/folder_style_render_test.cpp).

    python tests/bookmarks/folder_style_compose.py <renders dir> <out dir>

Renders are cropped to the part the board draws (the band's items, the side grid, the compact column) with the boxes
the render test writes (boxes.txt, DIP). Nothing is scaled: both sides are 2x.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
PAPER = ROOT / 'design' / '138-folders-evidence' / 'paper'
renders = Path(sys.argv[1]); out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
BG, INK, MUTED = (17, 17, 17), (230, 230, 230), (140, 140, 140)
FONT = ImageFont.truetype(str(ROOT / 'assets' / 'fonts' / 'InterDisplay-Regular.ttf'), 22)

boxes = {}
for line in (renders / 'boxes.txt').read_text(encoding='utf-8').splitlines():
    name, _, l, t, r, b = line.split()
    boxes[name] = tuple(float(v) for v in (l, t, r, b))

def img(name):
    return Image.open(renders / f'{name}@2x.png').convert('RGB')

def crop(name, pad=8):
    l, t, r, b = boxes[f'{name}@2x'] if f'{name}@2x' in boxes else boxes[name]
    return img(name).crop((int((l - pad) * 2) if l >= pad else 0, int((t - pad) * 2), int((r + pad) * 2), int((b + pad) * 2)))

def stack(parts, gap=24):
    w = max(p.width for _, p in parts) ; h = sum(p.height + 28 for _, p in parts) + gap * (len(parts) - 1)
    sheet = Image.new('RGB', (w, h), BG); d = ImageDraw.Draw(sheet); y = 0
    for label, p in parts:
        d.text((0, y + 2), label, fill=MUTED, font=FONT); sheet.paste(p, (0, y + 28)); y += p.height + 28 + gap
    return sheet

def side_by_side(state, title, paper, code):
    left = Image.open(PAPER / paper).convert('RGB')
    w = left.width + 64 + code.width; h = max(left.height, code.height) + 60
    sheet = Image.new('RGB', (w, h), BG); d = ImageDraw.Draw(sheet)
    d.text((0, 8), f'{title}   ·   gauche : Paper (2x)   ·   droite : rendu du code (192 DPI)', fill=INK, font=FONT)
    sheet.paste(left, (0, 40)); sheet.paste(code, (left.width + 64, 40))
    sheet.save(out / f'{state}.png'); print(out / f'{state}.png', sheet.size)

side_by_side('1-survol', '1 · Survol d\'un dossier (tiroir)', '1-survol@2x.png',
             stack([('Tiroir Favoris (survol du bouton palette)', img('1-tiroir-survol')), ('Info-bulle « Personnaliser » (C5 de l\'app)', img('1-info-bulle'))]))
side_by_side('2-selecteur-clavier', '2 · Sélecteur ouvert · focus clavier', '2-selecteur-clavier@2x.png', stack([('Tiroir Favoris', img('2-selecteur-clavier'))]))
side_by_side('3-clic-droit', '3 · Clic droit → « Personnaliser… »', '3-clic-droit@2x.png',
             stack([('Barre de favoris (Veille sous le pointeur)', crop('3-barre')), ('Menu du dossier', img('3-menu')), ('Popover', img('3-popover'))]))
side_by_side('4-sans-couleur', '4 · Sans couleur', '4-sans-couleur@2x.png', stack([('Tiroir Favoris', img('4-sans-couleur'))]))
side_by_side('5-nom-long', '5 · Nom long : fondu de 24, jamais « … »', '5-nom-long@2x.png',
             stack([('Barre de favoris', crop('5-barre')), ('Tiroir', img('5-tiroir')), ('Menu « Déplacer vers »', img('5-deplacer-vers')), ('Info-bulle de la grille', img('5-info-bulle'))]))
side_by_side('6-partout', '6 · Le dossier partout', '6-partout@2x.png',
             stack([('Grille du panneau (232)', crop('6-grille')), ('Barre de favoris', crop('6-barre')), ('Colonne compacte', crop('6-colonne', 4)), ('Tiroir Favoris', img('6-tiroir')), ('Menu « Déplacer vers »', img('6-deplacer-vers'))]))
