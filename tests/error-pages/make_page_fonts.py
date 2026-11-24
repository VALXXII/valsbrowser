"""#121: generates the reduced JetBrains Mono of the error pages' technical sheet, from the bundled font (OFL, no
Reserved Font Name):

    assets/fonts/JetBrainsMono-Codes.ttf    the sheet's code (ERR_CONNECTION_REFUSED, CERTIFICATE_*, « Code inconnu »)

    pip install --target <dir> fonttools ; PYTHONPATH=<dir> python tests/error-pages/make_page_fonts.py

Why: the error pages are one NavigateToString document with their fonts as data: URIs, and the engine refuses a
document over ~1.5 MB (it becomes a base64 data: URL capped at 2 MB; measured with tests/error-pages/run.py:
1 108 832 bytes shown, 1 663 648 refused with E_INVALIDARG). Inter Display goes in as the WOFF2 files the crashed-tab
page uses too (assets/fonts/InterDisplay-*.woff2); the code only needs printable ASCII. Same approach as
tests/annotations-ui/make_label_font.py.
"""
import os
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
opts = subset.Options(); opts.layout_features = ["kern", "liga", "calt", "ccmp", "locl", "mark", "mkmk"]; opts.hinting = True
opts.name_IDs = ["*"]; opts.notdef_outline = True
font = TTFont(os.path.join(ROOT, "assets", "fonts", "JetBrainsMono-Regular.ttf"))
s = subset.Subsetter(opts); s.populate(unicodes=subset.parse_unicodes("U+0020-007E")); s.subset(font)
path = os.path.join(ROOT, "assets", "fonts", "JetBrainsMono-Codes.ttf")
font.save(path)
print("JetBrainsMono-Codes.ttf", os.path.getsize(path))
