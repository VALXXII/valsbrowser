"""Compare ThorVG-rendered frames (thorvg_dump.exe) with shipped Phosphor Bold PNGs."""
import sys, pathlib
from PIL import Image
ROOT, OUT = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
fails = []
def req(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what); fails.append(what) if not ok else None
def frame(name, side, i):
    return Image.frombytes('L', (side, side), (OUT / f'{name}-{side}-f{i:02d}.a8').read_bytes())
def phosphor(icon, side):
    img = Image.open(ROOT / 'assets/icons' / f'{icon}-{ {16: 96, 32: 192, 64: 384}[side] }.png').convert('RGBA')
    return img.split()[3].point(lambda v: min(255, v * 2))
def diff(a, b):
    pairs = [(x, y) for x, y in zip(a.get_flattened_data(), b.get_flattened_data()) if x > 8 or y > 8]
    return sum(abs(x - y) for x, y in pairs) / (255 * max(1, len(pairs)))
counts = {n: len(list(OUT.glob(f'{n}-64-f*.a8'))) for n in ('copy-link', 'refresh')}
for side in (16, 32, 64):
    d = diff(frame('copy-link', side, 0), phosphor('copy', side)); req(d < .08, f'ThorVG copy-link rest == Phosphor link @{side}px ({d:.3f})')
    d = diff(frame('copy-link', side, 10), phosphor('check', side)); req(d < .08, f'ThorVG copy-link f10 == Phosphor check @{side}px ({d:.3f})')
    d = diff(frame('refresh', side, 0), phosphor('reload', side)); req(d < .08, f'ThorVG refresh rest == Phosphor reload @{side}px ({d:.3f})')
    last = counts['refresh'] - 1
    d = diff(frame('refresh', side, last), phosphor('reload', side)); req(d < .12, f'ThorVG refresh last sampled frame ~ rest @{side}px ({d:.3f})')
d = diff(frame('copy-link', 64, counts['copy-link'] - 1), frame('copy-link', 64, 0)); req(d < .01, f'ThorVG copy-link returns to link ({d:.3f})')
for name, n in counts.items():
    sheet = Image.new('RGB', (n * 72 + 8, 80), (23, 23, 23))
    for i in range(n):
        a = frame(name, 64, i).point(lambda v: v // 2)
        sheet.paste(Image.new('RGB', (64, 64), (255, 255, 255)), (8 + i * 72, 8), a)
    sheet.save(OUT / f'thorvg-{name}-contact-64.png')
print('FAILURES: %d' % len(fails)); sys.exit(1 if fails else 0)
