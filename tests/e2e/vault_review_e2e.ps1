param([string]$Runtime = '', [switch]$SkipBuild, [string]$Only = '')
# Vault fixes of the independent review of e931c85 (build/vault-review-e931c85/review.md in the review worktree):
#   C  (V-1) two profiles in one window: a login of Personnel completes after the window switched to Travail. No offer
#      may show over Travail; back on Personnel the offer shows, and « Enregistrer » writes PERSONNEL's vault only.
#   A  (V-4) no offer without a real user action: an untrusted script submit; a real click whose page then writes the
#      password it wants (planted values).
#   F  (V-7) a classic login whose answer asks for the password again (wrong password): no offer; a successful one: offer.
#   H  (V-6) the fill refuses a transparent password field (nothing written into it).
#   K  (V-5) cost of a console-heavy page loop with the vault active, compared with the vault-inactive host.
#   E  (option A) the engine's old saved passwords are never deleted automatically: after 30 days of use Réglages ›
#      Mots de passe offers the deletion; « Annuler » keeps them; the confirmation deletes them; the offer is then gone.
# Test host tests/e2e/vault_host.cpp (vault on, foreground condition lifted: UI Automation, no mouse), control host
# tests/e2e/mtls_host.cpp (vault off). Synthetic profiles in build/, fictitious accounts, local fixture on 127.0.0.1 only.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'job_guard.ps1')   # every process started here dies with this script, even interrupted
$root = (Resolve-Path '.').Path; $pki = Join-Path $root 'build\e2e-vault-review-pki'
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
$port = 18481; $cdp = 9481; $O1 = "https://127.0.0.1:$port"
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Security
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Wq {
  [StructLayout(LayoutKind.Sequential)] struct CDS { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, ref CDS l, uint flags, uint timeout, out IntPtr result);
  public static long Send(IntPtr h, string text) {
    var bytes = System.Text.Encoding.Unicode.GetBytes(text + "\0"); var mem = Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes, 0, mem, bytes.Length);
    var cds = new CDS { dwData = (IntPtr)0x464F5250, cbData = bytes.Length, lpData = mem }; IntPtr r;
    SendMessageTimeout(h, 0x004A, IntPtr.Zero, ref cds, 2, 5000, out r); Marshal.FreeHGlobal(mem); return r.ToInt64(); }
}
"@
$AE = [System.Windows.Automation.AutomationElement]
function Find([int]$hostPid, [string]$aid) {
  $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $hostPid)
  foreach ($w in $AE::RootElement.FindAll('Children', $cond)) { $e = $w.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $aid))); if ($e) { return $e } }
  return $null }
function FindName([int]$hostPid, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $hostPid)
  foreach ($w in $AE::RootElement.FindAll('Children', $cond)) { $e = $w.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition($AE::NameProperty, $name))); if ($e) { return $e } }
  return $null }
function WaitFind([int]$hostPid, [string]$aid, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $e = Find $hostPid $aid; if ($e) { return $e }; Start-Sleep -Milliseconds 200 }; return $null }
function InvokeEl($e) { Start-Sleep -Milliseconds 700; $p = $null; if ($e -and $e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); return $true }; return $false }
function NoOffer([int]$hostPid, [int]$ms = 5000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { if ((Find $hostPid 'PasswordSaveConfirm') -or (Find $hostPid 'PasswordLater')) { return $false }; Start-Sleep -Milliseconds 200 }; return $true }
function Later([int]$hostPid) { $l = Find $hostPid 'PasswordLater'; if ($l) { [void](InvokeEl $l); Start-Sleep -Milliseconds 500 } }
$script:cid = 0
function Targets { try { return @((Invoke-RestMethod "http://127.0.0.1:$cdp/json") | ForEach-Object { $_ }) } catch { return @() } }
function Page { for ($i = 0; $i -lt 60; $i++) { $t = Targets | Where-Object { $_.type -eq 'page' } | Select-Object -Last 1; if ($t) { break }; Start-Sleep -Milliseconds 300 }
  $ws = New-Object System.Net.WebSockets.ClientWebSocket; $ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(); return $ws }
