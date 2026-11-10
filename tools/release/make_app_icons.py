"""Regenerate the two Windows app icons from their original exports.

Sources (exported unmodified, transparent corners; see design/app-icons/README.md):
  design/app-icons/live-1080.png                      "App Icon - Release / Live", single 1080 px export
  design/app-icons/preview-256.png, preview-1024.png  Paper artboard "App Icon - Beta / Build - Worktrees", 1x + 4x

Outputs (committed, so builds and CI never need Pillow):
  src/icons/browser-live.ico, src/icons/browser-preview.ico

Each ICO holds 16, 20, 24, 32, 40, 48, 64, 128 and 256 px, 256 as a PNG entry and every
smaller size as a 32-bit BGRA DIB with its AND mask, the format every Windows icon API accepts.
Resampling is an area-average (box) downscale (premultiplied by Pillow, no ringing on the
transparent corners), never a retouch of the artwork:
  live     every size, 256 included, is downscaled from the 1080 px master;
  preview  256 is the 1x export verbatim, smaller sizes are downscaled from the 4x export.
Requires Pillow (`python -m pip install pillow`) only when regenerating; the output is
deterministic (same sources + Pillow -> same bytes).
"""
from __future__ import annotations
import io, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
ICONS = ROOT / 'design/app-icons'


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


def load(path: Path, size: int):
    from PIL import Image
    img = Image.open(path).convert('RGBA')
    if img.size != (size, size):
        raise SystemExit(f'Unexpected size for {path.name}: {img.size}, want {size}x{size}')
    corners = [img.getpixel(p)[3] for p in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1))]
    if any(corners):
        raise SystemExit(f'{path.name}: corners must be transparent, alpha {corners}')
    return img


def live_images():
    """Single high-resolution master: every size is a box downscale of it."""
    from PIL import Image
    master = load(ICONS / 'live-1080.png', 1080)
    return {size: master.resize((size, size), Image.BOX) for size in SIZES}


def preview_images():
    """Paper 1x + 4x exports: 256 verbatim, smaller sizes downscaled from 4x."""
    from PIL import Image
    big, exact = load(ICONS / 'preview-1024.png', 1024), load(ICONS / 'preview-256.png', 256)
    return {size: exact if size == 256 else big.resize((size, size), Image.BOX) for size in SIZES}


def build_ico(images: dict) -> bytes:
    entries = []
    for size in SIZES:
        if size == 256:
            buf = io.BytesIO()
            images[size].save(buf, 'PNG', optimize=True)
            entries.append((size, buf.getvalue()))
        else:
            entries.append((size, dib_entry(images[size])))
    out = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        dim = 0 if size == 256 else size
        out += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    return bytes(out)


VARIANTS = {'live': live_images, 'preview': preview_images}


def main() -> int:
    for name, images in VARIANTS.items():
        ico = build_ico(images())
        target = ROOT / f'src/icons/browser-{name}.ico'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(ico)
        print(f'{target.relative_to(ROOT)}: {len(ico)} bytes, sizes {SIZES}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
