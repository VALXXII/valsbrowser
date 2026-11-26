$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!(Test-Path 'build/lottie-backend/src/libthorvg-1.a')) { throw 'Run Python build-lottie.py before native UI fixtures.' }
$out = 'build/downloads-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/downloads/download_panel_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/download_panel_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Download panel harness compilation failed.' }
& "$out/download_panel_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Download panel checks failed.' }
# Rows' file icons: type by extension + MIME, thumbnails and their fallbacks, SVG never decoded (no window).
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-misleading-indentation -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' tests/downloads/download_file_kinds_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$out/download_file_kinds_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lcomctl32 -lgdi32 -ldwmapi -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Download file-kind harness compilation failed.' }
& "$out/download_file_kinds_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Download file-kind checks failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -static -municode tests/downloads/kept_downloads_test.cpp -o "$out/kept_downloads_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Kept downloads harness compilation failed.' }
& "$out/kept_downloads_test.exe" "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Kept downloads checks failed.' }
