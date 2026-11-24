param([string]$Runtime = '', [switch]$SkipBuild, [switch]$KeepEvidence)
# #121 NIGHT QA (real windows: run only in the night round, never in the daytime). Our list under a page field, the
# merged list, our validation bubble and the key pill, on the PRODUCT code (tests/e2e/vault_product_host.cpp: main.cpp
# with only the fixture pin for the local HTTPS server), compared with the Paper boards (tests/e2e/autofill-reference,
# Paper exports) and attacked by the hostile pages of tests/e2e/autofill_server.py (contract P1-P16:
# design/security-review-autofill-under-field.md).
# Typing goes through the DevTools Input domain (trusted keys, like a keyboard); clicks are real SendInput clicks, only
# at points whose window belongs to the host process. Synthetic profile in build\, fictitious accounts and e-mails,
# local fixture server on 127.0.0.1 only; only processes whose image is this fixture's host are touched.
# Evidence: build\autofill-qa\ (captures, diffs against Paper, the host log excerpt).
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'job_guard.ps1')   # every process started here dies with this script, even interrupted
$root = (Resolve-Path '.').Path; $pki = Join-Path $root 'build\e2e-autofill-pki'; $evidence = Join-Path $root 'build\autofill-qa'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$port = 18451; $cdp = 9405; $origin = "https://127.0.0.1:$port"
$account = 'val@atelier-lumen.fr'; $secret = 'E2e-Fictif-Autofill-1!'; $info = 'valentin.m@example.com'
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Drawing; using System.Drawing.Imaging;
public static class AQ {
  [DllImport("user32.dll")] static extern IntPtr SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint f);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
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
  // The visible top-level window of class `cls` owned by process `pid` (our list / bubble), else zero.
  public static IntPtr Find(string cls, uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { var s = new StringBuilder(128); GetClassName(h, s, 128); if (s.ToString() == cls && PidOf(h) == pid && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found; }
  // The engine's page surface (the WebView child of the host window): its screen origin maps CSS px.
  public static RECT PageRect(IntPtr top) {
    RECT best = new RECT(); int area = 0;
    EnumChildWindows(top, (h, l) => { var s = new StringBuilder(128); GetClassName(h, s, 128); if (s.ToString() == "Chrome_RenderWidgetHostHWND" && IsWindowVisible(h)) { RECT r; GetWindowRect(h, out r); int a = (r.R - r.L) * (r.B - r.T); if (a > area) { area = a; best = r; } } return true; }, IntPtr.Zero);
    return best; }
  public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  public static string Click(int x, int y, uint pid) {
    var p = new POINT { X = x, Y = y }; var w = WindowFromPoint(p); var top = GetAncestor(w, 3);
    if (PidOf(top) != pid && PidOf(w) != pid) return "refused: the window under the point is not the host's";
    POINT old; GetCursorPos(out old); SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    var down = new INPUT { type = 0, mi = new MI { flags = 0x0002 } }; var up = new INPUT { type = 0, mi = new MI { flags = 0x0004 } };
    SendInput(1, new[] { down }, Marshal.SizeOf(typeof(INPUT))); System.Threading.Thread.Sleep(60); SendInput(1, new[] { up }, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80); SetCursorPos(old.X, old.Y); return "clicked"; }
  public static void Capture(RECT r, string path) {
    using (var b = new Bitmap(r.R - r.L, r.B - r.T, PixelFormat.Format32bppArgb)) { using (var g = Graphics.FromImage(b)) g.CopyFromScreen(r.L, r.T, 0, 0, b.Size); b.Save(path, ImageFormat.Png); } }
}
"@
[AQ]::Aware()
$script:cid = 0
function Page { for ($i = 0; $i -lt 60; $i++) { try { $t = (Invoke-RestMethod "http://127.0.0.1:$cdp/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' } | Select-Object -Last 1; if ($t) { break } } catch {}; Start-Sleep -Milliseconds 300 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws }
function Send($ws, [string]$method, $params) {
  $script:cid++; $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $script:cid; method = $method; params = $params } | ConvertTo-Json -Depth 6 -Compress))
  $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 262144
  while ($true) { $sb = New-Object Text.StringBuilder
    do { $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$r.Wait(10000)) { return $null }; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)) } while (!$r.Result.EndOfMessage)
    $m = $sb.ToString() | ConvertFrom-Json; if ($m.id -eq $script:cid) { return $m } } }
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function Navigate([string]$path) { $w = Page; [void](Send $w 'Page.navigate' @{ url = "$origin$path" }); $w.Dispose(); Start-Sleep 2; return (Page) }
# Trusted keys (DevTools Input domain): a key down with its text, then up, for each character.
function TypeText($ws, [string]$text) { foreach ($c in $text.ToCharArray()) { $s = [string]$c
  [void](Send $ws 'Input.dispatchKeyEvent' @{ type = 'keyDown'; key = $s; text = $s; unmodifiedText = $s }); [void](Send $ws 'Input.dispatchKeyEvent' @{ type = 'keyUp'; key = $s }); Start-Sleep -Milliseconds 60 } }
