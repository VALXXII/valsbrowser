param([string]$Runtime = '')
# Vault E2E on a SYNTHETIC profile (test host only: vault active + fixture pin; never dev/release).
# Fictitious account « e2e-synth-user » / « E2e-Synth-0000! »; no personal secret is read, written or shown.
# Proves: capture on submit -> B04 armed (an immediate click does nothing) -> explicit « Enregistrer » ->
# v2 file without plaintext; the native B03 key control appears, ignores an un-armed activation, then
# fills the focused form after an explicit account choice; a private window can fill (read-only) but
# captures nothing and writes nothing. Only processes whose image is this fixture's host are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'job_guard.ps1')   # every process started here dies with this script, even interrupted
$root = (Resolve-Path '.').Path; $hostDir = Join-Path $root 'build\e2e-vault-host'; $pki = Join-Path $root 'build\e2e-vault\pki'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$user = 'e2e-synth-user'; $secret = 'E2e-Synth-0000!'; $port = 18447
# fresh fixture
foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq (Join-Path $hostDir 'browser.exe') })) { Stop-Process -Id $p.ProcessId -Force }
Remove-Item $hostDir, (Split-Path $pki) -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $hostDir, $pki | Out-Null
& (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' tests/e2e/vault_host.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$hostDir\browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
if ($LASTEXITCODE -ne 0) { throw 'vault test host build failed' }
Copy-Item (Join-Path $root 'deps\webview2\build\native\x64\WebView2Loader.dll') $hostDir; Copy-Item (Join-Path $root 'assets') "$hostDir\assets" -Recurse
# Review R-2: two profiles in the synthetic root (Personnel = default, Travail), each with its own vault.
[IO.File]::WriteAllText((Join-Path $hostDir 'profiles.txt'), "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`n")
$travailDir = Join-Path $hostDir 'profiles\ptravail01'; $travailLog = Join-Path $travailDir 'browser.log'; $travailVault = Join-Path $travailDir 'passwords'
$tUser = 'e2e-synth-travail'; $tSecret = 'E2e-Travail-7777!'
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
# Review V-4: a login counts only with a real user action and a password the person typed: typing and clicks go through
# the DevTools Input domain (trusted input, like a person), never through values set by script.
function TypeInto($ws, [string]$id, [string]$text) { [void](Eval $ws "document.getElementById('$id').focus();document.getElementById('$id').value='';1"); [void](Send $ws 'Input.insertText' @{ text = $text }) }
function PageClick($ws, [string]$id) {
  $r = Eval $ws "(function(){var r=document.getElementById('$id').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"
  $x, $y = ($r -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) } }
