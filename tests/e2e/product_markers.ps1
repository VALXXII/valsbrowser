param([string]$Product = 'build/v2', [string]$Runtime = '')
# P-I1: the PRODUCT build (tools/release/build.py flags; the release channel differs only by the channel
# string of the build info and the app icon resource, and is refused off main by design) carries no test
# host code: no fixture pin, no vault test host code, no lifted foreground condition, no forced-inactive vault. The test
# hosts do. A fresh copy of the product is started on an empty fixture profile and must report « VAULT state: active »
# (activated after the independent review); the engine-control host (mtls_host) is forced inactive.
# Only processes whose image is this fixture copy are stopped.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$root = (Resolve-Path '.').Path; $script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
function Has([string]$f, [string]$m) { $b = [IO.File]::ReadAllBytes($f); $u = [Text.Encoding]::Unicode; return ($u.GetString($b).Contains($m) -or $u.GetString($b, 1, $b.Length - 1).Contains($m)) }
$prod = Join-Path $root "$Product\browser.exe"; $vh = Join-Path $root 'build\e2e-vault-host\browser.exe'; $mh = Join-Path $root 'build\e2e-mtls-host\browser.exe'
"product: $prod ($((Get-Item $prod).LastWriteTime.ToString('s')))"
Check (-not (Has $prod 'E2E FIXTURE PIN') -and -not (Has $prod 'E2E VAULT HOST') -and -not (Has $prod 'E2E VAULT ACTIVE') -and -not (Has $prod 'BROWSER_E2E_PIN_MANIFEST') -and -not (Has $prod 'E2E VAULT FORCED INACTIVE')) 'product binary: no fixture pin, no vault test host code, no forced-active or forced-inactive test marker'
$ph = Join-Path $root 'build\e2e-vault-product\browser.exe'
if (Test-Path $ph) { Check ((Has $ph 'E2E VAULT ACTIVE') -and -not (Has $ph 'E2E VAULT HOST')) 'vault product-path host binary: forced-active marker present, no lifted foreground condition' }
Check ((Has $vh 'E2E FIXTURE PIN') -and (Has $vh 'E2E VAULT HOST')) 'vault test host binary: pin and vault host markers present (the check can see them)'
Check ((Has $mh 'E2E FIXTURE PIN') -and -not (Has $mh 'E2E VAULT HOST') -and (Has $mh 'E2E VAULT FORCED INACTIVE')) 'mTLS / engine-control test host binary: pin, vault forced inactive there'
$fix = Join-Path $root 'build\e2e-product-check'; Remove-Item $fix -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $fix | Out-Null
Copy-Item -Recurse -Force (Join-Path $root "$Product\*") $fix
Get-ChildItem $fix -Include 'profile', 'sessions', '*.log', 'session', 'settings', 'passwords' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$exe = Join-Path $fix 'browser.exe'; $log = Join-Path $fix 'prototype.log'
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Process -FilePath $exe -ArgumentList '--smoke-test' -WorkingDirectory $fix   # --smoke-test: never hands over to an installed copy (the personal browser is never involved)
  $ok = $false; for ($i = 0; $i -lt 60 -and !$ok; $i++) { Start-Sleep -Milliseconds 250; $ok = (Get-Content $log -Raw -ErrorAction SilentlyContinue) -match 'VAULT state: (in)?active' }
  $l = Get-Content $log -Raw -ErrorAction SilentlyContinue
  Check ($l -match 'VAULT state: active' -and $l -notmatch 'VAULT state: inactive' -and $l -notmatch 'E2E VAULT' -and $l -notmatch 'E2E BOUND') 'product run: « VAULT state: active », no test host path'
} finally {
  foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe })) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
  Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL product markers: $script:fails"; exit 1 } else { 'PASS product markers (P-I1): product build without test host code, vault active at run time' }
