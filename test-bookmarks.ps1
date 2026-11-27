param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Pure bookmarks model (src/bookmarks.h): no HWND, Direct2D or WebView2.
if (!$Compiler) {
    $Compiler = "$PSScriptRoot/tools/w64devkit/bin/g++.exe"
    if (!(Test-Path $Compiler)) { $found = Get-Command g++ -ErrorAction SilentlyContinue; if ($found) { $Compiler = $found.Source } }
}
if (!(Test-Path $Compiler)) { throw 'g++ not found: run setup.ps1 (tools/w64devkit) or pass -Compiler.' }
$out = 'build/bookmarks-qa'
New-Item -ItemType Directory -Force $out | Out-Null
& $Compiler -std=c++17 -Wall -Wextra -Werror -static tests/bookmarks_test.cpp -o "$out/bookmarks_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Bookmarks test compilation failed.' }
& "$out/bookmarks_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Bookmarks tests failed.' }
# A folder's icon and colour (#138, src/bookmark-folder-style.h): palette, icons, picker geometry and keyboard model.
& $Compiler -std=c++17 -Wall -Wextra -Werror -static tests/folder_style_test.cpp -o "$out/folder_style_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Folder style test compilation failed.' }
& "$out/folder_style_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Folder style tests failed.' }
