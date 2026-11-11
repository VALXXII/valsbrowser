param([string]$Runtime = '')
# Real mTLS E2E (authorized, bounded). Stages:
#  0 recovery: a previous interrupted run's certificate (exact thumbprint + run id in its manifest) is removed first
#  1 durable ownership manifest BEFORE anything is created (run id, subject, pre-existing thumbprints)
#  2 throwaway server PKI (files) + harness pin manifest (host 127.0.0.1, port, leaf SHA-256)
#  3 ONE client certificate for this run only: CurrentUser\My, ClientAuth EKU, non-exportable key;
#    its public part (no key) is what the local server accepts, so the CertificateRequest names
#    only it and no personal identity can match
#  4 test host (tests/e2e/mtls_host.cpp, the only binary with the pin) on the Fixed Version runtime:
#    real ClientCertificateRequested -> our B08 -> select the row -> « Envoyer » -> the SERVER observes the identity
#  5 fresh profile: « Ne pas envoyer » -> the server receives no identity
#  finally: certificate AND private key removed by exact thumbprint, verified; processes and files cleaned.
# Nothing is ever trusted, no Root/CA/LocalMachine store is touched, no other certificate is read.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$root = (Resolve-Path '.').Path; $work = Join-Path $root 'build\e2e-mtls'; New-Item -ItemType Directory -Force $work | Out-Null
$manifestPath = Join-Path $work 'ownership.json'; $lockPath = Join-Path $work 'run.lock'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
function Save-Manifest($m) { ($m | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 $manifestPath }
function Remove-OwnedCert($m) {
  if (!$m -or !$m.thumbprint) { return $true }
  $c = Get-Item "Cert:\CurrentUser\My\$($m.thumbprint)" -ErrorAction SilentlyContinue
  if ($c -and $c.Subject -ne $m.subject) { "REFUSED cleanup: thumbprint $($m.thumbprint) does not carry this run's subject"; return $false }
  if ($c) { Remove-Item "Cert:\CurrentUser\My\$($m.thumbprint)" -DeleteKey }
  $gone = -not (Test-Path "Cert:\CurrentUser\My\$($m.thumbprint)")
  # The run's own CNG key file only (its unique container name, recorded in the manifest): no key
  # enumeration, no other identity is listed, read or used.
  $keyGone = $true; if ($m.keyContainer) { $keyGone = -not (Test-Path (Join-Path $env:APPDATA "Microsoft\Crypto\Keys\$($m.keyContainer)")) }
  return ($gone -and $keyGone)
}
# One run at a time for this store (local lock file, exclusive).
$lock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
try {
  # 0 recovery
  if (Test-Path $manifestPath) { $old = Get-Content $manifestPath -Raw | ConvertFrom-Json; if ($old.status -ne 'cleaned') { "recovery of run $($old.runId)"; Check (Remove-OwnedCert $old) 'recovery: the interrupted run''s certificate and key are gone'; $old.status = 'cleaned'; Save-Manifest $old } }
  # 1 manifest before creation
  $runId = [guid]::NewGuid().ToString('N').Substring(0, 12); $subject = "CN=browser-e2e-mtls-$runId"
  $m = [ordered]@{ runId = $runId; subject = $subject; created = (Get-Date).ToString('o'); status = 'planned'; thumbprint = ''; keyContainer = ''; before = @((Get-ChildItem Cert:\CurrentUser\My | ForEach-Object Thumbprint)); cleanup = 'tests/e2e/mtls_e2e.ps1 removes it at the next start (stage 0)' }
  Save-Manifest $m
  # 2 server PKI + pin
  $pki = Join-Path $work 'pki'; Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $pki | Out-Null
  $o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
  & $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
  & $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
  $ErrorActionPreference = 'Stop'
  $pin = (Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower(); $port = 18445
  [IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nsha256=$pin`n")
  # 3 client certificate (this run only)
  $m.status = 'creating'; Save-Manifest $m
  $cert = New-SelfSignedCertificate -CertStoreLocation Cert:\CurrentUser\My -Subject $subject -KeyExportPolicy NonExportable -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 -NotAfter (Get-Date).AddHours(2) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.2') -Provider 'Microsoft Software Key Storage Provider'
  $m.thumbprint = $cert.Thumbprint; $m.status = 'created'; Save-Manifest $m   # review: recorded at once, before anything else can be interrupted
  $info = & certutil.exe -user -store My $cert.Thumbprint 2>$null   # this run's certificate only (exact thumbprint)
  $kc = ($info | Select-String 'Unique container name:\s*(.+)$').Matches | Select-Object -First 1
  if ($kc) { $m.keyContainer = $kc.Groups[1].Value.Trim() }
  Check ([bool]$m.keyContainer -and (Test-Path (Join-Path $env:APPDATA "Microsoft\Crypto\Keys\$($m.keyContainer)"))) 'the run''s key file is located exactly (for its verified removal)'
  Save-Manifest $m
  $pfx = Join-Path $pki 'export-attempt.pfx'; $exportable = $true
  try { Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString 'x' -AsPlainText -Force) -ErrorAction Stop | Out-Null } catch { $exportable = $false }
  Remove-Item $pfx -ErrorAction SilentlyContinue
  Check (-not $exportable -and $cert.HasPrivateKey) "client certificate created for this run only ($subject); its private key cannot be exported"
  Export-Certificate -Cert $cert -FilePath "$pki\client.cer" -Type CERT | Out-Null
  & $o x509 -inform DER -in "$pki\client.cer" -out "$pki\ca.crt" 2>&1 | Out-Null
  # Review P-M1: the test host is (re)built here from the current sources, so the run proves this code.
  $hostDir = Join-Path $root 'build\e2e-mtls-host'; New-Item -ItemType Directory -Force $hostDir | Out-Null
  & (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' tests/e2e/mtls_host.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$hostDir\browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
  if ($LASTEXITCODE -ne 0) { throw 'mTLS test host build failed' }
  # The pin exists only in the test host: the dev/release build has no trace of it.
  function Has-Marker([string]$f) { $b = [IO.File]::ReadAllBytes($f); $u = [Text.Encoding]::Unicode; return ($u.GetString($b).Contains('E2E FIXTURE PIN') -or $u.GetString($b, 1, $b.Length - 1).Contains('E2E FIXTURE PIN')) }
  Check (-not (Has-Marker (Join-Path $root 'build/real-integration-check/browser.exe')) -and (Has-Marker (Join-Path $root 'build/e2e-mtls-host/browser.exe'))) 'the fixture pin exists only in the test host binary, not in the dev/release browser'
  # 4 host + server
  $hostDir = Join-Path $root 'build\e2e-mtls-host'
  foreach ($n in 'WebView2Loader.dll') { Copy-Item (Join-Path $root "build\real-integration-check\$n") $hostDir -Force }
  if (!(Test-Path "$hostDir\assets")) { Copy-Item (Join-Path $root 'build\real-integration-check\assets') "$hostDir\assets" -Recurse }
  $exe = Join-Path $hostDir 'browser.exe'
  function Host-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe } | ForEach-Object ProcessId }
  function Fresh-Profile { foreach ($p in Host-Pids) { Stop-Process -Id $p -Force }
    # Review P-L2: the profile must really be gone and the fixture engine processes ended, else the stage stops.
    for ($i = 0; $i -lt 40 -and (Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.ExecutablePath -like '*\build\webview2-fixed\*' }); $i++) { Start-Sleep -Milliseconds 250 }   # the fixture runtime's engine processes end with the host
    Start-Sleep 1; foreach ($d in 'profile', 'sessions') { Remove-Item (Join-Path $hostDir $d) -Recurse -Force -ErrorAction SilentlyContinue }; Remove-Item (Join-Path $hostDir 'prototype.log') -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $hostDir 'profile')) { throw 'fresh profile: the previous profile could not be removed' }
    if (Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.ExecutablePath -like '*\build\webview2-fixed\*' }) { throw 'fresh profile: fixture engine processes still running' } }
  $server = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port" -PassThru -WindowStyle Hidden
  $env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9391'
  if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
  Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
  function Find-InHost([string]$name, [string]$automationId) {
    $hostPid = Host-Pids | Select-Object -First 1; if (!$hostPid) { return $null }
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$hostPid)
    foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', $cond)) {
      $c2 = if ($automationId) { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId) } else { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name) }
      $e = $w.FindFirst('Descendants', $c2); if ($e) { return $e }
    }
    return $null
  }
  function Invoke-El($e) { $p = $null; if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); return $true }; if ($e.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$p)) { $p.Select(); return $true }; return $false }
  function Wait-Cleared { for ($i = 0; $i -lt 60; $i++) { $l = Get-Content (Join-Path $hostDir 'prototype.log') -Raw -ErrorAction SilentlyContinue; if ($l -match 'E2E BOUND certificate decisions cleared') { return $true }; Start-Sleep -Milliseconds 250 }; return $false }
  function Navigate {
    if (-not (Wait-Cleared)) { throw 'the test host never confirmed the certificate-decision clear' }
    for ($i = 0; $i -lt 60 -and !$t; $i++) { try { $t = (Invoke-RestMethod 'http://127.0.0.1:9391/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -First 1 } catch {}; Start-Sleep -Milliseconds 500 }
    $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
    $b = [Text.Encoding]::UTF8.GetBytes("{`"id`":1,`"method`":`"Page.navigate`",`"params`":{`"url`":`"https://127.0.0.1:$port/compte`"}}"); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait(); $ws.Dispose()
  }
  $observed = Join-Path $pki 'observed.txt'
  Fresh-Profile; Start-Process -FilePath $exe -WorkingDirectory $hostDir; Navigate
  $row = $null; for ($i = 0; $i -lt 40 -and !$row; $i++) { Start-Sleep -Milliseconds 500; $row = Find-InHost "browser-e2e-mtls-$runId" '' }
  $log = Get-Content (Join-Path $hostDir 'prototype.log') -Raw -ErrorAction SilentlyContinue
  "     stage 4 host log: $((($log -split "`n") | Where-Object { $_ -match 'E2E|CERTIFICATE|WEBVIEW2|NAVIGATION' }) -join ' | ')"
  Check ($log -match 'E2E FIXTURE PIN allowed') 'the pinned fixture server certificate was allowed by the test host only'
  Check ($log -match "CERTIFICATE requested 127\.0\.0\.1:$port candidates=1") 'a real ClientCertificateRequested with exactly this run''s certificate'
  Check ([bool]$row) 'our B08 lists the run''s certificate (UIA)'
  if ($row) {
    $send = Find-InHost '' 'CertificateSend'
    Check ($send -and -not $send.Current.IsEnabled) '« Envoyer » is disabled before a choice'
    [void](Invoke-El $row); Start-Sleep -Milliseconds 500
    Check (-not (Test-Path $observed)) 'selecting the row sends nothing'
    $send = Find-InHost '' 'CertificateSend'; [void](Invoke-El $send)
    for ($i = 0; $i -lt 30 -and !(Test-Path $observed); $i++) { Start-Sleep -Milliseconds 500 }
    $seen = if (Test-Path $observed) { Get-Content $observed -Raw } else { '' }
    Check ($seen -match "browser-e2e-mtls-$runId") 'mTLS handshake: the SERVER observed this run''s client identity after « Envoyer »'
  }
  # 4b Review P-L2: the SAME certificate on a SECOND port, after the allow decision is cached: the test
  #    host refuses navigation, redirect and sub-resource before any request reaches that server.
  $pki2 = Join-Path $work 'pki2'; Remove-Item $pki2 -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $pki2 | Out-Null
  Copy-Item "$pki\server.crt", "$pki\server.key" $pki2; $port2 = $port + 1
  $server2 = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki2, "$port2", '--no-client-auth' -PassThru -WindowStyle Hidden; Start-Sleep 1
  function Nav-To([string]$url) { $t = (Invoke-RestMethod 'http://127.0.0.1:9391/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -First 1; $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); $b = [Text.Encoding]::UTF8.GetBytes("{`"id`":2,`"method`":`"Page.navigate`",`"params`":{`"url`":`"$url`"}}"); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait(); Start-Sleep 3; $ws.Dispose() }
  Nav-To "https://127.0.0.1:$port2/second"
  Nav-To "https://127.0.0.1:$port/redir?p=$port2"
  Nav-To "https://127.0.0.1:$port/with-img?p=$port2"
  $logB = Get-Content (Join-Path $hostDir 'prototype.log') -Raw -ErrorAction SilentlyContinue
  Check (-not (Test-Path "$pki2\observed.txt")) 'second port with the same certificate: no request ever reached it (navigation, redirect, sub-resource)'
  Check ($logB -match "E2E BOUND refused navigation https://127\.0\.0\.1:$port2/second" -and $logB -match "E2E BOUND refused (navigation|request) https://127\.0\.0\.1:$port2/r" -and $logB -match "E2E BOUND refused request https://127\.0\.0\.1:$port2/img\.png") 'the test host refused each second-port destination before sending'
  Check ($logB -match 'E2E BOUND certificate decisions cleared') 'certificate decisions cleared at the host''s start (ClearServerCertificateErrorActions)'
  if ($server2 -and !$server2.HasExited) { Stop-Process -Id $server2.Id -Force }
  # 4c Fail-closed branches of the host guards (failure injection, test host only).
  $hostExit = { param($envName, $url) Fresh-Profile; Remove-Item "$pki2\observed.txt" -ErrorAction SilentlyContinue
    $server2b = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki2, "$port2", '--no-client-auth' -PassThru -WindowStyle Hidden; Start-Sleep 1
    Set-Item "Env:$envName" '1'; $h = Start-Process -FilePath $exe -WorkingDirectory $hostDir -PassThru; Remove-Item "Env:$envName"
    if ($url) { try { if (Wait-Cleared) { Nav-To $url } } catch {} }
    $exited = $h.WaitForExit(20000); $code = if ($exited) { $h.ExitCode } else { -1 }; if (!$exited) { Stop-Process -Id $h.Id -Force }
    if ($server2b -and !$server2b.HasExited) { Stop-Process -Id $server2b.Id -Force }
    return @($code, (Test-Path "$pki2\observed.txt"), (Get-Content (Join-Path $hostDir 'prototype.log') -Raw -ErrorAction SilentlyContinue)) }
  $r1 = & $hostExit 'BROWSER_E2E_INJECT_CLEAR_FAILURE' $null
  "     clear-failure run: exit=$($r1[0]) log=$((($r1[2] -split "`n") | Where-Object { $_ -match 'E2E|WEBVIEW2|START' }) -join ' | ')"
  Check ($r1[0] -eq 41 -and $r1[2] -match 'E2E BOUND ABORT certificate decisions not cleared' -and $r1[2] -notmatch 'E2E BOUND certificate decisions cleared') 'clear failure: the host aborts before any fixture navigation (exit 41)'
  $r2 = & $hostExit 'BROWSER_E2E_INJECT_RESPONSE_FAILURE' "data:text/html,<img src='https://127.0.0.1:$port2/i.png'>"
  "     response-failure run: exit=$($r2[0]) second-port-reached=$($r2[1]) log=$((($r2[2] -split "`n") | Where-Object { $_ -match 'E2E|WEBVIEW2' }) -join ' | ')"
  Check ($r2[0] -eq 41 -and -not $r2[1] -and $r2[2] -match 'E2E BOUND ABORT refusal response not installed' -and $r2[2] -notmatch "E2E BOUND refused request https://127\.0\.0\.1:$port2") 'refusal response not installable: the host aborts, no refusal is claimed, the second port receives nothing'
  # 5 Cancel path on a fresh profile
  Remove-Item $observed -ErrorAction SilentlyContinue
  Fresh-Profile; Start-Process -FilePath $exe -WorkingDirectory $hostDir; Navigate
  $cancel = $null; for ($i = 0; $i -lt 40 -and !$cancel; $i++) { Start-Sleep -Milliseconds 500; $cancel = Find-InHost '' 'CertificateCancel' }
  if ($cancel) { [void](Invoke-El $cancel); Start-Sleep 3 }
  "     cancel prompt found: $([bool]$cancel); server saw: $(if (Test-Path $observed) { (Get-Content $observed -Raw).Trim() } else { 'nothing' })"
  $log2 = Get-Content (Join-Path $hostDir 'prototype.log') -Raw -ErrorAction SilentlyContinue; "     host log: $((($log2 -split "`n") | Where-Object { $_ -match 'CERTIFICATE|E2E' }) -join ' | ')"
  Check ([bool]$cancel -and -not (Test-Path $observed)) '« Ne pas envoyer »: the server receives no client identity'
} finally {
  foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like '*\build\e2e-mtls-host\browser.exe' } | ForEach-Object ProcessId)) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  if ($m) { $clean = Remove-OwnedCert $m; Check $clean 'cleanup: this run''s certificate AND private key removed (exact thumbprint, verified)'; $m.status = if ($clean) { 'cleaned' } else { 'cleanup-failed' }; Save-Manifest $m
    $after = @(Get-ChildItem Cert:\CurrentUser\My | ForEach-Object Thumbprint); Check ((@($after) -join ',') -eq (@($m.before) -join ',')) 'CurrentUser\My is back to its exact state before the run' }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  if ($pki) { Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue }
  $lock.Close()
}
if ($script:fails) { "FAIL mtls e2e: $script:fails"; exit 1 } else { 'PASS mtls e2e (fixture PKI pinned in the test host only; Fixed Version runtime): real handshake after explicit « Envoyer », cancel sends nothing, certificate and key cleaned' }
