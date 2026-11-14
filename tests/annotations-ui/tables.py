"""Renders the measured tables of build/annotations-ui-qa/report.json as Markdown (for design/annotations-ui.md).

    python tests/annotations-ui/tables.py > build/annotations-ui-qa/tables.md
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
report = json.load(open(os.path.join(ROOT, 'build', 'annotations-ui-qa', 'report.json'), encoding='utf-8'))
out = []
m = report['motion']
out.append('### Échantillons de mouvement (carte, modèle pur)\n')
out.append('| Instant | t (ms) | progression | opacité | échelle | dy (DIP) |')
out.append('| --- | ---: | ---: | ---: | ---: | ---: |')
for s in m['samples']:
    if s:
        out.append(f"| {s['name']} | {s['t']:.1f} | {s['progress']:.4f} | {s['opacity']:.4f} | {s['scale']:.4f} | {s['dy']:.3f} |")
i = report.get('idle')
if i:
    out.append('\n### Fenêtre réelle : entrée de la carte puis repos\n')
    out.append(f"Entrée : {i['cardEnterMs']} ms, {i['cardEnterFrames']} images (horloge compositeur). "
               f"Repos {i['ms']} ms : images {i['frames']}, requêtes d'horloge {i['clockRequests']}, réveils du thread {i['clockWakes']}, "
               f"ticks {i['clockTicks']}, messages postés {i['posted']}, WM_TIMER {i['wmTimer']}.")
out.append('\n### Géométrie Paper (modèle de boîtes CSS) vs layout natif\n')
out.append('| Surface | largeur | hauteur Paper | hauteur native |')
out.append('| --- | ---: | ---: | ---: |')
for g in report['geometry']:
    if g:
        paper = f"{g['paperHeight']:.2f}" if 'paperHeight' in g else '—'
        out.append(f"| {g['surface']} | {g['width']} | {paper} | {g['height']:.2f} |")
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
out.append('### Captures (`build/annotations-ui-qa/`)\n')
out.append(', '.join(f'`{c}`' for c in report['captures'] if c))
sys.stdout.reconfigure(encoding='utf-8')
print('\n'.join(out))
