param([string]$Runtime = '', [switch]$NoControl, [switch]$SkipBuild, [switch]$ControlOnly)
# Vault activation on the PRODUCT path (review R-5). The host (tests/e2e/vault_product_host.cpp) is the production
# main.cpp / elements-integration.h with only Vault::ActiveFlag() forced on (BROWSER_E2E_VAULT_ACTIVE) and the
# fixture pin for the local HTTPS server: no lifted foreground condition. Proves, with the foreground checks in
# force: « VAULT state: active »; B04 appears after a login submit and saves only after a real click on
# « Enregistrer » in the foreground window; the native key then the account row (real clicks) fill the focused
# form; the engine's own password autosave is OFF while ours is active (its Login Data holds no synthetic
# account), with a CONTROL run of the same flow on the vault-inactive fixture host (tests/e2e/mtls_host.cpp)
# whose engine does record it, so the check is not vacuous.
# Input: the host window is brought to the foreground and clicked with SendInput, ONLY at points whose window
# belongs to the host process (checked with WindowFromPoint before every click); the cursor is put back.
# Synthetic profile in build/, fictitious account, local fixture server on 127.0.0.1 only; only processes
# whose image is this fixture's host are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'job_guard.ps1')   # every process started here dies with this script, even interrupted
$root = (Resolve-Path '.').Path; $pki = Join-Path $root 'build\e2e-vault-product-pki'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$user = 'e2e-synth-product'; $secret = 'E2e-Product-6666!'; $port = 18449; $cdp = 9403
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class VP {
  [DllImport("user32.dll")] static extern IntPtr SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint f);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx, dy; public uint data, flags, time; public UIntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public MI mi; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  public static void Aware() { try { SetProcessDpiAwarenessContext((IntPtr)(-4)); } catch { } }
  public static uint PidOf(IntPtr h) { uint p; GetWindowThreadProcessId(h, out p); return p; }
  public static bool Front(IntPtr h, uint pid) {
    for (int i = 0; i < 5; i++) {
      ShowWindow(h, 9); keybd_event(0x12, 0, 0, UIntPtr.Zero); keybd_event(0x12, 0, 2, UIntPtr.Zero); SetForegroundWindow(h);
      System.Threading.Thread.Sleep(250); if (PidOf(GetForegroundWindow()) == pid) return true; }
    return false; }
  // A real click at (x, y) only when the window under that point belongs to `pid`; the cursor is put back.
  public static string Click(int x, int y, uint pid) {
    var p = new POINT { X = x, Y = y }; var w = WindowFromPoint(p); var top = GetAncestor(w, 2);
    if (PidOf(top) != pid) return "refused: the window under the point is not the host's";
    POINT old; GetCursorPos(out old); SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    var down = new INPUT { type = 0, mi = new MI { flags = 0x0002 } }; var up = new INPUT { type = 0, mi = new MI { flags = 0x0004 } };
    SendInput(1, new[] { down }, Marshal.SizeOf(typeof(INPUT))); System.Threading.Thread.Sleep(60); SendInput(1, new[] { up }, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80); SetCursorPos(old.X, old.Y); return "clicked"; }
}
"@
[VP]::Aware()
function Find([int]$hostPid, [string]$aid, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $hostPid)
  foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', $cond)) {
    $c = if ($aid) { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $aid) } else { New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name) }
    $e = $w.FindFirst('Descendants', $c); if ($e) { return $e } }
  return $null }
function WaitFind([int]$hostPid, [string]$aid, [string]$name, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $e = Find $hostPid $aid $name; if ($e) { return $e }; Start-Sleep -Milliseconds 200 }; return $null }
function ClickEl($e, [int]$hostPid) {   # a real mouse click at the element's centre, our window in front
  if (!$e) { return 'no element' }
  $r = $e.Current.BoundingRectangle; if ($r.Width -le 0) { return 'no bounds' }
  return [VP]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), [uint32]$hostPid) }
