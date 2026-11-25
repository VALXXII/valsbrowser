"""The #121 error-page cases and what the Paper boards say about them (shared by run.py and window_qa.py).

Values read with the Paper tools (get_jsx / get_tree_summary) on the file « Browser Design », page « #121 — Fenêtres
du moteur à notre design » (p-V-0), 2026-11-23. The boards' block PNGs (« Bloc erreur 420 », 1x, transparent) are in
tests/error-pages/boards/ (exported with the Paper tool, never edited).
"""
from __future__ import annotations
import json, re
from pathlib import Path

HERE = Path(__file__).resolve().parent
BOARDS = HERE / 'boards'
CANVAS = (0x17, 0x17, 0x17)   # #171717, the page and the boards' « Aperçu »

# Per case: the board node, the block height (420 wide), the title's text width, the text's height (lines x 20),
# the action buttons left to right (label, width, class), the sheet (width, height, rows) and the status/countdown box.
CASES: dict[str, dict] = {
    'offline': {'board': '1LXI-0 (Hors ligne, polished by Val)', 'height': 268, 'title_w': 181, 'body_h': 60,
                'buttons': [('Réessayer', 93, 'primary'), ('Paramètres réseau', 144, 'secondary')], 'status': (149, 44), 'icon': 'wo'},
    'insecure': {'board': '1LZS-0 (Site non sécurisé)', 'height': 228, 'buttons': [('Revenir', None, 'primary'), ('Continuer quand même', None, 'secondary')], 'icon': 'la',
                 'note': 'board text reads « établie.Si » (no space): the catalog keeps « établie. Si »'},
    'dns': {'board': '1M3D-0 (Adresse introuvable)', 'height': 188, 'title_w': 199, 'body_h': 40,
            'buttons': [('Rechercher « lemond.fr »', 184, 'primary'), ('Réessayer', 93, 'secondary')], 'icon': 'gs'},
    'refused': {'board': '1M2X-0 (Connexion refusée, sheet open, secondary hovered)', 'height': 334, 'title_w': 255, 'body_h': 40,
                'buttons': [('Réessayer', 93, 'primary'), ('Détails techniques', 142, 'secondary')], 'sheet': (340, 130, 3), 'icon': 'se',
                'code': 'ERR_CONNECTION_REFUSED'},   # night 24/11: a refusal fell back to the generic page (ERR_FAILED)
    'timeout': {'board': '1M2G-0 (Délai dépassé)', 'height': 258, 'title_w': 329, 'body_h': 40,
                'buttons': [('Réessayer maintenant', 165, 'primary'), ('Détails techniques', 142, 'secondary')], 'retry': (134, 54), 'icon': 'hg'},
    'restored': {'board': '1M20-0 (Reprise)', 'height': 248, 'title_w': 181, 'body_h': 40,
                 'buttons': [('Rechargement…', 129, 'primary'), ('Paramètres réseau', 144, 'secondary')], 'status': (128, 44), 'icon': 'wc'},
}
for name, node, sheet in (('cert-invalid', '1M5H-0', (340, 87, 2)), ('cert-expired', '1M52-0', (340, 87, 2)), ('cert-wrong-name', '1M4N-0', (420, 87, 2)),
                          ('cert-revoked', '1M48-0', (340, 87, 2)), ('cert-client', '1M3U-0', (420, 44, 1))):
    CASES[name] = {'board': f'{node} (certificat refusé)', 'height': 291 if sheet[2] == 2 else 248, 'body_h': 40,
                   'buttons': [('Retour', 69, 'secondary'), ('Réessayer', 93, 'primary')], 'sheet': sheet, 'icon': 'sh'}

# Board « Erreurs réseau, compléments » (1SYN-0, validated 2026-11-24; read with the Paper tool, 24/11 02:22).
for name, node, icon in (('reset', '1T17-0 (Connexion interrompue)', 'pd0'), ('unreachable', '1SZH-0 (Site injoignable)', 'co0'),
                         ('generic', '1SYR-0 (Page non chargée)', 'de0')):
    CASES[name] = {'board': node, 'height': 188, 'body_h': 40, 'icon': icon,
                   'buttons': [('Réessayer', 93, 'primary'), ('Détails techniques', 142, 'secondary')]}
CASES['timeout-open'] = {'board': '1T04-0 (Délai dépassé, détails ouverts)', 'height': 334, 'body_h': 40, 'icon': 'hg', 'code': 'ERR_TIMED_OUT',
                         'buttons': [('Réessayer maintenant', 165, 'primary'), ('Détails techniques', 142, 'secondary')], 'sheet': (340, 130, 3),
                         'open_bg': (62, 62, 62), 'essai': r'3e tentative · nouvel essai dans 1[0-6] s'}
