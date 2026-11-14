"""Download success: the progress arrow MORPHS into the Phosphor Bold check (256x256, 60 fps).

Arrow-only asset. The ring is not in this file: the native renderer keeps composing the
existing ring (download-ring-track/fill held at their completed frame, FILL_END = 54), so
the circle stays exactly where it is (R 88, stroke 24). This file replaces the arrow
channel, and download-success-cutout.json the cut channel, of DownloadIcon::Compose.

    frame 0   = the existing arrow at rest (download-arrow.json, loop boundary, dy = 30):
                stem (128,62)-(128,170), head (88,130)-(128,170)-(168,130)
    frame 24  = the official Phosphor Bold check-circle check centreline:
                (88,136)-(112,160)-(168,104), stroke 24, round caps and joins

True vertex morph, same vertex counts, no swap or cross-fade:
    head  3 vertices: left tip -> check short end, corner -> check corner, right tip -> check long end
    stem  2 vertices: bottom -> check corner, top -> check long end (folds onto the long arm)
Both paths share the corner at every frame, so the joint never opens; the stem stays steeper
than the head's right arm until they coincide, so the paths never cross.

Usage: python -I design/lottie/download-success/download-success.py [out-dir]
Writes assets/lottie/download-success.json (arrow channel) and download-success-cutout.json (cut channel).
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
WHITE = [1, 1, 1, 1]
W, KNOCKOUT = 24, 11            # Phosphor Bold stroke; same clear band as download-progress.py
FRAMES = 24                     # 400 ms at 60 fps (op = 25 so frame 24 exists)
ARROW_DROP = 30                 # download-progress.py rest pose
SETTLE = {"o": {"x": [0.45], "y": [0]}, "i": {"x": [0.15], "y": [1]}}

# ---- Official end geometry --------------------------------------------------------
OFFICIAL_D = ("M176.49,95.51a12,12,0,0,1,0,17l-56,56a12,12,0,0,1-17,0l-24-24a12,12,0,1,1,17-17L112,143l47.51-47.52"
              "A12,12,0,0,1,176.49,95.51ZM236,128A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84"
              "A84.09,84.09,0,0,0,212,128Z")
CHECK = [[88, 136], [112, 160], [168, 104]]   # centreline: the r12 cap/join arc centres of OFFICIAL_D

def verify_official():
    """The check centreline is derived from the official outline: each r=12 arc of the check is
    centred on one centreline vertex. Checked against deps/phosphor-core when present."""
    src = os.path.join(REPO, "deps", "phosphor-core", "package", "assets", "bold", "check-circle-bold.svg")
    if os.path.exists(src):
        d = re.search(r'\sd="([^"]+)"', open(src, encoding="utf-8").read()).group(1)
        assert d == OFFICIAL_D, "deps/phosphor-core check-circle-bold.svg differs from the embedded official path"
    # Arc centres: (176.49,95.51)->(176.49,112.51) sweep 1; (120.49,168.51)->(103.51,168.51); (79.51,144.49)->(96.51,127.49).
    for (x1, y1, x2, y2), c in zip([(176.49, 95.51, 176.49, 112.51), (120.49, 168.51, 103.51, 168.51), (79.51, 144.49, 96.51, 127.49)],
                                   [CHECK[2], CHECK[1], CHECK[0]]):
        for x, y in ((x1, y1), (x2, y2)):
            assert abs(math.hypot(x - c[0], y - c[1]) - 12) < 0.05, (x, y, c)
    return src if os.path.exists(src) else "embedded"

# ---- Start geometry (must equal download-arrow.json at a loop boundary) -------------
STEM0 = [[128, 32 + ARROW_DROP], [128, 140 + ARROW_DROP]]
HEAD0 = [[88, 100 + ARROW_DROP], [128, 140 + ARROW_DROP], [168, 100 + ARROW_DROP]]
HEAD1 = [CHECK[0], CHECK[1], CHECK[2]]

def verify_start():
    """Frame 0 equals the existing arrow's rest pose (download-arrow.json key at frame 14)."""
    doc = json.load(open(os.path.join(REPO, "assets", "lottie", "download-arrow.json"), encoding="utf-8"))
    shapes = doc["layers"][0]["shapes"][0]["it"]
    for sh, want in zip([s for s in shapes if s["ty"] == "sh"], [STEM0, HEAD0]):
        key = next(k for k in sh["ks"]["k"] if k["t"] == 14)
        assert key["s"][0]["v"] == want, (key["s"][0]["v"], want)

