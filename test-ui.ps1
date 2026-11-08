$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
New-Item -ItemType Directory -Force build/native-qa | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC -Ideps/thorvg-1.1.3/inc tests/ui/ui_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/native-qa/ui_test.exe -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Native UI harness compilation failed.' }
& build/native-qa/ui_test.exe $PSScriptRoot "$PSScriptRoot/build/native-qa"
if ($LASTEXITCODE -ne 0) { throw 'Native UI checks failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/motion/motion_test.cpp -o build/native-qa/motion_test.exe -luser32
if ($LASTEXITCODE -ne 0) { throw 'Native motion harness compilation failed.' }
& build/native-qa/motion_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Native motion checks failed.' }

& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -static tests/address_background_test.cpp -o build/native-qa/address_background_test.exe -lgdi32 -luser32 -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Address background harness compilation failed.' }
& build/native-qa/address_background_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Address background checks failed.' }

# Address bar (issue #20): rest states, editing glyph and suggestion rows, 96/144 DPI.
New-Item -ItemType Directory -Force build/native-qa/address | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC -Ideps/thorvg-1.1.3/inc tests/ui/address_render_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/native-qa/address_render_test.exe -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Address render harness compilation failed.' }
& build/native-qa/address_render_test.exe $PSScriptRoot "$PSScriptRoot/build/native-qa/address"
if ($LASTEXITCODE -ne 0) { throw 'Address render checks failed.' }
