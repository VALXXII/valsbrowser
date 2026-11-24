param([string]$DependencyRoot = $PSScriptRoot)
# Panneau latéral (#127, variante A finale), headless only (no window, no WebView2, no profile):
#  1. tests/sidebar_test.cpp: Paper geometry of both modes, the column over the page, the fold and hover timers;
#  2. tests/settings_test.cpp: the « sidebar=complete|compact » key;
#  3. tests/ui/sidebar_render_test.cpp: the production painter in its column layout, PNGs + pixel checks
#     (build/sidebar-qa/renders), then tools/sidebar/compose.py: side-by-side with the Paper boards
#     (design/127-sidebar-evidence/paper -> design/127-sidebar-evidence).
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
Write-Host 'test-sidebar: ok'
