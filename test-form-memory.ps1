param([string]$DependencyRoot = $PSScriptRoot)
# #121 windowless checks: typed-info store and file, engine import (fixture SQLite), report parser, rows, placement,
# arming, validation words, the isolated-world script (node --check when Node.js is present) and the list / bubble
# rendered OFFSCREEN into PNG (software Direct2D, no window). Contract: design/security-review-autofill-under-field.md.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/form-memory'
New-Item -ItemType Directory -Force $out | Out-Null
& "$DependencyRoot/tools/w64devkit/bin/g++.exe" -std=c++17 -O1 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -municode -Isrc -DTVG_STATIC "-I$DependencyRoot/deps/thorvg-1.1.3/inc" tests/form_memory_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$out/form_memory_test.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -loleaut32 -lshell32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Form memory test compilation failed.' }
& "$out/form_memory_test.exe" $PSScriptRoot (Join-Path $PSScriptRoot $out)
if ($LASTEXITCODE -ne 0) { throw 'Form memory checks failed.' }
$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if ($node) {
    & $node --check "$out/forms-world.js"
    if ($LASTEXITCODE -ne 0) { throw 'The isolated-world script is not valid JavaScript.' }
    Write-Output 'ok   the isolated-world script parses (node --check)'
} else { Write-Output 'SKIP node --check (Node.js not found)' }
# Fidelity against the Paper exports of the boards (tests/e2e/autofill-reference, exported from Paper): the offscreen
# renders, aligned within 2 px. The remaining difference is text rasterisation (DirectWrite grayscale vs Paper's).
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($python) {
    $bad = 0
    foreach ($c in @(@('infos', 7, 4.5), @('merged', 7.5, 5), @('bubble', 12, 8), @('keyboard', 7.5, 5), @('footer-hover', 7.5, 5), @('cross-hover', 7.5, 5))) {
        foreach ($scale in @(@('96', '1x'), @('192', '2x'))) {
            & $python tests/e2e/compare_png.py "$out/$($c[0])-$($scale[0]).png" "tests/e2e/autofill-reference/$($c[0])-$($scale[1]).png" --limit-mean $c[1] --limit-strong $c[2] --diff "$out/diff-$($c[0])-$($scale[0]).png"
            if ($LASTEXITCODE -ne 0) { $bad++ }
        }
    }
    foreach ($b in 'bubble-missing', 'bubble-domain', 'bubble-too-short') {   # board « états »: 2x exports only
        & $python tests/e2e/compare_png.py "$out/$b-192.png" "tests/e2e/autofill-reference/$b-2x.png" --limit-mean 12 --limit-strong 8 --diff "$out/diff-$b-192.png"
        if ($LASTEXITCODE -ne 0) { $bad++ }
    }
    if ($bad) { throw "Offscreen renders differ from the Paper boards ($bad)." }
} else { Write-Output 'SKIP Paper comparison (Python not found)' }
