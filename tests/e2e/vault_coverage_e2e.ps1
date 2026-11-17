param([string]$Runtime = '', [switch]$SkipBuild)
# Vault CAPTURE COVERAGE and ENGINE-PASSWORD MIGRATION on the PRODUCT path (owner decisions A and B).
# Host: tests/e2e/vault_product_host.cpp (production code, vault forced active, every foreground condition in force,
# fixture pin for the local HTTPS fixture only). Control: tests/e2e/mtls_host.cpp (vault inactive, the engine's own
# password autosave on) is used ONCE to let the engine save a synthetic login, so the migration has something real to
# migrate. Proves, each with a real « Enregistrer » click in the foreground window and the vault file read back
# (DPAPI, this Windows user, synthetic secrets only):
#   A. classic submit (control) / script-driven logins (click + fetch + field gone; Enter + fetch + navigation; a
#      failed fetch offers nothing) / same-origin and cross-origin frames (offer and entry for the FRAME's origin) /
#      two-step login (username of page 1 joined to the password of page 2) / change-password forms (the NEW password
#      is saved, with and without autocomplete hints; a confirm that differs offers nothing) / never for an opaque
#      (sandboxed) document, top-level or framed, even when it forges the old page channel (R-6) / never for a
#      password the vault already holds.
#   B. the activation date is recorded; 30 days later (clock injected through the recorded date, no waiting) the
#      engine's password store is cleared (its rows for the synthetic login are gone) and Réglages › Confidentialité
#      shows a one-time notice; « Effacer en quittant » says it deletes saved passwords. A password the ENGINE
#      autofilled is offered as « Garder ce mot de passe dans browser ? » when the engine has one to autofill (it only
#      saves after its own prompt, which this fixture cannot show: then the step is reported as not verified).
# Synthetic profiles under build/, local fixture servers on 127.0.0.1 only; only this fixture's processes are touched.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$root = (Resolve-Path '.').Path; $pki = Join-Path $root 'build\e2e-vault-coverage-pki'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$port = 18451; $port2 = 18452; $cdp = 9405
$mUser = 'e2e-cov-moteur'; $mSecret = 'E2e-Moteur-1001!'          # saved by the ENGINE (control host), then migrated
$sUser = 'e2e-cov-spa'; $sSecret = 'E2e-Spa-2002!'; $sNew = 'E2e-Spa-Nouveau-2112!'
$eUser = 'e2e-cov-enter'; $eSecret = 'E2e-Enter-3003!'
$fUser = 'e2e-cov-cadre'; $fSecret = 'E2e-Cadre-4004!'
$xUser = 'e2e-cov-autre'; $xSecret = 'E2e-Autre-5005!'
$tUser = 'e2e-cov-deux'; $tSecret = 'E2e-Deux-6006!'; $tNew = 'E2e-Deux-Nouveau-6116!'
$nUser = 'e2e-cov-sbx'; $nSecret = 'E2e-Sbx-7007!'   # frames: the fixture fills these itself (tests/e2e/mtls_server.py FILL)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Security
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class VC {
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
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
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
  public static string Click(int x, int y, uint pid) {
    var p = new POINT { X = x, Y = y }; var w = WindowFromPoint(p); var top = GetAncestor(w, 2);
    if (PidOf(top) != pid) { var cls = new System.Text.StringBuilder(64); GetClassName(top, cls, 64); return "refused: the window under the point is not the host's (" + cls + ", pid " + PidOf(top) + ")"; }
    POINT old; GetCursorPos(out old); SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    var down = new INPUT { type = 0, mi = new MI { flags = 0x0002 } }; var up = new INPUT { type = 0, mi = new MI { flags = 0x0004 } };
    SendInput(1, new[] { down }, Marshal.SizeOf(typeof(INPUT))); System.Threading.Thread.Sleep(60); SendInput(1, new[] { up }, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80); SetCursorPos(old.X, old.Y); return "clicked"; }
}
"@
[VC]::Aware()
$AE = [System.Windows.Automation.AutomationElement]
function Find([int]$hostPid, [string]$aid, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $hostPid)
  foreach ($w in $AE::RootElement.FindAll('Children', $cond)) {
    $c = if ($aid) { New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $aid) } else { New-Object System.Windows.Automation.PropertyCondition($AE::NameProperty, $name) }
    $e = $w.FindFirst('Descendants', $c); if ($e) { return $e } }
  return $null }
