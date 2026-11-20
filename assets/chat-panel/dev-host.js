// Fake host for QA only (never shipped: the build copies index.html, panel.css, panel.js,
// markdown.js and icons.js, not this file nor dev.html). It stands in for chrome.webview and
// replays one scenario per Paper board: dev.html?scenario=6.1-autorisation, etc.
"use strict";
(function () {
  const listeners = [];
  const posted = (window.__posted = []);
  window.__scenarioDone = false;
  window.__scenario = {};
  let bubbleOn = false, curTurn = null, lastLay = {};
  function send(m) {
    const msg = JSON.parse(JSON.stringify(Object.assign({ v: 1 }, m)));
    if (msg.type === "layout") {   // the fake host's « window »: sized from bubble.size while in bubble layout
      bubbleOn = msg.mode === "bubble";
      lastLay = Object.assign({}, lastLay, msg);
      if (!bubbleOn) { document.documentElement.style.width = ""; document.documentElement.style.height = ""; }
    }
    if (msg.type === "turn.start") curTurn = msg.turnId;
    if (msg.type === "thread.reset") { curTurn = null; for (const it of msg.items || []) if (it.type === "turn.start") curTurn = it.turnId; }
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
    // B 1.9: as the host sends them (Paper 14RK-0 order); the adapter's « bypass » mode never reaches the panel
    modes: [{ id: "acceptEdits", label: "Accept edits", description: "Automatically accept all file edits", kind: "edits" },
            { id: "auto", label: "Auto", description: "Claude handles permission decisions", kind: "auto" },
            { id: "default", label: "Demander", description: "Propose chaque modification.", kind: "ask" },
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
  // « Traiter les annotations » fixtures (Paper 186K-0): two landing-page notes with drawn thumbnails.
  function annThumb(kind) {
    const c = document.createElement("canvas"); c.width = 88; c.height = 64;
    const g = c.getContext("2d");
    if (kind === "text") { g.fillStyle = "#F7F3EC"; g.fillRect(0, 0, 88, 64); g.fillStyle = "#1F1D1A"; g.fillRect(12, 22, 56, 8); g.fillStyle = "#C9C3B9"; g.fillRect(12, 36, 48, 6); }
    else { g.fillStyle = "#FFFFFF"; g.fillRect(0, 0, 88, 64); g.fillStyle = "#B5532A"; g.beginPath(); g.roundRect(34, 8, 44, 14, 7); g.fill(); }
    return c.toDataURL("image/png");
  }
  const ANN = () => [
    { id: "a-hero", note: "Le texte sous le titre est trop pâle, on le lit mal.", element: "p.hero-text", source: "landing.html:23", thumb: annThumb("text") },
    { id: "a-badge", note: "Le badge « Le plus choisi » dépasse de la carte.", element: "span.badge", source: "landing.html:64", thumb: annThumb("badge") },
  ];
  const ANN_MORE = () => ANN().concat([
    { id: "a-cta", note: "Le bouton « Commencer » manque de contraste au survol.", element: "a.cta", source: "landing.html:31" },
    { id: "a-grid", note: "Les cartes ne sont pas alignées sur tablette.", element: "div.plans", source: "landing.html:58" },
    { id: "a-foot", note: "Le pied de page est collé au contenu.", element: "footer", source: "landing.html:112" },
    { id: "a-logo", note: "Le logo est flou sur écran Retina.", element: "img.logo", source: "landing.html:9" },
    { id: "a-faq", note: "La FAQ s'ouvre sans animation.", element: "details.faq", source: "landing.html:88" }]);
  const INSTRUCTION = "Traite ces 2 annotations du site 127.0.0.1:5183, une par une, dans le dossier du projet.\n\n1. p.hero-text (landing.html:23) : « Le texte sous le titre est trop pâle, on le lit mal. »\n2. span.badge (landing.html:64) : « Le badge « Le plus choisi » dépasse de la carte. »\n\nPour chacune : fais la plus petite modification, puis marque-la terminée avec un résumé d'une phrase.";
  const annMsg = (items) => ({ type: "message.user", id: "u-ann", text: "", at: today(10, 51), editable: false, annotations: { batchId: "b1", items, instruction: INSTRUCTION } });
  function userMsg(id, text, at, els) { return { type: "message.user", id, text, at: at || today(15, 34), elements: els || [EL] }; }
  function doneTurn(id, opts) {
    const o = Object.assign({ text: "Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px.", end: "C'est fait. La page s'est rechargée ; l'annotation est marquée « terminée ».", model: "Sonnet 5.5", ms: 18000 }, opts || {});
    const items = [{ type: "turn.start", turnId: id, model: o.model, effort: "Élevé", agentId: "claude-code", at: today(15, 34) }];
    if (o.thought) items.push({ type: "thought", turnId: id, id: id + "-th", text: "Je regarde la règle CSS du bouton et le point de rupture mobile.", done: true, durationMs: o.thought });
    items.push({ type: "message.agentChunk", turnId: id, messageId: id + "-m1", text: o.text });
    items.push({ type: "toolCall", turnId: id, id: id + "-r1", title: "A lu Checkout.tsx", kind: "read", status: "completed", locations: [{ path: "src/pages/Checkout.tsx" }] });
    items.push({ type: "toolCall", turnId: id, id: id + "-r2", title: "A lu checkout.css", kind: "read", status: "completed", locations: [{ path: "src/styles/checkout.css" }] });
    if (!o.noEdit) items.push({ type: "toolCall", turnId: id, id: id + "-e1", title: "A modifié checkout.css", kind: "edit", status: "completed", locations: [{ path: "src/styles/checkout.css" }] });
    if (o.end) items.push({ type: "message.agentChunk", turnId: id, messageId: id + "-m2", text: o.end });
    items.push({ type: "turn.end", turnId: id, stopReason: "end_turn", durationMs: o.ms, changes: o.noEdit ? [] : o.changes || CHANGES, checkpointId: "cp-" + id, undoable: true });
    return items;
  }
  const typeIn = (text) => () => { const c = document.querySelector(".cin"); c.focus(); document.execCommand("insertText", false, text); };
  const click = (sel) => () => { const n = document.querySelector(sel); if (n) n.click(); };
  const wait = (ms) => () => new Promise((r) => setTimeout(r, ms));
  const blur = () => document.activeElement && document.activeElement.blur();

  // Comment-mode bubble (B 1.8, Paper row « Mode Commentaire »): the same thread drawn in a 305 px bubble.
  const LAY = (over) => Object.assign({ type: "layout", mode: "bubble", maxWidth: 305, maxHeight: 520, side: "right", element: "button.btn-primary" }, over || {});
  const CASK = "Fais en sorte que le bouton prenne toute la largeur du form";
  const CTEXT = "Le bouton a une largeur fixe de 240 px. Je le passe à droite sur grand écran et en pleine largeur sous 640 px.";
  const CEND = "C'est fait. La page s'est rechargée ; l'annotation est marquée « terminée ».";
  const C_DIFF = Object.assign({}, DIFF, { path: "src/styles/checkout.css" });
  const cUser = () => ({ type: "message.user", id: "u1", text: CASK, at: today(15, 34), elements: [] });
  const cStart = () => ({ type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé", agentId: "claude-code", at: ago(12000) });
  const cSteps = (running) => [
    { type: "toolCall", turnId: "t1", id: "r1", title: "A lu index.html", kind: "read", status: "completed", locations: [{ path: "index.html" }] },
    { type: "toolCall", turnId: "t1", id: "r2", title: "A lu Checkout.tsx", kind: "read", status: "completed", locations: [{ path: "src/pages/Checkout.tsx" }] },
    { type: "toolCall", turnId: "t1", id: "r3", title: "mcp__browser-annotations__browser_annotations_claim", kind: "other", status: "completed" },
    { type: "toolCall", turnId: "t1", id: "r4", title: running ? "Lit checkout.css…" : "A lu checkout.css", kind: "read", status: running ? "in_progress" : "completed", locations: [{ path: "src/styles/checkout.css" }] }];
  const cRunning = () => [cUser(), cStart(), { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: CTEXT }].concat(cSteps(true));
  const cRun = (lay) => [LAY(lay), st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))), { type: "thread.reset", conversationId: "c1", items: cRunning() }];
  const cDoneItems = (changes) => [cUser(), cStart()].concat(cSteps(false), [
    { type: "toolCall", turnId: "t1", id: "e1", title: "A modifié checkout.css", kind: "edit", status: "completed", locations: [{ path: "src/styles/checkout.css" }] },
    { type: "toolCall", turnId: "t1", id: "x1", title: "A lancé npm run build", kind: "execute", status: "completed" },
    { type: "message.agentChunk", turnId: "t1", messageId: "m2", text: CEND },
    { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 18000, changes: changes || [{ path: "src/styles/checkout.css", status: "modified", added: 6, removed: 2, diff: C_DIFF }], checkpointId: "cp-t1", undoable: true }]);
  const cDone = (lay, changes) => [LAY(lay), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: cDoneItems(changes) }];
  const C_PERM = { type: "permission.request", requestId: "p1", turnId: "t1", title: "Claude veut modifier checkout.css", kind: "edit", diff: DIFF,
    options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "always", name: "Toujours autoriser", kind: "allow_always" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] };
  const cError = (lay) => [LAY(lay), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart(),
    { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: CTEXT }, { type: "turn.end", turnId: "t1", stopReason: "error", durationMs: 18000 },
    { type: "error", id: "e1", scope: "thread", turnId: "t1", title: "Les serveurs de Claude étaient surchargés", detail: "Erreur 529 · overloaded_error",
      message: "La réponse s'est arrêtée au bout de 18 s, avant toute modification : aucun fichier n'a été changé. « Réessayer » relance la même demande.", actions: [{ id: "retry", label: "Réessayer" }] }] }];
  const RAW_STEPS = () => [
    { type: "toolCall", turnId: "t1", id: "b1", title: "Read index.html", kind: "read", status: "completed", locations: [{ path: "C:\\site\\index.html" }] },
    { type: "toolCall", turnId: "t1", id: "b2", title: "mcp__browser-annotations__browser_annotations_claim", kind: "other", status: "completed" },
    { type: "toolCall", turnId: "t1", id: "b3", title: "`npm run build`", kind: "execute", status: "completed" },
    { type: "toolCall", turnId: "t1", id: "b4", title: "Edit styles\\landing.css", kind: "edit", status: "in_progress" }];
  const C_SCENARIOS = {
    "c-1-saisie": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, typeIn(CASK), blur],
    "c-1-vide": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }],
    "c-2-envoye": cRun(),
    "c-2.1-survol": cRun(),
    "c-2.1-arrete": [LAY(), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: cRunning().concat([{ type: "turn.end", turnId: "t1", stopReason: "cancelled", durationMs: 9000 }]) }],
    "c-2-autorisation": cRun({ maxHeight: 303 }).concat([C_PERM, wait(60), click(".bperm .always")]),
    "c-2-autorisation-diff": cRun({ maxHeight: 303 }).concat([C_PERM, wait(60), click(".bperm .always"), click(".bperm-file"), wait(60)]),
    "c-2.2b-erreur": cError(),
    "c-2.2b-erreur-details": cError().concat([click(".bt-more"), wait(30)]),
    "c-3-termine": cDone(),
    "c-3-termine-diff": cDone({ maxHeight: 303 }).concat([click(".bt-bar-main"), wait(80)]),
    "c-3-historique": cDone(),
    // what the boards do not draw: taller room, several files, a command, the queue between elements, menus, no folder
    "c-autorisation-grande": cRun().concat([C_PERM, wait(60)]),
    "c-diff-plusieurs-fichiers": cDone({ maxHeight: 420 }, CHANGES3).concat([click(".bt-bar-main"), wait(80)]),
    "c-autorisation-commande": cRun().concat([{ type: "permission.request", requestId: "p2", turnId: "t1", title: "Claude veut lancer une commande", kind: "execute", command: "npm test -- --run checkout",
      options: [{ optionId: "allow", name: "Autoriser", kind: "allow_once" }, { optionId: "reject", name: "Refuser", kind: "reject_once" }] }, wait(60), click(".bperm-file"), wait(60)]),
    "c-file-attente": [LAY({ element: "h1.title" }), st(), { type: "thread.reset", conversationId: "new", items: [] },
      { type: "queue", items: [{ id: "q1", text: "Mets ce titre en gras", after: "button.btn-primary" }] }],
    "c-menu-modele": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".model .m"), wait(60)],
    "c-menu-mode": cDone().concat([wait(30), click(".tb.mode"), wait(60)]),
    "c-menu-au-dessus": [LAY({ side: "above" }), st(), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".tb.mode"), wait(60)],
    "c-fil-long": [LAY({ maxHeight: 360 }), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: cDoneItems().concat([
      { type: "message.user", id: "u2", text: "Et garde 16 px de marge sur tablette.", at: today(15, 40), elements: [] },
      { type: "turn.start", turnId: "t2", model: "Sonnet 5.5", effort: "Élevé", agentId: "claude-code", at: today(15, 40) },
      { type: "message.agentChunk", turnId: "t2", messageId: "m3", text: "C'est ajouté : entre 640 et 1024 px, le bouton garde **16 px** de marge de chaque côté.\n\n- `checkout.css` : une règle de plus\n- rien d'autre n'a changé" },
      { type: "turn.end", turnId: "t2", stopReason: "end_turn", durationMs: 7000 }]) }],
    "c-sans-dossier": [LAY(), st({ folder: null }), { type: "thread.reset", conversationId: "new", items: [] }],
    // B 1.9 — R8: before the agent's first words; R7: the user's message with its element; R3: menus outside the
    // bubble, with a host that cannot give the room above (screen edge); R5: « + » with a folder found for sure or not
    // R9: the bubble closed and opened again during a request: thread.reset, then the host sends the waiting request again
    "c-autorisation-reouverte": cRun({ maxHeight: 303 }).concat([C_PERM, wait(60), LAY({ maxHeight: 303 }), st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: cRunning() }, C_PERM, { type: "focus", target: "composer" }, wait(60), C_PERM, wait(30)]),
    // D4 (audit): the agent's raw titles (real Claude Code) — the page says them in French, file name alone
    "c-etapes-brutes": [LAY(), st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart(),
      { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: CTEXT }].concat(RAW_STEPS()) }],
    // cv3: R20 (320 px), R13 (no model chosen yet: the agent's default), R17 (several agent messages, with and without a bar)
    "c-largeur-320": [LAY({ maxWidth: 320 }), st(), { type: "thread.reset", conversationId: "new", items: [] }],
    "c-modele-inconnu": [LAY({ maxWidth: 320 }), st({ model: null, agents: [CLAUDE, Object.assign({}, CODEX, { models: [] })] }), { type: "thread.reset", conversationId: "new", items: [] }],
    "c-messages-multiples": [LAY({ maxWidth: 320 }), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart(),
      { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: CTEXT },
      { type: "toolCall", turnId: "t1", id: "e1", title: "A modifié checkout.css", kind: "edit", status: "completed", locations: [{ path: "src/styles/checkout.css" }] },
      { type: "message.agentChunk", turnId: "t1", messageId: "m2", text: CEND },
      { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 18000, changes: [{ path: "src/styles/checkout.css", status: "modified", added: 6, removed: 2, diff: C_DIFF }] }] }],
    "c-messages-deux": [LAY({ maxWidth: 320 }), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart(),
      { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: CTEXT }, { type: "message.agentChunk", turnId: "t1", messageId: "m2", text: "Rien d'autre n'a changé." },
      { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 9000 }] }],
    "c-2-sans-texte": [LAY(), st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart()].concat(cSteps(true)) }],
    "c-2-puce": [LAY(), st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [{ type: "message.user", id: "u1", text: CASK, at: today(15, 34), elements: [EL] }] }],
    "c-menu-refuse": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, () => { window.__grant = { top: 40 }; }, wait(30), click(".model .m"), wait(120)],
    // the menus next to their boards (17US-0 with « Autres modèles », 17VU-0)
    "c-menu-modele-plus": [LAY(), st({ agents: [Object.assign({}, CLAUDE, { models: CLAUDE.models.concat([{ id: "opus41", label: "Opus 4.1" }]) }), CODEX] }), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".model .m"), wait(80)],
    "c-menu-effort": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".model .m"), wait(40), click(".menu .mi[data-key=effort]"), wait(80)],
    "c-menu-autres": [LAY(), st({ agents: [Object.assign({}, CLAUDE, { models: CLAUDE.models.concat([{ id: "opus41", label: "Opus 4.1" }]) }), CODEX] }), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".model .m"), wait(40), click(".menu .mi[data-key=more]"), wait(80)],
    "c-menu-effort-gauche": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, () => { window.__grant = { right: 0 }; }, wait(30), click(".model .m"), wait(40), click(".menu .mi[data-key=effort]"), wait(120)],
    "c-menu-plus": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".tools .tb[aria-label=Joindre]"), wait(60)],
    "c-menu-plus-incertain": [LAY(), st({ askFolder: true }), { type: "thread.reset", conversationId: "new", items: [] }, wait(30), click(".tools .tb[aria-label=Joindre]"), wait(60)],
    "c-infobulle-envoyer": [LAY(), st(), { type: "thread.reset", conversationId: "new", items: [] }, typeIn(CASK), blur],
    "c-mode-auto": [LAY(), st({ mode: "acceptEdits" }), { type: "thread.reset", conversationId: "new", items: [] }],
    "c-plan": [LAY(), st(Object.assign({ mode: "plan" }, conv("Paiement sur mobile"))), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart(),
      { type: "plan", turnId: "t1", id: "plan1", proposed: true, entries: [{ content: "Empiler le formulaire et le récapitulatif sous 768 px", status: "pending" }, { content: "Bouton Payer en pleine largeur", status: "pending" }] },
      { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 22000 }] }],
    "c-bascule-panneau": cDone().concat([{ type: "layout", mode: "panel" }, st(conv("Bouton de paiement")),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) }]),
  };

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
    // V1.2 (Paper 17US-0 / 17VU-0): one group per connected provider; efforts as the adapters say them (English, « Default » first)
    "modeles-deux-fournisseurs": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m")],
    "modeles-un-fournisseur": [st(Object.assign({ agents: [CLAUDE, Object.assign({}, CODEX, { status: "notConnected", detail: "Pas connecté", authMethods: [{ id: "chatgpt", label: "ChatGPT" }] })] }, conv("Bouton de paiement"))),
      { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m")],
    "modeles-aucun-fournisseur": [st({ agents: [Object.assign({}, CLAUDE, { status: "notConnected", detail: "Pas connecté", authMethods: [{ id: "claude-ai-login", label: "Claude" }], models: [] }),
      Object.assign({}, CODEX, { status: "notInstalled", detail: "Pas installé", models: [] })] }), { type: "thread.reset", conversationId: "new", items: [] }, click(".model .m")],
    "modeles-codex-inconnu": [st(Object.assign({ agents: [CLAUDE, Object.assign({}, CODEX, { models: [] })] }, conv("Bouton de paiement"))), { type: "thread.reset", conversationId: "c1", items: [] }, click(".model .m")],
    "effort-adaptateur": [st(Object.assign({ efforts: [{ id: "default", label: "Default" }, { id: "low", label: "Low" }, { id: "medium", label: "Medium" }, { id: "high", label: "High" }, { id: "xhigh", label: "Xhigh" }, { id: "max", label: "Max" }], effort: "medium" }, conv("Bouton de paiement"))),
      { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Plus grand"), { type: "turn.start", turnId: "t1", model: "Opus 5.5", effort: "Default", agentId: "claude-code" },
        { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 4000 }, userMsg("u2", "Encore"), { type: "turn.start", turnId: "t2", model: "Opus 5.5", effort: "Medium", agentId: "claude-code" },
        { type: "turn.end", turnId: "t2", stopReason: "end_turn", durationMs: 4000 }] },
      click(".model .m"), wait(30), click(".menu .mi[data-key='effort']")],
    "jauge-survol": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", ASK)].concat(doneTurn("t1")) },
      { type: "usage", used: 86000, size: 270000 }],
    // V1.3 (B 1.6): a click on the page put the element in the composer; Ctrl+Entrée keeps it as an annotation
    "garder-annotation": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Aligne-le à droite")].concat(doneTurn("t1")) },
      () => send({ type: "element.attached", element: Object.assign({}, EL, { id: "el-title", label: "h1.title", kind: "heading" }) }), typeIn("Mets ce titre en gras")],
    "garde-en-annotation": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Aligne-le à droite")].concat(doneTurn("t1"),
      [{ type: "annotation.kept", id: "k1", text: "h1.title Mets ce titre en gras", elements: [Object.assign({}, EL, { id: "el-title", label: "h1.title", kind: "heading" })], annotations: ["ann_1"], at: today(15, 40) }]) }],
    // B 1.7: the selection switch, on (pressed) and off
    "selection-active": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [] }, { type: "pick.state", active: true }],
    "selection-coupee": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [] }, { type: "pick.state", active: false }],
    "garder-sans-texte": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [] }, () => send({ type: "element.attached", element: EL })],
    "envoyer-infobulle": [st(conv("Titre de la page")), { type: "thread.reset", conversationId: "c1", items: [] },
      () => send({ type: "element.attached", element: EL }), typeIn("Aligne-le à droite")],
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
    "etapes-annotations": [st(conv("Annotations en attente")), { type: "thread.reset", conversationId: "c1", items: [userMsg("u1", "Traite les annotations en attente sur ce site.", null, []),
        { type: "turn.start", turnId: "t1", model: "Sonnet 5.5", effort: "Élevé" },
        { type: "toolCall", turnId: "t1", id: "a1", title: "A lu 2 annotations", kind: "read", status: "completed", summary: { verb: "a lu", object: "2 annotations", count: 2, one: "annotation", many: "annotations" } },
        { type: "toolCall", turnId: "t1", id: "a2", title: "A lu landing.css", kind: "read", status: "completed", locations: [{ path: "src/styles/landing.css" }] },
        { type: "toolCall", turnId: "t1", id: "a3", title: "A marqué l'annotation « Bouton de paiement » comme terminée", kind: "other", status: "completed",
          summary: { verb: "a marqué", object: "l'annotation « Bouton de paiement »", after: "comme terminée", afterMany: "comme terminées" } },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "C'est fait : le bouton est aligné et l'annotation est terminée." },
        { type: "toolCall", turnId: "t1", id: "b1", title: "A lu 1 annotation", kind: "read", status: "completed", summary: { verb: "a lu", object: "1 annotation", count: 1, one: "annotation", many: "annotations" } },
        { type: "message.agentChunk", turnId: "t1", messageId: "m2", text: "Il en reste une, je la garde pour le prochain tour." },
        { type: "toolCall", turnId: "t1", id: "c1", title: "A marqué l'annotation « Titre » comme terminée", kind: "other", status: "completed",
          summary: { verb: "a marqué", object: "l'annotation « Titre »", after: "comme terminée", afterMany: "comme terminées" } },
        { type: "toolCall", turnId: "t1", id: "c2", title: "A marqué l'annotation « Badge » comme terminée", kind: "other", status: "completed",
          summary: { verb: "a marqué", object: "l'annotation « Badge »", after: "comme terminée", afterMany: "comme terminées" } },
        { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 9000 }] }],
    "annotations-avant": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      { type: "annotations.batch", batchId: "b1", siteLabel: "127.0.0.1:5183", items: ANN() }, () => document.activeElement && document.activeElement.blur()],
    "annotations-avant-long": [st(), { type: "thread.reset", conversationId: "new", items: [] },
      { type: "annotations.batch", batchId: "b1", siteLabel: "127.0.0.1:5183", items: ANN_MORE() }, click(".batch-row[data-id='a-badge']"), () => document.activeElement && document.activeElement.blur()],
    "annotations-pendant": [st(Object.assign({ running: true }, conv("Annotations en attente", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [annMsg(ANN().map((it, i) => Object.assign(it, { status: i ? "pending" : "working" }))),
        { type: "turn.start", turnId: "t1", model: "Opus 5.5", effort: "Moyen", at: ago(9000) },
        { type: "toolCall", turnId: "t1", id: "s1", title: "A lu 2 annotations", kind: "read", status: "completed", summary: { verb: "a lu", object: "2 annotations", count: 2, one: "annotation", many: "annotations" } },
        { type: "toolCall", turnId: "t1", id: "s2", title: "A lu landing.css", kind: "read", status: "completed", locations: [{ path: "styles/landing.css" }] },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "Je commence par le texte d'accroche : sa couleur #a8a299 est trop claire sur ce fond. Je la passe à la couleur secondaire du site." }] }],
    "annotations-fin": [st(conv("Annotations en attente")),
      { type: "thread.reset", conversationId: "c1", items: [annMsg(ANN().map((it, i) => Object.assign(it, { status: "done", summary: i ? "Badge replacé dans le coin de la carte." : "Couleur passée au gris secondaire du site." }))),
        { type: "turn.start", turnId: "t1", model: "Opus 5.5", effort: "Moyen", at: today(10, 51) },
        { type: "toolCall", turnId: "t1", id: "s1", title: "A lu 2 annotations", kind: "read", status: "completed", summary: { verb: "a lu", object: "2 annotations", count: 2, one: "annotation", many: "annotations" } },
        { type: "toolCall", turnId: "t1", id: "s2", title: "A modifié landing.css", kind: "edit", status: "completed", locations: [{ path: "styles/landing.css" }] },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "C'est fait pour les deux. La page s'est rechargée ; les annotations sont marquées « terminées » dans l'historique." },
        { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 41000, changes: [{ path: "styles/landing.css", status: "modified", added: 4, removed: 2 }], checkpointId: "cp-a", undoable: true }] }],
    "annotations-non-traitee": [st(conv("Annotations en attente")),
      { type: "thread.reset", conversationId: "c1", items: [annMsg(ANN().map((it, i) => Object.assign(it, i ? { status: "skipped", reason: "le badge vient d'une bibliothèque externe, rien à changer dans le projet." } : { status: "done", summary: "Couleur passée au gris secondaire du site." }))),
        { type: "turn.start", turnId: "t1", model: "Opus 5.5", effort: "Moyen", at: today(10, 51) },
        { type: "message.agentChunk", turnId: "t1", messageId: "m1", text: "J'ai corrigé le texte d'accroche. Le badge vient d'un composant externe : je l'ai laissé." },
        { type: "turn.end", turnId: "t1", stopReason: "end_turn", durationMs: 30000 }] }, click(".ann-inst")],
    "annotations-statuts": [st(Object.assign({ running: true }, conv("Annotations en attente", { running: true }))),
      { type: "thread.reset", conversationId: "c1", items: [annMsg(ANN()), { type: "turn.start", turnId: "t1", model: "Opus 5.5", effort: "Moyen", at: ago(3000) }] },
      { type: "annotations.status", batchId: "b1", id: "a-hero", status: "working" }, wait(20),
      { type: "annotations.status", batchId: "b1", id: "a-hero", status: "done", summary: "Couleur passée au gris secondaire du site." },
      { type: "annotations.status", batchId: "b1", id: "a-badge", status: "working" }],
    "dossier-puce": [st(), { type: "thread.reset", conversationId: "new", items: [] }],
    "dossier-demande": [st({ askFolder: true, folder: null, folderSuggestion: { path: "C:\\Users\\val\\projets\\boutique", display: "C:\\Users\\…\\projets\\boutique", name: "boutique", reason: "auto" } }), { type: "thread.reset", conversationId: "new", items: [] }],
    "6.7a-vide": [st(), { type: "thread.reset", conversationId: "new", items: [] }],
    "etapes-brutes": [st(Object.assign({ running: true }, conv("Bouton de paiement", { running: true }))), { type: "thread.reset", conversationId: "c1", items: [cUser(), cStart()].concat(RAW_STEPS()) }],
    "titre-renommer": [st(conv("Bouton de paiement")), { type: "thread.reset", conversationId: "c1", items: [] }],
    "6.7b-historique": [st(conv("Titre de la page", { running: true })), { type: "thread.reset", conversationId: "c1", items: [] },
      { type: "history", items: [
        { id: "c1", title: "Titre de la page", subtitle: "Modifie Header.tsx…", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "claude", updatedAt: ago(10000), running: true, current: true },
        { id: "c2", title: "Bouton de paiement", subtitle: "1 fichier modifié", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "claude", updatedAt: ago(2 * 60000) },
        { id: "c3", title: "Badge code promo", subtitle: "Questions seulement", siteLabel: "127.0.0.1:5173", thisSite: true, agentIcon: "codex", updatedAt: ago(26 * 3600000) },
        { id: "c4", title: "Menu de navigation", subtitle: "3 fichiers modifiés", siteLabel: "localhost:4173", thisSite: false, agentIcon: "claude", updatedAt: ago(4 * 24 * 3600000) }] },
      click(".hd-btn[aria-label=Historique]"), () => document.activeElement && document.activeElement.blur()],
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
  Object.assign(SC, C_SCENARIOS);
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
      case "setModel":   // another provider's model: the host starts a new conversation with that agent
        if (m.agentId && m.agentId !== (window.__agentId || "claude-code")) { window.__agentId = m.agentId; send({ type: "state", agentId: m.agentId, model: m.id, conversation: null }); send({ type: "thread.reset", conversationId: "new", items: [] }); }
        else send({ type: "state", model: m.id });
        break;
      case "setEffort": send({ type: "state", effort: m.id }); break;
      case "setMode": send({ type: "state", mode: m.id }); break;
      case "copy": send({ type: "copy.done", ok: true }); break;
      case "pickElement": send({ type: "pick.state", active: !!m.active }); break;
      case "annotate": send({ type: "annotation.kept", id: "k" + Date.now(), text: m.text, elements: [Object.assign({}, EL, { id: m.elements[0], label: "h1.title", kind: "heading" })], annotations: ["ann_x"], at: new Date().toISOString() }); break;
      case "annotations.request": case "suggestion.run":
        if (m.type === "suggestion.run" && m.id !== "annotations") break;
        send({ type: "annotations.batch", batchId: "b1", siteLabel: "127.0.0.1:5183", items: ANN() }); break;
      case "setAgent": send({ type: "state", agentId: m.id, model: m.id === "codex" ? "terra" : "sonnet", conversation: null }); send({ type: "thread.reset", conversationId: "new", items: [] }); break;
      case "checkpoint.viewChanges": send({ type: "changes.diff", turnId: m.turnId, diffs: /^c-/.test(window.__scenarioName || "") && window.__scenarioName !== "c-diff-plusieurs-fichiers" ? [C_DIFF] : FULL_DIFFS }); break;
      // bubble layout: the fake host's window follows the bubble (a real host resizes the WebView)
      // B 1.9: the overflow asked around the surface is granted (window.__grant caps a side, to play a screen edge) and
      // answered with `layout {open: false, inset}`, as the real host does
      // cv3 (R19): the host's window is always larger than the surface — a fixed transparent margin of up to 400 px on each
      // side (window.__grant caps a side, to play a screen edge); `layout {open: false, inset}` gives it after each
      // bubble.size. The overflow asked only sets the visible and clickable region (not played here).
      case "bubble.size": {
        const g = window.__grant || {}, side = (k) => Math.max(0, Math.min(400, g[k] == null ? 400 : g[k]));
        const inset = { top: side("top"), right: side("right"), bottom: side("bottom"), left: side("left") };
        window.__inset = inset; window.__surface = { width: m.width, height: m.height };
        window.__region = { x: inset.left - (m.overflowLeft || 0), y: inset.top - (m.overflowTop || 0), width: m.width + (m.overflowLeft || 0) + (m.overflowRight || 0), height: m.height + (m.overflowTop || 0) + (m.overflowBottom || 0) };
        document.documentElement.style.width = (m.width + inset.left + inset.right) + "px"; document.documentElement.style.height = (m.height + inset.top + inset.bottom) + "px";
        send({ type: "layout", mode: "bubble", open: false, maxWidth: lastLay.maxWidth, maxHeight: lastLay.maxHeight, side: lastLay.side, element: lastLay.element, inset });
        break;
      }
      case "close": if (bubbleOn && window.__scenarioName === "interactive") setTimeout(() => { send(LAY()); send({ type: "focus", target: "composer" }); }, 700); break;
      case "retry": if (window.__scenarioName === "interactive") fakeTurn("Réessayer"); break;
      case "queue.remove": send({ type: "queue", items: [] }); break;
      case "pickElement": send({ type: "pick.state", active: m.active }); if (m.active) setTimeout(() => send({ type: "element.attached", element: EL }), 600); break;
      case "attachImage": send({ type: "attachment.added", attachment: { id: "img" + Date.now(), kind: "image", name: m.name || "capture.png", thumb: thumbPng() } }); break;
      case "send": {
        if (m.annotations) {
          const items = ANN().filter((it) => m.annotations.ids.includes(it.id));
          send(Object.assign(annMsg(items), { text: m.text, at: new Date().toISOString() }));
          send({ type: "state", running: true, conversation: { id: "c1", title: "Annotations en attente", running: true } });
          send({ type: "turn.start", turnId: "ta", model: "Sonnet 5.5", effort: "Élevé", at: new Date().toISOString() });
          items.forEach((it, i) => {
            setTimeout(() => send({ type: "annotations.status", batchId: "b1", id: it.id, status: "working" }), 400 + i * 1400);
            setTimeout(() => send({ type: "annotations.status", batchId: "b1", id: it.id, status: "done", summary: "Corrigé dans landing.css." }), 1200 + i * 1400);
          });
          setTimeout(() => { send({ type: "turn.end", turnId: "ta", stopReason: "end_turn", durationMs: 3000 }); send({ type: "state", running: false }); }, 1400 + items.length * 1400);
          break;
        }
        if (window.__scenarioName === "interactive" || !window.__scenarioName) fakeTurn(m.text);
        break;
      }
      case "stop": send({ type: "turn.end", turnId: curTurn || "t" + turnSeq, stopReason: "cancelled", durationMs: 1000 }); send({ type: "state", running: false }); break;
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
    // dev.html?layout=bubble (no scenario): the interactive fake host in the comment-mode bubble
    const bubble = new URLSearchParams(location.search).get("layout") === "bubble";
    const steps = SC[name] || (bubble ? [LAY()] : []).concat([st(), { type: "thread.reset", conversationId: "new", items: [] }]);
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
