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
    const n = document.createElement(tag);
    if (props) for (const k in props) {
      const v = props[k];
      if (v == null || v === false) continue;
      if (k === "class") n.className = v;
      else if (k === "text") n.textContent = v;
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
  const agentIcon = (a) => icon(a && (a.icon === "codex" || a.id === "codex") ? "codex" : a && (a.icon === "claude" || /claude/.test(a.id || "")) ? "claude" : "k-icon");
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
    perms: [], queue: [], strips: new Map(), history: null, historyOpen: false, commands: [],
    picking: false, menu: null, atBottom: true, newCount: 0,
    cEls: new Map(), cAtt: new Map(), editing: null, replay: false, fatal: false,
    planPending: null,
  };
  const agentById = (id) => (S.st.agents || []).find((a) => a.id === id) || null;
  const curAgent = () => agentById(S.st.agentId) || (S.st.agents || [])[0] || null;
  const curAgentName = () => (curAgent() && curAgent().name) || "l'agent";
  function curModel() {
    const a = curAgent();
    return a && a.models ? a.models.find((m) => m.id === S.st.model) || null : null;
  }
  const curEffort = () => (S.st.efforts || []).find((e) => e.id === S.st.effort) || null;
  const curMode = () => (S.st.modes || []).find((m) => m.id === S.st.mode) || null;

  // ---------------------------------------------------------------- skeleton
  const app = document.getElementById("app");
  const live = h("div", { class: "sr-only", role: "status", "aria-live": "polite" });
  const alertLive = h("div", { class: "sr-only", role: "alert", "aria-live": "assertive" });

  // header
  const hdLogo = h("span", { class: "hd-logo" });
  const hdSource = h("button", { class: "hd-source", type: "button", hidden: true, onclick: () => S.st.conversation && S.st.conversation.source && post("file.open", srcRef(S.st.conversation.source)) });
  const hdTitleText = h("span", { class: "hd-title-text", text: "Nouveau chat" });
  const hdSpin = h("span", { class: "spin", hidden: true, role: "img", "aria-label": "En cours" }, icon("spinner"));
  const hdTitle = h("button", { class: "hd-title", type: "button", "aria-haspopup": "dialog", "aria-expanded": "false", "aria-controls": "history",
                                onclick: () => toggleHistory() }, hdTitleText, hdSpin, icon("chevron", "chev"));
  const hd = h("header", { class: "hd" },
    h("div", { class: "hd-main" }, hdLogo, h("div", { class: "hd-titles" }, hdSource, hdTitle)),
    h("button", { class: "hd-btn", type: "button", "aria-label": "Nouveau chat", title: "Nouveau chat", onclick: () => post("newChat") }, icon("plus")),
    h("button", { class: "hd-btn", type: "button", "aria-label": "Historique", title: "Historique", onclick: () => toggleHistory() }, icon("history")),
    h("button", { class: "hd-btn", type: "button", "aria-label": "Fermer le chat", title: "Fermer", onclick: () => post("close") }, icon("close")));
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
  const main = h("div", { style: "position:relative;flex:1;min-height:0;display:flex;flex-direction:column" }, body, dock, historyEl);
  const menuLayer = h("div");
  app.append(hd, main, live, alertLive, menuLayer);

  scroller.addEventListener("scroll", () => {
    const at = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 24;
    S.atBottom = at;
    if (at && S.newCount) { S.newCount = 0; newSteps.hidden = true; }
  }, { passive: true });
  // Item heights settle after layout (content-visibility, fonts, code blocks): stay glued to the end.
  if (window.ResizeObserver) new ResizeObserver(() => { if (S.atBottom) scroller.scrollTop = scroller.scrollHeight; }).observe(log);
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
  const pickBtn = h("button", { class: "tb", type: "button", "aria-label": "Viser un élément de la page", title: "Viser un élément", "aria-pressed": "false",
                                onclick: () => post("pickElement", { active: !S.picking }) }, icon("cursor", "ic"));
  const plusBtn = h("button", { class: "tb", type: "button", "aria-label": "Joindre ou changer de dossier", title: "Joindre", "aria-haspopup": "menu", "aria-expanded": "false",
                                onclick: (e) => openPlusMenu(e.currentTarget) }, icon("plus", "ic"));
  const modeBtn = h("button", { class: "tb mode", type: "button", "aria-haspopup": "menu", "aria-expanded": "false", onclick: (e) => openModeMenu(e.currentTarget) });
  const modelName = h("span", { class: "mn" });
  const modelLogo = h("span", { style: "display:flex" });
  const modelBtn = h("button", { class: "m", type: "button", "aria-haspopup": "menu", "aria-expanded": "false", "aria-keyshortcuts": "Control+/",
                                 onclick: (e) => openModelMenu(e.currentTarget) }, modelLogo, modelName);
  const effortTxt = h("span");
  const effortBtn = h("button", { class: "e", type: "button", "aria-haspopup": "menu", "aria-expanded": "false",
                                  onclick: (e) => openEffortMenu(e.currentTarget) }, effortTxt, icon("chevron"));
  const sendBtn = h("button", { class: "send", type: "button", onclick: () => onSendButton() });
  const composer = h("div", { class: "composer" }, thumbs, cin,
    h("div", { class: "foot" }, h("div", { class: "tools" }, pickBtn, plusBtn, modeBtn), h("div", { class: "grow" }),
      h("div", { class: "model" }, modelBtn, effortBtn), sendBtn));
  const keysRow = h("div", { class: "keys", "aria-hidden": "true", hidden: true },
    h("span", { class: "k" }, h("span", { class: "kc" }, "↵"), "Envoyer"),
    h("span", { class: "k" }, h("span", { class: "kcs" }, h("span", { class: "kc" }, "Maj"), h("span", { class: "kc" }, "↵")), "Nouvelle ligne"));

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
  function hasContent() { return composerText().trim().length > 0 || S.cAtt.size > 0; }
  function canSend() {
    const a = curAgent();
    return !S.st.disabledReason && !!S.st.folder && !hasDangerStrip() && (!a || a.status === "ready" || a.status === "starting");
  }
  function updateSend() {
    const running = !!S.st.running, content = hasContent();
    clear(sendBtn);
    sendBtn.className = "send";
    if (running && !content) {
      sendBtn.classList.add("stop"); sendBtn.appendChild(icon("stop"));
      sendBtn.setAttribute("aria-label", "Arrêter (Échap)"); sendBtn.title = "Arrêter (Échap)"; sendBtn.removeAttribute("aria-disabled");
    } else {
      sendBtn.appendChild(icon("arrowUp"));
      const ok = content && canSend();
      if (ok) sendBtn.classList.add("ready");
      sendBtn.setAttribute("aria-label", running ? "Ajouter à la file" : "Envoyer");
      sendBtn.title = running ? "Ajouter à la file (Entrée)" : "Envoyer (Entrée)";
      if (ok) sendBtn.removeAttribute("aria-disabled"); else sendBtn.setAttribute("aria-disabled", "true");
    }
    const lines = composerText().split("\n").length;
    keysRow.hidden = !(lines > 1 && document.activeElement === cin);
  }
  function onSendButton() {
    if (sendBtn.classList.contains("stop")) { post("stop"); return; }
    send();
  }
  function send() {
    if (!hasContent() || !canSend()) return;
    const text = composerText().replace(/\s+$/, "");
    post("send", { text, elements: chipsInComposer().filter((id) => S.cEls.has(id)), attachments: Array.from(S.cAtt.keys()) });
    S.cEls.clear(); S.cAtt.clear(); clear(cin); renderThumbs(); closeMenu(); updateSend();
  }
  function placeholder() {
    let p = "Que faut-il changer ?";
    if (S.planPending) p = "Ajouter une précision au plan…";
    else if (S.st.running) p = "Ajouter une suite…";
    else if (S.lastTurn) p = "Autre chose à changer ?";
    cin.setAttribute("data-placeholder", p);
  }
  function chipNode(e, inComposer) {
    const n = h("span", { class: "chip", "data-id": e.id, "data-label": e.label, contenteditable: inComposer ? "false" : null,
                         title: [e.selector || e.label, e.source].filter(Boolean).join(" · ") },
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

  cin.addEventListener("input", () => {
    if (!composerText().trim() && !cin.querySelector(".chip")) clear(cin);
    syncChips(); detectTrigger();
  });
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
    if (e.key === "Enter" && !e.shiftKey && !e.isComposing) { e.preventDefault(); send(); return; }
    if (e.key === "Enter" && e.shiftKey) { e.preventDefault(); document.execCommand("insertText", false, "\n"); return; }
    if (e.key === "Tab" && e.shiftKey && (S.st.modes || []).length > 1) {
      e.preventDefault();
      const ms = S.st.modes, i = ms.findIndex((m) => m.id === S.st.mode);
      const next = ms[(i + 1) % ms.length];
      post("setMode", { id: next.id }); S.st.mode = next.id; renderComposerBar(); say("Mode " + next.label);
    }
  });

  // @ and / triggers
  let mentionTimer = 0;
  function detectTrigger() {
    const text = composerText();
    if (/^\/[\w-]*$/.test(text) && (S.commands || []).length) { openCommandMenu(text.slice(1)); return; }
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
    S.picking ? pickBtn.setAttribute("aria-pressed", "true") : pickBtn.setAttribute("aria-pressed", "false");
    const mode = curMode();
    clear(modeBtn);
    modeBtn.className = "tb mode";
    modeBtn.hidden = !(S.st.modes || []).length;
    if (mode) {
      const kind = mode.kind || "other";
      if (kind === "auto") { modeBtn.classList.add("auto"); modeBtn.append(icon("auto", "ic"), h("span", { class: "lbl", text: mode.label })); }
      else if (kind === "other") modeBtn.append(h("span", { class: "lbl", text: mode.label, style: "color:#FFFFFFB3" }), icon("chevron", "cv"));
      else modeBtn.append(icon(kind === "plan" ? "list" : "shield", "ic"), icon("chevron", "cv"));
      modeBtn.setAttribute("aria-label", "Mode : " + mode.label);
      modeBtn.title = "Mode : " + mode.label + " (Maj+Tab)";
    }
    const a = curAgent(), m = curModel();
    clear(modelLogo).appendChild(agentIcon(a));
    modelName.textContent = m ? m.label : a ? a.name : "Agent";
    modelBtn.setAttribute("aria-label", "Modèle : " + modelName.textContent);
    modelBtn.title = "Modèle (Ctrl+/)";
    const e = curEffort();
    effortBtn.hidden = !(S.st.efforts || []).length;
    effortTxt.textContent = e ? e.label : "Effort";
    effortBtn.setAttribute("aria-label", "Effort : " + effortTxt.textContent);
    placeholder();
    updateSend();
  }

  // ---------------------------------------------------------------- menus
  function closeMenu(restore) {
    if (!S.menu) return;
    const m = S.menu;
    S.menu = null;
    m.el.remove();
    if (m.anchor) m.anchor.setAttribute("aria-expanded", "false");
    if (m.combo) { cin.removeAttribute("aria-activedescendant"); cin.removeAttribute("aria-controls"); }
    if (restore && m.anchor) m.anchor.focus();
  }
  function place(menuEl, anchor, align, width) {
    const ar = app.getBoundingClientRect(), r = anchor.getBoundingClientRect();
    menuEl.style.width = Math.min(width, ar.width - 16) + "px";
    menuEl.style.bottom = (ar.bottom - r.top + 6) + "px";
    const w = Math.min(width, ar.width - 16);
    let left = align === "right" ? r.right - ar.left - w : r.left - ar.left;
    left = Math.max(8, Math.min(left, ar.width - 8 - w));
    menuEl.style.left = left + "px";
    menuEl.style.setProperty("--origin", (align === "right" ? "bottom right" : "bottom left"));
  }
  // Button-opened menu: focus moves in, arrows move, Enter picks, Escape returns to the trigger.
  function openMenu(anchor, opts) {
    if (S.menu && S.menu.anchor === anchor && !opts.keep) { closeMenu(true); return; }
    closeMenu();
    const el = h("div", { class: "menu", role: "menu", "aria-label": opts.label, tabindex: "-1" });
    S.menu = { el, anchor, opts };
    fillMenu(el, opts.items);
    menuLayer.appendChild(el);
    place(el, anchor, opts.align, opts.width);
    anchor.setAttribute("aria-expanded", "true");
    const items = menuItems();
    (items.find((i) => i.getAttribute("aria-checked") === "true") || items[0] || el).focus();
    el.addEventListener("keydown", (e) => {
      const its = menuItems(), i = its.indexOf(document.activeElement);
      if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); const n = its.length; if (n) its[(i + (e.key === "ArrowDown" ? 1 : n - 1) + n) % n].focus(); }
      else if (e.key === "Home") { e.preventDefault(); its[0] && its[0].focus(); }
      else if (e.key === "End") { e.preventDefault(); its[its.length - 1] && its[its.length - 1].focus(); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); closeMenu(true); }
      else if (e.key === "Tab") closeMenu();
      else if (e.key === "ArrowRight" && document.activeElement && document.activeElement.dataset.sub) { e.preventDefault(); document.activeElement.click(); }
    });
  }
  const menuItems = () => S.menu ? Array.from(S.menu.el.querySelectorAll(".mi:not([aria-disabled='true']), .menu .btn, .menu .btn-s")) : [];
  function fillMenu(el, items) {
    clear(el);
    for (const it of items) {
      if (it.sep) { el.appendChild(h("div", { class: "sep", role: "separator" })); continue; }
      if (it.head) { el.appendChild(h("div", { class: "mh", role: "presentation", text: it.head })); continue; }
      if (it.node) { el.appendChild(it.node); continue; }
      const b = h("button", { class: "mi" + (it.desc ? " top" : ""), type: "button", role: it.radio ? "menuitemradio" : "menuitem", tabindex: "-1",
                              "aria-checked": it.radio ? String(!!it.checked) : null, "aria-disabled": it.disabled ? "true" : null, "data-sub": it.sub ? "1" : null,
                              onclick: () => { if (!it.disabled) it.onSelect(); } });
      if (it.lead) b.appendChild(it.lead);
      const g = h("span", { class: "grow", style: "flex:1" }, h("span", { class: "row1" }, h("span", { class: "lb", text: it.label }), it.tag ? h("span", { class: "tag", text: it.tag }) : null));
      if (it.desc) g.appendChild(h("span", { class: "ds", text: it.desc }));
      b.appendChild(g);
      if (it.value) b.appendChild(h("span", { class: "val", text: it.value }));
      if (it.checked) b.appendChild(icon("check", "ck"));
      if (it.sub) b.appendChild(icon("chevron", "sub"));
      el.appendChild(b);
    }
  }
  document.addEventListener("pointerdown", (e) => {
    if (S.menu && !S.menu.el.contains(e.target) && !(S.menu.anchor && S.menu.anchor.contains(e.target)) && e.target !== cin && !cin.contains(e.target)) closeMenu();
  }, true);

  function openModelMenu(anchor, expanded) {
    const a = curAgent(), items = [];
    const models = (a && a.models) || [];
    const shown = expanded || models.length <= 4 ? models : models.slice(0, 4);
    for (const m of shown) items.push({ radio: true, label: m.label, tag: m.isDefault ? "Par défaut" : null, checked: m.id === S.st.model, desc: null,
                                         onSelect: () => { post("setModel", { id: m.id, agentId: a.id }); S.st.model = m.id; closeMenu(true); renderComposerBar(); } });
    if (models.length > 4 && !expanded) items.push({ label: "Autres modèles", sub: true, onSelect: () => openModelMenu(anchor, true) });
    for (const o of (S.st.agents || []).filter((x) => x !== a)) {
      items.push({ sep: true });
      if (o.status === "ready" && o.models && o.models.length) {
        for (const m of o.models.slice(0, 4)) items.push({ label: m.label, onSelect: () => confirmSwitch(anchor, o, m) });
      } else items.push({ node: agentRow(o, true) });
    }
    if ((S.st.efforts || []).length) {
      items.push({ sep: true });
      items.push({ label: "Effort", value: curEffort() ? curEffort().label : "", sub: true, onSelect: () => { closeMenu(); openEffortMenu(effortBtn); } });
    }
    openMenu(anchor, { label: "Modèle", items, align: "right", width: 300, keep: !!expanded });
  }
  function confirmSwitch(anchor, agent, model) {
    const go = h("button", { class: "btn primary", type: "button", onclick: () => { post("setModel", { id: model.id, agentId: agent.id }); closeMenu(true); } }, "Passer à " + agent.name);
    const no = h("button", { class: "btn", type: "button", onclick: () => closeMenu(true) }, "Annuler");
    fillMenu(S.menu.el, [{ node: h("div", { class: "mwarn", role: "group", "aria-label": "Changer d'agent" },
      h("span", { text: "Passer à " + agent.name + " ouvre un nouveau chat. Celui-ci reste dans l'historique." }), h("div", { class: "row" }, go, no)) }]);
    go.focus();
    say("Passer à " + agent.name + " ouvre un nouveau chat.");
  }
  function agentAction(a) {
    if (a.status === "notConnected") return { label: "Se connecter", run: () => post("agent.connect", { agentId: a.id, methodId: a.authMethods && a.authMethods[0] ? a.authMethods[0].id : undefined }) };
    if (a.status === "notInstalled") return { label: "Installer", run: () => post("agent.prepare", { agentId: a.id }) };
    if (a.status === "error") return { label: "Réessayer", run: () => post("agent.prepare", { agentId: a.id }) };
    return null;
  }
  const statusText = (a) => a.detail || ({ ready: "Installé", starting: "Démarrage…", preparing: "Préparation…", notInstalled: "Pas installé", notConnected: "Pas connecté", error: "Erreur" })[a.status] || "";
  function agentRow(a, inMenu) {
    const act = agentAction(a);
    return h("div", { class: "agent-row", role: "group", "aria-label": a.name + ", " + statusText(a) }, agentIcon(a),
      h("span", { class: "grow" }, h("span", { class: "lb", text: a.name }), h("span", { class: "ds", text: statusText(a) })),
      act ? h("button", { class: "btn-s", type: "button", onclick: () => { act.run(); if (inMenu) closeMenu(true); } }, act.label) : null);
  }
  function openEffortMenu(anchor) {
    const items = (S.st.efforts || []).map((e) => ({ radio: true, label: e.label, checked: e.id === S.st.effort,
      onSelect: () => { post("setEffort", { id: e.id }); S.st.effort = e.id; closeMenu(true); renderComposerBar(); } }));
    openMenu(anchor, { label: "Effort", items, align: "right", width: 214 });
  }
  function openModeMenu(anchor) {
    const items = (S.st.modes || []).map((m) => ({ radio: true, label: m.label, desc: m.description, checked: m.id === S.st.mode,
      lead: m.kind === "auto" ? icon("auto", "lead auto") : m.kind === "plan" ? icon("list", "lead") : m.kind === "ask" ? icon("shield", "lead") : null,
      onSelect: () => { post("setMode", { id: m.id }); S.st.mode = m.id; closeMenu(true); renderComposerBar(); } }));
    openMenu(anchor, { label: "Mode", items, align: "left", width: 290 });
  }
  function openPlusMenu(anchor) {
    const items = [];
    if (S.st.capabilities && S.st.capabilities.image === false) items.push({ label: "Joindre une image", desc: "Cet agent ne lit pas les images", disabled: true, lead: icon("k-image", "lead"), onSelect: () => {} });
    else items.push({ label: "Joindre une image", lead: icon("k-image", "lead"), onSelect: () => { closeMenu(true); post("attachImage", { source: "picker" }); } });
    items.push({ label: "Choisir un autre dossier…", desc: S.st.folder ? S.st.folder.display : null, lead: icon("folder", "lead"), onSelect: () => { closeMenu(true); post("folder.choose"); } });
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
    const rows = (S.commands || []).filter((c) => c.name.toLowerCase().startsWith(ql) || !ql).slice(0, 8)
      .map((c) => ({ label: c.name, desc: c.description, pick: () => { closeMenu(); clear(cin); cin.focus(); document.execCommand("insertText", false, "/" + c.name + " "); updateSend(); } }));
    openCombo("/", rows);
  }

  // ---------------------------------------------------------------- dock (strips, queue, permission, composer)
  function derivedStrips() {
    const out = [];
    if (S.st.disabledReason) out.push({ id: "site", tone: "neutral", text: S.st.disabledReason });
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
    const all = Array.from(S.strips.values()).concat(derivedStrips());
    return all.find((s) => s.tone === "danger") || all[0] || null;
  }
  const hasDangerStrip = () => { const s = activeStrip(); return !!(s && s.tone === "danger"); };
  function stripAction(s) {
    if (s.id === "agent") { const a = curAgent(); const act = a && agentAction(a); if (act) act.run(); return; }
    if (s.errorId) post("error.action", { errorId: s.errorId, actionId: s.action.id });
    else post("strip.action", { stripId: s.id, actionId: s.action.id });
  }
  let lastDockKey = "";
  function renderDock(force) {
    const p = S.perms[0];
    const s = activeStrip();
    const key = JSON.stringify([p && p.requestId, S.perms.length, s, S.queue, S.st.disabledReason]);
    if (!force && key === lastDockKey) { composer.classList.toggle("disabled", !!(S.st.disabledReason || (s && s.tone === "danger"))); updateSend(); return; }
    lastDockKey = key;
    const hadFocusInComposer = composer.contains(document.activeElement);
    clear(dock);
    if (p) { dock.appendChild(permCard(p)); return; }
    for (const q of S.queue) {
      dock.appendChild(h("div", { class: "queue", role: "group", "aria-label": "Suite en file" },
        h("span", { class: "tag", text: "En file" }), h("span", { class: "tx", text: q.text, title: q.text }),
        h("button", { class: "btn-s", type: "button", onclick: () => post("queue.sendNow", { id: q.id }) }, "Envoyer maintenant"),
        h("button", { class: "icon-btn", type: "button", "aria-label": "Retirer de la file", onclick: () => post("queue.remove", { id: q.id }) }, icon("close"))));
    }
    const stack = h("div", { class: "stack" });
    if (s) {
      const strip = h("div", { class: "strip " + (s.tone === "danger" ? "danger" : ""), role: s.tone === "danger" ? "alert" : "status" },
        h("div", { class: "tx" }, s.text, s.progress != null ? h("div", { class: "progress", role: "progressbar", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": String(Math.round(s.progress * 100)) },
          h("i", { style: "width:" + Math.round(s.progress * 100) + "%" })) : null),
        s.action ? h("button", { class: "btn-s", type: "button", onclick: () => stripAction(s) }, s.action.label) : null);
      stack.appendChild(strip);
    }
    stack.appendChild(composer);
    composer.classList.toggle("disabled", !!(S.st.disabledReason || (s && s.tone === "danger")));
    cin.setAttribute("contenteditable", S.st.disabledReason ? "false" : "true");
    cin.setAttribute("aria-disabled", S.st.disabledReason ? "true" : "false");
    dock.append(stack, keysRow);
    if (hadFocusInComposer) cin.focus();
    updateSend();
  }

  function permCard(p) {
    const allowOnce = p.options.find((o) => o.kind === "allow_once") || p.options.find((o) => /^allow/.test(o.kind));
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
      if (!S.perms.length) cin.focus(); else dock.querySelector(".btn.primary") && dock.querySelector(".btn.primary").focus();
    };
    const refuse = h("button", { class: "btn", type: "button", "aria-keyshortcuts": "Escape", onclick: () => respond(reject, instead.value.trim() || undefined) },
      "Refuser", h("span", { class: "kbd", "aria-hidden": "true" }, "Échap"));
    const allow = h("button", { class: "btn primary", type: "button", "aria-keyshortcuts": "Enter",
                                onclick: () => respond(always && allowAlways ? allowAlways : allowOnce || allowAlways) },
      "Autoriser", h("span", { class: "kbd", "aria-hidden": "true" }, "↵"));
    instead.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.isComposing) { e.preventDefault(); if (instead.value.trim()) respond(reject, instead.value.trim()); else allow.click(); }
    });
    const card = h("div", { class: "perm" + (S.replay ? "" : " pop-in"), role: "alertdialog", "aria-modal": "false", "aria-labelledby": "perm-t", "aria-describedby": "perm-d" },
      h("div", { class: "perm-hd" }, h("span", { class: "perm-t", id: "perm-t", text: p.title }),
        S.perms.length > 1 ? h("span", { class: "perm-of", text: "1 sur " + S.perms.length }) : null,
        p.diff ? h("span", { class: "num plus", text: "+" + p.diff.added }) : null, p.diff ? h("span", { class: "num minus", text: "−" + p.diff.removed }) : null),
      h("div", { id: "perm-d" }, p.diff ? diffView(p.diff) : null, p.command ? h("pre", { class: "cmd", text: p.command }) : null),
      instead,
      h("div", { class: "perm-ft" }, sw, refuse, allow));
    card.addEventListener("keydown", (e) => {
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); refuse.click(); }
      else if (e.key === "Enter" && e.target !== instead && e.target.tagName !== "BUTTON") { e.preventDefault(); allow.click(); }
    });
    requestAnimationFrame(() => allow.focus());
    return card;
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

  // ---------------------------------------------------------------- thread
  function updateView() {
    const firstRun = S.st.folder === null;
    const isEmpty = !log.firstChild;
    first.hidden = !firstRun;
    empty.hidden = firstRun || !isEmpty;
    log.hidden = firstRun || isEmpty;
    if (firstRun) renderFirst();
    else if (isEmpty) renderEmpty();
  }
  function add(node, liveItem) {
    if (!S.replay) node.classList.add("enter", "live");
    log.appendChild(node);
    if (liveItem === false || S.replay) node.classList.remove("live");
    if (!empty.hidden || log.hidden) updateView();
    stick();
    return node;
  }
  function dayLabel(t) {
    const d = new Date(t), now = new Date();
    const hm = d.toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" });
    const sameDay = (a, b) => a.toDateString() === b.toDateString();
    const y = new Date(now); y.setDate(now.getDate() - 1);
    if (sameDay(d, now)) return "Aujourd'hui · " + hm;
    if (sameDay(d, y)) return "Hier · " + hm;
    return d.toLocaleDateString("fr-FR", { day: "numeric", month: "short" }) + " · " + hm;
  }

  function onUser(m) {
    const at = Date.parse(m.at) || Date.now();
    if (!S.lastAt || new Date(at).toDateString() !== new Date(S.lastAt).toDateString() || at - S.lastAt > 30 * 60 * 1000) add(h("div", { class: "date", text: dayLabel(at) }), false);
    S.lastAt = at;
    const els = (m.elements || []).filter(validElement);
    const bubble = h("div", { class: "bubble" });
    const inlineIds = fillWithChips(bubble, m.text, els);
    const pencil = m.editable === false ? null : h("button", { class: "icon-btn", type: "button", "aria-label": "Modifier ce message", title: "Modifier", onclick: () => startEdit(m.id) }, icon("pencil"));
    const row = h("div", { class: "um-row" }, h("div", { class: "um-tools" }, pencil), bubble);
    const under = h("div", { class: "um-attach" });
    for (const e of els) if (!inlineIds.has(e.id)) under.appendChild(chipNode(Object.assign({ picked: true }, e), false));
    for (const a of (m.attachments || []).filter(validAttachment)) under.appendChild(h("div", { class: "thumb", title: a.name }, h("img", { src: a.thumb, alt: a.name, style: "width:100%;height:100%;object-fit:cover;border-radius:5px;display:block" })));
    const node = h("div", { class: "um", "data-id": m.id }, row, under.firstChild ? under : null);
    S.users.set(m.id, { el: node, row, bubble, text: m.text, els, m });
    S.lastUser = m.id;
    add(node);
    if (!S.replay) { scrollToEnd(false); }
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

  function newTurn(id, model, effort, at) {
    const t = { id, model: model || (curModel() ? curModel().label : ""), effort: effort || (curEffort() ? curEffort().label : ""), start: Date.parse(at) || Date.now(),
                running: true, segs: [], userId: S.lastUser, metaEl: h("div", { class: "meta" }), workEl: h("div", { class: "working", "aria-hidden": "true" }), waitEl: null, endEl: null };
    t.el = h("div", { class: "turn", "data-turn": id, "aria-busy": "true" }, t.metaEl, t.workEl);
    t.metaEl.textContent = [t.model, t.effort].filter(Boolean).join(" · ");
    t.workEl.append("Travaille… ", h("span", { class: "t", text: "0 s" }));
    const prev = S.lastTurn && S.turns.get(S.lastTurn);
    if (prev && (prev.model !== t.model || prev.effort !== t.effort)) {
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
  function addSeg(t, seg) {
    t.segs.push(seg);
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
    const seg = { kind: "text", messageId, text: "", spoken: 0, el, render() { doc.set(seg.text); el.classList.toggle("streaming", t.running && seg === t.segs.filter((s) => s.kind === "text").pop()); } };
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

  const KIND_ORDER = ["read", "search", "edit", "delete", "move", "execute", "fetch", "think", "other"];
  function stepsSummary(tools) {
    const by = {};
    for (const x of tools) (by[x.kind] = by[x.kind] || []).push(x);
    const parts = [];
    let filesSaid = false;
    for (const k of KIND_ORDER) {
      const n = (by[k] || []).length;
      if (!n) continue;
      const firstP = !parts.length;
      let s;
      switch (k) {
        case "read": s = (firstP ? "A lu " : "a lu ") + plural(n, "fichier", "fichiers"); filesSaid = true; break;
        case "edit": s = filesSaid ? "en a modifié " + n : (firstP ? "A modifié " : "a modifié ") + plural(n, "fichier", "fichiers"); filesSaid = true; break;
        case "delete": s = filesSaid ? "en a supprimé " + n : (firstP ? "A supprimé " : "a supprimé ") + plural(n, "fichier", "fichiers"); break;
        case "move": s = (firstP ? "A déplacé " : "a déplacé ") + plural(n, "fichier", "fichiers"); break;
        case "search": s = (firstP ? "A cherché " : "a cherché ") + n + " fois"; break;
        case "execute": s = (firstP ? "A lancé " : "a lancé ") + plural(n, "commande", "commandes"); break;
        case "fetch": s = (firstP ? "A consulté " : "a consulté ") + plural(n, "page", "pages"); break;
        default: s = (firstP ? "" : "et ") + plural(n, "étape", "étapes"); break;
      }
      parts.push(s);
    }
    return parts.join(", ");
  }
  function stepLine(x) {
    const run = x.status === "in_progress" || x.status === "pending";
    const fail = x.status === "failed";
    const ic = run ? h("span", { class: "spin", style: "display:flex" }, icon("spinner")) : fail ? icon("x") : icon("check");
    const label = x.title || (x.locations && x.locations[0] ? x.locations[0].path : "Étape");
    return h("div", { class: "step " + (run ? "run" : fail ? "fail" : "done"), role: "listitem" }, ic, h("span", { text: label }), fail ? h("span", { class: "fail-tag", text: " (échec)" }) : null);
  }
  function stepsSeg(t) {
    const el = h("div", { class: "steps" });
    let open = false;
    const seg = { kind: "steps", tools: [], el, render() {
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
    if (!seg || seg.kind !== "steps") seg = stepsSeg(t);
    else noteNew(1);
    const x = { id: m.id, title: m.title, kind: m.kind || "other", status: m.status || "pending", locations: m.locations || [], diff: m.diff };
    seg.tools.push(x);
    S.tools.set(m.id, { x, seg, t });
    queueRender(seg);
    if (x.status === "in_progress") say(x.title);
  }
  function onToolUpdate(m) {
    const r = S.tools.get(m.id);
    if (!r) return;
    for (const k of ["title", "status", "locations", "diff"]) if (m[k] != null) r.x[k] = m[k];
    queueRender(r.seg);
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
    S.perms.push({ requestId: m.requestId, turnId: m.turnId || S.lastTurn, title: m.title || "Autorisation demandée", kind: m.kind, diff: validDiff(m.diff), command: typeof m.command === "string" ? m.command : null, options: m.options });
    refreshWaiting();
    renderDock(true);
    noteNew(1);
    shout(m.title + ". Autoriser avec Entrée, refuser avec Échap.");
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
          h("button", { class: "file", type: "button", role: "listitem", title: "Voir les changements", onclick: () => post("checkpoint.viewChanges", { turnId: t.id, path: c.path }) },
            h("span", { class: "p", text: c.path }), c.status === "created" ? h("span", { class: "tag", text: "nouveau" }) : c.status === "deleted" ? h("span", { class: "tag", text: "supprimé" }) : null,
            h("span", { class: "num plus", text: "+" + (c.added || 0) }), h("span", { class: "num minus", text: "−" + (c.removed || 0) }))).concat(
          changes.length === 1 && validDiff(changes[0].diff) ? [diffView(validDiff(changes[0].diff))] : [])));
      }
    };
    render();
    box._render = render;
    return box;
  }

  function onTurnEnd(m) {
    const t = turnOf(m.turnId);
    t.running = false;
    t.el.setAttribute("aria-busy", "false");
    t.workEl.remove();
    if (t.waitEl) { t.waitEl.remove(); t.waitEl = null; }
    S.perms = S.perms.filter((p) => p.turnId !== t.id);
    const d = typeof m.durationMs === "number" ? m.durationMs : Date.now() - t.start;
    t.metaEl.textContent = [t.model.replace(/\s+[\d.]+$/, ""), t.effort, dur(d)].filter(Boolean).join(" · ");
    for (const s of t.segs) { if (s.kind === "text") { s.render(); s.el.classList.remove("streaming"); const rest = s.text.slice(s.spoken); if (rest.trim()) say(plain(rest)); s.spoken = s.text.length; } else s.render(); }
    const notes = { cancelled: "Arrêté.", refusal: "L'agent a refusé de continuer cette demande.", max_tokens: "La réponse a atteint sa longueur maximale. Demandez la suite pour continuer." };
    if (notes[m.stopReason]) t.el.appendChild(h("div", { class: "note", text: notes[m.stopReason] }));
    const changes = Array.isArray(m.changes) ? m.changes.filter((c) => c && typeof c.path === "string") : [];
    if (changes.length) { t.changesEl = changesBar(t, changes, m.undoable !== false); t.el.appendChild(t.changesEl); }
    const copyText = typeof m.copyText === "string" ? m.copyText : t.segs.filter((s) => s.kind === "text").map((s) => s.text).join("\n\n");
    if (m.stopReason !== "error") t.el.appendChild(h("div", { class: "actions" },
      h("button", { class: "icon-btn", type: "button", "aria-label": "Copier la réponse", title: "Copier", onclick: (e) => { lastCopy = null; post("copy", { text: copyText }); } }, icon("copy")),
      h("button", { class: "icon-btn", type: "button", "aria-label": "Relancer", title: "Relancer", onclick: () => post("retry", { turnId: t.id }) }, icon("history"))));
    if (m.checkpointId && ID.test(m.checkpointId)) {
      const u = S.users.get(t.userId);
      if (u && !u.bubble.querySelector(".rewind")) u.bubble.appendChild(h("button", { class: "rewind", type: "button", title: "Revenir à l'état d'avant ce message",
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
    if (m.scope === "composer") {
      S.strips.set("err:" + m.id, { id: "err:" + m.id, errorId: m.id, tone: "danger", text: m.message || m.title, action: (m.actions || [])[0] });
      renderDock(true);
      shout(m.title + ". " + (m.message || ""));
      return;
    }
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
    if (sug) q.appendChild(h("div", { class: "tile found" }, icon("folder", "ic"),
      h("div", { class: "grow" }, h("div", { class: "nm" }, h("span", { text: sug.name }), h("span", { class: "badge", text: "Détecté auto" })), h("div", { class: "path", text: sug.display, title: sug.path })),
      h("button", { class: "btn primary", type: "button", "aria-label": "Utiliser le dossier " + sug.name, onclick: () => post("folder.use", { path: sug.path }) }, "Utiliser")));
    q.appendChild(h("button", { class: "tile other", type: "button", onclick: () => post("folder.choose") }, icon("plus", "ic"), h("span", { class: "lb", text: "Choisir un autre dossier…" })));
    q.querySelector(".tile.other .ic").style.opacity = ".6";
    if (sug) q.querySelector(".tile.found .ic").style.opacity = ".8";
    first.appendChild(q);
    const agents = S.st.agents || [];
    if (agents.length) {
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
    if (S.st.pendingAnnotations > 0) sugs.push({ id: "annotations", label: "Traiter les " + plural(S.st.pendingAnnotations, "annotation en attente", "annotations en attente").replace(/^1 /, "1 "), badge: String(S.st.pendingAnnotations) });
    for (const s of S.st.suggestions || []) if (s && ID.test(s.id) && typeof s.label === "string") sugs.push(s);
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
    hdTitle.setAttribute("aria-expanded", String(open));
    historyEl.hidden = !open;
    if (open) { closeMenu(); post("history.open"); renderHistory(); const i = historyEl.querySelector("input"); if (i) i.focus(); }
    else hdTitle.focus();
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
      h("span", { class: "grow" }, h("span", { class: "t", text: it.title }), h("span", { class: "s", text: other ? [it.siteLabel, it.subtitle].filter(Boolean).join(" · ") : it.subtitle || "" })),
      it.running ? h("span", { class: "live" }, h("i"), "En cours") : h("span", { class: "when", text: relTime(it.updatedAt) }));
    if (mine.length) { list.appendChild(h("div", { class: "hsec", text: "Ce site" + (S.st.site ? " · " + S.st.site.label : "") })); mine.forEach((i) => list.appendChild(row(i, false))); }
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
    hdSpin.hidden = !(S.st.running || (c && c.running));
    hdTitle.setAttribute("aria-label", hdTitleText.textContent + " — ouvrir l'historique");
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
  function validDiff(d) {
    if (!d || !isStr(d.path, 1000) || !Array.isArray(d.lines)) return null;
    return { path: d.path, added: d.added | 0, removed: d.removed | 0, truncated: !!d.truncated || d.lines.length > 400,
             lines: d.lines.filter((l) => l && isStr(l.text, 4000) && ["+", "-", " ", "@"].includes(l.op)).slice(0, 400) };
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
    "edit.confirm": (m) => ID.test(m.messageId), state: (m) => true, hello: (m) => true, focus: () => true, "copy.done": () => true,
  };

  // ---------------------------------------------------------------- dispatch
  const H = {
    hello(m) { if (m.reducedMotion) document.documentElement.classList.add("reduce-motion"); app.removeAttribute("aria-busy"); },
    state(m) {
      const keys = ["site", "folder", "folderSuggestion", "agents", "agentId", "model", "efforts", "effort", "modes", "mode", "conversation", "running", "pendingAnnotations", "suggestions", "capabilities", "disabledReason"];
      for (const k of keys) if (k in m) S.st[k] = m[k];
      S.st.agents = (S.st.agents || []).filter((a) => a && ID.test(a.id) && isStr(a.name, 100));
      if (!S.st.agentId && S.st.agents[0]) S.st.agentId = S.st.agents[0].id;
      renderHeader(); renderComposerBar(); updateView(); renderDock(); refreshWaiting();
      if (S.historyOpen) renderHistoryList();
    },
    "thread.reset"(m) {
      clear(log); S.users.clear(); S.turns.clear(); S.tools.clear(); S.perms = []; S.lastUser = null; S.lastTurn = null; S.lastAt = 0; S.planPending = null; S.editing = null;
      S.atBottom = true; S.newCount = 0; newSteps.hidden = true;
      tick();
      H["thread.append"](m);
      renderDock(true); placeholder(); updateView();
      requestAnimationFrame(() => { scroller.scrollTop = scroller.scrollHeight; });
    },
    "thread.append"(m) {
      S.replay = true;
      try { for (const it of m.items) if (it && typeof it.type === "string" && it.type !== "thread.reset" && it.type !== "thread.append") receive(Object.assign({ v: V }, it)); }
      finally { S.replay = false; }
      flushRender(); updateView();
    },
    "message.user": onUser,
    "turn.start"(m) { if (S.turns.has(m.turnId)) return; newTurn(m.turnId, isStr(m.model, 100) ? m.model : null, isStr(m.effort, 100) ? m.effort : null, m.at); },
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
    queue(m) { S.queue = m.items.filter((q) => q && ID.test(q.id) && isStr(q.text)); renderDock(); },
    history(m) { S.history = { items: m.items.filter((i) => i && ID.test(i.id) && isStr(i.title, 300)), query: m.query }; if (S.historyOpen) renderHistoryList(); },
    "element.attached"(m) {
      if (S.cEls.has(m.element.id)) return;
      S.cEls.set(m.element.id, m.element);
      insertNodeAtCaret(chipNode(m.element, true)); updateSend();
      S.picking = false; renderComposerBar();
      say("Élément " + m.element.label + " ajouté.");
    },
    "attachment.added"(m) { S.cAtt.set(m.attachment.id, m.attachment); renderThumbs(); updateSend(); say("Image " + m.attachment.name + " jointe."); },
    "pick.state"(m) { S.picking = m.active; renderComposerBar(); say(m.active ? "Viseur actif : cliquez un élément de la page." : ""); },
    mentions(m) { S.mentions = { query: m.query, elements: (m.elements || []).filter(validElement), files: (m.files || []).filter((f) => f && isStr(f.path, 1000)) }; if (S.menu && S.menu.combo === "@" && mentionCtx) openMentionMenu(mentionCtx.q, mentionCtx.node, mentionCtx.offset); },
    commands(m) { S.commands = m.items.filter((c) => c && isStr(c.name, 80) && /^[\w-]+$/.test(c.name)); },
    "edit.confirm": onEditConfirm,
    focus() { if (S.perms.length) { const b = dock.querySelector(".btn.primary"); if (b) b.focus(); } else cin.focus(); },
    "copy.done": onCopyDone,
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
  }

  // ---------------------------------------------------------------- global keys
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "/") { e.preventDefault(); openModelMenu(modelBtn); return; }
    if (e.key !== "Escape" || e.defaultPrevented) return;
    if (S.menu) { e.preventDefault(); closeMenu(!S.menu.combo); return; }
    if (S.historyOpen) { e.preventDefault(); toggleHistory(false); return; }
    if (S.editing) { e.preventDefault(); endEdit(); return; }
    if (S.perms.length) { e.preventDefault(); const r = dock.querySelector(".perm .btn:not(.primary)"); if (r) r.click(); return; }
    if (S.st.running) { e.preventDefault(); post("stop"); say("Arrêt demandé."); return; }
    post("close");
  });
  window.addEventListener("resize", () => closeMenu());
  reduceMQ.addEventListener && reduceMQ.addEventListener("change", () => {});

  // ---------------------------------------------------------------- boot
  renderHeader(); renderComposerBar(); renderDock(true); updateView();
  if (wv) wv.addEventListener("message", (e) => receive(e.data));
  window.addEventListener("error", (e) => post("panel.log", { level: "error", message: String(e.message).slice(0, 2000) }));
  post("ready", { v: V, build: BUILD });
})();
