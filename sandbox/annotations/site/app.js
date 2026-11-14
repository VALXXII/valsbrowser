// Tiny "SPA" for the annotation sandbox. Everything is re-created by render(): the
// « Re-rendre la page » button replaces every node (same URL), « Changer d'articles »
// replaces the list, « Aller à /merci » changes the URL with pushState (the browser's
// annotation mode must end: SourceChanged).
'use strict';
const SECRET = 'MDP-SANDBOX-7f3a9c';   // pre-filled password: must NEVER appear in the annotation store
const catalogues = [
  [['Carnet ligné A5', '18,00 €'], ['Stylo plume', '30,00 €']],
  [['Tasse en grès', '22,00 €'], ['Torchon lin', '14,00 €'], ['Bougie figue', '12,00 €']],
];
let generation = 0, catalogue = 0;

function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'on') for (const [type, fn] of Object.entries(v)) e.addEventListener(type, fn);
    else e.setAttribute(k, v);
  }
  for (const c of children) e.append(c);
  return e;
}
function list() {
  return el('ul', { class: 'items', 'data-testid': 'cart-items', 'data-generation': String(generation) },
    ...catalogues[catalogue].map(([name, price]) => el('li', {}, el('span', {}, name), el('span', {}, price))));
}
function total() {
  const sum = catalogues[catalogue].reduce((s, [, p]) => s + parseFloat(p.replace(',', '.')), 0);
  return sum.toFixed(2).replace('.', ',') + ' €';
}
function drawCanvas(c) {
  const ctx = c.getContext('2d'); const w = c.width = 400, h = c.height = 260;
  ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, w, h);
  const bars = [40, 90, 65, 130, 110, 170];
  bars.forEach((v, i) => { ctx.fillStyle = i === bars.length - 1 ? '#3e67ff' : '#c8d2ff'; ctx.fillRect(30 + i * 58, h - 30 - v, 36, v); });
  ctx.fillStyle = '#1d1d1d'; ctx.font = '22px system-ui'; ctx.fillText('Ventes (canvas)', 30, 36);
}
function render() {
  generation++;
  const app = document.getElementById('app');
  const pay = el('button', { class: 'btn-primary', type: 'submit', 'data-testid': 'pay-button', 'data-source-line': 'Checkout.tsx:42' }, 'Payer ' + total());
  const form = el('form', { class: 'checkout', on: { submit: (e) => { e.preventDefault(); pay.textContent = 'Paiement envoyé ✓'; } } },
    el('h1', {}, 'Paiement'),
    el('label', { for: 'email' }, 'Adresse e-mail'),
    el('input', { id: 'email', name: 'email', type: 'email', autocomplete: 'email', placeholder: 'vous@exemple.fr', value: 'client@exemple.fr' }),
    el('label', { for: 'card' }, 'Carte'),
    el('input', { id: 'card', name: 'card', inputmode: 'numeric', placeholder: '1234 5678 9012 3456' }),
    el('label', { for: 'password' }, 'Mot de passe du compte'),
    el('input', { id: 'password', name: 'password', type: 'password', autocomplete: 'current-password', value: SECRET }),
    el('secure-field', {}),
    el('div', { class: 'actions' }, pay, el('promo-badge', {})));
  const summary = el('aside', { class: 'summary', 'aria-label': 'Récapitulatif' },
    el('h2', {}, 'Récapitulatif'), list(),
    el('div', { class: 'total' }, el('span', {}, 'Total'), el('span', { 'data-testid': 'total' }, total())),
    el('div', { class: 'row' },
      el('button', { class: 'btn-ghost', type: 'button', 'data-testid': 'swap-items', on: { click: () => { catalogue = 1 - catalogue; summary.querySelector('.items').replaceWith(list()); summary.querySelector('[data-testid=total]').textContent = total(); pay.textContent = 'Payer ' + total(); } } }, 'Changer d\'articles'),
      el('button', { class: 'btn-ghost', type: 'button', 'data-testid': 'rerender', on: { click: render } }, 'Re-rendre la page (SPA)'),
      el('button', { class: 'btn-ghost', type: 'button', 'data-testid': 'go-thanks', on: { click: () => { history.pushState({}, '', '/merci'); document.title = 'Merci — Boutique'; render(); } } }, 'Aller à /merci')));
  const crossSrc = location.protocol + '//127.0.0.2:5174/cross.html';
  const canvas = el('canvas', { 'aria-label': 'Graphique des ventes', 'data-testid': 'sales-chart' });
  const frames = el('section', { class: 'frames' },
    el('figure', {}, el('figcaption', {}, 'iframe même origine'), el('iframe', { src: 'frame.html', title: 'Livraison' })),
    el('figure', {}, el('figcaption', {}, 'iframe d\'une autre origine (127.0.0.2:5174)'), el('iframe', { src: crossSrc, title: 'Paiement PayCo' })),
    el('figure', {}, el('figcaption', {}, 'canvas'), canvas));
  app.replaceChildren(form, summary, frames, el('p', { class: 'note' }, 'Génération ' + generation + ' · ' + location.pathname));
  drawCanvas(canvas);
}
window.addEventListener('popstate', render);
render();