function Send($ws, [string]$method, $params) { $script:cid++; $b = [Text.Encoding]::UTF8.GetBytes((@{ id = $script:cid; method = $method; params = $params } | ConvertTo-Json -Depth 6 -Compress)); $ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
  $buf = New-Object byte[] 262144; while ($true) { $sb = New-Object Text.StringBuilder; do { $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None); if (!$r.Wait(10000)) { return $null }; [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Result.Count)) } while (!$r.Result.EndOfMessage); $j = $sb.ToString() | ConvertFrom-Json; if ($j.id -eq $script:cid) { return $j } } }
function Eval($ws, [string]$e) { (Send $ws 'Runtime.evaluate' @{ expression = $e; returnByValue = $true }).result.result.value }
function Navigate([string]$path) { $w = Page; [void](Send $w 'Page.navigate' @{ url = "$O1$path" }); $w.Dispose(); Start-Sleep 3; return (Page) }
function TypeInto($ws, [string]$id, [string]$text) { [void](Eval $ws "document.getElementById('$id').focus();1"); [void](Send $ws 'Input.insertText' @{ text = $text }) }
function PageClick($ws, [string]$id) {   # trusted (DevTools Input domain), like a person
  $r = Eval $ws "(function(){var r=document.getElementById('$id').getBoundingClientRect();return Math.round(r.left+r.width/2)+','+Math.round(r.top+r.height/2);})()"
  $x, $y = ($r -split ',') | ForEach-Object { [int]$_ }
  foreach ($t in 'mousePressed', 'mouseReleased') { [void](Send $ws 'Input.dispatchMouseEvent' @{ type = $t; x = $x; y = $y; button = 'left'; clickCount = 1 }) } }
function Build([string]$source, [string]$dir) {
  if ($SkipBuild -and (Test-Path "$dir\browser.exe")) { Get-ChildItem $dir -Force | Where-Object { @('browser.exe', 'WebView2Loader.dll', 'assets') -notcontains $_.Name } | Remove-Item -Recurse -Force; return }
  Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $dir | Out-Null
  & (Join-Path $root 'tools\w64devkit\bin\g++.exe') -std=c++17 -O1 -w -static -municode -mwindows -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Isrc '-Ideps/webview2/build/native/include' $source src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o "$dir\browser.exe" -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinhttp -lbcrypt -lshell32 -luser32 -lkernel32 -ladvapi32 -lcrypt32 -loleaut32
  if ($LASTEXITCODE -ne 0) { throw "host build failed: $source" }
  Copy-Item (Join-Path $root 'deps\webview2\build\native\x64\WebView2Loader.dll') $dir; Copy-Item (Join-Path $root 'assets') "$dir\assets" -Recurse }
function Fresh([string]$dir) {
  Get-ChildItem $dir -Force | Where-Object { @('browser.exe', 'WebView2Loader.dll', 'assets') -notcontains $_.Name } | Remove-Item -Recurse -Force
  [IO.File]::WriteAllText((Join-Path $dir 'profiles.txt'), "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`n") }
