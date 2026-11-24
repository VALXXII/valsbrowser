# Regenerate + validate the authored Lottie assets and design tokens.
# Native only: rlottie (PyPI rlottie-python, isolated venv) + the production
# ThorVG path (src/native-lottie.cpp + build/lottie-backend). No WebView2,
# no browser launch, no global installs. Output: build/opus-lottie-qa/.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'build/opus-lottie-qa'
$venv = Join-Path $out 'venv'
$py = Join-Path $venv 'Scripts/python.exe'
if (!(Test-Path $py)) {
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (!$python) { $python = Join-Path $env:LOCALAPPDATA 'Programs/Python/Python312/python.exe' }
    & $python -m venv $venv
    & $py -m pip install --disable-pip-version-check -q --only-binary=:all: 'rlottie-python==1.3.8' 'pillow==13.0.0'
}
$gxx = Join-Path $root 'tools/w64devkit/bin/g++.exe'
New-Item -ItemType Directory -Force "$out/ds", "$out/frames", "$out/thorvg" | Out-Null

& $py -I "$root/tests/lottie-assets/generate.py" $root; if ($LASTEXITCODE) { throw 'generate failed' }
& $py -I "$root/tests/lottie-assets/validate.py" $root "$out/frames"; if ($LASTEXITCODE) { throw 'rlottie validation failed' }

& $gxx -std=c++17 -Wall -Wextra -Werror -static "$root/tests/design-system/geometry_test.cpp" -o "$out/ds/geometry_test.exe"
if ($LASTEXITCODE) { throw 'geometry test build failed' }
& "$out/ds/geometry_test.exe"; if ($LASTEXITCODE) { throw 'geometry test failed' }

$tvg = Join-Path $root 'build/lottie-backend/src/libthorvg-1.a'
if (Test-Path $tvg) {
    & $gxx -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -DTVG_STATIC "-I$root/deps/thorvg-1.1.3/inc" "-I$root/src" `
        "$root/tests/lottie-assets/thorvg_dump.cpp" "$root/src/native-lottie.cpp" $tvg -o "$out/thorvg/thorvg_dump.exe"
    if ($LASTEXITCODE) { throw 'ThorVG harness build failed' }
    & "$out/thorvg/thorvg_dump.exe" $root "$out/thorvg"; if ($LASTEXITCODE) { throw 'ThorVG render failed' }
    & $py -I "$root/tests/lottie-assets/compare_thorvg.py" $root "$out/thorvg"; if ($LASTEXITCODE) { throw 'ThorVG comparison failed' }
} else { Write-Warning 'ThorVG library not built (build-lottie.py); ThorVG validation skipped.' }
