$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Link preview bubble (src/status-bubble.h): native fixture, no WebView2. Evidence BMPs
# land in build/status-bubble-qa.
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
New-Item -ItemType Directory -Force build/status-bubble-qa | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/ui/status_bubble_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/status-bubble-qa/status_bubble_test.exe -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lgdi32 -luser32 -lmsimg32
if ($LASTEXITCODE -ne 0) { throw 'Status bubble test compilation failed.' }
& build/status-bubble-qa/status_bubble_test.exe $PSScriptRoot "$PSScriptRoot/build/status-bubble-qa"
if ($LASTEXITCODE -ne 0) { throw 'Status bubble test failed.' }
