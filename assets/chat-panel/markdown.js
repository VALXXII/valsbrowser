// Safe Markdown for agent text: a CommonMark + GFM subset (paragraphs, headings, lists, quotes,
// fenced code, tables, rules, inline code, emphasis, links). No raw HTML is ever interpreted:
// every node is built with createElement / textContent. Links never navigate from the panel;
// a click calls opts.onLink(url) so the host decides.
"use strict";
(function () {
  const FENCE = /^ {0,3}(`{3,}|~{3,})\s*([^\s`]*)\s*(.*)$/;
  const HEADING = /^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$/;
  const RULE = /^ {0,3}([-*_])(\s*\1){2,}\s*$/;
  const QUOTE = /^ {0,3}>\s?(.*)$/;
  const ITEM = /^(\s*)([-*+]|\d{1,9}[.)])\s+(.*)$/;
  const TABLE_SEP = /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$/;
  const SAFE_URL = /^(https?:\/\/|file:\/\/\/)[^\s]*$/i;

  function el(tag, cls, text) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }

  // ---- block parser: text -> [{type, src, ...}] -------------------------------------------
  function parseBlocks(text) {
    const lines = text.replace(/\r\n?/g, "\n").split("\n");
    const out = [];
    let i = 0;
    const startsBlock = (l) => FENCE.test(l) || HEADING.test(l) || RULE.test(l) || QUOTE.test(l) || ITEM.test(l);
    while (i < lines.length) {
      const line = lines[i];
      if (!line.trim()) { i++; continue; }
      let m;
      if ((m = FENCE.exec(line))) {
        const mark = m[1], start = i;
        const body = [];
        i++;
        let closed = false;
        while (i < lines.length) {
          const l = lines[i];
          const c = /^ {0,3}(`{3,}|~{3,})\s*$/.exec(l);
          if (c && c[1][0] === mark[0] && c[1].length >= mark.length) { closed = true; i++; break; }
          body.push(l); i++;
        }
        const info = (m[3] || "").trim();
        let name = "";
        const t = /(?:title|file|filename)=["']?([^"'\s]+)/.exec(info);
        if (t) name = t[1];
        else if (/^[\w./\\-]+\.[\w]+(?::\d+)?$/.test(info)) name = info;
        else if (/^[\w./\\-]+\.[\w]+$/.test(m[2]) && !info) name = m[2];
        out.push({ type: "code", lang: m[2] && m[2] !== name ? m[2] : "", name, code: body.join("\n"), closed,
                   src: lines.slice(start, i).join("\n") });
        continue;
      }
      if ((m = HEADING.exec(line))) { out.push({ type: "h", level: m[1].length, text: m[2], src: line }); i++; continue; }
      if (RULE.test(line)) { out.push({ type: "hr", src: line }); i++; continue; }
      if (QUOTE.test(line)) {
        const start = i, body = [];
        while (i < lines.length && lines[i].trim() && (m = QUOTE.exec(lines[i]))) { body.push(m[1]); i++; }
        out.push({ type: "quote", text: body.join("\n"), src: lines.slice(start, i).join("\n") });
        continue;
      }
      if ((m = ITEM.exec(line))) {
        const start = i, ordered = /\d/.test(m[2]), items = [];
        const baseIndent = m[1].length;
        while (i < lines.length) {
          const l = lines[i];
          const im = ITEM.exec(l);
          if (im && im[1].length <= baseIndent + 1 && /\d/.test(im[2]) === ordered) {
            items.push({ text: im[3], num: ordered ? parseInt(im[2], 10) : 0 }); i++; continue;
          }
          if (l.trim() && items.length && /^\s+\S/.test(l) && !FENCE.test(l)) { items[items.length - 1].text += "\n" + l.trim(); i++; continue; }
          break;
        }
        out.push({ type: ordered ? "ol" : "ul", items, start: items[0].num || 1, src: lines.slice(start, i).join("\n") });
        continue;
      }
      if (line.includes("|") && i + 1 < lines.length && TABLE_SEP.test(lines[i + 1]) && lines[i + 1].includes("-")) {
        const start = i;
        const row = (l) => l.trim().replace(/^\|/, "").replace(/\|$/, "").split("|").map((c) => c.trim());
        const head = row(line);
        i += 2;
        const rows = [];
        while (i < lines.length && lines[i].includes("|") && lines[i].trim()) { rows.push(row(lines[i])); i++; }
        out.push({ type: "table", head, rows, src: lines.slice(start, i).join("\n") });
        continue;
      }
      const start = i, body = [];
      while (i < lines.length && lines[i].trim() && (i === start || !startsBlock(lines[i]))) { body.push(lines[i]); i++; }
      out.push({ type: "p", text: body.join("\n"), src: lines.slice(start, i).join("\n") });
    }
    return out;
  }

  // ---- inline parser ------------------------------------------------------------------------
  function inline(parent, text, opts) {
    let i = 0, buf = "";
    const flush = () => { if (buf) { parent.appendChild(document.createTextNode(buf)); buf = ""; } };
    while (i < text.length) {
      const c = text[i];
      if (c === "\\" && i + 1 < text.length && /[\\`*_\[\]()#+\-.!~|>]/.test(text[i + 1])) { buf += text[i + 1]; i += 2; continue; }
      if (c === "\n") { flush(); parent.appendChild(document.createElement("br")); i++; continue; }
      if (c === "`") {
        let n = 1; while (text[i + n] === "`") n++;
        const fence = "`".repeat(n);
        const end = text.indexOf(fence, i + n);
        if (end > 0) { flush(); parent.appendChild(el("code", "md-code", text.slice(i + n, end).replace(/^ (.*) $/, "$1"))); i = end + n; continue; }
      }
      if ((c === "*" || c === "_") && text[i + 1] === c) {
        const end = text.indexOf(c + c, i + 2);
        if (end > i + 2) { flush(); const s = el("strong"); inline(s, text.slice(i + 2, end), opts); parent.appendChild(s); i = end + 2; continue; }
      }
      if (c === "~" && text[i + 1] === "~") {
        const end = text.indexOf("~~", i + 2);
        if (end > i + 2) { flush(); const s = el("del"); inline(s, text.slice(i + 2, end), opts); parent.appendChild(s); i = end + 2; continue; }
      }
      if ((c === "*" || c === "_") && text[i + 1] && !/\s/.test(text[i + 1]) && (c === "*" || i === 0 || /\W/.test(text[i - 1]))) {
        const end = text.indexOf(c, i + 1);
        if (end > i + 1 && !/\s/.test(text[end - 1])) { flush(); const s = el("em"); inline(s, text.slice(i + 1, end), opts); parent.appendChild(s); i = end + 1; continue; }
      }
      if (c === "[") {
        const m = /^\[([^\]\n]{1,500})\]\(([^)\s]{1,2048})\)/.exec(text.slice(i));
        if (m) { flush(); parent.appendChild(link(m[1], m[2], opts)); i += m[0].length; continue; }
      }
      if ((c === "h" || c === "f") && /^(https?:\/\/|file:\/\/\/)/i.test(text.slice(i, i + 8)) && (i === 0 || /[\s(]/.test(text[i - 1]))) {
        const m = /^\S+/.exec(text.slice(i));
        let u = m[0].replace(/[.,;:!?)]+$/, "");
        flush(); parent.appendChild(link(u, u, opts)); i += u.length; continue;
      }
      buf += c; i++;
    }
    flush();
  }

  function link(label, url, opts) {
    if (!SAFE_URL.test(url)) { const s = el("span"); inline(s, label, opts); return s; }
    const a = el("a", "md-link");
    a.href = url;
    a.title = url;
    inline(a, label, opts);
    const go = (e) => { e.preventDefault(); if (opts && opts.onLink) opts.onLink(url); };
    a.addEventListener("click", go);
    a.addEventListener("auxclick", go);
    return a;
  }

  // ---- block rendering ---------------------------------------------------------------------
  function renderBlock(b, opts) {
    switch (b.type) {
      case "h": { const n = el("p", "md-h md-h" + Math.min(b.level, 3)); n.setAttribute("role", "heading"); n.setAttribute("aria-level", String(Math.min(b.level + 2, 6))); inline(n, b.text, opts); return n; }
      case "hr": return el("hr", "md-hr");
      case "quote": { const n = el("blockquote", "md-quote"); inline(n, b.text, opts); return n; }
      case "ul": case "ol": {
        const n = el(b.type, "md-list");
        if (b.type === "ol" && b.start !== 1) n.start = b.start;
        for (const it of b.items) { const li = el("li"); inline(li, it.text, opts); n.appendChild(li); }
        return n;
      }
      case "table": {
        const wrap = el("div", "md-table-wrap");
        const t = el("table", "md-table");
        const tr = el("tr");
        for (const h of b.head) { const th = el("th"); inline(th, h, opts); tr.appendChild(th); }
        const thead = el("thead"); thead.appendChild(tr); t.appendChild(thead);
        const tb = el("tbody");
        for (const r of b.rows) { const row = el("tr"); for (let k = 0; k < b.head.length; k++) { const td = el("td"); inline(td, r[k] || "", opts); row.appendChild(td); } tb.appendChild(row); }
        t.appendChild(tb); wrap.appendChild(t);
        return wrap;
      }
      case "code": return opts && opts.codeBlock ? opts.codeBlock(b) : el("pre", "md-pre", b.code);
      default: { const n = el("p", "md-p"); inline(n, b.text, opts); return n; }
    }
  }

  // Incremental renderer: only blocks whose source changed are rebuilt (during streaming that is
  // the last one), so a long answer costs one paragraph of DOM work per frame.
  class Doc {
    constructor(root, opts) { this.root = root; this.opts = opts || {}; this.blocks = []; this.nodes = []; }
    set(text) {
      const next = parseBlocks(text);
      let k = 0;
      while (k < next.length && k < this.blocks.length && next[k].src === this.blocks[k].src) k++;
      for (let j = this.nodes.length - 1; j >= k; j--) { this.nodes[j].remove(); this.nodes.pop(); }
      for (let j = k; j < next.length; j++) { const n = renderBlock(next[j], this.opts); this.root.appendChild(n); this.nodes.push(n); }
      this.blocks = next;
    }
  }

  window.ChatMarkdown = { parseBlocks, inline, Doc };
})();
