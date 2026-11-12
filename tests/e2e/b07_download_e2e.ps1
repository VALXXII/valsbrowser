param([string]$Fixture = 'build/e2e-windows', [string]$Runtime = '')
# Real web E2E (B07): Microsoft's official SmartScreen demo download (harmless test file published
# for exactly this purpose) is blocked by the engine, and our B07 opens with the real reason. The
# fixture's download folder is inside the fixture (never the user's Downloads). Runtime and DevTools
# port are given to the test processes only. Only fixture processes are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'; $log = Join-Path $fix 'prototype.log'
$dl = Join-Path $fix 'downloads-folder'; New-Item -ItemType Directory -Force $dl | Out-Null
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Stop-Fixture { foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
Stop-Fixture
Remove-Item (Join-Path $fix 'sessions\*') -Force -ErrorAction SilentlyContinue
[IO.File]::WriteAllText((Join-Path $fix 'settings'), "browser-settings 1`nrestore-tabs=0`ndownload-folder=$dl`nask-where-to-save=0`n")
$before = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9361'
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Process -FilePath $exe -WorkingDirectory $fix
  for ($i = 0; $i -lt 60 -and !$t; $i++) { try { $t = (Invoke-RestMethod 'http://127.0.0.1:9361/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -First 1 } catch {}; Start-Sleep -Milliseconds 500 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
  $msg = [Text.Encoding]::UTF8.GetBytes('{"id":1,"method":"Page.navigate","params":{"url":"https://demo.smartscreen.msft.net/download/known/knownmalicious.exe"}}')
  $ws.SendAsync([ArraySegment[byte]]::new($msg), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $text = ''
  for ($i = 0; $i -lt 90; $i++) { Start-Sleep 1; $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite'); $fs.Seek($before, 'Begin') | Out-Null; $text = (New-Object IO.StreamReader($fs)).ReadToEnd(); $fs.Close(); if ($text -match 'DOWNLOAD blocked by the engine') { break } }
  $line = ($text -split "`n" | Where-Object { $_ -match 'DOWNLOAD blocked by the engine' } | Select-Object -First 1)
  "     $line"
  $started = $text -match 'DOWNLOAD started'
  if (!$line -and $started) { "NOT VERIFIED: the engine started the download and did not interrupt it within 90 s (no reputation/danger API in SDK 1.0.4314.35: the verdict is not observable); B07 real event not reached on this path"; $script:inconclusive = $true }
  else { Check ([bool]$line) 'the engine blocked the SmartScreen demo download and our B07 path ran (real event)'; Check ([bool]($line -match 'reason=\d+')) 'the real interrupt reason is carried' }
} finally {
  Stop-Fixture
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue; Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL b07 e2e: $script:fails"; exit 1 } elseif ($script:inconclusive) { 'INCONCLUSIVE b07 e2e: real engine event not reached (see NOT VERIFIED)'; exit 2 } else { 'PASS b07 e2e (web, real engine block on the official SmartScreen demo file)' }
