// Bac à sable du chat : simule ce qu'un site React en développement laisse sur ses éléments.
// Le bouton « Modifier le panier » reçoit une fausse « fibre » React avec _debugSource (React < 19) et un
// composant propriétaire, pour tester fichier:ligne sans React. window.__CHAT_SANDBOX_ROOT__ est écrit par
// serve.py (chemin absolu du dossier servi).
(function () {
  var root = window.__CHAT_SANDBOX_ROOT__ || '';
  var button = document.getElementById('react-button');
  if (!button || !root) return;
  function CartSummary() {}
  function CartButton() {}
  var owner = { type: CartSummary, _debugSource: null, _debugOwner: null, return: null };
  var component = { type: CartButton, _debugSource: { fileName: root + '/src/CartSummary.jsx', lineNumber: 9, columnNumber: 7 }, _debugOwner: owner, return: owner };
  button['__reactFiber$sandbox'] = {
    type: 'button',
    _debugSource: { fileName: root + '/src/CartButton.jsx', lineNumber: 6, columnNumber: 5 },
    _debugOwner: component,
    return: component
  };
})();