# The copy button's states (1T2J-0: zone 20 x 20, radius 4, icon 14; « Copié » 1.5 s). Keyboard focus keeps the owner's
# A1 rule (1 px inset ring #FFFFFF80), not the board's 2 px accent ring. Alphas of #FFFFFF: 14 -> 20/255, 24 -> 36/255,
# 80 -> 128/255, CC -> 204/255.
COPY_STATES = {
    'copy-rest': {'bg': 0.0, 'fill': 128 / 255, 'icon': 'cc', 'tip': 0.0},
    'copy-hover': {'bg': 20 / 255, 'fill': 204 / 255, 'icon': 'cc', 'tip': 1.0},
    'copy-pressed': {'bg': 36 / 255, 'fill': 1.0, 'icon': 'cc'},
    'copy-copied': {'bg': 36 / 255, 'fill': 1.0, 'icon': 'ok'},
    'copy-focus': {'bg': 0.0, 'fill': 204 / 255, 'icon': 'cc', 'tip': 1.0, 'ring': True, 'night': True},   # needs a focused window
}
COPY_PAGE = 'timeout-open'   # the copy states are measured on this page
_OPEN = 'const S=document.getElementById("secondary");if(S.getAttribute("aria-expanded")!=="true")S.click();'
_COPY = 'const C=document.querySelector("[data-action=copy]"),r=C.getBoundingClientRect(),xy=Math.round(r.x+r.width/2)+","+Math.round(r.y+r.height/2);'

# The refused board shows its sheet open and « Détails techniques » hovered: the same state is set up here
# (a DOM click, then a DevTools mouse move over the button: no real mouse). Returns what the host does next:
# "x,y" / "hover:x,y", "press:x,y" (button held), "post:<message>" (as main answers), "wait:<ms>" or "".
PREPARE = {
    'refused': '(()=>{const S=document.getElementById("secondary");if(S.getAttribute("aria-expanded")!=="true")S.click();'
               'const r=S.getBoundingClientRect();return Math.round(r.x+r.width/2)+","+Math.round(r.y+r.height/2)})()',
    'timeout-open': '(()=>{' + _OPEN + 'return ""})()',
    'copy-rest': '(()=>{' + _OPEN + 'return ""})()',
    'copy-hover': '(()=>{' + _OPEN + _COPY + 'return "hover:"+xy})()',
    'copy-pressed': '(()=>{' + _OPEN + _COPY + 'return "press:"+xy})()',
    'copy-copied': '(()=>{' + _OPEN + 'return "post:copied"})()',
    'copy-focus': '(()=>{' + _OPEN + 'document.querySelector("[data-action=copy]").focus({focusVisible:true});return "wait:300"})()',
}

# Everything measured in the page (CSS px, 1x). Only visible elements count (the offline page carries the
# « Reprise » state hidden).
MEASURE_JS = r'''(()=>{const vis=e=>!!e&&e.getClientRects().length>0;
const R=e=>{if(!vis(e))return null;const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}};
const T=e=>{if(!vis(e))return null;const g=document.createRange();g.selectNodeContents(e);const r=g.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}};
const q=s=>[...document.querySelectorAll(s)].filter(vis);
const icon=q(".icon")[0];
return JSON.stringify({main:R(document.querySelector("main")),icon:R(icon),iconSvg:R(icon&&icon.querySelector("svg")),
iconId:icon&&icon.querySelector("linearGradient")?icon.querySelector("linearGradient").id:"",
text:R(q(".text")[0]),h1:R(q("h1")[0]),h1Text:T(q("h1")[0]),p:R(q("p")[0]),actions:R(q(".actions")[0]),
buttons:q(".actions button").map(b=>Object.assign({label:b.textContent,cls:b.className,bg:getComputedStyle(b).backgroundColor},R(b))),
sheet:R(q(".sheet")[0]),rows:q(".sheet .row").map(R),seps:q(".sheet .sep").map(R),copy:R(q(".sheet .cp")[0]),
code:q(".v.code").map(e=>({text:e.textContent,font:getComputedStyle(e).fontFamily})),
status:R(q(".status")[0]),retry:R(q(".retry")[0]),
faces:[...document.fonts].map(f=>f.family+" "+f.weight+" "+f.status),
copyState:(()=>{const b=q(".sheet button")[0];if(!b)return null;const cs=getComputedStyle(b);const svg=[...b.querySelectorAll("svg")].find(s=>getComputedStyle(s).display!=="none");
 const p=svg&&svg.querySelector("path"),tip=b.nextElementSibling;return {bg:cs.backgroundColor,shadow:cs.boxShadow,radius:cs.borderTopLeftRadius,w:b.offsetWidth,h:b.offsetHeight,
 icon:svg?svg.getAttribute("class"):"",fill:p?getComputedStyle(p).fill:"",tip:tip?Number(getComputedStyle(tip).opacity):-1,tipText:tip?tip.textContent:""}})(),
essai:(document.getElementById("essai")||{}).textContent||"",
h1Weight:getComputedStyle(q("h1")[0]).fontWeight,title:document.title,focused:document.activeElement?document.activeElement.id:""})})()'''


