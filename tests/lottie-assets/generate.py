"""Author assets/lottie/*.json (+ reference SVGs) from Phosphor Bold centerlines.

Geometry: @phosphor-icons/core 2.1.1 Bold ships *filled outlines*. Bold is a
24-unit round-cap/round-join stroke on a 256 grid, so the centerlines below
were recovered from those outlines (cap centres = midpoints of the 12-radius
cap arcs, arc radius = mean of outer/inner radius). validate.py checks the
recovered strokes against the shipped Phosphor PNGs pixel-by-pixel.

Run:  python -I tests/lottie-assets/generate.py <repo-root>
"""
import json, math, sys, pathlib

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
OUT = ROOT / 'assets' / 'lottie'
FPS = 30
STROKE = 24
# Ease tokens (cubic-bezier) shared with design/lottie-spec.md.
EASE_STANDARD = ((0.2, 0, 0, 1))        # better-ui default for state swaps
EASE_INOUT = ((0.45, 0, 0.2, 1))        # symmetric sweep that lands softly


def r2(v):
    return round(v, 2)


# ---- tiny path builder: absolute anchors + Lottie relative tangents --------
class Path:
    def __init__(self, x, y):
        self.v = [[x, y]]; self.i = [[0, 0]]; self.o = [[0, 0]]

    def line(self, x, y):
        self.v.append([x, y]); self.i.append([0, 0]); self.o.append([0, 0]); return self

    def arc(self, cx, cy, r, a0, a1, max_step=90):
        """Arc in y-down degrees (increasing = clockwise on screen)."""
        n = max(1, math.ceil(abs(a1 - a0) / max_step - 1e-9))
        step = (a1 - a0) / n
        k = 4 / 3 * math.tan(math.radians(step) / 4) * r
        for s in range(n):
            t0 = math.radians(a0 + step * s); t1 = math.radians(a0 + step * (s + 1))
            # out tangent of the current last vertex
            self.o[-1] = [-math.sin(t0) * k, math.cos(t0) * k]
            self.v.append([cx + r * math.cos(t1), cy + r * math.sin(t1)])
            self.i.append([math.sin(t1) * k, -math.cos(t1) * k])
            self.o.append([0, 0])
        return self

    def data(self):
        return {'c': False, 'v': [[r2(a), r2(b)] for a, b in self.v],
                'i': [[r2(a), r2(b)] for a, b in self.i], 'o': [[r2(a), r2(b)] for a, b in self.o]}

    def svg(self):
        d = 'M%s,%s' % tuple(r2(c) for c in self.v[0])
        for k in range(1, len(self.v)):
            p, q = self.v[k - 1], self.v[k]
            c1 = [p[0] + self.o[k - 1][0], p[1] + self.o[k - 1][1]]
            c2 = [q[0] + self.i[k][0], q[1] + self.i[k][1]]
            if self.o[k - 1] == [0, 0] and self.i[k] == [0, 0]:
                d += ' L%s,%s' % (r2(q[0]), r2(q[1]))
            else:
                d += ' C%s,%s %s,%s %s,%s' % tuple(r2(c) for c in (*c1, *c2, *q))
        return d


def rotate180(p):
    q = Path(256 - p.v[0][0], 256 - p.v[0][1])
    q.v = [[256 - x, 256 - y] for x, y in p.v]
    q.i = [[-x, -y] for x, y in p.i]; q.o = [[-x, -y] for x, y in p.o]
    return q


# ---- Phosphor Bold "link" (copy-link) -------------------------------------
R = 46.63                       # (58.62 outer + 34.65 inner) / 2
CA = (70.6, 169.4)              # lower-left loop centre
CB = (105.37, 134.65)           # crossing arc centre
END = 346.8                     # angle of the inner cap centre (150.77,124.00)


def link_a():
    # cap (108.68,197.25) -> 45deg on CA, semicircle to 225deg, straight to CB 225deg,
    # arc to the inner cap. Split so A has 7 anchors (= check morph target).
    p = Path(108.68, 197.25)
    p.line(CA[0] + R * math.cos(math.radians(45)), CA[1] + R * math.sin(math.radians(45)))
    p.arc(*CA, R, 45, 225)                       # 2 cubic segments -> anchors 135, 225
    p.line(CB[0] + R * math.cos(math.radians(225)), CB[1] + R * math.sin(math.radians(225)))
    p.arc(*CB, R, 225, END, max_step=61)         # 2 cubic segments
    return p


