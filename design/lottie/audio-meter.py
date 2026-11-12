"""Toolbar audio icon: a speaker whose two level bars move while a site plays music.

One Lottie (256x256, 60 fps, 97 frames, <= 120 = the native player's limit):

    0 -> 72   loop: the two bars rise and fall out of phase, like a level meter.
              Frame 0 (= 72) is Phosphor Bold speaker-simple-high exactly.
    72 -> 96  outro: the outer bar shrinks to a dot and fades; the last frame is
              Phosphor Bold speaker-simple-low exactly (the static, subdued icon).
              Played backwards (96 -> 72) it is the intro.

Mirrored in src/audio-mixer.h (AudioIcon frame layout, checked on load).

The speaker body is the official Phosphor Bold speaker-simple-none outline (filled,
arcs converted to cubic Beziers below); the bars are 24-unit strokes with round
caps, i.e. Phosphor Bold's own geometry. Everything is white: the native renderer
inks it like every other toolbar icon (white at 50 %) and dims it for the subdued
state. Only paths, strokes and opacity are used.
"""
import argparse, json, math, os, re, sys

# ---- Character of the meter (the same rule runs live in design/audio-preview's debug mode) ----
# amplitude: each key's swing from the rest pose (1 = the first, livelier version)
# beats:     keys per loop for each bar (fewer = calmer)
# offset:    how late the outer bar's keys come, as a fraction of half a beat (0 = together)
# ease:      Bezier handle of every loop key (0.45 = the first version; higher = softer turns)
# Defaults: subtle (user feedback 2026-11-12: the first version was too strong and fast).
# The loop's speed is a playback setting, not part of the file: Audio::Icon::LoopMs.
DEFAULTS = {"amplitude": 0.3, "beats": 5, "offset": 0.7, "ease": 0.4}  # chosen by the user in the preview, 2026-11-12

WHITE = [1, 1, 1, 1]
W = 24
LOOP, OUT = 72, 24
FRAMES = LOOP + OUT + 1  # 97
BODY = ("M165.27,21.22a12,12,0,0,0-12.64,1.31L83.88,76H40A20,20,0,0,0,20,96v64a20,20,0,0,0,20,20H83.88"
        "l68.75,53.47A12,12,0,0,0,172,224V32A12,12,0,0,0,165.27,21.22ZM148,199.46,95.37,158.53A12,12,0,0,0,88,156"
        "H44V100H88a12,12,0,0,0,7.37-2.53L148,56.54Z")
BAR1_X, BAR2_X = 200, 236
BAR1_REST, BAR2_REST = 48, 80  # stroke lengths (caps excluded): 104..152 and 88..168

