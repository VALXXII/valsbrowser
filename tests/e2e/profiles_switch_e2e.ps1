param([string]$Fixture = 'build/profiles-switch-qa', [int]$Port = 8768)
# Same-window profiles: runtime regressions of the independent review (H1, M1, M2, L1-L4, L6) on a REAL
# browser.exe, its real WebView2 callbacks, UI Automation and its WM_COPYDATA channel. A fresh copy of the
# fixture build runs in a new temporary root (its profile root); only processes of that copy are touched.
# Hooks: `--workspace-test` enables « PROF » « qa ... » commands (src/profile-workspace.h) whose outcome is
# logged as « WSQA ... ». Pages come from tests/e2e/profiles_switch_server.py on 127.0.0.1.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path
$root = Join-Path $env:TEMP ("browser-wsqa-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class Wsqa {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] struct CDS { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, ref CDS l, uint flags, uint timeout, out IntPtr result);
  public static IntPtr Find(uint pid, string cls) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (p == pid && sb.ToString() == cls && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
  public static long Send(IntPtr h, string text) {
    var bytes = Encoding.Unicode.GetBytes(text + "\0"); var mem = Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes, 0, mem, bytes.Length);
    var cds = new CDS { dwData = (IntPtr)0x464F5250, cbData = bytes.Length, lpData = mem }; IntPtr r;
    SendMessageTimeout(h, 0x004A, IntPtr.Zero, ref cds, 2, 5000, out r); Marshal.FreeHGlobal(mem); return r.ToInt64(); }
}
"@
$AE = [System.Windows.Automation.AutomationElement]
function Procs { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like "$root\*" } }
function Main([int]$procId) { for ($i = 0; $i -lt 40; $i++) { $h = [Wsqa]::Find([uint32]$procId, 'LisiereBrowser'); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 250 }; throw "no window for $procId" }
function Qa([int]$procId, [string]$cmd) { [Wsqa]::Send((Main $procId), "qa $cmd") }
function Elements([int]$procId) {
  $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $procId)
  $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond) | ForEach-Object { $_.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) } | ForEach-Object { $_ }
}
function Uia([int]$procId, [string]$id, [string]$name = '', [string]$parent = '') {
  for ($i = 0; $i -lt 40; $i++) {
    foreach ($e in (Elements $procId)) {
      try { $c = $e.Current } catch { continue }
      if ($c.AutomationId -ne $id -or ($name -and $c.Name -notlike $name)) { continue }
      if ($parent) { $p = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($e); if (!$p -or $p.Current.Name -notlike $parent) { continue } }
      return $e
    }
    Start-Sleep -Milliseconds 250
  }
  return $null
}
function Invoke-Uia([int]$procId, [string]$id, [string]$name = '', [string]$parent = '') {
  $e = Uia $procId $id $name $parent; if (!$e) { throw "UIA not found: $id $name $parent" }
  $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function ProfileShown([int]$procId) { $e = Uia $procId 'ProfileButton'; if ($e) { $e.Current.Name } else { '' } }
function Pick([int]$procId, [string]$name) { Invoke-Uia $procId 'ProfileButton'; Start-Sleep -Milliseconds 700; Invoke-Uia $procId 'ProfileMenuProfile' "$name*" }
function LogOf([string]$profile) { if ($profile -eq 'default') { "$root\prototype.log" } else { "$root\profiles\$profile\browser.log" } }
function LogText([string]$profile) { $f = LogOf $profile; if (Test-Path -LiteralPath $f) { [IO.File]::ReadAllText($f) } else { '' } }
function WaitLog([string]$profile, [string]$pattern, [int]$seconds = 20) { $t = [DateTime]::Now.AddSeconds($seconds); while ([DateTime]::Now -lt $t) { if ((LogText $profile) -match $pattern) { return $true }; Start-Sleep -Milliseconds 250 }; return $false }
function LastLine([string]$profile, [string]$pattern) { ((LogText $profile) -split "`n" | Where-Object { $_ -match $pattern } | Select-Object -Last 1) }
function DeleteFrom([int]$procId, [string]$name) {
  Invoke-Uia $procId 'ProfileButton'; Start-Sleep -Milliseconds 700
  Invoke-Uia $procId 'ProfileMenuManage'; Start-Sleep -Milliseconds 1300
  Invoke-Uia $procId 'ProfileListItem' "$name*"; Start-Sleep -Milliseconds 1000
  Invoke-Uia $procId 'ProfileDelete'; Start-Sleep -Milliseconds 1100
  Invoke-Uia $procId 'ProfileDeleteConfirm'
}
$enc = New-Object System.Text.UTF8Encoding($false)
function W([string]$path, [string]$text) { New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null; [IO.File]::WriteAllText($path, $text, $enc) }
$u = "http://127.0.0.1:$Port"
function Session([string]$url) { "browser-session-2`n0`n1`n`"$url`"`n0`n" }
# A fresh root: Personnel (neutral, default) + Travail + Loisirs; this window's set {Personnel active, Travail cached}.
function Seed([switch]$LoisirsOrphans) {
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; Start-Sleep 1
  Get-ChildItem $root -Force | Where-Object { $_.Name -in @('profile','sessions','profiles','adblock','favicons','windows','profiles.txt','session','settings','history','downloads','bookmarks','prototype.log','engine-args') } | Remove-Item -Recurse -Force
  W "$root\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`np=ploisirs01|users-three|40|140|0|Loisirs`n"
  W "$root\sessions\wqaA" (Session "$u/show")
  W "$root\profiles\ptravail01\sessions\wqaB" (Session "$u/tick")
  W "$root\windows\00000000deadbeef" "browser-window-set 1`nslot=default|wqaA|active`nslot=ptravail01|wqaB|cached`n"
  foreach ($p in @("$root", "$root\profiles\ptravail01", "$root\profiles\ploisirs01")) { W "$p\settings" "browser-settings 1`nrestore-tabs=1`nask-where-to-save=0`n" }
  if ($LoisirsOrphans) { W "$root\profiles\ploisirs01\sessions\wL1" (Session "$u/ok?l1"); W "$root\profiles\ploisirs01\sessions\wL2" (Session "$u/ok?l2") }
}
function StartA([string]$extra = '') { $p = Start-Process "$root\browser.exe" -ArgumentList "--profile default --restore-window wqaA $extra" -PassThru; Main $p.Id | Out-Null; Start-Sleep 6; $p }

# ---- fixture copy and page server ------------------------------------------------------------------------
New-Item -ItemType Directory -Force $root | Out-Null
robocopy $fix $root /E /NFL /NDL /NJH /NJS /NP /XD profile sessions profiles adblock favicons windows /XF profiles.txt session settings history downloads bookmarks prototype.log engine-args | Out-Null
$server = Start-Process python -ArgumentList "-I `"$PWD\tests\e2e\profiles_switch_server.py`" $Port" -PassThru -WindowStyle Hidden
Start-Sleep 1
"root: $root"
try {
  # ==== H1 (same window): a restored cold slot, never shown, is deleted from this window ====================
  Seed; $A = StartA
  Check (WaitLog 'default' 'cached slot restored ptravail01' 5) 'H1: Travail restored as a cached slot of this window (never shown)'
  DeleteFrom $A.Id 'Travail'
  $deleted = WaitLog 'default' 'PROFILES deleted ptravail01' 15
  Check $deleted 'H1 same window: the never-shown cached Travail is deleted (no « open-windows » refusal)'
  Check ((LogText 'default') -match 'cached slot released ptravail01') 'H1 same window: its slot claim was released'
  Check (-not ((LogText 'default') -match 'delete refused ptravail01')) 'H1 same window: no refusal logged'
  Check ((Get-Process -Id $A.Id -ErrorAction SilentlyContinue) -ne $null -and (ProfileShown $A.Id) -like '*Personnel*') 'H1 same window: the window stays, on Personnel'

  # ==== H1 (other window): the same slot, deleted from a Loisirs window ===================================
  Seed; $A = StartA
  $B = Start-Process "$root\browser.exe" -ArgumentList '--profile ploisirs01' -PassThru; Main $B.Id | Out-Null; Start-Sleep 6
  DeleteFrom $B.Id 'Travail'
  Check (WaitLog 'ploisirs01' 'PROFILES deleted ptravail01' 20) 'H1 other window: Loisirs deletes Travail while window A holds its never-shown slot'
  Check ((LogText 'default') -match 'cached slot released ptravail01') 'H1 other window: window A released the slot'
  Check ((Get-Process -Id $A.Id -ErrorAction SilentlyContinue) -ne $null -and (ProfileShown $A.Id) -like '*Personnel*') 'H1 other window: window A stays open on Personnel'
  Check (-not ((Get-Content "$root\profiles.txt" -Raw) -match 'ptravail01')) 'H1 other window: Travail left the registry'

  # ==== N1: no working fallback (Travail is only a cold restored slot): the normal recovery runs ONCE ==========
  # The failure is injected only because the test armed `expect-terminal`: Fail is reached and captured (its dialog
  # suppressed by --workspace-test only); nothing is switched to the cold slot, no notice, no loop.
  Seed; $A = StartA '--workspace-test'
  $procs0 = (Procs | Where-Object { $_.CommandLine -notmatch '--type=' } | Measure-Object).Count
  Qa $A.Id 'expect-terminal' | Out-Null; Qa $A.Id 'controller-fail' | Out-Null
  Invoke-Uia $A.Id 'NewTab'
  Check (WaitLog 'default' 'WSQA controller failure injected' 15) 'N1: a controller failure injected in Personnel (only working profile)'
  Check (WaitLog 'default' 'no working profile in this window: normal recovery' 5) 'N1: the cold restored slot is not a fallback: normal recovery'
  Check (WaitLog 'default' 'WSQA terminal failure captured' 5) 'N1: Fail reached (captured by the armed test mode, no dialog)'
  Start-Sleep 4
  $log = LogText 'default'
  Check ((([regex]::Matches($log, 'WSQA terminal failure captured')).Count) -eq 1) 'N1: the normal recovery ran exactly once'
  Check (-not ($log -match 'switched this window to ptravail01')) 'N1: no switch to the cold slot (no engine started for it)'
  Check (-not ($log -match 'workspace engine failed default: .*working fallback')) 'N1: no local failure, no notice'
  Check ((Procs | Where-Object { $_.CommandLine -notmatch '--type=' } | Measure-Object).Count -eq $procs0) 'N1: no other window launched'
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; Start-Sleep 1

  # ==== M2, L4, L1, M1, L3, L6, L2 on one window with --workspace-test =================================
  Seed; $A = StartA '--workspace-test'
  # Loisirs orphans appear AFTER the start (a cold start would relaunch them as their own windows, as before).
  W "$root\profiles\ploisirs01\sessions\wL1" (Session "$u/ok?l1"); W "$root\profiles\ploisirs01\sessions\wL2" (Session "$u/ok?l2")
  Qa $A.Id 'state' | Out-Null; Start-Sleep -Milliseconds 500
  Check ((LastLine 'default' 'WSQA state') -match '\[default started holds=1\]') 'M1: one engine lease for the start profile (the version probe takes none)'
  Pick $A.Id 'Travail'; Start-Sleep 6
  $args0 = Get-Content "$root\profiles\ptravail01\engine-args" -Raw -ErrorAction SilentlyContinue
  Check ($args0 -match 'autoplay-policy=user-gesture-required') 'M1 setup: Travail engine started with its own arguments (user gesture)'
  Pick $A.Id 'Personnel'; Start-Sleep 3
  # L1: Travail is parked; its /tick title keeps changing: ITS sidecar follows, Personnel's never shows it.
  $side = "$root\profiles\ptravail01\sessions\wqaB.hwnd"
  $t1 = [regex]::Match((Get-Content $side -Raw), 'tick \d+').Value; Start-Sleep 3; $t2 = [regex]::Match((Get-Content $side -Raw), 'tick \d+').Value
  Check ($t1 -and $t2 -and $t1 -ne $t2) "L1: the parked Travail's own sidecar is republished with its new titles ($t1 -> $t2)"
  Check (-not ((Get-Content "$root\sessions\wqaA.hwnd" -Raw) -match 'tick')) "L1: the active Personnel's sidecar never carries the parked profile's titles"
  # M2 + L4: a job created in the PARKED Travail's scope switches the window to Loisirs (first start in this window).
  $before = (Procs | Measure-Object).Count
  Check ((Qa $A.Id 'scoped-switch ptravail01 ploisirs01') -eq 1) 'M2: scoped switch accepted'
  Check (WaitLog 'default' 'WSQA after-scoped-switch' 10) 'M2: the job ran'
  $after = LastLine 'default' 'WSQA after-scoped-switch'
  Check ($after -match 'globals=ploisirs01 active=ploisirs01 parked=0') "M2: after the scoped job, the globals are the window's active Loisirs (UI live): $after"
  Start-Sleep 4
  Check ((ProfileShown $A.Id) -like '*Loisirs*') 'M2: the window shows Loisirs'
  $tabs0 = @(Elements $A.Id | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem }).Count
  Invoke-Uia $A.Id 'NewTab'; Start-Sleep 2
  $tabs1 = @(Elements $A.Id | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem }).Count
  Check ($tabs1 -eq $tabs0 + 1) "M2: the UI is not inert after the scoped switch (Nouvel onglet: $tabs0 -> $tabs1 tabs)"
  Check ((Procs | Measure-Object).Count -eq $before) 'L4: starting Loisirs inside the window launched no other window for its extra orphan'
  Check ((LogText 'default') -match 'in-window start: 1 other session') 'L4: the extra orphan is left for the next start (logged)'
  Check ((@(Get-ChildItem "$root\profiles\ploisirs01\sessions" -File | Where-Object { $_.Extension -eq '' }).Count) -eq 2) 'L4: both Loisirs sessions kept (one adopted here, one orphan)'
  # M1: Travail (parked) gets new engine flags, is made cold: its lease is released only after its engine EXITED.
  Qa $A.Id 'autoplay ptravail01 allow' | Out-Null
  Check ((Qa $A.Id 'cold ptravail01') -eq 1) 'M1: Travail made cold'
  Check (WaitLog 'default' 'engine of ptravail01 released, waiting for its exit' 5) 'M1: the environment is kept alive by its watch until the exit event'
  Check (WaitLog 'default' 'engine of ptravail01 exited, lease released' 20) 'M1: BrowserProcessExited fired and released the lease'
  Start-Sleep 1   # its watch is dropped by a deferred job (never inside its own event)
  Qa $A.Id 'state' | Out-Null; Start-Sleep -Milliseconds 500
  $st = LastLine 'default' 'WSQA state'
  Check ($st -match 'watches=0' -and $st -match '\[ptravail01 started cold holds=0\]') "M1: no watch left, no hold on Travail: $st"
  Pick $A.Id 'Travail'; Start-Sleep 6
  Check ((Get-Content "$root\profiles\ptravail01\engine-args" -Raw) -match 'autoplay-policy=no-user-gesture-required') 'M1: the new flags apply to the next engine after the eviction'
  # L3 + engine failures of a profile started in this window: expected, injected into the REAL callbacks (only in a
  # secondary workspace): that profile fails alone, its lease is released, the window returns to Personnel, a
  # non-modal notice tells the user; never the global halt / error dialog.
  Pick $A.Id 'Personnel'; Start-Sleep 2
  Qa $A.Id 'cold ptravail01' | Out-Null; Start-Sleep 1
  Qa $A.Id 'bad-engine' | Out-Null; Pick $A.Id 'Travail'
  Check (WaitLog 'default' 'engine of ptravail01 failed to start, lease released' 15) 'L3: an environment creation failure (completion) released its lease'
  Check (WaitLog 'default' 'workspace engine failed ptravail01: CreateCoreWebView2Environment' 5) 'failure: Travail failed alone (logged)'
  Start-Sleep 2
  Check ((ProfileShown $A.Id) -like '*Personnel*') 'failure: the window came back to Personnel, usable'
  Check ((Uia $A.Id 'VaultNotice') -ne $null) 'failure: a non-modal notice tells the user'
  Qa $A.Id 'bad-engine sync' | Out-Null; Pick $A.Id 'Travail'
  Check (WaitLog 'default' 'engine of ptravail01 not started, lease released' 15) 'L3: a synchronous creation failure released its lease'
  Start-Sleep 2
  Check ((ProfileShown $A.Id) -like '*Personnel*') 'failure (sync): back on Personnel'
  # A controller creation failure (asynchronous, after the environment exists): its environment is released after exit.
  Qa $A.Id 'controller-fail' | Out-Null; Pick $A.Id 'Travail'
  Check (WaitLog 'default' 'WSQA controller failure injected' 20) 'controller failure: injected into the real CreateController completion'
  Check (WaitLog 'default' 'workspace engine failed ptravail01: CreateController callback' 5) 'controller failure: Travail failed alone (logged)'
  Check (WaitLog 'default' '(?s)CreateController callback.*engine of ptravail01 exited, lease released' 20) 'controller failure: its engine exited and the owner lease was released'
  Start-Sleep 2
  Check ((ProfileShown $A.Id) -like '*Personnel*') 'controller failure: the window came back to Personnel, usable'
  Qa $A.Id 'state' | Out-Null; Start-Sleep -Milliseconds 500
  $st = LastLine 'default' 'WSQA state'
  Check ($st -match '\[ptravail01 started cold holds=0\]' -and $st -match 'active=default parked=0') "failures: no hold left on Travail, Personnel active: $st"
  Check (-not ((LogText 'default') -match 'WEBVIEW2 ERROR|Opérations arrêtées')) 'failures: never the global halt / error dialog'
  Pick $A.Id 'Travail'; Start-Sleep 7
  $titles = (Elements $A.Id | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem } | ForEach-Object { $_.Current.Name }) -join ','
  Check ($titles -match 'tick' -and (ProfileShown $A.Id) -like '*Travail*') "recovery: choosing Travail again starts its engine normally ($titles)"
  Pick $A.Id 'Personnel'; Start-Sleep 2
  # L6: the row's context action through UI Automation (a child Button), never a switch.
  $n0 = (Procs | Where-Object { $_.CommandLine -notmatch '--type=' } | Measure-Object).Count
  Invoke-Uia $A.Id 'ProfileButton'; Start-Sleep -Milliseconds 700
  Invoke-Uia $A.Id 'ProfileMenuNewWindow' '' 'Travail*'
  Start-Sleep 7
  $C = Procs | Where-Object { $_.CommandLine -match '--profile ptravail01' -and $_.CommandLine -notmatch '--type=' } | Select-Object -First 1
  Check ($C -ne $null) 'L6: UIA Invoke on Travail''s « Ouvrir dans une nouvelle fenêtre » opened a new Travail window'
  Check ((ProfileShown $A.Id) -like '*Personnel*') 'L6: window A did not switch'
  # L2: the user closes A while another Travail window WITH A SESSION is open (WindowSessions counts session files;
  # a brand-new empty window has none yet): the parked Travail session follows the active rule.
  W "$root\profiles\ptravail01\sessions\wC" (Session "$u/show?c")
  $C2 = Start-Process "$root\browser.exe" -ArgumentList '--profile ptravail01 --restore-window wC' -PassThru; Main $C2.Id | Out-Null; Start-Sleep 6
  [Wsqa]::PostMessage((Main $A.Id), 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  for ($i = 0; $i -lt 40 -and (Get-Process -Id $A.Id -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
  Check (-not (Get-Process -Id $A.Id -ErrorAction SilentlyContinue)) 'L2: window A closed'
  Check (-not (Test-Path "$root\profiles\ptravail01\sessions\wqaB")) 'L2: the parked Travail session is removed (another Travail window is open), like the active rule'
  Check (Test-Path "$root\sessions\wqaA") 'L2: the active Personnel session is kept (no other Personnel window)'
}
finally {
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
}
if ($script:fails) { "$($script:fails) FAILED (root kept: $root)"; exit 1 }
"all profile switch runtime checks passed"; Start-Sleep 1; Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue; exit 0
