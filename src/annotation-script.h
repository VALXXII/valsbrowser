#pragma once
// Annotation mode — the page script (V1, V2 capture at the click). Injected with ExecuteScript ONLY when the mode is
// activated, in the active tab only (never AddScriptToExecuteOnDocumentCreated); it removes
// itself completely when the mode ends. Contract: plan-annotations-mcp-local/design/
// annotations-v1-brief.md, plan §1.3/§4/§6.1/§8.
//
// What it does, and nothing else:
//   - hover: one hit-test per animation frame at most (requestAnimationFrame), drawn in a CLOSED
//     shadow root owned by the script: accent frame + compact label. The frame follows the
//     element with transform/opacity transitions <= 120 ms (CSS transitions retarget from the
//     presented value: interruptible); prefers-reduced-motion (or the host's setting): no
//     movement, opacity only. Zero message to the host while hovering.
//   - targets: same-origin iframes (descends into contentDocument), open shadow roots
//     (shadowRoot.elementFromPoint); a closed shadow root stops at its host; a cross-origin
//     iframe is annotated as a box. Every visible iframe (either origin) lies under a transparent
//     shield of the closed root, so its pointer events and clicks stay in the top document.
//   - keys: Up = parent, Down = child, Enter = select, Tab follows the page focus, Escape asks
//     the host to leave. Clicks (and the pointer/mouse events around them) are consumed.
//   - selection: ONE message (<= 64 KiB) with a bounded context: allow-listed attributes and
//     computed styles (lists given by the host), text <= 2000 characters, never a field value
//     (input/textarea/select contents, password fields), no cookies/storage/HTML.
// The host talks to it through window[Symbol.for('browser.annotation')] guarded by the nonce
// (handoff, show, resume, select at a point, select the text selection, exit).
// V3 capture without any visible change (retours §2): at a selection NOTHING changes on screen. The message
// carries the frame's exact geometry (edges + label box + label text); the host draws the same edges and label
// in a native layer above the page, then sends « handoff »: the page hides its own edges and label (the native
// copy covers them pixel for pixel) and answers « hidden » in the same task; the host then asks for the shot,
// which never contains the edges or the label. The 8 % fill stays in the page (it never changes on screen) and
// is removed from the image arithmetically (src/annotation-capture.h, RemoveFill). After the shot, « show »
// puts the page's edges back and answers « shown » two frames later; only then does the host drop its layer.
// « hide » (a host without the native layer) still hides everything and answers « hidden » two frames later.
// V3-B (Paper 5.3, 5.4, validated):
//   - cursor « viseur » (Paper 5.4, direction B) on the whole page while the mode is on: one constructed style sheet adopted by the
//     document (and by the open shadow roots the hit-test enters) sets it on every element with !important; the
//     iframe shields (both origins) carry it too. Sizes 32 / 48 / 64 from the host (image-set at the page's pixel
//     ratio). At the exit the sheet is un-adopted and the shields go with the closed root: no style is left.
//   - spacing inspector on the hovered element only (same closed root as the frame, same fade): padding as blue
//     135° hatches on ONE ring (padding box with its inner radius minus the content box, even-odd clip), gaps
//     (flex, grid) as pink 45° hatches on ONE union of the spaces between the children's real rectangles; both
//     layers span the viewport, so every hatch is anchored on the viewport's origin: no seam where bands meet.
//     Drawing order: hatches -> the frame's edges -> its label -> the value labels (« 20 » in px CSS, one per
//     distinct value and axis, none under 4 px), always whole: on a thin side they overflow inward, and they move
//     off the element label (and each other) when they would collide. Hidden at
//     once when the target changes and shown 120 ms later (the frame's glide): nothing during a fast movement, one
//     update per frame at most. At a selection its geometry goes to the host with the frame's (« sp »): the native
//     copy draws it too and the « handoff » hides the page's, so it is never in the shot. padding / gap values join
//     the selection (target.spacing).
// V1.2 (glued while scrolling): the frame, its inspector and the chat marks sit in wrappers that belong to the DOCUMENT
// (the host element is absolutely positioned at the document's origin), not to the viewport. A wrapper is placed at the
// viewport's origin (in document coordinates) and has a transform, so its position:fixed children are laid out in it
// with the same viewport coordinates as before; when the page scrolls, the compositor moves the wrapper with the
// page in the same frame (no « scroll-linked » lag), and the next animation frame puts both back in step. The
// wrapper clips to the viewport's client box: it never adds scrollable overflow to the page. An element inside a
// fixed or sticky box does not scroll with the document: its wrapper is position:fixed instead (followed per frame).
// While the frame follows the SAME element (scroll, resize), it jumps: the 120 ms glide is only for a new target.
// V1.4 (chat intégré): « pick » false switches the SELECTION off while the chat panel stays open: no « viseur » cursor
// (the sheet is emptied), no hover frame nor inspector, no iframe shields, the page gets its own clicks and keys
// again. The script stays: the chat's marks, source() and sources() go on. « pick » true brings it all back.
// Chat, Mode Commentaire (design/chat-integration.md): `cmarks` [{id, selector, state}] draws one frame + icon per
// conversation of the page (Paper 18IJ-0: 2 px accent border 4 px around the element, a 24 px accent tag at its top
// left with the mark; state working = a spinner in the tag, waiting = « En attente » after the mark) and answers the
// ids it found; `follow` {selector} (or {current: true}) posts { op: 'anchor', x, y, w, h, vw, vh } whenever the
// element moves (scroll, layout, resize: checked each frame while the page scrolls, every 250 ms otherwise) and
// { op: 'anchor', gone: true } once it is no longer in the page; null stops. Both live in document-anchored wrappers
// like the chat marks (glued while scrolling).
// Comment mode v4 (design/chat-comment-v2-retours.md): R26 `peek` {selector, active} shows a chip's element as hovered
// (soft scroll when out of view, back without scrolling); R27 every frame (hover / selection, conversation, bubble, chat
// mark) is one bordered box on the same rectangle snapped to device pixels, and over a conversation frame the other
// frames draw no second contour; R31 Ctrl held = the page as usual (cursor, clicks, keys), released = the selection;
// R33 the size label goes under the name, inside the frame, when the two do not fit side by side; R36 the
// « browser-restyle » event (chat-transition.h) = follow the element frame by frame while its values glide; R37 the
// selected element's inspector is placed in the frame's own frame and transitions only when a value changes.
// The page could tamper with all of this (same world): the host validates every message and
// the note itself is typed in a native control the page never sees.
#include <cstring>
#include <string>
#include "annotation-font.h"   // R45: the labels' font, bundled (generated)

