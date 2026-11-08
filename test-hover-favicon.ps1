$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
New-Item -ItemType Directory -Force build/hover-favicon-qa | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/ui/hover_favicon_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/hover-favicon-qa/hover_favicon_test.exe -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lgdi32
if ($LASTEXITCODE -ne 0) { throw 'Focused native test compilation failed.' }
& build/hover-favicon-qa/hover_favicon_test.exe $PSScriptRoot "$PSScriptRoot/build/hover-favicon-qa"
if ($LASTEXITCODE -ne 0) { throw 'Focused native hover/favicon test failed.' }
