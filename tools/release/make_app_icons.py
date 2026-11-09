"""Regenerate the two Windows app icons from their original Paper exports.

Sources (Paper file "Browser Design", page Icon, exported unmodified, transparent corners):
  design/app-icons/live-256.png, live-1024.png        artboard "App Icon - Release / Live"
  design/app-icons/preview-256.png, preview-1024.png  artboard "App Icon - Beta / Build - Worktrees"

Outputs (committed, so builds and CI never need Pillow):
  src/icons/browser-live.ico, src/icons/browser-preview.ico

Each ICO holds 16, 20, 24, 32, 40, 48, 64, 128 and 256 px. 256 is the 1x export
verbatim (PNG entry); every smaller size is an area-average (box) downscale of the 4x export
(premultiplied by Pillow, no ringing on the transparent corners) stored as a
32-bit BGRA DIB with its AND mask, the format every Windows icon API accepts.
Requires Pillow (`python -m pip install pillow`) only when regenerating.
"""
from __future__ import annotations
import io, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
VARIANTS = ('live', 'preview')


def dib_entry(img) -> bytes:
    """32-bit BI_RGB DIB (height doubled for the AND mask), bottom-up, alpha kept."""
    w, h = img.size
    px = img.tobytes('raw', 'BGRA')
    stride = w * 4
    xor = b''.join(px[y * stride:(y + 1) * stride] for y in range(h - 1, -1, -1))
    mask_stride = ((w + 31) // 32) * 4
    alpha = img.getchannel('A').tobytes()
    rows = []
    for y in range(h - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(w):
            if alpha[y * w + x] == 0:  # fully transparent -> masked
                bits[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(bits))
    header = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(xor) + mask_stride * h, 0, 0, 0, 0)
    return header + xor + b''.join(rows)


def build_ico(src256: Path, src1024: Path) -> bytes:
    from PIL import Image
    big = Image.open(src1024).convert('RGBA')
    exact = Image.open(src256).convert('RGBA')
    if big.size != (1024, 1024) or exact.size != (256, 256):
        raise SystemExit(f'Unexpected source sizes: {big.size} {exact.size}')
    entries = []
    for size in SIZES:
        if size == 256:
            buf = io.BytesIO()
            exact.save(buf, 'PNG', optimize=True)
            entries.append((size, buf.getvalue()))
        else:
            entries.append((size, dib_entry(big.resize((size, size), Image.BOX))))
    out = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        dim = 0 if size == 256 else size
        out += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    return bytes(out)


def main() -> int:
    for name in VARIANTS:
        ico = build_ico(ROOT / f'design/app-icons/{name}-256.png', ROOT / f'design/app-icons/{name}-1024.png')
        target = ROOT / f'src/icons/browser-{name}.ico'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(ico)
        print(f'{target.relative_to(ROOT)}: {len(ico)} bytes, sizes {SIZES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