$script:cid = 0
function Page { for ($i = 0; $i -lt 60; $i++) { try { $t = (Invoke-RestMethod "http://127.0.0.1:$cdp/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -Last 1; if ($t) { break } } catch {}; Start-Sleep -Milliseconds 300 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws }
function Send($ws, [string]$method, $params) { $script:cid++; $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $script:cid; method = $method; params = $params } | ConvertTo-Json -Depth 6 -Compress)); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 262144; while ($true) { $sb = New-Object Text.StringBuilder; do { $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$r.Wait(10000)) { return $null }; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)) } while (!$r.Result.EndOfMessage); $j = $sb.ToString() | ConvertFrom-Json; if ($j.id -eq $script:cid) { return $j } } }
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function Navigate([string]$path) { $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port$path" }); $w.Dispose(); Start-Sleep 3; return (Page) }

function Build([string]$source, [string]$dir) {
  if ($SkipBuild -and (Test-Path "$dir\browser.exe")) {   # reuse the host, fresh synthetic profile
    Get-ChildItem $dir -Force | Where-Object { @('browser.exe', 'WebView2Loader.dll', 'assets') -notcontains $_.Name } | Remove-Item -Recurse -Force
    [IO.File]::WriteAllText((Join-Path $dir 'profiles.txt'), "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`n"); return }
  Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $dir | Out-Null
  & (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' $source src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$dir\browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
  if ($LASTEXITCODE -ne 0) { throw "host build failed: $source" }
  Copy-Item (Join-Path $root 'deps\webview2\build\native\x64\WebView2Loader.dll') $dir; Copy-Item (Join-Path $root 'assets') "$dir\assets" -Recurse
  [IO.File]::WriteAllText((Join-Path $dir 'profiles.txt'), "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`n") }
function HostPids([string]$exe) { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe } | ForEach-Object ProcessId }
function StopHost([string]$exe, [string]$dir) {
  foreach ($p in HostPids $exe) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  for ($i = 0; $i -lt 60; $i++) { $left = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$dir*" }); if (!$left.Count) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
function StartHost([string]$exe, [string]$dir) {
  Start-Process -FilePath $exe -WorkingDirectory $dir; $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = HostPids $exe | Select-Object -First 1 }; return $hp }
function WaitLog([string]$log, [string]$pattern, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
function LoginDataHits([string]$dir) {
  $ld = @(Get-ChildItem $dir -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'Login Data*' })
  $hits = @($ld | Where-Object { $b = [IO.File]::ReadAllBytes($_.FullName); [Text.Encoding]::UTF8.GetString($b).Contains($user) -or [Text.Encoding]::Unicode.GetString($b).Contains($user) })
  return @{ files = $ld.Count; hits = $hits.Count } }
# Row counts of the engine's password store for the synthetic username (logins: saved credentials; stats: its save-prompt
# statistics), read on a copy with sqlite3, no password column read. A raw byte search is not enough: a « stats » row
# alone (the engine only offered to save) holds the username too (vault-coverage finding).
function EngineRows([string]$dir) {
  $db = Get-ChildItem $dir -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'Login Data' } | Select-Object -First 1
  if (!$db) { return @{ text = 'no Login Data'; logins = -1; stats = -1 } }
  $copy = Join-Path $pki 'login-data-copy.db'; Copy-Item $db.FullName $copy -Force
  $py = "import sqlite3,sys;c=sqlite3.connect(sys.argv[1]);u=sys.argv[2];print('%d %d'%(c.execute('select count(*) from logins where username_value=?',(u,)).fetchone()[0],c.execute('select count(*) from stats where username_value=?',(u,)).fetchone()[0]))"
  $r = (& python -I -c $py $copy $user) -split ' '; Remove-Item $copy -Force
  return @{ text = "logins=$($r[0]) stats=$($r[1])"; logins = [int]$r[0]; stats = [int]$r[1] } }
function SubmitLogin($hostPid) {
  $w = Navigate '/login'
  # Typed as trusted input (CDP Input.insertText into the focused field, like a keyboard), then submitted.
  [void](Eval $w "document.getElementById('u').focus();1"); [void](Send $w 'Input.insertText' @{ text = $user })
  [void](Eval $w "document.getElementById('p').focus();1"); [void](Send $w 'Input.insertText' @{ text = $secret })
  # Review V-4: the click is trusted too (DevTools Input domain): a login counts only after a real user action.
  $r = Eval $w "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($r -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $w 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }
  $w.Dispose() }

New-Item -ItemType Directory -Force $pki | Out-Null
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
& $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
& $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nsha256=$((Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower())`n")
$prodDir = Join-Path $root 'build\e2e-vault-product'; $ctlDir = Join-Path $root 'build\e2e-vault-product-control'
$prodExe = Join-Path $prodDir 'browser.exe'; $ctlExe = Join-Path $ctlDir 'browser.exe'
foreach ($e in @($prodExe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force } }
if (!$ControlOnly) { Build 'tests/e2e/vault_product_host.cpp' $prodDir }
if (!$NoControl) { Build 'tests/e2e/mtls_host.cpp' $ctlDir }
$server = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port", '--no-client-auth' -PassThru -WindowStyle Hidden
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$cdp"
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  # ---- Product path, vault active ----------------------------------------------------------------
  if (!$ControlOnly) {
  $log = Join-Path $prodDir 'prototype.log'; $vaultFile = Join-Path $prodDir 'passwords'
  $hp = StartHost $prodExe $prodDir
  Check (WaitLog $log 'E2E BOUND certificate decisions cleared' 20000) 'product-path host ready'
  $l = Get-Content $log -Raw -ErrorAction SilentlyContinue
  Check ($l -match 'VAULT state: active' -and $l -match 'E2E VAULT ACTIVE' -and $l -notmatch 'E2E VAULT HOST') 'product path: « VAULT state: active », no lifted foreground condition (not the vault test host)'
  Check ($l -match 'SETTINGS engine password autosave off' -and $l -notmatch 'SETTINGS engine password autosave on') 'product path: the engine reports its own password autosave OFF (read back from the engine)'
  $hwnd = (Get-Process -Id $hp).MainWindowHandle
  $front = [VP]::Front($hwnd, [uint32]$hp)
  Check $front 'the host window is in the foreground (required by the product path)'
  # B04: capture -> armed prompt -> real click on « Enregistrer »
  SubmitLogin $hp
  $save = WaitFind $hp 'PasswordSaveConfirm' ''
  Check ([bool]$save) 'B04: the real save prompt appears after the submit'
  Check (-not (Test-Path $vaultFile)) 'nothing written before the explicit choice'
  # (No new foreground request here: the window is already in front, and deactivating it would dismiss the anchored prompt.)
  if ($save) { Start-Sleep -Milliseconds 700; "     foreground is the host's: $([VP]::PidOf([VP]::GetForegroundWindow()) -eq $hp)"; $save = Find $hp 'PasswordSaveConfirm' ''; $c = ClickEl $save $hp; "     click « Enregistrer »: $c"; Start-Sleep 1 }
  $bytes = if (Test-Path $vaultFile) { [IO.File]::ReadAllText($vaultFile) } else { '' }
  Check ($bytes.StartsWith("browser-passwords 2`n") -and $bytes -match "`nend`t1`t[0-9a-f]{64}`n$" -and -not $bytes.Contains($secret) -and -not $bytes.Contains($user)) 'B04 product path: saved after a real click in the foreground window (v2, one account, no plaintext)'
  Check (WaitLog $log 'VAULT saved' 3000) 'B04 product path: the host logs the save'
  # B03: real click on the native key, then on the account row; the fill callback's foreground check is in force.
  $ws = Navigate '/login'; [void](Eval $ws "document.getElementById('p').focus();1")
  [void][VP]::Front($hwnd, [uint32]$hp)
  $key = WaitFind $hp 'VaultFill' ''
  Check ([bool]$key) 'B03: the native key control is shown for an origin with a saved account'
  $filled = $false
  if ($key) {
    Start-Sleep -Milliseconds 800; $c = ClickEl (Find $hp 'VaultFill' '') $hp; "     click on the key: $c"
    $row = WaitFind $hp '' $user 5000
    Check ([bool]$row) 'B03: a real click on the armed key opens the account list'
    if ($row) { Start-Sleep -Milliseconds 800; $c = ClickEl (Find $hp '' $user) $hp; "     click on the account row: $c"
      for ($i = 0; $i -lt 20 -and !$filled; $i++) { Start-Sleep -Milliseconds 200; $filled = (Eval $ws "document.getElementById('p').value") -eq $secret -and (Eval $ws "document.getElementById('u').value") -eq $user } }
  }
  Check ($filled -and (WaitLog $log 'VAULT filled \(user action, same document, confirmed by the page\)' 3000)) 'B03 product path: the focused form is filled after real clicks, foreground checks in force'
  if (!$filled) { Get-Content $log | Where-Object { $_ -match 'VAULT|PROMPT' } | Select-Object -Last 12 | ForEach-Object { "     log: $_" } }
  $ws.Dispose()
  Check (StopHost $prodExe $prodDir) 'product path: every engine process of the host has exited'
  $ld = LoginDataHits $prodDir; $pr = EngineRows $prodDir
  "     product path: engine Login Data files $($ld.files), raw bytes holding the synthetic account: $($ld.hits); rows: $($pr.text)"
  Check ($ld.files -gt 0 -and $ld.hits -eq 0 -and $pr.logins -eq 0 -and $pr.stats -eq 0) 'product path: the engine''s own password store did not record the account (engine autosave off while ours is active)'
  }
  # ---- Control: the same submit on the vault-INACTIVE fixture host (engine autosave on) ------------
  if (!$NoControl) {
    $clog = Join-Path $ctlDir 'prototype.log'
    $cp = StartHost $ctlExe $ctlDir
    Check (WaitLog $clog 'E2E BOUND certificate decisions cleared' 20000) 'control host ready'
    Check ((Get-Content $clog -Raw) -match 'VAULT state: inactive' -and (Get-Content $clog -Raw) -match 'SETTINGS engine password autosave on') 'control: vault inactive there, the engine reports its own password autosave ON'
    SubmitLogin $cp; Start-Sleep 4
    Check (-not (Find $cp 'PasswordSaveConfirm' '')) 'control: no B04 (vault inactive)'
    [void](StopHost $ctlExe $ctlDir)
    $cl = LoginDataHits $ctlDir; $cr = EngineRows $ctlDir
    "     control: engine Login Data files $($cl.files), raw bytes holding the synthetic account: $($cl.hits); rows: $($cr.text) (the engine saves the credential itself only after its own save prompt, which this fixture never shows)"
    Check ($cl.hits -gt 0 -and ($cr.logins + $cr.stats) -gt 0) 'control: with our vault inactive the engine records the same login in its password store (the check can see it)'
  }
} finally {
  foreach ($e in @($prodExe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL vault product-path e2e: $script:fails"; exit 1 } else { 'PASS vault product-path e2e (production code, vault forced active, foreground checks in force, real clicks): B04 save, B03 key + row fill, engine autosave off (control: on when our vault is inactive)' }
