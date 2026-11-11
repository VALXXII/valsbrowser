param([string]$Fixture = 'build/e2e-windows', [string]$Runtime = '')
# Real web E2E: a server certificate the engine refuses (throwaway self-signed, local) gets OUR page
# for the top-level navigation, with the real cause, no « continue », and the server never serves the
# page; a refused sub-resource does not replace its host page. Nothing is trusted or installed:
# the certificate is a file used by the local test server only. Only fixture processes are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
$pki = 'build/e2e-tls'; Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $pki | Out-Null
$openssl = 'C:\Program Files\Git\usr\bin\openssl.exe'
$ErrorActionPreference = 'Continue'   # openssl reports progress on stderr
& $openssl req -x509 -newkey rsa:2048 -nodes -keyout "$pki/server.key" -out "$pki/server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
function Stop-Fixture { foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
$script:id = 0
function Send($ws, [string]$method, $params) {
  $script:id++; $msg = @{ id = $script:id; method = $method; params = $params } | ConvertTo-Json -Depth 8 -Compress
  $b = [Text.Encoding]::UTF8.GetBytes($msg); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 1048576
  while ($true) { $sb = New-Object Text.StringBuilder; do { $t = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$t.Wait(15000)) { return $null }; $r = $t.Result; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count)) } while (!$r.EndOfMessage); $o = $sb.ToString() | ConvertFrom-Json; if ($o.id -eq $script:id) { return $o } }
}
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function Connect-Page {
  for ($i = 0; $i -lt 60; $i++) { try { $t = (Invoke-RestMethod 'http://127.0.0.1:9381/json') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -First 1; if ($t) { break } } catch {}; Start-Sleep -Milliseconds 500 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws
}
Stop-Fixture; Remove-Item (Join-Path $fix 'sessions\*') -Force -ErrorAction SilentlyContinue
$tls = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, '18444', '--no-client-auth' -PassThru -WindowStyle Hidden
$http = Start-Process python -ArgumentList '-m', 'http.server', '18766', '--bind', '127.0.0.1', '--directory', $pki -PassThru -WindowStyle Hidden
[IO.File]::WriteAllText("$pki/host.html", '<title>page hote</title><img src="https://127.0.0.1:18444/x.png">')
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9381'
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Sleep 1; Start-Process -FilePath $exe -WorkingDirectory $fix
  $ws = Connect-Page
  [void](Send $ws 'Page.navigate' @{ url = 'https://127.0.0.1:18444/compte' }); Start-Sleep 4
  $ws.Dispose(); $ws = Connect-Page
  $title = Eval $ws 'document.title'; $body = Eval $ws 'document.body ? document.body.innerText : ""'
  "     title: $title"
  Check ($title -like 'La connexion s*curis*e n*a pas pu *tre *tablie') 'top-level navigation: our certificate page (not the engine interstitial)'
  Check ($body -like '*127.0.0.1*' -and $body -like '*pas valide*' -and $body -like '*CERTIFICATE_IS_INVALID*' -and -not ($body -like '*NET::*')) 'real destination, cause and code shown'
  Check (-not ($body -like '*Continuer*')) 'no « continue » action'
  Check (-not (Test-Path "$pki/observed.txt")) 'the refused server never served the page (no request reached it)'
  [void](Send $ws 'Page.navigate' @{ url = 'http://127.0.0.1:18766/host.html' }); Start-Sleep 4
  $ws.Dispose(); $ws = Connect-Page
  Check ((Eval $ws 'document.title') -eq 'page hote') 'a refused sub-resource does not replace its host page'
  $ws.Dispose()
} finally {
  Stop-Fixture; foreach ($p in $tls, $http) { if ($p -and !$p.HasExited) { Stop-Process -Id $p.Id -Force } }
  Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue; Remove-Item Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL tls page e2e: $script:fails"; exit 1 } else { 'PASS tls page e2e (web, Fixed Version runtime): our page for a refused top-level certificate, no bypass, sub-resource unaffected' }
