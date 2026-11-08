$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Bookmarks in the real chrome: geometry, PNG renders, UI Automation (band, star, native
# menus, popover, drawer), keyboard, drag, undo and persistence. Demo data only; the
# profile it writes lives in build/bookmarks-ui-qa/profile.
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
$out = 'build/bookmarks-ui-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O1 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' '-Ideps/webview2/build/native/include' tests/bookmarks/bookmarks_ui_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/bookmarks_ui_test.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -loleaut32 -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Bookmarks UI fixture compilation failed.' }
& "$out/bookmarks_ui_test.exe" $PSScriptRoot "$PSScriptRoot\build\bookmarks-ui-qa"
if ($LASTEXITCODE -ne 0) { throw 'Bookmarks UI checks failed.' }
