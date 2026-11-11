param([string]$Fixture = 'build/e2e-windows', [string]$Runtime = '')
# Real web E2E (A9): a normal window and a private window of OUR fixture copy share nothing
# (cookies, localStorage), and a private window's data is gone after it closes. The Microsoft Fixed
# Version runtime and the DevTools port are given to the test processes only (process environment
# of this script; nothing installed, registered or global). Only fixture processes are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$script:cdpId = 0
function Page-Ids([int]$port) { try { (Invoke-RestMethod "http://127.0.0.1:$port/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | ForEach-Object { $_.id } } catch { @() } }
# Windows of one profile share one WebView2 browser process (and its DevTools port): a new
# window's page is the target that was not there before.
function Cdp-ConnectNew([int]$port, $before) {
  for ($i = 0; $i -lt 60; $i++) {
    $t = (Invoke-RestMethod "http://127.0.0.1:$port/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' -and ($before -notcontains $_.id) } | Select-Object -First 1
    if ($t) { break }; Start-Sleep -Milliseconds 500
  }
  if (!$t) { throw "no new target on $port" }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket
  $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws
}
function Cdp-Connect([int]$port, [string]$match) {
  for ($i = 0; $i -lt 60; $i++) {
    try { $list = Invoke-RestMethod "http://127.0.0.1:$port/json"; $t = $list | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' -and $_.url -like "*$match*" } | Select-Object -First 1; if ($t) { break } } catch {}
    Start-Sleep -Milliseconds 500
  }
  if (!$t) { throw "no target on $port matching $match" }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket
  $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws
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
function Eval($ws, [string]$expr) { (Cdp-Send $ws 'Runtime.evaluate' @{ expression = $expr; returnByValue = $true }).result.result.value }
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Stop-Fixture { foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
function Start-Browser([int]$port, [string[]]$extra) {
  $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$port"
  if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
  if ($extra) { Start-Process -FilePath $exe -ArgumentList $extra -WorkingDirectory $fix } else { Start-Process -FilePath $exe -WorkingDirectory $fix }
}

Stop-Fixture
Remove-Item (Join-Path $fix 'sessions\*') -Force -ErrorAction SilentlyContinue   # fixture only: no restored windows in this test
$server = Start-Process -FilePath python -ArgumentList '-m', 'http.server', '18765', '--bind', '127.0.0.1' -WorkingDirectory (Join-Path $fix 'assets') -PassThru -WindowStyle Hidden
try {
  Start-Sleep 1
  $origin = 'http://127.0.0.1:18765/'
  Start-Browser 9351
  $n = Cdp-Connect 9351 ''
  [void](Cdp-Send $n 'Page.navigate' @{ url = $origin }); Start-Sleep 2
  [void](Eval $n "document.cookie='e2e=normal; max-age=3600; path=/'; localStorage.setItem('e2e','normal'); 1")
  Check ((Eval $n 'document.cookie') -like '*e2e=normal*') 'normal window: cookie set'
  $before = @(Page-Ids 9351)
  Start-Browser 9351 @('--private')
  $p = Cdp-ConnectNew 9351 $before
  [void](Cdp-Send $p 'Page.navigate' @{ url = $origin }); Start-Sleep 2
  Check (-not ((Eval $p 'document.cookie') -like '*e2e=normal*')) 'private window does not see the normal cookie'
  Check ($null -eq (Eval $p "localStorage.getItem('e2e')")) 'private window does not see the normal localStorage'
  [void](Eval $p "document.cookie='e2e=prive; max-age=3600; path=/'; localStorage.setItem('e2e','prive'); 1")
  Check (-not ((Eval $n 'document.cookie') -like '*e2e=prive*')) 'normal window does not see the private cookie'
  $p.Dispose()
  $privatePid = Fixture-Pids | Where-Object { (Get-CimInstance Win32_Process -Filter "ProcessId=$_").CommandLine -like '*--private*' }
  foreach ($pp in $privatePid) { $h = (Get-Process -Id $pp).MainWindowHandle; Add-Type -Namespace W -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);' -ErrorAction SilentlyContinue; [void][W.U]::PostMessageW($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }
  Start-Sleep 4
  $before = @(Page-Ids 9351)
  Start-Browser 9351 @('--private')
  $p2 = Cdp-ConnectNew 9351 $before
  [void](Cdp-Send $p2 'Page.navigate' @{ url = $origin }); Start-Sleep 2
  Check (-not ((Eval $p2 'document.cookie') -like '*e2e=prive*') -and $null -eq (Eval $p2 "localStorage.getItem('e2e')")) 'a new private window starts empty (the previous private data is gone)'
  Check ((Eval $n 'document.cookie') -like '*e2e=normal*') 'the normal window kept its own cookie throughout'
  $p2.Dispose(); $n.Dispose()
} finally {
  Stop-Fixture
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue; Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL private isolation e2e: $script:fails"; exit 1 } else { 'PASS private isolation e2e (web, real WebView2 Fixed Version runtime): cookies and localStorage isolated, private data gone after close' }
