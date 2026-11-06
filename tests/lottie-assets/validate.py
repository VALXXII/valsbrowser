"""Validate assets/lottie/*.json with a real native Lottie renderer (rlottie).

Run (isolated venv with rlottie-python + pillow from PyPI):
  <venv>/python -I tests/lottie-assets/validate.py <repo-root> <out-dir>
Writes contact sheets + per-frame PNGs to <out-dir>; exits non-zero on failure.
"""
import json, sys, pathlib
from rlottie_python import LottieAnimation
from PIL import Image, ImageChops

ROOT = pathlib.Path(sys.argv[1]).resolve()
OUT = pathlib.Path(sys.argv[2]).resolve(); OUT.mkdir(parents=True, exist_ok=True)
LOTTIE, ICONS = ROOT / 'assets' / 'lottie', ROOT / 'assets' / 'icons'
failures = []


def require(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


ALLOWED_SHAPES = {'gr', 'sh', 'st', 'tm', 'tr'}
BANNED_KEYS = {'masksProperties', 'tt', 'td', 'ef', 'refId', 'x'}  # masks, mattes, effects (blur), images, expressions


def walk(node, shapes, banned):
    if isinstance(node, dict):
        if 'ty' in node and isinstance(node['ty'], str):
            shapes.add(node['ty'])
        for k, v in node.items():
            if k in BANNED_KEYS and k != 'x':
                banned.add(k)
            if k == 'x' and isinstance(v, str):
                banned.add('expression')
            walk(v, shapes, banned)
    elif isinstance(node, list):
        for v in node:
            walk(v, shapes, banned)


def alpha(img):
    return img.convert('RGBA').split()[3]


def render(anim, frame, size):
    return anim.render_pillow_frame(frame_num=frame, width=size, height=size)


def ink_diff(a, b):
    """Mean |alpha difference| over pixels inked in either image (0..1)."""
    pa, pb = list(a.get_flattened_data()), list(b.get_flattened_data())
    pairs = [(x, y) for x, y in zip(pa, pb) if x > 8 or y > 8]
    return sum(abs(x - y) for x, y in pairs) / (255 * max(1, len(pairs)))


def phosphor(name, size):
    """Shipped Phosphor Bold PNG has opacity 0.5 baked in: undo it for comparison."""
    img = Image.open(ICONS / f'{name}-{ {16: 96, 32: 192, 64: 384}[size] }.png')
    return alpha(img).point(lambda v: min(255, v * 2))


def contact(anim, frames, size, path):
    sheet = Image.new('RGBA', (len(frames) * (size + 8) + 8, size + 16), (23, 23, 23, 255))
    for k, f in enumerate(frames):
        fr = render(anim, f, size)
        # composite at the host's 0.5 ink opacity on the canvas token
        ink = Image.new('RGBA', fr.size, (255, 255, 255, 0)); ink.putalpha(alpha(fr).point(lambda v: v // 2))
        sheet.alpha_composite(ink, (8 + k * (size + 8), 8))
    sheet.save(path)


for name in ('copy-link', 'refresh'):
    path = LOTTIE / f'{name}.json'
    data = json.loads(path.read_text(encoding='utf-8'))
    shapes, banned = set(), set()
    walk(data['layers'], shapes, banned)
    require(shapes <= ALLOWED_SHAPES | {4}, f'{name}: only sh/st/tm/tr/gr shape items ({sorted(map(str, shapes))})')
    require(not banned and not data['assets'], f'{name}: no masks/mattes/effects/images/expressions')
    require(data['fr'] <= 30 and data['op'] <= 24, f'{name}: modest fps/duration ({data["fr"]} fps, {data["op"]} frames)')
    anim = LottieAnimation.from_file(str(path))
    total = anim.lottie_animation_get_totalframe()
    require(total == data['op'] - data['ip'] + 1, f'{name}: rlottie parsed {total} frames (ip..op inclusive)')
    for f in range(total):
        for size in (16, 24, 32):
            render(anim, f, size).save(OUT / f'{name}-{size}px-f{f:02d}.png') if size == 32 else render(anim, f, size)
    contact(anim, list(range(total)), 64, OUT / f'{name}-contact-64.png')
    contact(anim, list(range(total)), 16, OUT / f'{name}-contact-16.png')
    if name == 'copy-link':
        for size in (16, 32, 64):
            d0 = ink_diff(alpha(render(anim, 0, size)), phosphor('copy', size))
            d10 = ink_diff(alpha(render(anim, 10, size)), phosphor('check', size))
            require(d0 < .08, f'copy-link f0 == Phosphor Bold link at {size}px (ink diff {d0:.3f})')
            require(d10 < .08, f'copy-link f10 == Phosphor Bold check at {size}px (ink diff {d10:.3f})')
        same = ImageChops.difference(render(anim, 0, 64), render(anim, total - 1, 64)).getbbox() is None
        require(same, 'copy-link last frame returns exactly to the link rest frame')
        a = json.loads(path.read_text())['layers'][0]['shapes'][1]['it'][0]['ks']['k']
        require(all(len(k['s'][0]['v']) == len(a[0]['s'][0]['v']) for k in a), 'copy-link morph keyframes share topology')
    else:
        for size in (16, 32, 64):
            d0 = ink_diff(alpha(render(anim, 0, size)), phosphor('reload', size))
            require(d0 < .08, f'refresh f0 == Phosphor Bold arrow-counter-clockwise at {size}px (ink diff {d0:.3f})')
        same = ImageChops.difference(render(anim, 0, 64), render(anim, total - 1, 64)).getbbox() is None
        require(same, 'refresh last frame == rest frame (clean end, seamless loop)')
        mid = ink_diff(alpha(render(anim, 7, 64)), alpha(render(anim, 0, 64)))
        require(mid > .2, f'refresh mid-sweep path visibly differs from rest (diff {mid:.3f})')

print('FAILURES: %d' % len(failures))
sys.exit(1 if failures else 0)
