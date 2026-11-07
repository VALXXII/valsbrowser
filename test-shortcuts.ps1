$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O0 -static -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' '-Ideps/webview2/build/native/include' tests/shortcuts_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/shortcuts_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Shortcut test compilation failed.' }
& build/shortcuts_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Shortcut checks failed.' }
