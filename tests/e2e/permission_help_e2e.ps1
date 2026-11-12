param([string]$Build = 'build/v2', [string]$Fixture = 'build/e2e-permission', [string]$Runtime = '')
# Real engine E2E of the permission popovers (design/origin-prompt-help.md): a throwaway copy of the build
# with a fresh profile, a temporary local server (killed at the end) and our own fixture page. It asks
# for persistent storage (kind 13) and notifications (kind 4) through the real WebView2 runtime, then
# reads the popover through UI Automation only (title, subtitle, actions, no collapsible help), closes it. Nothing
# is ever allowed; no real site and no personal profile are touched. Only fixture processes are stopped.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
if (Test-Path $Fixture) { Remove-Item -Recurse -Force $Fixture }
New-Item -ItemType Directory -Force $Fixture | Out-Null
Copy-Item -Recurse -Force (Join-Path $Build '*') $Fixture
Get-ChildItem $Fixture -Include 'profile', 'sessions', '*.log', 'session', 'settings' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
$site = Join-Path $fix 'e2e-site'; New-Item -ItemType Directory -Force $site | Out-Null
[IO.File]::WriteAllText((Join-Path $site 'index.html'), '<!doctype html><title>permission fixture</title><p>fixture</p>')
[IO.File]::WriteAllText((Join-Path $site 'other.html'), '<!doctype html><title>other</title><p>other</p>')
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
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Log-Text { $f = Get-ChildItem $fix -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($f) { [IO.File]::ReadAllText($f.FullName) } else { '' } }
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
$server = Start-Process -FilePath python -ArgumentList '-m', 'http.server', '18767', '--bind', '127.0.0.1' -WorkingDirectory $site -PassThru -WindowStyle Hidden
$origin = 'http://127.0.0.1:18767/'
try {
  Start-Sleep 1
  $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9367'
  if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
  Start-Process -FilePath $exe -WorkingDirectory $fix
  $ws = Cdp-Connect 9367
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'index.html' }); Start-Sleep 2
  $rt = (Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like '*9367*' } | Select-Object -First 1).ExecutablePath
  "runtime: $rt"

  # 1. Persistent storage (kind 13): the real engine request; the popover has no collapsible help (Paper B02).
  [void](Eval $ws "window.__persist='pending'; navigator.storage.persist().then(v=>window.__persist=String(v)); 1" $true)
  $asked = Wait-Log 'PERMISSION request kind=13 http://127\.0\.0\.1:18767'
  Check $asked 'engine raised PermissionRequested kind=13 (persistent storage) for the fixture origin'
  if ($asked) {
    $group = Find-Id 'PermissionPrompt'; Check ($group -and $group.Current.Name -like '127.0.0.1*conserver ses données*') "popover title names persistent storage: $($group.Current.Name)"
    Check ($group -and $group.Current.HelpText -eq "Sinon, le navigateur peut les effacer s'il manque d'espace.") 'popover subtitle (UIA help) explains persistent storage'
    Check ($null -eq (Find-Id 'PermissionMore' 2)) 'no « Plus d''infos » row on the popover (UIA)'
    Check ($null -ne (Find-Id 'PermissionBlock') -and $null -ne (Find-Id 'PermissionAllow') -and $null -ne (Find-Id 'DialogClose')) 'Bloquer, Autoriser and Fermer exposed'
    Check ((Count-Name 'Mémoriser pour ce site') -eq 1) 'remember check exposed for persistent storage (control for the notifications check)'
    Check ((Eval $ws 'window.__persist') -eq 'pending' -and -not ((Log-Text) -match 'PERMISSION kind=13 action=')) 'request pending until the user decides'
    [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'other.html' }); Start-Sleep 2
    Check (Wait-Log 'PERMISSION kind=13 action=0') 'navigation: the request is dismissed (action 0, not saved)'
    Check (([regex]::Matches((Log-Text), 'PERMISSION kind=13 action=')).Count -eq 1) 'answered exactly once'
  }

  # 2. Notifications (kind 4): title, subtitle and buttons only; closed with the close button (Dismiss).
  [void](Cdp-Send $ws 'Page.navigate' @{ url = $origin + 'index.html' }); Start-Sleep 2
  [void](Eval $ws "window.__notif='pending'; Notification.requestPermission().then(v=>window.__notif=v); 1" $true)
  $asked = Wait-Log 'PERMISSION request kind=4 http://127\.0\.0\.1:18767'
  Check $asked 'engine raised PermissionRequested kind=4 (notifications) for the fixture origin'
  if ($asked) {
    $group = Find-Id 'PermissionPrompt'; Check ($group -and $group.Current.Name -like '*veut vous envoyer des notifications') "notifications title: $($group.Current.Name)"
    Check ($group -and $group.Current.HelpText -eq 'Le site pourra afficher des alertes en dehors de la page.') 'notifications subtitle, no unproven promise'
    Check ($null -eq (Find-Id 'PermissionMore' 2)) 'notifications: no « Plus d''infos » row (UIA)'
    Check ((Count-Name 'Mémoriser pour ce site') -eq 0) 'notifications: no remember check (always remembered, policy unchanged)'
    Check ((Eval $ws 'window.__notif') -eq 'pending') 'notifications: pending until the user decides'
    $close = Find-Id 'DialogClose'; if ($close) { $close.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    Check (Wait-Log 'PERMISSION kind=4 action=0') 'close button: dismissed (action 0)'
    Start-Sleep 1; "page sees: Notification.requestPermission() -> $(Eval $ws 'window.__notif')"
  }
  $lines = ([regex]::Matches((Log-Text), 'PERMISSION[^\r\n]*') | ForEach-Object { $_.Value }) -join "`n"
  "---- fixture log (PERMISSION lines only) ----`n$lines"
} finally {
  foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue; Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($script:fails) { "$($script:fails) failure(s)"; exit 1 }
'permission help E2E: all checks passed'
