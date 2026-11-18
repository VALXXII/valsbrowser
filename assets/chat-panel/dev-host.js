// Fake host for QA only (never shipped: the build copies index.html, panel.css, panel.js,
// markdown.js and icons.js, not this file nor dev.html). It stands in for chrome.webview and
// replays one scenario per Paper board: dev.html?scenario=6.1-autorisation, etc.
"use strict";
(function () {
  const listeners = [];
  const posted = (window.__posted = []);
  window.__scenarioDone = false;
  window.__scenario = {};
  function send(m) {
    const msg = JSON.parse(JSON.stringify(Object.assign({ v: 1 }, m)));
    for (const f of listeners) f({ data: msg });
  }
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage(m) { posted.push(m); setTimeout(() => onPanel(m), 0); },
    addEventListener(t, f) { if (t === "message") listeners.push(f); },
    removeEventListener() {},
  };
  window.devHost = { send };

  // ------------------------------------------------------------------ fixtures
  const today = (hh, mm) => { const d = new Date(); d.setHours(hh, mm, 0, 0); return d.toISOString(); };
  const ago = (ms) => new Date(Date.now() - ms).toISOString();
  const EL = { id: "el-btn", label: "button.btn-primary", kind: "button", selector: "main form > button.btn-primary", source: "Checkout.tsx:42", hint: "Payer 48,00 €", text: "Payer 48,00 €" };
  const EL2 = { id: "el-badge", label: "span.badge", kind: "text", selector: "span.badge", hint: "Code promo" };
  const CLAUDE = { id: "claude-code", name: "Claude Code", icon: "claude", status: "ready", detail: "Installé",
    models: [{ id: "opus", label: "Opus 5.5" }, { id: "sonnet", label: "Sonnet 5.5", isDefault: true }, { id: "haiku", label: "Haiku 5.5" }] };
  const CODEX = { id: "codex", name: "Codex", icon: "codex", status: "ready", detail: "Installé",
    models: [{ id: "sol", label: "6.1 Sol" }, { id: "terra", label: "6.1 Terra" }, { id: "luna", label: "6.1 Luna" }] };
  const BASE = {
    site: { origin: "http://127.0.0.1:5173", label: "127.0.0.1:5173", local: true },
    folder: { path: "C:\\Users\\val\\projets\\boutique", display: "C:\\Users\\…\\projets\\boutique", name: "boutique" },
    folderSuggestion: null,
    agents: [CLAUDE, CODEX], agentId: "claude-code", model: "sonnet",
    efforts: [{ id: "low", label: "Faible" }, { id: "medium", label: "Moyen" }, { id: "high", label: "Élevé" }, { id: "xhigh", label: "X-Élevé" }, { id: "max", label: "Max" }],
    effort: "high",
    modes: [{ id: "default", label: "Demander", description: "Propose chaque modification.", kind: "ask" },
            { id: "acceptEdits", label: "Auto", description: "Applique les modifications directement.", kind: "auto" },
            { id: "plan", label: "Planifier", description: "Propose un plan, ne touche à aucun fichier.", kind: "plan" }],
    mode: "default", conversation: null, running: false, pendingAnnotations: 2,
    suggestions: [{ id: "mobile", label: "Vérifier cette page sur mobile" }, { id: "contrast", label: "Trouver les textes trop peu contrastés" }],
    capabilities: { image: true, steering: true, commands: true }, disabledReason: null,
  };
  const st = (over) => Object.assign({ type: "state" }, JSON.parse(JSON.stringify(BASE)), over || {});
  const ASK = "Aligne-le à droite, et en pleine largeur sur mobile comparé à button.btn-primary dans le canvas";
  const DIFF = { path: "checkout.css", added: 6, removed: 2, lines: [{ op: "-", text: "width: 240px;" }, { op: "+", text: "margin-left: auto;" }, { op: "+", text: "width: 100%;  /* sous 640 px */" }] };
  const CHANGES = [{ path: "src/styles/checkout.css", status: "modified", added: 6, removed: 2, diff: { path: "src/styles/checkout.css", added: 6, removed: 2, lines: [{ op: "@", text: "@@ .btn-primary @@" }, { op: "-", text: "  width: 240px;" }, { op: "+", text: "  margin-left: auto;" }, { op: " ", text: "}" }, { op: "+", text: "@media (max-width: 640px) {" }, { op: "+", text: "  .btn-primary { width: 100%; }" }, { op: "+", text: "}" }] } }];
  // Three files for the « Voir les changements » view: long unchanged runs fold, line numbers come from @@ headers.
  const CSS_CTX = Array.from({ length: 14 }, (_, i) => ({ op: " ", text: ["  font-size: 16px;", "  line-height: 24px;", "  color: var(--ink);", "  padding: 12px 16px;", "  border-radius: 8px;", "  gap: 8px;", "  display: flex;"][i % 7] }));
  const FULL_DIFFS = [
    { path: "src/styles/checkout.css", added: 6, removed: 2, lines: [{ op: "@", text: "@@ -18,24 +18,28 @@ .checkout" }, { op: " ", text: ".checkout {" }].concat(CSS_CTX.slice(0, 9), [
      { op: " ", text: "}" }, { op: " ", text: ".btn-primary {" }, { op: "-", text: "  width: 240px;" }, { op: "-", text: "  margin-left: 0;" }, { op: "+", text: "  margin-left: auto;" },
      { op: " ", text: "  height: 44px;" }, { op: " ", text: "}" }], CSS_CTX.slice(0, 8), [
      { op: "+", text: "@media (max-width: 640px) {" }, { op: "+", text: "  .btn-primary { width: 100%; }" }, { op: "+", text: "}" }, { op: "+", text: "" }, { op: "+", text: "/* bouton pleine largeur sous 640 px */" }]) },
    { path: "src/pages/Checkout.tsx", added: 3, removed: 1, lines: [{ op: "@", text: "@@ -40,7 +40,9 @@ export function Checkout()" }, { op: " ", text: "  return (" }, { op: " ", text: "    <form className=\"checkout\">" },
      { op: "-", text: "      <button className=\"btn-primary\">Payer</button>" }, { op: "+", text: "      <button className=\"btn-primary\" type=\"submit\">" }, { op: "+", text: "        Payer {total}" }, { op: "+", text: "      </button>" },
      { op: " ", text: "    </form>" }, { op: " ", text: "  );" }, { op: " ", text: "}" }] },
    { path: "src/styles/pay-button.css", added: 4, removed: 0, lines: [{ op: "@", text: "@@ -0,0 +1,4 @@" }, { op: "+", text: ".pay-button {" }, { op: "+", text: "  font-weight: 500;" }, { op: "+", text: "  letter-spacing: -0.01em;" }, { op: "+", text: "}" }] },
  ];
  const CHANGES3 = [
    { path: "src/styles/checkout.css", status: "modified", added: 6, removed: 2, diff: FULL_DIFFS[0] },
    { path: "src/pages/Checkout.tsx", status: "modified", added: 3, removed: 1 },
    { path: "src/styles/pay-button.css", status: "created", added: 4, removed: 0 },
  ];
  const conv = (title, extra) => ({ conversation: Object.assign({ id: "c1", title }, extra || {}) });

  function thumbPng() {
    const c = document.createElement("canvas"); c.width = 80; c.height = 52;
    const g = c.getContext("2d"); g.fillStyle = "#F4F4F3"; g.fillRect(0, 0, 80, 52); g.fillStyle = "#1C1C1C"; g.fillRect(16, 20, 48, 12);
    return c.toDataURL("image/png");
  }
  function userMsg(id, text, at, els) { return { type: "message.user", id, text, at: at || today(15, 34), elements: els || [EL] }; }
  function doneTurn(id, opts) {
    const o = Object.assign({ text: "Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px.", end: "C'est fait. La page s'est rechargée ; l'annotation est marquée « terminée ».", model: "Sonnet 5.5", ms: 18000 }, opts || {});
    const items = [{ type: "turn.start", turnId: id, model: o.model, effort: "Élevé", agentId: "claude-code", at: today(15, 34) }];
    if (o.thought) items.push({ type: "thought", turnId: id, id: id + "-th", text: "Je regarde la règle CSS du bouton et le point de rupture mobile.", done: true, durationMs: o.thought });
    items.push({ type: "message.agentChunk", turnId: id, messageId: id + "-m1", text: o.text });
    items.push({ type: "toolCall", turnId: id, id: id + "-r1", title: "A lu Checkout.tsx", kind: "read", status: "completed" });
    items.push({ type: "toolCall", turnId: id, id: id + "-r2", title: "A lu checkout.css", kind: "read", status: "completed" });
    if (!o.noEdit) items.push({ type: "toolCall", turnId: id, id: id + "-e1", title: "A modifié checkout.css", kind: "edit", status: "completed" });
    if (o.end) items.push({ type: "message.agentChunk", turnId: id, messageId: id + "-m2", text: o.end });
    items.push({ type: "turn.end", turnId: id, stopReason: "end_turn", durationMs: o.ms, changes: o.noEdit ? [] : o.changes || CHANGES, checkpointId: "cp-" + id, undoable: true });
    return items;
  }
  const typeIn = (text) => () => { const c = document.querySelector(".cin"); c.focus(); document.execCommand("insertText", false, text); };
  const click = (sel) => () => { const n = document.querySelector(sel); if (n) n.click(); };
  const wait = (ms) => () => new Promise((r) => setTimeout(r, ms));

  // ------------------------------------------------------------------ scenarios
  const SC = {
    "6.1-autorisation": [st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", at: ago(12000) },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px." },
        { type: "toolCall", turnId: "t1", id: "r1", title: "A lu Checkout.tsx", kind: "read", status: "completed" },
        { type: "toolCall", turnId: "t1", id: "r2", title: "A lu checkout.css", kind: "read", status: "completed" }] },
      { type: "permission.request", requestId: "p1", turnId: "t1", title: "Claude veut modifier checkout.css", kind: "edit", diff: DIFF,
        options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "always", name: "Toujours autoriser", kind: "allow_always" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] },
      wait(50), click(".always")],
    "6.1b-commande": [st(Object.assign({ running: true }, conv("Lancer les tests", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Vérifie que les tests passent encore.", null, []),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" }] },
      { type: "permission.request", requestId: "p2", turnId: "t1", title: "Claude veut lancer une commande", kind: "execute", command: "npm test -- --run checkout",
        options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] }],
    "6.2-premiere-fois": [st({ folder: null, folderSuggestion: { path: "C:\\Users\\val\\projets\\boutique", display: "C:\\Users\\…\\projets\\boutique", name: "boutique", reason: "auto" } }),
      { type: "thread.reset", conversationId: "new", items: [] }],
    "6.3-termine": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { noEditLine: true })) }],
    "6.4-modele": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m")],
    "6.4-effort": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m"), wait(30), click(".menu .mi[data-key='effort']")],
    "6.4-mode": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }, click(".tb.mode")],
    "6.4-mode-auto": [st(Object.assign({ mode: "acceptEdits" }, conv("Bouton de paiement"))), { type: "thread.reset", conversationId: "c1", items: [] }],
    "6.4-autres-modeles": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m"), wait(30), click(".menu .mi[data-key='more']")],
    "6.4-autres-modeles-long": [st(Object.assign({ agents: [Object.assign({}, CLAUDE, { models: CLAUDE.models.concat([{ id: "sonnet-4", label: "Sonnet 4.5" }, { id: "opus-4", label: "Opus 4.1" }]) }), CODEX] }, conv("Bouton de paiement"))),
      { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m"), wait(30), click(".menu .mi[data-key='more']")],
    "6.4-agent-pas-pret": [st(Object.assign({ agents: [CLAUDE, Object.assign({}, CODEX, { status: "notConnected", detail: "Pas connecté", authMethods: [{ id: "chatgpt", label: "ChatGPT" }], models: [] })] }, conv("Bouton de paiement"))),
      { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m"), wait(30), click(".menu .mi[data-key='more']")],
    "6.4-preparation": [st({ agents: [Object.assign({}, CLAUDE, { status: "preparing", detail: "Préparation de Claude Code… 42 %", progress: 0.42 }), CODEX] }), { type: "thread.reset", conversationId: "new", items: [] }],
    "6.4-pas-installe": [st({ agents: [Object.assign({}, CLAUDE, { status: "notInstalled", detail: "Pas installé" }), CODEX] }), { type: "thread.reset", conversationId: "new", items: [] }],
    "6.5-1-vide": [st(), { type: "thread.reset", conversationId: "new", items: [] }, () => document.querySelector(".cin").setAttribute("data-placeholder", "Que faut-il changer sur la page ?")],
    "6.5-2-element-image": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      () => send({ type: "attachment.added", attachment: { id: "img1", kind: "image", name: "bouton.png", thumb: thumbPng() } }),
      typeIn("Aligne-le à droite, et en pleine largeur sur mobile comparé à "), () => send({ type: "element.attached", element: EL }), typeIn("dans le canvas")],
    "6.5-3-lignes": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      typeIn("Sur mobile, le bouton doit prendre toute la largeur.\nSur tablette, garde-le à droite mais avec 16 px de marge.\nLe texte doit rester sur une seule ligne.\nVérifie aussi l'état désactivé : il est trop pâle.\nEt aligne l'icône du cadenas avec le texte.")],
    "6.5-4-arobase": [st(), { type: "thread.reset", conversationId: "new", items: [] }, typeIn("Rapproche le prix de @butt"), wait(200)],
    "6.5-5-slash": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      { type: "commands", items: [{ name: "expliquer", description: "Explique comment cet élément est construit" }, { name: "responsive", description: "Vérifie l'élément sur mobile, tablette et bureau" }, { name: "annotations", description: "Traite les annotations en attente sur ce site" }] },
      typeIn("/")],
    "6.5-6-file": [st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", at: ago(12000) }] },
      { type: "queue", items: [{ id: "q1", text: "Et fais pareil pour le titre" }] }],
    "6.5-7-deconnecte": [st({ agents: [Object.assign({}, CLAUDE, { status: "notConnected", detail: "Pas connecté", authMethods: [{ id: "claude-ai-login", label: "Claude" }] }), CODEX] }), { type: "thread.reset", conversationId: "new", items: [] }],
    "6.5-8-limite": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      { type: "strip", id: "limit", tone: "neutral", text: "Limite Claude atteinte - Retour à 15:40", action: { id: "switch-codex", label: "Passer à Codex" } }],
    "6.5-9-site-non-local": [st({ site: { origin: "https://exemple.fr", label: "exemple.fr", local: false }, disabledReason: "Le chat ne marche que sur les sites locaux (localhost, 127.0.0.1, fichiers). Ouvrez la version locale de ce site." }),
      { type: "thread.reset", conversationId: "new", items: [] }],
    "6.6-fil": [st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { thought: 6000, end: "", text: "Le bouton avait une largeur fixe. Il est maintenant à droite sur grand écran et en pleine largeur sous 640 px.\n\n```css checkout.css\n.btn-primary {\n  margin-left: auto;\n}\n@media (max-width: 640px) {\n  .btn-primary { width: 100%; }\n}\n```" })).concat([
        userMsg("u2", "Fais pareil pour le titre de la page.", today(15, 36), []),
        { type: "turn.start", turnId: "t2", model: "Opus 5.5", effort: "Élevé", at: ago(8000) },
        { type: "toolCall", turnId: "t2", id: "t2-e", title: "Modifie Header.tsx…", kind: "edit", status: "in_progress" }]) }],
    "6.6-nouvelles-etapes": [st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u0", "Le titre est trop gros sur mobile.", today(15, 20), [])].concat(doneTurn("t0", { thought: 3000 }), [userMsg("u1", ASK)], doneTurn("t1", { thought: 6000 }), [userMsg("u2", "Fais pareil pour le titre de la page.", today(15, 36), []),
        { type: "turn.start", turnId: "t2", model: "Opus 5.5", effort: "Élevé" }]) },
      () => { const s = document.querySelector(".scroller"); s.scrollTop = 0; s.dispatchEvent(new Event("scroll")); }, wait(50),
      { type: "toolCall", turnId: "t2", id: "t2-r", title: "A lu Header.tsx", kind: "read", status: "completed" },
      { type: "toolCall", turnId: "t2", id: "t2-e", title: "Modifie Header.tsx…", kind: "edit", status: "in_progress" }, wait(50)],
    "6.6-survol": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { thought: 6000 })) }],
    "6.6-changements": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { thought: 6000 })) },
      click(".card4 .toggle-chev"), click(".fold")],
    "6.6-voir-changements": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { changes: CHANGES3 })) },
      click(".card4 .toggle-chev"), click(".card4 .more"), wait(80)],
    "6.6-voir-changements-deplie": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { changes: CHANGES3 })) },
      click(".card4 .toggle-chev"), click(".card4 .more"), wait(80), click(".changes .unfold"), wait(30)],
    "6.6-voir-changements-fichier": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1", { changes: CHANGES3 })) },
      click(".card4 .toggle-chev"), () => { const b = Array.from(document.querySelectorAll(".card4 .file")).find((x) => x.textContent.includes("pay-button")); b && b.click(); }, wait(120)],
    "6.7a-vide": [st(), { type: "thread.reset", conversationId: "new", items: [] }],
    "6.7b-historique": [st(conv("Titre de la page", { running: true })), { type: "thread.reset", conversationId: "c1", items: [] },
      { type: "history", items: [
        { id: "c1", title: "Titre de la page", subtitle: "Modifie Header.tsx…", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "claude", updatedAt: ago(10000), running: true, current: true },
        { id: "c2", title: "Bouton de paiement", subtitle: "1 fichier modifié", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "claude", updatedAt: ago(2 * 60000) },
        { id: "c3", title: "Badge code promo", subtitle: "Questions seulement", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "codex", updatedAt: ago(26 * 3600000) },
        { id: "c4", title: "Menu de navigation", subtitle: "3 fichiers modifiés", siteLabel: "localhost:4173", thisSite: false, agentIcon: "claude", updatedAt: ago(4 * 24 * 3600000) }] },
      click(".hd-title"), () => document.activeElement && document.activeElement.blur()],
    "6.7c-modifier": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Le bouton devrait être aligné à droite, et passer en pleine largeur sous 768 px.", null, [])].concat(doneTurn("t1")) },
      click(".um .um-tools .icon-btn")],
    "6.7c-modifier-confirmer": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Le bouton devrait être aligné à droite, et passer en pleine largeur sous 768 px.", null, [])].concat(doneTurn("t1")) },
      click(".um .um-tools .icon-btn"), wait(30), click(".editor .send"), wait(80)],
    "6.7d-erreur": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Fais pareil pour le titre de la page.", null, []),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" },
        { type: "toolCall", turnId: "t1", id: "r1", title: "A lu Header.tsx", kind: "read", status: "completed" },
        { type: "turn.end", turnId: "t1", stopReason: "error", durationMs: 4000 },
        { type: "error", id: "e1", scope: "thread", turnId: "t1", title: "Claude s'est arrêté en route", message: "La connexion avec Claude Code sur cet ordinateur a été perdue. Rien n'a été modifié.",
          detail: "claude-agent-acp exited with code 1\nECONNRESET while reading stdout", actions: [{ id: "retry", label: "Réessayer" }] }] }],
    "6.8-viser": [st(conv("Nouveau chat", { source: "Checkout.tsx:42" })), { type: "thread.reset", conversationId: "new", items: [] }, { type: "pick.state", active: true }],
    "6.9-ecrire": [st(conv("Nouveau chat", { source: "Checkout.tsx:42" })), { type: "thread.reset", conversationId: "new", items: [] },
      typeIn("Aligne-le à droite, et en pleine largeur sur mobile comparé à "), () => send({ type: "element.attached", element: EL }), typeIn("dans le canvas"), () => document.activeElement.blur()],
    "6.10-travaille": [st(Object.assign({ running: true }, conv("Bouton de paiement", { source: "Checkout.tsx:42", running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", at: ago(8000) },
        { type: "thought", turnId: "t1", id: "th", text: "Le bouton est dans Checkout.tsx, la largeur vient de checkout.css.", done: true, durationMs: 4000 },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "Le bouton a une largeur fixe de 240 px dans checkout.css. Je le passe à droite et en pleine largeur sous" },
        { type: "toolCall", turnId: "t1", id: "r1", title: "A lu Checkout.tsx", kind: "read", status: "completed" },
        { type: "toolCall", turnId: "t1", id: "r2", title: "Lit checkout.css…", kind: "read", status: "in_progress" }] }],
    "6.11-fait": [st(conv("Bouton de paiement", { source: "Checkout.tsx:42" })), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) },
      { type: "usage", used: 41000, size: 200000 }],
    "en-tete-jauge": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) },
      { type: "usage", used: 124000, size: 200000 }, () => document.querySelector(".gauge").focus(), wait(160)],
    "en-tete-jauge-pleine": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) },
      { type: "usage", used: 172000, size: 200000 }, () => document.querySelector(".gauge").focus(), wait(160)],
    "6.1c-outil-mcp": [st(Object.assign({ running: true }, conv("Annotations en attente", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Traite les annotations en attente sur ce site.", null, []),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", at: ago(6000) },
        { type: "toolCall", turnId: "t1", id: "s1", title: "mcp__browser-annotations-preview__browser_annotations_status", kind: "other", status: "completed" }] },
      { type: "permission.request", requestId: "p3", turnId: "t1", title: "mcp__browser-annotations-preview__browser_annotations_list", kind: "other",
        tool: { name: "mcp__browser-annotations-preview__browser_annotations_list" }, input: "{\"site\":\"http://127.0.0.1:5173\",\"status\":\"pending\"}",
        options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "always", name: "Toujours autoriser", kind: "allow_always" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] },
      wait(30), click(".perm .perm-more")],
    "6.1d-outil-hote": [st(Object.assign({ running: true }, conv("Annotations en attente", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Traite les annotations en attente sur ce site.", null, []), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", at: ago(6000) }] },
      { type: "permission.request", requestId: "p4", turnId: "t1", title: "Claude veut lire les 3 annotations en attente sur 127.0.0.1:5173", kind: "other",
        tool: { name: "mcp__browser-annotations__browser_annotations_list", label: "Lister les annotations", serverLabel: "Annotations du navigateur" }, input: "{\"status\":\"pending\"}",
        options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] }],
    "6.12-plan": [st(Object.assign({ mode: "plan" }, conv("Paiement sur mobile", { source: "Checkout.tsx:42" }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Comment rendre cette page plus claire sur mobile ? Propose un plan, ne touche à rien pour l'instant.", null, []),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" },
        { type: "toolCall", turnId: "t1", id: "r1", title: "A lu Checkout.tsx", kind: "read", status: "completed" },
        { type: "toolCall", turnId: "t1", id: "r2", title: "A lu checkout.css", kind: "read", status: "completed" },
        { type: "toolCall", turnId: "t1", id: "r3", title: "A lu Summary.tsx", kind: "read", status: "completed" },
        { type: "plan", turnId: "t1", id: "plan1", proposed: true, entries: [{ content: "Empiler le formulaire et le récapitulatif sous 768 px", status: "pending" }, { content: "Bouton Payer en pleine largeur, collé en bas de l'écran", status: "pending" }, { content: "Champs de 48 px de haut pour le toucher", status: "pending" }, { content: "Vérifier le rendu à 375, 768 et 1280 px", status: "pending" }] },
        { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 22000 }] }],
    "plan-en-cours": [st(Object.assign({ running: true, mode: "acceptEdits" }, conv("Paiement sur mobile", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Lance le plan.", null, []), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" },
        { type: "plan", turnId: "t1", id: "plan1", entries: [{ content: "Empiler le formulaire et le récapitulatif sous 768 px", status: "completed" }, { content: "Bouton Payer en pleine largeur", status: "in_progress" }, { content: "Champs de 48 px de haut", status: "pending" }] }] }],
    "markdown": [st(conv("Explication")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "/expliquer", null, []), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Moyen" },
      { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "## Comment le bouton est construit\n\nLe composant `PayButton` vit dans **Checkout.tsx** et reçoit son style de *checkout.css* :\n\n1. la classe `.btn-primary` fixe la largeur ;\n2. le parent est un `flex` en colonne ;\n3. rien ne change sous 640 px.\n\n- Voir la [doc de flexbox](https://developer.mozilla.org/fr/docs/Web/CSS/flex)\n- Rien d'autre à signaler\n\n> Le texte venu de la page est traité comme non fiable.\n\n| Écran | Largeur |\n|---|---|\n| Mobile | 100 % |\n| Bureau | 240 px |\n\n<script>alert(1)</script> reste du texte, sans danger.\n\n```tsx Checkout.tsx\n" + Array.from({ length: 20 }, (_, i) => "  const line" + (i + 1) + " = " + i + ";").join("\n") + "\n```" },
      { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 9000 }] }],
    "refus-limite": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" },
      { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "Je commence par lire le fichier." },
      { type: "toolCall", turnId: "t1", id: "f1", title: "Lit theme.css", kind: "read", status: "failed" },
      { type: "toolCall", turnId: "t1", id: "f2", title: "A lu checkout.css", kind: "read", status: "completed" },
      { type: "turn.end", turnId: "t1", stopReason: "cancelled", durationMs: 3000 }] }],
    "annule-conflit": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) },
      { type: "turn.undone", turnId: "t1", ok: false, conflicts: ["src/styles/checkout.css"] }],
    "perf-500": [st(conv("Fil de 500 messages")), () => send({ type: "thread.reset", conversationId: "big", items: bigThread(250) })],
    "stream": [st(Object.assign({ running: true }, conv("Texte en flux", { running: true }))), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Explique tout en détail.", null, []), { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" }] }],
  };
  function bigThread(n) {
    const out = [];
    for (let i = 0; i < n; i++) {
      out.push({ type: "message.user", id: "u" + i, text: "Demande numéro " + i + " : ajuste la marge de la carte " + i + ".", at: new Date(Date.now() - (n - i) * 60000).toISOString(), elements: [] });
      out.push({ type: "turn.start", turnId: "t" + i, model: "Sonnet 5.5", effort: "Élevé" });
      out.push({ type: "message.agentChunk", turnId: "t" + i, messageId: "m" + i, text: "J'ai changé la marge de **16 px** à `24 px` dans card.css. Voici le détail :\n\n- carte " + i + "\n- rien d'autre" });
      out.push({ type: "toolCall", turnId: "t" + i, id: "r" + i, title: "A lu card.css", kind: "read", status: "completed" });
      out.push({ type: "turn.end", turnId: "t" + i, stopReason: "end_turn", durationMs: 5000, changes: [{ path: "card.css", status: "modified", added: 1, removed: 1 }], checkpointId: "cp" + i, undoable: true });
    }
    return out;
  }
  // streaming helper for the perf test: words at ~60 Hz for `ms`
  window.devHost.stream = (ms) => new Promise((done) => {
    const words = ("Le texte arrive au fil de l'eau sans animation par mot. Chaque phrase terminée est annoncée aux lecteurs d'écran. " +
                   "Le fil suit tant qu'on est en bas, et `le code` reste lisible. ").split(" ");
    let k = 0; const t0 = performance.now();
    const step = () => {
      if (performance.now() - t0 > ms) { send({ type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: ms }); done(k); return; }
      send({ type: "message.agentChunk", turnId: "t1", messageId: "m1", text: words[k % words.length] + " " + (k % 40 === 39 ? "\n\n" : "") }); k++;
      setTimeout(step, 16);
    };
    step();
  });

  // ------------------------------------------------------------------ host side of the protocol
  let turnSeq = 100;
  function onPanel(m) {
    switch (m.type) {
      case "ready": run(); break;
      case "mention.query": send({ type: "mentions", query: m.query, elements: [EL, EL2], files: [{ path: "Checkout.tsx", dir: "src/pages" }] }); break;
      case "history.open": if (!window.__historyLoaded) send({ type: "history", items: [] }); break;
      case "message.edit": if (m.restore == null) send({ type: "edit.confirm", messageId: m.messageId, files: ["src/styles/checkout.css"] }); break;
      case "setModel": send({ type: "state", model: m.id }); break;
      case "setEffort": send({ type: "state", effort: m.id }); break;
      case "setMode": send({ type: "state", mode: m.id }); break;
      case "copy": send({ type: "copy.done", ok: true }); break;
      case "setAgent": send({ type: "state", agentId: m.id, model: m.id === "codex" ? "terra" : "sonnet", conversation: null }); send({ type: "thread.reset", conversationId: "new", items: [] }); break;
      case "checkpoint.viewChanges": send({ type: "changes.diff", turnId: m.turnId, diffs: FULL_DIFFS }); break;
      case "pickElement": send({ type: "pick.state", active: m.active }); if (m.active) setTimeout(() => send({ type: "element.attached", element: EL }), 600); break;
      case "attachImage": send({ type: "attachment.added", attachment: { id: "img" + Date.now(), kind: "image", name: m.name || "capture.png", thumb: thumbPng() } }); break;
      case "send": {
        if (window.__scenarioName === "interactive" || !window.__scenarioName) fakeTurn(m.text);
        break;
      }
      case "stop": send({ type: "turn.end", turnId: "t" + turnSeq, stopReason: "cancelled", durationMs: 1000 }); send({ type: "state", running: false }); break;
      case "permission.respond": send({ type: "state", running: true }); break;
      case "newChat": send({ type: "state", conversation: null }); send({ type: "thread.reset", conversationId: "new", items: [] }); break;
      default: break;
    }
  }
  function fakeTurn(text) {
    const id = "t" + (++turnSeq);
    send({ type: "message.user", id: "u" + turnSeq, text, at: new Date().toISOString(), elements: [] });
    send({ type: "state", running: true, conversation: { id: "c1", title: text.slice(0, 40), running: true } });
    send({ type: "turn.start", turnId: id, model: "Sonnet 5.5", effort: "Élevé", at: new Date().toISOString() });
    send({ type: "toolCall", turnId: id, id: id + "r", title: "Lit checkout.css…", kind: "read", status: "in_progress" });
    setTimeout(() => send({ type: "toolCallUpdate", id: id + "r", status: "completed", title: "A lu checkout.css" }), 600);
    const answer = "D'accord. Je regarde le fichier puis je propose un changement. ";
    answer.split(" ").forEach((w, i) => setTimeout(() => send({ type: "message.agentChunk", turnId: id, messageId: id + "m", text: w + " " }), 800 + i * 60));
    setTimeout(() => { send({ type: "turn.end", turnId: id, stopReason: "end_turn", durationMs: 2000, changes: CHANGES, checkpointId: "cp" + id, undoable: true }); send({ type: "state", running: false, conversation: { id: "c1", title: text.slice(0, 40) } }); }, 1600);
  }

  async function run() {
    const name = new URLSearchParams(location.search).get("scenario") || "interactive";
    window.__scenarioName = name;
    send({ type: "hello", locale: "fr", platform: "windows" });
    const steps = SC[name] || [st(), { type: "thread.reset", conversationId: "new", items: [] }];
    if (name === "6.7b-historique") window.__historyLoaded = true;
    for (const s of steps) {
      if (typeof s === "function") await s();
      else send(s);
      await new Promise((r) => setTimeout(r, 0));
    }
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
    window.__scenarioDone = true;
  }
  window.__scenarios = Object.keys(SC);

})();
