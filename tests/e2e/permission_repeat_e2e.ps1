param([string]$Build = 'build/v2', [string]$Fixture = 'build/e2e-permission-repeat', [string]$Runtime = '')
# Real engine E2E of repeated camera / microphone requests (Google Meet asks again on every device
# check). A throwaway copy of the build with a fresh profile, a temporary local server and a fixture page;
# Chromium's fake capture devices (--use-fake-device-for-media-stream) so no real camera or microphone is
# opened, and the permission UI stays ours (no --use-fake-ui-for-media-stream). After one « Autoriser »
# (not remembered), the same page asking again gets its devices without a new popover; after a
# navigation it is asked again; one « Bloquer » is kept the same way. No real site, no personal profile.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if (Test-Path $Fixture) { Remove-Item -Recurse -Force $Fixture }
New-Item -ItemType Directory -Force $Fixture | Out-Null
Copy-Item -Recurse -Force (Join-Path $Build '*') $Fixture
Get-ChildItem $Fixture -Include 'profile', 'sessions', '*.log', 'session', 'settings' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
$site = Join-Path $fix 'e2e-site'; New-Item -ItemType Directory -Force $site | Out-Null
$page = '<!doctype html><title>media fixture</title><p>fixture</p><script>window.ask=function(n,c){window[n]="pending";navigator.mediaDevices.getUserMedia(c).then(function(s){s.getTracks().forEach(function(t){t.stop()});window[n]="ok"},function(e){window[n]=e.name})};</script>'
[IO.File]::WriteAllText((Join-Path $site 'index.html'), $page)
[IO.File]::WriteAllText((Join-Path $site 'again.html'), $page)
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$script:cdpId = 0
function Cdp-Connect([int]$port) {
  for ($i = 0; $i -lt 60; $i++) { try { $t = (Invoke-RestMethod "http://127.0.0.1:$port/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -First 1; if ($t) { break } } catch {}; Start-Sleep -Milliseconds 500 }
  if (!$t) { throw "no target on $port" }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws
}
function Cdp-Send($ws, [string]$method, $params) {
  $script:cdpId++; $msg = @{ id = $script:cdpId; method = $method; params = $params } | ConvertTo-Json -Depth 10 -Compress
  $bytes = [Text.Encoding]::UTF8.GetBytes($msg); $ws.SendAsync([ArraySegment[byte]]::new($bytes), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 1048576
  while ($true) {
    $sb = New-Object Text.StringBuilder
    do { $task = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$task.Wait(15000)) { return $null }; $r = $task.Result; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count)) } while (!$r.EndOfMessage)
    $obj = $sb.ToString() | ConvertFrom-Json; if ($obj.id -eq $script:cdpId) { return $obj }
  }
}
function Eval($ws, [string]$expr, [bool]$gesture = $false) { (Cdp-Send $ws 'Runtime.evaluate' @{ expression = $expr; returnByValue = $true; userGesture = $gesture }).result.result.value }
# READ-ONLY page state: a missing CDP answer (no response / no value) is retried a few times and logged; a REAL value is
# returned as-is (a wrong state still fails). Never used for expressions with side effects.
function Read-Page($ws, [string]$expr) { for ($i = 0; $i -lt 3; $i++) { $r = Cdp-Send $ws 'Runtime.evaluate' @{ expression = $expr; returnByValue = $true }; if ($r -and $r.result -and $r.result.result -and $null -ne $r.result.result.value) { return $r.result.result.value }; "     cdp: no value for a read (attempt $($i + 1))" | Out-Host; Start-Sleep -Milliseconds 300 }; return $null }
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Log-Text { $f = Get-ChildItem $fix -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($f) { $st = [IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite, Delete'); try { (New-Object IO.StreamReader($st)).ReadToEnd() } finally { $st.Dispose() } } else { '' } }   # shared read: the browser may be writing it
function Wait-Log([string]$pattern, [int]$seconds = 15) { for ($i = 0; $i -lt $seconds * 4; $i++) { if ((Log-Text) -match $pattern) { return $true }; Start-Sleep -Milliseconds 250 }; return $false }
$UIA = [Windows.Automation.AutomationElement]
function Count-Name([string]$name) { $n = 0; foreach ($fp in @(Fixture-Pids)) { $cond = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, [int]$fp)), (New-Object Windows.Automation.PropertyCondition($UIA::NameProperty, $name))); $n += $UIA::RootElement.FindAll([Windows.Automation.TreeScope]::Descendants, $cond).Count }; return $n }
function Find-Id([string]$id, [int]$seconds = 10) {
  $pids = @(Fixture-Pids)
  for ($i = 0; $i -lt $seconds * 4; $i++) {
    foreach ($p in $pids) {
      $cond = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, [int]$p)), (New-Object Windows.Automation.PropertyCondition($UIA::AutomationIdProperty, $id)))
      try { $e = $UIA::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants, $cond) } catch { $e = $null }; if ($e) { return $e }
    }
    Start-Sleep -Milliseconds 250
  }
  return $null
}

foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
$server = Start-Process -FilePath python -ArgumentList '-m', 'http.server', '18768', '--bind', '127.0.0.1' -WorkingDirectory $site -PassThru -WindowStyle Hidden
$origin = 'http://127.0.0.1:18768/'
function Count-Log([string]$pattern) { ([regex]::Matches((Log-Text), $pattern)).Count }
function Remember-Check { foreach ($fp in @(Fixture-Pids)) { $c = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, [int]$fp)), (New-Object Windows.Automation.PropertyCondition($UIA::NameProperty, 'Mémoriser pour ce site'))); $e = $UIA::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants, $c); if ($e) { return $e } }; return $null }
function Remember-State { $e = Remember-Check; if (!$e) { return 'missing' }; $t = $null; if ($e.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern, [ref]$t)) { return [string]$t.Current.ToggleState }; return 'no-toggle' }
function Clear-Remember { $e = Remember-Check; if (!$e) { return $false }; $t = $null; if ($e.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern, [ref]$t)) { $t.Toggle(); return $true }; $i = $null; if ($e.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$i)) { $i.Invoke(); return $true }; return $false }
function Wait-Value($ws, [string]$name, [int]$seconds = 10) { for ($i = 0; $i -lt $seconds * 4; $i++) { $v = Read-Page $ws "window.$name"; if ($v -and $v -ne 'pending') { return $v }; Start-Sleep -Milliseconds 250 }; return (Read-Page $ws "window.$name") }
function Press([string]$id) { for ($i = 0; $i -lt 3; $i++) { $b = Find-Id $id; if ($b) { try { $b.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke(); return $true } catch { "     press $id retried: $($_.Exception.Message)" | Out-Host } }; Start-Sleep -Milliseconds 300 }; return $false }   # the popover may still be animating in
try {
  Start-Sleep 1
  $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9368 --use-fake-device-for-media-stream'
  if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
  Start-Process -FilePath $exe -WorkingDirectory $fix
  $ws = Cdp-Connect 9368
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'index.html' }); Start-Sleep 2

  # 1. Camera + microphone together, answered « Autoriser » for this time only (remember cleared): the same page
  #    asking again (Meet) is answered from that answer.
  [void](Eval $ws "ask('__r0',{audio:true,video:true}); 1" $true)
  Check (Wait-Log 'PERMISSION request kind=2 http://127\.0\.0\.1:18768') 'engine asked for the camera'
  Check ($null -ne (Find-Id 'PermissionPrompt')) 'first request: our popover shows'
  Check ((Remember-State) -eq 'On') "« Mémoriser pour ce site » checked by default ($(Remember-State))"
  Check (Clear-Remember) 'remember cleared for this time only (UIA)'; Start-Sleep -Milliseconds 300
  Check ((Remember-State) -eq 'Off') "remember now cleared ($(Remember-State))"
  Check (Press 'PermissionAllow') 'Autoriser pressed (UIA)'
  $r0 = Wait-Value $ws '__r0'; Check ($r0 -eq 'ok') "first request: devices granted ($r0)"
  Start-Sleep 1
  $shown = Count-Log 'PERMISSION chip shown'

  # 2. The same page asks again, like Meet: video alone, audio alone, both, several times.
  $all = $true
  foreach ($n in 1..4) {
    foreach ($c in @('{video:true}', '{audio:true}', '{audio:true,video:true}')) {
      $name = "__r$n$($c.Length)"; [void](Eval $ws "ask('$name',$c); 1" $true)
      $v = Wait-Value $ws $name 8; if ($v -ne 'ok') { $all = $false; "     ask $c -> $v" }
      if ($null -ne (Find-Id 'PermissionPrompt' 1)) { $all = $false; "     popover shown again for $c"; [void](Press 'DialogClose') }
    }
  }
  Check $all 'repeat requests (12): devices granted each time, no popover'
  Check ((Count-Log 'PERMISSION chip shown') -eq $shown) "no new request chip ($shown before, $(Count-Log 'PERMISSION chip shown') after)"
  "     engine re-asked and was answered from the earlier answer: $(Count-Log 'reused allow') time(s)"
  Check (-not ((Log-Text) -match 'PERMISSION kind=[12] action=1 saved')) 'nothing saved in the profile (allow was for this time only)'

  # 3. Another document of the same tab: asked again (the answer was for that page only).
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'again.html' }); Start-Sleep 2
  [void](Eval $ws "ask('__b0',{video:true}); 1" $true)
  Check ($null -ne (Find-Id 'PermissionPrompt')) 'after a navigation: asked again'
  Check (Press 'PermissionBlock') 'Bloquer pressed (UIA)'
  $b0 = Wait-Value $ws '__b0'; Check ($b0 -eq 'NotAllowedError') "blocked: page refused ($b0)"
  Start-Sleep 1
  $blockedAll = $true
  foreach ($n in 1..3) { [void](Eval $ws "ask('__b$n',{video:true}); 1" $true); $v = Wait-Value $ws "__b$n" 8; if ($v -ne 'NotAllowedError') { $blockedAll = $false; "     ask -> $v" }; if ($null -ne (Find-Id 'PermissionPrompt' 1)) { $blockedAll = $false; '     popover shown again after Bloquer'; [void](Press 'DialogClose') } }
  Check $blockedAll 'repeat requests after Bloquer: refused, no popover'

  # 4. Default answers (remember checked) last across visits: the camera stays blocked, a microphone allowed now is
  #    still allowed on the next visit, without a popover.
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'index.html' }); Start-Sleep 2
  [void](Eval $ws "ask('__c1',{video:true}); 1" $true); $c1 = Wait-Value $ws '__c1' 8
  Check ($c1 -eq 'NotAllowedError' -and $null -eq (Find-Id 'PermissionPrompt' 1)) "next visit: camera still blocked, no popover ($c1)"
  [void](Eval $ws "ask('__m1',{audio:true}); 1" $true)
  Check ($null -ne (Find-Id 'PermissionPrompt')) 'next visit: microphone (allowed this time only before) asked again'
  Check ((Remember-State) -eq 'On') 'remember checked by default'
  Check (Press 'PermissionAllow') 'Autoriser pressed with remember checked'
  $m1 = Wait-Value $ws '__m1'; Check ($m1 -eq 'ok') "microphone granted ($m1)"
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'again.html' }); Start-Sleep 2
  [void](Eval $ws "ask('__m2',{audio:true}); 1" $true); $m2 = Wait-Value $ws '__m2' 8
  Check ($m2 -eq 'ok' -and $null -eq (Find-Id 'PermissionPrompt' 1)) "following visit: microphone granted without a popover ($m2)"
  $lines = ([regex]::Matches((Log-Text), 'PERMISSION[^\r\n]*') | ForEach-Object { $_.Value }) -join "`n"
  "---- fixture log (PERMISSION lines only) ----`n$lines"
} finally {
  foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue; Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($script:fails) { "$($script:fails) failure(s)"; exit 1 }
'permission repeat E2E: all checks passed'
