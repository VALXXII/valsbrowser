param([string]$DependencyRoot = $PSScriptRoot)
# Panneau latéral (#127, variante A finale), headless only (no window, no WebView2, no profile):
#  1. tests/sidebar_test.cpp: Paper geometry of both modes, the column over the page, the fold and hover timers;
#  2. tests/settings_test.cpp: the « sidebar=complete|compact » key;
#  3. tests/ui/sidebar_render_test.cpp: the production painter in its column layout, PNGs + pixel checks
#     (build/sidebar-qa/renders), then tools/sidebar/compose.py: side-by-side with the Paper boards
#     (design/127-sidebar-evidence/paper -> design/127-sidebar-evidence);
#     tools/sidebar/compose_split.py: the split view in the column (tab list crops) next to Paper 2473-0's frames.
#  4. tests/ui/sidebar_split_drag_test.cpp: a tab dragged from the column onto the page = split view (the real window
#     procedure, its window and the drag overlay off screen at -32000, never shown): reorder / lift / grid / tear-off /
#     Escape, panel, compact, the column over the page, motion; renders in build/sidebar-split-qa.
#     WebView2 headers: deps/webview2 of $DependencyRoot, or WEBVIEW2_INCLUDE.
# The keyboard shortcut (Ctrl+Maj+,) is covered by test-shortcuts.ps1 (it compiles the whole browser).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/sidebar-qa'
New-Item -ItemType Directory -Force "$out/renders" | Out-Null
$gxx = "$DependencyRoot/tools/w64devkit/bin/g++.exe"
# Start-up guard: no // comment may swallow code in the one-line statement lists (the 0.3.850 trial's start failure).
python tools/sidebar/check_line_comments.py; if ($LASTEXITCODE -ne 0) { throw 'check_line_comments failed.' }
& $gxx -std=c++17 -Wall -Wextra -Werror -static tests/sidebar_test.cpp -o "$out/sidebar_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'sidebar_test compilation failed.' }
& "./$out/sidebar_test.exe"; if ($LASTEXITCODE -ne 0) { throw 'sidebar_test failed.' }
& $gxx -std=c++17 -Wall -Wextra -Werror -static tests/settings_test.cpp -o "$out/settings_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'settings_test compilation failed.' }
& "./$out/settings_test.exe"; if ($LASTEXITCODE -ne 0) { throw 'settings_test failed.' }
& $gxx -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC "-I$DependencyRoot/deps/thorvg-1.1.3/inc" tests/ui/sidebar_render_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$out/sidebar_render_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi -lshlwapi
if ($LASTEXITCODE -ne 0) { throw 'sidebar_render_test compilation failed.' }
& "./$out/sidebar_render_test.exe" $PSScriptRoot "$PSScriptRoot/$out/renders"; if ($LASTEXITCODE -ne 0) { throw 'sidebar_render_test failed.' }
python tools/sidebar/compose.py "$out/renders" design/127-sidebar-evidence
if ($LASTEXITCODE -ne 0) { throw 'compose failed.' }
python tools/sidebar/compose_split.py "$out/renders" design/127-sidebar-evidence
if ($LASTEXITCODE -ne 0) { throw 'compose_split failed.' }
$wv2 = if ($env:WEBVIEW2_INCLUDE) { $env:WEBVIEW2_INCLUDE } else { "$DependencyRoot/deps/webview2/build/native/include" }
New-Item -ItemType Directory -Force build/sidebar-split-qa | Out-Null
& $gxx -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -DTVG_STATIC "-I$DependencyRoot/deps/thorvg-1.1.3/inc" "-I$wv2" tests/ui/sidebar_split_drag_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o build/sidebar-split-qa/sidebar_split_drag_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'sidebar_split_drag_test compilation failed.' }
& ./build/sidebar-split-qa/sidebar_split_drag_test.exe; if ($LASTEXITCODE -ne 0) { throw 'sidebar_split_drag_test failed.' }
Write-Host 'test-sidebar: ok'
