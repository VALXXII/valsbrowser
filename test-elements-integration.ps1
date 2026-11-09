param([string]$DependencyRoot = $PSScriptRoot)
# Contracts of src/elements-integration.h on the production handlers (no window, no WebView2).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/elements-integration | Out-Null
& "$DependencyRoot/tools/w64devkit/bin/g++.exe" -std=c++17 -O1 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -DTVG_STATIC "-I$DependencyRoot/deps/thorvg-1.1.3/inc" "-I$DependencyRoot/deps/webview2/build/native/include" tests/elements_integration_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o build/elements-integration/elements_integration_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Elements integration test compilation failed.' }
& ./build/elements-integration/elements_integration_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Elements integration tests failed.' }