def check(name: str, m: dict) -> list[str]:
    """The page's measurements against the board: returns the differences (empty = as drawn)."""
    c, bad = CASES[name], []

    def near(what, got, want, tol=1.0):
        if want is None:
            return
        if got is None or abs(got - want) > tol:
            bad.append(f'{what}: {got if got is None else round(got, 2)} (board {want})')

    main = m.get('main') or {}
    near('block width', main.get('w'), 420, 0.5)
    near('block height', main.get('h'), c['height'], 1.0)
    icon = m.get('iconSvg')
    if not icon:
        bad.append('no icon')
    else:
        near('icon width', icon['w'], 48, 0.01); near('icon height', icon['h'], 48, 0.01)
        if m.get('iconId') != c['icon']:
            bad.append(f'icon {m.get("iconId")!r} (board {c["icon"]!r})')
        near('gap icon → title+text', m['text']['y'] - (m['icon']['y'] + m['icon']['h']), 16, 0.01)
    near('gap title → text', m['p']['y'] - (m['h1']['y'] + m['h1']['h']), 8, 0.01)
    near('title line height', m['h1']['h'], 28, 0.01)
    if c.get('title_w'):
        near('title text width', m['h1Text']['w'], c['title_w'], 1.5)
    if c.get('body_h'):
        near('text height (lines)', m['p']['h'], c['body_h'], 0.01)
    near('gap title+text → buttons', m['actions']['y'] - (m['text']['y'] + m['text']['h']), 16, 0.01)
    buttons = m.get('buttons') or []
    if [b['label'] for b in buttons] != [b[0] for b in c['buttons']]:
        bad.append(f'buttons {[b["label"] for b in buttons]} (board {[b[0] for b in c["buttons"]]})')
    for got, (label, width, cls) in zip(buttons, c['buttons']):
        near(f'« {label} » height', got['h'], 32, 0.01)
        if width:
            near(f'« {label} » width', got['w'], width, 1.5)
        if cls not in got['cls']:
            bad.append(f'« {label} » is {got["cls"]!r} (board {cls})')
    if len(buttons) == 2:
        near('gap between buttons', buttons[1]['x'] - (buttons[0]['x'] + buttons[0]['w']), 8, 0.01)
    below = m['actions']['y'] + m['actions']['h']
    if c.get('sheet'):
        sheet = m.get('sheet')
        if not sheet:
            bad.append('no technical sheet')
        else:
            w, h, rows = c['sheet']
            near('sheet width', sheet['w'], w, 0.01); near('sheet height', sheet['h'], h, 0.01)
            near('gap buttons → sheet', sheet['y'] - below, 16, 0.01)
            if len(m['rows']) != rows:
                bad.append(f'sheet rows {len(m["rows"])} (board {rows})')
            for i, r in enumerate(m['rows']):
                near(f'sheet row {i + 1} height', r['h'], 42, 0.01)
            if not m.get('copy'):
                bad.append('no copy button on the Code row')
            else:
                near('copy button width', m['copy']['w'], 16, 0.01); near('copy button height', m['copy']['h'], 18, 0.01)
            if c.get('code') and [x['text'] for x in m.get('code') or []] != [c['code']]:
                bad.append(f'code {[x["text"] for x in m.get("code") or []]} (expected {c["code"]})')
            for code in m.get('code') or []:
                if not code['font'].startswith('"JetBrains Mono"') or code['text'].startswith('WebView2'):
                    bad.append(f'code {code}')
    elif m.get('sheet'):
        bad.append('a sheet the board does not show')
    for key in ('status', 'retry'):
        if c.get(key):
            box = m.get(key)
            if not box:
                bad.append(f'no {key} row')
            else:
                near(f'{key} width', box['w'], c[key][0], 1.5); near(f'{key} height', box['h'], c[key][1], 0.01)
                near(f'gap buttons → {key}', box['y'] - below, 16, 0.01)
    if c.get('open_bg'):
        sec = next((b for b in buttons if 'secondary' in b['cls']), None)
        if not sec or _rgba(sec['bg'])[:3] != c['open_bg']:
            bad.append(f'« Détails techniques » open: {sec and sec["bg"]} (board #3E3E3E)')
    if c.get('essai') and not re.fullmatch(c['essai'], m.get('essai') or ''):
        bad.append(f'Essai row {m.get("essai")!r} (board « 3e tentative · nouvel essai dans 16 s »)')
    if m.get('h1Weight') != '600':
        bad.append(f'title weight {m.get("h1Weight")} (board 600 SemiBold)')
    faces = m.get('faces') or []
    for want in ('Inter Display 400', 'Inter Display 500', 'Inter Display 600') + (('JetBrains Mono 400',) if c.get('sheet') else ()):
        if not any(f.startswith(want) and f.endswith('loaded') for f in faces):
            bad.append(f'font not loaded: {want} ({faces})')
    return bad


