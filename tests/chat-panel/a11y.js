// Accessibility audit evaluated inside the panel page by run.py. Returns {controls, problems, small}.
(() => {
  const problems = [], small = [];
  const visible = (n) => { const r = n.getBoundingClientRect(); const s = getComputedStyle(n); return r.width > 0 && r.height > 0 && s.visibility !== "hidden" && !n.closest("[hidden]"); };
  const byId = (id) => document.getElementById(id);
  const nameOf = (n) => {
    if (n.getAttribute("aria-label")) return n.getAttribute("aria-label").trim();
    const lb = n.getAttribute("aria-labelledby");
    if (lb) return lb.split(/\s+/).map((i) => (byId(i) || {}).textContent || "").join(" ").trim();
    if (n.labels && n.labels.length) return n.labels[0].textContent.trim();
    return (n.textContent || "").trim() || (n.getAttribute("title") || "").trim();
  };
  const sel = "button, a[href], input, textarea, select, [contenteditable='true'], [role=button], [role=menuitem], [role=menuitemradio], [role=option], [role=radio], [role=switch], [role=textbox], [tabindex]:not([tabindex='-1'])";
  const controls = Array.from(document.querySelectorAll(sel)).filter(visible);
  for (const n of controls) {
    const label = n.tagName.toLowerCase() + (n.className && typeof n.className === "string" ? "." + n.className.split(" ")[0] : "");
    if (!nameOf(n)) problems.push("no accessible name: " + label);
    const ti = parseInt(n.getAttribute("tabindex") || "0", 10);
    if (ti > 0) problems.push("positive tabindex: " + label);
    if (n.closest("[aria-hidden='true']")) problems.push("focusable inside aria-hidden: " + label);
    if (n.tagName === "BUTTON" && !n.textContent.trim() && !n.getAttribute("aria-label")) problems.push("icon-only button without aria-label: " + label);
    const r = n.getBoundingClientRect();
    if ((r.width < 24 || r.height < 24) && n.tagName === "BUTTON" && n.tabIndex >= 0) small.push(label + " " + Math.round(r.width) + "x" + Math.round(r.height));
  }
  for (const img of document.querySelectorAll("img")) if (!img.hasAttribute("alt")) problems.push("img without alt");
  const ids = {};
  for (const n of document.querySelectorAll("[id]")) { if (ids[n.id]) problems.push("duplicate id " + n.id); ids[n.id] = 1; }
  for (const attr of ["aria-controls", "aria-labelledby", "aria-describedby", "aria-activedescendant"])
    for (const n of document.querySelectorAll("[" + attr + "]")) for (const id of n.getAttribute(attr).split(/\s+/).filter(Boolean))
      if (!byId(id) && !(attr === "aria-controls" && n.getAttribute("aria-expanded") === "false")) problems.push(attr + " → missing #" + id);
  if (!document.querySelector("[role=log]")) problems.push("no role=log for the thread");
  if (!document.querySelector("[role=status][aria-live=polite]") || !document.querySelector("[role=alert]")) problems.push("missing live regions");
  if (!document.documentElement.lang) problems.push("no lang");
  for (const n of document.querySelectorAll("[role=menuitemradio],[role=radio],[role=switch]")) if (!n.hasAttribute("aria-checked")) problems.push("checkable without aria-checked");
  return { controls: controls.length, problems, small };
})()
