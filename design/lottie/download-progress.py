"""Download icon: tray -> progress ring, as three Lottie layers (256x256, 60 fps).

The fill follows the download (frame = progress) while the arrow keeps moving
in time, so they cannot share one timeline. The native renderer composes:

    track (download-ring-track.json)    grey ink, drawn first
    fill  (download-ring-fill.json)     #3E67FF over it
    cut   (download-arrow-cutout.json)  erased from both (destination-out)
    arrow (download-arrow.json)         grey ink, drawn on top

which cuts a real (transparent) band around the arrow on any background.

download-ring-*.json 0 -> 14   start: the tray's arms curl up into a ring
                     14 -> 54  progress: #3E67FF fills it clockwise, frame = 14 + 40 * fraction
                     54 -> 81  done: the ring unrolls back into the tray, blue fading out
download-arrow*.json 0 -> 14   start: the arrow drops into the ring
                     14 -> 92  loop: bobs down and back while waiting and downloading
                     92 -> 119 done: rises back to the tray pose (start it at a loop boundary)
Mirrored in src/download-icon.h (frame layout), which checks the frame counts.

Geometry follows Phosphor Bold download-simple (stroke 24, round caps/joins), so
frame 0 of both is the current toolbar icon. Every file stays <= 120 frames
(the native player's limit) and uses only paths, strokes, trim paths, opacity.
"""
import json, math, os, sys

GREY = [0x94 / 255, 0x94 / 255, 0x94 / 255, 1]
BLUE = [0x3E / 255, 0x67 / 255, 0xFF / 255, 1]
WHITE = [1, 1, 1, 1]
W, CX, CY = 24, 128, 128
R, GAP = 88, 0             # ring centre-line radius; half-gap at the top (degrees; 0 = closed)
ARROW_DROP = 30            # the stem's cap sits just inside the ring's top (Paper)
KNOCKOUT = 11              # clear band cut around the arrow where it meets the ring (tuned)
MORPH, STEPS, OUT = 14, 40, 27   # tuned in the preview: 233 ms in, 40 fill steps, 450 ms back
LOOP, BOB = 78, 12         # 1.3 s; 12 units down (tuned). Arrow file = 14 + 78 + 27 + 1 = 120 frames, the limit
FILL_END = MORPH + STEPS
DONE = FILL_END + OUT

def path(v, i=None, o=None):
    z = [[0, 0]] * len(v)
    return {"c": False, "v": v, "i": i or z, "o": o or z}

# Tray, drawn from the right arm's top, clockwise like the ring that follows.
tray = path([[216, 144], [216, 208], [128, 208], [40, 208], [40, 144]])

def ring():
    """Ring, 4 cubic segments, same 5 vertices as the tray, clockwise from +GAP."""
    span = (360 - 2 * GAP) / 4
    k = 4 / 3 * math.tan(math.radians(span) / 4) * R
    v, ins, outs = [], [], []
    for n in range(5):
        a = math.radians(GAP + n * span)
        tx, ty = math.cos(a), math.sin(a)          # clockwise tangent
        v.append([round(CX + R * math.sin(a), 3), round(CY - R * math.cos(a), 3)])
        ins.append([0, 0] if n == 0 else [round(-k * tx, 3), round(-k * ty, 3)])
        outs.append([0, 0] if n == 4 else [round(k * tx, 3), round(k * ty, 3)])
    return path(v, ins, outs)

def arrow(dy):
    return (path([[128, 32 + dy], [128, 140 + dy]]),
            path([[88, 100 + dy], [128, 140 + dy], [168, 100 + dy]]))

# Morphs leave from rest and settle; the bob eases in and out (no kink at its
# ends); the fill is linear because its frame *is* the progress.
SETTLE = {"o": {"x": [0.45], "y": [0]}, "i": {"x": [0.15], "y": [1]}}
SINE = {"o": {"x": [0.45], "y": [0]}, "i": {"x": [0.55], "y": [1]}}
LINEAR = {"o": {"x": [0], "y": [0]}, "i": {"x": [1], "y": [1]}}

def anim(keys):
    """keys: [(frame, value, easing-to-next or None)]; None holds until the next key."""
    out = []
    for n, (t, s, e) in enumerate(keys):
        k = {"t": t, "s": s if isinstance(s, list) else [s]}
        if e: k.update(e)
        elif n < len(keys) - 1: k["h"] = 1
        out.append(k)
    return {"a": 1, "k": out}

