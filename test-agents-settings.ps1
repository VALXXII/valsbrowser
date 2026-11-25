param([string]$DependencyRoot = $PSScriptRoot)
# « Ton agent » — Réglages › Agents and the annotation band's reminder (worker C), headless:
#  1. tests/agents_settings_test.cpp: the pure model (every chat agent state), the mapping from the agents' one status
#     (src/agent-status.h), the band's reminder rule, the real drawer on an owner far off screen (rows per status, the
#     button's click only, « Connecter un agent externe », hit testing, Tab order, UIA, search), PNG captures (96 / 144
#     dpi) in build/ton-agent-qa/settings — never a real install, sign-in or detection: the statuses are the test's;
#  2. tests/annotations/ton_agent_band_contract_test.py: « Connecter un agent » never opens by itself, the reminder is
#     the chat's and opens « Ton agent », the settings' install takes its consent from the click only.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/ton-agent-qa/settings'
New-Item -ItemType Directory -Force $out | Out-Null
$gxx = "$DependencyRoot/tools/w64devkit/bin/g++.exe"
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-DTVG_STATIC', "-I$DependencyRoot/deps/thorvg-1.1.3/inc", "-I$DependencyRoot/deps/webview2/build/native/include")
$libs = @('-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt')
$ErrorActionPreference = 'Continue'
$log = & $gxx @flags -static -municode tests/agents_settings_test.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$out/agents_settings_test.exe" @libs 2>&1 | Out-String
$built = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($built -ne 0 -or $log -match 'warning') { Write-Host $log; throw 'agents_settings_test compilation failed (or warned).' }
$root = (Resolve-Path .).Path
& "./$out/agents_settings_test.exe" $root (Resolve-Path $out).Path
if ($LASTEXITCODE -ne 0) { throw 'agents_settings_test failed.' }
python tests/annotations/ton_agent_band_contract_test.py
if ($LASTEXITCODE -ne 0) { throw 'ton_agent_band_contract_test failed.' }
Write-Host 'Réglages › Agents and the band reminder: ok'
