# Lot B (missing elements): error pages, sandbox bridge and native B components.
# Builds into build/sandbox and runs both tests. No WebView2, no browser, no build/v2.
param([string]$DependencyRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
# g++ writes diagnostics on stderr: collect them as text (checked below), never as terminating errors.
function Gpp { $ErrorActionPreference = 'Continue'; & $gpp @args 2>&1 | ForEach-Object { "$_" } }
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/sandbox | Out-Null
$gpp = "$DependencyRoot/tools/w64devkit/bin/g++.exe"
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-static', '-DTVG_STATIC', "-I$DependencyRoot/deps/thorvg-1.1.3/inc", "-I$DependencyRoot/deps/webview2/build/native/include")
$libs = @('-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32')

# Every B header compiles alone and together with chrome.h, warning-free.
foreach ($h in 'tooltip-placement', 'tooltip', 'find-bar', 'context-toast', 'fullscreen-hint', 'error-pages', 'debug-sandbox', 'shortcuts-panel') {
    Set-Content -Encoding ascii "build/sandbox/inc_$h.cpp" "#include `"../../src/$h.h`"`nint main(){return 0;}"
    $out = Gpp @flags -fsyntax-only "build/sandbox/inc_$h.cpp"
    if ($LASTEXITCODE -ne 0 -or ($out | Select-String 'warning')) { $out; throw "$h.h does not compile cleanly alone" }
}
Set-Content -Encoding ascii build/sandbox/inc_all.cpp ('#include "../../src/chrome.h"', '#include "../../src/find-bar.h"', '#include "../../src/error-pages.h"', '#include "../../src/context-toast.h"', '#include "../../src/tooltip.h"', '#include "../../src/fullscreen-hint.h"', '#include "../../src/debug-sandbox.h"', '#include "../../src/shortcuts-panel.h"', 'int main(){return 0;}')
$out = Gpp @flags -fsyntax-only build/sandbox/inc_all.cpp
if ($LASTEXITCODE -ne 0 -or ($out | Select-String 'warning')) { $out; throw 'B headers do not compile together with chrome.h' }

$out = Gpp @flags tests/tooltip_placement_test.cpp -o build/sandbox/tooltip_placement_test.exe @libs
if ($LASTEXITCODE -ne 0 -or ($out | Select-String 'warning')) { $out; throw 'Tooltip placement test compilation failed or warned.' }
& ./build/sandbox/tooltip_placement_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tooltip placement tests failed.' }

$out = Gpp @flags tests/error_pages_test.cpp -o build/sandbox/error_pages_test.exe @libs
if ($LASTEXITCODE -ne 0 -or ($out | Select-String 'warning')) { $out; throw 'Error page test compilation failed or warned.' }
& ./build/sandbox/error_pages_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Error page tests failed.' }

$out = Gpp @flags tests/debug_sandbox_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o build/sandbox/debug_sandbox_test.exe @libs
if ($LASTEXITCODE -ne 0) { $out; throw 'Sandbox test compilation failed.' }
if ($out | Select-String 'warning') { $out; throw 'Sandbox test compiled with warnings.' }
& ./build/sandbox/debug_sandbox_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Sandbox / component tests failed.' }