function WaitFind([int]$hostPid, [string]$aid, [string]$name, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $e = Find $hostPid $aid $name; if ($e) { return $e }; Start-Sleep -Milliseconds 200 }; return $null }
function ClickEl($e, [int]$hostPid) { if (!$e) { return 'no element' }; $r = $e.Current.BoundingRectangle; if ($r.Width -le 0) { return 'no bounds' }; return [VC]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), [uint32]$hostPid) }
function InvokeEl($e) { if (!$e) { return $false }; $p = $null; if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); return $true }; return $false }
$script:cid = 0
function Targets { try { return @((Invoke-RestMethod "http://127.0.0.1:$cdp/json") | ForEach-Object { $_ }) } catch { return @() } }
function Page { for ($i = 0; $i -lt 60; $i++) { $t = Targets | Where-Object { $_.type -eq 'page' } | Select-Object -Last 1; if ($t) { break }; Start-Sleep -Milliseconds 300 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws }
function Send($ws, [string]$method, $params) { $script:cid++; $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $script:cid; method = $method; params = $params } | ConvertTo-Json -Depth 6 -Compress)); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 262144; while ($true) { $sb = New-Object Text.StringBuilder; do { $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$r.Wait(10000)) { return $null }; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)) } while (!$r.Result.EndOfMessage); $j = $sb.ToString() | ConvertFrom-Json; if ($j.id -eq $script:cid) { return $j } } }
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function Navigate([string]$path) { $w = Page; [void](Send $w 'Page.navigate' @{ url = "https://127.0.0.1:$port$path" }); $w.Dispose(); Start-Sleep 3; return (Page) }
# Trusted input (the DevTools Input domain, like a person): typing, a click at an element's centre, Enter.
function TypeInto($ws, [string]$id, [string]$text) { [void](Eval $ws "document.getElementById('$id').focus();1"); [void](Send $ws 'Input.insertText' @{ text = $text }) }
function PageClick($ws, [string]$id) {
  $r = Eval $ws "(function(){var r=document.getElementById('$id').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"
  $x, $y = ($r -split ',') | ForEach-Object { [int]$_ }
  [void](Send $ws 'Input.dispatchMouseEvent' @{ type = 'mouseMoved'; x = $x; y = $y })
  [void](Send $ws 'Input.dispatchMouseEvent' @{ type = 'mousePressed'; x = $x; y = $y; button = 'left'; clickCount = 1 })
  [void](Send $ws 'Input.dispatchMouseEvent' @{ type = 'mouseReleased'; x = $x; y = $y; button = 'left'; clickCount = 1 }) }
function PageEnter($ws) { [void](Send $ws 'Input.dispatchKeyEvent' @{ type = 'keyDown'; key = 'Enter'; code = 'Enter'; windowsVirtualKeyCode = 13; nativeVirtualKeyCode = 13; text = "`r" }); [void](Send $ws 'Input.dispatchKeyEvent' @{ type = 'keyUp'; key = 'Enter'; code = 'Enter'; windowsVirtualKeyCode = 13; nativeVirtualKeyCode = 13 }) }

