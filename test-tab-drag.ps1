$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/tab-drag | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -DTVG_STATIC -Ideps/thorvg-1.1.3/inc -Ideps/webview2/build/native/include tests/ui/tab_drag_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/tab-drag/tab_drag_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Tab drag fixture compilation failed.' }
& ./build/tab-drag/tab_drag_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab drag regression failed.' }
