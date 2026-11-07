param([string]$DependencyRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/navigation-errors | Out-Null
& "$DependencyRoot/tools/w64devkit/bin/g++.exe" -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -DTVG_STATIC "-I$DependencyRoot/deps/thorvg-1.1.3/inc" "-I$DependencyRoot/deps/webview2/build/native/include" tests/navigation_errors_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o build/navigation-errors/navigation_errors_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Navigation error test compilation failed.' }
& ./build/navigation-errors/navigation_errors_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Navigation error tests failed.' }