function TypedLogin($ws, [string]$u, [string]$p) { TypeInto $ws 'u' $u; TypeInto $ws 'p' $p; PageClick $ws 'go' }
function LogCount([string]$pattern, [string]$file = $log) { $l = Get-Content $file -Raw -ErrorAction SilentlyContinue; if (!$l) { return 0 }; return ([regex]::Matches($l, $pattern)).Count }
# Review R-13: a host's engine may outlive the force-killed host for seconds and keep the debugging port.
function WaitEngineExit { for ($i = 0; $i -lt 60; $i++) { $left = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$hostDir*" }); if (!$left.Count) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
function WaitLog([string]$pattern, [int]$ms = 15000, [string]$file = $log) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $file -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
$server = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port", '--no-client-auth' -PassThru -WindowStyle Hidden
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9401'
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Process -FilePath $exe -WorkingDirectory $hostDir; $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = Pids '' | Select-Object -First 1 }
  Check (WaitLog 'E2E BOUND certificate decisions cleared') 'test host ready (decisions cleared)'
  $ws = Page; [void](Send $ws 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); Start-Sleep 3; $ws.Dispose(); $ws = Page
  TypedLogin $ws $user $secret
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
  # Review 885d322 #1: the browser keeps no copy of a submitted password in web storage, whatever happens next.
  function WebCopy([string]$s) { $w = Page; $v = Eval $w "(function(){try{return JSON.stringify(sessionStorage)+JSON.stringify(localStorage)}catch(e){return ''}})()"; $w.Dispose(); return ([string]$v).Contains($s) }
  function Submit([string]$path, [string]$u, [string]$p) { $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port$path" }); Start-Sleep 3; $w.Dispose(); $w = Page
    TypedLogin $w $u $p; $w.Dispose() }
  Check (-not (WebCopy $secret)) 'saved: no copy of the password in the landing page storage'
  $hash0 = (Get-FileHash $vaultFile).Hash
  $s2 = 'E2e-Refus-1111!'; Submit '/login' 'e2e-synth-refus' $s2
  $later = WaitFind $hp 'PasswordLater' ''
  Check ([bool]$later) 'refusal: the save offer appears'
  if ($later) { Start-Sleep -Milliseconds 700; [void](Invoke-El (Find $hp 'PasswordLater' '')); Start-Sleep 1 }
  Check (-not (WebCopy $s2) -and (Get-FileHash $vaultFile).Hash -eq $hash0) 'refusal: nothing written, no copy in web storage'
  $s3 = 'E2e-Stay-2222!'; Submit '/login?stay' 'e2e-synth-stay' $s3
  Check ([bool](WaitFind $hp 'PasswordSaveConfirm' '')) 'no navigation: the offer appears (one direct message)'
  Check (-not (WebCopy $s3)) 'no navigation: no copy in web storage'
  # Review N13: one same-origin navigation may be the submission; the next one drops the offer.
  $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port/login?n=1" }); $w.Dispose(); Start-Sleep 2
  $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port/login?n=2" }); $w.Dispose()
  Check ((WaitLog 'VAULT capture dropped: the tab navigated elsewhere') -and -not (Find $hp 'PasswordSaveConfirm' '')) 'no navigation: a second navigation drops the offer and its secret'
  $s4 = 'E2e-Away-3333!'; Submit '/login?away' 'e2e-synth-away' $s4
  Check (WaitLog 'E2E BOUND refused navigation https://localhost') 'cross-origin submit: the navigation fails (refused by the test host)'
  Start-Sleep 2; Check (-not (Find $hp 'PasswordSaveConfirm' '') -and -not (Find $hp 'PasswordLater' '')) 'cross-origin submit: no offer (the submission targets another origin; SSO side effect, fail safe)'
  Check (-not (WebCopy $s4)) 'cross-origin / failed navigation: no copy in web storage'
  $l = Find $hp 'PasswordLater' ''; if ($l) { Start-Sleep -Milliseconds 700; [void](Invoke-El $l); Start-Sleep 1 }
  # Review N13: submit, then an immediate navigation to another origin: no trusted prompt above another page.
  $s6 = 'E2e-Leave-5555!'; $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); Start-Sleep 3; $w.Dispose(); $w = Page
  [void](Eval $w "document.getElementById('go').addEventListener('click',function(){setTimeout(function(){location.href='https://localhost:$port/elsewhere'},0);});1"); TypedLogin $w 'e2e-synth-leave' $s6; $w.Dispose()
  Start-Sleep 3; Check (-not (Find $hp 'PasswordSaveConfirm' '') -and -not (Find $hp 'PasswordLater' '')) 'submit then immediate navigation to another origin: no offer shown'
  Check (-not (WebCopy $s6)) 'submit then immediate navigation elsewhere: no copy in web storage'
  Check ((Get-FileHash $vaultFile).Hash -eq $hash0) 'nothing written without « Enregistrer »'
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
    if ($row) { Start-Sleep -Milliseconds 700; [void](Invoke-El $row)
      $filled = $false; for ($i = 0; $i -lt 15 -and !$filled; $i++) { Start-Sleep -Milliseconds 200; $filled = (Eval $ws "document.getElementById('p').value") -eq $secret -and (Eval $ws "document.getElementById('u').value") -eq $user }
      Check ($filled -and (WaitLog 'VAULT filled \(user action, same document, confirmed by the page\)' 3000)) 'B03: the focused form is filled after the explicit choice (confirmed by the page, waited on the DOM up to 3 s)'
      if (!$filled) { Get-Content $log | Where-Object { $_ -match 'VAULT|PROMPT' } | Select-Object -Last 12 | ForEach-Object { "     log: $_" } } }
  }
  # Review R-1: an OPAQUE-origin document (CSP sandbox) served at the account's URL never receives the secret,
  # even when its own script replaces window.self so that self.origin reads as the URL's origin. The host
  # judges the key by URL, so the key and the row may show; the fill itself must be refused (outcome « origin »).
  function TryFill([string]$path) {   # navigate, focus the password field, key -> row; returns the page's password value
    $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port$path" }); $w.Dispose(); Start-Sleep 3; $w = Page
    [void](Eval $w "document.getElementById('p').focus();1")
    $k = WaitFind $hp 'VaultFill' '' 6000; $rw = $null
    if ($k) { Start-Sleep -Milliseconds 700; [void](Invoke-El (Find $hp 'VaultFill' '')); $rw = WaitFind $hp '' $user 5000; if ($rw) { Start-Sleep -Milliseconds 700; [void](Invoke-El $rw); Start-Sleep 2 } }
    $v = Eval $w "document.getElementById('p').value"; $o = Eval $w "(function(){try{return String(self.origin)+'|'+String(window.origin)+'|'+location.origin}catch(e){return 'err'}})()"; $w.Dispose()
    "     [$path] key shown: $([bool]$k), row shown: $([bool]$rw), page self.origin|window.origin|location.origin = $o, password field: $(if ($v -eq $secret) { 'THE SECRET' } elseif ($v) { 'other value' } else { 'empty' })" | Write-Host
    return @{ value = $v; picked = [bool]$rw } }
  $refused0 = LogCount 'VAULT fill not done: outcome=1 '; $filled0 = LogCount 'VAULT filled \(user action'
  $sbx = TryFill '/sbx'
  Check ($sbx.picked -and $sbx.value -eq '' -and (LogCount 'VAULT fill not done: outcome=1 ') -gt $refused0) 'R-1 control: opaque-origin (CSP sandbox) document at the account URL: account picked, field stays empty, host logs the refusal (origin)'
  $refused1 = LogCount 'VAULT fill not done: outcome=1 '
  $spoof = TryFill '/sbx?spoof'
  Check ($spoof.picked -and $spoof.value -eq '' -and (LogCount 'VAULT fill not done: outcome=1 ') -gt $refused1 -and (LogCount 'VAULT filled \(user action') -eq $filled0) 'R-1: opaque-origin document that replaces window.self: account picked, NO secret (field empty), host logs the refusal (origin)'
  # The same same-origin page still fills after the refusals.
  $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); $w.Dispose(); Start-Sleep 3; $w = Page; [void](Eval $w "document.getElementById('p').focus();1")
  $k = WaitFind $hp 'VaultFill' '' 6000; if ($k) { Start-Sleep -Milliseconds 700; [void](Invoke-El (Find $hp 'VaultFill' '')); $rw = WaitFind $hp '' $user 5000; if ($rw) { Start-Sleep -Milliseconds 700; [void](Invoke-El $rw) } }
  $ok = $false; for ($i = 0; $i -lt 15 -and !$ok; $i++) { Start-Sleep -Milliseconds 200; $ok = (Eval $w "document.getElementById('p').value") -eq $secret }; $w.Dispose()
  Check ($ok -and (LogCount 'VAULT filled \(user action') -gt $filled0) 'R-1: a normal same-origin page still fills after the refusals'
  # Crash with an offer open: the secret lived only in the killed process.
  $s5 = 'E2e-Crash-4444!'; Submit '/login' 'e2e-synth-crash' $s5
  Check ([bool](WaitFind $hp 'PasswordSaveConfirm' '')) 'crash: the offer is open when the host is killed'
  $ws.Dispose(); foreach ($p in Pids '') { Stop-Process -Id $p -Force }
  Check (WaitEngineExit) 'crash: the killed host''s engine has exited before the next window starts (review R-13)'
  Check ((Get-FileHash $vaultFile).Hash -eq $hash0) 'crash: nothing written'
  # Review R-2: a Travail window saves its own account into the Travail vault (profiles\ptravail01\passwords).
  Start-Process -FilePath $exe -ArgumentList '--profile ptravail01' -WorkingDirectory $hostDir; $tp = 0; for ($i = 0; $i -lt 40 -and !$tp; $i++) { Start-Sleep -Milliseconds 250; $tp = Pids '--profile ptravail01' | Select-Object -First 1 }
  Check (WaitLog 'E2E BOUND certificate decisions cleared' 20000 $travailLog) 'R-2 setup: a Travail window is ready (its own profile folder and log)'
  Submit '/login' $tUser $tSecret
  $ts = WaitFind $tp 'PasswordSaveConfirm' ''; if ($ts) { Start-Sleep -Milliseconds 700; $ts = Find $tp 'PasswordSaveConfirm' ''; if ($ts) { [void](Invoke-El $ts) }; Start-Sleep 1 }
  $tb = if (Test-Path $travailVault) { [IO.File]::ReadAllText($travailVault) } else { '' }
  Check ($tb.StartsWith("browser-passwords 2`n") -and $tb -match "`nend`t1`t[0-9a-f]{64}`n$" -and (Get-FileHash $vaultFile).Hash -eq $hash0) 'R-2 setup: the Travail account is saved in the Travail vault only (Personnel vault unchanged)'
  $tHash = if (Test-Path $travailVault) { (Get-FileHash $travailVault).Hash } else { '' }
  # The product's own « Navigation privée » command (the Ctrl+Maj+N path, OpenPrivateWindow) from the Travail window.
  Add-Type -Namespace W -Name P -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);' -ErrorAction SilentlyContinue
  $known = @(Pids '--private')
  [void][W.P]::PostMessage((Get-Process -Id $tp).MainWindowHandle, 0x8001, [IntPtr]0x50, [IntPtr]::Zero)   # WM_APP+1 (Shortcut) with 'P' = OpenPrivateWindow
  $child = $null; for ($i = 0; $i -lt 40 -and !$child; $i++) { Start-Sleep -Milliseconds 250; $child = Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe -and $_.CommandLine -like '*--private*' -and $known -notcontains $_.ProcessId } | Select-Object -First 1 }
  "     private window launched from Travail: $(if ($child) { $child.CommandLine -replace '^.*browser\.exe"?', 'browser.exe' } else { 'none' })"
  Check ($child -and $child.CommandLine -match '--private' -and $child.CommandLine -match '--profile ptravail01( |$)') 'R-2: « Navigation privée » from a Travail window opens a private window bound to Travail (--profile passed)'
  foreach ($p in Pids '') { Stop-Process -Id $p -Force }; [void](WaitEngineExit)
  # Review R-13: a fresh private window lists more than one page target and their order changes, so « the newest
  # target » may be another one than the tab that was navigated. Its page is then found by its URL.
  function PageAt([string]$url) { $t = $null; for ($i = 0; $i -lt 40 -and !$t; $i++) { try { $t = (Invoke-RestMethod 'http://127.0.0.1:9401/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' -and $_.url -eq $url } | Select-Object -First 1 } catch {}; if (!$t) { Start-Sleep -Milliseconds 300 } }
    if (!$t) { return $null }; $w = New-Object System.Net.WebSockets.ClientWebSocket; $w.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $w }
  # The new window first loads its own home page (data:); a navigation sent before that is replaced by it.
  function PrivateLogin {   # a fresh private window: wait for its home page, open /login, wait until it is really there
    for ($i = 0; $i -lt 50; $i++) { $homePage = $null; try { $homePage = (Invoke-RestMethod 'http://127.0.0.1:9401/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' -and $_.url -like 'data:*' } | Select-Object -First 1 } catch {}; if ($homePage) { break }; Start-Sleep -Milliseconds 300 }
    for ($try = 0; $try -lt 2; $try++) {
      $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port/login" }); $w.Dispose()
      $w = PageAt "https://127.0.0.1:$port/login"
      for ($i = 0; $w -and $i -lt 20; $i++) { if ((Eval $w "document.getElementById('p')?1:0") -eq 1) { return $w }; Start-Sleep -Milliseconds 300 }
      if ($w) { $w.Dispose() } }
    "     private window: no /login page target" | Write-Host; return (Page) }
  function OpenRows([int]$procId, [string]$name) {   # key -> B03 list (one more try if the first activation came before the arming)
    for ($a = 0; $a -lt 2; $a++) { $k = Find $procId 'VaultFill' ''; if (!$k) { return $null }; Start-Sleep -Milliseconds 700; [void](Invoke-El $k); $r = WaitFind $procId '' $name 4000; if ($r) { return $r } }
    return $null }
  # Private window (review R-13: started only once every engine of the fixture has exited): read-only fill, no capture, no write.
  $before = (Get-FileHash $vaultFile).Hash
  Start-Process -FilePath $exe -ArgumentList '--private' -WorkingDirectory $hostDir; $pp = 0; for ($i = 0; $i -lt 40 -and !$pp; $i++) { Start-Sleep -Milliseconds 250; $pp = Pids '--private' | Select-Object -First 1 }
  $ws = PrivateLogin
  [void](Eval $ws "document.getElementById('p').focus();1")
  $pkey = WaitFind $pp 'VaultFill' ''; $prow = $null; $otherRow = $false
  if ($pkey) { $prow = OpenRows $pp $user; $otherRow = [bool](Find $pp '' $tUser); if ($prow) { Start-Sleep -Milliseconds 700; [void](Invoke-El $prow); Start-Sleep 1 } }
  Check ([bool]$pkey -and (Eval $ws "document.getElementById('p').value") -eq $secret) 'private window: explicit read-only fill works'
  Check ([bool]$prow -and -not $otherRow) 'R-2: a private window opened from Personnel (no --profile) lists Personnel''s account only, not Travail''s'
  TypedLogin $ws 'autre' 'Autre-0000!'; Start-Sleep 2
  Check (-not (Find $pp 'PasswordSaveConfirm' '') -and (Get-FileHash $vaultFile).Hash -eq $before) 'private window: no capture prompt and the vault file is unchanged'
  $ws.Dispose()
  # Review R-2: a private window opened from Travail reads (never writes) the TRAVAIL vault.
  foreach ($p in Pids '') { Stop-Process -Id $p -Force }; [void](WaitEngineExit)
  Start-Process -FilePath $exe -ArgumentList '--private --profile ptravail01' -WorkingDirectory $hostDir; $pt = 0; for ($i = 0; $i -lt 40 -and !$pt; $i++) { Start-Sleep -Milliseconds 250; $pt = Pids '--private --profile ptravail01' | Select-Object -First 1 }
  $ws = PrivateLogin
  [void](Eval $ws "document.getElementById('p').focus();1")
  $tkey = WaitFind $pt 'VaultFill' ''; $trow = $null; $drow = $false
  if ($tkey) { $trow = OpenRows $pt $tUser; $drow = [bool](Find $pt '' $user); if ($trow) { Start-Sleep -Milliseconds 700; [void](Invoke-El $trow); Start-Sleep 1 } }
  $tv = Eval $ws "document.getElementById('p').value"
  "     private window from Travail: key $([bool]$tkey), Travail row $([bool]$trow), Personnel row $drow, field: $(if ($tv -eq $tSecret) { 'Travail secret' } elseif ($tv -eq $secret) { 'PERSONNEL secret' } elseif ($tv) { 'other' } else { 'empty' })"
  Check ([bool]$tkey -and [bool]$trow -and -not $drow -and $tv -eq $tSecret) 'R-2: a private window opened from Travail offers and fills the Travail account only (not Personnel)'
  TypedLogin $ws 'autre' 'Autre-0000!'; Start-Sleep 2
  Check (-not (Find $pt 'PasswordSaveConfirm' '') -and (Get-FileHash $vaultFile).Hash -eq $before -and $tHash -and (Get-FileHash $travailVault).Hash -eq $tHash) 'R-2: that private window captures nothing and writes neither vault'
  $ws.Dispose()
  # Whole fixture profile (engine user data, log, vault): no synthetic secret in clear, UTF-8 or UTF-16, once every process is gone.
  foreach ($p in Pids '') { Stop-Process -Id $p -Force }
  for ($i = 0; $i -lt 60; $i++) { $left = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$hostDir*" }); if (!$left.Count) { break }; Start-Sleep -Milliseconds 500 }
  Check (-not $left.Count) 'profile scan: every engine process of the fixture has exited'
  $scan = Join-Path (Split-Path $pki) 'scan.py'
  [IO.File]::WriteAllText($scan, "import os,sys`nroot=sys.argv[1];n=[]`nfor s in sys.argv[2:]: n+=[s.encode('utf-8'),s.encode('utf-16-le')]`nhits=[];bad=0;count=0`nfor d,_,fs in os.walk(root):`n  for f in fs:`n    p=os.path.join(d,f);count+=1`n    try: b=open(p,'rb').read()`n    except Exception: bad+=1;continue`n    if any(x in b for x in n): hits.append(os.path.relpath(p,root))`nprint('files=%d unreadable=%d hits=%s'%(count,bad,';'.join(hits)))`n")
  $ctl = 'E2e-Control-9999!'; [IO.File]::WriteAllText((Join-Path $hostDir 'sessions\scan-control.bin'), $ctl, [Text.Encoding]::Unicode)   # positive control (UTF-16)
  $r = & python -I $scan $hostDir $secret $s2 $s3 $s4 $s5 $s6 $tSecret $ctl
  "     $r"
  Check ($r -match 'unreadable=0 hits=sessions.scan-control\.bin$') 'profile scan: no synthetic password in clear anywhere in the fixture profile (all files read; the planted control is found)'
} finally {
  foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe })) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item (Split-Path $pki) -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL vault e2e: $script:fails"; exit 1 } else { 'PASS vault e2e (synthetic profile, test host only; foreground condition lifted in the test host only, 500 ms arming kept; immediate click on the control not proven E2E, arming proven in U): armed save, v2 without plaintext, no web-storage copy after save/refusal/no navigation/failed cross-origin navigation/crash, armed native fill after explicit choice, opaque-origin documents refused even when they fake self.origin (R-1), private windows read their opener profile''s vault only (R-2), private read-only fill without capture or write, profile scan clean' }