function Key($ws, [string]$key, [int]$vk) { $text = if ($key -eq 'Enter') { "`r" } else { '' }
  $down = @{ type = $(if ($text) { 'keyDown' } else { 'rawKeyDown' }); key = $key; code = $key; windowsVirtualKeyCode = $vk; nativeVirtualKeyCode = $vk }; if ($text) { $down.text = $text }
  [void](Send $ws 'Input.dispatchKeyEvent' $down); [void](Send $ws 'Input.dispatchKeyEvent' @{ type = 'keyUp'; key = $key; code = $key; windowsVirtualKeyCode = $vk; nativeVirtualKeyCode = $vk }); Start-Sleep -Milliseconds 120 }
function FocusField($ws, [string]$id) {   # a real click in the field (trusted), so the page focuses it as a person would
  $r = Eval $ws "(function(){var e=document.getElementById('$id');e.scrollIntoView({block:'center'});var r=e.getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"
  $x, $y = ($r -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) } ; Start-Sleep -Milliseconds 150 }
function FieldScreen($ws, [string]$id, $hwnd) {   # the field's rectangle in screen px (CSS px x devicePixelRatio + the page surface origin)
  $j = Eval $ws "(function(){var r=document.getElementById('$id').getBoundingClientRect();return JSON.stringify({x:r.left,y:r.top,w:r.width,h:r.height,d:devicePixelRatio});})()" | ConvertFrom-Json
  $p = [AQ]::PageRect($hwnd); return @{ L = [int][Math]::Round($p.L + $j.x * $j.d); T = [int][Math]::Round($p.T + $j.y * $j.d); R = [int][Math]::Round($p.L + ($j.x + $j.w) * $j.d); B = [int][Math]::Round($p.T + ($j.y + $j.h) * $j.d); D = $j.d } }
