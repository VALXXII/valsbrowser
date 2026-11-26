"""Pixel comparison of the native update card with Val's VALIDATED preview, at 100 % and 150 %.

    python tests/update-toast/compare.py [snapshotDir]

Native renders: <snapshotDir>/native-{A,B,C}-{100,150}.png (toast_tests.cpp: the production Painter, the real
GrainGradient at Paper's frame, French texts). References: tests/update-toast/reference/preview-*.png
(preview_reference.py: the preview page rendered headless). Both are the card's border box, same pixel size.

Measured per case: the mean absolute difference (0-255, RGB) and the share of pixels that differ by more than 48 on
some channel. Text and icon edges rasterise differently in DirectWrite/Direct2D and in Chromium, so the limits leave
room for anti-aliasing but not for a moved, missing or recoloured element. Writes build/update-toast/compare/
{sheet-*.png, report.json}: preview | native | difference × 4.
"""
import json, os, sys

try:
    from PIL import Image, ImageChops, ImageFilter
except ImportError:  # Pillow is a dev dependency of the repo's other pixel checks
    print("compare: SKIPPED (Pillow not installed)")
    sys.exit(0)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "reference")
SNAP = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "update-toast", "snapshots")
OUT = os.path.join(ROOT, "build", "update-toast", "compare")
LIMITS = {"mean": 6.0, "share": 0.03}   # per case: mean |Δ| ≤ 6/255, ≤ 3 % of pixels off by more than 48


def main():
    os.makedirs(OUT, exist_ok=True)
    report, failed = [], []
    for scale in (100, 150):
        for state in "ABC":
            ref = Image.open(os.path.join(REF, f"preview-{state}-{scale}.png")).convert("RGB")
            nat_rgba = Image.open(os.path.join(SNAP, f"native-{state}-{scale}.png")).convert("RGBA")
            # The native card is drawn on a transparent canvas (its rounded corners are click-through): put it on the
            # app's canvas, as the preview page shows it.
            ground = Image.new("RGBA", nat_rgba.size, (23, 23, 23, 255))
            nat = Image.alpha_composite(ground, nat_rgba).convert("RGB")
            if nat.size != ref.size:
                failed.append(f"{state}-{scale}: size {nat.size} != preview {ref.size}")
                continue
            diff = ImageChops.difference(nat, ref)
            def stats(d):
                px = list(d.get_flattened_data()) if hasattr(d, "get_flattened_data") else list(d.getdata())
                n = len(px)
                return sum(sum(p) for p in px) / (3 * n), sum(1 for p in px if max(p) > 48) / n
            raw_mean, raw_share = stats(diff)
            # The GrainGradient's grain is per-pixel noise: Direct3D and the preview's WebGL (SwiftShader) differ by a few
            # ulps in its hash, so single grains differ. The gate compares the two after a 3 × 3 box blur (the grain
            # averages out; a glyph, an icon or a button off by one pixel does not), the raw figures are reported.
            mean, share = stats(ImageChops.difference(nat.filter(ImageFilter.BoxBlur(1)), ref.filter(ImageFilter.BoxBlur(1))))
            ok = mean <= LIMITS["mean"] and share <= LIMITS["share"]
            report.append({"case": f"{state}-{scale}", "mean": round(mean, 3), "share_over_48": round(share, 4),
                           "raw_mean": round(raw_mean, 3), "raw_share_over_48": round(raw_share, 4), "ok": ok})
            print(f"{'ok  ' if ok else 'FAIL'} {state} at {scale} %: mean |d| {mean:.2f}/255, {share * 100:.2f} % off by > 48 "
                  f"(raw pixels: {raw_mean:.2f}, {raw_share * 100:.2f} %)")
            if not ok:
                failed.append(f"{state}-{scale}")
            w, h = ref.size
            sheet = Image.new("RGB", (w * 3 + 16, h), (40, 40, 40))
            sheet.paste(ref, (0, 0)); sheet.paste(nat, (w + 8, 0))
            sheet.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 16, 0))
            sheet.save(os.path.join(OUT, f"sheet-{state}-{scale}.png"))
    with open(os.path.join(OUT, "report.json"), "w", encoding="utf-8") as f:
        json.dump({"limits": LIMITS, "cases": report, "failed": failed}, f, indent=1)
    if failed:
        print("compare: FAILED", ", ".join(failed))
        return 1
    print("compare: native card matches the validated preview (100 % and 150 %)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
