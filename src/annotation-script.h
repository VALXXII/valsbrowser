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
// The page could tamper with all of this (same world): the host validates every message and
// the note itself is typed in a native control the page never sees.
#include <string>

namespace NativeChrome { namespace Annotations {

inline const wchar_t* PageScript() {
    return LR"JS((function (P) {
'use strict';
var KEY = Symbol.for('browser.annotation');
try { var old = window[KEY]; if (old && typeof old.teardown === 'function') old.teardown(); } catch (e) {}
var NONCE = String(P.nonce), STAMP = P.navStamp, D = document;
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

var host = D.createElement('browser-annotation-layer');
host.setAttribute('aria-hidden', 'true');
host.style.cssText = 'all:initial;position:fixed;left:0;top:0;width:0;height:0;z-index:2147483647;pointer-events:none;';
var root = host.attachShadow({ mode: 'closed' });
var T = 'transform 120ms cubic-bezier(0.2,0,0,1),opacity 120ms ease-out';
// The accent rules (frame fill 8 %, edges, label, padding hatches and their labels), rebuilt by the « accent » command.
function accentCss() {
  return '.f{width:100px;height:100px;background:rgba(' + A + ',0.08)}' +
    '.h,.v{background:rgb(' + A + ')}' +
    '.l{background:rgb(' + A + ')}' +
    '.pb{background:repeating-linear-gradient(135deg,rgba(' + A + ',.4) 0 1px,transparent 1px 5px),rgba(' + A + ',.08)}' +
    '.pl{background:rgb(' + A + ')}' +
    '.s{' + cursorDecl(false) + '}';   // the iframe shields carry the cursor (its dot is the accent)
}
root.innerHTML = '<style>' +
  '.x{position:fixed;left:0;top:0;transform-origin:0 0;opacity:0;pointer-events:none;will-change:transform,opacity}' +
  '.on{transition:' + (reduced ? 'opacity 120ms ease-out' : T) + '}' +
  '.mv{transition:opacity 120ms ease-out}' +
  '.h{width:100px;height:2px}.v{width:2px;height:100px}' +
  '.l{font:500 11px/16px "JetBrains Mono","Cascadia Mono",Consolas,monospace;color:#fff;' +
  'padding:2px 6px;border-radius:4px;white-space:nowrap;max-width:60vw;overflow:hidden;text-overflow:ellipsis;letter-spacing:0}' +
  '.s{position:fixed;left:0;top:0;width:0;height:0;pointer-events:auto;background:transparent}' +
  '.i,.j{position:fixed;left:0;top:0;width:0;height:0;opacity:0;pointer-events:none}.i.on,.j.on{transition:opacity 120ms ease-out}' +
  '.pb,.gb,.pl,.gl{position:fixed;left:0;top:0;box-sizing:border-box}' +
  '.gb{background:repeating-linear-gradient(45deg,rgba(224,71,158,.45) 0 1px,transparent 1px 5px),rgba(224,71,158,.12)}' +
  '.pl,.gl{height:14px;padding:0 4px;border-radius:3px;font:400 10px/14px "JetBrains Mono","Cascadia Mono",Consolas,monospace;color:#fff;white-space:nowrap;letter-spacing:0}' +
  '.gl{background:#D63E92}' +
  '</style><style class="a"></style><div class="x f"></div><div class="i"><div class="pb"></div><div class="gb"></div></div><div class="x h"></div><div class="x h"></div><div class="x v"></div><div class="x v"></div><div class="x l"></div><div class="j"></div>';
var accentStyle = root.querySelector('style.a'); accentStyle.textContent = accentCss();
var parts = root.querySelectorAll('.x'), shields = [];
var fill = parts[0], eT = parts[1], eB = parts[2], eL = parts[3], eR = parts[4], label = parts[5];
var ins = root.querySelector('.i'), insL = root.querySelector('.j'), padEl = root.querySelector('.pb'), gapEl = root.querySelector('.gb');
var insPool = { pl: [], gl: [] }, insData = null, insEl = null, insTimer = 0, insShown = false, labelBox = null;
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
  var m = describe(info, selText);
  if (JSON.stringify(m).length > MAX_MSG) { m.target.text = m.target.text.slice(0, 400); if (m.target.selectionText) m.target.selectionText = m.target.selectionText.slice(0, 400); }
  if (JSON.stringify(m).length > MAX_MSG) { m.target.styles = {}; m.target.path = m.target.path.slice(-4); m.target.selector = m.target.selector.slice(-200); }
  if (JSON.stringify(m).length > MAX_MSG) return;
  // V3: nothing changes on screen at the click; the host takes the edges and label over (« handoff »).
  var mine = ++seq; m.seq = mine; settle(); m.mark = geometry(info);
  // When the user acted (event time on the system clock): the host measures click -> image ready.
  m.at = ev && ev.timeStamp > 0 && performance.timeOrigin ? performance.timeOrigin + ev.timeStamp : Date.now();
  post(m);
}
// marks: the edges and the label only (the fill stays: it is removed from the image by the host).
function conceal(on, marksOnly) { for (var i = marksOnly ? 1 : 0; i < parts.length; i++) parts[i].style.visibility = on ? 'hidden' : ''; ins.style.visibility = insL.style.visibility = on ? 'hidden' : ''; }
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
  return { x: r.x, y: r.y, w: Math.max(r.w, 1), h: Math.max(r.h, 1), lx: lb.left, ly: lb.top, lw: lb.width, lh: lb.height,
           text: String(label.textContent || '').slice(0, 200), fill: 0.08 * (parseFloat(getComputedStyle(fill).opacity) || 0), sp: spaceMark() };
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
  var obstacles = [labelBox];
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
function placeSpace(info, first) {
  var el = info ? info.el : null;
  insData = info ? spaceGeometry(info) : null;
  drawSpace(insData);
  if (!insData) { if (insTimer) { clearTimeout(insTimer); insTimer = 0; } insFade(false, false); insEl = el; return; }
  if (el === insEl) return;
  insEl = el; if (insTimer) { clearTimeout(insTimer); insTimer = 0; }
  if (first) { insFade(true, true); return; }   // first appearance: with the frame's own fade
  insFade(false, false);
  insTimer = setTimeout(function () { insTimer = 0; if (alive && insData && insEl === el) insFade(true, true); }, reduced ? 0 : 120);
}

// ---- drawing
function place(info) {
  if (!info) { if (shown) { setOpacity(0); shown = false; } placeSpace(null, false); return; }
  var r = rectOf(info);
  var first = !shown;
  if (first) moveOnly(true);
  var x = r.x, y = r.y, w = Math.max(r.w, 1), h = Math.max(r.h, 1);
  // 100 px bases: a 1 px box would be snapped to one device pixel (DPR 1.25 -> scale 20 % short).
  tr(fill, x, y, w / 100, h / 100); tr(eT, x, y, w / 100, 1); tr(eB, x, y + h - 2, w / 100, 1); tr(eL, x, y, 1, h / 100); tr(eR, x + w - 2, y, 1, h / 100);
  var tag = seg(info.el === info.frame && info.kind === 'cross-origin' ? info.frame : info.el);
  label.textContent = (info.range ? 'texte' : tag) + ' \u00b7 ' + Math.round(r.w) + '\u00d7' + Math.round(r.h);
  var ly = y >= 22 ? y - 22 : Math.min(y + h + 4, innerHeight - 20);
  var lx = Math.max(0, Math.min(x, innerWidth - 120));
  tr(label, lx, ly);
  labelBox = { x: lx - 2, y: ly - 2, w: (label.offsetWidth || 120) + 4, h: (label.offsetHeight || 20) + 4 };   // + 2 px of air
  if (first) { void getComputedStyle(fill).transform; moveOnly(false); setOpacity(1); shown = true; }
  placeSpace(info, first);
}
// Shields over the visible top-level iframes (same and cross origin): their events reach our window
// listeners, the hit-test (elementsFromPoint without our host) still finds the iframe below.
function syncShields() {
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
  if (frozen) { if (cur) place(cur); return; }
  if (!haveMove) return;
  haveMove = false;
  var info = hit(px, py);
  if (!same(info, cur)) down = [];
  cur = info; place(cur);
}
function schedule() { if (!raf && alive) raf = requestAnimationFrame(frame); }

// ---- events
function onMove(e) { px = e.clientX; py = e.clientY; haveMove = true; keyboard = false; schedule(); }
// Only the user's own input is consumed: the page's synthetic events (el.click(), dispatchEvent) pass.
function stop(e) { if (!e.isTrusted) return; e.preventDefault(); e.stopImmediatePropagation(); }
function onClick(e) {
  if (!e.isTrusted) return;
  stop(e);
  if (e.button !== 0) return;
  px = e.clientX; py = e.clientY; keyboard = false;
  frozen = false;
  var info = hit(px, py);
  if (!info) return;
  if (e.altKey) { var p = parentInfo(info); if (p) info = p; }
  cur = info; down = []; frozen = true; place(cur); sendSelect(cur, null, e);
}
function onKey(e) {
  if (!e.isTrusted || e.ctrlKey || e.metaKey) return;
  var k = e.key;
  if (k === 'Escape') { stop(e); post({ op: 'escape' }); return; }
  if (frozen) return;
  if (k === 'ArrowUp' && cur && !e.altKey) { stop(e); var p = parentInfo(cur); if (p) { down.push(cur); cur = p; keyboard = true; place(cur); } return; }
  if (k === 'ArrowDown' && cur && !e.altKey) { stop(e); var c = down.length ? down.pop() : childInfo(cur); if (c) { cur = c; keyboard = true; place(cur); } return; }
  if (k === 'Enter' && cur) { stop(e); keyboard = true; frozen = true; place(cur); sendSelect(cur, null, e); return; }
}
function onFocus(e) {
  if (frozen) return;
  var t = e.composedPath ? e.composedPath()[0] : e.target;
  if (!t || t.nodeType !== 1 || t === host) return;
  var r = t.getBoundingClientRect();
  cur = { el: t, kind: 'main', ox: 0, oy: 0, shadow: [], frames: [], frame: null, range: null };
  if (t.getRootNode && t.getRootNode().host) { var info = hit(r.left + 1, r.top + 1); if (info && info.el === t) cur = info; }
  down = []; keyboard = true; place(cur);
}
function onScroll() { if (!cur) return; if (!frozen && px >= 0) haveMove = true; schedule(); }
function onLeave(e) { if (!e.relatedTarget && !frozen) { haveMove = false; cur = null; place(null); } }

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

function teardown() {
  if (!alive) return; alive = false;
  if (raf) cancelAnimationFrame(raf); raf = 0;
  for (var i = 0; i < listeners.length; i++) { var l = listeners[i]; try { l[0].removeEventListener(l[1], l[2], l[3]); } catch (e) {} }
  listeners = []; cur = null; down = [];
  if (insTimer) { clearTimeout(insTimer); insTimer = 0; }
  for (var a = 0; a < adopted.length; a++) { try { var rn = adopted[a]; rn.adoptedStyleSheets = rn.adoptedStyleSheets.filter(function (x) { return x !== sheet; }); } catch (e) {} }
  adopted = [];
  try { host.remove(); } catch (e) {}
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
  if (op === 'point') { selectAtPoint(arg.x, arg.y); return; }
  if (op === 'accent') {   // { accent: 'r,g,b', svg: cursor } — colours only: no move, no transition, no new selection
    var a = rgbList(arg && arg.accent); if (!a) return;
    A = a;
    if (arg.svg) { buildCursor(arg.svg); if (sheet) { try { sheet.replaceSync('*,*::before,*::after{' + cursorDecl(true) + '}'); } catch (e) {} } }
    accentStyle.textContent = accentCss();
    return;
  }
  if (op === 'selection') { if (!selectSelection(arg)) post({ op: 'noselection' }); return; }
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
on(window, 'focusin', onFocus, true);
on(window, 'scroll', onScroll, opts);
on(window, 'resize', onScroll, { passive: true });
on(D, 'pointerout', onLeave, opts);
Object.defineProperty(window, KEY, { value: { nonce: NONCE, teardown: teardown, cmd: cmd }, configurable: true });
syncShields();
var st = P.start || {};
if (st.kind === 'point') selectAtPoint(st.x, st.y);
else if (st.kind === 'selection') { if (!selectSelection(st.text)) post({ op: 'noselection' }); }
return 'ok';
}))JS";
}

// Builds `(script)(params)`; `paramsJson` is a JSON object built by the host.
inline std::wstring PageScriptCall(const std::wstring& paramsJson) { return std::wstring(PageScript()) + L"(" + paramsJson + L")"; }
// A command to the live instance (no-op if the nonce differs, e.g. after a navigation).
inline std::wstring PageCommand(const std::wstring& nonce, const std::wstring& op, const std::wstring& argJson = L"null") {
    return L"(function(){try{var h=window[Symbol.for('browser.annotation')];if(h&&h.nonce==='" + nonce + L"')h.cmd('" + op + L"'," + argJson + L");}catch(e){}return 0;})()";
}
}} // namespace NativeChrome::Annotations