def stroke(color, opacity=None, width=W):
    return {"ty": "st", "c": {"a": 0, "k": color}, "o": opacity or {"a": 0, "k": 100},
            "w": {"a": 0, "k": width}, "lc": 2, "lj": 2, "ml": 4, "nm": "stroke"}

def shape(p):
    return {"ty": "sh", "ks": p, "nm": "path"}

def group(name, items):
    return {"ty": "gr", "nm": name, "it": items + [{"ty": "tr", "p": {"a": 0, "k": [0, 0]}, "a": {"a": 0, "k": [0, 0]},
            "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}}]}

def doc(name, frames, shapes, markers):
    return {"v": "5.7.4", "fr": 60, "ip": 0, "op": frames, "w": 256, "h": 256, "nm": name, "ddd": 0, "assets": [],
            "markers": [{"tm": t, "cm": c, "dr": d} for c, t, d in markers],
            "layers": [{"ddd": 0, "ind": 1, "ty": 4, "nm": name, "sr": 1, "ip": 0, "op": frames, "st": 0, "bm": 0,
                        "ks": {"o": {"a": 0, "k": 100}, "r": {"a": 0, "k": 0}, "p": {"a": 0, "k": [128, 128, 0]},
                               "a": {"a": 0, "k": [128, 128, 0]}, "s": {"a": 0, "k": [100, 100, 100]}},
                        "shapes": shapes}]}

# ---- Ring --------------------------------------------------------------------
ring_path = ring()
outline = anim([(0, tray, SETTLE), (MORPH, ring_path, None), (FILL_END, ring_path, SETTLE), (DONE, tray, None)])
ring_marks = [("start", 0, MORPH), ("progress", MORPH, STEPS), ("done", FILL_END, OUT)]
# Track and fill are separate files: the native renderer inks the grey parts
# (track, arrow) exactly like the other toolbar icons (white at 50%), so the
# animated icon matches the static one on any button surface; only the fill is
# an opaque colour.
track_doc = doc("download-ring-track", DONE + 1, [group("track", [shape(outline), stroke(GREY)])], ring_marks)
fill_doc = doc("download-ring-fill", DONE + 1, [
    group("progress", [shape(outline),
        {"ty": "tm", "nm": "fill", "s": {"a": 0, "k": 0}, "o": {"a": 0, "k": 0}, "m": 1, "e": anim([(MORPH, 0, LINEAR), (FILL_END, 100, None)])},
        # Blue only while it means progress; fades back to grey when done.
        stroke(BLUE, anim([(0, 0, None), (MORPH - 1, 0, None), (MORPH, 100, None), (FILL_END, 100, SETTLE), (DONE, 0, None)]))])],
    ring_marks)

# ---- Arrow and its cut-out ------------------------------------------------------
A_LOOP_END = MORPH + LOOP
A_DONE = A_LOOP_END + OUT
rest, low = arrow(ARROW_DROP), arrow(ARROW_DROP + BOB)
tray_arrow = arrow(0)
def arrow_track(i):
    return anim([(0, tray_arrow[i], SETTLE), (MORPH, rest[i], SINE), (MORPH + LOOP // 2, low[i], SINE),
                 (A_LOOP_END, rest[i], SETTLE), (A_DONE, tray_arrow[i], None)])
arrow_marks = [("start", 0, MORPH), ("loop", MORPH, LOOP), ("done", A_LOOP_END, OUT)]
arrow_doc = doc("download-arrow", A_DONE + 1, [group("arrow", [shape(arrow_track(0)), shape(arrow_track(1)), stroke(GREY)])], arrow_marks)
cut_doc = doc("download-arrow-cutout", A_DONE + 1,
              [group("cutout", [shape(arrow_track(0)), shape(arrow_track(1)), stroke(WHITE, None, W + 2 * KNOCKOUT)])], arrow_marks)

folder = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
for name, d in (("download-ring-track", track_doc), ("download-ring-fill", fill_doc), ("download-arrow", arrow_doc), ("download-arrow-cutout", cut_doc)):
    target = os.path.join(folder, name + ".json")
    json.dump(d, open(target, "w"), separators=(",", ":"))
    print("wrote", target, d["op"], "frames")
