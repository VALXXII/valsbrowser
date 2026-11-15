param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Import from a browser (src/browser-import.h): discovery, readers, rules and commits on synthetic roots only
# (design/browser-data-import-plan.md). Needs Windows' own winsqlite3.dll; never reads a personal profile.
if (!$Compiler) { $Compiler = "$PSScriptRoot/tools/w64devkit/bin/g++.exe" }
if (!(Test-Path $Compiler)) { throw 'g++ not found: run setup.ps1 (tools/w64devkit) or pass -Compiler.' }
$out = 'build/browser-import-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& $Compiler -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -Isrc tests/browser-import/browser_import_test.cpp -o "$out/browser_import_test.exe" -lshell32 -lole32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Browser import test compilation failed.' }
& "$out/browser_import_test.exe" (Resolve-Path $out).Path
if ($LASTEXITCODE -ne 0) { throw 'Browser import tests failed.' }
