param([switch]$Native,[string]$Out='build/split-dev')
# Split view regression: the pure model and the host (WndProc) fixtures, no WebView2.
# -Native also runs build\split-dev\browser.exe --split-test: real WebView2 pages from
# local files only (no network), evidence in build\split-dev\split-qa.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/split-tests | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -static -Wall -Wextra -Werror -Wno-missing-field-initializers tests/split-view/split_view_test.cpp -o build/split-tests/split_view_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Split model compilation failed.' }
& ./build/split-tests/split_view_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Split model regression failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -static -Wall -Wextra -Werror tests/split-view/site_name_test.cpp -o build/split-tests/site_name_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Site name compilation failed.' }
& ./build/split-tests/site_name_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Site name regression failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static '-DTVG_STATIC' '-Ideps/thorvg-1.1.3/inc' '-Ideps/webview2/build/native/include' tests/ui/split_ui_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/split-tests/split_ui_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Split host fixture compilation failed.' }
& ./build/split-tests/split_ui_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Split host regression failed.' }
if ($Native) {
    $dir = Join-Path $PSScriptRoot $Out
    & python tools/release/build.py --out $Out
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    Set-Content (Join-Path $dir 'prototype.log') ''
    $p = Start-Process -FilePath (Join-Path $dir 'browser.exe') -ArgumentList '--split-test' -WorkingDirectory $dir -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(150000)) { throw 'Native split test timed out; inspect prototype.log (process left running).' }
    $log = Get-Content (Join-Path $dir 'prototype.log') -Encoding UTF8 -Raw
    ($log -split "`n") | Where-Object { $_ -match '^SPLIT' }
    if ($p.ExitCode -ne 0 -or !$log.Contains('SPLIT PASS')) { throw "Native split test failed ($($p.ExitCode))." }
}
Write-Output 'Split view tests passed.'