def path(v):
    z = [[0, 0]] * len(v)
    return {"c": False, "v": v, "i": z, "o": z}

# ---- Baked morph ---------------------------------------------------------------------
# One key per frame (linear in between) so each vertex can follow its own schedule. The head's
# three vertices move on the global SETTLE curve. The stem keeps its bottom on the corner and
# ROTATES onto the long arm while it SHORTENS inside it: it never meets the arm tip-to-tip, so the
# two never form a doubled tip or a blob; once aligned it lies inside the long arm's stroke, and
# the final frame renders exactly the official check (the stem is a sub-segment of the long arm).
def bezier_ease(t, x1=0.45, y1=0.0, x2=0.15, y2=1.0):
    """Lottie/CSS cubic-bezier(x1,y1,x2,y2) evaluated at time t (same as SETTLE)."""
    lo, hi = 0.0, 1.0
    for _ in range(60):
        s = (lo + hi) / 2
        x = 3 * (1 - s) ** 2 * s * x1 + 3 * (1 - s) * s * s * x2 + s ** 3
        lo, hi = (s, hi) if x < t else (lo, s)
    s = (lo + hi) / 2
    return 3 * (1 - s) ** 2 * s * y1 + 3 * (1 - s) * s * s * y2 + s ** 3

def lerp(p, q, t): return [p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t]
def smooth(a, b, x):
    x = min(1.0, max(0.0, (x - a) / (b - a))); return x * x * (3 - 2 * x)
STEM_ALIGN = 0.55       # ...and is aligned with the long arm by 55 % of the morph (eased time)
STEM_KEEP = 0.2         # it ends as the first fifth of the long arm (inside the corner join + arm)
STEM_SHRINK = 0.30      # short by 30 %; only then it rotates (never sweeps a long stem across the arm)

def pose(t):
    """(stem, head) vertex lists at normalised time t in [0, 1]."""
    u = bezier_ease(t)
    head = [lerp(a, b, u) for a, b in zip(HEAD0, HEAD1)]
    corner, right = head[1], head[2]
    arm = math.atan2(right[1] - corner[1], right[0] - corner[0])
    w = smooth(STEM_SHRINK, STEM_ALIGN, u)   # rotates only once it is short: never sweeps across the arm
    angle = -math.pi / 2 + (arm + math.pi / 2) * w
    length = 108 + (STEM_KEEP * math.hypot(right[0] - corner[0], right[1] - corner[1]) - 108) * smooth(0.0, STEM_SHRINK, u)
    top = [corner[0] + length * math.cos(angle), corner[1] + length * math.sin(angle)]
    return [top, corner], head

def r3(v): return [[round(x, 3), round(y, 3)] for x, y in v]
def baked(index):
    keys = []
    for f in range(FRAMES + 1):
        v = r3(pose(f / FRAMES)[index])
        # The head is written right tip -> corner -> left tip so its long arm runs TOWARD the corner,
        # like the stem (top -> corner): overlapping strokes of one path then add their winding
        # instead of cancelling (opposite directions punch holes where they overlap).
        if index == 1: v = v[::-1]
        k = {"t": f, "s": [path(v)]}
        if f < FRAMES: k.update({"o": {"x": [0], "y": [0]}, "i": {"x": [1], "y": [1]}})
        keys.append(k)
    return {"a": 1, "k": keys}

def stroke(width):
    return {"ty": "st", "c": {"a": 0, "k": WHITE}, "o": {"a": 0, "k": 100}, "w": {"a": 0, "k": width}, "lc": 2, "lj": 2, "ml": 4, "nm": "stroke"}

