// Fichier « source » simulé du récapitulatif (bac à sable du chat).
import { CartButton } from './CartButton.jsx';

export function CartSummary({ lines, onEdit }) {
  return (
    <aside className="summary">
      <h2>Récapitulatif</h2>
      <ul className="lines">{lines.map((l) => <li key={l.name}><span>{l.name}</span><span>{l.price}</span></li>)}</ul>
      <CartButton onEdit={onEdit} />
    </aside>
  );
}
