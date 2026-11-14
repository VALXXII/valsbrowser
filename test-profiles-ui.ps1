$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Profiles UI (Paper p-I-0): menu, tab preview, editor, delete dialog, P04 notices, download ring tint
# and success morph frames, rendered off screen (no window, no WebView2) and checked against Paper values.
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
$out = 'build/profiles-ui'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/ui/profile_ui_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/profile_ui_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi -loleaut32
if ($LASTEXITCODE -ne 0) { throw 'Profiles UI harness compilation failed.' }
& "$out/profile_ui_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Profiles UI checks failed.' }
