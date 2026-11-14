// Two web components for the annotation sandbox:
//   <promo-badge>   OPEN shadow root  -> the annotation script descends into it (shadowPath)
//   <secure-field>  CLOSED shadow root -> only the host can be annotated
'use strict';
customElements.define('promo-badge', class extends HTMLElement {
  connectedCallback() {
    if (this.shadowRoot) return;
    const root = this.attachShadow({ mode: 'open' });
    root.innerHTML = '<style>.badge{display:inline-flex;gap:6px;align-items:center;padding:4px 10px;border-radius:999px;background:#eef2ff;color:#2f4fd9;font:500 12px/18px system-ui}' +
      '.code{font-family:Consolas,monospace;background:#fff;border-radius:4px;padding:0 4px}</style>' +
      '<span class="badge" part="badge">Code promo <span class="code" data-testid="promo-code">AUTOMNE10</span></span>';
  }
});
customElements.define('secure-field', class extends HTMLElement {
  connectedCallback() {
    if (this._root) return;
    this._root = this.attachShadow({ mode: 'closed' });
    this._root.innerHTML = '<style>div{border:1px dashed #c9c9c6;border-radius:8px;padding:10px 12px;font:13px system-ui;color:#4a4a4a}</style>' +
      '<div>Champ sécurisé (shadow DOM fermé) — <b>3D Secure</b></div>';
  }
});