function Build([string]$source, [string]$dir) {
  if ($SkipBuild -and (Test-Path "$dir\browser.exe")) {
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
function StartHost([string]$exe, [string]$dir) { Start-Process -FilePath $exe -WorkingDirectory $dir; $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = HostPids $exe | Select-Object -First 1 }; return $hp }
function WaitLog([string]$log, [string]$pattern, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
function LoginDataHits([string]$dir, [string]$u) {
  $ld = @(Get-ChildItem $dir -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'Login Data*' })
  $hits = @($ld | Where-Object { $b = [IO.File]::ReadAllBytes($_.FullName); [Text.Encoding]::UTF8.GetString($b).Contains($u) -or [Text.Encoding]::Unicode.GetString($b).Contains($u) })
  return @{ files = $ld.Count; hits = $hits.Count } }
# The vault file read back: origin, user and the DPAPI secret (entropy « browser-vault-v1|<origin> », this user).
function VaultEntries([string]$file) {
  $out = @(); if (!(Test-Path $file)) { return $out }
  $lines = [IO.File]::ReadAllText($file) -split "`n"
  foreach ($l in $lines[1..($lines.Count - 1)]) { $f = $l -split "`t"; if ($f.Count -ne 6) { continue }
    $o = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($f[3])); $u = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($f[4]))
    $s = try { [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect([Convert]::FromBase64String($f[5]), [Text.Encoding]::UTF8.GetBytes("browser-vault-v1|$o"), 'CurrentUser')) } catch { '<undecryptable>' }
    $out += [pscustomobject]@{ origin = $o; user = $u; secret = $s } }
  return $out }
# The engine's own password store of a synthetic profile, as ROW COUNTS for one synthetic username (logins: saved
# credentials; stats: its save-prompt statistics). Read on a copy with sqlite3; no password column is read or decrypted.
function EngineRows([string]$dir, [string]$u) {
  $db = Get-ChildItem $dir -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'Login Data' } | Select-Object -First 1
  if (!$db) { return 'logins=? stats=?' }
  $copy = Join-Path $pki 'login-data-copy.db'; Copy-Item $db.FullName $copy -Force
  $py = "import sqlite3,sys;c=sqlite3.connect(sys.argv[1]);u=sys.argv[2];print('logins=%d stats=%d'%(c.execute('select count(*) from logins where username_value=?',(u,)).fetchone()[0],c.execute('select count(*) from stats where username_value=?',(u,)).fetchone()[0]))"
  $r = & python -I -c $py $copy $u; Remove-Item $copy -Force; return [string]$r }
function Holds([string]$file, [string]$origin, [string]$u, [string]$s) { return [bool](@(VaultEntries $file) | Where-Object { $_.origin -eq $origin -and $_.user -eq $u -and $_.secret -eq $s }) }
function NoOffer([int]$hp, [int]$ms = 4000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { if ((Find $hp 'PasswordSaveConfirm' '') -or (Find $hp 'PasswordLater' '')) { return $false }; Start-Sleep -Milliseconds 250 }; return $true }
# Closes a leftover offer (« Plus tard »: UI Automation first, a real click otherwise), so the next check starts clean.
function Dismiss([int]$hp) { for ($i = 0; $i -lt 3; $i++) { $l = Find $hp 'PasswordLater' ''; if (!$l) { return }; Start-Sleep -Milliseconds 700; if (!(InvokeEl $l)) { [void](ClickEl $l $hp) }; Start-Sleep -Milliseconds 600 } }
# A login ($act), then a B04 offer of kind $kind (group AutomationId PasswordSave / PasswordUpdate / PasswordKeep)
# accepted with a REAL click on its confirm button, then $ok must hold (the vault read back). The host window shares
# this desktop with the person using it: an anchored offer closes when its window is deactivated, and takes what is
# typed while it has the focus. An attempt spoiled that way is reported and run again, three attempts at most (never silently).
function Scenario([int]$hp, [string]$kind, [string]$label, [scriptblock]$act, [scriptblock]$ok) {
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    Dismiss $hp
    if ([VC]::PidOf([VC]::GetForegroundWindow()) -ne $hp) { [void][VC]::Front($script:hwnd, [uint32]$hp) }
    & $act
    $g = WaitFind $hp $kind '' 8000; $c = 'no offer'; $other = @()
    if ($g) { Start-Sleep -Milliseconds 700; for ($i = 0; $i -lt 4; $i++) { $c = ClickEl (Find $hp 'PasswordSaveConfirm' '') $hp; if ($c -eq 'clicked') { break }; if ($c -like 'refused*') { [void][VC]::Front($script:hwnd, [uint32]$hp) }; Start-Sleep -Milliseconds 700 }; Start-Sleep 1 }
    else { $other = @('PasswordSave', 'PasswordUpdate', 'PasswordKeep' | Where-Object { Find $hp $_ '' }) }
    if ($g -and (& $ok)) { Check $true "${label}: offer ($kind), real click, saved as expected$(if ($attempt -gt 1) { " (attempt $attempt)" })"; return }
    "     ${label}: attempt $attempt did not complete (offer shown: $([bool]$g)$(if ($other.Count) { ', another kind shown: ' + ($other -join ',') }); click: $c)"
    Dismiss $hp }
  Check $false "${label}: offer ($kind), real click, saved as expected" }

