$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
$out = 'build/lottie-polish/design-system'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/design-system/design_system_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/design_system_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Design system fixture compilation failed.' }
& "$PSScriptRoot/$out/design_system_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Design system fixture failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/ui/hover_favicon_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/hover_favicon_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Hover/favicon regression fixture compilation failed.' }
& "$PSScriptRoot/$out/hover_favicon_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Hover/favicon regression fixture failed.' }