def transform(opacity):
    return {"ty": "tr", "p": {"a": 0, "k": [0, 0]}, "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0}, "o": opacity}

def handover_frame():
    """First frame at which the stem lies entirely inside the long arm (aligned and shorter)."""
    for f in range(FRAMES + 1):
        if smooth(STEM_SHRINK, STEM_ALIGN, bezier_ease(f / FRAMES)) >= 1.0: return f
    return FRAMES

def held(before, after, at):
    """Opacity `before` on frames < at, `after` from `at` on (hold keys, no blend)."""
    return {"a": 1, "k": [{"t": 0, "s": [before], "h": 1}, {"t": at, "s": [after]}]}

def group(name, width):
    # ThorVG adds the coverage of overlapping strokes of one shape, so the stem hidden inside the long
    # arm would darken a few anti-aliased edge pixels. As soon as the stem is fully hidden (while the
    # head is still moving, so the change is masked by motion) the stem + head group hands over to a
    # head-only group following the same head keys: the last frame is exactly the official check.
    at = handover_frame()
    morphing = {"ty": "gr", "nm": name + " morph", "it": [{"ty": "sh", "nm": "stem", "ks": baked(0)}, {"ty": "sh", "nm": "head", "ks": baked(1)}, stroke(width), transform(held(100, 0, at))]}
    final = {"ty": "gr", "nm": name + " head", "it": [{"ty": "sh", "nm": "head", "ks": baked(1)}, stroke(width), transform(held(0, 100, at))]}
    return {"ty": "gr", "nm": name, "it": [final, morphing, transform({"a": 0, "k": 100})]}

def doc(name, width):
    return {"v": "5.7.4", "fr": 60, "ip": 0, "op": FRAMES + 1, "w": 256, "h": 256, "nm": name, "ddd": 0, "assets": [],
            "markers": [{"tm": 0, "cm": "morph", "dr": FRAMES}],
            "layers": [{"ddd": 0, "ind": 1, "ty": 4, "nm": name, "sr": 1, "ip": 0, "op": FRAMES + 1, "st": 0, "bm": 0,
                        "ks": {"o": {"a": 0, "k": 100}, "r": {"a": 0, "k": 0}, "p": {"a": 0, "k": [128, 128, 0]},
                               "a": {"a": 0, "k": [128, 128, 0]}, "s": {"a": 0, "k": [100, 100, 100]}},
                        "shapes": [group(name, width)]}]}

def verify_morph(samples=240):
    """Endpoints exact; the stem never crosses outside the V, and its tip never sits near the long
    arm's tip (no doubled tip / blob): it is either clearly apart or inside the long arm's stroke."""
    stem0, head0 = pose(0); stem1, head1 = pose(1)
    assert r3(stem0) == STEM0 and r3(head0) == HEAD0, "frame 0 must be the existing arrow at rest"
    assert r3(head1) == HEAD1, "the head must end on the official check"
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    def seg_dist(p, a, b):
        ax, ay, bx, by = b[0] - a[0], b[1] - a[1], p[0] - a[0], p[1] - a[1]
        t = max(0.0, min(1.0, (ax * bx + ay * by) / (ax * ax + ay * ay)))
        return math.hypot(p[0] - a[0] - ax * t, p[1] - a[1] - ay * t)
    worst = 0.0
    for n in range(samples + 1):
        (top, bottom), (left, corner, right) = pose(n / samples)
        assert bottom == corner
        assert cross(corner, left, top) * cross(corner, left, right) > 0, "stem left of the V"
        assert cross(corner, right, top) * cross(corner, right, left) >= -1e-6, "stem right of the V"
        tips = math.hypot(top[0] - right[0], top[1] - right[1])
        assert tips >= W, ("stem tip within a stroke width of the arm tip", n, tips)   # never a doubled tip
        # No nub: the stem's round tip never sits half-merged into the long arm (its centre between
        # 1 and 12 units, half a stroke, from the arm's centreline) except inside the corner's round
        # join, where it is invisible.
        bump, length = seg_dist(top, corner, right), math.hypot(top[0] - corner[0], top[1] - corner[1])
        if 1.0 < bump < W / 2: assert length <= W / 2 + 4, ("stem tip half-merged into the long arm", n, bump, length)
        worst = max(worst, seg_dist(stem1[0], corner, right) if n == samples else 0)
    stem_end = pose(1)[0]
    assert seg_dist(stem_end[0], HEAD1[1], HEAD1[2]) < 1e-6, "final stem lies on the long arm"
    return worst

if __name__ == "__main__":
    source = verify_official(); verify_start(); verify_morph()
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "assets", "lottie")
    for target, d in ((os.path.join(out, "download-success.json"), doc("download-success", W)), (os.path.join(out, "download-success-cutout.json"), doc("download-success-cutout", W + 2 * KNOCKOUT))):
        json.dump(d, open(target, "w"), separators=(",", ":"))
        print("wrote", target, d["op"], "frames")
    # Static reference: the official check alone (no stem), used by render-review.cpp to prove that
    # the last frame renders pixel-identical to it.
    ref = doc("reference-check", W); ref["layers"][0]["shapes"][0]["it"] = [{"ty": "sh", "nm": "check", "ks": {"a": 0, "k": path(CHECK)}}, stroke(W), ref["layers"][0]["shapes"][0]["it"][-1]]
    json.dump(ref, open(os.path.join(HERE, "review", "reference-check.json"), "w"), separators=(",", ":"))
    print("official source:", source, "| stem hidden, hand-over at frame", handover_frame())
