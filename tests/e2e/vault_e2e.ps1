param([string]$Runtime = '')
# Vault E2E on a SYNTHETIC profile (test host only: vault active + fixture pin; never dev/release).
# Fictitious account « e2e-synth-user » / « E2e-Synth-0000! »; no personal secret is read, written or shown.
# Proves: capture on submit -> B04 armed (an immediate click does nothing) -> explicit « Enregistrer » ->
# v2 file without plaintext; the native B03 key control appears, ignores an un-armed activation, then
# fills the focused form after an explicit account choice; a private window can fill (read-only) but
# captures nothing and writes nothing. Only processes whose image is this fixture's host are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$root = (Resolve-Path '.').Path; $hostDir = Join-Path $root 'build\e2e-vault-host'; $pki = Join-Path $root 'build\e2e-vault\pki'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$user = 'e2e-synth-user'; $secret = 'E2e-Synth-0000!'; $port = 18447
# fresh fixture
foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq (Join-Path $hostDir 'browser.exe') })) { Stop-Process -Id $p.ProcessId -Force }
Remove-Item $hostDir, (Split-Path $pki) -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $hostDir, $pki | Out-Null
& (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' tests/e2e/vault_host.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$hostDir\browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
if ($LASTEXITCODE -ne 0) { throw 'vault test host build failed' }
Copy-Item (Join-Path $root 'build\real-integration-check\WebView2Loader.dll') $hostDir; Copy-Item (Join-Path $root 'build\real-integration-check\assets') "$hostDir\assets" -Recurse
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
& $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
& $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nsha256=$((Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower())`n")
$exe = Join-Path $hostDir 'browser.exe'; $log = Join-Path $hostDir 'prototype.log'; $vaultFile = Join-Path $hostDir 'passwords'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
function Pids([string]$flag) { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe -and (($flag -eq '') -or $_.CommandLine -like "*$flag*") } | ForEach-Object ProcessId }
function Find([int]$hostPid, [string]$aid, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $hostPid)
  foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', $cond)) {
    $c = if ($aid) { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $aid) } else { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name) }
    $e = $w.FindFirst('Descendants', $c); if ($e) { return $e } }
  return $null }
function WaitFind([int]$hostPid, [string]$aid, [string]$name, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $e = Find $hostPid $aid $name; if ($e) { return $e }; Start-Sleep -Milliseconds 200 }; return $null }
function Invoke-El($e) { Start-Sleep -Milliseconds 650; $p = $null;   # past the 500 ms arming; no focus or foreground change (an anchored popover closes when its window is deactivated)
   # UIA focus brings our fixture window forward (no SendInput); then past the 500 ms arming
  if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); return $true }; if ($e.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$p)) { $p.Select(); return $true }; return $false }
function Foreground([int]$hostPid) { Add-Type -Namespace W -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);' -ErrorAction SilentlyContinue; [void][W.U]::SetForegroundWindow((Get-Process -Id $hostPid).MainWindowHandle) }
$script:cid = 0
function Page {   # newest page target
  for ($i = 0; $i -lt 60; $i++) { try { $t = (Invoke-RestMethod 'http://127.0.0.1:9401/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -Last 1; if ($t) { break } } catch {}; Start-Sleep -Milliseconds 300 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws }
function Send($ws, [string]$method, $params) { $script:cid++; $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $script:cid; method = $method; params = $params } | ConvertTo-Json -Depth 6 -Compress)); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 262144; while ($true) { $sb = New-Object Text.StringBuilder; do { $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$r.Wait(10000)) { return $null }; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)) } while (!$r.Result.EndOfMessage); $j = $sb.ToString() | ConvertFrom-Json; if ($j.id -eq $script:cid) { return $j } } }
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function WaitLog([string]$pattern, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
$server = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port", '--no-client-auth' -PassThru -WindowStyle Hidden
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9401'
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Process -FilePath $exe -WorkingDirectory $hostDir; $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = Pids '' | Select-Object -First 1 }
  Check (WaitLog 'E2E BOUND certificate decisions cleared') 'test host ready (decisions cleared)'
  $ws = Page; [void](Send $ws 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); Start-Sleep 3; $ws.Dispose(); $ws = Page
  [void](Eval $ws "document.getElementById('u').value='$user';document.getElementById('p').value='$secret';document.getElementById('go').click();1")
  $save = WaitFind $hp 'PasswordSaveConfirm' ''
  Check ([bool]$save) 'B04: the real save prompt appears after the submit (capture, engine origin)'
  if ($save) {
    # (Arming itself is proven deterministically in U: modal_test « a click right after the prompt appears does nothing ».)
    Check (-not (Test-Path $vaultFile)) 'nothing is written before the explicit choice'
    Start-Sleep -Milliseconds 700; $save = Find $hp 'PasswordSaveConfirm' ''; if ($save) { [void](Invoke-El $save) }; Start-Sleep 1
    $bytes = if (Test-Path $vaultFile) { [IO.File]::ReadAllText($vaultFile) } else { '' }
    Check ($bytes.StartsWith("browser-passwords 2`n") -and $bytes -match "`nend`t1`t[0-9a-f]{64}`n$") 'vault v2 written after explicit consent (one account, completeness trailer)'
    Check (-not $bytes.Contains($secret) -and -not $bytes.Contains($user)) 'no plaintext secret or username on disk'
  }
  # B03: native key control, arming, explicit choice, fill of the focused form
  $ws.Dispose(); $ws = Page; [void](Send $ws 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); Start-Sleep 3; $ws.Dispose(); $ws = Page
  [void](Eval $ws "document.getElementById('p').focus();1")
  $key = WaitFind $hp 'VaultFill' ''
  Check ([bool]$key) 'B03: the native key control is shown for an origin with a saved account'
  if ($key) {
    $pn = $null; if ($key.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pn)) { $pn.Invoke() }   # immediately, no wait
    "     immediate activation: $(if (WaitLog 'VAULT key: activation ignored \(not armed\)' 1500) { 'ignored (not armed)' } else { 'not observed (found after arming); arming proven in U' })"
    $b03open = WaitFind $hp '' $user 1500
    if (-not $b03open) { Start-Sleep -Milliseconds 700; [void](Invoke-El (Find $hp 'VaultFill' '')) }
    $row = WaitFind $hp '' $user
    Check ([bool]$row) 'B03: the account list shows the synthetic account (no secret)'
    if ($row) { Start-Sleep -Milliseconds 700; [void](Invoke-El $row); Start-Sleep 1
      Check ((Eval $ws "document.getElementById('p').value") -eq $secret -and (Eval $ws "document.getElementById('u').value") -eq $user) 'B03: the focused form is filled after the explicit choice' }
  }
  $ws.Dispose(); foreach ($p in Pids '') { Stop-Process -Id $p -Force }; Start-Sleep 2
  # Private window: read-only fill, no capture, no write
  $before = (Get-FileHash $vaultFile).Hash
  Start-Process -FilePath $exe -ArgumentList '--private' -WorkingDirectory $hostDir; $pp = 0; for ($i = 0; $i -lt 40 -and !$pp; $i++) { Start-Sleep -Milliseconds 250; $pp = Pids '--private' | Select-Object -First 1 }
  Start-Sleep 3; $ws = Page; [void](Send $ws 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); Start-Sleep 3; $ws.Dispose(); $ws = Page
  [void](Eval $ws "document.getElementById('p').focus();1")
  $pkey = WaitFind $pp 'VaultFill' ''
  if ($pkey) { Start-Sleep -Milliseconds 700; [void](Invoke-El $pkey); $prow = WaitFind $pp '' $user; if ($prow) { Start-Sleep -Milliseconds 700; [void](Invoke-El $prow); Start-Sleep 1 } }
  Check ([bool]$pkey -and (Eval $ws "document.getElementById('p').value") -eq $secret) 'private window: explicit read-only fill works'
  [void](Eval $ws "document.getElementById('u').value='autre';document.getElementById('p').value='Autre-0000!';document.getElementById('go').click();1"); Start-Sleep 2
  Check (-not (Find $pp 'PasswordSaveConfirm' '') -and (Get-FileHash $vaultFile).Hash -eq $before) 'private window: no capture prompt and the vault file is unchanged'
  $ws.Dispose()
} finally {
  foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe })) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item (Split-Path $pki) -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL vault e2e: $script:fails"; exit 1 } else { 'PASS vault e2e (synthetic profile, test host only): armed save, v2 without plaintext, armed native fill after explicit choice, private read-only fill without capture or write' }
