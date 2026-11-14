$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
$out = 'build/history-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/history/history_panel_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/history_panel_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi -lshell32
if ($LASTEXITCODE -ne 0) { throw 'History panel harness compilation failed.' }
& "$out/history_panel_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'History panel checks failed.' }