namespace NativeChrome { namespace Annotations {

inline const wchar_t* PageScriptSource() {
    return LR"JS((function (P) {
'use strict';
var KEY = Symbol.for('browser.annotation');
try { var old = window[KEY]; if (old && typeof old.teardown === 'function') old.teardown(); } catch (e) {}
var NONCE = String(P.nonce), STAMP = P.navStamp, D = document;
// R45: the labels' font is the bundled Inter Display (the file the native copy of the frame draws with, Paper's font),
// subset and embedded: a FontFace from an ArrayBuffer (no request, no CSP, nothing installed), under a family name of
// its own (the page's fonts are untouched); removed at the exit. Hover, selection and bubble labels: the same face.
var FONT = { '400': '@@FONT400@@', '500': '@@FONT500@@' }, faces = [];
try {
  if (window.FontFace && D.fonts && window.atob) for (var fw in FONT) {
    var bin = atob(FONT[fw]), buf = new Uint8Array(bin.length);
    for (var bi = 0; bi < bin.length; bi++) buf[bi] = bin.charCodeAt(bi);
    var face = new FontFace('Browser Annotation Inter', buf.buffer, { weight: fw, style: 'normal' });
    D.fonts.add(face); faces.push(face);
  }
} catch (e) {}
var STYLE_PROPS = P.styles || [], ATTRS = P.attrs || [];
var reduced = !!P.reduced;
try { if (window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches) reduced = true; } catch (e) {}
// The window's profile accent « r,g,b » (ints). Replaced live by the « accent » command (profile colour edit, the
// value the host settles on, never a step of its fade); only these rules and the cursor's dot change.
function rgbList(v) { var m = /^(\d{1,3}),(\d{1,3}),(\d{1,3})$/.exec(String(v || '')); return m && +m[1] < 256 && +m[2] < 256 && +m[3] < 256 ? m[1] + ',' + m[2] + ',' + m[3] : ''; }
var A = rgbList(P.accent) || '62,103,255';
// V3-B cursor: the host's SVG at its pixel size, shown 1:1 on screen. The image-set resolution is the SCREEN's scale
// (the host's DPI, not devicePixelRatio: custom cursors are not zoomed with the page), so a 48 px image at 125 % is
// 48 device px at any page zoom. The hot spot is in the image's CSS coordinates (pixels / resolution).
var CUR = P.cursor || {}, CURSOR = 'default', CURSOR_W = 'default';
function buildCursor(svg) {
  var px = Math.max(16, Math.min(128, +CUR.size || 32)), k = px / 32;
  svg = String(svg || '');
  if (!svg) { CURSOR = CURSOR_W = 'default'; return; }
  svg = svg.replace("width='32' height='32'", "width='" + px + "' height='" + px + "'");
  var url = 'url("data:image/svg+xml,' + encodeURIComponent(svg).replace(/'/g, '%27') + '")';
  var r = Math.max(0.25, Math.min(8, +CUR.scale || 1));
  var hx = ((+CUR.hx || 16) * k / r).toFixed(2), hy = ((+CUR.hy || 16) * k / r).toFixed(2);
  var set = '(' + url + ' ' + r.toFixed(4) + 'x) ' + hx + ' ' + hy + ',default';
  CURSOR_W = '-webkit-image-set' + set; CURSOR = 'image-set' + set;
}
buildCursor(CUR.svg);
function cursorDecl(important) { var imp = important ? ' !important' : ''; return 'cursor:' + CURSOR_W + imp + ';cursor:' + CURSOR + imp; }
var sheet = null, adopted = [];
function adopt(rootNode) {
  if (!sheet || !rootNode || adopted.indexOf(rootNode) >= 0) return;
  try { rootNode.adoptedStyleSheets = rootNode.adoptedStyleSheets.concat([sheet]); adopted.push(rootNode); } catch (e) {}
}
try { sheet = new CSSStyleSheet(); sheet.replaceSync('*,*::before,*::after{' + cursorDecl(true) + '}'); } catch (e) { sheet = null; }
var MAX_TEXT = 2000, MAX_MSG = 60000;
var listeners = [], raf = 0, alive = true;
var px = -1, py = -1, haveMove = false;
var cur = null, down = [], frozen = false, shown = false, keyboard = false, seq = 0;
var off = false;   // V1.4: selection switched off (« pick » false): the page is the page again
var ctrl = false;  // R31: Ctrl held = the page is the page again, for as long as it is held
var hostCtrl = false;   // final pass 1: the host says Ctrl is held (its keyboard: the page's pointer events may not carry the flag)
var peek = null;   // R26: the element of a chip the user rests on (panel / bubble), shown as hovered
var trackUntil = 0;   // R36: a restyle glides: everything follows the element frame by frame until then
function idle() { return off || ctrl; }
function tracking() { return performance.now() < trackUntil; }

var host = D.createElement('browser-annotation-layer');
host.setAttribute('aria-hidden', 'true');
host.style.cssText = 'all:initial;position:absolute;left:0;top:0;width:0;height:0;z-index:2147483647;pointer-events:none;';
var root = host.attachShadow({ mode: 'closed' });
var T = 'transform 120ms cubic-bezier(0.2,0,0,1),width 120ms cubic-bezier(0.2,0,0,1),height 120ms cubic-bezier(0.2,0,0,1),opacity 120ms ease-out';
// The accent rules (frame fill 8 %, edges, label, padding hatches and their labels), rebuilt by the « accent » command.
function accentCss() {
  return ':host{--a:' + A + '}.cf,.ct,.bf,.bl{--a:' + A + '}' +
    '.f{background:rgba(' + A + ',0.08);border-color:rgb(' + A + ')}' +
    '.l{background:rgb(' + A + ')}' +
    '.pb{background:repeating-linear-gradient(135deg,rgba(' + A + ',.4) 0 1px,transparent 1px 5px),rgba(' + A + ',.08)}' +
    '.pl{background:rgb(' + A + ')}' +
    '.mf{background:rgba(' + A + ',0.08);border:2px solid rgb(' + A + ')}.mt,.mc{background:rgb(' + A + ')}' +   // chat marks (Paper 6.10, 6.11)
    '.s{' + cursorDecl(false) + '}';   // the iframe shields carry the cursor (its dot is the accent)
}
root.innerHTML = '<style>' +
  '.x{position:fixed;left:0;top:0;transform-origin:0 0;opacity:0;pointer-events:none;will-change:transform,opacity}' +
  '.on{transition:' + (reduced ? 'opacity 120ms ease-out' : T) + '}' +
  '.mv{transition:opacity 120ms ease-out}' +
  // R27: the hover / selection frame is ONE bordered box, exactly as the conversation frame (.cf) and the bubble's (.bf):
  // same rectangle (frameBox, snapped to device pixels), same border (edgePx), no layer of its own
  '.f{box-sizing:border-box;border-style:solid;border-width:2px;will-change:auto}.f.hb,.f.nb,.bf.nb{border-color:transparent !important}' +
  // R14 (Paper 18QK-0 / 18QQ-0): the hover labels as the selection's — Inter Display 13/18, 20 px high, padding 0 6,
  // top corners 3, name at the left, real size at the right
  // R45: ONE label style for the hover / selection labels (.l) and the bubble element's (.bl): same font face, weight,
  // size, line height, letter spacing, padding, corners, same layer (will-change: the same grayscale text rendering as
  // the native copy), positions snapped to device pixels
  '.l,.bl{font:400 13px/18px "Browser Annotation Inter","Inter Display",Inter,"Segoe UI",system-ui,sans-serif;color:#fff;height:20px;box-sizing:border-box;' +
  'padding:1px 6px;border-radius:3px 3px 0 0;white-space:pre;max-width:60vw;overflow:hidden;text-overflow:ellipsis;letter-spacing:0;' +
  'font-kerning:normal;font-feature-settings:normal;font-synthesis:none;text-rendering:auto;will-change:transform,opacity;' +
  // final pass 1, defect 3: the width is the text's own (never the containing block's: .bx is 0 wide, the hover's is the viewport)
  'width:max-content;flex:none}' +
  // R16: the selected element's inspector follows a change of its values softly (same path structure: interpolated)
  '.pb.sm,.gb.sm{transition:clip-path 160ms cubic-bezier(.23,1,.32,1)}.pl.sm,.gl.sm{transition:transform 160ms cubic-bezier(.23,1,.32,1)}' +
  '.s{position:fixed;left:0;top:0;width:0;height:0;pointer-events:auto;background:transparent}' +
  '.i,.j{position:fixed;left:0;top:0;width:0;height:0;opacity:0;pointer-events:none}.i.on,.j.on{transition:opacity 120ms ease-out}' +
  '.pb,.gb,.pl,.gl{position:fixed;left:0;top:0;box-sizing:border-box}' +
  '.gb{background:repeating-linear-gradient(45deg,rgba(224,71,158,.45) 0 1px,transparent 1px 5px),rgba(224,71,158,.12)}' +
  // D8 (decided): the padding / gap values in the labels' font (Paper 18QK-0): Inter Display, plain white, 10/14 (a band may be 4 px)
  '.pl,.gl{height:14px;padding:0 4px;border-radius:3px;font:400 10px/14px "Browser Annotation Inter","Inter Display",Inter,"Segoe UI",system-ui,sans-serif;color:#fff;white-space:nowrap;letter-spacing:0}' +
  '.gl{background:#D63E92}' +
  '.w{position:absolute;left:0;top:0;width:0;height:0;overflow:hidden;pointer-events:none;transform:translate(0px,0px)}' +
  '.mk{position:fixed;left:0;top:0;pointer-events:none;opacity:0;transition:opacity 120ms ease-out}' +
  '.cf{box-sizing:border-box;position:absolute;left:0;top:0;border:2px solid rgb(var(--a))}' +
  '.ct{position:absolute;left:-1.4px;top:-1.4px;height:24px;min-width:24px;box-sizing:border-box;display:flex;align-items:center;justify-content:center;gap:6px;padding:0 0;' +
  'border-bottom-right-radius:6px;background:rgb(var(--a));color:#fff;font:500 11px/14px "Browser Annotation Inter","Inter Display",Inter,"Segoe UI",system-ui,sans-serif;white-space:nowrap}' +
  '.ct.wait{padding:0 8px 0 6px}.ct.wait svg{opacity:.7}.ct .ms{width:11px;height:11px}' +
  '.mf{box-sizing:border-box;position:absolute;left:0;top:0}' +
  '.mt{position:absolute;left:0;top:0;height:20px;box-sizing:border-box;display:flex;align-items:center;gap:6px;padding:0 6px;border-radius:3px 3px 0 0;' +
  'font:500 12px/16px "Browser Annotation Inter","Inter Display",Inter,"Segoe UI",system-ui,sans-serif;color:#fff;white-space:nowrap;max-width:60vw;overflow:hidden;letter-spacing:0}' +
  '.mu{color:rgba(255,255,255,.75);font-size:11px;line-height:14px}' +
  '.ms{width:12px;height:12px;box-sizing:border-box;border:2px solid rgba(255,255,255,.15);border-top-color:#fff;border-radius:50%;flex:none}' +
  '.ms.spin{animation:mks 1.2s linear infinite}@keyframes mks{to{transform:rotate(360deg)}}' +
  '.mc{position:absolute;left:-2px;top:-2px;width:16px;height:16px;display:flex;align-items:center;justify-content:center;border-bottom-right-radius:4px}' +
  // D1 (Paper 18QJ-0, 18QK-0, 18QQ-0): the element of the open bubble — frame 4 px out, 2 px accent, accent 8 %; two labels
  '.bx{position:fixed;left:0;top:0;pointer-events:none}' +
  '.bf{box-sizing:border-box;position:absolute;left:0;top:0;border:2px solid rgb(var(--a));background:rgba(var(--a),.08)}' +
  '.bl{position:absolute;left:0;top:0;background:rgb(var(--a))}' +   // R45: the rest is .l's
  '</style><style class="a"></style><div class="w fw"><div class="x f"></div><div class="i"><div class="pb"></div><div class="gb"></div></div><div class="x l"></div><div class="x l"></div><div class="j"></div></div><div class="w mw"></div>';
var accentStyle = root.querySelector('style.a'); accentStyle.textContent = accentCss();
var parts = root.querySelectorAll('.x'), shields = [];
var fill = parts[0], label = parts[1], label2 = parts[2];   // R27: the frame's box (fill + border); R14: name | size
var ins = root.querySelector('.i'), insL = root.querySelector('.j'), padEl = root.querySelector('.pb'), gapEl = root.querySelector('.gb');
var insPool = { pl: [], gl: [] }, insData = null, insEl = null, insTimer = 0, insShown = false, labelBox = [];
var frameWrap = root.querySelector('.fw'), markWrap = root.querySelector('.mw'), placed = null;
var cmarks = {}, cmTimer = 0, follow = null, followTimer = 0, followLast = '';   // Mode Commentaire
(D.documentElement || D).appendChild(host);
adopt(D);

function on(t, type, fn, opt) { t.addEventListener(type, fn, opt); listeners.push([t, type, fn, opt]); }
function post(msg) {
  msg.type = 'browser-annotation'; msg.v = 1; msg.nonce = NONCE; msg.navStamp = STAMP;
  try { window.chrome.webview.postMessage(msg); } catch (e) {}
}
function tr(el, x, y, sx, sy) {
  el.style.transform = 'translate(' + x.toFixed(2) + 'px,' + y.toFixed(2) + 'px)' + (sx === undefined ? '' : ' scale(' + Math.max(sx, 0).toFixed(4) + ',' + Math.max(sy, 0).toFixed(4) + ')');
}
function setOpacity(v) { for (var i = 0; i < parts.length; i++) parts[i].style.opacity = v; }
function animate(onOff) { for (var i = 0; i < parts.length; i++) { if (onOff) parts[i].classList.add('on'); else parts[i].classList.remove('on'); } }
// The position jumps (no glide from the old place) while the opacity goes on from what is on screen.
function moveOnly(onOff) { for (var i = 0; i < parts.length; i++) { if (onOff) { parts[i].classList.remove('on'); parts[i].classList.add('mv'); } else { parts[i].classList.remove('mv'); parts[i].classList.add('on'); } } }

// ---- geometry
function rectOf(info) {
  var r;
  if (info.range) r = info.range.getBoundingClientRect(); else r = info.el.getBoundingClientRect();
  return { x: r.left + info.ox, y: r.top + info.oy, w: r.width, h: r.height };
}
function frameOffset(fr) {
  var r = fr.getBoundingClientRect(), cs = getComputedStyle(fr);
  return { x: r.left + fr.clientLeft + (parseFloat(cs.paddingLeft) || 0), y: r.top + fr.clientTop + (parseFloat(cs.paddingTop) || 0) };
}
function hitList(doc, x, y) {
  var list = doc.elementsFromPoint(x, y);
  for (var i = 0; i < list.length; i++) if (list[i] !== host) return list[i];
  return null;
}
// Deepest reachable element at (x, y) of the top viewport.
function hit(x, y) {
  var el = hitList(D, x, y);
  if (!el) return null;
  var info = { el: el, kind: 'main', ox: 0, oy: 0, shadow: [], frames: [], frame: null, range: null };
  for (var guard = 0; guard < 32; guard++) {
    el = info.el;
    var sr = el.shadowRoot;   // open shadow roots only (closed ones return null: the host is the target)
    if (sr && sr.elementFromPoint) {
      var inner = sr.elementFromPoint(x - info.ox, y - info.oy);
      if (inner && inner !== el && sr.contains(inner)) { adopt(sr); info.shadow.push(el); info.el = inner; continue; }
    }
    if (el.localName === 'iframe' || el.localName === 'frame') {
      var cd = null; try { cd = el.contentDocument; } catch (e) { cd = null; }
      if (!cd || !cd.documentElement) { info.kind = 'cross-origin'; info.frame = el; return info; }
      var o = frameOffset(el), ix = x - info.ox - o.x, iy = y - info.oy - o.y;
      var ie = cd.elementFromPoint(ix, iy);
      if (!ie) return info;
      info.frames.push(el); info.frame = el; info.kind = 'same-origin'; info.ox += o.x; info.oy += o.y; info.el = ie;
      continue;
    }
    return info;
  }
  return info;
}
// Up/down: the parent crosses shadow hosts and same-origin frames.
function parentInfo(info) {
  var el = info.el, p = el.parentElement;
  var n = { el: null, kind: info.kind, ox: info.ox, oy: info.oy, shadow: info.shadow.slice(), frames: info.frames.slice(), frame: info.frame, range: null };
  if (p) { n.el = p; return n; }
  var rn = el.getRootNode();
  if (rn && rn.host) { n.el = rn.host; n.shadow.pop(); return n; }
  if (info.frames.length) {
    var fr = n.frames.pop(), o = frameOffset(fr);
    n.el = fr; n.ox -= o.x; n.oy -= o.y; n.frame = n.frames.length ? n.frames[n.frames.length - 1] : null;
    n.kind = n.frames.length ? 'same-origin' : 'main';
    return n;
  }
  return null;
}
function childInfo(info) {
  if (info.kind === 'cross-origin' && info.el === info.frame) return null;
  var c = info.el.firstElementChild;
  if (!c) return null;
  return { el: c, kind: info.kind, ox: info.ox, oy: info.oy, shadow: info.shadow.slice(), frames: info.frames.slice(), frame: info.frame, range: null };
}

// ---- description
function classes(el) {
  var c = el.getAttribute && el.getAttribute('class'); if (!c) return [];
  return c.split(/\s+/).filter(function (s) { return s && s.length <= 48; });
}
function seg(el) { var s = el.localName, c = classes(el).slice(0, 2); return s + (c.length ? '.' + c.join('.') : ''); }
function esc(s) { try { return CSS.escape(s); } catch (e) { return String(s).replace(/[^a-zA-Z0-9_-]/g, function (ch) { return '\\' + ch; }); } }
function unique(rootNode, sel) { try { return rootNode.querySelectorAll(sel).length === 1; } catch (e) { return false; } }
function selectorFor(el) {
  var rootNode = el.getRootNode ? el.getRootNode() : D;
  if (!rootNode.querySelectorAll) rootNode = D;
  for (var pass = 0; pass < 2; pass++) {
    var chain = [];
    for (var n = el; n && n.nodeType === 1; n = n.parentElement) {
      if (n.id && unique(rootNode, '#' + esc(n.id))) { chain.unshift('#' + esc(n.id)); break; }
      var tid = n.getAttribute('data-testid');
      if (tid && tid.length <= 80) { var q = n.localName + '[data-testid="' + tid.replace(/["\\]/g, '\\$&') + '"]'; if (unique(rootNode, q)) { chain.unshift(q); break; } }
      var part = n.localName + classes(n).slice(0, 2).map(function (c) { return '.' + esc(c); }).join('');
      if (pass === 1 && n.parentElement) {
        var same = 0, idx = 0;
        for (var s = n.parentElement.firstElementChild; s; s = s.nextElementSibling) if (s.localName === n.localName) { same++; if (s === n) idx = same; }
        if (same > 1) part += ':nth-of-type(' + idx + ')';
      }
      chain.unshift(part);
      if (n.localName === 'html' || n.localName === 'body') break;
      if (chain.length >= 2 && unique(rootNode, chain.join(' > '))) break;
    }
    var sel = chain.join(' > ');
    if (sel.length > 1000) sel = sel.slice(sel.length - 1000);
    if (unique(rootNode, sel) || pass === 1) return { sel: sel, unique: unique(rootNode, sel) };
  }
  return { sel: el.localName, unique: false };
}
var ROLE = { a: 'link', button: 'button', nav: 'navigation', main: 'main', header: 'banner', footer: 'contentinfo', form: 'form',
  select: 'combobox', textarea: 'textbox', img: 'img', ul: 'list', ol: 'list', li: 'listitem', table: 'table', dialog: 'dialog',
  h1: 'heading', h2: 'heading', h3: 'heading', h4: 'heading', h5: 'heading', h6: 'heading', section: 'region', aside: 'complementary', iframe: 'iframe' };
function roleOf(el) {
  var r = el.getAttribute('role'); if (r) return r.slice(0, 40);
  if (el.localName === 'input') { var t = (el.getAttribute('type') || 'text').toLowerCase(); return t === 'checkbox' ? 'checkbox' : t === 'radio' ? 'radio' : (t === 'submit' || t === 'button' || t === 'reset') ? 'button' : t === 'range' ? 'slider' : 'textbox'; }
  if (el.localName === 'a' && !el.hasAttribute('href')) return '';
  return ROLE[el.localName] || '';
}
function isField(el) { var n = el.localName; return n === 'input' || n === 'textarea' || n === 'select' || n === 'option'; }
function clip(s, n) { s = String(s || '').replace(/\s+/g, ' ').trim(); return s.length > n ? s.slice(0, n) : s; }
function nameOf(el, kind) {
  var a = el.getAttribute('aria-label'); if (a) return clip(a, 200);
  var by = el.getAttribute('aria-labelledby');
  if (by) { try { var rn = el.getRootNode(); var t = by.split(/\s+/).map(function (id) { var e = rn.getElementById ? rn.getElementById(id) : null; return e ? e.textContent : ''; }).join(' '); if (clip(t, 200)) return clip(t, 200); } catch (e) {} }
  if (kind === 'cross-origin') { var src = ''; try { src = new URL(el.src, location.href).origin; } catch (e) {} return clip(el.getAttribute('title') || ('iframe ' + src), 200); }
  if (el.localName === 'img') return clip(el.getAttribute('alt'), 200);
  if (el.localName === 'input') {
    var ty = (el.getAttribute('type') || 'text').toLowerCase();
    if (el.labels && el.labels.length) return clip(el.labels[0].textContent, 200);
    if (ty === 'submit' || ty === 'button' || ty === 'reset') return clip(el.getAttribute('value'), 200);   // a button's caption, never a typed value
    return clip(el.getAttribute('placeholder') || el.getAttribute('title'), 200);
  }
  if (isField(el)) { if (el.labels && el.labels.length) return clip(el.labels[0].textContent, 200); return clip(el.getAttribute('title'), 200); }
  return clip(el.getAttribute('title') || (el.innerText || ''), 200);
}
function textOf(el, kind) {
  if (kind === 'cross-origin' || isField(el) || el.localName === 'canvas' || el.localName === 'video' || el.localName === 'iframe') return '';
  var t = '';
  try { t = el.innerText || ''; } catch (e) { t = ''; }
  t = t.replace(/[ \t]+/g, ' ').replace(/\n{3,}/g, '\n\n').trim();
  return t.length > MAX_TEXT ? t.slice(0, MAX_TEXT) : t;
}
function stripUrl(u) { try { var x = new URL(u, location.href); x.search = ''; x.hash = ''; x.username = ''; x.password = ''; return x.href; } catch (e) { return ''; } }
function describe(info, selText) {
  var el = info.el, r = rectOf(info), k = info.kind;
  if (k === 'cross-origin' && el !== info.frame) el = info.frame;
  var pathArr = [];
  for (var n = el; n && n.nodeType === 1 && pathArr.length < 16; ) {
    pathArr.unshift(seg(n));
    if (n.parentElement) n = n.parentElement; else { var rn = n.getRootNode(); n = rn && rn.host ? rn.host : null; }
  }
  var s = selectorFor(el), attrs = {}, styles = {}, src = {}, srcN = 0;
  for (var i = 0; i < ATTRS.length; i++) {
    var an = ATTRS[i];
    if (el.hasAttribute(an)) { var v = el.getAttribute(an) || ''; attrs[an] = (an === 'href' ? stripUrl(v) : v).slice(0, 512); }
    else if (an === 'id') attrs.id = null;
  }
  try {
    var cs = (el.ownerDocument.defaultView || window).getComputedStyle(el);
    for (var j = 0; j < STYLE_PROPS.length; j++) { var pv = cs.getPropertyValue(STYLE_PROPS[j]); if (pv) styles[STYLE_PROPS[j]] = pv.slice(0, 200); }
  } catch (e) {}
  for (var a = 0; a < el.attributes.length && srcN < 8; a++) {
    var at = el.attributes[a];
    if (/^data-/.test(at.name) && /(source|file|component|loc|line|inspector)/.test(at.name)) { src[at.name] = String(el.getAttribute(at.name) || "").slice(0, 512); srcN++; }
  }
  return {
    op: 'select', keyboard: keyboard,
    frame: { kind: k },
    target: {
      kind: selText === null ? 'element' : 'selection',
      selector: s.sel, selectorUnique: s.unique, path: pathArr, shadowPath: info.shadow.map(seg),
      tag: el.localName, role: roleOf(el), accessibleName: nameOf(el, k), attributes: attrs,
      text: selText === null ? textOf(el, k) : '', selectionText: selText,
      rect: { x: Math.round(r.x * 100) / 100, y: Math.round(r.y * 100) / 100, width: Math.round(r.w * 100) / 100, height: Math.round(r.h * 100) / 100 },
      styles: styles, sourceAttributes: src, spacing: selText === null && k !== 'cross-origin' ? spacingOf(el) : null
    },
    viewport: { width: innerWidth, height: innerHeight, scrollX: scrollX, scrollY: scrollY, devicePixelRatio: devicePixelRatio || 1 }
  };
}
function sendSelect(info, selText, ev) {
  // Latency: the host learns at once WHAT was clicked (« selecting »: the selector and the element's rectangle, nothing
  // else), before the full description (styles, text, spacing: the slow part) is computed and posted (« select »).
  var at = ev && ev.timeStamp > 0 && performance.timeOrigin ? performance.timeOrigin + ev.timeStamp : Date.now();
  var mine = ++seq;
  // Review E1: only for what the host can follow from the top document by its selector: an element of the main document
  // (no frame), outside any shadow root, not a text selection, whose selector names exactly it. Anything else opens at
  // the full « select » (one frame later), which carries the frame kind and the shadow path.
  if (selText === null && info.kind === 'main' && !info.range && !info.shadow.length && info.el.getRootNode() === D) {
    var s0 = selectorFor(info.el), r0 = rectOf(info);
    if (s0.unique && s0.sel.length <= 1000) post({ op: 'selecting', seq: mine, at: at, selector: s0.sel, rect: { x: r2(r0.x), y: r2(r0.y), width: r2(r0.w), height: r2(r0.h) },
                                              pt: { x: r2(px), y: r2(py) } });   // final pass 1: where the page saw the click (host diagnostics)
  }
  var m = describe(info, selText);
  if (JSON.stringify(m).length > MAX_MSG) { m.target.text = m.target.text.slice(0, 400); if (m.target.selectionText) m.target.selectionText = m.target.selectionText.slice(0, 400); }
  if (JSON.stringify(m).length > MAX_MSG) { m.target.styles = {}; m.target.path = m.target.path.slice(-4); m.target.selector = m.target.selector.slice(-200); }
  if (JSON.stringify(m).length > MAX_MSG) return;
  // V3: nothing changes on screen at the click; the host takes the edges and label over (« handoff »).
  m.seq = mine; settle(); m.mark = geometry(info);   // V3: what the screen shows (nothing changed since the click)
  m.at = at;   // when the user acted (event time on the system clock): the host measures click -> image ready
  post(m);
}
// marks: the edges and the label only (the fill stays: it is removed from the image by the host).
function conceal(on, marksOnly) {
  for (var i = marksOnly ? 1 : 0; i < parts.length; i++) parts[i].style.visibility = on ? 'hidden' : '';
  if (on && marksOnly) fill.classList.add('hb'); else fill.classList.remove('hb');   // R27: the border is the frame's edges
  ins.style.visibility = insL.style.visibility = on ? 'hidden' : '';
}
// At the click: the frame jumps to its final state (any glide or fade-in ends now, transitions off inline), so the
// geometry and the fill opacity sent are exactly what the next frame shows (V3: a click within a fade-in left the
// fill nearly transparent in the shot, and the host removed 8 % that was not there).
function settle() {
  var i;
  for (i = 0; i < parts.length; i++) parts[i].style.transition = 'none';
  if (shown) setOpacity(1);
  if (insTimer) { clearTimeout(insTimer); insTimer = 0; }
  if (insData && insEl === (cur && cur.el)) insFade(true, false); else insFade(false, false);
  void getComputedStyle(ins).opacity; void getComputedStyle(insL).opacity;
  for (i = 0; i < parts.length; i++) { var cs = getComputedStyle(parts[i]); void cs.opacity; void cs.transform; }
  for (i = 0; i < parts.length; i++) parts[i].style.transition = '';
}
function geometry(info) {
  var r = rectOf(info), lb = label.getBoundingClientRect();
  var fb = frameBox(r), rb = label2.getBoundingClientRect(), two = rb.width > 0;
  return { x: fb.x, y: fb.y, w: fb.w, h: fb.h, lx: lb.left, ly: lb.top, lw: lb.width, lh: lb.height,
           text: String(label.textContent || '').slice(0, 200), rx: two ? rb.left : 0, ry: two ? rb.top : 0, rw: two ? rb.width : 0, rh: two ? rb.height : 0,
           rtext: two ? String(label2.textContent || '').slice(0, 40) : '', fill: 0.08 * (parseFloat(getComputedStyle(fill).opacity) || 0), sp: spaceMark() };
}
// The inspector as drawn (only when on screen): the padding ring [outer x, y, w, h, inner x, y, w, h] + radii, the gap
// rectangles [x, y, w, h], the value labels [kind, x, y, w, h, text], the viewport (the hatches' common box).
function spaceMark() {
  if (!insData || !insShown || insEl !== (cur && cur.el)) return null;
  var gaps = [], labels = [], p = insData.pad;
  for (var i = 0; i < insData.gaps.length && i < 96; i++) { var b = insData.gaps[i]; gaps.push([r2(b.x), r2(b.y), r2(b.w), r2(b.h)]); }
  for (var j = 0; j < insData.labels.length && j < 12; j++) { var l = insData.labels[j]; if (l.w) labels.push([l.k, r2(l.x), r2(l.y), r2(l.w), 14, l.t]); }
  return { pad: p ? [r2(p.ox), r2(p.oy), r2(p.ow), r2(p.oh), r2(p.ix), r2(p.iy), r2(p.iw), r2(p.ih)] : null, radius: p ? r2(p.r) : 0, inner: p ? r2(p.ri) : 0,
           gaps: gaps, labels: labels, vw: innerWidth, vh: innerHeight };
}

// ---- spacing inspector (V3-B, Paper 5.3)
function num(v) { var n = parseFloat(v); return isFinite(n) ? n : 0; }
function r2(v) { return Math.round(v * 100) / 100; }
function flowOf(cs) { var d = cs.display || ''; return /grid/.test(d) ? 'grid' : /flex/.test(d) ? 'flex' : ''; }
function spacingOf(el) {
  try {
    var cs = (el.ownerDocument.defaultView || window).getComputedStyle(el), flow = flowOf(cs);
    var o = { padding: [r2(num(cs.paddingTop)), r2(num(cs.paddingRight)), r2(num(cs.paddingBottom)), r2(num(cs.paddingLeft))], layout: flow };
    o.gap = flow ? { row: r2(num(cs.rowGap)), column: r2(num(cs.columnGap)) } : null;
    return o;
  } catch (e) { return null; }
}
// Bands (CSS px of the top viewport) and labels of an element; null when it has neither padding nor gap.
function spaceGeometry(info) {
  if (!info || info.range || (info.kind === 'cross-origin' && info.el === info.frame)) return null;
  var el = info.el, cs;
  try { cs = (el.ownerDocument.defaultView || window).getComputedStyle(el); } catch (e) { return null; }
  var r = rectOf(info); if (r.w < 1 || r.h < 1) return null;
  var bt = num(cs.borderTopWidth), br = num(cs.borderRightWidth), bb = num(cs.borderBottomWidth), bl = num(cs.borderLeftWidth);
  var pt = num(cs.paddingTop), pr = num(cs.paddingRight), pb = num(cs.paddingBottom), pl = num(cs.paddingLeft);
  var ix = r.x + bl, iy = r.y + bt, iw = Math.max(0, r.w - bl - br), ih = Math.max(0, r.h - bt - bb);
  var g = { bands: [], gaps: [], labels: [], pad: null }, rad = cs.borderTopLeftRadius || '', radius = 0;
  if (/px$/.test(rad)) radius = Math.max(0, num(rad) - Math.max(bt, bl));
  function band(k, x, y, w, h, value, side) { if (w < 0.5 || h < 0.5) return null; var b = { k: k, x: x, y: y, w: w, h: h, v: value, side: side }; g.bands.push(b); if (k) g.gaps.push(b); return b; }
  var top = band(0, ix, iy, iw, Math.min(pt, ih), pt, 'top'), bottom = band(0, ix, iy + ih - Math.min(pb, ih), iw, Math.min(pb, ih), pb, 'bottom');
  var midH = Math.max(0, ih - pt - pb);
  var left = band(0, ix, iy + pt, Math.min(pl, iw), midH, pl, 'left'), right = band(0, ix + iw - Math.min(pr, iw), iy + pt, Math.min(pr, iw), midH, pr, 'right');
  if (top || bottom || left || right) {   // the ring: padding box (inner radius) minus the content box
    var cw0 = Math.max(0, iw - pl - pr), ch0 = Math.max(0, ih - pt - pb);
    g.pad = { ox: ix, oy: iy, ow: iw, oh: ih, r: Math.min(radius, iw / 2, ih / 2), ix: ix + Math.min(pl, iw), iy: iy + Math.min(pt, ih), iw: cw0, ih: ch0,
              ri: Math.max(0, radius - Math.max(pt, pr, pb, pl)) };
  }
  // gaps: from the children's real rectangles (rows by vertical overlap; flex keeps the real space, grid the gap)
  var flow = flowOf(cs), rg = num(cs.rowGap), cg = num(cs.columnGap), firstRow = null, firstCol = null;
  if (flow && (rg > 0 || cg > 0)) {
    var cx = ix + pl, cy = iy + pt, cw = Math.max(0, iw - pl - pr), items = [], kids = el.children;
    for (var i = 0; i < kids.length && items.length < 200; i++) {
      var c = kids[i], ccs; try { ccs = getComputedStyle(c); } catch (e) { continue; }
      if (ccs.display === 'none' || ccs.position === 'absolute' || ccs.position === 'fixed') continue;
      var b = c.getBoundingClientRect(); if (b.width <= 0 && b.height <= 0) continue;
      items.push({ x: b.left + info.ox, y: b.top + info.oy, r: b.right + info.ox, b: b.bottom + info.oy });
    }
    items.sort(function (a, b) { return a.y - b.y || a.x - b.x; });
    var rows = [];
    for (var j = 0; j < items.length; j++) {
      var it = items[j], row = rows.length ? rows[rows.length - 1] : null;
      if (row && it.y < row.b - 0.5) { row.items.push(it); row.b = Math.max(row.b, it.b); row.y = Math.min(row.y, it.y); }
      else rows.push({ y: it.y, b: it.b, items: [it] });
    }
    if (cg > 0) for (var q = 0; q < rows.length; q++) {
      var list = rows[q].items.slice().sort(function (a, b) { return a.x - b.x; });
      for (var n = 1; n < list.length; n++) {
        var from = list[n - 1].r, to = list[n].x; if (to - from <= 0.5) continue;
        if (flow === 'grid') from = Math.max(from, to - cg);
        var cb = band(1, from, rows[q].y, to - from, rows[q].b - rows[q].y, cg, 'col'); if (cb && !firstCol) firstCol = cb;
      }
    }
    if (rg > 0) for (var u = 1; u < rows.length; u++) {
      var a0 = rows[u - 1].b, a1 = rows[u].y; if (a1 - a0 <= 0.5) continue;
      if (flow === 'grid') a0 = Math.max(a0, a1 - rg);
      var rb = band(1, cx, a0, cw, a1 - a0, rg, 'row'); if (rb && !firstRow) firstRow = rb;
    }
  }
  if (!g.bands.length) return null;
  // one label per distinct value and axis (Paper 5.3: « 20 » on the top band and on the left band); none under 4 px
  function lab(b) { if (!b || Math.round(b.v) < 4) return; g.labels.push({ k: b.k, b: b, t: String(Math.round(b.v)) }); }
  if (top && bottom && Math.round(pt) === Math.round(pb)) lab(top); else { lab(top); lab(bottom); }
  if (left && right && Math.round(pl) === Math.round(pr)) lab(left); else { lab(left); lab(right); }
  lab(firstRow); lab(firstCol);
  return g;
}
function pool(kind, i) {
  var list = insPool[kind], d = list[i];
  if (!d) { d = D.createElement('div'); d.className = kind; insL.appendChild(d); list.push(d); }
  d.style.display = ''; return d;
}
function trim(kind, n) { var list = insPool[kind]; for (var i = n; i < list.length; i++) list[i].style.display = 'none'; }
function f2(v) { return (Math.round(v * 100) / 100).toString(); }
function rectPath(x, y, w, h, r) {
  r = Math.max(0, Math.min(r, w / 2, h / 2));
  if (!(r > 0.01)) return 'M' + f2(x) + ' ' + f2(y) + 'h' + f2(w) + 'v' + f2(h) + 'h' + f2(-w) + 'z';
  var a = 'a' + f2(r) + ' ' + f2(r) + ' 0 0 1 ';
  return 'M' + f2(x + r) + ' ' + f2(y) + 'h' + f2(w - 2 * r) + a + f2(r) + ' ' + f2(r) + 'v' + f2(h - 2 * r) + a + f2(-r) + ' ' + f2(r) +
         'h' + f2(-(w - 2 * r)) + a + f2(-r) + ' ' + f2(-r) + 'v' + f2(-(h - 2 * r)) + a + f2(r) + ' ' + f2(-r) + 'z';
}
// One element per layer, as big as the viewport (the gradient's box): the hatches share the viewport's origin.
function layer(el, d, rule) {
  if (!d) { el.style.display = 'none'; return; }
  el.style.display = ''; el.style.width = innerWidth + 'px'; el.style.height = innerHeight + 'px';
  el.style.clipPath = 'path(' + rule + ", '" + d + "')";
}
function hits(a, b) { return a && b && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
// A value label's place: centred on its band; on a side too thin for it, flush with the band's inner edge (it
// overflows inward, never out of the element); moved along the band, then past the obstacle, if it would collide.
function labelPlace(b, lw, obstacles) {
  var lh = 14, x = b.x + b.w / 2 - lw / 2, y = b.y + b.h / 2 - lh / 2;
  if (b.side === 'top' && b.h < lh) y = b.y;
  if (b.side === 'bottom' && b.h < lh) y = b.y + b.h - lh;
  if (b.side === 'left' && b.w < lw) x = b.x;
  if (b.side === 'right' && b.w < lw) x = b.x + b.w - lw;
  var horizontal = b.side === 'top' || b.side === 'bottom' || b.side === 'row';
  var c = [{ x: x, y: y }];
  for (var o = 0; o < obstacles.length; o++) {
    var ob = obstacles[o]; if (!ob) continue;
    if (horizontal) { c.push({ x: ob.x + ob.w + 4, y: y }); c.push({ x: ob.x - lw - 4, y: y }); c.push({ x: x, y: ob.y + ob.h + 2 }); }
    else { c.push({ x: x, y: ob.y + ob.h + 4 }); c.push({ x: x, y: ob.y - lh - 4 }); c.push({ x: ob.x + ob.w + 2, y: y }); }
  }
  for (var k = 0; k < c.length; k++) {
    var p = { x: Math.max(0, Math.min(c[k].x, innerWidth - lw)), y: Math.max(0, Math.min(c[k].y, innerHeight - lh)), w: lw, h: lh }, ok = true;
    for (var q = 0; q < obstacles.length && ok; q++) if (hits(p, obstacles[q])) ok = false;
    if (ok) return p;
  }
  return { x: Math.max(0, Math.min(x, innerWidth - lw)), y: Math.max(0, Math.min(y, innerHeight - lh)), w: lw, h: lh };
}
function drawSpace(g) {
  var counts = { pl: 0, gl: 0 };
  if (!g) { layer(padEl, null); layer(gapEl, null); trim('pl', 0); trim('gl', 0); return; }
  var p = g.pad;
  layer(padEl, p ? rectPath(p.ox, p.oy, p.ow, p.oh, p.r) + (p.iw > 0 && p.ih > 0 ? rectPath(p.ix, p.iy, p.iw, p.ih, p.ri) : '') : null, 'evenodd');
  var gd = ''; for (var i = 0; i < g.gaps.length && i < 96; i++) { var b = g.gaps[i]; gd += rectPath(b.x, b.y, b.w, b.h, 0); }
  layer(gapEl, gd || null, 'nonzero');
  var obstacles = labelBox.slice();
  for (var j = 0; j < g.labels.length && j < 12; j++) {
    var l = g.labels[j], lk = l.k ? 'gl' : 'pl', e = pool(lk, counts[lk]++);
    e.textContent = l.t;
    var pl = labelPlace(l.b, e.offsetWidth || 20, obstacles); obstacles.push(pl);
    e.style.transform = 'translate(' + pl.x.toFixed(2) + 'px,' + pl.y.toFixed(2) + 'px)';
    l.x = pl.x; l.y = pl.y; l.w = pl.w; l.h = 14;
  }
  trim('pl', counts.pl); trim('gl', counts.gl);
}
function insFade(on, animate) {
  for (var i = 0, l = [ins, insL]; i < 2; i++) { if (animate && !reduced) l[i].classList.add('on'); else l[i].classList.remove('on'); l[i].style.opacity = on ? '1' : '0'; }
  insShown = on;
}
// The inspector follows the frame: same element => updated in place; another element => hidden now, shown when the
// pointer has rested 120 ms (the frame's glide); nothing at all during a fast movement.
function placeSpace(info, first, g) {
  var el = info ? info.el : null;
  insData = info ? (g === undefined ? spaceGeometry(info) : g) : null;
  drawSpace(insData);
  if (!insData) { if (insTimer) { clearTimeout(insTimer); insTimer = 0; } insFade(false, false); insEl = el; return; }
  if (el === insEl) return;
  insEl = el; if (insTimer) { clearTimeout(insTimer); insTimer = 0; }
  if (first) { insFade(true, true); return; }   // first appearance: with the frame's own fade
  insFade(false, false);
  insTimer = setTimeout(function () { insTimer = 0; if (alive && insData && insEl === el) insFade(true, true); }, reduced ? 0 : 120);
}

// ---- glued to the page (V1.2, see the header)
// Does this element scroll with the document? Not when it (or a box around it, up to the top document) is fixed or
// sticky. Same-origin frames: their element in the top document decides. Cached per element.
var fixedOf = { el: null, v: false };
function inFixed(el) {
  if (!el) return false;
  if (fixedOf.el === el) return fixedOf.v;
  var v = false, n = el;
  for (var g = 0; n && g < 400 && !v; g++) {
    if (n.nodeType === 1) { try { var p = (n.ownerDocument.defaultView || window).getComputedStyle(n).position; if (p === 'fixed' || p === 'sticky') v = true; } catch (e) {} }
    var up = n.parentElement;
    if (!up) { var rn = n.getRootNode ? n.getRootNode() : null; up = rn && rn.host ? rn.host : null; }
    if (!up && n.ownerDocument && n.ownerDocument !== D) { try { up = n.ownerDocument.defaultView.frameElement; } catch (e) { up = null; } }
    n = up;
  }
  fixedOf = { el: el, v: v };
  return v;
}
function anchorEl(info) {
  if (!info) return null;
  if (info.frames && info.frames.length) return info.frames[0];
  if (info.range) { var c = info.range.commonAncestorContainer; return c && c.nodeType === 1 ? c : c ? c.parentElement : null; }
  return info.el;
}
// The wrapper at the viewport's origin, in document coordinates (or fixed), as big as the viewport's client box.
function anchor(wrap, el) {
  var de = D.documentElement, cw = Math.min(innerWidth, (de && de.clientWidth) || innerWidth), ch = Math.min(innerHeight, (de && de.clientHeight) || innerHeight);
  if (inFixed(el)) { wrap.style.position = 'fixed'; wrap.style.transform = 'translate(0px,0px)'; }
  else { var hr = host.getBoundingClientRect(); wrap.style.position = 'absolute'; wrap.style.transform = 'translate(' + (-hr.left).toFixed(2) + 'px,' + (-hr.top).toFixed(2) + 'px)'; }
  wrap.style.width = cw + 'px'; wrap.style.height = ch + 'px';
}
// Paper 18QJ-0 + R15: the frame's box (4 px out of the element, at least 2 px from the window's left and right sides).
// R27: snapped to the device pixels, so the hover / selection frame, the conversation frame and the bubble's frame are
// the same pixels whatever the zoom (100 / 125 / 150 %); edgePx = the 2 px border rounded to whole device pixels.
function dpr() { return devicePixelRatio || 1; }
function snap(v) { var d = dpr(); return Math.round(v * d) / d; }
function edgePx() { var d = dpr(); return Math.max(1, Math.round(2 * d)) / d; }
// The border as the engine draws it (it may snap the width again): what the inner corner of R33 is measured from.
function edgeShown() { var v = 0; try { v = parseFloat(getComputedStyle(fill).borderTopWidth); } catch (e) {} return v > 0 ? v : edgePx(); }
function frameBox(r) {
  var x = snap(Math.max(2, r.x - 4)), right = snap(Math.min(innerWidth - 2, r.x + Math.max(r.w, 1) + 4));
  var y = snap(r.y - 4), bottom = snap(r.y + Math.max(r.h, 1) + 4);
  return { x: x, y: y, w: Math.max(right - x, 1 / dpr()), h: Math.max(bottom - y, 1 / dpr()) };
}
// The same box on any frame element (hover .f, conversation .cf, bubble .bf, chat mark .mf): position, size, border.
// R45: a label at a point snapped to device pixels (the same text pixels in the hover and the bubble labels).
function atPx(el, x, y) { el.style.transform = 'translate(' + snap(x).toFixed(6) + 'px,' + snap(y).toFixed(6) + 'px)'; }
function boxTo(el, fb, viaTransform) {
  if (viaTransform !== false) el.style.transform = 'translate(' + fb.x.toFixed(6) + 'px,' + fb.y.toFixed(6) + 'px)';
  el.style.width = fb.w.toFixed(6) + 'px'; el.style.height = fb.h.toFixed(6) + 'px'; el.style.borderWidth = edgePx().toFixed(6) + 'px';
}
// Paper 18QK-0 / 18QQ-0: the name label flush with the frame's left edge, the size label with its right edge, both on
// the frame's top (24 px above the element); under the frame when there is no room above. R15: 2 px from the sides.
// `both`: false when the two do not fit side by side: R33, the size label then goes UNDER the name, inside the frame,
// flush with its inner top-left corner (same style, same corners); under the name when the labels are below the frame.
function labelsPlace(fb, lw, rw) {
  var above = fb.y - 20 >= 0, y = above ? fb.y - 20 : Math.min(fb.y + fb.h, innerHeight - 20);
  var lx = Math.max(2, Math.min(fb.x, innerWidth - 2 - lw)), rx = Math.min(innerWidth - 2, fb.x + fb.w) - rw;
  if (rx >= lx + lw + 2) return { lx: lx, rx: rx, y: y, ry: y, both: true };
  var e = edgeShown(), ix = Math.max(2, Math.min(fb.x + e, innerWidth - 2 - rw));
  return above ? { lx: lx, rx: ix, y: y, ry: fb.y + e, both: false } : { lx: lx, rx: lx, y: y, ry: Math.min(y + 20, innerHeight - 20), both: false };
}
// The value labels keep off the element labels (both boxes, + 2 px of air).
function labelBoxes(lp, lw, rw) {
  var a = { x: lp.lx - 2, y: lp.y - 2, w: lw + 4, h: 24 }, b = { x: lp.rx - 2, y: lp.ry - 2, w: rw + 4, h: 24 };
  return lp.both ? [{ x: a.x, y: a.y, w: lp.rx + rw - lp.lx + 4, h: 24 }] : [a, b];
}
function glide(on) { for (var i = 0; i < parts.length; i++) parts[i].style.transition = on ? '' : 'none'; }

// ---- drawing
function place(info) {
  if (follow && follow.draw && !(peek && info === peek)) {   // D1: the bubble's frame (bx) replaces the hover / selection frame
    // final pass 1 (R45): in the same frame, never faded (a fade would show the two 8 % fills on top of each other)
    if (shown) { glide(false); setOpacity(0); for (var i = 0; i < parts.length; i++) void getComputedStyle(parts[i]).opacity; shown = false; }
    glide(true); placed = null; return;
  }   // R16: its inspector stays (bxPlace)
  if (!info) { glide(true); placed = null; if (shown) { setOpacity(0); shown = false; } placeSpace(null, false); return; }
  anchor(frameWrap, anchorEl(info));
  glide(!(shown && placed && placed.el === info.el && placed.range === info.range));   // the same element: it jumps (no lag behind a scroll)
  placed = { el: info.el, range: info.range };
  var r = rectOf(info);
  var first = !shown;
  if (first) moveOnly(true);
  // Paper 18QJ-0: the frame goes 4 px out of the element; R15: never closer than 2 px to the window's left / right sides
  var fb = frameBox(r), x = fb.x, y = fb.y, w = fb.w, h = fb.h;
  boxTo(fill, fb); void x; void y; void w; void h;   // R27: one bordered box, the conversation frame's rectangle
  if (cmOn(info.el)) fill.classList.add('nb'); else fill.classList.remove('nb');   // R27: its contour is already there: drawn once
  var tag = seg(info.el === info.frame && info.kind === 'cross-origin' ? info.frame : info.el);
  label.textContent = info.range ? 'texte' : tag; label2.textContent = Math.round(r.w) + 'x' + Math.round(r.h);   // R14 (Paper writes « 240x44 »)
  var lw = label.offsetWidth || 120, rw = label2.offsetWidth || 60, lp = labelsPlace(fb, lw, rw);
  atPx(label, lp.lx, lp.y); atPx(label2, lp.rx, lp.ry);   // R33: the size label never disappears; R45: device pixels
  labelBox = labelBoxes(lp, lw, rw);
  if (first) { void getComputedStyle(fill).transform; moveOnly(false); setOpacity(1); shown = true; }
  placeSpace(info, first);
}
// Shields over the visible top-level iframes (same and cross origin): their events reach our window
// listeners, the hit-test (elementsFromPoint without our host) still finds the iframe below.
function syncShields() {
  if (idle()) { for (var z = 0; z < shields.length; z++) shields[z].style.cssText = 'display:none'; return; }
  var list = D.querySelectorAll('iframe,frame'), n = 0;
  for (var i = 0; i < list.length && n < 64; i++) {
    var r = list[i].getBoundingClientRect();
    if (r.width < 1 || r.height < 1 || r.bottom < 0 || r.right < 0 || r.top > innerHeight || r.left > innerWidth) continue;
    var s = shields[n];
    if (!s) { s = D.createElement('div'); s.className = 's'; root.appendChild(s); shields.push(s); }
    s.style.cssText = 'left:' + r.left + 'px;top:' + r.top + 'px;width:' + r.width + 'px;height:' + r.height + 'px';
    n++;
  }
  for (var j = n; j < shields.length; j++) shields[j].style.cssText = 'display:none';
}
function same(a, b) { return a && b && a.el === b.el && a.range === b.range; }
function frame() {
  raf = 0; if (!alive) return;
  syncShields();
  if (mark) markPlace();
  cmPlaceAll(); followCheck();
  if (shown && placed) { if (cmOn(placed.el)) fill.classList.add('nb'); else fill.classList.remove('nb'); }   // R27
  if (tracking()) { schedule(); if (px >= 0 && !frozen) haveMove = true; }   // R36: every frame while the page's values glide
  if (peek) { if (peek.el.isConnected) place(peek); else peekSet(null); return; }   // R26
  if (off) return;
  if (frozen) { if (cur) place(cur); return; }
  if (ctrl) return;   // R31
  if (!haveMove) return;
  haveMove = false;
  var info = hit(px, py);
  if (!same(info, cur)) down = [];
  cur = info; place(cur);
}
function schedule() { if (!raf && alive) raf = requestAnimationFrame(frame); }

// ---- events
var modsAt = 0;
function onMove(e) {
  if (e.isTrusted && !e.ctrlKey) { var tn = performance.now(); if (tn - modsAt > 30) { modsAt = tn; post({ op: 'mods' }); } }   // R31: the host reads Ctrl (its keyboard)
  px = e.clientX; py = e.clientY; if (e.isTrusted && !!e.ctrlKey !== ctrl && !(hostCtrl && !e.ctrlKey)) setCtrl(!!e.ctrlKey); if (idle()) return; haveMove = true; keyboard = false; schedule(); }
// R31: Ctrl held = the page as usual: its own cursors (the sheet is emptied), no hover frame nor inspector, no iframe
// shields; clicks, scrolling and typing reach the site, nothing is selected. Released = the selection at once. A
// selected element (note open, bubble) keeps its frame. The Ctrl+... shortcuts are never consumed here (onKey).
function cursorSheet(on) { try { if (sheet) sheet.replaceSync(on ? '*,*::before,*::after{' + cursorDecl(true) + '}' : ''); } catch (e) {} }
function setCtrl(v) {
  v = !!v; if (v === ctrl) return; ctrl = v;
  if (off) return;
  if (ctrl) { cursorSheet(false); if (!frozen && !peek) { cur = null; down = []; haveMove = false; place(null); } syncShields(); }
  else { cursorSheet(true); syncShields(); haveMove = px >= 0; schedule(); }
}
function onKeyUp(e) { if (e.isTrusted && e.key === 'Control') { hostCtrl = false; setCtrl(false); } }
function onBlur(e) { if (e.target === window && !hostCtrl) setCtrl(false); }
// Only the user's own input is consumed: the page's synthetic events (el.click(), dispatchEvent) pass.
// Final pass 1, defect 2: a button event never RELEASES Ctrl (in the real window the click may come without the Ctrl flag
// while it is held: it was selected). Ctrl ends on its keyup, a window blur, or a pointer move without the flag.
function stop(e) {
  if (!e.isTrusted || off) return;
  if (e.type === 'pointerdown') { dpx = e.clientX; dpy = e.clientY; }   // the press, at the pointer's fractional point (see onClick)
  if (e.ctrlKey) setCtrl(true); if (ctrl) return; e.preventDefault(); e.stopImmediatePropagation();
}
var dpx = -1, dpy = -1;
function onClick(e) {
  if (!e.isTrusted || off) return;
  if (e.ctrlKey) setCtrl(true); if (ctrl) return;   // R31: the site's click
  stop(e);
  if (e.button !== 0) return;
  // Final pass 1 (R45 div.plans, monkey a.btn-primary): a click's clientX / clientY are whole CSS pixels, the pointer's are
  // not (page zoom, scaling): at 125 % the rounded point can fall on the neighbour or the container of what the hover
  // shows. The press's own pointer point (same gesture) is used, so the click selects exactly what the hover framed.
  var near = dpx >= 0 && Math.abs(dpx - e.clientX) < 1 && Math.abs(dpy - e.clientY) < 1;
  px = near ? dpx : e.clientX; py = near ? dpy : e.clientY; keyboard = false;
  frozen = false;
  var info = hit(px, py);
  if (!info) return;
  if (e.altKey) { var p = parentInfo(info); if (p) info = p; }
  cur = info; down = []; frozen = true; place(cur); sendSelect(cur, null, e);
}
function onKey(e) {
  if (e.isTrusted && e.key === 'Control') { setCtrl(true); return; }   // R31 (never consumed: Ctrl+... stay the site's and the host's)
  if (!e.isTrusted || e.ctrlKey || e.metaKey || idle()) return;
  var k = e.key;
  if (k === 'Escape') { stop(e); post({ op: 'escape' }); return; }
  if (frozen) return;
  if (k === 'ArrowUp' && cur && !e.altKey) { stop(e); var p = parentInfo(cur); if (p) { down.push(cur); cur = p; keyboard = true; place(cur); } return; }
  if (k === 'ArrowDown' && cur && !e.altKey) { stop(e); var c = down.length ? down.pop() : childInfo(cur); if (c) { cur = c; keyboard = true; place(cur); } return; }
  if (k === 'Enter' && cur) { stop(e); keyboard = true; frozen = true; place(cur); sendSelect(cur, null, e); return; }
}
function onFocus(e) {
  if (frozen || idle() || peek) return;
  var t = e.composedPath ? e.composedPath()[0] : e.target;
  if (!t || t.nodeType !== 1 || t === host) return;
  var r = t.getBoundingClientRect();
  cur = { el: t, kind: 'main', ox: 0, oy: 0, shadow: [], frames: [], frame: null, range: null };
  if (t.getRootNode && t.getRootNode().host) { var info = hit(r.left + 1, r.top + 1); if (info && info.el === t) cur = info; }
  down = []; keyboard = true; place(cur);
}
function onScroll() { if (follow) followPost();   // R39: the bubble moves with this very scroll, before any drawing
  if (peek || tracking()) { schedule(); return; } if (follow || cmAny()) schedule(); if (off && !mark) return; if (!cur && !mark) return; if (cur && !frozen && px >= 0) haveMove = true; schedule(); }
function onLeave(e) { if (!idle() && !peek && !e.relatedTarget && !frozen) { haveMove = false; cur = null; place(null); } }

function selectionInfo(text) {
  var sel = null; try { sel = window.getSelection(); } catch (e) {}
  var ox = 0, oy = 0, frames = [], kind = 'main';
  var ae = D.activeElement;
  if ((!sel || sel.isCollapsed) && ae && (ae.localName === 'iframe' || ae.localName === 'frame')) {
    try { var cw = ae.contentWindow, s2 = cw.getSelection(); if (s2 && !s2.isCollapsed) { var o = frameOffset(ae); sel = s2; ox = o.x; oy = o.y; frames = [ae]; kind = 'same-origin'; } }
    catch (e) { return { info: { el: ae, kind: 'cross-origin', ox: 0, oy: 0, shadow: [], frames: [], frame: ae, range: null }, text: text }; }
  }
  if (!sel || sel.isCollapsed || !sel.rangeCount) return null;
  var range = sel.getRangeAt(0), node = range.commonAncestorContainer;
  var el = node.nodeType === 1 ? node : node.parentElement;
  if (!el) return null;
  var t = String(sel.toString() || text || '');
  if (t.length > MAX_TEXT) t = t.slice(0, MAX_TEXT);
  return { info: { el: el, kind: kind, ox: ox, oy: oy, shadow: [], frames: frames, frame: frames[0] || null, range: range }, text: t };
}
function selectAtPoint(x, y) {
  var dpr = devicePixelRatio || 1; px = x / dpr; py = y / dpr;
  var info = hit(px, py); if (!info) return;
  cur = info; down = []; frozen = true; place(cur); sendSelect(cur, null);
}
function selectSelection(text) {
  var s = selectionInfo(text);
  if (!s) { return false; }
  cur = s.info; down = []; frozen = true; place(cur); sendSelect(cur, s.text || String(text || '').slice(0, MAX_TEXT));
  return true;
}

// ---- chat intégré (design/chat-integration.md): where the selected element comes from in the code, and the mark an
// agent's turn leaves on it. Both are asked by the host only while the chat panel is open; nothing runs otherwise.
// source(): debug data only, read synchronously, never guessed — Astro's data-astro-source-file / -loc on the element
// (depth 0) or on one of its 10 ancestors (depth > 0: the host gives that one to the agent as « nearest ancestor », never
// as the element's own line); React's fiber of the element itself (_debugSource of that fiber or of its owners,
// React < 19; the `__source` prop where a build keeps it). The host checks the path against the project folder.
function fiberOf(el) {
  try { var ks = Object.keys(el); for (var i = 0; i < ks.length; i++) if (ks[i].indexOf('__reactFiber$') === 0 || ks[i].indexOf('__reactInternalInstance$') === 0) return el[ks[i]]; } catch (e) {}
  return null;
}
function compName(t) {
  try { if (typeof t === 'function') return String(t.displayName || t.name || ''); if (t && typeof t === 'object') { if (t.displayName) return String(t.displayName); if (t.render) return String(t.render.displayName || t.render.name || ''); if (t.type) return compName(t.type); } } catch (e) {}
  return '';
}
function sourceOf() {
  var out = { astro: null, react: null, components: [] };
  if (!cur || !cur.el || cur.range || (cur.kind === 'cross-origin' && cur.el === cur.frame)) return out;
  try {
    for (var n = cur.el, d = 0; n && n.nodeType === 1 && d <= 10; n = n.parentElement, d++) {
      var f = n.getAttribute('data-astro-source-file');
      if (f) { out.astro = { file: String(f).slice(0, 1024), loc: String(n.getAttribute('data-astro-source-loc') || '').slice(0, 32), depth: d }; break; }
    }
  } catch (e) {}
  try {
    var fb = null;
    fb = fiberOf(cur.el);   // the element's own fiber only: an ancestor's source is not this element's
    for (var o = fb, g = 0; o && g < 40; g++) {
      var ds = o._debugSource || (o.memoizedProps && o.memoizedProps.__source) || (o.pendingProps && o.pendingProps.__source);
      if (ds && !out.react && typeof ds.fileName === 'string' && ds.fileName) out.react = { file: ds.fileName.slice(0, 1024), line: +ds.lineNumber || 0, column: +ds.columnNumber || 0 };
      if (typeof o.type !== 'string') { var nm = compName(o.type); if (nm && nm.length <= 80 && !/^_|^(Fragment|Suspense|StrictMode|Profiler)$/.test(nm) && out.components.indexOf(nm) < 0 && out.components.length < 6) out.components.push(nm); }
      o = o._debugOwner || o.return;
    }
  } catch (e) {}
  return out;
}
// sources(): the source files this page's debug data names (Astro attributes of the document, React debug sources of
// a sample of elements), each once, 24 at most. The host checks them against the project folder it is about to use.
function sourcesOf() {
  var out = [], seen = {};
  function add(f) { f = String(f || '').slice(0, 1024); if (f && !seen[f] && out.length < 24) { seen[f] = 1; out.push(f); } }
  try { var l = D.querySelectorAll('[data-astro-source-file]'); for (var i = 0; i < l.length && i < 400; i++) add(l[i].getAttribute('data-astro-source-file')); } catch (e) {}
  try {
    var all = D.body ? D.body.querySelectorAll('*') : [];
    for (var j = 0; j < all.length && j < 300 && out.length < 24; j++) {
      for (var o = fiberOf(all[j]), g = 0; o && g < 12; g++) {
        var ds = o._debugSource || (o.memoizedProps && o.memoizedProps.__source);
        if (ds && typeof ds.fileName === 'string' && ds.fileName) { add(ds.fileName); break; }
        o = o._debugOwner || o.return;
      }
    }
  } catch (e) {}
  return out;
}
// mark: { state: 'working' | 'done' | 'show' | 'clear', selector, current, title, sub } — Paper 6.10 (label « ⟳ titre - modèle »
// above the element) and 6.11 (frame + check tag). `current`: the element selected now (no reload since); otherwise
// the selector must match exactly one element of the top document, or there is no mark at all.
var mark = null, markTimer = 0;
function markClear() {
  if (markTimer) { clearInterval(markTimer); markTimer = 0; }
  if (mark) { try { mark.box.remove(); } catch (e) {} mark = null; }
}
function markPlace() {
  if (!mark || !alive) return;
  var el = mark.el;
  if (!el || !el.isConnected) { markClear(); return; }
  var r = el.getBoundingClientRect();
  if (r.width < 1 && r.height < 1) { mark.box.style.opacity = '0'; return; }
  anchor(markWrap, mark.anchor);
  var fb = frameBox({ x: r.left + mark.ox, y: r.top + mark.oy, w: r.width, h: r.height }), y = fb.y, h = fb.h;   // R27: the hover frame's rectangle
  mark.box.style.opacity = '1';
  mark.box.style.transform = 'translate(' + fb.x.toFixed(6) + 'px,' + fb.y.toFixed(6) + 'px)';
  boxTo(mark.frame, fb, false);
  if (mark.tag) mark.tag.style.transform = y >= 20 ? 'translate(0,-20px)' : 'translate(0,' + h.toFixed(2) + 'px)';
}
function markSet(a) {
  markClear();
  if (!a || a.state === 'clear') return;
  var el = null, ox = 0, oy = 0;
  if (a.current && cur && cur.el && !cur.range) { el = cur.el; ox = cur.ox; oy = cur.oy; }
  else if (a.selector) { try { var l = D.querySelectorAll(String(a.selector)); if (l.length === 1) el = l[0]; } catch (e) {} }
  if (!el) return false;
  var box = D.createElement('div'); box.className = 'mk';
  var fr = D.createElement('div'); fr.className = 'mf'; box.appendChild(fr);
  var tag = null;
  if (a.state === 'working') {
    tag = D.createElement('div'); tag.className = 'mt';
    var sp = D.createElement('div'); sp.className = 'ms' + (reduced ? '' : ' spin'); tag.appendChild(sp);
    var t1 = D.createElement('span'); t1.textContent = String(a.title || '').slice(0, 60) + (a.sub ? ' - ' : ''); tag.appendChild(t1);
    if (a.sub) { var t2 = D.createElement('span'); t2.className = 'mu'; t2.textContent = String(a.sub).slice(0, 40); t1.appendChild(t2); }
    box.appendChild(tag);
  } else if (a.state === 'show') {   // chat, « Traiter les annotations »: where this annotation is — brought into view, framed for a moment
    try { el.scrollIntoView({ block: 'center', inline: 'nearest', behavior: reduced ? 'auto' : 'smooth' }); } catch (e) {}
    setTimeout(function () { if (mark && mark.box === box) markClear(); }, 2600);
  } else {
    var ck = D.createElement('div'); ck.className = 'mc';
    try { ck.innerHTML = '<svg width="12" height="12" viewBox="0 0 256 256"><path d="M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z" fill="#fff"/></svg>'; } catch (e) {}
    fr.appendChild(ck);
  }
  markWrap.appendChild(box);
  mark = { el: el, ox: ox, oy: oy, box: box, frame: fr, tag: tag, state: a.state, anchor: a.current && cur && cur.el === el ? anchorEl(cur) : el };
  markPlace();
  markTimer = setInterval(markPlace, a.state === 'working' || a.state === 'show' ? 100 : 400);   // follows a layout change (HMR) without a frame loop
  return true;
}

// ---- Mode Commentaire: one frame + icon per conversation of this page, and the element the bubble follows
var MARK_SVG = '<svg width="13" height="12.25" viewBox="48 239.883 13 12.26"><path d="M48.001 250.363L48.049 240.981C48.051 240.535 48.43 240.178 48.89 240.19L59.287 240.226C61.157 240.272 61.667 242.743 59.96 243.484L54.486 245.391C54.03 245.589 53.645 245.909 53.375 246.314 53.342 246.363 53.316 246.416 53.293 246.47L51.265 251.246C50.347 252.733 47.992 252.096 48.001 250.363Z" fill="#fff"/></svg>';
// R27: the element has its conversation frame on screen: the hover / bubble frame on it draws no second contour (two
// identical anti-aliased edges would add up on a fractional pixel), only its fill and labels.
function cmOn(el) { for (var k in cmarks) { var m = cmarks[k]; if (m.el === el && m.wrap.style.display !== 'none') return true; } return false; }
function cmAny() { for (var k in cmarks) return true; return false; }
function cmFind(sel) { try { var l = D.querySelectorAll(String(sel)); return l.length === 1 ? l[0] : null; } catch (e) { return null; } }
function cmTag(m) {
  var tag = m.tag; while (tag.firstChild) tag.removeChild(tag.firstChild);
  tag.className = 'ct' + (m.state === 'waiting' ? ' wait' : '');
  // D2 (Paper 18IJ-0): the icon as drawn, whether the agent works or not (no spinner: Paper draws none)
  var holder = D.createElement('span'); try { holder.innerHTML = MARK_SVG; } catch (e) {} while (holder.firstChild) tag.appendChild(holder.firstChild);
  if (m.state === 'waiting') { var t = D.createElement('span'); t.textContent = String(m.waitLabel || 'En attente').slice(0, 40); tag.appendChild(t); }
}
function cmPlace(m) {
  if (!m.el || !m.el.isConnected) { m.el = cmFind(m.selector); }
  if (!m.el) { m.wrap.style.display = 'none'; return false; }
  var r = m.el.getBoundingClientRect();
  if (r.width < 1 && r.height < 1) { m.wrap.style.display = 'none'; return false; }
  m.wrap.style.display = ''; anchor(m.wrap, m.el);
  var fb = frameBox({ x: r.left, y: r.top, w: r.width, h: r.height });   // R27: exactly the hover / selection frame's rectangle
  m.box.style.transform = 'translate(' + fb.x.toFixed(6) + 'px,' + fb.y.toFixed(6) + 'px)';
  boxTo(m.frame, fb, false);
  return true;
}
function cmPlaceAll() { for (var k in cmarks) cmPlace(cmarks[k]); }
function cmSet(list) {
  var keep = {}, found = [];
  if (!Array.isArray(list)) list = [];
  for (var i = 0; i < list.length && i < 200; i++) {
    var it = list[i] || {}; var id = String(it.id || '').slice(0, 128); if (!id || !it.selector) continue;
    keep[id] = 1;
    var m = cmarks[id];
    if (!m || m.selector !== String(it.selector)) {
      if (m) try { m.wrap.remove(); } catch (e) {}
      var wrap = D.createElement('div'); wrap.className = 'w';
      var box = D.createElement('div'); box.className = 'mk'; box.style.opacity = '1';
      var fr = D.createElement('div'); fr.className = 'cf'; var tag = D.createElement('div'); tag.className = 'ct';
      fr.appendChild(tag); box.appendChild(fr); wrap.appendChild(box); root.insertBefore(wrap, markWrap);
      m = cmarks[id] = { id: id, selector: String(it.selector), wrap: wrap, box: box, frame: fr, tag: tag, el: null, state: '' };
    }
    if (m.state !== String(it.state || 'idle') || m.waitLabel !== it.waitLabel) { m.state = String(it.state || 'idle'); m.waitLabel = it.waitLabel; cmTag(m); }
    m.el = null;
    if (cmPlace(m)) found.push(id);
  }
  for (var k in cmarks) if (!keep[k]) { try { cmarks[k].wrap.remove(); } catch (e) {} delete cmarks[k]; }
  if (cmTimer) { clearInterval(cmTimer); cmTimer = 0; }
  if (cmAny()) cmTimer = setInterval(cmPlaceAll, 400);   // follows a layout change (HMR) without a frame loop
  schedule();   // R27: a frame over one of these elements draws its contour once
  return found;
}
// R39: where the bubble's element is, posted BEFORE anything is drawn (the drawing — frame, labels, inspector — reads
// computed styles: it must never delay the host). Called from the scroll listener itself (the same task as the scroll)
// and at the start of every frame / check. Returns the element and its rectangle (null: gone).
function followPost() {
  if (!follow) return null;
  var el = follow.el;
  if (!el || !el.isConnected) { el = follow.el = follow.selector ? cmFind(follow.selector) : null; }
  // Review C3: « gone » only once the element has been missing for 500 ms and at least 2 checks (an agent's edit or a hot
  // reload replaces it for a moment); back before that: nothing was said, it is followed again.
  if (!el) {
    var now = performance.now();
    if (!follow.missSince) { follow.missSince = now; follow.missChecks = 0; }
    follow.missChecks++;
    if (followLast !== 'gone' && now - follow.missSince >= 500 && follow.missChecks >= 2) { followLast = 'gone'; post({ op: 'anchor', gone: true }); }
    return null;
  }
  follow.missSince = 0; follow.missChecks = 0;
  var r = el.getBoundingClientRect(), o = { x: r.left + follow.ox, y: r.top + follow.oy, w: r.width, h: r.height, vw: innerWidth, vh: innerHeight };
  var key = [o.x, o.y, o.w, o.h, o.vw, o.vh].map(function (v) { return v.toFixed(1); }).join(',');
  if (key !== followLast) {
    followLast = key; o.op = 'anchor'; o.sx = scrollX; o.sy = scrollY;
    o.t = performance.timeOrigin ? performance.timeOrigin + performance.now() : Date.now();   // R39: the host measures page -> host
    post(o);
  }
  return { el: el, r: r };
}
function followCheck() {
  if (!follow) return;
  var f = followPost();   // posted first
  bxPlace(f ? f.el : null, f ? f.r : null);   // then drawn
}
// D1: the frame + two labels of the bubble's element (document-anchored like the marks: glued while scrolling)
var bx = null;
function bxPlace(el, r) {
  if (!follow || !follow.draw || !el) { if (bx) { try { bx.wrap.remove(); } catch (e) {} bx = null; } return; }
  if (!bx) {
    var wrap = D.createElement('div'); wrap.className = 'w'; var box = D.createElement('div'); box.className = 'bx';
    var fr = D.createElement('div'); fr.className = 'bf'; var l1 = D.createElement('div'); l1.className = 'bl'; var l2 = D.createElement('div'); l2.className = 'bl';
    box.appendChild(fr); box.appendChild(l1); box.appendChild(l2); wrap.appendChild(box); root.insertBefore(wrap, markWrap);
    bx = { wrap: wrap, box: box, frame: fr, left: l1, right: l2 };
  }
  bx.wrap.style.display = ''; anchor(bx.wrap, el);
  var fb = frameBox({ x: r.left, y: r.top, w: r.width, h: r.height });   // R15: 2 px from the window's sides; R27: the hover frame's box
  boxTo(bx.frame, fb); if (cmOn(el)) bx.frame.classList.add('nb'); else bx.frame.classList.remove('nb');
  var t1 = String(follow.draw.label || seg(el)).slice(0, 120), t2 = Math.round(r.width) + 'x' + Math.round(r.height);
  if (bx.left.textContent !== t1) bx.left.textContent = t1;
  if (bx.right.textContent !== t2) bx.right.textContent = t2;
  var lw = bx.left.offsetWidth || 120, rw = bx.right.offsetWidth || 60, lp = labelsPlace(fb, lw, rw);
  atPx(bx.left, lp.lx, lp.y);
  atPx(bx.right, lp.rx, lp.ry);   // R33: under the name, inside the frame, when narrow; R45: placed as the hover labels
  // R16: the padding and gap of the selected element stay drawn, bubble open. R37: computed in the same frame as the
  // frame and its labels (glued while scrolling); the soft transition only when a VALUE changed (padding, gap, size),
  // never for a move (scroll, window resize), and never while a restyle (R36) is followed frame by frame.
  if (follow.el && !peek && !(cur && cur.range)) {
    labelBox = labelBoxes(lp, lw, rw);
    anchor(frameWrap, el);
    var info = { el: el, range: null, frame: null, ox: 0, oy: 0, kind: 'main' }, g = spaceGeometry(info), key = spaceKey(g, r), now = performance.now();
    var resized = innerWidth !== insVW || innerHeight !== insVH; insVW = innerWidth; insVH = innerHeight;
    if (insShown && insEl === el && key !== insKey && !resized && !tracking()) smUntil = now + 200;
    smooth(now < smUntil);
    insKey = key;
    placeSpace(info, !insShown, g);
  }
}
// R37: the inspector's values relative to the element (what a scroll does not change), rounded to 1/4 px.
var insKey = '', insVW = 0, insVH = 0, smUntil = 0;
function spaceKey(g, r) {
  if (!g) return '';
  var q = function (v) { return Math.round(v * 4) / 4; }, out = [q(r.width), q(r.height)], p = g.pad;
  if (p) out.push(q(p.ox - r.left), q(p.oy - r.top), q(p.ow), q(p.oh), q(p.ix - r.left), q(p.iy - r.top), q(p.iw), q(p.ih), q(p.r));
  for (var i = 0; i < g.gaps.length; i++) { var b = g.gaps[i]; out.push(q(b.x - r.left), q(b.y - r.top), q(b.w), q(b.h)); }
  for (var j = 0; j < g.labels.length; j++) out.push(g.labels[j].t);
  return out.join(',');
}
// R16: soft updates of the selected element's inspector (not for the hover, which jumps to another element)
function smooth(on) {
  var l = [padEl, gapEl].concat(insPool.pl, insPool.gl);
  for (var i = 0; i < l.length; i++) { if (on && !reduced) l[i].classList.add('sm'); else l[i].classList.remove('sm'); }
}
function followSet(a) {
  if (followTimer) { clearInterval(followTimer); followTimer = 0; }
  var drew = follow && follow.draw; follow = null; followLast = ''; bxPlace(null);
  if (drew) { smooth(false); placeSpace(null, false); haveMove = px >= 0; schedule(); }   // the hover frame (and its inspector) come back under the pointer
  if (!a) return true;
  var el = null, ox = 0, oy = 0;
  if (a.current && cur && cur.el && !cur.range) { el = cur.el; ox = cur.ox; oy = cur.oy; }
  else if (a.selector) el = cmFind(a.selector);
  follow = { el: el, selector: a.selector ? String(a.selector) : '', ox: ox, oy: oy, draw: a.draw && !(cur && cur.range) ? { label: String(a.draw.label || '') } : null };
  if (follow.draw) place(null);
  followCheck();
  followTimer = setInterval(followCheck, 250);
  return !!el;
}

// R26: the user rests on a chip of an element (composer or thread, panel or bubble): the page scrolls softly to it when it
// is not in view and shows it as hovered (frame, the two labels, the padding / gap inspector); a new one replaces it;
// at the end, back to what was there (the selected element, the bubble's element, or nothing), without scrolling back.
function peekSet(a) {
  var el = null;
  if (a && a.active && a.selector) { el = cmFind(a.selector); if (!el) { try { el = D.querySelector(String(a.selector)); } catch (e) { el = null; } } }
  if (!el) {
    if (!peek) return false;
    peek = null; insEl = null; smooth(false);
    if (follow && follow.draw) { place(null); followLast = ''; followCheck(); }
    else if (frozen && cur) place(cur);
    else { cur = null; haveMove = false; place(null); }
    return false;
  }
  if (peek && peek.el === el) return true;
  peek = { el: el, kind: 'main', ox: 0, oy: 0, shadow: [], frames: [], frame: null, range: null };
  smooth(false);
  var r = el.getBoundingClientRect(), seen = r.bottom > 0 && r.top < innerHeight && r.right > 0 && r.left < innerWidth;
  var whole = r.top >= 0 && r.bottom <= innerHeight && r.left >= 0 && r.right <= innerWidth;
  if (!seen || (!whole && r.height <= innerHeight && r.width <= innerWidth)) {
    try { el.scrollIntoView({ block: seen ? 'nearest' : 'center', inline: 'nearest', behavior: reduced ? 'auto' : 'smooth' }); } catch (e) {}
  }
  place(peek); schedule();
  return true;
}
// R36: the host swaps the page's style sheets in place with a temporary transition (chat-transition.h) and says so with
// this event: the frames, labels and inspector follow the gliding values frame by frame, without their own transition.
function onRestyle(e) {
  var ms = 0; try { ms = +(e.detail && e.detail.ms) || 0; } catch (x) {}
  trackUntil = performance.now() + Math.max(0, Math.min(2000, ms)) + 120; smooth(false); smUntil = 0; schedule();
}
function teardown() {
  if (!alive) return; alive = false;
  markClear();
  if (cmTimer) { clearInterval(cmTimer); cmTimer = 0; } if (followTimer) { clearInterval(followTimer); followTimer = 0; } cmarks = {}; follow = null; peek = null;
  if (raf) cancelAnimationFrame(raf); raf = 0;
  for (var i = 0; i < listeners.length; i++) { var l = listeners[i]; try { l[0].removeEventListener(l[1], l[2], l[3]); } catch (e) {} }
  listeners = []; cur = null; down = [];
  if (insTimer) { clearTimeout(insTimer); insTimer = 0; }
  for (var a = 0; a < adopted.length; a++) { try { var rn = adopted[a]; rn.adoptedStyleSheets = rn.adoptedStyleSheets.filter(function (x) { return x !== sheet; }); } catch (e) {} }
  adopted = [];
  try { host.remove(); } catch (e) {}
  for (var fi = 0; fi < faces.length; fi++) { try { D.fonts.delete(faces[fi]); } catch (e) {} }
  try { if (window[KEY] && window[KEY].nonce === NONCE) delete window[KEY]; } catch (e) {}
}
function cmd(op, arg) {
  if (!alive) return;
  if (op === 'exit') { teardown(); return; }
  if (op === 'hide') {
    conceal(true);
    requestAnimationFrame(function () { requestAnimationFrame(function () { if (alive) post({ op: 'hidden', serial: arg }); }); });
    return;
  }
  if (op === 'handoff') { conceal(true, true); post({ op: 'hidden', serial: arg }); return; }
  if (op === 'show') {
    conceal(false); syncShields();
    if (arg !== null && arg !== undefined) requestAnimationFrame(function () { requestAnimationFrame(function () { if (alive) post({ op: 'shown', serial: arg }); }); });
    return;
  }
  if (op === 'resume') { conceal(false); frozen = false; ++seq; haveMove = px >= 0; schedule(); if (!haveMove) place(null); return; }
  if (op === 'point') { if (!off) selectAtPoint(arg.x, arg.y); return; }
  if (op === 'accent') {   // { accent: 'r,g,b', svg: cursor } — colours only: no move, no transition, no new selection
    var a = rgbList(arg && arg.accent); if (!a) return;
    A = a;
    if (arg.svg) { buildCursor(arg.svg); if (sheet && !idle()) { try { sheet.replaceSync('*,*::before,*::after{' + cursorDecl(true) + '}'); } catch (e) {} } }
    accentStyle.textContent = accentCss();
    return;
  }
  if (op === 'selection') { if (off) return; if (!selectSelection(arg)) post({ op: 'noselection' }); return; }
  if (op === 'pick') {   // V1.4: the selection on (true) or off (false); the chat's marks stay either way
    var want = !arg;
    if (want === off) return true;
    off = want;
    if (off) {
      frozen = false; cur = null; down = []; haveMove = false; place(null);
      try { if (sheet) sheet.replaceSync(''); } catch (e) {}
      syncShields();
    } else {
      if (!ctrl) cursorSheet(true);
      syncShields(); haveMove = px >= 0; schedule();
    }
    return true;
  }
  if (op === 'mark') return markSet(arg);   // chat: true when the element was found and marked
  if (op === 'cmarks') return cmSet(arg);   // Mode Commentaire: the ids found in this page
  if (op === 'follow') return followSet(arg);
  if (op === 'peek') return peekSet(arg);   // R26: { selector, active }
  if (op === 'ctrl') { hostCtrl = !!arg; setCtrl(hostCtrl); return true; }   // final pass 1, R31: Ctrl from the host's keyboard
}

var opts = { capture: true, passive: true };
on(window, 'pointermove', onMove, opts);
on(window, 'pointerover', onMove, opts);
on(window, 'pointerdown', stop, true);
on(window, 'mousedown', stop, true);
on(window, 'pointerup', stop, true);
on(window, 'mouseup', stop, true);
on(window, 'click', onClick, true);
on(window, 'dblclick', stop, true);
on(window, 'auxclick', stop, true);
on(window, 'dragstart', stop, true);
on(window, 'keydown', onKey, true);
on(window, 'keyup', onKeyUp, true);
on(window, 'blur', onBlur, true);
on(window, 'browser-restyle', onRestyle, true);
on(window, 'focusin', onFocus, true);
on(window, 'scroll', onScroll, opts);
on(window, 'resize', onScroll, { passive: true });
on(D, 'pointerout', onLeave, opts);
Object.defineProperty(window, KEY, { value: { nonce: NONCE, teardown: teardown, cmd: cmd, source: sourceOf, sources: sourcesOf }, configurable: true });
syncShields();
var st = P.start || {};
if (st.kind === 'point') selectAtPoint(st.x, st.y);
else if (st.kind === 'selection') { if (!selectSelection(st.text)) post({ op: 'noselection' }); }
return 'ok';
}))JS";
}

// Builds `(script)(params)`; `paramsJson` is a JSON object built by the host.
// The page script with its embedded label font (R45), built once.
inline const wchar_t* PageScript() {
    static const std::wstring full = [] {
        std::wstring js = PageScriptSource();
        auto put = [&](const wchar_t* mark, const char* b64) { const auto at = js.find(mark); if (at != std::wstring::npos) js.replace(at, wcslen(mark), std::wstring(b64, b64 + strlen(b64))); };
        put(L"@@FONT400@@", LabelFontRegular()); put(L"@@FONT500@@", LabelFontMedium());
        return js;
    }();
    return full.c_str();
}
inline std::wstring PageScriptCall(const std::wstring& paramsJson) { return std::wstring(PageScript()) + L"(" + paramsJson + L")"; }
// A command to the live instance (no-op if the nonce differs, e.g. after a navigation).
inline std::wstring PageCommand(const std::wstring& nonce, const std::wstring& op, const std::wstring& argJson = L"null") {
    return L"(function(){try{var h=window[Symbol.for('browser.annotation')];if(h&&h.nonce==='" + nonce + L"')h.cmd('" + op + L"'," + argJson + L");}catch(e){}return 0;})()";
}
// Chat: a call whose RESULT comes back through ExecuteScript's completion (a JSON string, or "" when the instance is
// gone): `source` -> the selected element's debug source (see sourceOf), `sources` -> the files the page's debug data
// names (see sourcesOf), `mark` -> "true" / "false".
inline std::wstring PageQuery(const std::wstring& nonce, const std::wstring& what, const std::wstring& argJson = L"null") {
    return L"(function(){try{var h=window[Symbol.for('browser.annotation')];if(h&&h.nonce==='" + nonce + L"'){" +
           (what == L"source" ? std::wstring(L"return JSON.stringify(h.source());") : what == L"sources" ? std::wstring(L"return JSON.stringify(h.sources());") :
            what == L"cmarks" ? L"return JSON.stringify(h.cmd('cmarks'," + argJson + L")||[]);" : L"return JSON.stringify(!!h.cmd('" + what + L"'," + argJson + L"));") +
           L"}}catch(e){}return '';})()";
}
}} // namespace NativeChrome::Annotations
