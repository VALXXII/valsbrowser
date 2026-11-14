$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Profiles backend (src/profiles.h, src/profile-color.h): colour legibility for every hue, registry,
# names, delete / undo / crash recovery and window routing, with real child processes.
# Everything runs inside build\profiles-sandbox; no user profile is ever read or written.

# The toolchain lives in tools\w64devkit (a worktree may only have it in the main checkout).
$gxx = Join-Path $PSScriptRoot 'tools\w64devkit\bin\g++.exe'
if (-not (Test-Path $gxx) -and $env:W64DEVKIT) { $gxx = Join-Path $env:W64DEVKIT 'bin\g++.exe' }
if (-not (Test-Path $gxx)) {
    $common = (& git rev-parse --path-format=absolute --git-common-dir 2>$null)
    if ($common) { $gxx = Join-Path (Split-Path $common -Parent) 'tools\w64devkit\bin\g++.exe' }
}
if (-not (Test-Path $gxx)) { throw "g++ not found (tools\w64devkit, `$env:W64DEVKIT or the main checkout)." }

$out = Join-Path $PSScriptRoot 'build\profiles'
New-Item -ItemType Directory -Force $out | Out-Null

# Clean the sandbox only if it is exactly <repo>\build\profiles-sandbox (never anything else).
$sandbox = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build\profiles-sandbox'))
$expected = [System.IO.Path]::GetFullPath((Join-Path ([System.IO.Path]::GetFullPath($PSScriptRoot)) 'build\profiles-sandbox'))
if ($sandbox -ne $expected -or -not $sandbox.EndsWith('\build\profiles-sandbox')) { throw "Refusing to clean unexpected sandbox path: $sandbox" }
if (Test-Path -LiteralPath $sandbox) {
    $item = Get-Item -LiteralPath $sandbox -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { throw "Refusing: $sandbox is a junction/symlink." }
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}

& $gxx -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/profile_color_test.cpp -o "$out\profile_color_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Profile colour test compilation failed.' }
& "$out\profile_color_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Profile colour checks failed.' }

& $gxx -std=c++17 -O1 -Wall -Wextra -Werror -static -municode tests/profiles_test.cpp -o "$out\profiles_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Profiles test compilation failed.' }
& "$out\profiles_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Profiles checks failed.' }