function WaitWindow([string]$cls, [uint32]$hostPid, [int]$ms = 3000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $h = [AQ]::Find($cls, $hostPid); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 100 }; return [IntPtr]::Zero }
function Gone([string]$cls, [uint32]$hostPid, [int]$ms = 1500) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { if ([AQ]::Find($cls, $hostPid) -eq [IntPtr]::Zero) { return $true }; Start-Sleep -Milliseconds 100 }; return $false }
function WaitLog([string]$log, [string]$pattern, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
function ScaleName([uint32]$dpi) { if ($dpi -eq 96) { '1x' } elseif ($dpi -eq 192) { '2x' } else { '' } }
# Paper comparison of a capture: exact at 100 % / 200 %, resampled with looser limits at another scale (said in the line).
function Compare-Board([string]$capture, [string]$board, [uint32]$dpi, [double]$mean, [double]$strong) {
  $s = ScaleName $dpi
  if ($s) { & python tests/e2e/compare_png.py $capture "tests/e2e/autofill-reference/$board-$s.png" --limit-mean $mean --limit-strong $strong --diff ($capture -replace '\.png$', '-diff.png') | Out-Host }
  else { Write-Host "     scale $dpi DPI: compared after resampling to the Paper 1x export (looser limits)"; & python tests/e2e/compare_png.py $capture "tests/e2e/autofill-reference/$board-1x.png" --resize --limit-mean ($mean * 2) --limit-strong ($strong * 2.5) --diff ($capture -replace '\.png$', '-diff.png') | Out-Host }
  return $LASTEXITCODE -eq 0 }

# ---- setup -------------------------------------------------------------------------------------------------------------
$hostDir = Join-Path $root 'build\e2e-autofill'; $hostExe = Join-Path $hostDir 'browser.exe'; $log = Join-Path $hostDir 'prototype.log'
function HostPids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $hostExe } | ForEach-Object ProcessId }
foreach ($p in HostPids) { Stop-Process -Id $p -Force }
Remove-Item $evidence -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $evidence, $pki | Out-Null
if (!($SkipBuild -and (Test-Path $hostExe))) {
  Remove-Item $hostDir -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $hostDir | Out-Null
  & (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' tests/e2e/vault_product_host.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o $hostExe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
  if ($LASTEXITCODE -ne 0) { throw 'host build failed' }
  Copy-Item (Join-Path $root 'deps\webview2\build\native\x64\WebView2Loader.dll') $hostDir; Copy-Item (Join-Path $root 'assets') "$hostDir\assets" -Recurse
} else { Get-ChildItem $hostDir -Force | Where-Object { @('browser.exe', 'WebView2Loader.dll', 'assets') -notcontains $_.Name } | Remove-Item -Recurse -Force }
[IO.File]::WriteAllText((Join-Path $hostDir 'profiles.txt'), "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`n")
$seed = Join-Path $evidence 'autofill_seed.exe'
& (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -static -municode -Isrc tests/e2e/autofill_seed.cpp -o $seed -lshell32 -lole32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'seed helper build failed' }
& $seed $hostDir $origin '-' '-' $info $account
Check ($LASTEXITCODE -eq 0) 'synthetic profile seeded through the production stores (two typed e-mails, no account yet: board 1 has none)'
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
& $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
& $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nsha256=$((Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower())`n")
$server = Start-Process python -ArgumentList 'tests/e2e/autofill_server.py', $pki, "$port" -PassThru -WindowStyle Hidden
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$cdp"
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
try {
  Start-Process -FilePath $hostExe -WorkingDirectory $hostDir; $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = HostPids | Select-Object -First 1 }
  Check (WaitLog $log 'E2E BOUND certificate decisions cleared' 20000) 'host ready (product code, fixture pin only)'
  Check (WaitLog $log 'FORMS world installed' 10000) 'the forms world is installed in the tab (P16)'
  $hwnd = (Get-Process -Id $hp).MainWindowHandle; [void][AQ]::Front($hwnd, [uint32]$hp)
  $dpi = [AQ]::GetDpiForWindow($hwnd); "     window DPI: $dpi"

  # ---- 1. « Infos enregistrées · A » -----------------------------------------------------------------------------------
  $ws = Navigate '/polar'; FocusField $ws 'email'; TypeText $ws 'va'
  $list = WaitWindow 'BrowserFieldList' $hp
  Check ($list -ne [IntPtr]::Zero) 'board 1: typing « va » in an e-mail field opens OUR list (never the engine''s)'
  if ($list -ne [IntPtr]::Zero) {
    $r = [AQ]::Rect($list); $f = FieldScreen $ws 'email' $hwnd; $gap = [int][Math]::Round(8 * $dpi / 96)
    Check ([Math]::Abs($r.L - $f.L) -le 1 -and [Math]::Abs($r.T - ($f.B + $gap)) -le 1) "board 1: under the field, 8 DIP, left edges aligned (list $($r.L),$($r.T) field $($f.L),$($f.B))"
    Check ([Math]::Abs(($r.R - $r.L) - [Math]::Round(320 * $dpi / 96)) -le 1 -and [Math]::Abs(($r.B - $r.T) - [Math]::Round(157 * $dpi / 96)) -le 1) 'board 1: 320 x 157 DIP'
    # The board shows the first row hovered: the pointer goes there, then the list is captured.
    $k = $dpi / 96; [void][AQ]::SetCursorPos([int]($r.L + 160 * $k), [int]($r.T + (7 + 30 + 2 + 18) * $k)); Start-Sleep -Milliseconds 400
    [AQ]::Capture($r, "$evidence\infos.png"); Check (Compare-Board "$evidence\infos.png" 'infos' $dpi 7 4.5) 'board 1: pixels match the Paper board (hover on the first row, cross shown)'
    $keys = Eval $ws 'window.__keys.join(",")'; Check ($keys -eq 'v,a') 'the page got the two letters only (no key swallowed while typing)'
    # P7 then P9: a real click on the first row after arming fills the field.
    Start-Sleep -Milliseconds 600; $c = [AQ]::Click([int]($r.L + 160 * $k), [int]($r.T + 57 * $k), [uint32]$hp); "     click on the first row: $c"
    Start-Sleep -Milliseconds 400; Check ((Eval $ws "document.getElementById('email').value") -eq $info) 'board 1: a click on a row fills the field with that e-mail'
    Check (Gone 'BrowserFieldList' $hp) 'the list closes after the pick'
  }
  # Keyboard: ↓ then Entrée picks without the page receiving them; Échap closes.
  [void](Eval $ws "document.getElementById('email').value='';window.__keys=[];1"); FocusField $ws 'email'; TypeText $ws 'va'
  [void](WaitWindow 'BrowserFieldList' $hp); Start-Sleep -Milliseconds 650; Key $ws 'ArrowDown' 40; Key $ws 'Enter' 13; Start-Sleep -Milliseconds 400
  Check ((Eval $ws "document.getElementById('email').value") -eq $info -and (Eval $ws 'window.__submits') -eq 0) 'keyboard: ↓ then Entrée fills the first e-mail and does not submit the form'
  Check ((Eval $ws 'window.__keys.join(",")') -eq 'v,a') 'keyboard: ↓ and Entrée did not reach the page while the list was open'
  [void](Eval $ws "document.getElementById('email').value='';1"); FocusField $ws 'email'; TypeText $ws 'va'; [void](WaitWindow 'BrowserFieldList' $hp); Key $ws 'Escape' 27
  Check (Gone 'BrowserFieldList' $hp) 'keyboard: Échap closes the list'
  # P7: a click right after the list appears does nothing.
  [void](Eval $ws "document.getElementById('email').value='';1"); FocusField $ws 'email'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $k = $dpi / 96; [void][AQ]::Click([int]($r.L + 160 * $k), [int]($r.T + 57 * $k), [uint32]$hp); Start-Sleep -Milliseconds 300
    Check ((Eval $ws "document.getElementById('email').value") -eq 'va') 'P7: a click in the first 500 ms chooses nothing' }
  Key $ws 'Escape' 27
  # ---- States board (« Listes sous le champ + bulle · états »): ↓ chooses (cap « Entrée »), footer and cross hovered ----
  [void](Eval $ws "document.getElementById('email').value='';1"); FocusField $ws 'email'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $k = $dpi / 96; [void][AQ]::SetCursorPos([int]($r.R + 40), [int]($r.B + 40)); Start-Sleep -Milliseconds 650
    Key $ws 'ArrowDown' 40; Key $ws 'ArrowDown' 40; Start-Sleep -Milliseconds 300
    [AQ]::Capture($r, "$evidence\keyboard.png"); Check (Compare-Board "$evidence\keyboard.png" 'keyboard' $dpi 7.5 5) 'states: the row chosen with ↓ (cap « Entrée ») matches the board'
    [void][AQ]::SetCursorPos([int]($r.L + 160 * $k), [int]($r.T + 134 * $k)); Start-Sleep -Milliseconds 350
    Key $ws 'Escape' 27; [void](Eval $ws "document.getElementById('email').value='';1"); FocusField $ws 'email'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp
    if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list)
      [void][AQ]::SetCursorPos([int]($r.L + 160 * $k), [int]($r.T + 134 * $k)); Start-Sleep -Milliseconds 350
      [AQ]::Capture($r, "$evidence\footer-hover.png"); Check (Compare-Board "$evidence\footer-hover.png" 'footer-hover' $dpi 7.5 5) 'states: the footer hovered matches the board'
      [void][AQ]::SetCursorPos([int]($r.L + 295 * $k), [int]($r.T + 57 * $k)); Start-Sleep -Milliseconds 350
      [AQ]::Capture($r, "$evidence\cross-hover.png"); Check (Compare-Board "$evidence\cross-hover.png" 'cross-hover' $dpi 7.5 5) 'states: the cross hovered matches the board' }
    Key $ws 'Escape' 27 }
  # Suppr removes the chosen e-mail (after arming), the page never gets the key.
  [void](Eval $ws "document.getElementById('email').value='';window.__keys=[];1"); FocusField $ws 'email'; TypeText $ws 'val'; [void](WaitWindow 'BrowserFieldList' $hp); Start-Sleep -Milliseconds 650
  Key $ws 'ArrowDown' 40; Key $ws 'Delete' 46; Start-Sleep -Milliseconds 500
  Check (-not ([IO.File]::ReadAllText((Join-Path $hostDir 'form-entries'))).Contains([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($info))) -and (Eval $ws 'window.__keys.join(",")') -eq 'v,a,l') 'states: Suppr removes the chosen e-mail; ↓ and Suppr never reach the page'
  Key $ws 'Escape' 27; & $seed $hostDir $origin '-' '-' $info $account | Out-Null   # back for the next checks
  # « retirer »: the cross removes the e-mail from our file.
  [void](Eval $ws "document.getElementById('email').value='';1"); FocusField $ws 'email'; TypeText $ws 'valen'; $list = WaitWindow 'BrowserFieldList' $hp
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $k = $dpi / 96; $y = [int]($r.T + 57 * $k); [void][AQ]::SetCursorPos([int]($r.L + 160 * $k), $y); Start-Sleep -Milliseconds 650
    [void][AQ]::Click([int]($r.R - (1 + 6 + 4 + 14) * $k), $y, [uint32]$hp); Start-Sleep -Milliseconds 500
    $file = [IO.File]::ReadAllText((Join-Path $hostDir 'form-entries'))
    Check (-not $file.Contains([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($info)))) '« retirer »: the e-mail is removed from our file' }
  $ws.Dispose()

  # ---- 2. Saving what the person typed (P12) ----------------------------------------------------------------------------
  $ws = Navigate '/polar'; FocusField $ws 'email'; TypeText $ws 'nouveau.fictif@exemple.test'; Key $ws 'Escape' 27
  $go = Eval $ws "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($go -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }
  Check (WaitLog $log 'FORMS typed e-mail kept' 5000) 'P12: an e-mail typed and sent is kept'
  $ws.Dispose(); $ws = Navigate '/polar'; FocusField $ws 'email'; TypeText $ws 'nou'
  Check ((WaitWindow 'BrowserFieldList' $hp) -ne [IntPtr]::Zero) 'P12: it is offered the next time'; Key $ws 'Escape' 27; $ws.Dispose()
  $ws = Navigate '/hostile/planted'; FocusField $ws 'name'; TypeText $ws 'Camille'
  $go = Eval $ws "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($go -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }; Start-Sleep 2
  Check (-not ([IO.File]::ReadAllText((Join-Path $hostDir 'form-entries'))).Contains([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('planted@attaquant.test')))) 'T8: an e-mail the page planted is never kept'
  $ws.Dispose()

  # ---- 2b. States: the list ABOVE the field when there is no room below; nothing under a password field -----------
  $ws = Navigate '/bottom'; FocusField $ws 'email'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $f = FieldScreen $ws 'email' $hwnd
    Check ([Math]::Abs($r.B - ($f.T - [Math]::Round(8 * $dpi / 96))) -le 1) "states: no room below, the list opens above the field, 8 DIP (list bottom $($r.B), field top $($f.T))" } else { Check $false 'states: the list opens above a field at the bottom of the page' }
  Key $ws 'Escape' 27; $ws.Dispose()

  # ---- 3. « Liste réunie, comptes + infos · A » + identifier-only step ------------------------------------------------
  & $seed $hostDir $origin $account $secret $info | Out-Null   # the info removed above comes back for the merged board
  $ws = Navigate '/github'; FocusField $ws 'login'; TypeText $ws 'va'
  $list = WaitWindow 'BrowserFieldList' $hp
  Check ($list -ne [IntPtr]::Zero) 'board 2: the merged list opens under the identifier field'
  if ($list -ne [IntPtr]::Zero) {
    $r = [AQ]::Rect($list); $k = $dpi / 96
    Check ([Math]::Abs(($r.B - $r.T) - [Math]::Round(205 * $dpi / 96)) -le 1) 'board 2: 320 x 205 DIP (account on top, the e-mail that is an account not repeated)'
    [void][AQ]::SetCursorPos([int]($r.L + 160 * $k), [int]($r.T + (39 + 24) * $k)); Start-Sleep -Milliseconds 400
    [AQ]::Capture($r, "$evidence\merged.png"); Check (Compare-Board "$evidence\merged.png" 'merged' $dpi 7.5 5) 'board 2: pixels match the Paper board (account hovered)'
    Start-Sleep -Milliseconds 300; [void][AQ]::Click([int]($r.L + 160 * $k), [int]($r.T + 63 * $k), [uint32]$hp); Start-Sleep -Milliseconds 500
    Check ((Eval $ws "document.getElementById('login').value") -eq $account -and (WaitLog $log 'FORMS filled: an account''s username \(identifier step, no secret\)' 3000)) 'P10: identifier-only step: the account fills the username only, no secret decrypted'
    Check (-not ((Get-Content $log -Raw) -match 'VAULT fill not done')) 'the identifier step no longer says « Aucun champ de mot de passe visible » (#121 note)'
  }
  # Through the key pill: the identifier-only step gets the username too (no secret), as the merged list. The host
  # re-reads the vault at most every 2 s, at a rebuild of its toolbar (a navigation): the account seeded while it runs
  # shows its key only after that (as on main), so the page is opened again first.
  Start-Sleep -Milliseconds 2300; $ws.Dispose(); $ws = Navigate '/github'; Start-Sleep -Milliseconds 600
  [void](Eval $ws "document.getElementById('login').value='';1"); FocusField $ws 'login'; Start-Sleep -Milliseconds 300
  $kp = $null; for ($try = 0; $try -lt 15 -and !$kp; $try++) { if ($try) { Start-Sleep -Milliseconds 200 }; foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$hp)))) { $kp = $w.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'VaultFill'))); if ($kp) { break } } }
  if ($kp) { Start-Sleep -Milliseconds 700; $b = $kp.Current.BoundingRectangle; [void][AQ]::Click([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2), [uint32]$hp)
    $row = $null; for ($i = 0; $i -lt 25 -and !$row; $i++) { Start-Sleep -Milliseconds 200; foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$hp)))) { $row = $w.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $account))); if ($row) { break } } }
    if ($row) { Start-Sleep -Milliseconds 700; $b = $row.Current.BoundingRectangle; [void][AQ]::Click([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2), [uint32]$hp) } }
  "     key pill found: $([bool]$kp), its list row found: $([bool]$row)"
  Start-Sleep -Milliseconds 800
  Check ((Eval $ws "document.getElementById('login').value") -eq $account -and (WaitLog $log 'VAULT filled the username only \(identifier step, no secret decrypted\)' 3000)) 'key pill on an identifier-only step: the account''s username is filled, no « Aucun champ de mot de passe visible »'
  $ws.Dispose(); $ws = Navigate '/github-login'; FocusField $ws 'p'; TypeText $ws 'va'; Start-Sleep -Milliseconds 600
  Check (([AQ]::Find('BrowserFieldList', [uint32]$hp)) -eq [IntPtr]::Zero) 'states: no list under a password field'
  $ws.Dispose(); $ws = Navigate '/github-login'; FocusField $ws 'login'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $k = $dpi / 96; Start-Sleep -Milliseconds 650; [void][AQ]::Click([int]($r.L + 160 * $k), [int]($r.T + 63 * $k), [uint32]$hp) }
  $ok = $false; for ($i = 0; $i -lt 20 -and !$ok; $i++) { Start-Sleep -Milliseconds 200; $ok = (Eval $ws "document.getElementById('p').value") -eq $secret }
  Check ($ok -and (WaitLog $log 'VAULT filled \(user action, same document, confirmed by the page\)' 3000)) 'with a password field: the account fills both, through the B03 fill (isolated world, guards at choice)'
  $ws.Dispose()

  # ---- 4. « Bulle de vérification · A » --------------------------------------------------------------------------------
  $ws = Navigate '/polar'; FocusField $ws 'email'; TypeText $ws 'valentin.m'; Key $ws 'Escape' 27
  $go = Eval $ws "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($go -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }
  $bubble = WaitWindow 'BrowserValidationBubble' $hp
  Check ($bubble -ne [IntPtr]::Zero) 'board 3: an e-mail without @ shows OUR bubble'
  if ($bubble -ne [IntPtr]::Zero) {
    $r = [AQ]::Rect($bubble); $f = FieldScreen $ws 'email' $hwnd
    Check ([Math]::Abs($r.L - $f.L) -le 1 -and [Math]::Abs($r.T - ($f.B + [Math]::Round(6 * $dpi / 96))) -le 1) 'board 3: under the field, 6 DIP, left edges aligned, no arrow'
    [AQ]::Capture($r, "$evidence\bubble.png"); Check (Compare-Board "$evidence\bubble.png" 'bubble' $dpi 12 8) 'board 3: pixels match the Paper board'
    $engine = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$hostDir*" } | ForEach-Object { [AQ]::Find('Chrome_WidgetWin_1', [uint32]$_.ProcessId) } | Where-Object { $_ -ne [IntPtr]::Zero -and ([AQ]::Rect($_).T -ge $f.B - 2) -and ([AQ]::Rect($_).T -le $f.B + 60) })
    Check ($engine.Count -eq 0) "the engine's own bubble is not shown (engine popups near the field: $($engine.Count))"
    TypeText $ws '@'; Check (Gone 'BrowserValidationBubble' $hp) 'the bubble goes as soon as the person types'
  }
  $ws.Dispose()
  # States board: three more messages, word for word, as our bubble (reportValidity shows the bubble of that field).
  $ws = Navigate '/bubbles'
  foreach ($case in @(@('missing', 'bubble-missing'), @('domain', 'bubble-domain'), @('short', 'bubble-too-short'))) {
    # 'too short' exists only once the person changed the value (HTML: a value from the page's markup is never too short)
    if ($case[0] -eq 'short') { FocusField $ws 'short'; [void](Eval $ws "var e=document.getElementById('short');e.setSelectionRange(e.value.length,e.value.length);1"); TypeText $ws 'f'; Start-Sleep -Milliseconds 200 }
    [void](Eval $ws "document.getElementById('$($case[0])').reportValidity()"); $bubble = WaitWindow 'BrowserValidationBubble' $hp
    if ($bubble -ne [IntPtr]::Zero) { $r = [AQ]::Rect($bubble); [AQ]::Capture($r, "$evidence\$($case[1]).png")
      if ($dpi -eq 192) { Check (Compare-Board "$evidence\$($case[1]).png" $case[1] $dpi 12 8) "states: the bubble « $($case[0]) » matches the board" } else { "     $($case[1]).png captured (the board export is 2x only: compared at 200 % only)" } }
    else { Check $false "states: the bubble « $($case[0]) » is shown" }
    TypeText $ws ' '; Start-Sleep -Milliseconds 300 }
  # Owner decision (24 November): a required date left empty says « Choisissez une valeur. » in the same bubble.
  [void](Eval $ws "document.getElementById('day').reportValidity()"); $bubble = WaitWindow 'BrowserValidationBubble' $hp
  if ($bubble -ne [IntPtr]::Zero) { $r = [AQ]::Rect($bubble); [AQ]::Capture($r, "$evidence\bubble-missing-value.png"); Check (($r.B - $r.T) -le [Math]::Round(35 * $dpi / 96)) 'owner decision: « Choisissez une valeur. » for an empty required date, one line, same bubble (bubble-missing-value.png)' }
  else { Check $false 'owner decision: an empty required date shows our bubble' }
  $ws.Dispose()
  $ws = Navigate '/own-validation'; $go = Eval $ws "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($go -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }; Start-Sleep -Milliseconds 800
  Check (([AQ]::Find('BrowserValidationBubble', [uint32]$hp)) -eq [IntPtr]::Zero) 'P14: a page that shows its own validation keeps it (no bubble from us)'; $ws.Dispose()
  $ws = Navigate '/hostile/custom'; $go = Eval $ws "(function(){var r=document.getElementById('go').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"; $x, $y = ($go -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }
  $bubble = WaitWindow 'BrowserValidationBubble' $hp
  if ($bubble -ne [IntPtr]::Zero) { $r = [AQ]::Rect($bubble); [AQ]::Capture($r, "$evidence\bubble-custom.png"); Check (($r.R - $r.L) -le [Math]::Round(320 * $dpi / 96) + 1) 'T9: a long page message stays inside the 320 DIP bubble (one line of 150 characters at most, text-direction characters removed: see bubble-custom.png)' } else { Check $false 'T9: the custom message shows our bubble' }
  $ws.Dispose()

  # ---- 5. Hostile pages (T2-T6) -----------------------------------------------------------------------------------------
  $ws = Navigate '/hostile/exec'; FocusField $ws 'email'; Start-Sleep -Milliseconds 600
  Check (([AQ]::Find('BrowserFieldList', [uint32]$hp)) -eq [IntPtr]::Zero) 'T4: a page that types by itself (execCommand) never opens the list'; $ws.Dispose()
  $ws = Navigate '/hostile/invisible'; FocusField $ws 'email'; TypeText $ws 'va'; Start-Sleep -Milliseconds 500
  Check (([AQ]::Find('BrowserFieldList', [uint32]$hp)) -eq [IntPtr]::Zero) 'T6: a nearly transparent field gets no list'; $ws.Dispose()
  $ws = Navigate '/hostile/moving'; FocusField $ws 'email'; TypeText $ws 'va'; $list = WaitWindow 'BrowserFieldList' $hp 1500
  if ($list -ne [IntPtr]::Zero) { $r = [AQ]::Rect($list); $k = $dpi / 96; Start-Sleep -Milliseconds 650; [void][AQ]::Click([int]($r.L + 160 * $k), [int]($r.T + 57 * $k), [uint32]$hp); Start-Sleep -Milliseconds 400 }
  Check ((Eval $ws "document.getElementById('email').value") -eq 'va') 'T2/T7: a field that moves under the list is never filled'; $ws.Dispose()
  $ws = Navigate '/hostile/frame'; Start-Sleep 1
  $fr = Eval $ws "(function(){var r=document.getElementById('f').getBoundingClientRect();return Math.round(r.left+40)+','+Math.round(r.top+16);})()"; $x, $y = ($fr -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) }; TypeText $ws 'va'; Start-Sleep -Milliseconds 500
  Check (([AQ]::Find('BrowserFieldList', [uint32]$hp)) -eq [IntPtr]::Zero) 'T5: a field in a frame of another origin gets no list (top document only)'; $ws.Dispose()

  # ---- 6. The key pill (variant C) -------------------------------------------------------------------------------------
  $ws = Navigate '/github'; Start-Sleep 2
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'VaultFill')
  $pill = $null; foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$hp)))) { $pill = $w.FindFirst('Descendants', $cond); if ($pill) { break } }
  Check ([bool]$pill) 'key pill: shown for a site with a saved account'
  if ($pill) { $b = $pill.Current.BoundingRectangle; $pr = New-Object AQ+RECT; $pr.L = [int]$b.X; $pr.T = [int]$b.Y; $pr.R = [int]($b.X + $b.Width); $pr.B = [int]($b.Y + $b.Height)
    [void][AQ]::SetCursorPos(5, 5); Start-Sleep -Milliseconds 300; [AQ]::Capture($pr, "$evidence\key-pill.png")
    [void][AQ]::SetCursorPos([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2)); Start-Sleep -Milliseconds 400; [AQ]::Capture($pr, "$evidence\key-pill-hover.png")
    "     key-pill-hover.png: states board « Survol » (#FFFFFF24, solid key and number in white); the pill next to a camera / micro request pill is checked by hand (a request has to be pending)"
    "     the fixture has ONE account: the pill reads « 1 » where the board reads « 2 »; the comparison covers the plate and the key"
    Check ([Math]::Abs($b.Height - [Math]::Round(24 * $dpi / 96)) -le 1) 'key pill: 24 DIP high, right after the padlock (see key-pill.png)'
    $c = [AQ]::Click([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2), [uint32]$hp); Start-Sleep -Milliseconds 800
    Check ((Get-Content $log -Raw) -match 'VAULT key: activated|VAULT fill: B03 prompt') 'key pill: a click opens the B03 list as before (armed)' }
  $ws.Dispose()

  # ---- 7. Night 24/11 regression: the key pill in a PRIVATE window, after the leading chip ------------------------------
  # A private window starts a fresh engine: its first visit of the fixture raises the (accepted) certificate error. Before
  # #139 the address showed the « Non sécurisé » chip; with #139 (#127) the fixture on 127.0.0.1 reads « Local ». On main
  # the key stayed there; the pill must too (vault_e2e R-2 checks), and the divider + address come after the pill.
  foreach ($p in HostPids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  for ($i = 0; $i -lt 60; $i++) { $left = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$hostDir*" }); if (!$left.Count) { break }; Start-Sleep -Milliseconds 500 }
  Start-Process -FilePath $hostExe -ArgumentList '--private' -WorkingDirectory $hostDir
  $pp = 0; for ($i = 0; $i -lt 40 -and !$pp; $i++) { Start-Sleep -Milliseconds 250; $pp = Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $hostExe -and $_.CommandLine -like '*--private*' } | ForEach-Object ProcessId | Select-Object -First 1 }
  Check ([bool]$pp) 'private window of the fixture started'
  if ($pp) {
    [void][AQ]::Front((Get-Process -Id $pp).MainWindowHandle, [uint32]$pp)
    for ($i = 0; $i -lt 50; $i++) { $homePage = $null; try { $homePage = (Invoke-RestMethod "http://127.0.0.1:$cdp/json") | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' -and $_.url -like 'data:*' } | Select-Object -First 1 } catch {}; if ($homePage) { break }; Start-Sleep -Milliseconds 300 }
    # Review R-13 (vault_e2e): a fresh private window lists several page targets; its own home page (data:) is navigated.
    if ($homePage) { $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$homePage.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); [void](Send $ws 'Page.navigate' @{ url = "$origin/github-login" }); Start-Sleep 3 } else { $ws = Navigate '/github-login'; Start-Sleep 2 }
    $pcond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'VaultFill'); $ppill = $null
    for ($try = 0; $try -lt 20 -and !$ppill; $try++) { if ($try) { Start-Sleep -Milliseconds 250 }; foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children', (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$pp)))) { $ppill = $w.FindFirst('Descendants', $pcond); if ($ppill) { break } } }
    Check ([bool]$ppill) 'private window: the key pill is shown for the opener profile''s account, also after the accepted certificate error (« Local » with #139, « Non sécurisé » before)'
    if ($ppill) { $b = $ppill.Current.BoundingRectangle; $pr = New-Object AQ+RECT; $pr.L = [int]$b.X - [int](140 * $dpi / 96); $pr.T = [int]$b.Y - 4; $pr.R = [int]($b.X + $b.Width) + [int](90 * $dpi / 96); $pr.B = [int]($b.Y + $b.Height) + 4
      [void][AQ]::SetCursorPos(5, 5); Start-Sleep -Milliseconds 300; [AQ]::Capture($pr, "$evidence\key-pill-private.png"); "     key-pill-private.png: « Local » (or the chip), 2, the pill, 6, the divider, 6, the address (never under the pill)" }
    $ws.Dispose()
    Stop-Process -Id $pp -Force -ErrorAction SilentlyContinue
  }
} finally {
  foreach ($p in HostPids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue
  if (Test-Path $log) { Get-Content $log | Where-Object { $_ -match 'FORMS|VAULT' } | Set-Content (Join-Path $evidence 'host-log-forms.txt') -Encoding utf8 }
}
"     evidence: $evidence"
if ($script:fails) { "FAIL #121 autofill night QA: $script:fails"; exit 1 } else { 'PASS #121 autofill night QA (product code, real window, Paper boards, hostile pages)' }