# ---- Phosphor Bold "check", resampled to the SAME 7-anchor topology --------
def check_like_a():
    a, b, c = (40, 144), (96, 200), (224, 72)
    lerp = lambda p, q, t: (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
    pts = [a, lerp(a, b, 1 / 3), lerp(a, b, 2 / 3), b, lerp(b, c, 1 / 3), lerp(b, c, 2 / 3), c]
    p = Path(*pts[0])
    for q in pts[1:]:
        p.line(*q)
    return p


# ---- Phosphor Bold "arrow-counter-clockwise" (reload) ----------------------
def reload_arc():
    # tail cap (67.6,192) at 133.3deg on r=88 about the centre, CCW on screen
    # through 0deg to 224.9deg, then straight to the arrow corner (24,104).
    p = Path(128 + 88 * math.cos(math.radians(133.33)), 128 + 88 * math.sin(math.radians(133.33)))
    p.arc(128, 128, 88, 133.33, -135.07)         # decreasing angle = CCW on screen
    p.line(24, 104)
    return p


def reload_head():
    p = Path(72, 104); p.line(24, 104); p.line(24, 56); return p


# ---- Lottie helpers ---------------------------------------------------------
def kf(t, s, ease):
    x1, y1, x2, y2 = ease
    return {'t': t, 's': s, 'o': {'x': [x1], 'y': [y1]}, 'i': {'x': [x2], 'y': [y2]}}


def last(t, s):
    return {'t': t, 's': s}


def static(v):
    return {'a': 0, 'k': v}


def anim(*keys):
    return {'a': 1, 'k': list(keys)}


def stroke(name='Phosphor Bold ink'):
    # Opaque white: the host composites the rendered bitmap at the icon ink
    # opacity (0.5 * ButtonStyle.iconOpacity), exactly like the Phosphor PNGs,
    # so overlapping strokes never double-darken.
    return {'ty': 'st', 'nm': name, 'c': static([1, 1, 1, 1]), 'o': static(100),
            'w': static(STROKE), 'lc': 2, 'lj': 2, 'bm': 0}


def transform(rot=None, op=None):
    return {'ty': 'tr', 'nm': 'Transform', 'a': static([128, 128]), 'p': static([128, 128]),
            's': static([100, 100]), 'r': rot or static(0), 'o': op or static(100),
            'sk': static(0), 'sa': static(0)}


def shape(nm, ks):
    return {'ty': 'sh', 'nm': nm, 'ks': ks}


def trim(nm, s, e):
    return {'ty': 'tm', 'nm': nm, 's': s, 'e': e, 'o': static(0), 'm': 1}


def group(nm, items, tr):
    return {'ty': 'gr', 'nm': nm, 'it': items + [tr]}


def layer(nm, shapes, op):
    return {'ddd': 0, 'ind': 1, 'ty': 4, 'nm': nm, 'sr': 1,
            'ks': {'o': static(100), 'r': static(0), 'p': static([128, 128, 0]),
                   'a': static([128, 128, 0]), 's': static([100, 100, 100])},
            'ao': 0, 'shapes': shapes, 'ip': 0, 'op': op, 'st': 0, 'bm': 0}


def comp(nm, op, layers, markers):
    return {'v': '5.7.4', 'fr': FPS, 'ip': 0, 'op': op, 'w': 256, 'h': 256, 'nm': nm,
            'ddd': 0, 'assets': [], 'layers': layers, 'markers': markers,
            'meta': {'g': 'Browser tests/lottie-assets/generate.py',
                     'd': 'Phosphor Bold 2.1.1 (MIT) centerlines, stroke 24 round caps/joins, 256 grid'}}


def marker(name, start, dur):
    return {'tm': start, 'cm': name, 'dr': dur}


# ---- copy-link: link -> check (0..10), check -> link (10..20) ---------------
def copy_link():
    A, C, B = link_a().data(), check_like_a().data(), rotate180(link_a()).data()
    assert len(A['v']) == len(C['v']) == 7, 'morph topology must match'
    T_IN, T_HOLD, T_OUT = 0, 9, 10          # frames: 9f = 300 ms morph each way
    morph = anim(kf(T_IN, [A], EASE_STANDARD), kf(T_HOLD, [C], EASE_STANDARD),
                 kf(T_OUT, [C], EASE_STANDARD), last(T_OUT + 9, [A]))
    # The second chain half retracts into its own cap (trim end 100 -> 0) while
    # piece A becomes the check; hidden at zero length so no round-cap dot remains.
    b_end = anim(kf(0, [100], EASE_STANDARD), kf(6, [0], EASE_STANDARD),
                 kf(12, [0], EASE_STANDARD), last(19, [100]))
    # Fade with the retract so the zero-length remnant never shows as a cap dot.
    b_op = anim(kf(2, [100], EASE_STANDARD), kf(5, [0], EASE_STANDARD),
                kf(14, [0], EASE_STANDARD), last(17, [100]))
    shapes = [
        group('link B (retracts)', [shape('link B', static(B)), trim('retract', static(0), b_end), stroke()],
              transform(op=b_op)),
        group('link A -> check (morph)', [shape('link A / check', morph), stroke()], transform()),
    ]
    return comp('copy-link', 20, [layer('copy-link', shapes, 20)],
                [marker('toCheck', 0, 10), marker('toLink', 10, 10)])


# ---- refresh: exactly one rigid counter-clockwise turn -------------------
# User revision: no tail trim/regrow and no local clockwise motion. Arc and head
# paths are static (constant topology and length); only the group rotation
# animates, monotonically 0 -> -360 over frames 0..14 (467 ms), and frame 14
# is already rest so a host that samples count-1 = 14 ends without a snap.
# op stays 15 (500 ms) so the playback duration is unchanged.
REFRESH_OP = 15
REFRESH_TURN_END = REFRESH_OP - 1
REFRESH_TURN_HOLD = REFRESH_TURN_END - 0.01   # turn completes, held until normalisation


def refresh():
    arc, head = reload_arc().data(), reload_head().data()
    # 0 -> -360 (CCW, follows the arrow) eased over 0..TURN_HOLD, then a hold
    # keyframe swaps -360 for the identical orientation 0 at frame 14. Renderers
    # evaluate rotate(-360) with float residue (ThorVG: up to 4/255 AA noise);
    # rotate(0) is the exact identity, so frame 14 is pixel-identical to rest.
    rot = anim(kf(0, [0], EASE_INOUT),
               {'t': REFRESH_TURN_HOLD, 's': [-360], 'h': 1},
               last(REFRESH_TURN_END, [0]))
    shapes = [group('arrow (one turn)', [
        group('head', [shape('head', static(head)), stroke()], transform()),
        group('arc', [shape('arc', static(arc)), stroke()], transform()),
    ], transform(rot=rot))]
    return comp('refresh', REFRESH_OP, [layer('refresh', shapes, REFRESH_OP)],
                [marker('turn', 0, REFRESH_OP)])


def svg(paths, title):
    body = ''.join('<path d="%s"/>' % p.svg() for p in paths)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">'
            '<title>%s</title><g fill="none" stroke="#FFFFFF" stroke-width="24" '
            'stroke-linecap="round" stroke-linejoin="round">%s</g></svg>\n' % (title, body))


def main():
    (OUT / 'source').mkdir(parents=True, exist_ok=True)
    for name, data in (('copy-link', copy_link()), ('refresh', refresh())):
        (OUT / (name + '.json')).write_text(json.dumps(data, separators=(',', ':')) + '\n', encoding='utf-8')
    (OUT / 'source' / 'link.svg').write_text(svg([link_a(), rotate180(link_a())], 'Phosphor Bold link, centerline source'), encoding='utf-8')
    (OUT / 'source' / 'check-7pt.svg').write_text(svg([check_like_a()], 'Phosphor Bold check, 7-anchor morph target'), encoding='utf-8')
    (OUT / 'source' / 'refresh.svg').write_text(svg([reload_arc(), reload_head()], 'Phosphor Bold arrow-counter-clockwise, centerline source'), encoding='utf-8')
    print('wrote', *sorted(p.name for p in OUT.rglob('*') if p.is_file()))


if __name__ == '__main__':
    main()
