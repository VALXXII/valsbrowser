param([string]$Build = 'build/v2', [string]$Fixture = 'build/e2e-http-status', [string]$Runtime = '', [switch]$Observe)
# Real-engine E2E: what the browser shows when a site answers with an HTTP error status AND its own body
# (e.g. a 403 challenge page), versus real transport failures. A throwaway copy of the build, a fresh
# fixture profile, a local server (tests/e2e/http_status_server.py) on 127.0.0.1 only. For each case the
# script records the page's title, its JS sentinel and the log lines of that navigation.
#  -Observe: print the facts only (used for the BEFORE measurement), no expectation.
# Only fixture processes (exact image path) are stopped. No real site, no personal profile.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
if (Test-Path $Fixture) { Remove-Item -Recurse -Force $Fixture }
New-Item -ItemType Directory -Force $Fixture | Out-Null
Copy-Item -Recurse -Force (Join-Path $Build '*') $Fixture
Get-ChildItem $Fixture -Include 'profile', 'sessions', '*.log', 'session', 'settings', 'bookmarks', 'history', 'downloads', 'passwords' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($Observe) { "info $what" } elseif ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
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
function Read-Page($ws, [string]$expr) { for ($i = 0; $i -lt 3; $i++) { $r = Cdp-Send $ws 'Runtime.evaluate' @{ expression = $expr; returnByValue = $true }; if ($r -and $r.result -and $r.result.result) { return $r.result.result.value }; Start-Sleep -Milliseconds 300 }; return $null }
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Log-Text { $f = Join-Path $fix 'prototype.log'; if (Test-Path $f) { $st = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite, Delete'); try { (New-Object IO.StreamReader($st)).ReadToEnd() } finally { $st.Dispose() } } else { '' } }

$port = 18781; $closed = 18782   # $closed: nothing listens there (connection refused)
foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
$server = Start-Process -FilePath python -ArgumentList 'tests/e2e/http_status_server.py', "$port" -PassThru -WindowStyle Hidden
try {
  Start-Sleep 1
  $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9381'
  if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
  Start-Process -FilePath $exe -WorkingDirectory $fix
  $ws = Cdp-Connect 9381; Start-Sleep 2
  $rt = (Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like '*9381*' } | Select-Object -First 1).ExecutablePath
  "runtime: $rt"
  $cases = @(
    @{ name = '200'; url = "http://127.0.0.1:$port/s/200"; body = $true },
    @{ name = '403'; url = "http://127.0.0.1:$port/s/403"; body = $true },
    @{ name = '404'; url = "http://127.0.0.1:$port/s/404"; body = $true },
    @{ name = '429'; url = "http://127.0.0.1:$port/s/429"; body = $true },
    @{ name = '503'; url = "http://127.0.0.1:$port/s/503"; body = $true },
    @{ name = '302-404'; url = "http://127.0.0.1:$port/r/404"; body = $true; title = 'sentinel-404' },   # redirect, then the site's 404 page
    @{ name = 'empty-404'; url = "http://127.0.0.1:$port/e/404"; body = $null },  # 4xx with no body: engine behaviour recorded (review M1)
    @{ name = 'empty-500'; url = "http://127.0.0.1:$port/e/500"; body = $null },
    @{ name = 'cut'; url = "http://127.0.0.1:$port/cut"; body = $false },       # response cut after its 200 headers: our error page (HttpStatusCode 200)
    @{ name = 'empty'; url = "http://127.0.0.1:$port/empty"; body = $false },   # no response at all: our error page
    @{ name = 'rst'; url = "http://127.0.0.1:$port/rst"; body = $false },       # reset: our error page
    @{ name = 'refused'; url = "http://127.0.0.1:$closed/x"; body = $false }    # connection refused: our error page
  )
  foreach ($c in $cases) {
    $before = (Log-Text).Length
    [void](Cdp-Send $ws 'Page.navigate' @{ url = $c.url })
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 250; if ((Log-Text).Substring($before) -match '(?m)^(NAVIGATION|ERROR PAGE)') { break } }   # wait for this navigation's outcome in the log
    Start-Sleep -Milliseconds 800   # let our error page (NavigateToString) or the kept page settle
    $title = Read-Page $ws 'document.title'; $sentinel = Read-Page $ws 'String(window.__sentinel)'; $href = Read-Page $ws 'location.href'
    $lines = ((Log-Text).Substring($before) -split "`r?`n" | Where-Object { $_ -match '^(NAVIGATION|ERROR PAGE|NAV)' }) -join ' | '
    "case $($c.name): title=[$title] sentinel=[$sentinel] location=[$href]"
    "     log: $lines"
    $want = if ($c.title) { $c.title } else { "sentinel-$($c.name)" }; $js = 'js-' + $want.Substring(9)
    $own = $title -eq $want -and $sentinel -eq $js
    $errorPage = $lines -match 'ERROR PAGE'
    if ($c.body -eq $true) { Check ($own -and -not $errorPage) "HTTP $($c.name): the server's own body and script are shown (not replaced by our error page)" }
    elseif ($c.body -eq $false) { Check ($errorPage -and -not $own) "$($c.name): a real transport failure still shows our error page" }
    else { "     ($($c.name): recorded only)" }
  }
  $ws.Dispose()
} finally {
  foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($Observe) { 'OBSERVED (no expectation)' } elseif ($script:fails) { "FAIL http status e2e: $script:fails"; exit 1 } else { 'PASS http status e2e: HTTP error statuses keep the site''s own page; transport failures keep our error page' }
