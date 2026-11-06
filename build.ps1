param([string]$OutputDirectory = 'build\v2')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = Join-Path $PSScriptRoot 'tools\w64devkit\bin\g++.exe'
$sdk = Join-Path $PSScriptRoot 'deps\webview2\build\native'
$lottieLibrary = Join-Path $PSScriptRoot 'build\lottie-backend\src\libthorvg-1.a'
if (!(Test-Path $lottieLibrary)) {
    & 'C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe' "$PSScriptRoot/build-lottie.py"
    if ($LASTEXITCODE -ne 0) { throw 'Native ThorVG build failed.' }
}
if (!(Test-Path $compiler) -or !(Test-Path "$sdk\include\WebView2.h")) {
    throw 'Dépendances absentes : exécuter .\setup.ps1 avant la compilation.'
}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$resourceCompiler = Join-Path $PSScriptRoot 'tools\w64devkit\bin\windres.exe'
& $resourceCompiler '-I.' src/browser.rc -O coff -o "$OutputDirectory/browser-res.o"
if ($LASTEXITCODE -ne 0) { throw 'Compilation des ressources Windows echouee.' }
& $compiler -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' '-Isrc' "-I$sdk\include" src/main.cpp src/native-lottie.cpp "$OutputDirectory/browser-res.o" $lottieLibrary -o "$OutputDirectory/browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw "Compilation échouée : code $LASTEXITCODE" }
Copy-Item "$sdk\x64\WebView2Loader.dll" "$OutputDirectory/WebView2Loader.dll" -Force
Copy-Item 'deps\webview2\LICENSE.txt' "$OutputDirectory/WebView2-LICENSE.txt" -Force
Copy-Item 'deps\webview2\NOTICE.txt' "$OutputDirectory/WebView2-NOTICE.txt" -Force
Copy-Item 'third_party\thorvg\LICENSE' "$OutputDirectory/ThorVG-LICENSE.txt" -Force
Copy-Item 'third_party\thorvg\RapidJSON-LICENSE' "$OutputDirectory/RapidJSON-LICENSE.txt" -Force
New-Item -ItemType Directory -Force "$OutputDirectory/assets" | Out-Null
Copy-Item 'assets/*' "$OutputDirectory/assets" -Recurse -Force
Write-Output "Compilation réussie : $OutputDirectory\browser.exe"
