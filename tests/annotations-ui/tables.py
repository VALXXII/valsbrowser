"""Renders the measured tables of build/annotations-ui-qa/v2/report.json as Markdown (for design/annotations-ui.md).

    python tests/annotations-ui/tables.py > build/annotations-ui-qa/v2/tables.md
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
report = json.load(open(os.path.join(ROOT, 'build', 'annotations-ui-qa', 'v2', 'report.json'), encoding='utf-8'))
out = []
m = report['motion']
out.append('### Échantillons de mouvement (modèle pur)\n')
out.append(f"Modale : entrée {m['modalInMs']:.1f} ms, sortie {m['modalOutMs']:.1f} ms, mouvement réduit {m['reducedInMs']:.1f} ms ; "
           f"fondu croisé d'état à 98 % en {m['crossfade98Ms']:.1f} ms.\n")
out.append('| Surface | Instant | t (ms) | opacité | échelle | dx (DIP) | dy (DIP) |')
out.append('| --- | --- | ---: | ---: | ---: | ---: | ---: |')
for s in m['samples']:
    if s and s['surface'] != 'crossfade':
        out.append(f"| {s['surface']} | {s['name']} | {s['t']:.1f} | {s['opacity']:.4f} | {s['scale']:.4f} | {s['dx']:.3f} | {s['dy']:.3f} |")
for key, label in (('liveModal', 'Modale'), ('liveCard', 'Carte')):
    if key in report:
        out.append(f"\n{label} dans une vraie fenêtre : {report[key]['enterMs']} ms, {report[key]['frames']} images (horloge compositeur).")
for key in ('idle_card', 'idle_rest'):
    i = report.get(key)
    if i:
        out.append(f"\nRepos {i['ms']} ms ({key[5:]}) : images {i['frames']}, requêtes d'horloge {i['clockRequests']}, réveils {i['clockWakes']}, "
                   f"ticks {i['clockTicks']}, messages postés {i['posted']}, WM_TIMER des surfaces {i['wmTimer']} (autres fenêtres : {i['foreignWmTimer']}).")
out.append('\n### Géométrie Paper (modèle de boîtes CSS) vs layout natif\n')
out.append('| Surface | largeur | Paper | natif |')
out.append('| --- | ---: | ---: | ---: |')
for g in report['geometry']:
    if g:
        out.append(f"| {g['surface']} | {g['width']:g} | {g['paperHeight']:.2f} | {g['height']:.2f} |")
out.append('\n### Couleurs Paper échantillonnées dans les rendus (2x)\n')
out.append('| Point | Paper (composé) | Rendu |')
out.append('| --- | --- | --- |')
for c in report['paperColours']:
    if c:
        out.append(f"| {c['what']} | `{c['paper']}` | `{c['rendered']}` |")
out.append('\n### Contrastes mesurés (pixels rendus à 2x, puis paire de jetons)\n')
out.append('| Texte | mesuré | jetons | encre | fond |')
out.append('| --- | ---: | ---: | --- | --- |')
for c in report['contrast']:
    if c:
        out.append(f"| {c['what']} | {c['measured']:.2f}:1 | {c['tokens']:.2f}:1 | `{c['fg']}` | `{c['bg']}` |")
out.append(f"\nPolice mono résolue : {report['monoFamily']}.\n")
out.append(f"### Captures (`build/annotations-ui-qa/v2/`, {len([c for c in report['captures'] if c])})\n")
out.append(', '.join(f'`{c}`' for c in report['captures'] if c))
sys.stdout.reconfigure(encoding='utf-8')
print('\n'.join(out))
