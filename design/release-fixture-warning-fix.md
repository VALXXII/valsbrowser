# CI : avertissements `misleading-indentation` des fixtures (download_panel_test)

Base : `main` `411acbbdfd647f944a4a1c60ae3db20cadd4e98c`. Symptôme CI (run 43646165014, PR73 ; même défaut sur le run de
PR69) : `tests/update-integration/run_all.py` compile `download_panel_test` avec `STRICT` (`-Wall -Wextra -Werror
-Wno-missing-field-initializers`) et échoue sur `-Werror=misleading-indentation`. Le script local `test-downloads.ps1`
passe `-Wno-misleading-indentation`, d'où l'écart local/CI. Indépendant du packaging B.

## Reproduction (rouge)

Compilation exacte de `run_all.py` (`-std=c++17 -static` + `STRICT` + `-municode -DTVG_STATIC`, mêmes sources et
bibliothèques) : **29 erreurs**, 28 dans `src/accessibility.h` (lignes 150 à 458), 1 dans `src/download-panel.h:695`
(`01-…`). Cause unique : une ligne `if (cond) instruction; autre; …` — l'`if` ne garde que la première instruction,
les suivantes sur la même ligne semblent gardées.

## Correction

Sur les 29 lignes signalées seulement : accolades autour de l'instruction gardée, rien d'autre
(`if(!out)return E_POINTER;*out=nullptr;` → `if(!out){return E_POINTER;}*out=nullptr;`). Comportement identique :
`02-…` vérifie mécaniquement que chaque ligne modifiée ne diffère de l'originale que par l'insertion d'un `{` et d'un
`}` (29/29), nombre de lignes inchangé. Aucun drapeau, test, UIA ou comportement modifié ; `-Werror` conservé.

## Vérification (vert)

- Compilation stricte identique à la CI : exit 0, aucun diagnostic (`03-…`) ; `download_panel_test` : checks passés (`04-…`).
- `python tests/update-integration/run_all.py --only existing --ci` : **ALL PASSED** (`05-…`), dont `accessibility_test`
  (= `test-accessibility.ps1`, mêmes drapeaux), `download_panel_test` et `history_panel_test` (stricts).

Non exécuté : les étapes `peers`, `integration` et `app` de `run_all.py` (non concernées par ces deux en-têtes, `app`
demande une build release) ; la CI GitHub de la PR fera foi.
