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



def collect(node, out):
    if isinstance(node, dict):
        if node.get('ty') == 'sh':
            out.append(node)
        for v in node.values():
            collect(v, out)
    elif isinstance(node, list):
        for v in node:
            collect(v, out)


def find_rotation(layers):
    found = []
    def visit(n):
        if isinstance(n, dict):
            if n.get('ty') == 'tr' and n['r'].get('a') == 1:
                found.append(n['r'])
            for v in n.values():
                visit(v)
        elif isinstance(n, list):
            for v in n:
                visit(v)
    visit(layers)
    assert len(found) == 1, 'exactly one animated rotation expected'
    return found[0]


def bezier_y(x, x1, y1, x2, y2):
    lo, hi = 0.0, 1.0
    for _ in range(60):  # solve x(t) = x by bisection (x(t) monotonic for x1,x2 in [0,1])
        t = (lo + hi) / 2
        xt = 3 * (1 - t) ** 2 * t * x1 + 3 * (1 - t) * t * t * x2 + t ** 3
        lo, hi = (t, hi) if xt < x else (lo, t)
    t = (lo + hi) / 2
    return 3 * (1 - t) ** 2 * t * y1 + 3 * (1 - t) * t * t * y2 + t ** 3


def rotation_at(prop, frame):
    keys = prop['k']
    if frame <= keys[0]['t']:
        return keys[0]['s'][0]
    for a, b in zip(keys, keys[1:]):
        if frame < b['t']:
            if a.get('h') == 1:
                return a['s'][0]
            u = (frame - a['t']) / (b['t'] - a['t'])
            e = bezier_y(u, a['o']['x'][0], a['o']['y'][0], a['i']['x'][0], a['i']['y'][0])
            return a['s'][0] + (b['s'][0] - a['s'][0]) * e
    return keys[-1]['s'][0]


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
        # Constant geometry: no trim, every path static -> constant topology and length.
        require('tm' not in shapes, 'refresh: no trim paths (no tail shorten/regrow)')
        paths = []
        collect(data['layers'], paths)
        require(paths and all(p['ks']['a'] == 0 for p in paths), f'refresh: all {len(paths)} paths static (constant topology/length)')
        # Exactly one monotonic counter-clockwise turn.
        rot = find_rotation(data['layers'])
        keys = rot['k']
        hold = keys[-2]
        require(len(keys) == 3 and hold.get('h') == 1 and hold['s'][0] == -360 and keys[-1]['s'][0] == 0,
                'refresh: turn ends on a held -360 that normalises to the identical orientation 0')
        # Unwrapped orientation: the hold swap -360 -> 0 is the same angle (mod 360), not motion.
        times = [f / 100 for f in range(0, 100 * data['op'] + 1)]
        unwrapped = [rotation_at(rot, t) - (360 if t >= keys[-1]['t'] else 0) for t in times]
        require(all(b <= a + 1e-9 for a, b in zip(unwrapped, unwrapped[1:])), 'refresh: orientation non-increasing at 1/100-frame resolution (no clockwise motion)')
        require(abs(unwrapped[0]) < 1e-9 and abs(unwrapped[-1] + 360) < 1e-9, f'refresh: exactly one turn, 0 -> -360 ({unwrapped[0]} -> {unwrapped[-1]:.3f})')
        require(rotation_at(rot, data['op'] - 1) == 0, 'refresh: frame op-1 (last host-sampled, 14) is exact rest orientation')
        sampled = [rotation_at(rot, f) for f in range(data['op'] - 1)]
        print('     sampled frame angles:', ', '.join(f'{a:.1f}' for a in sampled), '| f14: 0 (== -360)')
        require(data['op'] / data['fr'] == .5, 'refresh: playback duration stays 500 ms')
        for f in (data['op'] - 1, data['op']):
            same = ImageChops.difference(render(anim, 0, 64), render(anim, f, 64)).getbbox() is None
            require(same, f'refresh frame {f} pixel-identical to rest frame 0 (no end snap)')
        mid = ink_diff(alpha(render(anim, 7, 64)), alpha(render(anim, 0, 64)))
        require(mid > .2, f'refresh mid-turn visibly rotated (diff {mid:.3f})')

print('FAILURES: %d' % len(failures))
sys.exit(1 if failures else 0)
