param([string]$DependencyRoot = $PSScriptRoot, [string]$Out = 'build/release-notes')
# « Nouveautés » release notes (design/release-notes-modal-report.md).
#  1. src/release-notes.h and src/modal.h compile alone and after chrome.h, no warning;
#  2. assets/lottie/logo-spin.json is byte-identical to its recorded source (SHA-256), no external image;
#  3. tests/release-notes/release_notes_test.cpp: model, read state, logo through the production ThorVG
#     player, the real modal (Mini / Full, FR / EN, 96 / 144 / 192 DPI renders in $Out).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force $Out | Out-Null
$gxx = "$DependencyRoot/tools/w64devkit/bin/g++.exe"
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-DTVG_STATIC', "-I$DependencyRoot/deps/thorvg-1.1.3/inc")
$libs = @('-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32', '-lmsimg32')

foreach ($h in 'release-notes', 'modal') {
    foreach ($withChrome in $false, $true) {
        $src = "$Out/hdr_check.cpp"
        $text = ($(if ($withChrome) { "#include `"../../src/chrome.h`"`n" } else { '' })) + "#include `"../../src/$h.h`"`nint main(){return 0;}`n"
        Set-Content -Encoding utf8 $src $text
        $log = & $gxx @flags -fsyntax-only $src 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $log -match 'warning|error') { Write-Host $log; throw "Header $h.h does not compile cleanly (chrome first: $withChrome)." }
    }
}
Write-Host 'headers: release-notes.h, modal.h compile alone and with chrome.h, no warning'

$expected = '84c8966245ff58d8eb55c32b50cf2fa5ec8e9caf76913fa28f52266a6d06e4d1'   # C:/Users/<user>/Downloads/logo-spin.json, 1 143 955 bytes
$hash = (Get-FileHash -Algorithm SHA256 'assets/lottie/logo-spin.json').Hash.ToLower()
if ($hash -ne $expected) { throw "logo-spin.json differs from its source: $hash" }
$json = Get-Content -Raw 'assets/lottie/logo-spin.json'
if ($json -match '"p"\s*:\s*"(data:|http|[^"]+\.(png|jpe?g|svg))') { throw 'logo-spin.json references an image' }
Write-Host "logo: assets/lottie/logo-spin.json SHA-256 $hash (identical to the source), no image asset"

& $gxx @flags -static -municode tests/release-notes/release_notes_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$Out/release_notes_test.exe" @libs
if ($LASTEXITCODE -ne 0) { throw 'release_notes_test compilation failed.' }
$root = (Resolve-Path .).Path
& "./$Out/release_notes_test.exe" $root (Join-Path $root $Out)
if ($LASTEXITCODE -ne 0) { throw 'release_notes_test failed.' }