# ---- SVG path (M L H V A Z, absolute/relative) -> Lottie closed bezier contours ----
def arc_to_cubics(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG endpoint arc -> list of cubic segments [(c1, c2, end)] (W3C implementation notes)."""
    if rx == 0 or ry == 0: return [((x1, y1), (x2, y2), (x2, y2))]
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1: rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = cp * cxp - sp * cyp + (x1 + x2) / 2, sp * cxp + cp * cyp + (y1 + y2) / 2
    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy); return a
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0: dt -= 2 * math.pi
    if sweep and dt < 0: dt += 2 * math.pi
    n = max(1, math.ceil(abs(dt) / (math.pi / 2) - 1e-9))
    step = dt / n; k = 4 / 3 * math.tan(step / 4); out = []
    def pt(t): return (cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp, cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp)
    def der(t): return (-rx * math.sin(t) * cp - ry * math.cos(t) * sp, -rx * math.sin(t) * sp + ry * math.cos(t) * cp)
    for i in range(n):
        a, b = t1 + i * step, t1 + (i + 1) * step
        p0, p3, d0, d3 = pt(a), pt(b), der(a), der(b)
        out.append(((p0[0] + k * d0[0], p0[1] + k * d0[1]), (p3[0] - k * d3[0], p3[1] - k * d3[1]), p3))
    return out

def svg_contours(d):
    tokens = re.findall(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    i, cmd, cur, start, contours, seg = 0, None, (0.0, 0.0), (0.0, 0.0), [], None
    def num():
        nonlocal i; v = float(tokens[i]); i += 1; return v
    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]): cmd = tokens[i]; i += 1
        rel, c = cmd.islower(), cmd.upper()
        ox, oy = (cur if rel else (0, 0))
        if c == "Z":
            if seg is not None: contours.append(seg); seg = None
            cur = start; continue
        if c == "M":
            p = (num() + ox, num() + oy); cur = start = p; seg = {"v": [p], "i": [(0, 0)], "o": []}
            cmd = "l" if rel else "L"; continue
        if c in "LHV":
            if c == "L": p = (num() + ox, num() + oy)
            elif c == "H": p = (num() + ox, cur[1])
            else: p = (cur[0], num() + oy)
            seg["o"].append((0, 0)); seg["v"].append(p); seg["i"].append((0, 0)); cur = p; continue
        if c == "A":
            rx, ry, phi, large, sweep = num(), num(), num(), int(num()), int(num())
            p = (num() + ox, num() + oy)
            for c1, c2, end in arc_to_cubics(cur[0], cur[1], rx, ry, phi, large, sweep, p[0], p[1]):
                last = seg["v"][-1]
                seg["o"].append((c1[0] - last[0], c1[1] - last[1]))
                seg["v"].append(end); seg["i"].append((c2[0] - end[0], c2[1] - end[1]))
            cur = p; continue
        raise SystemExit("unsupported path command " + cmd)
    shapes = []
    for s in contours:
        v, ins, outs = s["v"], s["i"], s["o"]
        # Closed: a final vertex equal to the first merges into it (its in-tangent moves to the first).
        if len(v) > 1 and abs(v[-1][0] - v[0][0]) < 1e-3 and abs(v[-1][1] - v[0][1]) < 1e-3:
            ins[0] = ins[-1]; v, ins = v[:-1], ins[:-1]
        else:
            outs.append((0, 0))
        r = lambda pts: [[round(x, 3), round(y, 3)] for x, y in pts]
        shapes.append({"c": True, "v": r(v), "i": r(ins), "o": r(outs)})
    return shapes

# ---- Lottie helpers -----------------------------------------------------------------
SETTLE = {"o": {"x": [0.2], "y": [0]}, "i": {"x": [0], "y": [1]}}  # cubic-bezier(.2,0,0,1)

def anim(keys):
    out = []
    for n, (t, s, e) in enumerate(keys):
        k = {"t": t, "s": s if isinstance(s, list) else [s]}
        if e: k.update(e)
        elif n < len(keys) - 1: k["h"] = 1
        out.append(k)
    return {"a": 1, "k": out}

def line(x, length):
    h = length / 2
    return {"c": False, "v": [[x, 128 - h], [x, 128 + h]], "i": [[0, 0], [0, 0]], "o": [[0, 0], [0, 0]]}

def tr(opacity=None):
    return {"ty": "tr", "p": {"a": 0, "k": [0, 0]}, "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
            "r": {"a": 0, "k": 0}, "o": opacity or {"a": 0, "k": 100}}

def bar(name, x, lengths, sine, opacity=None):
    keys = [(t, [line(x, l)], sine if t < LOOP else SETTLE) for t, l in lengths[:-1]] + [(lengths[-1][0], [line(x, lengths[-1][1])], None)]
    return {"ty": "gr", "nm": name, "it": [
        {"ty": "sh", "nm": "path", "ks": anim(keys)},
        {"ty": "st", "nm": "stroke", "c": {"a": 0, "k": WHITE}, "o": {"a": 0, "k": 100}, "w": {"a": 0, "k": W}, "lc": 2, "lj": 2, "ml": 4},
        tr(opacity)]}

# Out-of-phase meter: the inner bar starts low, the outer one high, alternating; every key
# eased (no kink); frames 0 and 72 stay the rest pose. amplitude=1, beats=5, offset=0, ease=0.45
# reproduce the first version exactly.
MAG1, MAG2 = [28, 24, 16, 16, 24], [32, 32, 16, 44, 24]
def loop_keys(amplitude, beats, offset):
    pitch = LOOP / (beats + 1); lag = offset * pitch / 2
    b1, b2 = [(0, BAR1_REST)], [(0, BAR2_REST)]
    for k in range(beats):
        sign = -1 if k % 2 == 0 else 1
        t = round((k + 1) * pitch)
        b1.append((t, round(BAR1_REST + sign * MAG1[k % 5] * amplitude, 3)))
        b2.append((min(LOOP - 4, round((k + 1) * pitch + lag)), round(BAR2_REST - sign * MAG2[k % 5] * amplitude, 3)))
    return b1, b2

cli = argparse.ArgumentParser(description="Generate assets/lottie/audio-meter.json")
cli.add_argument("folder", nargs="?", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "lottie"))
for name, value in DEFAULTS.items(): cli.add_argument("--" + name, type=type(value), default=value)
args = cli.parse_args()
SINE = {"o": {"x": [args.ease], "y": [0]}, "i": {"x": [1 - args.ease], "y": [1]}}
L1, L2 = loop_keys(args.amplitude, args.beats, args.offset)
B1 = L1 + [(LOOP, BAR1_REST), (FRAMES - 1, BAR1_REST)]
B2 = L2 + [(LOOP, BAR2_REST), (LOOP + 18, 0), (FRAMES - 1, 0)]
B2_OPACITY = anim([(0, 100, None), (LOOP + 6, 100, SETTLE), (FRAMES - 1, 0, None)])

body = {"ty": "gr", "nm": "speaker", "it": [{"ty": "sh", "nm": "contour%d" % n, "ks": {"a": 0, "k": c}} for n, c in enumerate(svg_contours(BODY))]
        + [{"ty": "fl", "nm": "fill", "c": {"a": 0, "k": WHITE}, "o": {"a": 0, "k": 100}, "r": 1}, tr()]}

doc = {"v": "5.7.4", "fr": 60, "ip": 0, "op": FRAMES, "w": 256, "h": 256, "nm": "audio-meter", "ddd": 0, "assets": [],
       "markers": [{"tm": 0, "cm": "loop", "dr": LOOP}, {"tm": LOOP, "cm": "outro", "dr": OUT}],
       "layers": [{"ddd": 0, "ind": 1, "ty": 4, "nm": "audio-meter", "sr": 1, "ip": 0, "op": FRAMES, "st": 0, "bm": 0,
                   "ks": {"o": {"a": 0, "k": 100}, "r": {"a": 0, "k": 0}, "p": {"a": 0, "k": [128, 128, 0]},
                          "a": {"a": 0, "k": [128, 128, 0]}, "s": {"a": 0, "k": [100, 100, 100]}},
                   "shapes": [bar("outer", BAR2_X, B2, SINE, B2_OPACITY), bar("inner", BAR1_X, B1, SINE), body]}]}

doc["meta"] = {"g": "design/lottie/audio-meter.py", "amplitude": args.amplitude, "beats": args.beats, "offset": args.offset, "ease": args.ease}
target = os.path.join(args.folder, "audio-meter.json")
with open(target, "w") as f: json.dump(doc, f, separators=(",", ":"))
print("wrote", os.path.normpath(target), FRAMES, "frames")