New-Item -ItemType Directory -Force $pki | Out-Null
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
& $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
& $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nport2=$port2`nsha256=$((Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower())`n")
$prodDir = Join-Path $root 'build\e2e-vault-coverage'; $ctlDir = Join-Path $root 'build\e2e-vault-coverage-control'
$prodExe = Join-Path $prodDir 'browser.exe'; $ctlExe = Join-Path $ctlDir 'browser.exe'
foreach ($e in @($prodExe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force } }
Build 'tests/e2e/vault_product_host.cpp' $prodDir
Build 'tests/e2e/mtls_host.cpp' $ctlDir
$servers = @((Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port", '--no-client-auth' -PassThru -WindowStyle Hidden), (Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port2", '--no-client-auth' -PassThru -WindowStyle Hidden))
# --isolate-origins + OriginKeyedProcessesByDefault: the second fixture origin (same host, another port) gets its own
# renderer process, so the cross-origin frame is an out-of-process frame, like a frame of another site on the web.
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$cdp --isolate-origins=https://127.0.0.1:$port2 --enable-features=OriginKeyedProcessesByDefault"
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
$O1 = "https://127.0.0.1:$port"; $O2 = "https://127.0.0.1:$port2"
try {
  # ---- 0. Control host (vault inactive): the ENGINE saves a synthetic login, the material of the migration ----------
  $clog = Join-Path $ctlDir 'prototype.log'; $cp = StartHost $ctlExe $ctlDir
  Check (WaitLog $clog 'E2E BOUND certificate decisions cleared' 20000) 'control host ready (vault inactive, engine autosave on)'
  $w = Navigate '/login'; TypeInto $w 'u' $mUser; TypeInto $w 'p' $mSecret; [void](Eval $w "document.getElementById('go').click();1"); $w.Dispose(); Start-Sleep 6
  [void](StopHost $ctlExe $ctlDir)
  $ctlRows = EngineRows $ctlDir $mUser
  "     control: the engine's password store for the synthetic login: $ctlRows (a saved credential is a « logins » row; WebView2 waits for its own save prompt, which this fixture never shows: only its statistics are written)"
  $script:engineSaved = $ctlRows -match 'logins=[1-9]'
  Check ($ctlRows -match 'stats=[1-9]') 'control: the engine recorded the synthetic login in its password store (the material the 30-day clearing must remove)'
  Copy-Item (Join-Path $ctlDir 'profile') (Join-Path $prodDir 'profile') -Recurse -Force   # the engine store the product host starts with

  # ---- Product path, vault active ---------------------------------------------------------------------------------
  $log = Join-Path $prodDir 'prototype.log'; $vf = Join-Path $prodDir 'passwords'; $mig = Join-Path $prodDir 'vault-engine-migration'
  $t0 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  $hp = StartHost $prodExe $prodDir
  Check (WaitLog $log 'E2E BOUND certificate decisions cleared' 20000) 'product-path host ready'
  Check ((Get-Content $log -Raw) -match 'VAULT state: active' -and (Get-Content $log -Raw) -match 'E2E VAULT ACTIVE') 'product path: vault active, foreground conditions in force'
  $hwnd = (Get-Process -Id $hp).MainWindowHandle; $script:hwnd = $hwnd; $script:oopif = $false
  $migText = if (Test-Path $mig) { [IO.File]::ReadAllText($mig) } else { '' }
  $act = [regex]::Match($migText, 'activated=(\d+)'); $actMs = if ($act.Success) { [int64]$act.Groups[1].Value } else { 0 }
  Check ($migText.StartsWith("browser-vault-migration 1`n") -and $actMs -ge $t0 - 5000 -and $actMs -le $t0 + 120000) 'B: the vault''s activation date is recorded in the profile at the first active start'
  Check (WaitLog $log 'VAULT capture coverage installed' 10000) 'A: the capture is installed in the isolated world of the tab (binding + new-document script)'

  # B. migration: when the engine autofills a password it saved, submitting it offers « Garder ce mot de passe dans browser ? ».
  # It needs a credential the ENGINE saved; WebView2 saves one only after its own save prompt, which never appears on
  # this fixture (see the control line above), so this step is reported, not asserted, when there is none.
  [void][VC]::Front($hwnd, [uint32]$hp)
  $w = Navigate '/login'; Start-Sleep 2
  $auto = Eval $w "(function(){var p=document.getElementById('p');try{return p.matches(':-webkit-autofill')}catch(e){return false}})()"; $w.Dispose()
  if ($script:engineSaved -or $auto -eq $true) {
    Scenario $hp 'PasswordKeep' 'B migration of an engine-autofilled password' { $w = Page; PageClick $w 'go'; $w.Dispose() } { Holds $vf $O1 $mUser $mSecret }
  } else {
    "     NOT VERIFIED here: the engine holds no saved credential to autofill (engine autofill: $auto); the migration wording is proven by modal_test and the offer by password_vault_test"
    Scenario $hp 'PasswordSave' 'A classic form submit (typed)' { $w = Navigate '/login'; TypeInto $w 'u' $mUser; TypeInto $w 'p' $mSecret; PageClick $w 'go'; $w.Dispose() } { Holds $vf $O1 $mUser $mSecret }
  }
  Dismiss $hp
  $w = Navigate '/login'; TypeInto $w 'u' $mUser; TypeInto $w 'p' $mSecret; PageClick $w 'go'; $w.Dispose()
  Check (NoOffer $hp) 'A: never an offer for a password the vault already holds unchanged'

  # A. script-driven: trusted click, fetch, the password field disappears; Enter, fetch, navigation; a failed fetch
  Scenario $hp 'PasswordSave' 'A script-driven login (click, fetch, field gone)' { $w = Navigate '/spa?mode=gone'; TypeInto $w 'u' $sUser; TypeInto $w 'p' $sSecret; PageClick $w 'go'; $w.Dispose() } { Holds $vf $O1 $sUser $sSecret }
  Scenario $hp 'PasswordSave' 'A script-driven login (Enter, fetch, navigation)' { $w = Navigate '/spa?mode=nav'; TypeInto $w 'u' $eUser; TypeInto $w 'p' $eSecret; PageEnter $w; $w.Dispose() } { Holds $vf $O1 $eUser $eSecret }
  Dismiss $hp
  $w = Navigate '/spa?mode=fail'; TypeInto $w 'u' 'e2e-cov-echec'; TypeInto $w 'p' 'E2e-Echec-0000!'; PageClick $w 'go'; $w.Dispose()
  Check (NoOffer $hp) 'A script-driven failure (the password field stays): no offer'
  $w = Navigate '/home'; $w.Dispose()   # the person goes elsewhere soon after the failure
  Check ((NoOffer $hp 3000) -and -not [bool](@(VaultEntries $vf) | Where-Object { $_.user -eq 'e2e-cov-echec' })) 'A script-driven failure, then the person leaves the page: still no offer'

  # A. frames: same origin, and another origin (offer and entry for the FRAME's origin, never the page's)
  Scenario $hp 'PasswordSave' 'A same-origin frame' { $w = Navigate '/frame?fill=cadre'; $w.Dispose() } { Holds $vf $O1 $fUser $fSecret }
  Scenario $hp 'PasswordSave' 'A cross-origin frame (entry for the FRAME''s origin, never the page''s)' { $w = Navigate "/frame?cross=$port2&fill=autre"; $w.Dispose(); $script:oopif = $script:oopif -or (@(Targets | Where-Object { $_.type -eq 'iframe' -and $_.url -like "$O2/*" }).Count -gt 0) } { (Holds $vf $O2 $xUser $xSecret) -and -not [bool](@(VaultEntries $vf) | Where-Object { $_.user -eq $xUser -and $_.origin -eq $O1 }) }
  "     cross-origin frame ran out of process (its own DevTools target): $script:oopif"
  Check $script:oopif 'A cross-origin frame: the out-of-process frame path was exercised (auto-attached before it ran)'

  # A. never for an opaque (sandboxed) document: a sandboxed frame, and a top-level CSP-sandboxed page forging the old channel (R-6)
  Dismiss $hp
  $w = Navigate "/frame?sbx&fill=sbx"; $w.Dispose()
  Check (NoOffer $hp 5000) 'A sandboxed (opaque) frame of the site: no offer'
  $w = Navigate "/sbx-forge?fill=sbx"; $w.Dispose()
  Check (NoOffer $hp 5000) 'R-6: an opaque top-level page served at the site''s URL (forging the page channel, then submitting): no offer'
  Check (-not [bool](@(VaultEntries $vf) | Where-Object { $_.user -eq $nUser })) 'R-6: nothing of the opaque pages reached the vault'

  # A. two-step: username on page 1, password on page 2 of the same origin (B04 refuses to save without a username,
  # so the saved account proves the first page's username was joined)
  Scenario $hp 'PasswordSave' 'A two-step login (username page 1, password page 2)' { $w = Navigate '/step1'; TypeInto $w 'u' $tUser; PageClick $w 'go'; $w.Dispose(); Start-Sleep 3; $w = Page; TypeInto $w 'p' $tSecret; PageClick $w 'go'; $w.Dispose() } { Holds $vf $O1 $tUser $tSecret }

  # A. change-password: the NEW password is saved (hints), then without hints (current, new, confirm); a confirm that differs offers nothing
  Scenario $hp 'PasswordUpdate' 'A change-password form (autocomplete hints): the NEW password, never the current one' { $w = Navigate '/change'; TypeInto $w 'u' $sUser; TypeInto $w 'cur' $sSecret; TypeInto $w 'new' $sNew; TypeInto $w 'conf' $sNew; PageClick $w 'go'; $w.Dispose() } { (Holds $vf $O1 $sUser $sNew) -and -not (Holds $vf $O1 $sUser $sSecret) }
  Scenario $hp 'PasswordUpdate' 'A change-password form (no hints: current, new, confirm): the NEW password' { $w = Navigate '/change?plain'; TypeInto $w 'u' $tUser; TypeInto $w 'cur' $tSecret; TypeInto $w 'new' $tNew; TypeInto $w 'conf' $tNew; PageClick $w 'go'; $w.Dispose() } { Holds $vf $O1 $tUser $tNew }
  Dismiss $hp
  $w = Navigate '/change'; TypeInto $w 'u' $sUser; TypeInto $w 'cur' $sNew; TypeInto $w 'new' 'E2e-Un-0001!'; TypeInto $w 'conf' 'E2e-Deux-0002!'; PageClick $w 'go'; $w.Dispose()
  Check (NoOffer $hp) 'A change-password whose confirmation differs: no offer'
  $entries = @(VaultEntries $vf); "     vault now holds $($entries.Count) account(s): " + (($entries | ForEach-Object { "$($_.user)@$($_.origin -replace 'https://','')" }) -join ', ')
  $raw = if (Test-Path $vf) { [IO.File]::ReadAllText($vf) } else { '' }
  Check (-not ($raw.Contains($sSecret) -or $raw.Contains($sNew) -or $raw.Contains($tUser) -or $raw.Contains($mSecret))) 'no plaintext secret or username in the vault file'
  $plog = Get-Content $log -Raw
  Check (-not ($plog.Contains($sSecret) -or $plog.Contains($sNew) -or $plog.Contains($xSecret) -or $plog.Contains($tSecret) -or $plog.Contains($mSecret))) 'no secret in the host log'

  # « Effacer en quittant » says what it really deletes (the engine's saved passwords and forms too)
  [void](InvokeEl (WaitFind $hp 'Settings' '' 5000)); [void](InvokeEl (WaitFind $hp '' 'Confidentialité' 5000)); Start-Sleep -Milliseconds 800
  $coe = WaitFind $hp '' 'Effacer en quittant' 5000; $help = if ($coe) { $coe.Current.HelpText } else { '' }
  "     « Effacer en quittant » note: $help"
  Check ($help -match 'mots de passe') '« Effacer en quittant »: the note says saved passwords are deleted too'
  Check (-not (Find $hp 'SettingsVaultNotice' '')) 'B: no engine-cleared notice before the 30 days'
  $w = Page; $w.Dispose()
  Check (StopHost $prodExe $prodDir) 'product host stopped (engine exited)'

  # ---- B. 30 days later (clock injected through the recorded activation date) -----------------------------------
  $day = 24L * 3600 * 1000; $old = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 31 * $day
  [IO.File]::WriteAllText($mig, ([IO.File]::ReadAllText($mig) -replace 'activated=\d+', "activated=$old"))
  $hp = StartHost $prodExe $prodDir; $hwnd = (Get-Process -Id $hp).MainWindowHandle
  Check (WaitLog $log 'E2E BOUND certificate decisions cleared' 20000) 'product host restarted with an activation date 31 days old'
  Check (WaitLog $log 'VAULT engine saved passwords cleared \(30 days after activation\)' 15000) 'B: after 30 days the engine''s saved passwords are cleared (ClearBrowsingData PASSWORD_AUTOSAVE)'
  $migText = [IO.File]::ReadAllText($mig)
  Check ($migText -match "cleared=[1-9]\d*" -and $migText -match "activated=$old") 'B: the clearing is recorded (done once)'
  [void](InvokeEl (WaitFind $hp 'Settings' '' 5000)); [void](InvokeEl (WaitFind $hp '' 'Confidentialité' 5000))
  $notice = WaitFind $hp 'SettingsVaultNotice' '' 5000
  "     notice: $(if ($notice) { $notice.Current.Name } else { '(none)' })"
  Check ($notice -and $notice.Current.Name -match '30') 'B: Réglages › Confidentialité › Mots de passe shows the one-time notice'
  Check (Holds $vf $O1 $mUser $mSecret) 'B: the vault keeps its passwords when the engine''s store is cleared'
  [void](StopHost $prodExe $prodDir)
  $hp = StartHost $prodExe $prodDir
  [void](WaitLog $log 'E2E BOUND certificate decisions cleared' 20000)
  [void](InvokeEl (WaitFind $hp 'Settings' '' 8000)); [void](InvokeEl (WaitFind $hp '' 'Confidentialité' 5000)); Start-Sleep 1
  Check ((WaitFind $hp '' 'Effacer en quittant' 5000) -and -not (Find $hp 'SettingsVaultNotice' '')) 'B: the notice is shown once (gone at the next start)'
  Check ((Select-String -Path $log -Pattern 'VAULT engine saved passwords cleared' -AllMatches).Matches.Count -eq 1) 'B: the engine store is cleared once, never again'
  [void](StopHost $prodExe $prodDir)
  $after = EngineRows $prodDir $mUser; "     engine password store for the synthetic login after the clearing: $after (before: $ctlRows)"
  "     (the engine reported the PASSWORD_AUTOSAVE clearing as done; this fixture holds no engine CREDENTIAL to watch disappear, and the engine keeps its save-prompt statistics row: NOT VERIFIED on a real saved credential)"
} finally {
  foreach ($e in @($prodExe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
  foreach ($s in $servers) { if ($s -and !$s.HasExited) { Stop-Process -Id $s.Id -Force } }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL vault coverage e2e: $script:fails"; exit 1 } else { 'PASS vault coverage e2e (product path): classic and script-driven logins, same- and cross-origin (out-of-process) frames, two-step, change-password, opaque documents refused (R-6), already-held refused, activation date, 30-day engine clearing with a one-time notice, honest clear-on-exit note (engine-autofill migration: see the NOT VERIFIED line when shown)' }
