// Chat panel (internal page). Talks only to the host through chrome.webview messages; protocol v1
// is specified in design/chat-panel.md. No network, no storage, no eval, and nothing received is
// ever parsed as HTML: text goes through textContent and markdown.js.
"use strict";
(function () {
  const V = 1;
  const BUILD = "chat-panel-1";
  const ID = /^[A-Za-z0-9._:-]{1,128}$/;
  const IMG = /^data:image\/(png|jpeg|webp|gif);base64,[A-Za-z0-9+/=]+$/;
  const KINDS = ["button", "link", "text", "heading", "image", "video", "icon", "input", "select", "checkbox", "form", "list", "table", "block"];
  const reduceMQ = window.matchMedia("(prefers-reduced-motion: reduce)");

  // ---------------------------------------------------------------- tiny DOM helpers
  function h(tag, props, ...kids) {
    const n = typeof tag === "string" ? document.createElement(tag) : tag;
    if (props) for (const k in props) {
      const v = props[k];
      if (v == null || v === false) continue;
      if (k === "class") n.className = v;
      else if (k === "text") n.textContent = v;
      else if (k === "title") continue;   // B 1.9 (audit D7): never a system tooltip — Paper draws its own bubbles; names live in aria-label
      else if (k.startsWith("on")) n.addEventListener(k.slice(2), v);
      else if (k === "style") n.style.cssText = v;
      else n.setAttribute(k, v === true ? "" : v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) n.appendChild(typeof c === "string" ? document.createTextNode(c) : c);
    return n;
  }
  const SVGNS = "http://www.w3.org/2000/svg";
  function icon(name, cls) {
    const def = window.ChatIcons[name] || window.ChatIcons["k-block"];
    const s = document.createElementNS(SVGNS, "svg");
    s.setAttribute("viewBox", def[0]);
    s.setAttribute("class", "i" + (cls ? " " + cls : ""));
    s.setAttribute("aria-hidden", "true");
    s.setAttribute("focusable", "false");
    s.setAttribute("fill", "currentColor");
    s.innerHTML = def[1]; // constant strings from icons.js only
    return s;
  }
  const agentIcon = (a, cls) => icon(a && (a.icon === "codex" || a.id === "codex") ? "codex" : a && (a.icon === "claude" || /claude/.test(a.id || "")) ? "claude" : "k-icon", cls);
  const clear = (n) => { while (n.firstChild) n.firstChild.remove(); return n; };
  const plural = (n, one, many) => n + " " + (n > 1 ? many : one);
  function dur(ms) {
    const s = Math.max(0, Math.round(ms / 1000));
    if (s < 60) return s + " s";
    const m = Math.floor(s / 60), r = s % 60;
    return r ? m + " min " + r + " s" : m + " min";
  }
  const isReduced = () => reduceMQ.matches || document.documentElement.classList.contains("reduce-motion");

  // ---------------------------------------------------------------- bridge
  const wv = window.chrome && window.chrome.webview;
  function post(type, fields) {
    const m = Object.assign({ v: V, type }, fields || {});
    try { if (wv) wv.postMessage(m); } catch (e) { /* host gone */ }
  }
  function warn(message) { post("panel.log", { level: "warn", message: String(message).slice(0, 2000) }); }

  // ---------------------------------------------------------------- state
  const S = {
    st: { site: null, folder: undefined, folderSuggestion: null, agents: [], agentId: null, model: null, efforts: [], effort: null,
          modes: [], mode: null, conversation: null, running: false, pendingAnnotations: 0, suggestions: [], capabilities: {}, disabledReason: null },
    users: new Map(), turns: new Map(), tools: new Map(),
    lastUser: null, lastTurn: null, lastAt: 0,
    batch: null, batches: new Map(),
    perms: [], queue: [], strips: new Map(), history: null, historyOpen: false, commands: [],
    picking: false, menu: null, atBottom: true, newCount: 0,
    cEls: new Map(), cAtt: new Map(), editing: null, replay: false, fatal: false,
    planPending: null,
    // B 1.8 — comment mode: the page as a compact bubble next to the element (Paper 18NS-0 … 19IO-0)
    sub: null,
    lay: { mode: "panel", maxW: 320, maxH: 520, side: "right", element: "" },
    bview: null, berr: null,
    // B 1.9 (R3): margins of transparent window around the surface — asked (`bubble.size` overflow*) and granted (`layout.inset`)
    inset: { top: 0, right: 0, bottom: 0, left: 0 }, ovAsk: { top: 0, right: 0, bottom: 0, left: 0 },
  };
  const BUB = () => S.lay.mode === "bubble";
  const folderName = (f) => (f && ((typeof f.name === "string" && f.name) || String(f.path || "").split(/[\\/]/).filter(Boolean).pop())) || "";
  const agentById = (id) => (S.st.agents || []).find((a) => a.id === id) || null;
  const curAgent = () => agentById(S.st.agentId) || (S.st.agents || [])[0] || null;
  const curAgentName = () => (curAgent() && curAgent().name) || "l'agent";
  function curModel() {
    const a = curAgent();
    return a && a.models ? a.models.find((m) => m.id === S.st.model) || null : null;
  }
  // R13 (fix 0.3.690): before an agent has ever said its models here (new profile, no saved lists, adapter still being
  // prepared) the button still names a model, never the tool. The models the pinned adapter announces, with its default
  // (claude-agent-acp 0.107.3: « Default (recommended) » = Opus 5.5); the agent's live list replaces it as soon as it comes.
  // Codex builds its list from the Codex CLI at run time: no fixed name to promise, a neutral word instead.
  const FALLBACK_MODEL = { claude: { label: "Opus 5.5", effort: "Moyen" }, codex: { label: "Modèle par défaut", effort: "Moyen" } };
  const fallbackModel = (a) => (a && (FALLBACK_MODEL[a.id] || FALLBACK_MODEL[a.icon])) || { label: "Modèle par défaut", effort: "" };
  // The model the button and a new turn name: the chosen one, else the agent's default, else its first, else the fallback.
  function shownModelLabel(a) {
    const m = curModel() && a === curAgent() ? curModel() : null;
    const x = m || (a && a.models && (a.models.find((y) => y.isDefault) || a.models[0])) || null;
    return x ? x.label : "";
  }
  // Efforts (V1.2, Paper 17VU-0): the levels the agent announces, named in French; its « Default » is not a level.
  const EFFORT_FR = { minimal: "Minimal", low: "Faible", medium: "Moyen", high: "Élevé", xhigh: "X-Élevé", "x-high": "X-Élevé", "extra high": "X-Élevé", max: "Max", maximum: "Max", ultra: "Ultra", none: "Aucun" };
  const isDefaultChoice = (o) => !!o && (/^default$/i.test(String(o.id || "")) || /^(default|par défaut)\b/i.test(String(o.label || "")));
  const effortName = (id, label) => EFFORT_FR[String(id || "").toLowerCase()] || EFFORT_FR[String(label || "").toLowerCase()] || label || id || "";
  const turnEffort = (label) => (!label || /^(default|par défaut)\b/i.test(label) ? "" : effortName(label, label));
  const visibleEfforts = () => (S.st.efforts || []).filter((e) => !isDefaultChoice(e));
  const curEffort = () => visibleEfforts().find((e) => e.id === S.st.effort) || null;
  // Model menu (#128, Paper « #128 Menu des modèles · A liste groupée (validée) »): every provider the host lists, in its
  // order, connected or not — a provider not ready to answer keeps its group, its models greyed out (`CONNECTED` = usable).
  const CONNECTED = ["ready", "starting", "preparing"];
  // Modes (B 1.9, Paper 14RK-0 as updated by the user): exactly these four rows, in this order, with these words. A mode
  // the agent does not have is not shown; anything else (the adapter's « bypass every permission », unknown modes) never is.
  // `kind` comes from the host; before 1.9 acceptEdits came as « auto » — its id still tells it apart.
  const MODE_ROWS = [
    { kind: "edits", label: "Accept edits", desc: () => "Automatically accept all file edits", icon: "chevronsUp" },
    { kind: "auto", label: "Auto", desc: () => curAgentName().replace(/ Code$/, "") + " handles permission decisions", icon: "bolt" },
    { kind: "ask", label: "Demander", desc: () => "Propose chaque modification.", icon: "shield" },
    { kind: "plan", label: "Planifier", desc: () => "Propose un plan, ne touche à aucun fichier.", icon: "list" },
  ];
  const modeKind = (m) => (m.kind === "edits" || (m.kind === "auto" && /accept|edit|workspace/i.test(String(m.id))) ? "edits"
    : ["auto", "ask", "plan"].includes(m.kind) ? m.kind : "other");
  function shownModes() {
    const all = (S.st.modes || []).filter((m) => m && ID.test(String(m.id)));
    const out = [];
    for (const row of MODE_ROWS) { const m = all.find((x) => modeKind(x) === row.kind); if (m) out.push({ id: m.id, kind: row.kind, label: row.label, desc: row.desc(), icon: row.icon }); }
    return out;
  }
  const curMode = () => shownModes().find((m) => m.id === S.st.mode) || null;

  // ---------------------------------------------------------------- skeleton
  const app = document.getElementById("app");
  const live = h("div", { class: "sr-only", role: "status", "aria-live": "polite" });
  const alertLive = h("div", { class: "sr-only", role: "alert", "aria-live": "assertive" });

  // header
  const hdLogo = h("span", { class: "hd-logo" });
  const hdSource = h("button", { class: "hd-source", type: "button", hidden: true, onclick: () => S.st.conversation && S.st.conversation.source && post("file.open", srcRef(S.st.conversation.source)) });
  const hdTitleText = h("span", { class: "hd-title-text", text: "Nouveau chat" });
  // B 1.9 (R11): the title no longer opens the history (its own button sits right next to it). A double-click (or F2)
  // renames the conversation in place: Entrée keeps the name, Échap or leaving the field puts the old one back.
  const hdTitle = h("span", { class: "hd-title", tabindex: "-1", role: "heading", "aria-level": "1",
                              "aria-keyshortcuts": "F2", ondblclick: () => startRename(),
                              onkeydown: (e) => { if (e.key === "F2" || (e.key === "Enter" && !e.repeat)) { e.preventDefault(); startRename(); } } }, hdTitleText);
  let renaming = null;
  function startRename() {
    const c = S.st.conversation;
    if (renaming || !c || !ID.test(String(c.id || ""))) return;
    const before = hdTitleText.textContent;
    const input = h("input", { class: "hd-rename", type: "text", maxlength: "120", "aria-label": "Nom de la conversation", spellcheck: "false" });
    input.value = before;
    renaming = { input, before };
    hdTitle.replaceWith(input);
    input.focus(); input.select();
    const done = (keep) => {
      if (!renaming || renaming.input !== input) return;
      renaming = null;
      const title = input.value.replace(/\s+/g, " ").trim().slice(0, 120);
      if (keep && title && title !== before) { post("conversation.rename", { id: c.id, title }); hdTitleText.textContent = title; if (S.st.conversation) S.st.conversation.title = title; say("Conversation renommée : " + title + "."); }
      input.replaceWith(hdTitle);
      renderHeader();
      hdTitle.focus();
    };
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.isComposing) { e.preventDefault(); done(true); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); done(false); }
    });
    input.addEventListener("blur", () => done(false));
  }
  // Context gauge (B 1.2 `usage`): how much of the agent's context window this chat uses before it gets summarised.
  const gaugeArc = document.createElementNS(SVGNS, "circle");
  const gaugeSvg = document.createElementNS(SVGNS, "svg");
  gaugeSvg.setAttribute("viewBox", "0 0 16 16"); gaugeSvg.setAttribute("class", "i"); gaugeSvg.setAttribute("aria-hidden", "true");
  for (const [c, track] of [[document.createElementNS(SVGNS, "circle"), true], [gaugeArc, false]]) {
    c.setAttribute("cx", "8"); c.setAttribute("cy", "8"); c.setAttribute("r", "6"); c.setAttribute("fill", "none"); c.setAttribute("stroke-width", "2");
    c.setAttribute("class", track ? "track" : "arc");
    gaugeSvg.appendChild(c);
  }
  const gaugeTip = h("span", { class: "tip", "aria-hidden": "true" });
  const gauge = h("span", { class: "gauge", tabindex: "0", role: "img", hidden: true }, gaugeSvg, gaugeTip);
  const GAUGE_C = 2 * Math.PI * 6;
  // B 1.5: the bubble shows 300 ms after the pointer enters and goes as soon as it leaves the gauge. The WebView does not
  // always get a leave when the pointer goes straight out of the panel (page, band, another window): the panel also
  // listens to the document's own leave and the window's blur, and tells the host a bubble is up (`hover`); the host
  // then watches the real cursor and answers `pointer.leave` once it is outside the panel.
  // Several anchors share it (the gauge, the send button): the first bubble waits 300 ms; while one is up, the next one
  // shows at once (emil-design-eng: tooltips skip the delay once one is open).
  let tipTimer = 0, tipHost = false, tipOn = null, tipAt = 0;
  function tipShow(el) {
    clearTimeout(tipTimer);
    const instant = !!tipOn || Date.now() - tipAt < 400;
    if (tipOn && tipOn !== el) tipOn.classList.remove("tip-on");
    if (tipOn && tipOn !== el && tipOn._tip) tipOn._tip(false);
    tipTimer = setTimeout(() => { tipTimer = 0; el.classList.add("tip-on"); tipOn = el; if (el._tip) el._tip(true); }, instant || isReduced() ? 0 : 300);
    if (!tipHost) { tipHost = true; post("hover", { active: true }); }
  }
  function tipHide() { clearTimeout(tipTimer); tipTimer = 0; if (tipOn) { tipOn.classList.remove("tip-on"); if (tipOn._tip) tipOn._tip(false); tipAt = Date.now(); } tipOn = null; if (tipHost) { tipHost = false; post("hover", { active: false }); } }
  function withTip(el) {
    el.addEventListener("pointerenter", (e) => { if (e.pointerType !== "touch") tipShow(el); });
    el.addEventListener("pointerleave", tipHide);
    el.addEventListener("pointerdown", tipHide);   // pressing it: the bubble goes, the action speaks
  }
  withTip(gauge);
  document.documentElement.addEventListener("pointerleave", tipHide);
  document.addEventListener("mouseout", (e) => { if (!e.relatedTarget) tipHide(); });
  window.addEventListener("blur", tipHide);
  const histBtn = h("button", { class: "hd-btn", type: "button", "aria-label": "Historique", "aria-haspopup": "dialog", "aria-expanded": "false", "aria-controls": "history",
                               onclick: () => toggleHistory() }, icon("history"));
  const hd = h("header", { class: "hd" },
    h("div", { class: "hd-main" }, hdLogo, h("div", { class: "hd-titles" }, hdSource, h("div", { class: "hd-row" }, hdTitle, gauge))),
    h("button", { class: "hd-btn", type: "button", "aria-label": "Nouveau chat", onclick: () => post("newChat") }, icon("plus")),
    histBtn,
    h("button", { class: "hd-btn", type: "button", "aria-label": "Fermer le chat", onclick: () => post("close") }, icon("close")));
  hdSource.style.cssText = "background:none;border:0;padding:0;text-align:left;font:inherit;font-size:12px;line-height:16px;color:#FFFFFF80;align-self:flex-start;max-width:100%";
  function srcRef(s) { const m = /^(.*?)(?::(\d+))?$/.exec(s); return m[2] ? { path: m[1], line: +m[2] } : { path: s }; }

  // body
  const log = h("div", { class: "log", role: "log", "aria-label": "Conversation", "aria-live": "off", id: "log" });
  const first = h("div", { class: "first", hidden: true });
  const empty = h("div", { class: "empty", hidden: true });
  const scroller = h("div", { class: "scroller", tabindex: "-1" }, first, empty, log);
  const newSteps = h("button", { class: "newsteps", type: "button", hidden: true, onclick: () => scrollToEnd(true) }, icon("arrowDown"), h("span"));
  const historyEl = h("div", { class: "history", id: "history", role: "dialog", "aria-label": "Historique des conversations", hidden: true });
  const dock = h("div", { class: "dock" });
  const body = h("div", { class: "body" }, scroller, newSteps);
  const changesEl = h("div", { class: "changes", id: "changes", role: "dialog", "aria-modal": "false", "aria-label": "Changements", hidden: true });
  // Bubble only (B 1.8): the diff « en grand », the hairline above the input, the fade over a scrolled thread, the steps' bubble
  const bviewEl = h("div", { class: "bview", id: "bview", role: "dialog", "aria-modal": "false", hidden: true });
  // cv5 R40: the resize handle of the diff « en grand » — two short oblique strokes, white 30 %, 10 px, bottom right
  const gripSvg = document.createElementNS(SVGNS, "svg");
  gripSvg.setAttribute("viewBox", "0 0 10 10"); gripSvg.setAttribute("aria-hidden", "true"); gripSvg.setAttribute("class", "i");
  for (const d of ["M9 3 L3 9", "M9 6.5 L6.5 9"]) { const pth = document.createElementNS(SVGNS, "path"); pth.setAttribute("d", d); gripSvg.appendChild(pth); }
  const gripEl = h("div", { class: "b-grip", role: "separator", "aria-orientation": "horizontal", "aria-label": "Redimensionner l'aperçu", tabindex: "-1", hidden: true }, gripSvg);
  const ruleEl = h("div", { class: "b-rule", "aria-hidden": "true", hidden: true });
  const fadeEl = h("div", { class: "b-fade", "aria-hidden": "true" });
  const btipEl = h("div", { class: "b-tip", id: "b-tip", role: "tooltip", hidden: true });
  const main = h("div", { class: "main" }, body, bviewEl, ruleEl, dock, historyEl, changesEl);
  // Menus and hover bubbles live in their own layer, outside the surface: in the bubble they may leave it (B 1.9, R3) —
  // the host grows the transparent window around the surface for them. The layer has the surface's origin.
  const menuLayer = h("div", { class: "layer" }, btipEl);
  app.append(hd, main, gripEl, live, alertLive);
  // R47: the fade covers the top of the thread only (it lives in the thread's box), never the permission card nor its buttons
  body.appendChild(fadeEl);
  let grip = null, gripFrame = 0;
  function sizeBounds() {
    const base = S.viewBase || { w: S.lay.maxW, h: S.pendH || 224 };
    const min = { w: S.lay.rMin ? Math.max(S.lay.rMin.w, base.w) : base.w, h: Math.max(S.lay.rMin ? S.lay.rMin.h : 0, base.h) };   // the host says 320
    const max = S.lay.rMax || { w: S.lay.maxW, h: S.lay.maxH };
    return { min, max: { w: Math.max(min.w, max.w), h: Math.max(min.h, max.h) } };
  }
  gripEl.addEventListener("pointerdown", (e) => {
    if (!BUB() || !S.bview || e.button !== 0) return;
    e.preventDefault();
    try { gripEl.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer (QA) has nothing to capture */ }
    const cur = S.bsize || { w: curW(), h: S.pendH || Math.ceil(bubbleNatural()) };
    grip = { id: e.pointerId, sx: e.screenX, sy: e.screenY, w0: cur.w, h0: cur.h };
    app.classList.add("resizing");
  });
  gripEl.addEventListener("pointermove", (e) => {
    if (!grip || e.pointerId !== grip.id) return;
    const b = sizeBounds();
    const w = Math.round(Math.max(b.min.w, Math.min(b.max.w, grip.w0 + e.screenX - grip.sx)));
    const hh = Math.round(Math.max(b.min.h, Math.min(b.max.h, grip.h0 + e.screenY - grip.sy)));
    S.bsize = { w, h: hh };
    if (!gripFrame) gripFrame = requestAnimationFrame(() => { gripFrame = 0; publishSize(); });   // one bubble.size per image
  });
  const gripEnd = (e) => { if (!grip || e.pointerId !== grip.id) return; grip = null; app.classList.remove("resizing"); cancelAnimationFrame(gripFrame); gripFrame = 0; publishSize(); };
  gripEl.addEventListener("pointerup", gripEnd);
  gripEl.addEventListener("pointercancel", gripEnd);
  document.body.appendChild(menuLayer);

  scroller.addEventListener("scroll", () => {
    const at = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 24;
    S.atBottom = at;
    if (at && S.newCount) { S.newCount = 0; newSteps.hidden = true; }
    bFade();
  }, { passive: true });
  // Item heights settle after layout (content-visibility, fonts, code blocks): stay glued to the end.
  if (window.ResizeObserver) {
    new ResizeObserver(() => { if (S.atBottom) scroller.scrollTop = scroller.scrollHeight; publishSize(); }).observe(log);
    new ResizeObserver(() => { if (BUB()) { if (S.atBottom) scroller.scrollTop = scroller.scrollHeight; bFade(); } }).observe(scroller);
    new ResizeObserver(() => publishSize()).observe(dock);
  }
  let scrollQueued = false;
  function stick() {
    if (scrollQueued) return;
    scrollQueued = true;
    requestAnimationFrame(() => { scrollQueued = false; if (S.atBottom) scroller.scrollTop = scroller.scrollHeight; });
  }
  function noteNew(n) {
    if (S.replay || S.atBottom) return;
    S.newCount += n;
    newSteps.lastChild.textContent = S.newCount > 1 ? S.newCount + " nouvelles étapes" : "1 nouvelle étape";
    newSteps.hidden = false;
  }
  function scrollToEnd(smooth) {
    S.atBottom = true; S.newCount = 0; newSteps.hidden = true;
    scroller.scrollTo({ top: scroller.scrollHeight, behavior: smooth && !isReduced() ? "smooth" : "auto" });
  }

  // ---------------------------------------------------------------- announcements
  let politeTimer = 0, politeBuf = "";
  function say(text) {
    if (S.replay || !text) return;
    politeBuf = (politeBuf + " " + text).trim();
    if (politeTimer) return;
    politeTimer = setTimeout(() => { live.textContent = ""; requestAnimationFrame(() => { live.textContent = politeBuf; politeBuf = ""; politeTimer = 0; }); }, 250);
  }
  function shout(text) { if (S.replay) return; alertLive.textContent = ""; requestAnimationFrame(() => { alertLive.textContent = text; }); }
  const plain = (md) => md.replace(/```[\s\S]*?(```|$)/g, " (bloc de code) ").replace(/[*_`#>|~]/g, "").replace(/\[([^\]]*)\]\([^)]*\)/g, "$1").replace(/\s+/g, " ").trim();

  // ---------------------------------------------------------------- composer
  const cin = h("div", { class: "cin", contenteditable: "true", role: "textbox", "aria-multiline": "true", "aria-label": "Message à l'agent",
                         spellcheck: "true", "data-placeholder": "Que faut-il changer ?", id: "composer-input" });
  const thumbs = h("div", { class: "thumbs", hidden: true });
  // B 1.7: the switch of the page's selection (« viseur » cursor, hover, a click picks an element). Pressed = on. Off:
  // the page is the page again, the chat stays as it is. Ctrl+Maj+A does the same from anywhere while the chat is open.
  const pickBtn = h("button", { class: "tb pick", type: "button", "aria-label": "Sélection d'éléments sur la page",
                                "aria-pressed": "false", "aria-keyshortcuts": "Control+Shift+A",
                                onclick: () => { S.picking = !S.picking; renderComposerBar(); post("pickElement", { active: S.picking }); } }, icon("cursor", "ic"));
  const plusBtn = h("button", { class: "tb", type: "button", "aria-label": "Joindre", "aria-haspopup": "menu", "aria-expanded": "false",
                                onclick: (e) => openPlusMenu(e.currentTarget) }, icon("plus", "ic"));
  const modeBtn = h("button", { class: "tb mode", type: "button", "aria-haspopup": "menu", "aria-expanded": "false", onclick: (e) => openModeMenu(e.currentTarget) });
  const modeTip = h("span", { class: "tip ftip mtip", role: "tooltip", id: "mode-tip", "aria-hidden": "true" });
  menuLayer.appendChild(modeTip);
  const batchEl = h("div", { class: "batch", hidden: true, role: "group" });
  const modelName = h("span", { class: "mn" });
  const modelLogo = h("span", { style: "display:flex" });
  const effortTxt = h("span");
  const effortEl = h("span", { class: "e" }, effortTxt);
  // The whole « modèle · effort » chip is one trigger for one menu (efforts live in its « Effort › » sub-view).
  const modelBtn = h("button", { class: "m", type: "button", "aria-haspopup": "menu", "aria-expanded": "false", "aria-keyshortcuts": "Control+/",
                                 onclick: (e) => openModelMenu(e.currentTarget) }, h("span", { class: "mw" }, modelLogo, modelName), effortEl, icon("chevron", "mcv"));
  const sendBtn = h("button", { class: "send", type: "button", "aria-keyshortcuts": "Enter Control+Enter", "aria-describedby": "send-tip", onclick: () => onSendButton() });
  const sendTip = h("span", { class: "tip", id: "send-tip", role: "tooltip" },
    h("span", { class: "tr" }, h("span", { text: "Demander à l'agent" }), h("span", { class: "kc" }, "↵")),
    h("span", { class: "tr" }, h("span", { text: "Garder en annotation" }), h("span", { class: "kcs" }, h("span", { class: "kc" }, "Ctrl"), h("span", { class: "kc" }, "↵"))));
  const sendWrap = h("span", { class: "sendw" }, sendBtn, sendTip);
  withTip(sendWrap);
  // Bubble (B 1.9, R3): the send button's bubble lives in the layer and may leave the surface — above the button, right
  // edges aligned (never cut: « Garder en annotation Ctrl ↵ » was).
  function sendTipFloat(on) {
    if (!BUB()) return;
    if (!on || sendWrap.classList.contains("no-tip")) { sendTip.classList.remove("on"); floatDrop(sendTip); return; }
    sendTip.classList.add("on");
    floatSet(sendTip, (lim, H) => {
      const ar = app.getBoundingClientRect(), r = sendBtn.getBoundingClientRect(), W = S.lay.maxW;
      const tw = sendTip.offsetWidth, th = sendTip.offsetHeight;
      const x = Math.max(-lim.left, Math.min(r.right - ar.left - tw, W + lim.right - tw));
      let y = r.top - ar.top - 6 - th;
      if (y < -lim.top) y = r.bottom - ar.top + 6 + th <= H + lim.bottom ? r.bottom - ar.top + 6 : Math.max(0, r.top - ar.top - 6 - th);
      sendTip.style.left = Math.round(x) + "px"; sendTip.style.top = Math.round(y) + "px";
      return { x, y, w: tw, h: th, pad: SHADOW.tip };
    });
  }
  sendWrap._tip = sendTipFloat;
  // R21: the mode's name above its icon (bubble only), same drawing as the send button's bubble, left edges aligned
  function modeTipFloat(on) {
    if (!BUB() || !on || S.menu || !modeBtn.classList.contains("solo")) { modeTip.classList.remove("on"); floatDrop(modeTip); return; }
    if (modeTip.parentNode !== menuLayer) menuLayer.appendChild(modeTip);
    modeTip.classList.add("on");
    floatSet(modeTip, (lim, H) => {
      const ar = app.getBoundingClientRect(), r = modeBtn.getBoundingClientRect(), W = S.lay.maxW;
      const tw = modeTip.offsetWidth, th = modeTip.offsetHeight;
      const x = Math.max(-lim.left, Math.min(r.left - ar.left, W + lim.right - tw));
      let y = r.top - ar.top - 6 - th;
      if (y < -lim.top) y = r.bottom - ar.top + 6;
      modeTip.style.left = Math.round(x) + "px"; modeTip.style.top = Math.round(y) + "px";
      return { x, y, w: tw, h: th, pad: SHADOW.tip };
    });
  }
  modeBtn._tip = modeTipFloat;
  withTip(modeBtn);
  modeBtn.addEventListener("pointerdown", () => modeTipFloat(false));
  sendBtn.addEventListener("focus", () => { if (BUB() && sendBtn.matches(":focus-visible")) sendTipFloat(true); });
  sendBtn.addEventListener("blur", () => { if (BUB() && tipOn !== sendWrap) sendTipFloat(false); });
  const foot = h("div", { class: "foot" });
  const composer = h("div", { class: "composer" }, batchEl, thumbs, cin,
    h(foot, null, h("div", { class: "tools" }, pickBtn, plusBtn, modeBtn), h("div", { class: "grow" }),
      h("div", { class: "model" }, modelBtn), sendWrap));
  const keysRow = h("div", { class: "keys", "aria-hidden": "true", hidden: true },
    h("span", { class: "k" }, h("span", { class: "kc" }, "↵"), "Envoyer"),
    h("span", { class: "k" }, h("span", { class: "kcs" }, h("span", { class: "kc" }, "Maj"), h("span", { class: "kc" }, "↵")), "Nouvelle ligne"),
    h("span", { class: "k" }, h("span", { class: "kcs" }, h("span", { class: "kc" }, "Ctrl"), h("span", { class: "kc" }, "↵")), "Annotation"));

  function composerText() {
    let out = "";
    const walk = (n) => {
      for (const c of n.childNodes) {
        if (c.nodeType === 3) out += c.nodeValue;
        else if (c.nodeName === "BR") out += "\n";
        else if (c.classList && c.classList.contains("chip")) out += c.dataset.label;
        else if (c.nodeType === 1) { if (c.nodeName === "DIV" && out && !out.endsWith("\n")) out += "\n"; walk(c); }
      }
    };
    walk(cin);
    return out.replace(/ /g, " ");
  }
  const chipsInComposer = () => Array.from(cin.querySelectorAll(".chip")).map((c) => c.dataset.id);
  const batchChecked = () => (S.batch ? S.batch.items.filter((it) => S.batch.checked.has(it.id)) : []);
  function hasContent() { return composerText().trim().length > 0 || S.cAtt.size > 0 || batchChecked().length > 0; }
  function canSend() {
    const a = curAgent();
    return !S.st.disabledReason && !!S.st.folder && !hasDangerStrip() && (!a || a.status === "ready" || a.status === "starting");
  }
  function updateSend() {
    const running = !!S.st.running, content = hasContent();
    clear(sendBtn);
    sendBtn.className = "send";
    if (running && (!content || BUB())) {   // bubble: no input during a turn (Paper 1B30-0), the button is always « Arrêter »
      sendBtn.classList.add("stop"); sendBtn.appendChild(icon(BUB() ? "stopB" : "stop"));
      sendBtn.setAttribute("aria-label", "Arrêter (Échap)"); sendBtn.removeAttribute("aria-disabled");
      sendWrap.classList.add("no-tip");
    } else {
      sendBtn.appendChild(icon("arrowUp"));
      // Bubble (Paper 1AIB-0, 1B16-0): the button stays lit over an empty input — pressing it then puts the cursor in the
      // input. It only goes out when sending is impossible (no folder, agent not ready, site not local).
      const ok = canSend() && (content || BUB());
      if (ok) sendBtn.classList.add("ready");
      sendBtn.setAttribute("aria-label", running ? "Ajouter à la file" : "Envoyer");
      sendBtn.removeAttribute("title"); sendWrap.classList.remove("no-tip");   // the bubble says it (no second, native tooltip)
      if (ok) sendBtn.removeAttribute("aria-disabled"); else sendBtn.setAttribute("aria-disabled", "true");
    }
    const lines = composerText().split("\n").length;
    keysRow.hidden = BUB() || !(lines > 1 && document.activeElement === cin);
  }
  function onSendButton() {
    if (sendBtn.classList.contains("stop")) { post("stop"); return; }
    if (BUB() && !hasContent()) { cin.focus(); return; }
    send();
  }
  function send() {
    if (!hasContent() || !canSend()) return;
    const text = composerText().replace(/\s+$/, "");
    const msg = { text, elements: chipsInComposer().filter((id) => S.cEls.has(id)), attachments: Array.from(S.cAtt.keys()) };
    const picked = batchChecked();
    if (picked.length) msg.annotations = { batchId: S.batch.batchId, ids: picked.map((it) => it.id) };
    post("send", msg);
    if (BUB()) drafts.delete("b:el:" + (S.lay.elementKey || S.lay.element || ""));
    if (BUB() && text) outboxSet(text);
    S.cEls.clear(); S.cAtt.clear(); clear(cin); renderThumbs(); closeMenu();
    if (S.batch) { S.batch = null; renderBatch(); }
    updateSend();
  }
  // « Garder en annotation » (B 1.6): the chips and the words become annotations of the store, read by the MCP agents;
  // nothing goes to the chat's agent. Needs an element and a few words (the note of an annotation); images stay.
  let hintTimer = 0;
  function hint(text) {
    clearTimeout(hintTimer);
    S.strips.set("keep-hint", { id: "keep-hint", tone: "neutral", text, local: true }); renderDock(); say(text);
    hintTimer = setTimeout(() => { S.strips.delete("keep-hint"); renderDock(); }, 4000);
  }
  function keep() {
    if (S.st.disabledReason || S.batch) return;
    const els = chipsInComposer().filter((id) => S.cEls.has(id));
    const text = composerText().replace(/\s+$/, "");
    let words = text; for (const id of els) words = words.split(S.cEls.get(id).label).join("");
    // bubble (B 1.8): the element is the one the bubble is anchored to — the host adds it, no chip is needed
    if (!els.length && !BUB()) { hint("Cliquez un élément de la page pour le garder en annotation."); return; }
    if (!words.trim()) { hint("Écrivez ce qu'il faut changer, puis Ctrl+Entrée."); return; }
    clearTimeout(hintTimer); if (S.strips.delete("keep-hint")) renderDock();
    post("annotate", { text, elements: els });
    for (const id of els) S.cEls.delete(id);
    clear(cin); closeMenu(); updateSend();
  }
  function placeholder() {
    let p = "Que faut-il changer ?";
    if (S.batch) p = "Ajouter une précision (facultatif)…";
    else if (S.planPending) p = "Ajouter une précision au plan…";
    else if (BUB()) {   // Paper 1AE7-0 (stopped) / 1AWI-0 (done)
      const lt = S.lastTurn && S.turns.get(S.lastTurn);
      if (lt) p = lt.stop && lt.stop !== "end_turn" ? "Que voulez-vous ajouter ?" : "Que voulez-vous ajouter de plus ?";
    }
    else if (S.st.running) p = "Ajouter une suite…";
    else if (S.lastTurn) p = "Autre chose à changer ?";
    cin.setAttribute("data-placeholder", p);
  }
  // cv4 R26 (contract §22.1): a pause of 120 ms on a chip asks the host to show its element in the page (smooth scroll if
  // needed, frame, labels and inspector, as on hover); leaving it gives the page back. One chip at a time.
  let hoverChip = null, hoverTimer = 0;
  function chipHover(id, on) {
    clearTimeout(hoverTimer); hoverTimer = 0;
    if (on && ID.test(String(id))) { hoverTimer = setTimeout(() => { hoverTimer = 0; if (hoverChip === id) return; hoverChip = id; post("element.hover", { id, active: true }); }, 120); return; }
    if (hoverChip) { const was = hoverChip; hoverChip = null; post("element.hover", { id: was, active: false }); }
  }
  function chipNode(e, inComposer) {
    const n = h("span", { class: "chip", "data-id": e.id, "data-label": e.label, contenteditable: inComposer ? "false" : null,
                         onpointerenter: (ev) => { if (ev.pointerType !== "touch") chipHover(e.id, true); }, onpointerleave: () => chipHover(null, false) },
      icon(inComposer || KINDS.includes(e.kind) && !e.picked ? "k-" + (KINDS.includes(e.kind) ? e.kind : "block") : "corners"), h("span", { class: "chip-label", text: e.label }));
    if (inComposer) {
      n.setAttribute("role", "img");
      n.setAttribute("aria-label", "Élément " + e.label);
      n.appendChild(h("button", { class: "chip-x", type: "button", tabindex: "-1", "aria-label": "Retirer " + e.label,
                                  onmousedown: (ev) => ev.preventDefault(), onclick: () => { n.remove(); syncChips(); } }, icon("close")));
    }
    return n;
  }
  function insertNodeAtCaret(node) {
    cin.focus();
    const sel = window.getSelection();
    let r = sel.rangeCount ? sel.getRangeAt(0) : null;
    if (!r || !cin.contains(r.commonAncestorContainer)) { r = document.createRange(); r.selectNodeContents(cin); r.collapse(false); }
    r.deleteContents();
    const space = document.createTextNode(" ");
    r.insertNode(space); r.insertNode(node);
    r.setStartAfter(space); r.collapse(true);
    sel.removeAllRanges(); sel.addRange(r);
  }
  function syncChips() {
    const present = new Set(chipsInComposer());
    for (const id of Array.from(S.cEls.keys())) if (!present.has(id)) { S.cEls.delete(id); post("element.remove", { id }); }
    updateSend();
  }
  function renderThumbs() {
    clear(thumbs);
    for (const a of S.cAtt.values()) {
      thumbs.appendChild(h("div", { class: "thumb" }, h("img", { src: a.thumb, alt: a.name, style: "width:100%;height:100%;object-fit:cover;border-radius:5px;display:block" }),
        h("button", { class: "x", type: "button", "aria-label": "Retirer l'image " + a.name, onclick: () => { S.cAtt.delete(a.id); post("attachment.remove", { id: a.id }); renderThumbs(); updateSend(); } }, icon("close"))));
    }
    thumbs.hidden = S.cAtt.size === 0;
  }

  cin.addEventListener("input", (e) => {
    if (errGhost() && !e.isComposing && hasContent()) errGhostEnd();   // H1 monkey #29: written after an error — the input comes back
    if (!composerText().trim() && !cin.querySelector(".chip")) clear(cin);
    syncChips(); detectTrigger();
  });
  cin.addEventListener("compositionend", () => { if (errGhost() && hasContent()) errGhostEnd(); });   // …also once a composed character (IME) is done
  cin.addEventListener("focus", updateSend);
  cin.addEventListener("blur", () => setTimeout(updateSend, 0));
  cin.addEventListener("paste", (e) => {
    e.preventDefault();
    const dt = e.clipboardData;
    const file = dt && Array.from(dt.files || []).find((f) => /^image\/(png|jpeg|webp|gif)$/.test(f.type));
    if (file) {
      if (file.size > 9 * 1024 * 1024) { shout("Image trop lourde : 9 Mo au plus."); return; }
      const fr = new FileReader();
      fr.onload = () => { const s = String(fr.result); post("attachImage", { source: "paste", name: file.name || "image", mime: file.type, data: s.slice(s.indexOf(",") + 1) }); };
      fr.readAsDataURL(file);
      return;
    }
    const text = dt ? dt.getData("text/plain") : "";
    if (text) document.execCommand("insertText", false, text.slice(0, 100000));
  });
  cin.addEventListener("drop", (e) => e.preventDefault());
  cin.addEventListener("keydown", (e) => {
    if (S.menu && S.menu.combo && comboKey(e)) return;
    if (e.key === "Enter" && (e.ctrlKey || e.metaKey) && !e.shiftKey && !e.isComposing) { e.preventDefault(); keep(); return; }
    if (e.key === "Enter" && !e.shiftKey && !e.isComposing) { e.preventDefault(); send(); return; }
    if (e.key === "Enter" && e.shiftKey) { e.preventDefault(); document.execCommand("insertText", false, "\n"); return; }
    if (e.key === "Tab" && e.shiftKey && shownModes().length > 1 && !errGhost()) {   // (out of sight after an error: Maj+Tab goes back to the buttons)
      e.preventDefault();
      const ms = shownModes(), i = ms.findIndex((m) => m.id === S.st.mode);
      const next = ms[(i + 1) % ms.length];
      post("setMode", { id: next.id }); S.st.mode = next.id; renderComposerBar(); say("Mode " + next.label);
    }
  });

  // @ and / triggers
  let mentionTimer = 0;
  function detectTrigger() {
    const text = composerText();
    if (/^\/[\w-]*$/.test(text)) { openCommandMenu(text.slice(1)); return; }
    const sel = window.getSelection();
    if (sel.rangeCount && sel.anchorNode && sel.anchorNode.nodeType === 3 && cin.contains(sel.anchorNode)) {
      const before = sel.anchorNode.nodeValue.slice(0, sel.anchorOffset);
      const m = /(^|[\s ])@([^\s@ ]{0,60})$/.exec(before);
      if (m) {
        const q = m[2];
        clearTimeout(mentionTimer);
        mentionTimer = setTimeout(() => post("mention.query", { query: q }), 80);
        openMentionMenu(q, sel.anchorNode, sel.anchorOffset - q.length - 1);
        return;
      }
    }
    if (S.menu && S.menu.combo) closeMenu();
  }

  // ---------------------------------------------------------------- composer bar (mode, model, effort)
  function renderComposerBar() {
    // pass 1 #8 (R22): « + » with its single action (a folder found for sure) opens the image picker — it is not a menu
    const plusMenu = !(S.st.folder && S.st.askFolder !== true) || (S.st.capabilities && S.st.capabilities.image === false);
    if (plusMenu) { plusBtn.setAttribute("aria-haspopup", "menu"); if (!plusBtn.hasAttribute("aria-expanded")) plusBtn.setAttribute("aria-expanded", "false"); }
    else { plusBtn.removeAttribute("aria-haspopup"); plusBtn.removeAttribute("aria-expanded"); }
    S.picking ? pickBtn.setAttribute("aria-pressed", "true") : pickBtn.setAttribute("aria-pressed", "false");
    const mode = curMode();
    clear(modeBtn);
    modeBtn.className = "tb mode";
    modeBtn.hidden = !shownModes().length;
    if (mode) {
      const kind = mode.kind, lit = kind === "auto" || kind === "edits";   // the two automatic modes wear the accent
      if (BUB()) {   // R21 (user's decision over Paper 18TZ-0): the icon alone; the name shows in the drawn bubble on hover
        if (lit) modeBtn.classList.add("auto");
        modeBtn.classList.add("solo");
        modeBtn.append(icon(mode.icon, "ic"));
        modeTip.textContent = mode.label;
      }
      else if (lit) { modeBtn.classList.add("auto"); modeBtn.append(icon(mode.icon, "ic"), h("span", { class: "lbl", text: mode.label })); }
      else modeBtn.append(icon(mode.icon, "ic"), icon("chevron", "cv"));
      modeBtn.setAttribute("aria-label", "Mode : " + mode.label);
      modeBtn.setAttribute("aria-keyshortcuts", "Shift+Tab");
    }
    const a = curAgent(), m = curModel();
    clear(modelLogo).appendChild(agentIcon(a));
    // R13: always a model (the chosen one, else the agent's default, else its first) — the agent's name only while none is known
    const shown = m || (a && a.models && (a.models.find((x) => x.isDefault) || a.models[0])) || null;
    // cv4 R28: a label once shown for this agent stays until another model is known (no flicker to the agent's name)
    if (shown && a) S.lastModel = { agent: a.id, label: shown.label };
    const kept = !shown && a && S.lastModel && S.lastModel.agent === a.id ? S.lastModel.label : "";
    modelName.textContent = shown ? shown.label : kept || fallbackModel(a).label;   // R13: never the tool's name
    const e = curEffort();
    // cv4 R28: the effort never disappears — the chosen level, else the remembered id named in French, else the last shown
    const eLabel = e ? effortName(e.id, e.label) : S.st.effort && !/^default$/i.test(String(S.st.effort)) ? effortName(S.st.effort, S.st.effort) : S.lastEffort || (shown || kept ? "" : fallbackModel(a).effort);
    if (eLabel) S.lastEffort = eLabel;
    effortEl.hidden = !eLabel;
    effortTxt.textContent = eLabel;
    modelBtn.setAttribute("aria-label", "Modèle et effort : " + modelName.textContent + (e ? " · " + effortTxt.textContent : ""));
    placeholder();
    updateSend();
    fitFoot();
  }
  // The foot keeps one line: when it does not fit, the mode's chevron goes first; then the model name is cut (CSS
  // ellipsis), never the effort nor the provider's logo (V1.2).
  const FIT = ["fit-chev"];
  function fitFoot() {
    if (!foot.isConnected || !foot.clientWidth) return;
    foot.classList.remove(...FIT);
    foot.classList.add("measure");
    for (const c of FIT) { if (foot.scrollWidth <= foot.clientWidth) break; foot.classList.add(c); }
    foot.classList.remove("measure");
  }
  if (window.ResizeObserver) new ResizeObserver(() => fitFoot()).observe(composer);

  // ---------------------------------------------------------------- menus
  function closeMenu(restore) {
    if (!S.menu) return;
    const m = S.menu;
    closeSub();
    S.menu = null;
    m.el.remove();
    if (m.anchor) m.anchor.setAttribute("aria-expanded", "false");
    if (m.combo) { cin.removeAttribute("aria-activedescendant"); cin.removeAttribute("aria-controls"); }
    floatDrop(m.el);
    if (restore && m.anchor) m.anchor.focus();
  }
  function place(menuEl, anchor, align, width) {
    const ar = app.getBoundingClientRect(), r = anchor.getBoundingClientRect();
    const w = Math.min(width, ar.width - 16);
    menuEl.style.width = w + "px";
    let left = align === "right" ? r.right - ar.left - w : r.left - ar.left;
    left = Math.max(8, Math.min(left, ar.width - 8 - w));
    menuEl.style.left = left + "px";
    const side = align === "right" ? "right" : "left";
    if (!BUB()) {
      menuEl.style.bottom = (ar.bottom - r.top + 6) + "px";
      menuEl.style.setProperty("--origin", "bottom " + side);
      return;
    }
    // Bubble (B 1.9, R3, Paper 17US-0 / 17VU-0 / 14RK-0): a drop-down above its button that may leave the bubble; the
    // host grows the transparent window around the surface for it (`bubble.size` overflow*, granted by `layout.inset`).
    menuEl.style.bottom = "";
    floatSet(menuEl, (lim, H) => {
      if (menuEl._morphBox) return menuEl._morphBox;   // R41: while it transforms, its final box (not the moving one)
      const ar = app.getBoundingClientRect(), r = anchor.getBoundingClientRect(), W = S.lay.maxW;
      menuEl.style.width = width + "px"; menuEl.style.maxHeight = "";
      const mh = menuEl.offsetHeight;
      let x = align === "right" ? r.right - ar.left - width : r.left - ar.left;
      x = Math.max(-lim.left, Math.min(x, W + lim.right - width));
      const aTop = r.top - ar.top, aBot = r.bottom - ar.top;
      let y = null, origin = "bottom";
      for (const [yy, o] of [[aTop - 6 - mh, "bottom"], [aBot + 6, "top"]]) if (yy >= -lim.top && yy + mh <= H + lim.bottom) { y = yy; origin = o; break; }
      if (y == null) {                                       // no room outside: inside the surface, its list scrolls
        menuEl.style.maxHeight = Math.max(80, H - 12) + "px";
        const h2 = menuEl.offsetHeight;
        y = Math.max(6, Math.min(aTop - 6 - h2, H - 6 - h2));
        x = Math.max(6, Math.min(x, W - 6 - width));
      }
      menuEl.style.left = Math.round(x) + "px"; menuEl.style.top = Math.round(y) + "px";
      menuEl.style.setProperty("--origin", origin + " " + side);
      return { x, y, w: width, h: menuEl.offsetHeight, pad: menuEl.classList.contains("models") ? SHADOW.none : SHADOW.menu };   // Paper 17US-0 / 17VU-0: no shadow
    });
  }
  // Button-opened menu: focus moves in, arrows move, Enter picks, Escape returns to the trigger.
  function openMenu(anchor, opts) {
    if (S.menu && S.menu.anchor === anchor && !opts.keep) { closeMenu(true); return; }
    // cv5 R41: another menu already open (not a combo) — it does not leave; the new one grows out of it (morph)
    const prev = S.menu && !S.menu.combo && S.menu.anchor !== anchor && !isReduced() ? S.menu : null;
    let from = null;
    if (prev) { from = ghostMenu(prev.el); closeSub(); S.menu = null; if (prev.anchor) prev.anchor.setAttribute("aria-expanded", "false"); }
    else closeMenu();
    const el = h("div", { class: "menu" + (opts.cls ? " " + opts.cls : ""), role: "menu", "aria-label": opts.label, tabindex: "-1" });
    S.menu = { el, anchor, opts };
    fillMenu(el, opts.items);
    menuLayer.appendChild(el);
    place(el, anchor, opts.align, opts.width);
    if (from) morphFrom(from, el);
    anchor.setAttribute("aria-expanded", "true");
    const items = menuItems();
    (items.find((i) => i.getAttribute("aria-checked") === "true") || items[0] || el).focus({ preventScroll: BUB() });   // bubble: the window grows for the menu, nothing must scroll meanwhile
    el.addEventListener("keydown", (e) => {
      const its = menuItems(), i = its.indexOf(document.activeElement);
      if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); const n = its.length; if (n) its[(i + (e.key === "ArrowDown" ? 1 : n - 1) + n) % n].focus(); }
      else if (e.key === "Home") { e.preventDefault(); its[0] && its[0].focus(); }
      else if (e.key === "End") { e.preventDefault(); its[its.length - 1] && its[its.length - 1].focus(); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); if (S.sub) closeSub(true); else closeMenu(true); }
      else if (e.key === "Tab") closeMenu();
      else if (e.key === "ArrowRight" && document.activeElement && document.activeElement.dataset.sub) { e.preventDefault(); document.activeElement.click(); }
      else if (e.key === "ArrowLeft" && S.menu && S.menu.view && S.menu.view !== "main") { e.preventDefault(); showModelView("main", S.menu.view); }
    });
  }
  // R41 (cv5): the old menu stays where it was, without its surface, its content fading out; its box stays in the region
  // asked of the host (the union of both) until the end, so no image shows a gap or a flash.
  const MORPH_MS = 200;
  function ghostMenu(oldEl) {
    const ar = app.getBoundingClientRect(), r = oldEl.getBoundingClientRect();
    const box = { x: r.left - ar.left, y: r.top - ar.top, w: r.width, h: r.height, pad: oldEl.classList.contains("models") ? SHADOW.none : SHADOW.menu };
    oldEl.classList.add("menu-ghost"); oldEl.setAttribute("aria-hidden", "true"); oldEl.removeAttribute("role"); oldEl.inert = true;
    if (BUB()) floats.set(oldEl, () => box);   // no bubble.size yet: the first one, once the new menu is placed, asks for both
    setTimeout(() => { oldEl.remove(); floatDrop(oldEl); }, MORPH_MS + 40);   // after the new menu's own end (+20)
    return { left: r.left, top: r.top, width: r.width, height: r.height };
  }
  // The new menu starts on the old one's box (position, width, height) and moves to its own; its content fades in.
  function morphFrom(o, el) {
    const fw = el.style.width;
    el.classList.add("morph-in");   // first: the menus' entrance (scale .97, opacity 0) must not touch this box nor this surface
    const n = el.getBoundingClientRect();
    // the final box, shadow included, stays in the region asked of the host until the transformation is over (with the
    // old box, kept by its ghost): the region never shrinks under the growing menu
    const ar = app.getBoundingClientRect();
    el._morphBox = { x: n.left - ar.left, y: n.top - ar.top, w: n.width, h: n.height, pad: el.classList.contains("models") ? SHADOW.none : SHADOW.menu };
    if (BUB()) publishSize();
    el.classList.add("morph-in");
    el.style.transition = "none"; el.style.overflow = "hidden";
    el.style.width = o.width + "px"; el.style.height = o.height + "px";
    const r0 = el.getBoundingClientRect();
    el.style.transform = "translate(" + (o.left - r0.left) + "px," + (o.top - r0.top) + "px)";
    void el.offsetWidth;
    el.style.transition = "transform " + MORPH_MS + "ms var(--ease-out), width " + MORPH_MS + "ms var(--ease-out), height " + MORPH_MS + "ms var(--ease-out)";
    el.style.transform = "none"; el.style.width = n.width + "px"; el.style.height = n.height + "px";
    setTimeout(() => {
      if (!el.isConnected) return;
      el.style.transition = ""; el.style.transform = ""; el.style.height = ""; el.style.overflow = ""; el.style.width = fw;
      el.classList.replace("morph-in", "morphed");   // the menus' entrance stays blocked: removing it replayed the fade from 0
      el._morphBox = null;
    }, MORPH_MS + 20);
  }
  const menuItems = () => S.menu ? Array.from(S.menu.el.querySelectorAll(".mi:not([aria-disabled='true']), .menu .btn, .menu .btn-s, .menu .mm-act")) : [];
  function fillMenu(el, items) {
    clear(el);
    for (const it of items) {
      if (it.sep) { el.appendChild(h("div", { class: "sep", role: "separator" })); continue; }
      if (it.head) { el.appendChild(h("div", { class: "mh", role: "presentation", text: it.head })); continue; }
      if (it.node) { el.appendChild(it.node); continue; }
      const b = h("button", { class: "mi" + (it.desc ? " top" : "") + (it.back ? " back" : "") + (it.cls ? " " + it.cls : ""), type: "button", role: it.radio ? "menuitemradio" : "menuitem", tabindex: "-1",
                              "aria-checked": it.radio ? String(!!it.checked) : null, "aria-disabled": it.disabled ? "true" : null, "data-sub": it.sub ? "1" : null, "data-key": it.key || null,
                              "aria-haspopup": it.sub && BUB() ? "menu" : null, "aria-expanded": it.sub && BUB() ? "false" : null,
                              onclick: () => { if (!it.disabled) it.onSelect(); } });
      if (it.lead) b.appendChild(it.lead);
      const g = h("span", { class: "grow", style: "flex:1" }, h("span", { class: "row1" }, h("span", { class: "lb", text: it.label }), it.tag ? h("span", { class: "tag", text: it.tag }) : null));
      if (it.desc) g.appendChild(h("span", { class: "ds", text: it.desc }));
      if (it.hint) g.appendChild(h("span", { class: "mm-hint", text: it.hint }));   // #128: shown on hover / keyboard focus (CSS)
      b.appendChild(g);
      if (it.value) b.appendChild(h("span", { class: "val", text: it.value }));
      if (it.checked) b.appendChild(icon("check", "ck"));
      if (it.sub) b.appendChild(icon("chevron", "sub"));
      el.appendChild(b);
    }
  }
  document.addEventListener("pointerdown", (e) => {
    if (BUB() && !app.contains(e.target) && !menuLayer.contains(e.target)) { bTipHide(); tipHide(); }
    const trigger = e.target && e.target.closest && e.target.closest(".foot [aria-haspopup='menu']");
    if (S.menu && !S.menu.combo && trigger && trigger !== S.menu.anchor) return;   // R41: its click turns this menu into the other
    if (S.menu && !S.menu.el.contains(e.target) && !(S.sub && S.sub.el.contains(e.target)) && !(S.menu.anchor && S.menu.anchor.contains(e.target)) && e.target !== cin && !cin.contains(e.target)) closeMenu();
  }, true);

  // #128 (Paper A): 280 px, one list grouped by provider; the class before the menu is placed (its rows set its height)
  const MM_W = 280;
  function openModelMenu(anchor) {
    openMenu(anchor, { label: "Modèle et effort", items: modelItems("main"), align: "right", width: MM_W, cls: "models mm-list" });
    if (!S.menu) return;
    S.menu.view = "main";
    if (!BUB()) return;
    // Bubble: « Effort › » opens a real sub-menu next to this one (pointer: a short pause on the row)
    S.menu.el.addEventListener("pointerover", (e) => {
      const row = e.target.closest && e.target.closest(".mi");
      if (!row || !S.menu || !S.menu.el.contains(row)) return;
      clearTimeout(subTimer);
      if (row.dataset.sub) { if (!S.sub || S.sub.view !== row.dataset.key) subTimer = setTimeout(() => openSub(row.dataset.key, row, false), 120); }
      else if (S.sub) subTimer = setTimeout(() => closeSub(), 250);
    });
    // #128: the hint under another provider's model (hover, keyboard focus) makes the menu taller: the region asked of the
    // host follows (the menu grows upward from its button)
    const reask = () => requestAnimationFrame(() => { if (S.menu && S.menu.el.querySelector(".mm-x")) publishSize(); });
    for (const t of ["pointerover", "pointerout", "focusin"]) S.menu.el.addEventListener(t, reask);
  }
  // B 1.9 (Paper 6.4: 17VU-0 next to 17US-0): in the bubble, Effort is a true sub-menu beside the model menu (#128: every
  // model is in the list, no « Autres modèles » any more) — on its right, or on its left when the host cannot give the
  // room (`overflowRight` / `overflowLeft`). Tops aligned, 4 px apart. ArrowRight or the pointer opens; ArrowLeft or Échap closes it and returns to its row. The panel, too narrow
  // for two menus side by side, keeps the view inside the menu (« ‹ Effort »).
  let subTimer = 0;
  function subItems(view) { const it = modelItems(view); while (it.length && (it[0].back || it[0].sep)) it.shift(); return it; }
  function closeSub(restore) {
    clearTimeout(subTimer);
    const sub = S.sub;
    if (!sub) return;
    S.sub = null;
    sub.el.remove(); floatDrop(sub.el);
    if (sub.row) sub.row.setAttribute("aria-expanded", "false");
    if (restore && sub.row && sub.row.isConnected) sub.row.focus({ preventScroll: true });
  }
  function openSub(view, row, focus) {
    if (!S.menu || !row) return;
    clearTimeout(subTimer);
    if (S.sub && S.sub.view === view) { if (focus) subFocus(); return; }
    let from = null;
    if (S.sub && !isReduced()) {   // R41: Effort ↔ Autres modèles: the sub-menu transforms
      const old = S.sub; S.sub = null; clearTimeout(subTimer);
      if (old.row) old.row.setAttribute("aria-expanded", "false");
      from = ghostMenu(old.el);
    } else closeSub();
    const width = 214;   // « Effort » (Paper 17VU-0), as before #128
    const el = h("div", { class: "menu models sub-menu", role: "menu", "aria-label": "Effort", tabindex: "-1" });
    fillMenu(el, subItems(view));
    menuLayer.appendChild(el);
    S.sub = { el, view, row };
    row.setAttribute("aria-expanded", "true");
    const parent = S.menu.el;
    floatSet(el, (lim, H) => {
      const W = S.lay.maxW;
      el.style.width = width + "px"; el.style.maxHeight = "";
      const x0 = parseFloat(parent.style.left) || 0, y0 = parseFloat(parent.style.top) || 0, pw = parent.offsetWidth, sh = el.offsetHeight;
      let x = x0 + pw + 4, from = "left";
      if (x + width > W + lim.right) { x = x0 - 4 - width; from = "right"; if (x < -lim.left) { x = Math.max(-lim.left, x0 + pw - width); from = "left"; } }
      let y = y0;
      if (y + sh > H + lim.bottom) y = Math.max(-lim.top, H + lim.bottom - sh);
      el.style.left = Math.round(x) + "px"; el.style.top = Math.round(y) + "px";
      el.style.setProperty("--origin", "top " + from);
      return { x, y, w: width, h: sh, pad: SHADOW.none };
    });
    if (from) morphFrom(from, el);
    el.addEventListener("pointerover", () => clearTimeout(subTimer));
    el.addEventListener("keydown", (e) => {
      const its = Array.from(el.querySelectorAll(".mi:not([aria-disabled='true'])")), i = its.indexOf(document.activeElement);
      if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); const n = its.length; if (n) its[(i + (e.key === "ArrowDown" ? 1 : n - 1) + n) % n].focus(); }
      else if (e.key === "Home") { e.preventDefault(); its[0] && its[0].focus(); }
      else if (e.key === "End") { e.preventDefault(); its[its.length - 1] && its[its.length - 1].focus(); }
      else if (e.key === "ArrowLeft" || e.key === "Escape") { e.preventDefault(); e.stopPropagation(); closeSub(true); }
      else if (e.key === "Tab") closeMenu();
    });
    if (focus) subFocus();
  }
  function subFocus() {
    if (!S.sub) return;
    const its = Array.from(S.sub.el.querySelectorAll(".mi:not([aria-disabled='true'])"));
    (its.find((i) => i.getAttribute("aria-checked") === "true") || its[0] || S.sub.el).focus({ preventScroll: true });
  }
  const subOrView = (view) => () => {
    if (!BUB()) { showModelView(view); return; }
    const row = S.menu && S.menu.el.querySelector(".mi[data-key='" + view + "']");
    openSub(view, row, true);
  };
  function showModelView(view, from) {
    if (!S.menu) return;
    S.menu.view = view;
    fillMenu(S.menu.el, modelItems(view));
    S.menu.el.classList.toggle("mm-list", view === "main");   // #128: the grouped list; « ‹ Effort » keeps its look (17VU-0)
    S.menu.el.setAttribute("aria-label", view === "effort" ? "Effort" : "Modèle et effort");
    place(S.menu.el, S.menu.anchor, "right", view === "effort" ? 214 : MM_W);
    const its = menuItems();
    const back = from && S.menu.el.querySelector(".mi[data-key='" + from + "']");
    (back || its.find((i) => i.getAttribute("aria-checked") === "true") || its.find((i) => !i.classList.contains("back")) || its[0] || S.menu.el).focus({ preventScroll: BUB() });
  }
  // R42: the model changes at any time with no confirmation. The same provider keeps the chat; another provider (#128)
  // opens a new chat when this one has messages — the host decides (`setModel` / `setAgent` with the other agent), the page
  // only tells it beforehand (the row's hint). Agents are compared by id: the state is sent again often (bubble), so the
  // same agent may be another object.
  const chatHasMessages = () => S.users.size > 0 || S.turns.size > 0;
  function chooseModel(a, m) {
    const cur = curAgent();
    closeSub(); closeMenu(true);
    if (m) { post("setModel", { id: m.id, agentId: a.id }); S.st.model = m.id; }
    else if (!cur || a.id !== cur.id) post("setAgent", { id: a.id });
    if (!cur || a.id !== cur.id) S.st.agentId = a.id;
    renderComposerBar();
  }
  function modelItems(view) {
    const cur = curAgent(), items = [];
    const back = (label) => ({ back: true, label, lead: icon("chevron", "lead backi"), onSelect: () => showModelView("main", view) });
    const efforts = visibleEfforts();
    if (view === "effort") {
      items.push(back("Effort"), { sep: true });
      for (const e of efforts) items.push({ radio: true, label: effortName(e.id, e.label), checked: e.id === S.st.effort,
        onSelect: () => { post("setEffort", { id: e.id }); S.st.effort = e.id; closeMenu(true); renderComposerBar(); } });
      return items;
    }
    // #128 (Paper A): one list — each provider the host lists, in its order (Claude Code, then Codex): its header (logo, name),
    // then every model the agent said (never a name written here). A provider that cannot answer yet keeps its group: the
    // header says why with the action that fixes it (Connecter, Installer, Réessayer — the existing flows), its models greyed
    // out (32 %), not choosable. A model of the other provider says, under its name, that it opens a new chat (when this
    // one has messages). The effort stays the last row, its sub-menu as before.
    const hasChat = chatHasMessages();
    const row = (a, label, m, usable) => {
      const other = !cur || a.id !== cur.id;
      return { radio: true, label, tag: m && m.isDefault ? "Par défaut" : null, cls: "mm-row" + (other && usable && hasChat ? " mm-x" : ""),
               hint: other && usable && hasChat ? "Passer à " + a.name + " ouvre un nouveau chat" : null, disabled: !usable,
               checked: !other && (m ? m.id === S.st.model : !curModel()), onSelect: () => chooseModel(a, m) };
    };
    (S.st.agents || []).forEach((a, i) => {
      const usable = CONNECTED.includes(a.status), ms = a.models || [];
      if (i) items.push({ sep: true });
      items.push({ node: groupHead(a, usable) });
      for (const m of ms) items.push(row(a, m.label, m, usable));
      // #129 / R13: a provider ready but whose models are not known yet (new profile, its agent never started here) — its
      // group and one row naming the model it will use (the button's own fallback, FALLBACK_MODEL): the bubble's menu
      // is never the empty 14 px box #129 fixed, and the choice is a model, never « Claude Code » / « Codex »
      if (!ms.length && usable) items.push(row(a, fallbackModel(a).label, null, true));
    });
    if (efforts.length) items.push({ sep: true }, { label: "Effort", value: curEffort() ? effortName(curEffort().id, curEffort().label) : "", sub: true, key: "effort", cls: "mm-eff", onSelect: subOrView("effort") });
    return items;
  }
  // #128: a provider's header (Paper: 28 px, logo 14, 12/16 Medium at 50 %); not ready: « Codex · non connecté » and its
  // action at the right (32 px row, accent button 24 px)
  const MM_STATE = { notConnected: "non connecté", notInstalled: "pas installé", error: "erreur" };
  function groupHead(a, usable) {
    const act = usable ? null : agentAction(a);
    const word = usable ? "" : MM_STATE[a.status] || "";
    const label = act && a.status === "notConnected" ? "Connecter" : act ? act.label : "";
    return h("div", { class: "mm-head" + (act ? " mm-off" : ""), role: "presentation" },
      h("span", { class: "mm-hl" }, agentIcon(a, "mm-logo"), h("span", { class: "mm-hn", text: a.name + (word ? " · " + word : "") })),
      act ? h("button", { class: "mm-act", type: "button", role: "menuitem", tabindex: "-1", "aria-label": label + " " + a.name,
                          onclick: () => { act.run(); closeMenu(true); } }, label) : null);
  }
  function agentAction(a) {
    if (a.status === "notConnected") return { label: "Se connecter", run: () => post("agent.connect", { agentId: a.id, methodId: a.authMethods && a.authMethods[0] ? a.authMethods[0].id : undefined }) };
    if (a.status === "notInstalled") return { label: "Installer", run: () => post("agent.prepare", { agentId: a.id }) };
    if (a.status === "error") return { label: "Réessayer", run: () => post("agent.prepare", { agentId: a.id }) };
    return null;
  }
  const statusText = (a) => a.detail || ({ ready: "Installé", starting: "Démarrage…", preparing: "Préparation…", notInstalled: "Pas installé", notConnected: "Pas connecté", error: "Erreur" })[a.status] || "";
  function openModeMenu(anchor) {
    // Paper 14RK-0: 290 px, rows 8/10, the two automatic modes' icons in the accent (16 px), the others white (15 px)
    const items = shownModes().map((m) => ({ radio: true, label: m.label, desc: m.desc, checked: m.id === S.st.mode,
      lead: icon(m.icon, "lead m-" + m.kind),
      onSelect: () => { post("setMode", { id: m.id }); S.st.mode = m.id; closeMenu(true); renderComposerBar(); } }));
    openMenu(anchor, { label: "Mode", items, align: "left", width: 290, cls: "modes" });
  }
  function openPlusMenu(anchor) {
    const items = [];
    // R22: with one action only (the folder is sure), « + » does it at once — no menu
    if ((S.st.folder && S.st.askFolder !== true) && !(S.st.capabilities && S.st.capabilities.image === false)) { closeMenu(); post("attachImage", { source: "picker" }); return; }
    if (S.st.capabilities && S.st.capabilities.image === false) items.push({ label: "Joindre une image", desc: "Cet agent ne lit pas les images", disabled: true, lead: icon("k-image", "lead"), onSelect: () => {} });
    else items.push({ label: "Joindre une image", lead: icon("k-image", "lead"), onSelect: () => { closeMenu(true); post("attachImage", { source: "picker" }); } });
    // B 1.9 (R5): a folder found for sure is never shown nor changed from the input. Only when the browser is not sure
    // (`askFolder`, or no folder yet) does the menu offer to choose it — by its name, not its path (Paper 156L-0).
    if (!S.st.folder || S.st.askFolder === true)
      items.push({ label: "Choisir le dossier…", desc: folderName(S.st.folder) || null, lead: icon("folder", "lead"), onSelect: () => { closeMenu(true); post("folder.choose"); } });
    openMenu(anchor, { label: "Joindre", items, align: "left", width: 260 });
  }

  // Combobox menus (@ and /): focus stays in the composer, aria-activedescendant follows the row.
  function openCombo(kind, rows, foot) {
    const id = "combo";
    const el = h("div", { class: "menu", role: "listbox", id, "aria-label": kind === "@" ? "Éléments et fichiers" : "Actions" });
    const prev = S.menu && S.menu.combo === kind ? S.menu.active : 0;
    if (S.menu && !S.menu.combo) closeMenu();
    if (S.menu) S.menu.el.remove();
    S.menu = { el, combo: kind, rows, active: Math.min(prev, Math.max(0, rows.filter((r) => r.pick).length - 1)) };
    let n = 0;
    for (const r of rows) {
      if (r.head) { el.appendChild(h("div", { class: "mh", role: "presentation", text: r.head })); continue; }
      const k = n++;
      const opt = h("div", { class: kind === "/" ? "mi slash" : "mi pick", role: "option", id: "opt-" + k, "aria-selected": String(k === S.menu.active),
                             onmousedown: (e) => e.preventDefault(), onclick: () => { r.pick(); } });
      if (kind === "/") opt.append(h("span", { class: "lb", text: "/" + r.label }), r.desc ? h("span", { class: "ds", text: r.desc }) : null);
      else opt.append(icon(r.icon, "kind"), h("span", { class: "lb", text: r.label }), r.hint ? h("span", { class: "hint", text: r.hint }) : null);
      el.appendChild(opt);
    }
    if (!n) el.appendChild(h("div", { class: "mh", text: kind === "@" ? "Aucun élément ni fichier" : "Aucune action" }));
    el.appendChild(h("div", { class: "mfoot", "aria-hidden": "true" },
      h("span", { class: "k" }, h("span", { class: "kcs" }, h("span", { class: "kc" }, "↑"), h("span", { class: "kc" }, "↓")), "Choisir"),
      h("span", { class: "k" }, h("span", { class: "kc" }, "↵"), "Ajouter"),
      h("span", { class: "k" }, h("span", { class: "kc" }, "Echap"), "Fermer")));
    menuLayer.appendChild(el);
    place(el, composer, "left", composer.getBoundingClientRect().width);
    cin.setAttribute("aria-controls", id);
    cin.setAttribute("aria-activedescendant", n ? "opt-" + S.menu.active : "");
  }
  function comboKey(e) {
    const opts = Array.from(S.menu.el.querySelectorAll("[role=option]"));
    const picks = S.menu.rows.filter((r) => r.pick);
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      if (!opts.length) return true;
      S.menu.active = (S.menu.active + (e.key === "ArrowDown" ? 1 : opts.length - 1)) % opts.length;
      opts.forEach((o, i) => o.setAttribute("aria-selected", String(i === S.menu.active)));
      opts[S.menu.active].scrollIntoView({ block: "nearest" });
      cin.setAttribute("aria-activedescendant", "opt-" + S.menu.active);
      return true;
    }
    if ((e.key === "Enter" || e.key === "Tab") && !e.shiftKey && picks[S.menu.active]) { e.preventDefault(); picks[S.menu.active].pick(); return true; }
    if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); closeMenu(); return true; }
    return false;
  }
  const kindOf = (k) => (KINDS.includes(k) ? "k-" + k : "k-block");
  let mentionCtx = null;
  function openMentionMenu(q, node, offset) {
    mentionCtx = { q, node, offset };
    const ms = S.mentions && S.mentions.query === q ? S.mentions : S.mentions || { elements: [], files: [] };
    const ql = q.toLowerCase();
    const els = (ms.elements || []).filter((e) => !ql || (e.label + " " + (e.hint || "")).toLowerCase().includes(ql)).slice(0, 6);
    const files = (ms.files || []).filter((f) => !ql || f.path.toLowerCase().includes(ql)).slice(0, 6);
    const rows = [];
    if (els.length) rows.push({ head: "Éléments de la page" });
    for (const e of els) rows.push({ label: e.label, hint: e.hint || e.text || "", icon: kindOf(e.kind), pick: () => pickMention({ element: e }) });
    if (files.length) rows.push({ head: "Fichiers du projet" });
    for (const f of files) rows.push({ label: f.path.split(/[\\/]/).pop(), hint: f.dir || "", icon: "code", pick: () => pickMention({ file: f }) });
    openCombo("@", rows);
  }
  function pickMention(choice) {
    const c = mentionCtx;
    closeMenu();
    if (!c || !c.node.isConnected) return;
    const r = document.createRange();
    r.setStart(c.node, Math.max(0, c.offset));
    r.setEnd(c.node, Math.min(c.node.nodeValue.length, c.offset + 1 + c.q.length));
    const sel = window.getSelection(); sel.removeAllRanges(); sel.addRange(r);
    if (choice.element) { S.cEls.set(choice.element.id, choice.element); insertNodeAtCaret(chipNode(choice.element, true)); }
    else document.execCommand("insertText", false, "@" + (choice.file.dir ? choice.file.dir.replace(/[\\/]$/, "") + "/" : "") + choice.file.path.split(/[\\/]/).pop() + " ");
    updateSend();
  }
  function openCommandMenu(q) {
    const ql = q.toLowerCase();
    // R24: no panel command of its own anymore (« /annotations » is gone); the commands are the agent's
    const all = (S.commands || []).filter((c) => c.name !== "annotations");
    const rows = all.filter((c) => c.name.toLowerCase().startsWith(ql) || !ql).slice(0, 8)
      .map((c) => ({ label: c.name, desc: c.description, pick: () => {
        closeMenu(); clear(cin); cin.focus();
        if (c.own) { post("annotations.request"); updateSend(); return; }
        document.execCommand("insertText", false, "/" + c.name + " "); updateSend(); } }));
    openCombo("/", rows);
  }

  // ---------------------------------------------------------------- dock (strips, queue, permission, composer)
  function derivedStrips() {
    const out = [];
    if (S.st.disabledReason) out.push({ id: "site", tone: "neutral", text: S.st.disabledReason });
    // bubble: there is no « Où est le code de ce site ? » screen — one line says what is missing, with its action
    else if (BUB() && S.st.folder === null) out.push({ id: "folder", tone: "neutral", text: "Choisissez le dossier du projet pour commencer.", action: { id: "choose", label: "Choisir le dossier…" } });
    const a = curAgent();
    if (a && S.st.folder) {
      if (a.status === "notConnected") out.push({ id: "agent", tone: "danger", text: "Vous n'êtes plus connecté à " + a.name + ".", action: { id: "connect", label: "Se connecter" } });
      else if (a.status === "notInstalled") out.push({ id: "agent", tone: "neutral", text: a.name + " n'est pas installé sur cet ordinateur.", action: { id: "prepare", label: "Installer" } });
      else if (a.status === "preparing") out.push({ id: "agent", tone: "neutral", text: (a.detail || "Préparation de " + a.name + "…"), progress: a.progress });
      else if (a.status === "error") out.push({ id: "agent", tone: "danger", text: a.detail || a.name + " n'a pas pu démarrer.", action: { id: "prepare", label: "Réessayer" } });
    }
    return out;
  }
  function activeStrip() {
    let all = Array.from(S.strips.values()).concat(derivedStrips());
    // bubble: the waiting message already says « Commencera après le travail en cours sur … » under itself (queue.after);
    // the host's strip that says the same thing (`wait`) would repeat it
    if (BUB() && S.queue.some((q) => q.after)) all = all.filter((s) => s.id !== "wait");
    return all.find((s) => s.tone === "danger") || all[0] || null;
  }
  const hasDangerStrip = () => { const s = activeStrip(); return !!(s && s.tone === "danger"); };
  function stripAction(s) {
    if (s.id === "folder") { post("folder.choose"); return; }
    if (s.id === "agent") { const a = curAgent(); const act = a && agentAction(a); if (act) act.run(); return; }
    if (s.errorId) post("error.action", { errorId: s.errorId, actionId: s.action.id });
    else post("strip.action", { stripId: s.id, actionId: s.action.id });
  }
  function stripNode(s) {
    return h("div", { class: "strip " + (s.tone === "danger" ? "danger" : ""), role: s.tone === "danger" ? "alert" : "status" },
      h("div", { class: "tx" }, s.text, s.progress != null ? h("div", { class: "progress", role: "progressbar", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": String(Math.round(s.progress * 100)) },
        h("i", { style: "width:" + Math.round(s.progress * 100) + "%" })) : null),
      s.action ? h("button", { class: "btn-s", type: "button", onclick: () => stripAction(s) }, s.action.label) : null);
  }
  let lastDockKey = "";
  function renderDock(force) {
    if (BUB()) { renderDockB(force); return; }
    const p = S.perms[0];
    const s = activeStrip();
    const key = JSON.stringify([p && p.requestId, S.perms.length, s, S.queue, S.st.disabledReason]);
    if (!force && key === lastDockKey) { composer.classList.toggle("disabled", !!(S.st.disabledReason || (s && s.tone === "danger"))); updateSend(); return; }
    lastDockKey = key;
    const hadFocusInComposer = composer.contains(document.activeElement);
    clear(dock);
    dock.className = "dock"; ruleEl.hidden = true;
    if (p) { dock.appendChild(permCard(p, hadFocusInComposer)); return; }
    for (const q of S.queue) {
      dock.appendChild(h("div", { class: "queue", role: "group", "aria-label": "Suite en file" },
        h("span", { class: "tag", text: "En file" }), h("span", { class: "tx", text: q.text }),
        h("button", { class: "btn-s", type: "button", onclick: () => post("queue.sendNow", { id: q.id }) }, "Envoyer maintenant"),
        h("button", { class: "icon-btn", type: "button", "aria-label": "Retirer de la file", onclick: () => post("queue.remove", { id: q.id }) }, icon("close"))));
    }
    const stack = h("div", { class: "stack" });
    if (s) stack.appendChild(stripNode(s));
    stack.appendChild(composer);
    composer.classList.toggle("disabled", !!(S.st.disabledReason || (s && s.tone === "danger")));
    cin.setAttribute("contenteditable", S.st.disabledReason ? "false" : "true");
    cin.setAttribute("aria-disabled", S.st.disabledReason ? "true" : "false");
    dock.append(stack, keysRow);
    if (hadFocusInComposer) cin.focus();
    updateSend();
  }

  // Tool names never reach the reader raw: mcp__<server>__<tool> becomes « Lister les annotations » (Annotations du navigateur).
  const TOOL_RE = /\bmcp__([A-Za-z0-9.-]+(?:_[A-Za-z0-9.-]+)*?)__([A-Za-z0-9_.-]+)/;
  const VERBS = { list: "Lister", get: "Lire", read: "Lire", fetch: "Récupérer", show: "Afficher", view: "Afficher", create: "Créer", add: "Ajouter", new: "Créer",
    update: "Modifier", edit: "Modifier", set: "Régler", write: "Écrire", save: "Enregistrer", delete: "Supprimer", remove: "Retirer", search: "Chercher", find: "Chercher",
    query: "Interroger", resolve: "Marquer comme traitée", claim: "Prendre en charge", release: "Relâcher", open: "Ouvrir", close: "Fermer", run: "Lancer",
    start: "Démarrer", stop: "Arrêter", capture: "Capturer", screenshot: "Capturer", navigate: "Ouvrir", click: "Cliquer sur", send: "Envoyer", reply: "Répondre à",
    check: "Vérifier", count: "Compter", select: "Choisir", reload: "Recharger", wait: "Attendre", evaluate: "Évaluer", install: "Installer" };
  const NOUNS = { annotations: "les annotations", annotation: "une annotation", screenshot: "une capture", screenshots: "des captures", file: "un fichier", files: "les fichiers",
    page: "la page", pages: "les pages", console: "la console", logs: "les journaux", log: "le journal", element: "un élément", elements: "les éléments",
    selection: "la sélection", tab: "un onglet", tabs: "les onglets", site: "le site", sites: "les sites", comment: "un commentaire", comments: "les commentaires",
    status: "l'état", url: "l'adresse", network: "le réseau", requests: "les requêtes", errors: "les erreurs", styles: "les styles", snapshot: "un instantané" };
  const SERVERS = { "browser-annotations": "Annotations du navigateur" };
  const cap = (t) => (t ? t[0].toUpperCase() + t.slice(1) : t);
  function serverLabel(srv) { const base = srv.replace(/[-_](preview|dev|local|beta|test)$/i, ""); return SERVERS[base] || cap(base.replace(/[-_.]+/g, " ")); }
  function toolLabel(tool, srv) {
    const toks = tool.split(/[_.-]+/).filter(Boolean).map((t) => t.toLowerCase());
    const vi = toks.findIndex((t) => VERBS[t]);
    if (vi < 0) { const rest = toks.filter((t) => !["browser", "mcp", "tool"].includes(t)); return cap((rest.length ? rest : toks).join(" ")); }
    let obj = toks.filter((t, i) => i !== vi && !["browser", "mcp", "tool"].includes(t));
    const srvToks = new Set((srv || "").toLowerCase().split(/[_.-]+/));
    if (obj.length > 1 && obj.some((t) => !srvToks.has(t))) obj = obj.filter((t) => !srvToks.has(t) || NOUNS[t]);
    if (obj.length > 1 && NOUNS[obj[obj.length - 1]]) obj = [obj[obj.length - 1]];
    const what = obj.length === 1 && NOUNS[obj[0]] ? NOUNS[obj[0]] : obj.join(" ");
    return (VERBS[toks[vi]] + (what ? " " + what : "")).trim();
  }
  function parseTool(raw) { const m = TOOL_RE.exec(raw || ""); return m ? { name: m[0], server: m[1], tool: m[2] } : null; }
  // Rewrites a text that contains raw tool names; a text that is only a tool name becomes « Label (Serveur) ».
  function humanizeTools(text) {
    if (!text || !/mcp__/.test(text)) return text;
    const whole = parseTool(text);
    if (whole && whole.name === text.trim()) return toolLabel(whole.tool, whole.server) + " (" + serverLabel(whole.server) + ")";
    return text.replace(new RegExp(TOOL_RE.source, "g"), (m0, srv, tool) => "« " + toolLabel(tool, srv) + " »");
  }
  function permissionTitle(m) {
    const who = curAgentName().replace(/ Code$/, "");
    const t = m.tool && typeof m.tool === "object" ? m.tool : null;
    const raw = parseTool((t && t.name) || "") || parseTool(m.title || "");
    const human = typeof m.title === "string" && m.title.trim() && !/mcp__|\b[a-z0-9]+(?:_[a-z0-9]+){2,}\b/.test(m.title);
    if (human) return m.title.trim();
    if (t && isStr(t.label, 200) && t.label) return who + " veut utiliser l'outil « " + t.label + " »" + (isStr(t.serverLabel, 200) && t.serverLabel ? " (" + t.serverLabel + ")" : "");
    if (raw) return who + " veut utiliser l'outil « " + toolLabel(raw.tool, raw.server) + " » (" + serverLabel(raw.server) + ")";
    if (t && isStr(t.name, 300) && t.name) return who + " veut utiliser l'outil « " + toolLabel(t.name, t.server || "") + " »";
    return (m.title || "").replace(/\b[a-z0-9]+(?:_[a-z0-9]+){2,}\b/g, (w) => "« " + toolLabel(w, "") + " »").trim() || who + " demande votre accord";
  }
  function permissionDetails(m) {
    const t = m.tool && typeof m.tool === "object" ? m.tool : null;
    const raw = (t && isStr(t.name, 300) && t.name) || (parseTool(m.title || "") || {}).name || "";
    let input = isStr(m.input, 4000) ? m.input : "";
    if (input) { try { input = JSON.stringify(JSON.parse(input), null, 2); } catch (e) { /* shown as sent */ } }
    if (!raw && !input) return "";
    return (raw ? "Outil : " + raw : "") + (raw && input ? "\n" : "") + (input ? "Arguments :\n" + input : "");
  }

  // SEC-23 (audit 2026-11-23): a request never takes a keypress or a click meant for something else. The card is not focused
  // on « Autoriser » (the card itself, or nothing while the user was typing in the composer), and it takes no Enter, Space or
  // click to allow for PERM_ARM_MS after it appears (or replaces the previous one); a key held down (repeat) never allows.
  // « Toujours » is sent only with its switch on: no fallback to an allow_always option.
  const PERM_ARM_MS = 600;
  const permArmed = (card, e) => !(e && e.repeat) && performance.now() - (card.__shownAt || 0) >= PERM_ARM_MS;
  function permCard(p, typing) {
    const allowOnce = p.options.find((o) => o.kind === "allow_once");
    const allowAlways = p.options.find((o) => o.kind === "allow_always");
    const reject = p.options.find((o) => o.kind === "reject_once") || p.options.find((o) => /^reject/.test(o.kind));
    let always = false;
    const instead = h("input", { class: "instead", type: "text", placeholder: "Dire à " + curAgentName().replace(/ Code$/, "") + " quoi faire à la place…",
                                 "aria-label": "Dire à l'agent quoi faire à la place", maxlength: "4000" });
    const sw = allowAlways ? h("button", { class: "always", type: "button", role: "switch", "aria-checked": "false",
                                           onclick: () => { always = !always; sw.setAttribute("aria-checked", String(always)); } },
                                h("span", { class: "sw", "aria-hidden": "true" }, h("i")), "Toujours autoriser") : h("span", { class: "always" });
    const respond = (opt, message) => {
      if (!opt) return;
      post("permission.respond", Object.assign({ requestId: p.requestId, optionId: opt.optionId }, message ? { message } : {}));
      S.perms.shift();
      refreshWaiting();
      renderDock(true);
      if (!S.perms.length) cin.focus(); else dock.querySelector(".perm") && dock.querySelector(".perm").focus();   // SEC-23: the next card, never its « Autoriser »
    };
    const refuse = h("button", { class: "btn", type: "button", "aria-keyshortcuts": "Escape", onclick: () => respond(reject, instead.value.trim() || undefined) },
      "Refuser", h("span", { class: "kbd", "aria-hidden": "true" }, "Échap"));
    const allow = h("button", { class: "btn primary", type: "button", "aria-keyshortcuts": "Enter",
                                onclick: (e) => { if (permArmed(card, e)) respond(always && allowAlways ? allowAlways : allowOnce); } },
      "Autoriser", h("span", { class: "kbd", "aria-hidden": "true" }, "↵"));
    instead.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.isComposing) { e.preventDefault(); if (instead.value.trim()) respond(reject, instead.value.trim()); else if (permArmed(card, e)) allow.click(); }
    });
    const card = h("div", { class: "perm" + (S.replay ? "" : " pop-in"), role: "alertdialog", "aria-modal": "false", "aria-labelledby": "perm-t", "aria-describedby": "perm-d", tabindex: "-1" },
      h("div", { class: "perm-hd" }, h("span", { class: "perm-t", id: "perm-t", text: p.title }),
        S.perms.length > 1 ? h("span", { class: "perm-of", text: "1 sur " + S.perms.length }) : null,
        p.diff ? h("span", { class: "num plus", text: "+" + p.diff.added }) : null, p.diff ? h("span", { class: "num minus", text: "−" + p.diff.removed }) : null),
      h("div", { id: "perm-d" }, p.diff ? diffView(p.diff) : null, p.command ? h("pre", { class: "cmd", text: p.command }) : null),
      p.details ? detailsBlock(p.details) : null,
      reject ? instead : null,
      h("div", { class: "perm-ft" }, sw, reject ? refuse : null, allow));   // F10: no refuse option, no « Refuser »
    card.__shownAt = p.shownAt || (p.shownAt = performance.now());
    card.addEventListener("keydown", (e) => {
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); refuse.click(); }
      else if ((e.key === "Enter" || e.key === " ") && e.repeat && e.target !== instead) e.preventDefault();   // SEC-23: a held key answers nothing
      else if (e.key === "Enter" && e.target !== instead && e.target.tagName !== "BUTTON") { e.preventDefault(); if (permArmed(card, e)) allow.click(); }
    });
    requestAnimationFrame(() => { if (!typing && card.isConnected) card.focus(); });   // SEC-23: never on « Autoriser »
    return card;
  }
  function detailsBlock(text) {
    const pre = h("pre", { class: "detail", id: "perm-details", hidden: true, text });
    const b = h("button", { class: "more perm-more", type: "button", "aria-expanded": "false", "aria-controls": "perm-details" }, icon("chevron", "dcv"), "Détails");
    b.addEventListener("click", () => { pre.hidden = !pre.hidden; b.setAttribute("aria-expanded", String(!pre.hidden)); });
    return h("div", { class: "perm-details" }, b, pre);
  }
  function diffView(d) {
    const box = h("div", { class: "diff", role: "region", "aria-label": "Aperçu du changement dans " + d.path, tabindex: "0" });
    const lines = (d.lines || []).slice(0, 400);
    for (const l of lines) {
      const cls = l.op === "+" ? "add" : l.op === "-" ? "del" : l.op === "@" ? "hunk" : "";
      box.appendChild(h("div", { class: "l " + cls, text: (l.op === "@" ? "" : (l.op === " " ? " " : l.op === "-" ? "-" : "+") + "  ") + l.text }));
    }
    if (d.truncated) box.appendChild(h("div", { class: "more-l", text: "… aperçu tronqué" }));
    return box;
  }

  // ---------------------------------------------------------------- comment-mode bubble (B 1.8, design/chat-panel.md §19)
  // Same thread, same messages, another drawing: a 305 px bubble next to the element, no header, no conversation list.
  // Its height follows its content — every change is published (`bubble.size`) and the host resizes the window.
  // Nothing is ever drawn outside the bubble: menus and hover bubbles open inside it.
  let lastSize = "";
  const curW = () => (S.bview && S.bsize ? S.bsize.w : S.lay.maxW);
  function bubbleNatural() {
    // cv5 R40: the diff « en grand » keeps the bubble's size of before (or the one chosen with the handle)
    if (S.bview && S.bsize) return S.bsize.h;
    if (S.bview && S.viewBase && !(S.bview.kind === "perm" && !S.bview.p.diff)) return S.viewBase.h;
    if (S.bview) {                                           // the diff « en grand » takes all the room there is;
      const box = !(S.bview.kind === "perm" && !S.bview.p.diff) ? null : bviewEl.querySelector(".bview-body");
      if (!box) return S.lay.maxH;
      let inner = 12;                                        // a command or a tool's arguments: their own height, never
      for (const c of box.children) inner += c.offsetHeight; // smaller than the bubble of a turn in progress (224)
      return Math.max(224, 68 + inner + dock.offsetHeight);
    }
    let hgt = 24, n = 0;                                     // 12 px of padding above and under
    const part = (x) => { if (x > 0) { hgt += x; n++; } };
    if (!body.hidden) part(log.offsetHeight);                // measured on the content: independent of the window's size
    if (!ruleEl.hidden) part(1);
    part(dock.offsetHeight);
    return hgt + Math.max(0, n - 1) * 12;
  }
  // B 1.9 (R3): what floats outside the surface (a menu, a hover bubble). Each placer draws its node for the room it may
  // use (`lim`, px around the surface) and returns its box (+ the room its shadow wants). The union, beyond the surface,
  // is the overflow the page asks for; a side the host granted less than asked becomes a limit, and the floats are placed
  // again (above, then under, then inside the surface). The limits only shrink while something floats: it ends.
  // The one bound (px CSS) on the transparent margin around the surface — the host's inset and the overflow asked — so a
  // bubble whose bottom stays fixed can grow by up to this much at once without its surface moving (H1: PageInsetCapCss)
  const SIDES = ["top", "right", "bottom", "left"], CAP = 1000;
  const SHADOW = { none: { top: 0, right: 0, bottom: 0, left: 0 },
                   menu: { top: 32, right: 48, bottom: 64, left: 48 },    // 0 16px 48px #00000080 (Paper 14RK-0)
                   tip: { top: 16, right: 24, bottom: 32, left: 24 } };   // 0 8px 24px #00000080 (send button bubble)
  const floats = new Map();
  let lim = { top: CAP, right: CAP, bottom: CAP, left: CAP };
  function floatSet(el, placer) { floats.set(el, placer); publishSize(); }
  function floatDrop(el) {
    if (!floats.delete(el)) return;
    if (!floats.size) lim = { top: CAP, right: CAP, bottom: CAP, left: CAP };
    publishSize();
  }
  function floatNeed(H) {
    const need = { top: 0, right: 0, bottom: 0, left: 0 }, W = S.lay.maxW;
    // cv3 (R19): the host's window keeps a fixed transparent margin around the surface (`layout.inset`); a float stays inside it
    const room = {};
    // E4 (b): and inside the page area the host measured (`layout.room`, px from each side of the surface to the page area's
    // edge) — a menu turns before the host has to refuse it
    for (const k of SIDES) room[k] = Math.min(lim[k], S.insetKnown ? S.inset[k] : CAP, S.lay.room && Number.isFinite(S.lay.room[k]) ? S.lay.room[k] : CAP);
    for (const [el, placer] of Array.from(floats)) {
      if (!el.isConnected || el.hidden) { floats.delete(el); continue; }
      const r = placer(room, H), p = r.pad || SHADOW.none;
      need.top = Math.max(need.top, p.top - r.y); need.left = Math.max(need.left, p.left - r.x);
      need.right = Math.max(need.right, r.x + r.w + p.right - W); need.bottom = Math.max(need.bottom, r.y + r.h + p.bottom - H);
    }
    for (const k of SIDES) need[k] = Math.max(0, Math.min(room[k], CAP, Math.ceil(need[k])));
    return need;
  }
  // `layout.inset`: where the host really put the surface in its window. Drawn at once; a side short of what was asked
  // becomes a limit and the floats move.
  function applyInset(inset) {
    const v = {};
    for (const k of SIDES) v[k] = inset && Number.isFinite(inset[k]) ? Math.max(0, Math.min(CAP, Math.floor(inset[k]))) : 0;
    S.insetKnown = !!inset;
    S.inset = v;
    const st = document.documentElement.style;
    // R19: the height the page asked for and the host's inset land together — the surface never moves nor stretches
    if (BUB() && S.pendH) { st.setProperty("--bh", S.pendH + "px"); st.setProperty("--bw", curW() + "px"); }
    for (const k of SIDES) st.setProperty("--i" + k[0], v[k] + "px");
    for (const k of SIDES) if (floats.size && S.ovAsk[k] > v[k] && lim[k] > v[k]) lim[k] = v[k];
    if (floats.size) publishSize();
  }
  // Q's journeys: the host shows the bubble at the size of the acknowledgement of its opening layout. Sent before the
  // thread (which H1 sends right after the layout, in the same go), it said the empty bubble's height and the bubble grew
  // by up to 500 px once shown. The acknowledgement now leaves with the thread drawn — at its `thread.reset`, else at the
  // next image — still the first `bubble.size` after the layout, with the height the bubble keeps.
  let ackPending = false;
  function ackSoon() { ackPending = true; requestAnimationFrame(ackNow); }
  function ackNow() { if (!ackPending) return; ackPending = false; publishSize(true); }
  function publishSize(force) {
    if (!BUB() || S.fatal) return;
    if (ackPending && !force) return;   // nothing before the acknowledgement (it carries the final size)
    const hgt = Math.max(1, Math.min(S.bsize ? Infinity : S.lay.maxH, Math.ceil(bubbleNatural())));
    const need = floats.size ? floatNeed(hgt) : { top: 0, right: 0, bottom: 0, left: 0 };
    const key = curW() + "x" + hgt + ":" + SIDES.map((k) => need[k]).join(",") + (S.bsize ? ":r" : "");
    bFade();
    S.pendH = hgt;   // R19: drawn when the host answers (layout open:false), with its inset, in the same task
    if (!force && key === lastSize) return;
    lastSize = key;
    S.ovAsk = need;
    const m = { width: curW(), height: hgt };
    if (S.bview && S.bsize) {   // R40: a size chosen with the handle — drawn at once (the host only answers when it grants less)
      m.resized = true;
      const st = document.documentElement.style;
      st.setProperty("--bw", m.width + "px"); st.setProperty("--bh", hgt + "px");
    }
    if (need.top) m.overflowTop = need.top;
    if (need.right) m.overflowRight = need.right;
    if (need.bottom) m.overflowBottom = need.bottom;
    if (need.left) m.overflowLeft = need.left;
    post("bubble.size", m);
  }
  // Paper 1ANU-0: once the bubble has reached its maximum, the thread scrolls and its top fades.
  function bFade() { app.classList.toggle("faded", BUB() && !body.hidden && scroller.scrollTop > 0.5 && bubbleNatural() > S.lay.maxH); }
  // cv4 R35: the input's words, chips and images belong to one conversation (the panel's, or one element's bubble); a
  // `thread.reset` puts them aside and brings back the ones of the conversation shown. A bubble never shows a chip (R7).
  const drafts = new Map();
  let draftKey = "";
  // F6 (review): while a bubble's conversation is empty (nothing sent), its draft belongs to the element (layout.elementKey,
  // else its label) — a click elsewhere then on the element again finds the words back; once sent, to the conversation.
  const draftKeyFor = (conv, empty) => BUB() ? (empty ? "b:el:" + (S.lay.elementKey || S.lay.element || "") : "b:" + (conv && conv !== "new" ? conv : "new:" + (S.lay.elementKey || S.lay.element || "")))
                                             : "p:" + (conv && conv !== "new" ? conv : "new");
  function stashDraft() {
    if (!draftKey) { clear(cin); S.cEls.clear(); S.cAtt.clear(); return; }
    const frag = document.createDocumentFragment();
    while (cin.firstChild) frag.appendChild(cin.firstChild);
    drafts.delete(draftKey);
    if (frag.textContent.trim() || frag.querySelector && frag.querySelector(".chip") || S.cAtt.size) drafts.set(draftKey, { frag, els: new Map(S.cEls), att: new Map(S.cAtt) });
    while (drafts.size > 40) drafts.delete(drafts.keys().next().value);   // F5 (review): the oldest drafts go (nothing grows without end)
    S.cEls.clear(); S.cAtt.clear();
  }
  function restoreDraft(key) {
    draftKey = key;
    const d = drafts.get(key);
    drafts.delete(key);
    clear(cin);
    if (d) { cin.appendChild(d.frag); for (const [k, v] of d.els) S.cEls.set(k, v); for (const [k, v] of d.att) S.cAtt.set(k, v); }
    if (BUB()) for (const c of Array.from(cin.querySelectorAll(".chip"))) { S.cEls.delete(c.dataset.id); c.remove(); }
    renderThumbs(); updateSend();
  }
  let resetGen = 0;
  // Q's journeys (first openings 150 to 211 ms): each exchange costs ~3.5 ms to draw. Before the first image only the last
  // exchanges that fill the bubble are drawn (their height estimated from their text, never under it: 1.5 × the bubble's
  // height, at least one exchange); the bubble has its full height at once, nothing jumps when the older ones come above.
  function firstCut(items) {
    const users = []; items.forEach((it, i) => { if (it && it.type === "message.user") users.push(i); });
    if (users.length < 2) return 0;
    const want = (BUB() ? S.lay.maxH : 900) * 1.5;
    let hgt = 0;
    for (let u = users.length - 1, end = items.length; u >= 1; end = users[u], u--) {
      for (let i = users[u]; i < end; i++) {
        const it = items[i]; if (!it) continue;
        const tx = typeof it.text === "string" ? it.text.length : 0;
        hgt += it.type === "message.user" || it.type === "message.agentChunk" ? 24 + Math.ceil(tx / 60) * 18 : it.type === "toolCall" ? 24 : 0;
      }
      if (hgt >= want) return users[u];
    }
    return 0;
  }
  function prependOlder(items) {
    const anchor = log.firstChild;
    const keep = { lastUser: S.lastUser, lastTurn: S.lastTurn, lastAt: S.lastAt, berr: S.berr, planPending: S.planPending, switched: S.switched };
    S.lastUser = null; S.lastTurn = null; S.lastAt = 0;
    S.prependAt = anchor; S.replay = true;
    try { for (const it of items) if (it && typeof it.type === "string" && it.type !== "thread.reset" && it.type !== "thread.append") receive(Object.assign({ v: V }, it)); }
    finally { S.replay = false; S.prependAt = null; }
    flushRender();
    Object.assign(S, keep);
    for (const t of S.turns.values()) if (t.running && t.id !== S.lastTurn) { S.replay = true; try { onTurnEnd({ turnId: t.id, stopReason: "cancelled", interrupted: true }); } finally { S.replay = false; } }
    updateView(); renderDock(true);
    if (S.atBottom) scroller.scrollTop = scroller.scrollHeight;
    publishSize();
  }
  // Q's journeys: the faces load when first used (font-display: block = text hidden meanwhile); ask for all of them now,
  // so no opening waits for a face (a code block's mono, a title's medium)
  try { for (const f of ["400 13px 'Inter Display'", "500 13px 'Inter Display'", "600 13px 'Inter Display'", "400 12px 'JetBrains Mono'"]) document.fonts.load(f).catch(() => {}); } catch (e) { /* no FontFaceSet */ }
  function resetThread() {
    resetGen++;
    closeChanges();
    S.usage = null; S.usageWarned = false; renderGauge();
    S.batches.clear();
    clear(log); S.users.clear(); S.turns.clear(); S.tools.clear(); S.perms = []; S.lastUser = null; S.lastTurn = null; S.lastAt = 0; S.planPending = null; S.editing = null;
    S.atBottom = true; S.newCount = 0; newSteps.hidden = true;
    S.berr = null; closeView(true); bTipHide();
    tick();
  }
  function applyLayout(changed) {
    const b = BUB(), root = document.documentElement;
    root.classList.toggle("bub", b);
    root.classList.toggle("bub-up", b && S.lay.side === "above");
    if (b) {
      app.setAttribute("role", "dialog"); app.setAttribute("aria-modal", "false"); app.setAttribute("tabindex", "-1");
      app.setAttribute("aria-label", S.lay.element ? "Commentaire sur " + S.lay.element : "Commentaire");
      root.style.setProperty("--bw", S.lay.maxW + "px");
      menuLayer.appendChild(sendTip); sendTip.classList.add("ftip");   // its bubble may leave the surface (R3)
      app.style.setProperty("--b-origin", ({ left: "right top", above: "left bottom", inside: "right top" })[S.lay.side] || "left top");
      // R18: the slide comes from the element's side (the bubble is right of it: it comes from the left, and so on)
      const from = ({ right: [-12, 0], left: [12, 0], below: [0, -12], above: [0, 12], inside: [-12, 0] })[S.lay.side] || [-12, 0];
      app.style.setProperty("--b-dx", from[0] + "px"); app.style.setProperty("--b-dy", from[1] + "px");
    } else {
      for (const a of ["role", "aria-modal", "aria-label", "tabindex"]) app.removeAttribute(a);
      // GRAVE (Q 0.3.546): a real closing (bubble.hidden: inert, aria-hidden, b-gone) before « Chat à droite » left the panel dead
      app.classList.remove("b-in", "b-out", "b-gone", "faded", "view", "running", "has-thread");
      app.inert = false; app.removeAttribute("aria-hidden");
      body.hidden = false; ruleEl.hidden = true; lastSize = "";
      if (sendTip.parentNode !== sendWrap) { sendWrap.appendChild(sendTip); sendTip.classList.remove("ftip", "on"); }
    }
    if (!changed) return;
    closeMenu(); if (S.historyOpen) toggleHistory(false);
    resetThread();                                           // a `thread.reset` follows: the thread is drawn again in the new layout
    renderHeader(); renderComposerBar(); updateView(); renderDock(true);
  }
  // Opening: from the element's side (transform-origin), 180 ms. Closing (Échap): the host is told at once — it hides the
  // bubble, the agent keeps working — and the surface goes in 120 ms; it stays invisible until the next `layout` / `focus`.
  function bOpen() {
    app.classList.remove("b-out", "b-in", "b-gone"); app.inert = false; app.removeAttribute("aria-hidden");
    if (!S.replay) { void app.offsetWidth; app.classList.add("b-in"); }
    requestAnimationFrame(() => { if (BUB() && !S.perms.length && !S.bview && !S.st.running) cin.focus(); });
  }
  app.addEventListener("animationend", (e) => { if (e.target === app) app.classList.remove("b-in"); });
  // The surface never scrolls: a focus landing on something the window does not show yet (it is being resized) must not shift it.
  app.addEventListener("scroll", () => { if (BUB() && (app.scrollTop || app.scrollLeft)) { app.scrollTop = 0; app.scrollLeft = 0; } });
  document.addEventListener("scroll", () => { if (BUB() && (window.scrollX || window.scrollY)) window.scrollTo(0, 0); });
  function bClose() { chipHover(null, false); post("close"); app.classList.remove("b-in"); app.classList.add("b-out"); app.inert = true; app.setAttribute("aria-hidden", "true"); }

  function renderDockB(force) {
    const p = S.perms[0], s = activeStrip(), running = !!S.st.running, none = !log.firstChild;
    const err = !running && !p ? S.berr : null;
    const off = !!(S.st.disabledReason || (s && s.tone === "danger"));
    const key = JSON.stringify(["b", p && p.requestId, S.perms.length, s, S.queue, S.st.disabledReason, running, err && err.id, !!S.bview, none, S.outbox && S.outbox.shown && S.outbox.text]);
    if (!force && key === lastDockKey) { composer.classList.toggle("disabled", off); updateSend(); return; }
    lastDockKey = key;
    const a = document.activeElement;
    const mine = composer.contains(a) || a === app || a === document.body || !a;
    clear(dock);
    dock.className = "dock" + (p ? " has-perm" : "");
    app.classList.toggle("running", running && !p);
    if (p) { dock.appendChild(bPerm(p, composer.contains(a))); ruleEl.hidden = none || !!S.bview; return; }
    if (err) {
      // H1 (monkey seed 1 #29): the input used to leave the page here — whatever was typed next went nowhere until
      // « Annuler ». It stays in the page, out of sight (Paper 1AAE-0 draws the two buttons alone), and keeps the keyboard:
      // the first character written brings it back (errGhostEnd). After an error the user can always write and send.
      dock.classList.add("err-ghost"); dock.append(composer, bErrFoot(err)); ruleEl.hidden = true;
      composer.classList.toggle("disabled", off);
      cin.setAttribute("contenteditable", S.st.disabledReason ? "false" : "true");
      cin.setAttribute("aria-disabled", S.st.disabledReason ? "true" : "false");
      if (mine && document.hasFocus()) dockFocusSoon();
      updateSend(); return;
    }
    for (const q of S.queue) dock.appendChild(bQueued(q));
    if (S.outbox && S.outbox.shown) dock.appendChild(bQueued({ text: S.outbox.text, local: true }));
    if (s) dock.appendChild(stripNode(s));
    dock.appendChild(composer);
    composer.classList.toggle("disabled", off);
    cin.setAttribute("contenteditable", S.st.disabledReason ? "false" : "true");
    cin.setAttribute("aria-disabled", S.st.disabledReason ? "true" : "false");
    ruleEl.hidden = running || (none && !S.bview);
    // no input during a turn: the focus waits on the bubble itself (never on « Arrêter »: Entrée must not stop the agent)
    if (mine && document.hasFocus()) dockFocusSoon();
    updateSend();
  }
  // Q's journeys: focus() lays the page out; called by each dock drawing of an opening (thread, turn ends, state) it laid the
  // whole thread out several times before the first image. One focus, at the image, with what is true then.
  let dockFocusPending = false;
  function dockFocusSoon() {
    if (dockFocusPending) return;
    dockFocusPending = true;
    requestAnimationFrame(() => {
      dockFocusPending = false;
      const a = document.activeElement;
      if (!BUB() || !document.hasFocus() || S.perms.length || !(composer.contains(a) || a === app || a === document.body || !a)) return;
      if (S.st.running) { if (a !== app) app.focus({ preventScroll: true }); } else if (!S.bview && composer.isConnected && a !== cin) cin.focus();
    });
  }
  function permRespond(p, opt, message) {
    if (!opt) return;
    post("permission.respond", Object.assign({ requestId: p.requestId, optionId: opt.optionId }, message ? { message } : {}));
    S.perms.shift();
    closeView(true);
    refreshWaiting(); updateView(); renderDock(true);
    if (S.perms.length) { const b = dock.querySelector(".bperm"); if (b) b.focus(); }   // SEC-23: the next card, never its « Autoriser »
    else if (S.st.running) app.focus({ preventScroll: true }); else cin.focus();
  }
  // Paper 1ANU-0: the request sits in the bubble, in place of the input.
  function bPerm(p, typing) {
    const allowOnce = p.options.find((o) => o.kind === "allow_once");
    const allowAlways = p.options.find((o) => o.kind === "allow_always");
    const reject = p.options.find((o) => o.kind === "reject_once") || p.options.find((o) => /^reject/.test(o.kind));
    const detail = !!(p.diff || p.command || p.details);
    const kids = [h("span", { class: "t", id: "perm-t", text: p.title }),
      S.perms.length > 1 ? h("span", { class: "perm-of", text: "1 sur " + S.perms.length }) : null,
      p.diff ? h("span", { class: "num plus", text: "+" + p.diff.added }) : null, p.diff ? h("span", { class: "num minus", text: "−" + p.diff.removed }) : null,
      detail ? icon("chevron", "cv") : null];
    const file = detail ? h("button", { class: "bperm-file", type: "button", "aria-haspopup": "dialog", onclick: () => openView({ kind: "perm", p }) }, kids) : h("div", { class: "bperm-file" }, kids);
    const instead = h("input", { class: "instead", type: "text", placeholder: "Dire à " + curAgentName().replace(/ Code$/, "") + " quoi faire à la place…",
                                 "aria-label": "Dire à l'agent quoi faire à la place", maxlength: "4000" });
    const sw = allowAlways ? h("button", { class: "always", type: "button", role: "switch", "aria-checked": String(!!p.always), "aria-label": "Auto : toujours autoriser ce type de demande",
                                           onclick: () => { p.always = !p.always; sw.setAttribute("aria-checked", String(p.always)); } },
                                h("span", { class: "sw", "aria-hidden": "true" }, h("i")), h("span", { text: "Auto." })) : h("span", { class: "always" });
    const refuse = h("button", { class: "btn", type: "button", onclick: () => permRespond(p, reject, instead.value.trim() || undefined) }, "Refuser");
    const allow = h("button", { class: "btn primary", type: "button", "aria-keyshortcuts": "Enter", onclick: (e) => { if (permArmed(card, e)) permRespond(p, p.always && allowAlways ? allowAlways : allowOnce); } }, "Autoriser");
    instead.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.isComposing) { e.preventDefault(); if (instead.value.trim()) permRespond(p, reject, instead.value.trim()); else if (permArmed(card, e)) allow.click(); }
    });
    const card = h("div", { class: "bperm" + (S.replay || p.shown ? "" : " pop-in"), role: "alertdialog", "aria-modal": "false", "aria-labelledby": "perm-t", tabindex: "-1" },
      h("div", { class: "bperm-rows" }, file, reject ? h("label", { class: "bperm-in" }, icon("pencilFill", "ic"), instead) : null),
      h("div", { class: "bperm-ft" }, sw, reject ? refuse : null, allow));   // F10
    card.__shownAt = p.shownAt || (p.shownAt = performance.now());   // SEC-23: armed from its first appearance (a redraw of the same request does not re-arm)
    card.addEventListener("keydown", (e) => {
      if ((e.key === "Enter" || e.key === " ") && e.repeat && e.target !== instead) e.preventDefault();   // SEC-23: a held key answers nothing
      else if (e.key === "Enter" && e.target !== instead && e.target.tagName !== "BUTTON") { e.preventDefault(); if (permArmed(card, e)) allow.click(); } });
    if (!p.shown) requestAnimationFrame(() => { if (!typing && card.isConnected) card.focus(); });   // SEC-23: never on « Autoriser »
    p.shown = true;
    return card;
  }
  // H1 (monkey #29): the input is in the page behind « Annuler / Réessayer », out of sight (renderDockB). The first thing
  // written brings it back — as « Annuler » does — with what was written and the caret at its end.
  const errGhost = () => BUB() && dock.classList.contains("err-ghost");
  function errGhostEnd() {
    S.berr = null; renderDock(true); placeholder(); cin.focus();
    const s = window.getSelection(); if (s) { s.selectAllChildren(cin); s.collapseToEnd(); }
  }
  // Paper 1AAE-0: after an error, the two ways out. « Annuler » is local (the input comes back); « Réessayer » asks the host.
  function bErrFoot(err) {
    const act = err.action && err.action.id !== "retry" ? err.action : null;
    const cancel = h("button", { class: "btn", type: "button", onclick: () => { S.berr = null; renderDock(true); placeholder(); cin.focus(); } }, "Annuler");
    const retry = h("button", { class: "btn danger", type: "button", onclick: () => {
      S.berr = null;
      if (act) post("error.action", { errorId: err.id, actionId: act.id }); else post("retry", err.turnId ? { turnId: err.turnId } : {});
      renderDock(true); placeholder(); } }, act ? act.label : "Réessayer");
    return h("div", { class: "b-errft", role: "group", "aria-label": "Après l'erreur" }, cancel, retry);
  }
  // Monkey 3 #63/#70: what the bubble sent stays seen (dimmed, like a waiting message) until the host takes it — its echo
  // (message.user) or its queue (§19.6, which then shows it with « Commencera après… » and « × »). Shown only after 150 ms:
  // an ordinary send is echoed sooner and never flickers. Ends with the echo, the queue that holds it, an error, another element.
  let outboxTimer = 0;
  function outboxSet(text) {
    clearTimeout(outboxTimer);
    S.outbox = { text, shown: false };
    outboxTimer = setTimeout(() => { if (S.outbox) { S.outbox.shown = true; renderDock(); } }, 150);
  }
  function outboxEnd(text) {
    if (!S.outbox || (text != null && text.trim() !== S.outbox.text.trim())) return;
    clearTimeout(outboxTimer); const was = S.outbox.shown; S.outbox = null;
    if (was) renderDock();
  }
  // One agent turn at a time: a request written in another element's bubble waits (B 1.8 `queue.items[].after`).
  function bQueued(q) {
    if (q.local) return h("div", { class: "bq", role: "group", "aria-label": "Message envoyé" }, h("div", { class: "bubble pending", text: q.text }));
    return h("div", { class: "bq", role: "group", "aria-label": "Message en attente" },
      h("div", { class: "bubble pending", text: q.text }),
      h("div", { class: "bq-cap" },
        // H1 c342b23: « held » = kept the time the element's image gets ready, then sent
        h("span", { class: "tx" }, q.after ? ["Commencera après le travail en cours sur ", h("span", { class: "el", text: q.after })] : q.id === "held" ? "Envoi dans un instant…" : "En file : commencera après le travail en cours"),
        h("button", { class: "icon-btn", type: "button", "aria-label": "Retirer ce message de la file", onclick: () => post("queue.remove", { id: q.id }) }, icon("close"))));
  }

  // The diff « en grand » (Paper 19ON-0 for a request, 19IO-0 for a finished turn): it takes the bubble, the thread waits.
  function openView(v) {
    closeMenu(); bTipHide();
    S.viewBase = { w: S.lay.maxW, h: S.pendH || Math.ceil(bubbleNatural()) }; S.bsize = null;   // R40: the size of before
    S.bview = v;
    app.classList.add("view"); gripEl.hidden = !BUB();
    if (v.kind === "changes") post("checkpoint.viewChanges", { turnId: v.t.id });
    renderView();
    updateView(); renderDock(true); publishSize();
    const x = bviewEl.querySelector(".bview-x"); if (x) x.focus();
    say("Affiché en grand. Échap pour revenir.");
  }
  function closeView(silent) {
    const v = S.bview;
    if (!v) return;
    S.bview = null; app.classList.remove("view"); gripEl.hidden = true; clear(bviewEl); bviewEl.hidden = true;
    const wasResized = !!S.bsize; S.bsize = null; S.viewBase = null;   // R40: closing gives the size of before back
    if (wasResized) { document.documentElement.style.setProperty("--bw", S.lay.maxW + "px"); lastSize = ""; }
    if (silent) return;
    updateView(); renderDock(true); publishSize();
    const back = v.kind === "perm" ? dock.querySelector(".bperm-file") : v.t.barEl && v.t.barEl.querySelector(".bt-bar-main");
    if (back && back.isConnected) back.focus(); else if (!S.st.running) cin.focus();
    S.atBottom = true; requestAnimationFrame(() => { scroller.scrollTop = scroller.scrollHeight; });
  }
  function diffLines(box, d) {
    for (const l of d.lines) {
      const cls = l.op === "+" ? "add" : l.op === "-" ? "del" : l.op === "@" ? "hunk" : "";
      box.appendChild(h("div", { class: "l " + cls }, l.op === "+" ? h("span", { class: "sr-only", text: "ajouté : " }) : l.op === "-" ? h("span", { class: "sr-only", text: "retiré : " }) : null,
        (l.op === "@" ? "" : (l.op === " " ? " " : l.op) + "  ") + l.text));
    }
    if (d.truncated) box.appendChild(h("div", { class: "l hunk", text: "… aperçu tronqué" }));
  }
  const turnDiff = (t, c) => (t.fullDiffs && t.fullDiffs.get(c.path)) || validDiff(c.diff, 4000);
  function changesText(t) {
    const out = [];
    for (const c of t.changes || []) { const d = turnDiff(t, c); if (d) out.push((t.changes.length > 1 ? "--- " + c.path + "\n" : "") + diffText(d)); }
    return out.length ? out.join("\n\n") : t.copyText || "";
  }
  function bCopy(get, label, cls) {
    const b = h("button", { class: cls, type: "button", "aria-label": label }, icon("copy"), h("span", { text: "Copier" }));
    b.addEventListener("click", () => { lastCopy = b; post("copy", { text: get() }); });
    return b;
  }
  function renderView() {
    const v = S.bview;
    if (!v) return;
    const hadFocus = bviewEl.contains(document.activeElement);
    clear(bviewEl); bviewEl.hidden = false;
    const box = h("div", { class: "bview-body", role: "region", tabindex: "0" });
    let title, added = null, removed = null, lead = null, copy = null;
    if (v.kind === "perm") {
      const p = v.p;
      title = p.title;
      if (p.diff) { added = p.diff.added; removed = p.diff.removed; diffLines(box, p.diff); }
      if (p.command) box.appendChild(h("pre", { class: "bview-pre", text: p.command }));
      if (p.details) box.appendChild(h("pre", { class: "bview-pre dim", text: p.details }));
      box.setAttribute("aria-label", p.diff ? "Aperçu du changement dans " + p.diff.path : "Détail de la demande");
    } else {
      const t = v.t, files = t.changes;
      added = files.reduce((n, c) => n + (c.added || 0), 0); removed = files.reduce((n, c) => n + (c.removed || 0), 0);
      title = files.length === 1 ? baseName(files[0].path) : plural(files.length, "fichier modifié", "fichiers modifiés");
      lead = icon("code", "ic");
      for (const c of files) {
        if (files.length > 1) box.appendChild(h("div", { class: "l file", text: c.path }));
        const d = turnDiff(t, c);
        if (d) diffLines(box, d); else box.appendChild(h("div", { class: "l hunk", text: t.fullDiffs ? "Pas d'aperçu pour ce fichier." : "Chargement du détail…" }));
      }
      box.setAttribute("aria-label", "Différences dans " + title);
      copy = bCopy(() => changesText(t), "Copier les changements", "bview-copy");
    }
    bviewEl.setAttribute("aria-label", title);
    bviewEl.append(
      h("div", { class: "bview-hd" }, lead, h("span", { class: "t", text: title }),
        added != null ? h("span", { class: "nums" }, h("span", { class: "num plus", text: "+" + added }), h("span", { class: "num minus", text: "−" + removed })) : null,
        h("button", { class: "bview-x", type: "button", "aria-label": "Fermer l'aperçu", "aria-keyshortcuts": "Escape", onclick: () => closeView() }, icon("x"))),
      h("div", { class: "bview-wrap" }, box, copy));
    if (hadFocus) { const x = bviewEl.querySelector(".bview-x"); if (x) x.focus(); }
  }

  // Hover bubble of a turn's steps (Paper 1AIE-0 while it works; the same list under « Historique » once it is done).
  // It opens inside the bubble: above its line when there is room, else under it; right edges aligned.
  let btipFor = null, btipPinned = false;
  function bTipRows(steps) {
    const rows = [];
    for (const x of steps) {
      if (x.say) { rows.push({ icon: "check", text: x.say, cls: "say" }); continue; }
      const failed = x.status === "failed";
      if (!failed && !x.summary && TOOL_RE.test(x.title || "")) {      // raw MCP calls count together: « A utilisé 2 outils MCP »
        const last = rows[rows.length - 1];
        if (last && last.mcp) last.mcp++; else rows.push({ mcp: 1 });
      } else rows.push({ icon: failed ? "x" : "check", text: stepTitle(x) + (failed ? " (échec)" : ""), cls: failed ? "fail" : "" });
    }
    const out = rows.map((r) => (r.mcp ? { icon: "plug", text: "A utilisé " + plural(r.mcp, "outil MCP", "outils MCP"), cls: "mcp" } : r));
    return out.length > 6 ? [{ text: plural(out.length - 5, "étape", "étapes") + " avant", cls: "more" }].concat(out.slice(-5)) : out;
  }
  // B 1.9 (audit D4, Paper 1B30-0 / 1AIE-0): a step is always said in French, with the file's name alone — « Lit
  // checkout.css… » while it runs, « A lu checkout.css » once done — never the agent's raw title (« Edit styles\landing.css »).
  // Order: the host's `summary` (verb + object); else `kind` + the name of `locations[0]`; a raw MCP call; the title last
  // (humanised, « … » while it runs) only when the kind says nothing.
  const PRESENT = { "a lu": "Lit", "a modifié": "Modifie", "a supprimé": "Supprime", "a déplacé": "Déplace", "a cherché": "Cherche", "a lancé": "Lance",
    "a exécuté": "Exécute", "a consulté": "Consulte", "a marqué": "Marque", "a utilisé": "Utilise", "a réfléchi": "Réfléchit", "a créé": "Crée", "a écrit": "Écrit" };
  const KIND_VERB = { read: "a lu", edit: "a modifié", delete: "a supprimé", move: "a déplacé", search: "a cherché", execute: "a lancé", fetch: "a consulté" };
  const KIND_NONE = { search: "dans le projet", execute: "une commande", fetch: "une page" };
  function stepLabel(x, running) {
    const say = (verb, object, after) => {
      const v = running ? PRESENT[verb] || cap(verb) : cap(verb);
      return v + (object ? " " + object : "") + (after ? " " + after : "") + (running ? "…" : "");
    };
    if (x.summary) return say(x.summary.verb, x.summary.object, x.summary.after);
    const raw = String(x.title || "").replace(/[`"]/g, "").trim();
    // no `locations`: the file named in the title (« Edit styles\landing.css » → landing.css)
    const named = /(?:^|[\s(])((?:[^\s"'`()]*[\\/])?[\w@.-]+\.[A-Za-z][A-Za-z0-9]{0,7})(?=$|[\s:,)…])/.exec(raw);
    const file = (x.locations || []).map((l) => l && l.path).find((p) => typeof p === "string" && p) || (named && !/^mcp__/.test(named[1]) ? named[1] : "");
    if (TOOL_RE.test(raw)) {
      const t = parseTool(raw);
      return running ? "Utilise l'outil « " + toolLabel(t.tool, t.server) + " »…" : "A utilisé 1 outil MCP";
    }
    if (x.kind === "think") return running ? "Réfléchit…" : "A réfléchi";
    if (KIND_VERB[x.kind]) {
      if (file) return say(KIND_VERB[x.kind], baseName(file));
      if (x.kind === "execute") {   // the title of an execute step is its command line: kept when short
        const cmd = raw.replace(/^(run|execute|exécute|lance|a lancé|a exécuté)\s+/i, "").replace(/…$/, "");
        return say("a lancé", cmd && cmd.length <= 40 && !/\n/.test(cmd) ? cmd : "une commande");
      }
      return say(KIND_VERB[x.kind], KIND_NONE[x.kind] || "");
    }
    const t = humanizeTools(raw) || (file && baseName(file)) || "Étape";
    return running && !/…$/.test(t) ? t + "…" : t;
  }
  const isRunning = (x) => x.status === "in_progress" || x.status === "pending";
  const stepTitle = (x) => stepLabel(x, isRunning(x));
  function bTipShow(el) {
    const rows = el._rows ? el._rows() : [];
    if (!rows.length) { if (btipFor === el) bTipHide(); return; }
    clear(btipEl);
    for (const r of rows) btipEl.appendChild(h("div", { class: "r" + (r.cls ? " " + r.cls : "") }, r.icon ? icon(r.icon) : null, h("span", { text: r.text })));
    const fresh = btipFor !== el;
    if (btipFor && fresh) btipFor.removeAttribute("aria-describedby");
    btipFor = el; btipEl.hidden = false; el.setAttribute("aria-describedby", "b-tip");
    bTipPlace();
    if (fresh) { btipEl.classList.remove("on"); void btipEl.offsetWidth; btipEl.classList.add("on"); }
  }
  // Paper 1AIE-0: the list sits right above its step, left edges aligned, 2 px apart, 178 px wide — it may leave the
  // bubble (7 px above it and 38 px right of it on the board). Under the step when the host cannot give the room above.
  function bTipPlace() {
    const el = btipFor;
    if (!el || !el.isConnected) { bTipHide(); return; }
    if (!BUB()) return;
    floatSet(btipEl, (lim, H) => {
      const ar = app.getBoundingClientRect(), r = el.getBoundingClientRect(), W = S.lay.maxW;
      btipEl.style.maxHeight = "";
      const tw = btipEl.offsetWidth, th = btipEl.offsetHeight;
      const x = Math.max(-lim.left, Math.min(r.left - ar.left, W + lim.right - tw));
      let y = r.top - ar.top - 2 - th, up = true;
      if (y < -lim.top) {
        const under = r.bottom - ar.top + 2;
        if (under + th <= H + lim.bottom) { y = under; up = false; }
        else { y = Math.max(-lim.top, 0); btipEl.style.maxHeight = Math.max(40, r.top - ar.top - 2 - y) + "px"; }
      }
      btipEl.style.top = Math.round(y) + "px"; btipEl.style.left = Math.round(x) + "px";
      btipEl.classList.toggle("up", up);
      return { x, y, w: tw, h: btipEl.offsetHeight, pad: SHADOW.none };
    });
  }
  function bTipHide() {
    if (btipFor) btipFor.removeAttribute("aria-describedby");
    btipFor = null; btipPinned = false; btipEl.classList.remove("on"); btipEl.hidden = true; floatDrop(btipEl);
  }
  function bTipBind(el, rows) {
    el._rows = rows;
    el._tip = (on) => { if (on) bTipShow(el); else if (!btipPinned) bTipHide(); };
    withTip(el);
    el.addEventListener("focus", () => { if (el.matches(":focus-visible")) bTipShow(el); });
    el.addEventListener("blur", () => { if (btipFor === el) bTipHide(); });
    // a click (or Entrée / Espace) pins it: the path for touch and keyboard
    el.addEventListener("click", () => { if (btipFor === el && btipPinned) bTipHide(); else { bTipShow(el); btipPinned = btipFor === el; } });
  }
  // Right of « modèle · effort »: the step in progress, « S'est arrêté », or « Historique ».
  function bStatus(t) {
    if (!t.b) return;
    const steps = [];
    for (const s of t.segs) if (s.kind === "steps") steps.push(...s.tools);
    let kind = "", label = "", rows = [];
    if (t.running) {
      const cur = steps.slice().reverse().find((x) => x.status === "in_progress" || x.status === "pending");
      label = cur ? stepTitle(cur) : S.perms.some((p) => p.turnId === t.id) ? "Attend votre accord" : t.segs.some((s) => s.kind === "thought" && !s.done) ? "Réfléchit…" : "Travaille…";
      rows = bTipRows(steps.filter((x) => x !== cur && x.status !== "in_progress" && x.status !== "pending"));
      kind = "run";
    } else if (t.stop === "cancelled") { kind = "stop"; label = "S'est arrêté"; }
    else if (t.stop !== "error" && steps.length) {
      kind = "done"; label = "Historique";
      const hist = [];   // R43: the announcements join the list, in their order
      for (const s of t.segs) { if (s.kind === "text" && s.closed) hist.push({ say: plain(s.text) }); else if (s.kind === "steps") hist.push(...s.tools); }
      rows = bTipRows(hist);
    }
    t.stRows = rows;
    // B 1.9 (R8, Paper 1B30-0): until the agent's first words arrive, the step in progress sits where its message will be,
    // under « modèle · effort », like a message — then the words take that place and the step moves right of the model.
    const hasText = t.segs.some((s) => s.kind === "text");
    const inBody = kind === "run" && !hasText;
    if (inBody && !t.pendEl) t.pendEl = h("div", { class: "bt-pend" });
    if (inBody && t.pendEl.parentNode !== t.textBox) t.textBox.prepend(t.pendEl);
    if (!inBody && t.pendEl) t.pendEl.remove();
    t.textBox.hidden = !inBody && !hasText;
    bCards(t);
    const home = inBody ? t.pendEl : t.statusEl;
    const live = rows.length > 0 && kind !== "stop", sig = kind + "|" + live + "|" + inBody;
    if (t.stSig !== sig) {
      t.stSig = sig;
      if (btipFor && t.stBtn && t.stBtn.contains(btipFor)) bTipHide();
      clear(t.statusEl); if (t.pendEl) clear(t.pendEl); t.stBtn = null; t.stLb = null;
      if (!kind) return;
      const ic = kind === "run" ? h("span", { class: "spin", style: "display:flex" }, icon("spinnerB")) : icon(kind === "stop" ? "prohibit" : "history");
      t.stLb = h("span", { class: "lb" });
      t.stBtn = h(live ? "button" : "span", Object.assign({ class: "bt-st " + kind }, live ? { type: "button" } : {}, live && kind === "done" ? { "aria-label": "Historique des actions de ce tour" } : {}), ic, t.stLb);
      if (live) bTipBind(t.stBtn, () => t.stRows);
      home.appendChild(t.stBtn);
    }
    if (t.stLb && t.stLb.textContent !== label) { t.stLb.textContent = label; }
    if (btipFor && btipFor === t.stBtn) bTipShow(t.stBtn);       // the list follows the steps while it is open
  }
  // R17 (Paper 1AZV-0): the agent's cards are a group, 2 px apart; the corners follow the place in the group — the first
  // 4/10/4/4, the middle ones 4, the result bar last (4/4/10/10); a lone card 4/12/16/12; a closing card 4/4/16/12.
  function bCards(t) {
    const cards = Array.from(t.el.querySelectorAll(":scope > .bt-text")).filter((c) => !c.hidden && !c.classList.contains("bt-follow") && !c.classList.contains("fl-closing"));
    const n = cards.length, bar = !!t.barEl;
    cards.forEach((c, i) => {
      const first = i === 0, last = i === n - 1 && !bar;
      c.classList.toggle("c-top", first && !last);
      c.classList.toggle("c-mid", !first && !last);
      c.classList.toggle("c-end", !first && last);
    });
  }
  // Paper 1AWI-0: « 1 fichier modifié +6 −2 › » opens the diff « en grand »; « Copier » copies it.
  function bBar(t) {
    const files = t.changes;
    const added = files.reduce((n, c) => n + (c.added || 0), 0), removed = files.reduce((n, c) => n + (c.removed || 0), 0);
    const label = plural(files.length, "fichier modifié", "fichiers modifiés");
    const open = h("button", { class: "bt-bar-main", type: "button", "aria-haspopup": "dialog", onclick: () => openView({ kind: "changes", t }) },
      icon("code", "ic"), h("span", { class: "name", text: label }),
      h("span", { class: "num plus", text: "+" + added }), h("span", { class: "num minus", text: "−" + removed }), icon("chevron", "cv"));
    return h("div", { class: "bt-bar", role: "group", "aria-label": "Changements de ce tour" }, open, bCopy(() => changesText(t), "Copier les changements", "bt-copy"));
  }
  // Paper 1AAE-0 / 1A6L-0: the agent's text turns red, one line says an error happened, « Plus d'infos » unfolds the
  // technical line (fixed-width) and the plain explanation — what happened, what changed or not, what to do.
  function bError(m) {
    let t = (m.turnId && S.turns.get(m.turnId)) || (S.lastTurn && S.turns.get(S.lastTurn)) || null;
    const retryId = t && !t.pseudo ? t.id : null;            // the turn « Réessayer » relaunches
    if (!t || t.err) { t = newTurn("e:" + m.id); t.pseudo = true; t.running = false; t.stop = "error"; t.el.setAttribute("aria-busy", "false"); tick(); }
    t.err = m.id; t.el.classList.add("b-err");
    const id = "errd-" + m.id.replace(/[^\w-]/g, "");
    const title = (m.title || "").trim(), msg = (m.message || "").trim();
    const plainText = [title && msg ? title + (/[.!?…»:]$/.test(title) ? "" : ".") : title, msg].filter(Boolean).join(" ");
    const code = isStr(m.detail, 4000) && m.detail.trim() ? m.detail.trim().split("\n").slice(0, 4).join("\n") : "";
    const det = h("div", { class: "bt-errd", id, hidden: true }, h("div", { class: "tx" }, code ? [h("span", { class: "ecode", text: code }), h("br")] : null, plainText));
    const more = h("button", { class: "bt-more", type: "button", "aria-expanded": "false", "aria-controls": id }, h("span", { text: "Plus d'infos" }), icon("caretRight", "cv"));
    more.addEventListener("click", () => {
      const openNow = det.hidden;
      det.hidden = !openNow; more.setAttribute("aria-expanded", String(openNow)); t.el.classList.toggle("b-err-open", openNow);
      stick(); publishSize();
    });
    t.el.append(h("div", { class: "bt-err" }, h("span", { class: "w" }, icon("xCircle"), h("span", { text: "Une erreur est apparue." })), more), det);
    const first = (m.actions || []).find((a) => a && ID.test(a.id) && isStr(a.label, 40));
    S.berr = { id: m.id, turnId: retryId, action: first || null };
    bStatus(t); stick(); renderDock(true);
    noteNew(1);
    shout("Une erreur est apparue. " + plainText);
  }

  // ---------------------------------------------------------------- thread
  function updateView() {
    if (BUB()) {   // no first-run screen, no suggestions: the input, and the thread once there is one
      const none = !log.firstChild;
      first.hidden = true; empty.hidden = true; log.hidden = none; body.hidden = none || !!S.bview;
      app.classList.toggle("has-thread", !none);
      renderDock();
      return;
    }
    body.hidden = false;
    const firstRun = S.st.askFolder === true || (S.st.askFolder === undefined && S.st.folder === null);
    const isEmpty = !log.firstChild;
    first.hidden = !firstRun;
    empty.hidden = firstRun || !isEmpty;
    log.hidden = firstRun || isEmpty;
    if (firstRun) renderFirst();
    else if (isEmpty) renderEmpty();
  }
  function add(node, liveItem) {
    if (!S.replay) node.classList.add("enter", "live");
    if (S.prependAt && S.prependAt.parentNode === log) log.insertBefore(node, S.prependAt); else log.appendChild(node);
    if (liveItem === false || S.replay) node.classList.remove("live");
    if (!empty.hidden || log.hidden) updateView();
    stick();
    return node;
  }
  // Q's journeys: a formatter made per call (toLocale…String) cost ~11 ms at the first opening with a thread (ICU data
  // loaded then); made once, at start
  const FMT_HM = new Intl.DateTimeFormat("fr-FR", { hour: "2-digit", minute: "2-digit" }), FMT_DM = new Intl.DateTimeFormat("fr-FR", { day: "numeric", month: "short" });
  function dayLabel(t) {
    const d = new Date(t), now = new Date();
    const hm = FMT_HM.format(d);
    const sameDay = (a, b) => a.toDateString() === b.toDateString();
    const y = new Date(now); y.setDate(now.getDate() - 1);
    if (sameDay(d, now)) return "Aujourd'hui · " + hm;
    if (sameDay(d, y)) return "Hier · " + hm;
    return FMT_DM.format(d) + " · " + hm;
  }

  function onUser(m) {
    outboxEnd(m.text);   // also in a replay: the host may redraw the thread that already holds it
    const at = Date.parse(m.at) || Date.now();
    if (!S.lastAt || new Date(at).toDateString() !== new Date(S.lastAt).toDateString() || at - S.lastAt > 30 * 60 * 1000) add(h("div", { class: "date", text: dayLabel(at) }), false);
    S.lastAt = at;
    const ann = validBatchMessage(m.annotations);
    if (ann) { onAnnotationMessage(m, ann); return; }
    const els = (m.elements || []).filter(validElement);
    const bubble = h("div", { class: "bubble" });
    const inlineIds = fillWithChips(bubble, m.text, BUB() ? [] : els);   // B 1.9 (R7): in the bubble the element is implicit — no chip
    const pencil = m.editable === false || BUB() ? null : h("button", { class: "icon-btn", type: "button", "aria-label": "Modifier ce message", onclick: () => startEdit(m.id) }, icon("pencil"));
    const row = h("div", { class: "um-row" }, h("div", { class: "um-tools" }, pencil), bubble);
    const under = h("div", { class: "um-attach" });
    if (!BUB()) for (const e of els) if (!inlineIds.has(e.id)) under.appendChild(chipNode(Object.assign({ picked: true }, e), false));
    for (const a of (m.attachments || []).filter(validAttachment)) under.appendChild(h("div", { class: "thumb" }, h("img", { src: a.thumb, alt: a.name, style: "width:100%;height:100%;object-fit:cover;border-radius:5px;display:block" })));
    const node = h("div", { class: "um", "data-id": m.id }, row, under.firstChild ? under : null);
    S.users.set(m.id, { el: node, row, bubble, text: m.text, els, m });
    S.lastUser = m.id;
    add(node);
    if (!S.replay) { scrollToEnd(false); }
    if (BUB()) { S.berr = null; renderDock(); }   // a new request: the « Annuler / Réessayer » of an earlier error goes
  }
  function onKept(m) {
    const els = (m.elements || []).filter(validElement);
    const n = Array.isArray(m.annotations) ? m.annotations.length : els.length;
    const words = (() => { let w = m.text; for (const e of els) w = w.split(e.label).join(""); return w.replace(/\s+/g, " ").trim(); })();
    const where = h("span", { class: "kept-els" });
    for (const e of els) where.appendChild(chipNode(Object.assign({ picked: true }, e), false));
    const node = h("div", { class: "kept", role: "note", "data-id": m.id, "aria-label": "Gardé en annotation : " + words },
      h("div", { class: "kept-note", text: words }),
      h("div", { class: "kept-meta" }, icon("check", "ck"), h("span", { text: n > 1 ? plural(n, "annotation gardée", "annotations gardées") : "Gardé en annotation" }), where),
      h("div", { class: "kept-sub", text: m.partial ? "Une partie n'a pas pu être enregistrée." : "Pour vos agents connectés · rien n'est envoyé au chat" }));
    add(node);
    if (!S.replay) { scrollToEnd(false); say("Gardé en annotation."); }
  }
  // ---------------------------------------------------------------- « Traiter les annotations » (B 1.4)
  function validAnnItem(it) {
    if (!it || !ID.test(it.id) || !isStr(it.note, 2000)) return null;
    return { id: it.id, note: it.note, element: isStr(it.element, 200) ? it.element : "", source: isStr(it.source, 300) ? it.source : "",
             thumb: isStr(it.thumb, 350000) && IMG.test(it.thumb) ? it.thumb : "", status: ["pending", "working", "done", "skipped"].includes(it.status) ? it.status : "pending",
             summary: isStr(it.summary, 600) ? it.summary : "", reason: isStr(it.reason, 600) ? it.reason : "" };
  }
  function validBatchMessage(a) {
    if (!a || typeof a !== "object" || !ID.test(a.batchId) || !Array.isArray(a.items)) return null;
    const items = a.items.slice(0, 50).map(validAnnItem).filter(Boolean);
    return items.length ? { batchId: a.batchId, items, instruction: isStr(a.instruction, 20000) ? a.instruction : "" } : null;
  }
  const annMeta = (it) => [it.element, it.source].filter(Boolean).join(" · ");
  function annThumb(it) {
    return it.thumb ? h("img", { class: "ann-thumb", src: it.thumb, alt: "" }) : h("span", { class: "ann-thumb blank", "aria-hidden": "true" });
  }
  // Composer block: what will be sent, everything checked; unchecking keeps an annotation for later.
  function renderBatch() {
    const b = S.batch;
    clear(batchEl);
    batchEl.hidden = !b;
    placeholder();
    if (!b) { batchEl.removeAttribute("aria-labelledby"); return; }
    batchEl.setAttribute("aria-labelledby", "batch-h");
    const n = batchChecked().length;
    batchEl.appendChild(h("div", { class: "batch-hd" }, icon("tray", "ic"),
      h("span", { class: "t", id: "batch-h", text: n ? plural(n, "annotation à traiter", "annotations à traiter") : "Aucune annotation cochée" }),
      b.siteLabel ? h("span", { class: "site", text: b.siteLabel }) : null,
      h("button", { class: "icon-btn", type: "button", "aria-label": "Retirer ces annotations de la saisie",
                    onclick: () => { S.batch = null; renderBatch(); updateSend(); cin.focus(); } }, icon("close"))));
    const shown = b.expanded || b.items.length <= 5 ? b.items : b.items.slice(0, 5);
    for (const it of shown) {
      const on = b.checked.has(it.id);
      const row = h("button", { class: "batch-row" + (on ? "" : " off"), type: "button", role: "checkbox", "aria-checked": String(on), "data-id": it.id,
                                "aria-label": it.note + (annMeta(it) ? " — " + annMeta(it) : "") },
        h("span", { class: "box", "aria-hidden": "true" }, on ? icon("check") : null), annThumb(it),
        h("span", { class: "tx" }, h("span", { class: "note", text: it.note }), annMeta(it) ? h("span", { class: "meta", text: annMeta(it) }) : null));
      row.addEventListener("click", () => {
        on ? b.checked.delete(it.id) : b.checked.add(it.id);
        renderBatch(); updateSend();
        const again = batchEl.querySelector(".batch-row[data-id='" + it.id + "']"); if (again) again.focus();
        say(on ? "Gardée pour plus tard." : "Ajoutée.");
      });
      batchEl.appendChild(row);
    }
    if (shown.length < b.items.length) batchEl.appendChild(h("button", { class: "batch-more", type: "button", onclick: () => { b.expanded = true; renderBatch(); const r = batchEl.querySelectorAll(".batch-row")[5]; if (r) r.focus(); } },
      "+ " + plural(b.items.length - shown.length, "autre", "autres")));
  }
  function onBatch(m) {
    const items = (Array.isArray(m.items) ? m.items : []).slice(0, 50).map(validAnnItem).filter(Boolean);
    S.batch = items.length ? { batchId: m.batchId, siteLabel: isStr(m.siteLabel, 200) ? m.siteLabel : "", items, checked: new Set(items.map((i) => i.id)), expanded: false } : null;
    renderBatch(); updateSend();
    if (S.batch) { cin.focus(); say(plural(items.length, "annotation prête à être traitée", "annotations prêtes à être traitées") + ". Décochez celles à garder pour plus tard."); }
  }
  // Thread card: the sent batch, kept alive by annotations.status.
  function onAnnotationMessage(m, ann) {
    const card = h("div", { class: "ann-card", role: "group", "aria-labelledby": "annh-" + ann.batchId });
    const node = h("div", { class: "um ann", "data-id": m.id }, card);
    const b = { batchId: ann.batchId, items: ann.items, instruction: ann.instruction, text: m.text || "", card, open: false };
    S.batches.set(ann.batchId, b);
    renderAnnCard(b);
    S.users.set(m.id, { el: node, row: card, bubble: card, text: m.text || "", els: [], m });
    S.lastUser = m.id;
    add(node);
    if (!S.replay) scrollToEnd(false);
  }
  function renderAnnCard(b) {
    const card = b.card;
    const prevFocus = card.contains(document.activeElement) ? document.activeElement.dataset.id || document.activeElement.className : null;
    clear(card);
    const n = b.items.length;
    const done = b.items.filter((i) => i.status === "done").length, skipped = b.items.filter((i) => i.status === "skipped").length;
    const working = b.items.findIndex((i) => i.status === "working");
    const finished = done + skipped === n;
    let right;
    if (finished) right = h("span", { class: "ann-prog" + (skipped ? " mixed" : " ok") }, done ? icon("check") : null,
      [done ? plural(done, "terminée", "terminées") : "", skipped ? plural(skipped, "non traitée", "non traitées") : ""].filter(Boolean).join(" · "));
    else right = h("span", { class: "ann-prog", text: (working >= 0 ? working + 1 : Math.min(n, done + skipped + 1)) + " sur " + n });
    const who = curAgentName().replace(/ Code$/, "");
    card.appendChild(h("div", { class: "ann-hd" }, icon("tray", "ic"), h("span", { class: "t", id: "annh-" + b.batchId, text: "Traiter " + plural(n, "annotation", "annotations") }), right));
    if (b.text.trim()) card.appendChild(h("div", { class: "ann-extra", text: b.text.trim() }));
    for (const it of b.items) {
      const st = it.status;
      const line = st === "working" ? h("span", { class: "ann-st is-working" }, h("span", { class: "spin", style: "display:flex" }, icon("spinner")), who + " s'en occupe")
        : st === "done" ? h("span", { class: "ann-st is-done" }, icon("check"), h("span", { text: it.summary || "Terminée" }))
        : st === "skipped" ? h("span", { class: "ann-st is-skipped" }, icon("x"), h("span", { text: it.reason ? "Non traitée : " + it.reason : who + " n'a pas pu la traiter" }))
        : h("span", { class: "ann-st is-pending" }, h("i", { "aria-hidden": "true" }), "En attente");
      card.appendChild(h("button", { class: "ann-row is-" + st, type: "button", "data-id": it.id, "aria-description": "Montrer sur la page" + (annMeta(it) ? " (" + annMeta(it) + ")" : ""),
                                     onclick: () => post("annotation.show", { id: it.id }) },
        annThumb(it), h("span", { class: "tx" }, h("span", { class: "note", text: it.note }), line)));
    }
    if (b.instruction) {
      const pre = h("pre", { class: "detail", id: "inst-" + b.batchId, hidden: !b.open, text: b.instruction });
      const t = h("button", { class: "ann-inst", type: "button", "aria-expanded": String(b.open), "aria-controls": "inst-" + b.batchId },
        "Voir la consigne envoyée à " + who, icon("chevron", "cv"));
      t.addEventListener("click", () => { b.open = !b.open; renderAnnCard(b); const x = b.card.querySelector(".ann-inst"); if (x) x.focus(); stick(); });
      card.append(t, pre);
    }
    if (prevFocus) { const x = card.querySelector("[data-id='" + prevFocus + "']"); if (x) x.focus(); }
  }
  function onAnnStatus(m) {
    const b = S.batches.get(m.batchId);
    const it = b && b.items.find((i) => i.id === m.id);
    if (!it) return;
    if (["pending", "working", "done", "skipped"].includes(m.status)) it.status = m.status;
    if (isStr(m.summary, 600)) it.summary = m.summary;
    if (isStr(m.reason, 600)) it.reason = m.reason;
    renderAnnCard(b);
    stick();
    const who = curAgentName().replace(/ Code$/, "");
    if (it.status === "working") say(who + " s'occupe de : " + it.note);
    else if (it.status === "done") say("Terminée : " + it.note + (it.summary ? ". " + it.summary : ""));
    else if (it.status === "skipped") say("Non traitée : " + it.note + (it.reason ? ". " + it.reason : ""));
  }

  // Chips sit where their label appears in the text (first occurrence); the rest go under the bubble.
  function fillWithChips(parent, text, els) {
    const used = new Set();
    let rest = text;
    const parts = [];
    while (rest) {
      let best = null;
      for (const e of els) { if (used.has(e.id)) continue; const i = rest.indexOf(e.label); if (i >= 0 && (!best || i < best.i)) best = { i, e }; }
      if (!best) { parts.push(rest); break; }
      if (best.i) parts.push(rest.slice(0, best.i));
      parts.push(best.e); used.add(best.e.id);
      rest = rest.slice(best.i + best.e.label.length);
    }
    for (const p of parts) parent.appendChild(typeof p === "string" ? document.createTextNode(p) : chipNode(Object.assign({ picked: true }, p), false));
    return used;
  }

  function newTurn(id, model, effort, at, agentId) {
    const t = { id, model: model || shownModelLabel((agentId && agentById(agentId)) || curAgent()) || fallbackModel((agentId && agentById(agentId)) || curAgent()).label, effort: effort ? turnEffort(effort) : (curEffort() ? effortName(curEffort().id, curEffort().label) : ""), start: Date.parse(at) || Date.now(),
                running: true, segs: [], userId: S.lastUser, metaEl: h("div", { class: "meta" }), workEl: h("div", { class: "working", "aria-hidden": "true" }), waitEl: null, endEl: null };
    t.el = h("div", { class: "turn", "data-turn": id, "aria-busy": "true" }, t.metaEl, t.workEl);
    t.who = (agentId && agentById(agentId)) || curAgent();
    setMeta(t, [t.model, t.effort].filter(Boolean).join(" · "));
    t.workEl.append("Travaille… ", h("span", { class: "t", text: "0 s" }));
    if (BUB()) {   // Paper 1B64-0: « modèle · effort » and the step in progress on one line, the agent's words in one block under it
      t.b = true;
      t.statusEl = h("span", { class: "bt-stw" });
      t.textBox = h("div", { class: "bt-text", hidden: true });
      const who = t.who;
      t.el = h("div", { class: "turn bt", "data-turn": id, "aria-busy": "true" },
        h("div", { class: "bt-head" }, t.modelEl = h("span", { class: "bt-model" }), t.statusEl),
        t.textBox);
      bModel(t);
      S.berr = null;
      S.turns.set(id, t); S.lastTurn = id;
      add(t.el);
      bStatus(t); tick();
      return t;
    }
    const prev = S.lastTurn && S.turns.get(S.lastTurn);
    const said = S.switched; S.switched = false;
    if (!said && prev && (prev.model !== t.model || prev.effort !== t.effort)) {
      const u = S.users.get(t.userId);
      const mk = h("div", { class: "marker", role: "note", text: "Passé à " + [t.model, t.effort].filter(Boolean).join(" · ") });
      if (u && u.el.isConnected) log.insertBefore(mk, u.el); else add(mk, false);
    }
    S.turns.set(id, t); S.lastTurn = id;
    add(t.el);
    tick();
    return t;
  }
  const turnOf = (id) => S.turns.get(id) || newTurn(id);
  // B 1.9 (R12): the provider's logo left of the model's name, as in the input (14 px in the bubble, 12 px on the panel's 14 px line)
  function setMeta(t, text) {
    clear(t.metaEl).append(agentIcon(t.who, "mlogo"), h("span", { text }));
    t.metaEl.hidden = !text;   // never a logo alone on its line: then it leads « Travaille… » (same line, centred)
    if (t.workEl) { const lead = t.workEl.querySelector(":scope > .mlogo"); if (!text && !lead) t.workEl.prepend(agentIcon(t.who, "mlogo")); else if (text && lead) lead.remove(); }
  }
  function bModel(t) { const who = t.who; clear(t.modelEl).append(agentIcon(who), h("span", { class: "n", text: t.model || fallbackModel(who).label }), t.effort ? h("span", { class: "e", text: t.effort }) : null); }
  // B 1.9: a second `turn.start` with the same turnId, once the agent has said its model and effort, only updates this
  // turn's header (model, effort, agent) — nothing else moves, no « Passé à … » marker.
  function updateTurnHead(t, model, effort, agentId) {
    if (model) t.model = model;
    if (effort != null) t.effort = turnEffort(effort);
    if (agentId && agentById(agentId)) t.who = agentById(agentId);
    if (t.b) { bModel(t); return; }
    setMeta(t, (t.running ? [t.model, t.effort] : [t.model.replace(/\s+[\d.]+$/, ""), t.effort, dur(t.durMs || 0)]).filter(Boolean).join(" · "));
  }
  // R43 (Paper « 5 · Suivi vs réponse », columns B and D): a text of the agent followed by an action in the same turn is an
  // announcement — a grey follow-up line without a card (13/20, #FFFFFF80; a 12 px check, white 40 %, once its action is
  // done; a small ring while it runs). The last text of the turn is the answer (a card). At the end of the turn the lines
  // close softly (height and fade, 250 ms) and join the « Historique » list; only the answer and the changes bar stay.
  function markFollow(t, seg, on) {
    if (!!seg.follow === on) return;
    seg.follow = on;
    if (on) seg.wasFollow = true;
    if (t.b) {
      const box = seg.box;
      if (!box) return;
      box.classList.toggle("bt-follow", on);
      if (on) { seg.ic = h("span", { class: "fl-ic", "aria-hidden": "true" }); box.prepend(seg.ic); }
      else if (seg.ic) { seg.ic.remove(); seg.ic = null; }
      bCards(t);
      return;
    }
    if (on) { seg.wrap = h("div", { class: "follow" + (S.replay ? "" : " enter") }); seg.el.before(seg.wrap); seg.ic = h("span", { class: "fl-ic", "aria-hidden": "true" }); seg.wrap.append(seg.ic, seg.el); }
    else if (seg.wrap) { seg.wrap.before(seg.el); seg.wrap.remove(); seg.wrap = null; seg.ic = null; }
  }
  function refreshFollow(t) {
    t.segs.forEach((seg, i) => {
      if (!seg.follow || !seg.ic) return;
      const after = [];
      for (let j = i + 1; j < t.segs.length && t.segs[j].kind !== "text"; j++) if (t.segs[j].kind === "steps") after.push(...t.segs[j].tools);
      const run = after.some((x) => x.status === "in_progress" || x.status === "pending") || (!after.length && t.running);
      const state = run ? "run" : "done";
      if (seg.icState === state) return;
      seg.icState = state;
      clear(seg.ic).appendChild(run ? h("span", { class: "spin", style: "display:flex" }, icon("spinnerB")) : icon("check"));
      seg.ic.className = "fl-ic " + state;
    });
  }
  function closeFollows(t) {
    const texts = t.segs.filter((x) => x.kind === "text");
    const last = texts[texts.length - 1];
    if (last && last.follow) markFollow(t, last, false);   // the last text of the turn is the answer
    for (const seg of texts) {
      if (!seg.follow) continue;
      const el = t.b ? seg.box : seg.wrap;
      seg.follow = false; seg.closed = true;
      if (!el) continue;
      if (S.replay || isReduced()) { el.remove(); continue; }
      el.style.height = el.offsetHeight + "px"; el.classList.add("fl-closing");
      void el.offsetWidth;
      el.style.height = "0px";
      setTimeout(() => { el.remove(); if (t.b) bCards(t); stick(); publishSize(); }, 270);
    }
    if (t.b) bCards(t);
  }
  function addSeg(t, seg) {
    t.segs.push(seg);
    if (t.b) {   // bubble: the words go in the agent's block; steps and thoughts are not lines, they feed the status and its hover list
      for (const s of t.segs) if (s.kind === "text" && s !== seg) s.el.classList.remove("streaming");   // the caret only while the words are the last thing
      if (seg.kind === "text") {
        const used = t.segs.some((x) => x !== seg && x.kind === "text");
        if (!used) { t.textBox.appendChild(seg.el); seg.box = t.textBox; }
        else {   // R17: each new message of the agent is a new card, right after the last one
          const card = h("div", { class: "bt-text" + (S.replay ? "" : " enter") }, seg.el);
          const cards = t.el.querySelectorAll(":scope > .bt-text");
          cards[cards.length - 1].after(card);
          seg.box = card;
        }
      }
      else if (seg.kind === "plan" || seg.kind === "error") t.el.appendChild(seg.el);
      bStatus(t);
      return seg;
    }
    const before = t.waitEl && t.waitEl.isConnected ? t.waitEl : t.workEl.isConnected ? t.workEl : null;
    t.el.insertBefore(seg.el, before);
    return seg;
  }

  const pendingRender = new Set();
  let renderQueued = false;
  function queueRender(seg) {
    pendingRender.add(seg);
    if (S.replay) { flushRender(); return; }
    if (!renderQueued) { renderQueued = true; requestAnimationFrame(flushRender); }
  }
  function flushRender() {
    renderQueued = false;
    for (const seg of pendingRender) seg.render();
    pendingRender.clear();
    stick();
  }

  function codeBlock(b) {
    const lines = b.code.split("\n");
    const pre = h("pre", { class: "code", text: b.code, tabindex: "0", "aria-label": "Code " + (b.name || b.lang || "") });
    const box = h("div", { class: "card4", role: "group", "aria-label": "Bloc de code " + (b.name || b.lang || "") },
      h("div", { class: "card4-hd" }, h("span", { class: "name", text: b.name || b.lang || "Code" }), h("span", { class: "grow" }), copyButton(() => b.code, "Copier le code")), pre);
    if (lines.length > 15) {
      pre.classList.add("clamped");
      const more = h("button", { class: "more", type: "button", "aria-expanded": "false" }, "Afficher plus");
      more.addEventListener("click", () => { const open = pre.classList.toggle("clamped"); more.textContent = open ? "Afficher plus" : "Afficher moins"; more.setAttribute("aria-expanded", String(!open)); });
      box.appendChild(more);
    }
    return box;
  }
  const mdOpts = { onLink: (url) => post("link.open", { url }), codeBlock };

  function textSeg(t, messageId) {
    const el = h("div", { class: "text" });
    const doc = new window.ChatMarkdown.Doc(el, mdOpts);
    const seg = { kind: "text", messageId, text: "", spoken: 0, el, render() { doc.set(seg.text); el.classList.toggle("streaming", !S.replay && t.running && seg === (t.b ? t.segs[t.segs.length - 1] : t.segs.filter((s) => s.kind === "text").pop())); } };
    noteNew(1);
    return addSeg(t, seg);
  }
  function onChunk(m) {
    const t = turnOf(m.turnId);
    let seg = t.segs[t.segs.length - 1];
    if (!seg || seg.kind !== "text" || seg.messageId !== m.messageId) {
      for (const s of t.segs) if (s.kind === "text") s.el.classList.remove("streaming");
      seg = textSeg(t, m.messageId);
    }
    seg.text += m.text;
    queueRender(seg);
    // speak finished sentences only, so a screen reader is not flooded per token
    const rest = seg.text.slice(seg.spoken);
    const cut = Math.max(rest.lastIndexOf(". "), rest.lastIndexOf(".\n"), rest.lastIndexOf("! "), rest.lastIndexOf("? "), rest.lastIndexOf(":\n"));
    if (cut > 0) { say(plain(rest.slice(0, cut + 1))); seg.spoken += cut + 1; }
  }

  function onThought(m) {
    const t = turnOf(m.turnId);
    let seg = t.segs.find((s) => s.kind === "thought" && s.id === m.id);
    if (!seg) {
      const btn = h("button", { class: "fold", type: "button", "aria-expanded": "false" }, h("span"), icon("chevron", "chev"));
      const det = h("div", { class: "fold-body", hidden: true });
      btn.addEventListener("click", () => { const o = btn.getAttribute("aria-expanded") !== "true"; btn.setAttribute("aria-expanded", String(o)); det.hidden = !o; stick(); });
      const el = h("div", { style: "display:flex;flex-direction:column;gap:10px" }, btn, det);
      const start = Date.now();
      seg = { kind: "thought", id: m.id, text: "", done: false, el, render() {
        if (t.b) { bStatus(t); return; }
        btn.firstChild.textContent = seg.done ? "A réfléchi " + dur(seg.ms != null ? seg.ms : Date.now() - start) : "Réfléchit…";
        det.textContent = seg.text;
        det.id = det.id || "th-" + Math.random().toString(36).slice(2);
        btn.setAttribute("aria-controls", det.id);
      } };
      addSeg(t, seg);
      seg.start = start;
    }
    if (m.text) seg.text += m.text;
    if (m.done) { seg.done = true; seg.ms = typeof m.durationMs === "number" ? m.durationMs : Date.now() - seg.start; }
    queueRender(seg);
  }

  // Folded summary of a group of steps (B 1.3). Each step contributes, in order of preference:
  //  1. the host's `summary` {verb, object, after?, afterMany?, count?, one?, many?} — never guessed;
  //  2. the real paths it touched (read / edit / delete / move with `locations`): only those are called « fichier » ;
  //  3. its own title. A group of one step shows that step's title as is.
  const FILE_VERB = { read: "a lu", edit: "a modifié", delete: "a supprimé", move: "a déplacé" };
  const COUNTED = { search: ["a cherché", "fois", "fois"], execute: ["a lancé", "commande", "commandes"], fetch: ["a consulté", "page", "pages"] };
  function validSummary(s) {
    if (!s || typeof s !== "object" || !isStr(s.verb, 80) || !isStr(s.object, 300) || !s.verb.trim() || !s.object.trim()) return null;
    const out = { verb: s.verb.trim(), object: s.object.trim(), after: isStr(s.after, 120) ? s.after.trim() : "", afterMany: isStr(s.afterMany, 120) ? s.afterMany.trim() : "" };
    if (Number.isInteger(s.count) && s.count > 0 && s.count < 1e6 && isStr(s.one, 40) && isStr(s.many, 40) && s.one && s.many) Object.assign(out, { count: s.count, one: s.one, many: s.many });
    return out;
  }
  function stepPart(x) {
    if (x.summary) return x.summary;
    const files = (x.locations || []).map((l) => l && l.path).filter((p) => typeof p === "string" && p);
    if (FILE_VERB[x.kind] && files.length) return { verb: FILE_VERB[x.kind], after: "", afterMany: "", files };
    if (COUNTED[x.kind]) { const c = COUNTED[x.kind]; return { verb: c[0], after: "", afterMany: "", count: 1, one: c[1], many: c[2], object: "" }; }
    return { title: humanizeTools(x.title) || "" };
  }
  const baseName = (p) => p.split(/[\\/]/).pop();
  const listFr = (xs) => (xs.length < 2 ? xs.join("") : xs.slice(0, -1).join(", ") + " et " + xs[xs.length - 1]);
  const lowerFirst = (t) => (t && t.length > 1 && t[1] === t[1].toLowerCase() ? t[0].toLowerCase() + t.slice(1) : t);
  function stepsSummary(tools) {
    if (tools.length === 1) {
      const p = stepPart(tools[0]);
      return stepLabel(tools[0], false);
      return p.title != null ? p.title || "1 étape" : summaryText([p]);
    }
    return summaryText(tools.map(stepPart));
  }
  function summaryText(parts) {
    const groups = [], byKey = new Map(), titles = [];
    for (const p of parts) {
      if (p.title !== undefined) { titles.push(p.title); continue; }
      const key = p.verb + "|" + p.after;
      let g = byKey.get(key);
      if (!g) { g = { verb: p.verb, after: p.after, afterMany: p.afterMany, files: [], counted: new Map(), objects: [] }; byKey.set(key, g); groups.push(g); }
      if (p.files) { for (const f of p.files) if (!g.files.includes(f)) g.files.push(f); }
      else if (p.count) { const c = g.counted.get(p.many) || { n: 0, one: p.one, many: p.many }; c.n += p.count; g.counted.set(p.many, c); }
      else if (!g.objects.includes(p.object)) g.objects.push(p.object);
      if (p.afterMany) g.afterMany = p.afterMany;
    }
    const clauses = [];
    let prevFilesOnly = false;
    for (const g of groups) {
      const filesOnly = g.files.length && !g.counted.size && !g.objects.length;
      if (filesOnly) {
        clauses.push(prevFilesOnly && /^a /.test(g.verb) ? "en " + g.verb + " " + g.files.length : g.verb + " " + plural(g.files.length, "fichier", "fichiers"));
      } else {
        const items = Array.from(g.counted.values()).map((c) => plural(c.n, c.one, c.many)).concat(g.objects,
          g.files.length > 2 ? [plural(g.files.length, "fichier", "fichiers")] : g.files.map(baseName));
        const many = Array.from(g.counted.values()).reduce((a, c) => a + c.n, 0) + g.objects.length + g.files.length > 1;
        const after = many && g.afterMany ? g.afterMany : g.after;
        clauses.push(g.verb + " " + listFr(items) + (after ? " " + after : ""));
      }
      prevFilesOnly = !!filesOnly;
    }
    const named = titles.filter(Boolean);
    if (titles.length > 2 || named.length < titles.length) { if (titles.length) clauses.push(plural(titles.length, clauses.length ? "autre étape" : "étape", clauses.length ? "autres étapes" : "étapes")); }
    else clauses.push(...named.map((t, i) => (clauses.length || i ? lowerFirst(t) : t)));
    const text = clauses.join(", ").replace(/« /g, "« ").replace(/ »/g, " »");   // guillemets never orphaned at a line end
    return text ? text[0].toUpperCase() + text.slice(1) : plural(parts.length, "étape", "étapes");
  }
  function stepLine(x) {
    const run = x.status === "in_progress" || x.status === "pending";
    const fail = x.status === "failed";
    const ic = run ? h("span", { class: "spin", style: "display:flex" }, icon("spinner")) : fail ? icon("x") : icon("check");
    const label = stepLabel(x, run);
    return h("div", { class: "step " + (run ? "run" : fail ? "fail" : "done"), role: "listitem" }, ic, h("span", { text: label }), fail ? h("span", { class: "fail-tag", text: " (échec)" }) : null);
  }
  function stepsSeg(t) {
    const el = h("div", { class: "steps" });
    let open = false;
    const seg = { kind: "steps", tools: [], el, render() {
      if (t.b) { bStatus(t); return; }
      clear(el);
      const liveGroup = t.running && seg === t.segs[t.segs.length - 1] && !t.waitEl;
      const list = h("div", { class: "steps", role: "list" }, seg.tools.map(stepLine));
      if (liveGroup) { el.appendChild(list); return; }
      const failed = seg.tools.filter((x) => x.status === "failed").length;
      const btn = h("button", { class: "fold", type: "button", "aria-expanded": String(open) }, h("span", { text: stepsSummary(seg.tools) }),
        failed ? h("span", { class: "fail", text: " (échec)" }) : null, icon("chevron", "chev"));
      btn.addEventListener("click", () => { open = !open; seg.render(); stick(); });
      el.appendChild(btn);
      if (open) { list.style.paddingLeft = "2px"; el.appendChild(list); }
    } };
    noteNew(1);
    return addSeg(t, seg);
  }
  function onToolCall(m) {
    const t = turnOf(m.turnId);
    let seg = t.segs[t.segs.length - 1];
    if (seg && seg.kind === "text" && t.running) markFollow(t, seg, true);   // R43
    if (!seg || seg.kind !== "steps") seg = stepsSeg(t);
    else noteNew(1);
    const x = { id: m.id, title: m.title, kind: m.kind || "other", status: m.status || "pending", locations: m.locations || [], diff: m.diff, summary: validSummary(m.summary) };
    seg.tools.push(x);
    S.tools.set(m.id, { x, seg, t });
    queueRender(seg);
    refreshFollow(t);
    if (x.status === "in_progress") say(x.title);
  }
  function onToolUpdate(m) {
    const r = S.tools.get(m.id);
    if (!r) return;
    for (const k of ["title", "status", "locations", "diff"]) if (m[k] != null) r.x[k] = m[k];
    if (m.summary != null) r.x.summary = validSummary(m.summary);
    queueRender(r.seg);
    refreshFollow(r.t);
    if (m.status === "failed") say("Étape en échec : " + r.x.title);
  }

  function onPlan(m) {
    const t = turnOf(m.turnId);
    let seg = t.segs.find((s) => s.kind === "plan" && s.id === m.id);
    if (!seg) {
      const el = h("div", { class: "plan", role: "group" });
      seg = { kind: "plan", id: m.id, entries: [], proposed: false, el, render() {
        clear(el);
        const done = seg.entries.filter((e) => e.status === "completed").length;
        const title = seg.proposed ? "Plan proposé · " + plural(seg.entries.length, "étape", "étapes") : "Plan · " + done + " sur " + seg.entries.length;
        el.setAttribute("aria-label", title);
        el.appendChild(h("div", { class: "plan-hd" }, icon("list"), h("span", { text: title })));
        el.appendChild(h("ol", null, seg.entries.map((e, i) => h("li", { class: e.status || "pending" },
          h("span", { class: "n", "aria-hidden": "true" }, e.status === "completed" ? icon("check") : e.status === "in_progress" ? h("span", { class: "spin", style: "display:flex" }, icon("spinner")) : String(i + 1)),
          h("span", { class: "tx", text: e.content }), h("span", { class: "sr-only", text: e.status === "completed" ? " (fait)" : e.status === "in_progress" ? " (en cours)" : "" })))));
        if (seg.proposed) {
          el.appendChild(h("hr"));
          el.appendChild(h("div", { class: "row" },
            h("button", { class: "btn l primary", type: "button", onclick: () => { post("plan.run", { planId: seg.id }); S.planPending = null; seg.proposed = false; seg.render(); placeholder(); } }, "Lancer le plan"),
            h("button", { class: "btn l", type: "button", onclick: () => { post("plan.edit", { planId: seg.id }); placeholder(); cin.focus(); } }, "Modifier")));
        }
      } };
      addSeg(t, seg);
      noteNew(1);
    }
    seg.entries = (m.entries || []).filter((e) => e && typeof e.content === "string");
    seg.proposed = !!m.proposed;
    S.planPending = seg.proposed ? seg.id : S.planPending === seg.id ? null : S.planPending;
    queueRender(seg);
    placeholder();
    if (seg.proposed) say("Plan proposé, " + plural(seg.entries.length, "étape", "étapes") + ".");
  }

  function refreshWaiting() {
    for (const t of S.turns.values()) {
      if (t.b) { bStatus(t); continue; }
      const p = S.perms.find((x) => x.turnId === t.id) || (t.id === S.lastTurn ? S.perms[0] : null);
      if (p && t.running) {
        const what = /veut (.+)$/.exec(p.title);
        const txt = what ? "Attend votre accord pour " + what[1] : "Attend votre accord";
        if (!t.waitEl) { t.waitEl = h("div", { class: "waiting" }, icon("waiting"), h("span")); t.el.insertBefore(t.waitEl, t.workEl); }
        t.waitEl.lastChild.textContent = txt;
        t.workEl.hidden = true;
      } else {
        if (t.waitEl) { t.waitEl.remove(); t.waitEl = null; }
        t.workEl.hidden = !t.running;
      }
      const last = t.segs[t.segs.length - 1];
      if (last && last.kind === "steps") queueRender(last);
    }
    stick();
  }
  function onPermission(m) {
    const ok = Array.isArray(m.options) && m.options.every((o) => o && ID.test(o.optionId) && typeof o.name === "string");
    if (!ok) { warn("permission.request: bad options"); return; }
    const title = permissionTitle(m);
    // B 1.9 (R9): after each `thread.reset` (every opening of the bubble) the host sends again the request still waiting —
    // the card comes back; the same request twice never makes two cards
    const again = S.perms.findIndex((p) => p.requestId === m.requestId);
    if (again >= 0) S.perms.splice(again, 1);
    S.perms.push({ requestId: m.requestId, turnId: m.turnId || S.lastTurn, title, details: permissionDetails(m), kind: m.kind, diff: validDiff(m.diff), command: typeof m.command === "string" ? m.command : null, options: m.options });
    refreshWaiting();
    renderDock(true);
    noteNew(1);
    shout(title + (BUB() ? ". Autoriser avec Entrée." : ". Autoriser avec Entrée, refuser avec Échap."));
  }

  function copyButton(get, label) {
    const b = h("button", { class: "btn-s", type: "button", "aria-label": label || "Copier" }, icon("copy"), h("span", { text: "Copier" }));
    b.addEventListener("click", () => { lastCopy = b; post("copy", { text: get() }); });
    return b;
  }
  let lastCopy = null;
  function onCopyDone(m) {
    const b = lastCopy;
    if (!b || !m.ok) return;
    const span = b.querySelector("span");
    if (span) { span.textContent = "Copié"; setTimeout(() => { span.textContent = "Copier"; }, 1500); }
    say("Copié.");
  }

  function changesBar(t, changes, undoable) {
    const added = changes.reduce((a, c) => a + (c.added || 0), 0), removed = changes.reduce((a, c) => a + (c.removed || 0), 0);
    let open = false;
    const box = h("div", { class: "card4", role: "group", "aria-label": "Changements de ce tour" });
    const listId = "files-" + t.id.replace(/[^\w-]/g, "");
    const render = (state) => {
      clear(box);
      const hd = h("div", { class: "card4-hd" });
      if (state && state.undone) {
        hd.append(icon("undo", "ok-ico"), h("span", { class: "name undone", text: "Modifications annulées" }));
        hd.firstChild.style.color = "#FFFFFF80";
        box.appendChild(hd); return;
      }
      const label = plural(changes.length, "fichier modifié", "fichiers modifiés");
      const tog = h("button", { class: "toggle-chev", type: "button", "aria-expanded": String(open), "aria-controls": listId, "aria-label": open ? "Masquer les fichiers" : "Voir les fichiers" }, icon("chevron"));
      tog.addEventListener("click", () => { open = !open; render(state); stick(); });
      hd.append(icon("check", "ok-ico"), h("span", { class: "name", text: label }), h("span", { class: "num plus", text: "+" + added, "aria-label": added + " lignes ajoutées" }),
        h("span", { class: "num minus", text: "−" + removed, "aria-label": removed + " lignes retirées" }), tog, h("span", { class: "grow" }));
      if (undoable) hd.appendChild(h("button", { class: "btn-s", type: "button", onclick: () => post("undo.turn", { turnId: t.id }) }, icon("undo"), "Tout annuler"));
      box.appendChild(hd);
      if (state && state.conflicts) box.appendChild(h("div", { class: "conflict", role: "alert", text: "Rien n'a été annulé : " + state.conflicts.join(", ") + " " + (state.conflicts.length > 1 ? "ont" : "a") + " changé depuis. Aucun fichier n'a été écrasé." }));
      if (state && state.message) box.appendChild(h("div", { class: "conflict", role: "alert", text: state.message }));
      if (open) {
        box.appendChild(h("div", { class: "files", id: listId, role: "list" }, changes.map((c) =>
          h("button", { class: "file", type: "button", role: "listitem", "aria-label": "Voir les changements de " + c.path, onclick: (e) => openChanges(t, c.path, e.currentTarget) },
            h("span", { class: "p", text: c.path }), c.status === "created" ? h("span", { class: "tag", text: "nouveau" }) : c.status === "deleted" ? h("span", { class: "tag", text: "supprimé" }) : null,
            h("span", { class: "num plus", text: "+" + (c.added || 0) }), h("span", { class: "num minus", text: "−" + (c.removed || 0) }))).concat(
          changes.length === 1 && validDiff(changes[0].diff) ? [diffView(validDiff(changes[0].diff))] : []).concat(
          [h("button", { class: "more", type: "button", style: "margin:4px 6px", onclick: (e) => openChanges(t, null, e.currentTarget) }, "Voir les changements")])));
      }
    };
    render();
    box._render = render;
    return box;
  }

  // ---------------------------------------------------------------- changes view (checkpoint.viewChanges → changes.diff)
  // Opens at once with the diffs turn.end already carried, then fills in when the host answers.
  function openChanges(t, path, trigger) {
    if (!t || !t.changes) return;
    closeMenu();
    if (S.historyOpen) toggleHistory(false);
    const diffs = new Map();
    for (const c of t.changes) { const d = validDiff(c.diff, 4000); if (d) diffs.set(c.path, d); }
    for (const [p, d] of t.fullDiffs || []) diffs.set(p, d);
    // pin: keep the asked file in place while late diffs grow the files above it, until the user scrolls
    S.view = { turnId: t.id, t, files: t.changes, diffs, path, pin: path || null, trigger, loading: !t.fullDiffs, folded: new Map() };
    post("checkpoint.viewChanges", Object.assign({ turnId: t.id }, path ? { path } : {}));
    changesEl.hidden = false;
    renderChanges(true);
    say("Changements : " + plural(t.changes.length, "fichier", "fichiers") + ". Échap pour revenir au fil.");
  }
  function closeChanges() {
    if (!S.view) return;
    const trig = S.view.trigger;
    S.view = null;
    changesEl.hidden = true;
    clear(changesEl);
    if (trig && trig.isConnected) trig.focus(); else cin.focus();
  }
  function onChangesDiff(m) {
    const t = S.turns.get(m.turnId);
    const map = new Map();
    for (const d of m.diffs) { const v = validDiff(d, 4000); if (v) map.set(v.path, v); }
    if (t) t.fullDiffs = map;
    const v = S.view;
    if (S.bview && S.bview.kind === "changes" && S.bview.t === t) renderView();
    if (!v || v.turnId !== m.turnId) return;
    if (Array.isArray(m.files)) { const f = m.files.filter((c) => c && isStr(c.path, 1000)); if (f.length) v.files = f; }
    for (const [p, d] of map) v.diffs.set(p, d);
    v.loading = false;
    rerenderChanges();
  }
  function rerenderChanges(focusId) {
    const a = document.activeElement;
    if (!focusId && a && changesEl.contains(a)) focusId = a.id || (a.classList.contains("chg-back") ? "chg-back" : null);
    const top = changesEl.scrollTop;
    renderChanges(false);
    changesEl.scrollTop = top;
    if (S.view && S.view.pin) scrollToFile(S.view.pin);
    if (focusId) { const n = document.getElementById(focusId); if (n) n.focus(); }
  }
  function scrollToFile(path) {
    const k = S.view ? S.view.files.findIndex((c) => c.path === path) : -1;
    const n = k >= 0 && S.view.files.length > 1 && document.getElementById("chg-" + k);
    if (n) changesEl.scrollTop = n.offsetTop - 60;
  }
  for (const ev of ["wheel", "pointerdown", "keydown", "touchstart"]) changesEl.addEventListener(ev, () => { if (S.view) S.view.pin = null; }, { passive: true });
  // Late layout (fonts, long lines) can grow the files above the pinned one: follow it until the user scrolls.
  const chgRO = window.ResizeObserver ? new ResizeObserver(() => { if (S.view && S.view.pin) scrollToFile(S.view.pin); }) : null;
  const diffText = (d) => d.lines.map((l) => (l.op === "@" ? l.text : l.op + l.text)).join("\n");
  function unifiedView(d, key) {
    const v = S.view;
    const box = h("div", { class: "udiff", role: "region", tabindex: "0", id: key + "-d", "aria-label": "Différences dans " + d.path });
    let oldNo = null, newNo = null;
    const row = (l) => {
      if (l.op === "@") {
        const m = /^@@ -(\d+)(?:,\d+)? \+(\d+)/.exec(l.text);
        if (m) { oldNo = +m[1]; newNo = +m[2]; }
        return h("div", { class: "ul hunk" }, h("span", { class: "tx", text: l.text }));
      }
      const o = l.op !== "+" && oldNo != null ? String(oldNo++) : "";
      const n = l.op !== "-" && newNo != null ? String(newNo++) : "";
      return h("div", { class: "ul " + (l.op === "+" ? "add" : l.op === "-" ? "del" : "") },
        h("span", { class: "ln", "aria-hidden": "true", text: o }), h("span", { class: "ln", "aria-hidden": "true", text: n }),
        h("span", { class: "sg", "aria-hidden": "true", text: l.op === " " ? "" : l.op === "-" ? "−" : "+" }),
        h("span", { class: "tx" }, l.op === "+" ? h("span", { class: "sr-only", text: "ajouté : " }) : l.op === "-" ? h("span", { class: "sr-only", text: "retiré : " }) : null, l.text || " "));
    };
    // Long runs of unchanged lines fold down to the 3 lines around each change.
    const L = d.lines;
    let i = 0;
    while (i < L.length) {
      if (L[i].op !== " ") { box.appendChild(row(L[i])); i++; continue; }
      let j = i; while (j < L.length && L[j].op === " ") j++;
      const run = L.slice(i, j);
      const head = i === 0 || L[i - 1].op === "@" ? 0 : 3, tail = j === L.length || L[j].op === "@" ? 0 : 3;
      const fkey = key + ":" + i;
      if (run.length - head - tail >= 4 && !v.folded.get(fkey)) {
        run.slice(0, head).forEach((l) => box.appendChild(row(l)));
        const hidden = run.length - head - tail;
        if (oldNo != null) oldNo += hidden;
        if (newNo != null) newNo += hidden;
        const b = h("button", { class: "unfold", type: "button", "aria-expanded": "false", "aria-label": "Afficher " + plural(hidden, "ligne inchangée", "lignes inchangées") },
          icon("chevronsUp"), plural(hidden, "ligne inchangée", "lignes inchangées"));
        b.addEventListener("click", () => { v.folded.set(fkey, true); rerenderChanges(key + "-d"); say(plural(hidden, "ligne affichée", "lignes affichées") + "."); });
        box.appendChild(b);
        run.slice(run.length - tail).forEach((l) => box.appendChild(row(l)));
      } else run.forEach((l) => box.appendChild(row(l)));
      i = j;
    }
    if (d.truncated) box.appendChild(h("div", { class: "ul hunk" }, h("span", { class: "tx", text: "… aperçu tronqué" })));
    return box;
  }
  const statusTag = (c) => c.status === "created" ? h("span", { class: "tag", text: "nouveau" }) : c.status === "deleted" ? h("span", { class: "tag", text: "supprimé" }) : null;
  function renderChanges(first) {
    const v = S.view;
    if (!v) return;
    if (chgRO) chgRO.disconnect();
    clear(changesEl);
    const files = v.files;
    const added = files.reduce((a, c) => a + (c.added || 0), 0), removed = files.reduce((a, c) => a + (c.removed || 0), 0);
    const back = h("button", { class: "hd-btn chg-back", id: "chg-back", type: "button", "aria-label": "Retour au fil", "aria-keyshortcuts": "Escape", onclick: () => closeChanges() }, icon("chevron"));
    const canUndo = v.t.undoable && !(v.t.changesEl && v.t.changesEl.querySelector(".undone"));
    changesEl.appendChild(h("div", { class: "chg-hd" }, back,
      h("div", { class: "chg-t" }, h("h2", { id: "changes-h", text: "Changements" }),
        h("div", { class: "chg-sub" }, plural(files.length, "fichier", "fichiers") + " · ", h("span", { class: "num plus", text: "+" + added, "aria-label": added + " lignes ajoutées" }), " ",
          h("span", { class: "num minus", text: "−" + removed, "aria-label": removed + " lignes retirées" }))),
      canUndo ? h("button", { class: "btn-s", type: "button", onclick: () => { post("undo.turn", { turnId: v.turnId }); closeChanges(); } }, icon("undo"), "Tout annuler") : null));
    const ids = new Map(files.map((c, k) => [c.path, "chg-" + k]));
    if (files.length > 1) changesEl.appendChild(h("div", { class: "files chg-nav", role: "list", "aria-label": "Fichiers modifiés" }, files.map((c) =>
      h("button", { class: "file", type: "button", role: "listitem", "aria-label": "Aller à " + c.path,
                    onclick: () => { const n = document.getElementById(ids.get(c.path)); if (n) { n.scrollIntoView({ block: "start", behavior: isReduced() ? "auto" : "smooth" }); n.querySelector(".chg-fh").focus({ preventScroll: true }); } } },
        h("span", { class: "p", text: c.path }), statusTag(c),
        h("span", { class: "num plus", text: "+" + (c.added || 0) }), h("span", { class: "num minus", text: "−" + (c.removed || 0) })))));
    files.forEach((c) => {
      const id = ids.get(c.path);
      const d = v.diffs.get(c.path);
      const closed = !!v.folded.get("file:" + c.path);
      const fh = h("button", { class: "chg-fh", type: "button", "aria-expanded": String(!closed), "aria-controls": id + "-b", id: id + "-h" },
        icon("chevron", "cv"), h("span", { class: "p", text: c.path }), statusTag(c),
        h("span", { class: "num plus", text: "+" + (c.added || 0), "aria-label": (c.added || 0) + " ajoutées" }), h("span", { class: "num minus", text: "−" + (c.removed || 0), "aria-label": (c.removed || 0) + " retirées" }));
      fh.addEventListener("click", () => { v.folded.set("file:" + c.path, !closed); rerenderChanges(id + "-h"); });
      const body = h("div", { class: "chg-body", id: id + "-b", hidden: closed },
        d ? unifiedView(d, id) : h("div", { class: "chg-empty", role: "status", text: v.loading ? "Chargement du détail…" : "Pas d'aperçu pour ce fichier." }));
      changesEl.appendChild(h("section", { class: "card4 chg-file", id, "aria-labelledby": id + "-h" },
        h("div", { class: "card4-hd" }, h("h3", { class: "chg-h3" }, fh), d ? copyButton(() => diffText(d), "Copier le diff de " + c.path) : null), body));
    });
    if (chgRO && v.pin) for (const c of changesEl.children) chgRO.observe(c);
    if (first) requestAnimationFrame(() => {
      if (v.path) scrollToFile(v.path);
      const b = document.getElementById("chg-back"); if (b) b.focus({ preventScroll: true });
    });
  }

  function onTurnEnd(m) {
    const t = turnOf(m.turnId);
    t.running = false;
    t.stop = typeof m.stopReason === "string" ? m.stopReason : "end_turn";
    t.el.setAttribute("aria-busy", "false");
    t.workEl.remove();
    if (t.waitEl) { t.waitEl.remove(); t.waitEl = null; }
    S.perms = S.perms.filter((p) => p.turnId !== t.id);
    const d = typeof m.durationMs === "number" ? m.durationMs : m.interrupted ? null : Date.now() - t.start;
    t.durMs = d;
    setMeta(t, [t.model.replace(/\s+[\d.]+$/, ""), t.effort, d == null ? "" : dur(d)].filter(Boolean).join(" · "));
    for (const s of t.segs) { if (s.kind === "text") { s.render(); s.el.classList.remove("streaming"); const rest = s.text.slice(s.spoken); if (rest.trim()) say(plain(rest)); s.spoken = s.text.length; } else s.render(); }
    const notes = { cancelled: "Arrêté.", refusal: "L'agent a refusé de continuer cette demande.", max_tokens: "La réponse a atteint sa longueur maximale. Demandez la suite pour continuer." };
    if (notes[m.stopReason] && !(t.b && m.stopReason === "cancelled")) t.el.appendChild(h("div", { class: "note", text: notes[m.stopReason] }));
    const changes = Array.isArray(m.changes) ? m.changes.filter((c) => c && typeof c.path === "string") : [];
    const copyText = typeof m.copyText === "string" ? m.copyText : t.segs.filter((s) => s.kind === "text").map((s) => s.text).join("\n\n");
    t.copyText = copyText;
    if (changes.length && t.b) { t.changes = changes; t.barEl = bBar(t); if (!S.replay) t.barEl.classList.add("enter"); t.el.appendChild(t.barEl); t.el.classList.add("has-bar"); bCards(t); }
    else if (changes.length) { t.changes = changes; t.undoable = m.undoable !== false; t.changesEl = changesBar(t, changes, t.undoable); t.el.appendChild(t.changesEl); }
    closeFollows(t);
    if (t.b) bStatus(t);   // « S'est arrêté », or « Historique » (Paper 1AE7-0, 1AWI-0): no hover actions, no « Revenir » in the bubble
    else if (m.stopReason !== "error") t.el.appendChild(h("div", { class: "actions" },
      h("button", { class: "icon-btn", type: "button", "aria-label": "Copier la réponse", onclick: (e) => { lastCopy = null; post("copy", { text: copyText }); } }, icon("copy")),
      h("button", { class: "icon-btn", type: "button", "aria-label": "Relancer", onclick: () => post("retry", { turnId: t.id }) }, icon("history"))));
    if (!t.b && m.checkpointId && ID.test(m.checkpointId)) {
      const u = S.users.get(t.userId);
      if (u && !u.bubble.querySelector(".rewind")) u.bubble.appendChild(h("button", { class: "rewind", type: "button",
        "aria-label": "Revenir à avant ce message", onclick: () => post("checkpoint.restore", { checkpointId: m.checkpointId }) }, icon("undo"), "Revenir"));
    }
    t.el.classList.remove("live");
    refreshWaiting();
    renderDock(true);
    placeholder();
    noteNew(1);
    stick();
    if (!S.replay) say(changes.length ? "Terminé. " + plural(changes.length, "fichier modifié", "fichiers modifiés") + "." : notes[m.stopReason] || "Terminé.");
  }
  function onUndone(m) {
    const t = S.turns.get(m.turnId);
    if (!t || !t.changesEl) return;
    t.changesEl._render(m.ok ? { undone: true } : { conflicts: m.conflicts && m.conflicts.length ? m.conflicts : null, message: m.conflicts && m.conflicts.length ? null : m.message || "L'annulation n'a pas abouti." });
    say(m.ok ? "Modifications annulées." : "Rien n'a été annulé.");
  }

  function onError(m) {
    outboxEnd();
    if (m.scope === "composer") {
      S.strips.set("err:" + m.id, { id: "err:" + m.id, errorId: m.id, tone: "danger", text: m.message || m.title, action: (m.actions || [])[0] });
      renderDock(true);
      shout(m.title + ". " + (m.message || ""));
      return;
    }
    if (BUB()) { bError(m); return; }
    const det = m.detail ? h("pre", { class: "detail", hidden: true, text: m.detail }) : null;
    const acts = (m.actions || []).filter((a) => a && ID.test(a.id)).map((a, i) =>
      h("button", { class: "btn" + (i ? " ghost" : ""), type: "button", onclick: () => a.id === "retry" ? post("retry", { turnId: m.turnId || S.lastTurn }) : post("error.action", { errorId: m.id, actionId: a.id }) }, a.label));
    if (det) {
      const b = h("button", { class: "btn ghost", type: "button", "aria-expanded": "false" }, "Voir le détail");
      b.addEventListener("click", () => { det.hidden = !det.hidden; b.setAttribute("aria-expanded", String(!det.hidden)); b.textContent = det.hidden ? "Voir le détail" : "Masquer le détail"; stick(); });
      acts.push(b);
    }
    const card = h("div", { class: "err", role: "group", "aria-label": m.title },
      h("div", { style: "display:flex;flex-direction:column;gap:3px" }, h("div", { class: "err-t" }, icon("x"), h("span", { text: m.title })), h("div", { class: "err-m", text: m.message || "" })),
      acts.length ? h("div", { class: "row" }, acts) : null, det);
    const t = m.turnId && S.turns.get(m.turnId);
    if (t) { if (t.running) addSeg(t, { kind: "error", el: card, render() {} }); else t.el.appendChild(card); stick(); }
    else add(card);
    noteNew(1);
    shout(m.title + ". " + (m.message || ""));
  }

  // edit a sent message (6.7 C)
  function startEdit(id) {
    const u = S.users.get(id);
    if (!u || S.editing) return;
    S.editing = id;
    const ta = h("textarea", { rows: "1", "aria-label": "Modifier le message" });
    ta.value = u.text;
    const warnEl = h("div", { class: "warn", hidden: true, role: "alert" });
    const cancel = h("button", { class: "btn ghost", type: "button", onclick: () => endEdit() }, "Annuler");
    const go = h("button", { class: "send ready", type: "button", "aria-label": "Renvoyer le message", onclick: () => submit() }, icon("arrowUp"));
    const row = h("div", { class: "row" }, cancel, go);
    const ed = h("div", { class: "editor" + (S.replay ? "" : " pop-in") }, ta, warnEl, row);
    const fit = () => { ta.style.height = "auto"; ta.style.height = Math.min(ta.scrollHeight, 120) + "px"; };
    ta.addEventListener("input", fit);
    ta.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey && !e.isComposing) { e.preventDefault(); submit(); }
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); endEdit(); }
    });
    function submit(restore) {
      const text = ta.value.trim();
      if (!text) return;
      post("message.edit", Object.assign({ messageId: id, text }, restore == null ? {} : { restore }));
      if (restore != null) endEdit();
      else u.pendingText = text;
    }
    u.editor = { el: ed, warn: warnEl, row, submit };
    u.el.replaceChild(ed, u.row);
    const under = u.el.querySelector(".um-attach"); if (under) under.hidden = true;
    u.el.style.alignItems = "stretch";
    fit(); ta.focus(); ta.setSelectionRange(ta.value.length, ta.value.length);
  }
  function endEdit() {
    const u = S.users.get(S.editing);
    S.editing = null;
    if (!u || !u.editor) return;
    u.el.replaceChild(u.row, u.editor.el);
    const under = u.el.querySelector(".um-attach"); if (under) under.hidden = false;
    u.el.style.alignItems = "";
    u.editor = null;
    const pencil = u.row.querySelector(".icon-btn"); if (pencil) pencil.focus();
  }
  function onEditConfirm(m) {
    const u = S.users.get(m.messageId);
    if (!u || !u.editor) return;
    const files = (m.files || []).filter((f) => typeof f === "string");
    clear(u.editor.warn).append("Des fichiers ont changé depuis ce message : ", h("b", { text: files.join(", ") }), ". Les restaurer avant de reprendre ?");
    u.editor.warn.hidden = false;
    clear(u.editor.row).append(
      h("button", { class: "btn ghost", type: "button", onclick: () => endEdit() }, "Annuler"),
      h("button", { class: "btn", type: "button", onclick: () => u.editor.submit(false) }, "Renvoyer sans restaurer"),
      h("button", { class: "btn primary", type: "button", onclick: () => u.editor.submit(true) }, "Restaurer et renvoyer"));
    u.editor.row.lastChild.focus();
  }

  // ---------------------------------------------------------------- first run (6.2) and empty chat (6.7 A)
  function renderFirst() {
    clear(first);
    const site = S.st.site ? S.st.site.label : "ce site";
    const sug = S.st.folderSuggestion;
    const q = h("section", { class: "q", "aria-labelledby": "first-h" },
      h("div", null, h("h2", { id: "first-h", text: "Où est le code de ce site ?" }), h("p", { class: "sub", text: "Choisissez le dossier à partir duquel " + site + " est construit. On ne le demande qu'une fois." })));
    // B 1.8 (Paper 156L-0): the folder's name says which project this is; its full path stays in the tooltip
    if (sug) q.appendChild(h("div", { class: "tile found", "aria-description": sug.path }, icon("folder", "ic"),
      h("div", { class: "grow" }, h("div", { class: "nm" }, h("span", { text: folderName(sug) }), h("span", { class: "badge", text: "Détecté auto" }))),
      h("button", { class: "btn primary", type: "button", "aria-label": "Utiliser le dossier " + folderName(sug), onclick: () => post("folder.use", { path: sug.path }) }, "Utiliser")));
    q.appendChild(h("button", { class: "tile other", type: "button", onclick: () => post("folder.choose") }, icon("plus", "ic"), h("span", { class: "lb", text: "Choisir un autre dossier…" })));
    q.querySelector(".tile.other .ic").style.opacity = ".6";
    if (sug) q.querySelector(".tile.found .ic").style.opacity = ".8";
    first.appendChild(q);
    const agents = S.st.agents || [];
    if (agents.length && !(curAgent() && curAgent().status === "ready")) {
      const group = h("div", { class: "agents", role: "radiogroup", "aria-labelledby": "agent-h" });
      for (const a of agents) {
        const act = a.status !== "ready" ? agentAction(a) : null;
        const sel = a.id === S.st.agentId;
        const row = h("div", { class: "agent", role: "radio", tabindex: sel ? "0" : "-1", "aria-checked": String(sel), onclick: () => pick(a) },
          h("span", { class: "logo", style: "display:flex" }, agentIcon(a)), h("span", { class: "grow" }, h("span", { class: "nm", text: a.name }), h("span", { class: "dt", text: statusText(a) })),
          act ? h("button", { class: "btn-s", type: "button", onclick: (e) => { e.stopPropagation(); act.run(); } }, act.label) : null,
          sel ? icon("check", "ck") : null);
        row.querySelector(".logo svg").style.cssText = "width:18px;height:18px";
        row.addEventListener("keydown", (e) => {
          const i = agents.indexOf(a);
          if (e.key === "ArrowDown" || e.key === "ArrowRight") { e.preventDefault(); pick(agents[(i + 1) % agents.length], true); }
          if (e.key === "ArrowUp" || e.key === "ArrowLeft") { e.preventDefault(); pick(agents[(i - 1 + agents.length) % agents.length], true); }
          if (e.key === " " || e.key === "Enter") { e.preventDefault(); pick(a, true); }
        });
        group.appendChild(row);
      }
      first.appendChild(h("section", { style: "display:flex;flex-direction:column;gap:8px" }, h("h3", { class: "sect", id: "agent-h", text: "Agent" }), group));
    }
    function pick(a, focus) {
      if (a.id !== S.st.agentId) { post("setAgent", { id: a.id }); S.st.agentId = a.id; renderFirst(); renderComposerBar(); }
      if (focus) { const r = first.querySelector(".agent[aria-checked='true']"); if (r) r.focus(); }
    }
  }
  function renderEmpty() {
    clear(empty);
    const sugs = [];
    // R24: the « annotations en attente » concept is gone; a suggestion never shows twice (same id or same words)
    const seen = new Set();
    for (const s of S.st.suggestions || []) {
      if (!s || !ID.test(s.id) || typeof s.label !== "string" || s.id === "annotations" || /annotations? en attente/i.test(s.label)) continue;
      const k = s.label.trim().toLowerCase();
      if (seen.has(s.id) || seen.has(k)) continue;
      seen.add(s.id); seen.add(k); sugs.push(s);
    }
    if (S.st.disabledReason) sugs.length = 0;
    empty.append(h("h2", { id: "empty-h", text: "Que voulez-vous changer ?" }),
      h("div", { class: "sugs", role: "list", "aria-labelledby": "empty-h" }, sugs.map((s) =>
        h("button", { class: "sug", type: "button", role: "listitem", onclick: () => post("suggestion.run", { id: s.id }) }, h("span", { class: "lb", text: s.label }), s.badge ? h("span", { class: "ct", text: s.badge, "aria-hidden": "true" }) : null))));
  }

  // ---------------------------------------------------------------- history (6.7 B)
  let histTimer = 0;
  function toggleHistory(force) {
    const open = force != null ? force : !S.historyOpen;
    S.historyOpen = open;
    histBtn.setAttribute("aria-expanded", String(open));
    historyEl.hidden = !open;
    if (open) { closeMenu(); post("history.open"); renderHistory(); const i = historyEl.querySelector("input"); if (i) i.focus(); }
    else histBtn.focus();
  }
  function relTime(iso) {
    const t = Date.parse(iso); if (!t) return "";
    const d = new Date(t), now = new Date(), diff = now - d;
    if (diff < 60 * 60 * 1000) return Math.max(1, Math.round(diff / 60000)) + " min";
    if (d.toDateString() === now.toDateString()) return d.toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" });
    const y = new Date(now); y.setDate(now.getDate() - 1);
    if (d.toDateString() === y.toDateString()) return "Hier";
    if (diff < 6 * 24 * 3600 * 1000) { const w = d.toLocaleDateString("fr-FR", { weekday: "long" }); return w[0].toUpperCase() + w.slice(1); }
    return d.toLocaleDateString("fr-FR", { day: "numeric", month: "short" });
  }
  function renderHistory() {
    const prevInput = historyEl.querySelector("input");
    const q = prevInput ? prevInput.value : "";
    const hadFocus = prevInput && document.activeElement === prevInput;
    clear(historyEl);
    const input = h("input", { type: "search", placeholder: "Rechercher une conversation", "aria-label": "Rechercher une conversation", value: q });
    input.value = q;
    input.addEventListener("input", () => { clearTimeout(histTimer); histTimer = setTimeout(() => post("history.search", { query: input.value }), 120); renderHistoryList(); });
    historyEl.appendChild(h("div", { class: "search" }, icon("search"), input,
      h("button", { class: "btn-s", type: "button", onclick: () => { toggleHistory(false); post("newChat"); } }, icon("plus"), "Nouveau")));
    historyEl.appendChild(h("div", { class: "hlist", role: "list", style: "display:flex;flex-direction:column;gap:4px" }));
    historyEl.addEventListener("keydown", histKeys);
    renderHistoryList();
    if (hadFocus) { input.focus(); input.setSelectionRange(q.length, q.length); }
  }
  function histKeys(e) {
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    const rows = Array.from(historyEl.querySelectorAll(".hrow, input"));
    const i = rows.indexOf(document.activeElement);
    if (i < 0) return;
    e.preventDefault();
    const n = rows[Math.max(0, Math.min(rows.length - 1, i + (e.key === "ArrowDown" ? 1 : -1)))];
    n.focus();
  }
  function renderHistoryList() {
    const list = historyEl.querySelector(".hlist");
    if (!list) return;
    clear(list);
    const q = (historyEl.querySelector("input").value || "").toLowerCase();
    const items = ((S.history && S.history.items) || []).filter((it) => !q || (it.title + " " + (it.subtitle || "") + " " + (it.siteLabel || "")).toLowerCase().includes(q));
    if (!S.history) { list.appendChild(h("div", { class: "hnone", text: "Chargement…" })); return; }
    if (!items.length) { list.appendChild(h("div", { class: "hnone", role: "status", text: q ? "Aucune conversation ne contient « " + q + " »." : "Aucune conversation pour l'instant." })); return; }
    const mine = items.filter((i) => i.thisSite), others = items.filter((i) => !i.thisSite);
    const row = (it, other) => h("button", { class: "hrow", type: "button", role: "listitem", "aria-current": it.current ? "true" : null, onclick: () => { post("history.select", { id: it.id }); toggleHistory(false); } },
      agentIcon({ icon: it.agentIcon, id: it.agentIcon }),
      h("span", { class: "grow" }, h("span", { class: "t", text: it.title }), h("span", { class: "s", title: other && it.siteLabel ? it.siteLabel : null, text: other ? [isStr(it.folderName, 200) && it.folderName || it.siteLabel, it.subtitle].filter(Boolean).join(" · ") : it.subtitle || "" })),
      it.running ? h("span", { class: "live" }, h("i"), "En cours") : h("span", { class: "when", text: relTime(it.updatedAt) }));
    // B 1.8 (Paper 156L-0): the project folder's name rather than the address — « 127.0.0.1:5173 » does not say what it is
    const here = folderName(S.st.folder) || (S.st.site ? S.st.site.label : "");
    if (mine.length) { list.appendChild(h("div", { class: "hsec", text: "Ce site" + (here ? " · " + here : "") })); mine.forEach((i) => list.appendChild(row(i, false))); }
    if (others.length) { list.appendChild(h("div", { class: "hsec" + (mine.length ? " next" : ""), text: "Autres sites" })); others.forEach((i) => list.appendChild(row(i, true))); }
  }

  // ---------------------------------------------------------------- header
  function renderHeader() {
    const c = S.st.conversation;
    clear(hdLogo).appendChild(agentIcon(curAgent()));
    hdLogo.firstChild.style.cssText = "width:16px;height:16px";
    hdTitleText.textContent = c && c.title ? c.title : "Nouveau chat";
    const src = c && c.source;
    hd.classList.toggle("with-source", !!src);
    hdSource.hidden = !src;
    hdSource.textContent = src || "";
    hdSource.setAttribute("aria-label", src ? "Ouvrir " + src : "");
    renderGauge();
    const canRename = !!(c && ID.test(String(c.id || "")));
    hdTitle.tabIndex = canRename ? 0 : -1;   // a new chat's title has nothing to rename: not a Tab stop
    if (canRename) hdTitle.setAttribute("aria-description", "Double-clic ou F2 pour renommer"); else hdTitle.removeAttribute("aria-description");
  }

  function renderGauge() {
    const u = S.usage;
    gauge.hidden = !u;
    if (!u) return;
    const pct = Math.max(0, Math.min(100, Math.round((u.used / u.size) * 100)));
    gaugeArc.setAttribute("stroke-dasharray", (GAUGE_C * pct / 100).toFixed(2) + " " + GAUGE_C.toFixed(2));
    gauge.classList.toggle("high", pct > 80);
    // Paper 17Y8-0: the bubble only says « 86k / 270k »; the accessible name spells the numbers out.
    const k = (n) => (n >= 1000 ? Math.round(n / 1000) + "k" : String(n));
    gaugeTip.textContent = k(u.used) + " / " + k(u.size);
    gauge.setAttribute("aria-label", "Mémoire du chat : " + u.used.toLocaleString("fr-FR") + " tokens sur " + u.size.toLocaleString("fr-FR"));
    if (pct > 80 && !S.usageWarned) { S.usageWarned = true; say("Mémoire du chat presque pleine : elle se résumera bientôt."); }
    if (pct <= 80) S.usageWarned = false;
  }

  // ---------------------------------------------------------------- timers
  let ticker = 0;
  function tick() {
    let any = false;
    for (const t of S.turns.values()) if (t.running) { any = true; t.workEl.lastChild.textContent = dur(Date.now() - t.start); }
    if (any && !ticker) ticker = setInterval(tick, 1000);
    if (!any && ticker) { clearInterval(ticker); ticker = 0; }
  }

  // ---------------------------------------------------------------- validation
  const isStr = (x, max) => typeof x === "string" && x.length <= (max || 100000);
  function validElement(e) {
    if (!e || !ID.test(e.id) || !isStr(e.label, 300)) return false;
    if (e.thumb != null && !(isStr(e.thumb, 350000) && IMG.test(e.thumb))) e.thumb = null;
    if (!KINDS.includes(e.kind)) e.kind = "block";
    return true;
  }
  const validAttachment = (a) => a && ID.test(a.id) && isStr(a.name, 260) && isStr(a.thumb, 350000) && IMG.test(a.thumb);
  function validDiff(d, max) {
    max = max || 400;
    if (!d || !isStr(d.path, 1000) || !Array.isArray(d.lines)) return null;
    return { path: d.path, added: d.added | 0, removed: d.removed | 0, truncated: !!d.truncated || d.lines.length > max,
             lines: d.lines.filter((l) => l && isStr(l.text, 4000) && ["+", "-", " ", "@"].includes(l.op)).slice(0, max) };
  }
  const REQ = {
    "thread.reset": (m) => Array.isArray(m.items), "thread.append": (m) => Array.isArray(m.items),
    "message.user": (m) => ID.test(m.id) && isStr(m.text), "turn.start": (m) => ID.test(m.turnId),
    "message.agentChunk": (m) => ID.test(m.turnId) && ID.test(m.messageId) && isStr(m.text, 1000000),
    thought: (m) => ID.test(m.turnId) && ID.test(m.id) && (m.text == null || isStr(m.text, 1000000)),
    toolCall: (m) => ID.test(m.turnId) && ID.test(m.id) && isStr(m.title || "", 2000), toolCallUpdate: (m) => ID.test(m.id),
    plan: (m) => ID.test(m.turnId) && ID.test(m.id) && Array.isArray(m.entries),
    "permission.request": (m) => ID.test(m.requestId) && isStr(m.title, 2000), "permission.cancel": (m) => ID.test(m.requestId),
    "turn.end": (m) => ID.test(m.turnId), "turn.undone": (m) => ID.test(m.turnId),
    error: (m) => ID.test(m.id) && isStr(m.title, 500), strip: (m) => ID.test(m.id), queue: (m) => Array.isArray(m.items),
    history: (m) => Array.isArray(m.items), "element.attached": (m) => validElement(m.element), "attachment.added": (m) => validAttachment(m.attachment),
    "pick.state": (m) => typeof m.active === "boolean", mentions: (m) => isStr(m.query, 200), commands: (m) => Array.isArray(m.items),
    usage: (m) => Number.isFinite(m.used) && Number.isFinite(m.size) && m.used >= 0 && m.size >= 0,
    "annotations.batch": (m) => ID.test(m.batchId) && Array.isArray(m.items), "annotations.status": (m) => ID.test(m.batchId) && ID.test(m.id),
    layout: (m) => m.mode === "panel" || m.mode === "bubble",
    "changes.diff": (m) => ID.test(m.turnId) && Array.isArray(m.diffs), "edit.confirm": (m) => ID.test(m.messageId), state: (m) => true, hello: (m) => true, focus: () => true, "copy.done": () => true, "pointer.leave": () => true, "annotation.kept": (m) => ID.test(m.id) && isStr(m.text) && Array.isArray(m.elements),
  };

  // ---------------------------------------------------------------- dispatch
  const H = {
    hello(m) { if (m.reducedMotion) document.documentElement.classList.add("reduce-motion"); app.removeAttribute("aria-busy"); },
    state(m) {
      const keys = ["site", "folder", "folderSuggestion", "askFolder", "agents", "agentId", "model", "efforts", "effort", "modes", "mode", "conversation", "running", "pendingAnnotations", "suggestions", "capabilities", "disabledReason"];
      for (const k of keys) if (k in m) S.st[k] = m[k];
      S.st.agents = (S.st.agents || []).filter((a) => a && ID.test(a.id) && isStr(a.name, 100));
      if (!S.st.agentId && S.st.agents[0]) S.st.agentId = S.st.agents[0].id;
      renderHeader(); renderComposerBar(); updateView(); renderDock(); refreshWaiting();
      if (S.historyOpen) renderHistoryList();
    },
    // B 1.8: the page's layout — the panel on the right (default) or the comment-mode bubble. Sent at each opening and switch.
    layout(m) {
      const was = S.lay.mode;
      S.lay.mode = m.mode;
      S.lay.maxW = Math.max(240, Math.min(320, Number.isFinite(m.maxWidth) && m.maxWidth > 0 ? Math.floor(m.maxWidth) : 320));   // R20: 320 px (user's decision over Paper's 305)
      S.lay.maxH = Math.max(104, Number.isFinite(m.maxHeight) && m.maxHeight > 0 ? Math.floor(m.maxHeight) : 520);
      S.lay.side = ["right", "left", "below", "above", "inside"].includes(m.side) ? m.side : "right";
      if (m.mode !== "bubble" || (isStr(m.elementKey, 1000) ? m.elementKey : "") !== S.lay.elementKey || (isStr(m.element, 300) ? m.element : "") !== S.lay.element) outboxEnd();
      S.lay.element = isStr(m.element, 300) ? m.element : "";
      S.lay.elementKey = isStr(m.elementKey, 1000) ? m.elementKey : "";
      if (m.room && typeof m.room === "object") { const r = {}; for (const k of ["top", "right", "bottom", "left"]) if (Number.isFinite(m.room[k])) r[k] = Math.max(0, Math.floor(m.room[k])); S.lay.room = r; }
      else if (m.open !== false) S.lay.room = null;   // F6: a stable key of the element (selector or source)
      const dim = (o) => (o && Number.isFinite(o.width) && Number.isFinite(o.height) ? { w: Math.floor(o.width), h: Math.floor(o.height) } : null);
      if (m.resizeMax !== undefined) S.lay.rMax = dim(m.resizeMax);   // R40: the largest size from the anchored corner
      if (m.resizeMin !== undefined) S.lay.rMin = dim(m.resizeMin);
      applyLayout(was !== m.mode);
      if (!BUB()) { applyInset(null); return; }
      // B 1.9: an opening layout is acknowledged (`bubble.size`, even unchanged). A layout with `open: false` is the
      // host's answer to a `bubble.size` (granted `inset`, new bounds): the page answers it only if what it wants changed.
      const opening = m.open !== false;
      if (opening) { closeMenu(); bTipHide(); }
      applyInset(m.inset);
      if (opening) bOpen();
      updateView();
      if (opening) ackSoon(); else publishSize();
    },
    "thread.reset"(m) {
      stashDraft();
      resetThread();
      const conv = isStr(m.conversationId, 128) ? m.conversationId : "";
      // H1's measure (opening < 150 ms): a long conversation draws its last exchanges first — what the bubble shows, at the
      // end — and the older ones right after the first image, put above without moving what is seen
      const gen = ++resetGen;
      const cut = firstCut(m.items);
      if (cut > 0) {
        const older = m.items.slice(0, cut);
        const lastOlderAt = older.filter((it) => it && it.type === "message.user").map((it) => Date.parse(it.at) || 0).pop() || 0;
        S.lastAt = lastOlderAt;
        H["thread.append"]({ items: m.items.slice(cut) });
        requestAnimationFrame(() => setTimeout(() => { if (gen === resetGen) prependOlder(older); }, 0));
      } else H["thread.append"](m);
      restoreDraft(draftKeyFor(conv, !log.firstChild));
      // M11 (review): a replayed turn with no end while nothing runs (the browser was closed during it) ends as stopped
      if (!S.st.running) for (const t of Array.from(S.turns.values())) if (t.running) { S.replay = true; try { onTurnEnd({ turnId: t.id, stopReason: "cancelled", interrupted: true }); } finally { S.replay = false; } }
      renderDock(true); placeholder(); updateView();
      scroller.scrollTop = scroller.scrollHeight; ackNow();
      requestAnimationFrame(() => { scroller.scrollTop = scroller.scrollHeight; });
    },
    "thread.append"(m) {   // (see also prependOlder: the older part of a long conversation, put above after the first image)
      S.replay = true;
      try { for (const it of m.items) if (it && typeof it.type === "string" && it.type !== "thread.reset" && it.type !== "thread.append") receive(Object.assign({ v: V }, it)); }
      finally { S.replay = false; }
      flushRender(); updateView();
    },
    "message.user": onUser,
    // R42: « Passé à Opus 5.5 · Moyen » — a model or a provider changed in the same chat (sent live, kept in the thread)
    "model.switch"(m) {
      const a = isStr(m.agentId, 128) ? agentById(m.agentId) : null;
      const who = isStr(m.model, 100) && m.model ? m.model : isStr(m.agentName, 100) && m.agentName ? m.agentName : a ? a.name : "";
      if (!who) return;
      const eff = isStr(m.effort, 100) || isStr(m.effortId, 100) ? turnEffort(effortName(m.effortId || "", m.effort || m.effortId || "")) : "";
      add(h("div", { class: "marker mswitch", role: "note", text: "Passé à " + [who, isStr(m.model, 100) && m.model ? eff : ""].filter(Boolean).join(" · ") }), !S.replay);
      S.switched = true;
      if (!S.replay) say("Passé à " + who + ".");
    },
    "turn.start"(m) {
      const t = S.turns.get(m.turnId);
      if (t) { updateTurnHead(t, isStr(m.model, 100) ? m.model : null, isStr(m.effort, 100) ? m.effort : null, ID.test(m.agentId || "") ? m.agentId : null); return; }
      newTurn(m.turnId, isStr(m.model, 100) ? m.model : null, isStr(m.effort, 100) ? m.effort : null, m.at, ID.test(m.agentId || "") ? m.agentId : null);
    },
    "message.agentChunk": onChunk, thought: onThought, toolCall: onToolCall, toolCallUpdate: onToolUpdate, plan: onPlan,
    "permission.request": onPermission,
    "permission.cancel"(m) { S.perms = S.perms.filter((p) => p.requestId !== m.requestId); refreshWaiting(); renderDock(true); },
    "turn.end": onTurnEnd, "turn.undone": onUndone, error: onError,
    strip(m) {
      if (m.clear) S.strips.delete(m.id);
      else S.strips.set(m.id, { id: m.id, tone: ["danger", "info", "neutral"].includes(m.tone) ? m.tone : "neutral", text: String(m.text || ""), action: m.action && ID.test(m.action.id) && isStr(m.action.label, 80) ? m.action : null });
      renderDock();
      const s = S.strips.get(m.id); if (s && s.tone === "danger") shout(s.text);
    },
    queue(m) {
      S.queue = m.items.filter((q) => q && ID.test(q.id) && isStr(q.text)).map((q) => ({ id: q.id, text: q.text, after: isStr(q.after, 300) ? q.after : "" }));
      for (const q of S.queue) outboxEnd(q.text);
      renderDock();
    },
    history(m) { S.history = { items: m.items.filter((i) => i && ID.test(i.id) && isStr(i.title, 300)), query: m.query }; if (S.historyOpen) renderHistoryList(); },
    "element.attached"(m) {
      if (BUB()) return;   // cv4 R35 / R7: the bubble's element is implicit — a chip never lands in a bubble
      if (S.cEls.has(m.element.id)) return;
      S.cEls.set(m.element.id, m.element);
      insertNodeAtCaret(chipNode(m.element, true)); updateSend();
      S.picking = false; renderComposerBar();
      say("Élément " + m.element.label + " ajouté.");
    },
    "attachment.added"(m) { S.cAtt.set(m.attachment.id, m.attachment); renderThumbs(); updateSend(); say("Image " + m.attachment.name + " jointe."); },
    "pick.state"(m) { S.picking = m.active; renderComposerBar(); },   // B 1.7: the button's aria-pressed says it (the browser announces the change)
    mentions(m) { S.mentions = { query: m.query, elements: (m.elements || []).filter(validElement), files: (m.files || []).filter((f) => f && isStr(f.path, 1000)) }; if (S.menu && S.menu.combo === "@" && mentionCtx) openMentionMenu(mentionCtx.q, mentionCtx.node, mentionCtx.offset); },
    commands(m) { S.commands = m.items.filter((c) => c && isStr(c.name, 80) && /^[\w-]+$/.test(c.name)); },
    "edit.confirm": onEditConfirm, "annotations.status": onAnnStatus,   // R24: `annotations.batch` is ignored (no batch in the input)
    focus() {
      app.classList.remove("b-out");
      if (S.perms.length) { const b = dock.querySelector(".btn.primary"); if (b) b.focus(); }
      else if (BUB() && S.bview) { const x = bviewEl.querySelector(".bview-x"); if (x) x.focus(); }
      else if (BUB() && S.st.running) app.focus({ preventScroll: true });   // no input during a turn (after an error the input is out of sight but takes what is written: H1 monkey #29)
      else cin.focus();
    },
    "copy.done": onCopyDone, "changes.diff": onChangesDiff,
    usage(m) { S.usage = m.size > 0 ? { used: Math.min(m.used, m.size), size: m.size } : null; renderGauge(); },
    // M2 / F4 (review): a real closing decided by the host (click elsewhere, element gone, mode switch, Échap): the surface goes at
    // once (no animation), inert and hidden from screen readers; the draft is put aside and the thread emptied, so the next
    // opening can never show the old conversation, even for one image before its thread.reset
    "bubble.hidden"() {
      if (!BUB()) return;
      closeMenu(); bTipHide(); tipHide(); chipHover(null, false); closeView(true);
      stashDraft(); draftKey = ""; resetThread(); clear(cin);
      app.classList.remove("b-in"); app.classList.add("b-out", "b-gone");
      app.inert = true; app.setAttribute("aria-hidden", "true");
      lastSize = "";
    },
    "pointer.leave"() { tipHide(); chipHover(null, false); },   // B 1.5: the host saw the cursor leave the panel
    "annotation.kept": onKept,          // B 1.6: Ctrl+Entrée wrote the annotation(s)
  };
  function receive(m) {
    if (typeof m === "string") { try { m = JSON.parse(m); } catch (e) { warn("bad json"); return; } }
    if (!m || typeof m !== "object" || typeof m.type !== "string") { warn("bad message"); return; }
    if (m.v !== V) { if (!S.fatal) { S.fatal = true; clear(app).appendChild(h("div", { class: "fatal", role: "alert", text: "Le panneau n'est pas à jour. Fermez-le puis rouvrez-le." })); } return; }
    if (S.fatal) return;
    const fn = H[m.type];
    if (!fn) return; // unknown type: ignored by contract
    if (REQ[m.type] && !REQ[m.type](m)) { warn("invalid " + m.type); return; }
    try { fn(m); } catch (e) { post("panel.log", { level: "error", message: (m.type + ": " + (e && e.stack || e)).slice(0, 2000) }); }
    if (!S.replay) publishSize();   // bubble: the height follows the content (no-op in the panel)
  }

  // ---------------------------------------------------------------- global keys
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "/") { e.preventDefault(); if (modelBtn.offsetParent) openModelMenu(modelBtn); return; }
    if (e.key !== "Escape" || e.defaultPrevented) return;
    if (S.menu) { e.preventDefault(); closeMenu(!S.menu.combo); return; }
    if (BUB()) {   // B 1.8: Échap never stops the agent nor refuses a request — it closes what is open, then the bubble
      e.preventDefault();
      if (btipPinned) bTipHide(); else if (S.bview) closeView(); else bClose();
      return;
    }
    if (S.view) { e.preventDefault(); closeChanges(); return; }
    if (S.historyOpen) { e.preventDefault(); toggleHistory(false); return; }
    if (S.editing) { e.preventDefault(); endEdit(); return; }
    if (S.perms.length) { e.preventDefault(); const r = dock.querySelector(".perm .btn:not(.primary)"); if (r) r.click(); return; }
    if (S.st.running) { e.preventDefault(); post("stop"); say("Arrêt demandé."); return; }
    post("close");
  });
  window.addEventListener("resize", () => { if (!BUB()) closeMenu(); });   // the bubble's window is resized for the menu itself
  reduceMQ.addEventListener && reduceMQ.addEventListener("change", () => {});

  // ---------------------------------------------------------------- boot
  // `index.html?layout=bubble`: the bubble from the first paint, before the host's `layout` (no flash of the panel).
  if (/[?&]layout=bubble(&|$)/.test(location.search)) { S.lay.mode = "bubble"; applyLayout(false); }
  renderHeader(); renderComposerBar(); renderDock(true); updateView();
  if (wv) wv.addEventListener("message", (e) => receive(e.data));
  window.addEventListener("error", (e) => post("panel.log", { level: "error", message: String(e.message).slice(0, 2000) }));
  post("ready", { v: V, build: BUILD });
})();