def _rgba(text: str) -> tuple:
    """« rgb(62, 62, 62) » / « rgba(255, 255, 255, 0.08) » -> (r, g, b, a)."""
    n = [float(x) for x in re.findall(r'[0-9.]+', text or '')]
    return (tuple(int(v) for v in n[:3]) + ((n[3] if len(n) > 3 else 1.0),)) if len(n) >= 3 else (0, 0, 0, 0.0)


def check_copy(name: str, m: dict) -> list[str]:
    """The copy button against its state on the board (and the owner's A1 focus rule)."""
    want, s, bad = COPY_STATES[name], m.get('copyState'), []
    if not s:
        return ['no copy button']
    if (s['w'], s['h']) != (20, 20) or s['radius'] != '4px':
        bad.append(f'zone {s["w"]}x{s["h"]} radius {s["radius"]} (board 20 x 20, radius 4)')
    bg = _rgba(s['bg'])
    bg_alpha = 0.0 if bg[3] == 0 else (bg[3] if bg[:3] == (255, 255, 255) else -1)
    if abs(bg_alpha - want['bg']) > 0.01:
        bad.append(f'background {s["bg"]} (board #FFFFFF at {want["bg"]:.3f})')
    fill = _rgba(s['fill'])
    if fill[:3] != (255, 255, 255) or abs(fill[3] - want['fill']) > 0.01:
        bad.append(f'icon {s["fill"]} (board #FFFFFF at {want["fill"]:.3f})')
    if s['icon'] != want['icon']:
        bad.append(f'glyph {s["icon"]!r} (board {"check" if want["icon"] == "ok" else "copy"})')
    if 'tip' in want and abs(s['tip'] - want['tip']) > 0.05:
        bad.append(f'tooltip opacity {s["tip"]} (expected {want["tip"]})')
    if want.get('tip') and s['tipText'] != 'Copier le code':
        bad.append(f'tooltip text {s["tipText"]!r}')
    if want.get('ring') and not ('inset' in s['shadow'] and '1px' in s['shadow']):
        bad.append(f'focus ring {s["shadow"]!r} (A1: 1 px inset #FFFFFF80)')
    return bad


def board_image(name: str):
    """The board's block on the page colour (the export is transparent)."""
    from PIL import Image
    png = Image.open(BOARDS / f'{name}.png').convert('RGBA')
    ground = Image.new('RGBA', png.size, CANVAS + (255,))
    return Image.alpha_composite(ground, png).convert('RGB')


def compare(name: str, shot, out: Path) -> dict:
    """Pixel comparison of our block (1x) with the board's: mean channel difference, share of clearly different
    pixels (> 48 on a channel), and a side-by-side + difference image for the review."""
    from PIL import Image, ImageChops
    if not (BOARDS / f'{name}.png').exists():
        return {'note': 'no board export yet'}
    board = board_image(name)
    w, h = min(board.width, shot.width), min(board.height, shot.height)
    a, b = board.crop((0, 0, w, h)), shot.convert('RGB').crop((0, 0, w, h))
    diff = ImageChops.difference(a, b)
    data = list(diff.getdata())
    mean = sum(sum(p) for p in data) / (3 * len(data))
    strong = sum(1 for p in data if max(p) > 48) / len(data)
    sheet = Image.new('RGB', (w * 3 + 16, max(board.height, shot.height)), CANVAS)
    sheet.paste(board, (0, 0)); sheet.paste(shot.convert('RGB'), (w + 8, 0)); sheet.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0))
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / f'{name}-board-ours-diff.png')
    return {'size_board': list(board.size), 'size_ours': list(shot.size), 'mean_diff': round(mean, 2), 'strong_share': round(strong * 100, 2)}


def write_report(path: Path, title: str, rows: list[dict]) -> None:
    lines = [f'# {title}', '', '| Cas | Planche | Écarts de mise en page | Diff. moyenne | Pixels nettement différents |', '|---|---|---|---|---|']
    for r in rows:
        px = r.get('pixels') or {}
        lines.append(f"| {r['name']} | {CASES[r['name']]['board'] if r['name'] in CASES else 'Bouton copier · états 1T2J-0'} | {'; '.join(r['layout']) or 'aucun'} | {px.get('mean_diff', '—')} | {str(px.get('strong_share', '—')) + ' %' if px else '—'} |")
    lines += ['', 'Images côte à côte (planche · Browser · différence ×4) : `*-board-ours-diff.png` dans ce dossier.']
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (path.with_suffix('.json')).write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding='utf-8')