function HostPids([string]$exe) { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -ieq $exe } | ForEach-Object ProcessId }
function StopHost([string]$exe, [string]$dir) {
  foreach ($p in HostPids $exe) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
  for ($i = 0; $i -lt 60; $i++) { if (!@(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$dir*" }).Count) { return }; Start-Sleep -Milliseconds 500 } }
function StartHost([string]$exe, [string]$dir, [string]$arguments = '') {
  if ($arguments) { Start-Process -FilePath $exe -WorkingDirectory $dir -ArgumentList $arguments } else { Start-Process -FilePath $exe -WorkingDirectory $dir }
  $hp = 0; for ($i = 0; $i -lt 40 -and !$hp; $i++) { Start-Sleep -Milliseconds 250; $hp = HostPids $exe | Select-Object -First 1 }
  [void](WaitLog "$dir\prototype.log" 'E2E BOUND certificate decisions cleared' 20000); return $hp }
function WaitLog([string]$log, [string]$pattern, [int]$ms = 15000) { $t = [Environment]::TickCount; while ([Environment]::TickCount - $t -lt $ms) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if ($l -match $pattern) { return $true }; Start-Sleep -Milliseconds 200 }; return $false }
function LogCount([string]$log, [string]$pattern) { $l = Get-Content $log -Raw -ErrorAction SilentlyContinue; if (!$l) { return 0 }; return ([regex]::Matches($l, $pattern)).Count }
function VaultEntries([string]$file) {
  $out = @(); if (!(Test-Path $file)) { return $out }
  foreach ($l in ([IO.File]::ReadAllText($file) -split "`n")) { $f = $l -split "`t"; if ($f.Count -ne 6) { continue }
    $o = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($f[3])); $u = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($f[4]))
    $s = try { [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect([Convert]::FromBase64String($f[5]), [Text.Encoding]::UTF8.GetBytes("browser-vault-v1|$o"), 'CurrentUser')) } catch { '<undecryptable>' }
    $out += [pscustomobject]@{ origin = $o; user = $u; secret = $s } }
  return $out }

New-Item -ItemType Directory -Force $pki | Out-Null
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'; $ErrorActionPreference = 'Continue'
& $o req -x509 -newkey rsa:2048 -nodes -keyout "$pki\server.key" -out "$pki\server.crt" -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' 2>&1 | Out-Null
& $o x509 -in "$pki\server.crt" -outform DER -out "$pki\server.der" 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
[IO.File]::WriteAllText("$pki\pin.txt", "host=127.0.0.1`nport=$port`nsha256=$((Get-FileHash "$pki\server.der" -Algorithm SHA256).Hash.ToLower())`n")
$dir = Join-Path $root 'build\e2e-vault-review'; $ctl = Join-Path $root 'build\e2e-vault-review-control'; $exe = "$dir\browser.exe"; $ctlExe = "$ctl\browser.exe"
foreach ($e in @($exe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force } }
Build 'tests/e2e/vault_host.cpp' $dir
if (!$Only -or $Only -match 'K') { Build 'tests/e2e/mtls_host.cpp' $ctl }
$server = Start-Process python -ArgumentList 'tests/e2e/mtls_server.py', $pki, "$port", '--no-client-auth' -PassThru -WindowStyle Hidden
$env:BROWSER_E2E_PIN_MANIFEST = "$pki\pin.txt"; $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$cdp"
if ($Runtime) { $env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = (Resolve-Path $Runtime).Path }
$log = "$dir\prototype.log"; $perso = "$dir\passwords"; $travail = "$dir\profiles\ptravail01\passwords"
try {
  if (!$Only -or $Only -match 'C') {
    '--- C (V-1): a Personnel login completes while the window shows Travail'
    $ok = $false
    for ($attempt = 1; $attempt -le 3 -and !$ok; $attempt++) {
      StopHost $exe $dir; Fresh $dir; $hp = StartHost $exe $dir '--workspace-test'; $main = (Get-Process -Id $hp).MainWindowHandle
      $w = Navigate '/delayed'; TypeInto $w 'u' 'e2e-perso'; TypeInto $w 'p' 'E2e-Perso-1001!'; PageClick $w 'go'; $w.Dispose()
      [void][Wq]::Send($main, 'qa switch ptravail01')
      $switched = WaitLog $log 'WSQA switch ptravail01' 10000
      $t = [Environment]::TickCount; $shownOverTravail = $false
      while ([Environment]::TickCount - $t -lt 11000) { if (Find $hp 'PasswordSaveConfirm') { $shownOverTravail = $true; break }; Start-Sleep -Milliseconds 200 }
      [void][Wq]::Send($main, 'qa state'); Start-Sleep -Milliseconds 400
      $state = (Select-String -Path $log -Pattern 'WSQA state' | Select-Object -Last 1).Line
      $captured = (LogCount $log 'VAULT capture: B04 prompt') -ge 1
      "     attempt ${attempt}: switched=$switched captured=$captured shown-over-Travail=$shownOverTravail ($state)"
      if ($shownOverTravail) { $b = Find $hp 'PasswordSaveConfirm'; [void](InvokeEl $b); Start-Sleep 2 }   # what « Enregistrer » would do there
      else {
        [void][Wq]::Send($main, 'qa switch default'); [void](WaitLog $log 'WSQA switch default' 10000)
        $b = WaitFind $hp 'PasswordSaveConfirm' 8000
        "     back on Personnel: offer shown=$([bool]$b)"
        if ($b) { [void](InvokeEl $b); Start-Sleep 2 } }
      $pe = @(VaultEntries $perso); $te = @(VaultEntries $travail)
      "     Personnel vault: $(($pe | ForEach-Object { $_.user }) -join ', ')   Travail vault: $(($te | ForEach-Object { $_.user }) -join ', ')"
      $ok = $switched -and $captured -and !$shownOverTravail -and @($pe | Where-Object { $_.user -eq 'e2e-perso' -and $_.secret -eq 'E2e-Perso-1001!' }).Count -eq 1 -and $te.Count -eq 0
      if (!$ok -and $shownOverTravail) { break }   # the defect itself: no retry
      if (!$ok) { "     attempt $attempt did not complete (another window may have taken the foreground); retried" }
    }
    Check $ok 'V-1: no offer over Travail; back on Personnel the offer shows and « Enregistrer » writes PERSONNEL''s vault only (Travail''s stays empty)'
    StopHost $exe $dir
  }
  if (!$Only -or $Only -match 'A|F|H') {
    StopHost $exe $dir; Fresh $dir; $hp = StartHost $exe $dir
  }
  if (!$Only -or $Only -match 'A') {
    '--- A (V-4): offers need a real user action and a password the person typed'
    $before = LogCount $log 'VAULT capture: B04 prompt'
    $w = Navigate '/spoof'; $w.Dispose()
    Check ((NoOffer $hp 5000) -and (LogCount $log 'VAULT capture: B04 prompt') -eq $before) 'V-4: an untrusted script submit with values chosen by the page (no click, no key): no offer'
    Later $hp
    $w = Navigate '/planted'; PageClick $w 'go'; $w.Dispose()
    Check ((NoOffer $hp 5000) -and (LogCount $log 'VAULT capture: B04 prompt') -eq $before) 'V-4: a real click, but a password the PAGE wrote (planted): no offer'
    Later $hp
  }
  if (!$Only -or $Only -match 'F') {
    '--- F (V-7): offered only once the login succeeded'
    $w = Navigate '/login-fail'; TypeInto $w 'u' 'e2e-echec'; TypeInto $w 'p' 'E2e-Faux-0000!'; PageClick $w 'go'; $w.Dispose()
    Check (NoOffer $hp 6000) 'V-7: the site asks for the password again (wrong password): no offer'
    Later $hp
    $w = Navigate '/login'; TypeInto $w 'u' 'e2e-bon'; TypeInto $w 'p' 'E2e-Bon-2002!'; PageClick $w 'go'; $w.Dispose()
    $b = WaitFind $hp 'PasswordSaveConfirm' 8000
    Check ([bool]$b) 'V-7 control: the next page does not ask for a password: the offer shows'
    if ($b) { [void](InvokeEl $b); Start-Sleep 2 }
    Check (@(VaultEntries $perso | Where-Object { $_.user -eq 'e2e-bon' -and $_.secret -eq 'E2e-Bon-2002!' }).Count -eq 1) 'V-7 control: saved'
  }
  if (!$Only -or $Only -match 'H') {
    '--- H (V-6): no fill into a transparent field'
    if (!@(VaultEntries $perso | Where-Object { $_.user -eq 'e2e-bon' }).Count) {
      $w = Navigate '/login'; TypeInto $w 'u' 'e2e-bon'; TypeInto $w 'p' 'E2e-Bon-2002!'; PageClick $w 'go'; $w.Dispose(); $b = WaitFind $hp 'PasswordSaveConfirm' 8000; if ($b) { [void](InvokeEl $b); Start-Sleep 2 } }
    $notDone = LogCount $log 'VAULT fill not done: outcome=3'
    $w = Navigate '/hidden'; [void](Eval $w "document.getElementById('p').focus();1")
    $key = WaitFind $hp 'VaultFill' 8000
    if ($key) { [void](InvokeEl $key); $row = $null; $t = [Environment]::TickCount; while (!$row -and [Environment]::TickCount - $t -lt 5000) { $row = FindName $hp 'e2e-bon'; Start-Sleep -Milliseconds 200 }; if ($row) { [void](InvokeEl $row) } }
    Start-Sleep 2
    $val = Eval $w "document.getElementById('p').value"; $w.Dispose()
    "     key shown=$([bool]$key); transparent field after the choice: '$val'"
    Check ([bool]$key -and $val -eq '' -and (LogCount $log 'VAULT fill not done: outcome=3') -gt $notDone) 'V-6: the transparent password field is not filled (« Aucun champ de mot de passe visible »)'
    $w = Navigate '/login'; [void](Eval $w "document.getElementById('p').focus();1"); $key = WaitFind $hp 'VaultFill' 8000
    if ($key) { [void](InvokeEl $key); $row = $null; $t = [Environment]::TickCount; while (!$row -and [Environment]::TickCount - $t -lt 5000) { $row = FindName $hp 'e2e-bon'; Start-Sleep -Milliseconds 200 }; if ($row) { [void](InvokeEl $row) } }
    $filled = $false; for ($i = 0; $i -lt 20 -and !$filled; $i++) { Start-Sleep -Milliseconds 200; $filled = (Eval $w "document.getElementById('p').value") -eq 'E2e-Bon-2002!' }; $w.Dispose()
    Check $filled 'V-6 control: a visible field still fills'
  }
  if (!$Only -or $Only -match 'E') {
    '--- E (option A): deleting Browser''s old passwords is OFFERED after 30 days of use, done only on request'
    function FindLike([int]$hostPid, [string]$pattern) {
      $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $hostPid)
      foreach ($w in $AE::RootElement.FindAll('Children', $cond)) { foreach ($e in $w.FindAll('Descendants', [System.Windows.Automation.Condition]::TrueCondition)) { if ($e.Current.Name -like $pattern) { return $e } } }
      return $null }
    function OpenManager([int]$hostPid) {   # Réglages › Confidentialité › Gérer les mots de passe, step by step until the list is there
      $t = [Environment]::TickCount
      while ([Environment]::TickCount - $t -lt 25000) {
        $list = Find $hostPid 'PasswordList'; if ($list) { return $list }
        $g = FindName $hostPid 'Gérer les mots de passe'; if ($g) { [void](InvokeEl $g); Start-Sleep -Milliseconds 800; continue }
        $p = FindName $hostPid 'Confidentialité'; if ($p) { [void](InvokeEl $p); Start-Sleep -Milliseconds 800; continue }
        [void](InvokeEl (Find $hostPid 'Settings')); Start-Sleep -Milliseconds 1200 }
      return $null }
    $mig = "$dir\vault-engine-migration"
    function Cleared { if (!(Test-Path $mig)) { return -1 }; $m = [regex]::Match([IO.File]::ReadAllText($mig), 'cleared=(\d+)'); if ($m.Success) { return [int64]$m.Groups[1].Value }; return -1 }
    StopHost $exe $dir; Fresh $dir; $hp = StartHost $exe $dir
    Check ((Test-Path $mig) -and (Cleared) -eq 0) 'option A: the first active start records the activation (nothing deleted)'
    $list = OpenManager $hp
    Check ([bool]$list -and -not (Find $hp 'VaultEngineOffer')) 'option A: before 30 days of use, Réglages › Mots de passe offers nothing'
    StopHost $exe $dir
    # Clock injected through the recorded state: 29 days of use already counted, so this start is the 30th.
    [IO.File]::WriteAllText($mig, (([IO.File]::ReadAllText($mig) -replace 'days=\d+', 'days=29') -replace 'lastday=\d+', 'lastday=20000101'))
    $hp = StartHost $exe $dir; Start-Sleep 2
    Check ((Cleared) -eq 0 -and (LogCount $log 'VAULT engine saved passwords deleted') -eq 0) 'option A: on the 30th day of use NOTHING is deleted automatically'
    $list = OpenManager $hp; $offer = WaitFind $hp 'VaultEngineOffer' 5000
    Check ([bool]$list -and [bool]$offer) 'option A: on the 30th day of use Réglages › Mots de passe offers « Supprimer les anciens mots de passe de Browser… »'
    if ($offer) {
      [void](InvokeEl $offer); $confirm = WaitFind $hp 'DeleteEnginePasswords' 6000
      Check ([bool]$confirm) 'option A: the link closes the list and opens the confirmation (small-popup family, danger action)'
      [void](InvokeEl (WaitFind $hp 'DialogCancel' 3000)); [void](WaitFind $hp 'VaultEngineOffer' 6000)   # the list comes back
      Check ((Cleared) -eq 0 -and (LogCount $log 'VAULT engine saved passwords deleted') -eq 0 -and [bool](Find $hp 'VaultEngineOffer')) 'option A: « Annuler » deletes nothing and the offer stays'
      [void](InvokeEl (WaitFind $hp 'VaultEngineOffer' 6000)); [void](WaitFind $hp 'DeleteEnginePasswords' 6000)
      [void](InvokeEl (WaitFind $hp 'DialogDeleteEnginePasswords' 3000))
      Check (WaitLog $log 'VAULT engine saved passwords deleted on request' 15000) 'option A: after the confirmation the engine''s saved passwords are deleted (ClearBrowsingData PASSWORD_AUTOSAVE)'
      Start-Sleep 1
      $done = $null; $t = [Environment]::TickCount; while (!$done -and [Environment]::TickCount - $t -lt 6000) { $done = FindLike $hp "*Anciens mots de passe de Browser supprimés*"; Start-Sleep -Milliseconds 300 }
      Check ((Cleared) -gt 0 -and -not (Find $hp 'VaultEngineOffer') -and [bool]$done) 'option A: the list comes back and says so (« Anciens mots de passe de Browser supprimés », review N-4), the deletion is recorded and the offer is gone'
    }
    StopHost $exe $dir; $hp = StartHost $exe $dir; Start-Sleep 1
    $list = OpenManager $hp
    Check ([bool]$list -and -not (Find $hp 'VaultEngineOffer') -and (LogCount $log 'VAULT engine saved passwords deleted on request') -eq 1) 'option A: the offer never comes back (next start)'
    StopHost $exe $dir
  }
  if (!$Only -or $Only -match 'K') {
    '--- K (V-5): cost of a console-heavy loop (2000 console.debug of an object), three runs each'
    StopHost $exe $dir; Fresh $dir; $hp = StartHost $exe $dir
    $act = @(); for ($i = 0; $i -lt 3; $i++) { $w = Navigate '/console'; $act += [double](Eval $w 'window.__cost'); $w.Dispose() }
    StopHost $exe $dir
    Fresh $ctl; $cp = StartHost $ctlExe $ctl
    $off = @(); for ($i = 0; $i -lt 3; $i++) { $w = Navigate '/console'; $off += [double](Eval $w 'window.__cost'); $w.Dispose() }
    StopHost $ctlExe $ctl
    $ma = ($act | Measure-Object -Minimum).Minimum; $mo = ($off | Measure-Object -Minimum).Minimum
    "     vault active: $(($act | ForEach-Object { [math]::Round($_) }) -join ', ') ms; vault inactive: $(($off | ForEach-Object { [math]::Round($_) }) -join ', ') ms"
    Check ($ma -le 1.5 * $mo + 10) 'V-5: the vault''s capture does not slow a console-heavy page down (best run within 1.5x + 10 ms of the vault-inactive host)'
  }
} finally {
  foreach ($e in @($exe, $ctlExe)) { foreach ($p in HostPids $e) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Remove-Item Env:BROWSER_E2E_PIN_MANIFEST, Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS, Env:WEBVIEW2_BROWSER_EXECUTABLE_FOLDER -ErrorAction SilentlyContinue
  Remove-Item $pki -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "FAIL vault review e2e: $script:fails"; exit 1 } else { 'PASS vault review e2e: V-1 offers bound to their profile, V-4 no offer without a user action and a typed password, V-7 offered only after a successful login, V-6 no fill into a transparent field, V-5 no console slowdown, option A engine passwords deleted only on request' }
