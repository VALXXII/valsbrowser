"""#121: generates the two reduced fonts of the error pages, from the bundled fonts (OFL, no Reserved Font Name):

    assets/fonts/InterDisplay-SemiBold-Latin.ttf   the titles (catalog text only: French and English)
    assets/fonts/JetBrainsMono-Codes.ttf           the sheet's code (ERR_CONNECTION_REFUSED, CERTIFICATE_*, « Code inconnu »)

    pip install --target <dir> fonttools ; PYTHONPATH=<dir> python tests/error-pages/make_page_fonts.py

Why: the error pages are one NavigateToString document with their fonts as data: URIs, and the engine refuses a
document over ~1.5 MB (it becomes a base64 data: URL capped at 2 MB; measured with tests/error-pages/run.py:
1 108 832 bytes shown, 1 663 648 refused with E_INVALIDARG). Inter Display Regular and Medium stay whole (they also
draw the host and the issuer); the SemiBold of the titles and the mono of the codes only need these characters.
Same ranges and approach as tests/annotations-ui/make_label_font.py; the boards' character variants (cv01-cv04,
cv13) are kept.
"""
import os
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LATIN = "U+0020-007E,U+00A0-00FF,U+0152-0153,U+2013-2014,U+2018-201E,U+2026,U+2039-203A,U+202F,U+2212"
FEATURES = ["kern", "liga", "calt", "ccmp", "locl", "mark", "mkmk", "cv01", "cv02", "cv03", "cv04", "cv13"]
for source, target, unicodes in (("InterDisplay-SemiBold.ttf", "InterDisplay-SemiBold-Latin.ttf", LATIN),
                                 ("JetBrainsMono-Regular.ttf", "JetBrainsMono-Codes.ttf", "U+0020-007E")):
    opts = subset.Options(); opts.layout_features = FEATURES; opts.hinting = True; opts.name_IDs = ["*"]; opts.notdef_outline = True
    font = TTFont(os.path.join(ROOT, "assets", "fonts", source))
    s = subset.Subsetter(opts); s.populate(unicodes=subset.parse_unicodes(unicodes)); s.subset(font)
    path = os.path.join(ROOT, "assets", "fonts", target)
    font.save(path)
    print(target, os.path.getsize(path))
