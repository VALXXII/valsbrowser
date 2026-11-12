param([string]$DependencyRoot = $PSScriptRoot, [switch]$SkipPixels)
# Specialist A: zero drop shadow + dialog family (design/missing-elements-A.md).
#  1. every new header compiles alone and together with chrome.h (-Wall -Wextra, no warning);
#  2. tests/modal_test.cpp: done-once, Échap/Entrée, validation, busy, wipe, focus, presets;
#  3. tests/popup_shadow_test.cpp: IsShadowless on every real popup window + desktop pixels
#     around them (needs an unlocked interactive desktop; -SkipPixels reports them as skipped).
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/popups'
New-Item -ItemType Directory -Force $out | Out-Null
$gxx = "$DependencyRoot/tools/w64devkit/bin/g++.exe"
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-DTVG_STATIC', "-I$DependencyRoot/deps/thorvg-1.1.3/inc", "-I$DependencyRoot/deps/webview2/build/native/include")
$libs = @('-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32', '-lmsimg32')

# 1. Header hygiene: alone, and after chrome.h. Any warning fails.
foreach ($h in 'native-ui', 'modal', 'origin-prompt', 'password-demo') {
    foreach ($withChrome in $false, $true) {
        $src = "$out/hdr_check.cpp"
        $text = ($(if ($withChrome) { "#include `"../../src/chrome.h`"`n" } else { '' })) + "#include `"../../src/$h.h`"`nint main(){return 0;}`n"
        Set-Content -Encoding utf8 $src $text
        $log = & $gxx @flags -fsyntax-only $src 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $log -match 'warning|error') { Write-Host $log; throw "Header $h.h does not compile cleanly (chrome first: $withChrome)." }
    }
}
Write-Host 'headers: native-ui.h, modal.h, origin-prompt.h, password-demo.h compile alone and with chrome.h, no warning'

# 2 + 3. Native harnesses (static, like the browser: comctl32 v5, no v6 manifest).
foreach ($t in 'modal_test', 'popup_shadow_test') {
    & $gxx @flags -static -municode "tests/$t.cpp" src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$out/$t.exe" @libs
    if ($LASTEXITCODE -ne 0) { throw "$t compilation failed." }
}
$root = (Resolve-Path .).Path
& "./$out/modal_test.exe" $root
if ($LASTEXITCODE -ne 0) { throw 'modal_test failed.' }
if ($SkipPixels) { Write-Host 'popup_shadow_test: SKIPPED (-SkipPixels): pixel evidence not verified in this run'; exit 0 }
& "./$out/popup_shadow_test.exe" $root
if ($LASTEXITCODE -ne 0) { throw 'popup_shadow_test failed (a NOT VERIFIED line means the desktop was locked or not interactive).' }
