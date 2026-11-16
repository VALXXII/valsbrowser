param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Import from a browser (src/browser-import.h): discovery, readers, rules and commits on synthetic roots only
# (design/browser-data-import-plan.md). Needs Windows' own winsqlite3.dll; never reads a personal profile.
if (!$Compiler) { $Compiler = "$PSScriptRoot/tools/w64devkit/bin/g++.exe" }
if (!(Test-Path $Compiler)) { throw 'g++ not found: run setup.ps1 (tools/w64devkit) or pass -Compiler.' }
$out = 'build/browser-import-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& $Compiler -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -Isrc tests/browser-import/browser_import_test.cpp -o "$out/browser_import_test.exe" -lshell32 -lole32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Browser import test compilation failed.' }
& "$out/browser_import_test.exe" (Resolve-Path $out).Path
if ($LASTEXITCODE -ne 0) { throw 'Browser import tests failed.' }
# Settings wiring (main.cpp hooks) and the real drawer's Confidentialité rows, synthetic hooks only.
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before the settings fixture.' }
& $Compiler -std=c++17 -static -O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' '-Ideps/webview2/build/native/include' tests/browser-import/import_settings_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/import_settings_test.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -loleaut32 -lshell32 -lmsimg32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw 'Import settings test compilation failed.' }
& "$out/import_settings_test.exe" $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Import settings tests failed.' }
# Import dialog states rendered offscreen by the production modal (native fixture, not the real app); blank renders fail.
& $Compiler -std=c++17 -static -O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' '-Ideps/webview2/build/native/include' tests/browser-import/import_dialog_render_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/import_dialog_render_test.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -loleaut32 -lshell32 -lmsimg32 -lbcrypt -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Import dialog render test compilation failed.' }
& "$out/import_dialog_render_test.exe" $PSScriptRoot (Join-Path (Resolve-Path $out).Path 'import-render')
if ($LASTEXITCODE -ne 0) { throw 'Import dialog render test failed (a blank or missing render).' }
# Bookmark drag rules (pure) and « Placer le contenu dans la barre de favoris » through the real save / reload / undo.
& $Compiler -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static tests/bookmark_drag_test.cpp -o "$out/bookmark_drag_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Bookmark drag test compilation failed.' }
& "$out/bookmark_drag_test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Bookmark drag tests failed.' }
& $Compiler -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -Isrc tests/browser-import/bookmark_promote_test.cpp -o "$out/bookmark_promote_test.exe" -lshell32 -lole32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Bookmark promote test compilation failed.' }
& "$out/bookmark_promote_test.exe" (Resolve-Path $out).Path; if ($LASTEXITCODE -ne 0) { throw 'Bookmark promote tests failed.' }
