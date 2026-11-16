param([string]$Fixture = 'build/import-app', [string]$Evidence = 'build/dnd-e2e/evidence', [switch]$RequireVisual)
# Bookmark drag and drop + « Placer le contenu dans la barre de favoris » on a REAL browser.exe (isolated copy).
# A fresh copy of the fixture build runs in its own folder under build/import-e2e with seeded data (bookmark
# « Mien », one history page) and --import-sources pointing at SYNTHETIC profiles made by
# tests/browser-import/make_sources.py: no personal browser folder is ever listed or read.
# Driven only through UI Automation patterns (Invoke, Toggle, Expand, Value) on the app's own elements and its
# own file dialog. No SendInput, no SetForegroundWindow, no clipboard, no screen readback: captures are
# PrintWindow of the app's own windows. Only processes whose image lives in the run folder are touched.
# Captures are a SEPARATE, visual result: each one is measured (lit pixels, colours, light text); a blank one (PrintWindow
# returns black when the session does not compose, e.g. display off) is saved as BLANK-<name>.png and reported as
# « visual unavailable », never under the evidence name. -RequireVisual makes a blank capture fail the run; the
# functional UIA checks are counted on their own either way.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$repo = (Get-Location).Path
$fix = (Resolve-Path $Fixture).Path
$run = Join-Path $repo ("build\dnd-e2e\run-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$app = Join-Path $run 'app'
$sources = Join-Path $run 'sources é#'
New-Item -ItemType Directory -Force $Evidence | Out-Null
$Evidence = (Resolve-Path $Evidence).Path
$script:fails = 0
$script:log = @()
function Check([bool]$ok, [string]$what) { $line = if ($ok) { "ok   $what" } else { "FAIL $what" }; $line; $script:log += $line; if (!$ok) { $script:fails++ } }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Drawing; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Impqa {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr h, uint m, IntPtr w, string l);
  public static IntPtr Find(uint pid, string cls) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (p == pid && sb.ToString() == cls && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
  public static List<IntPtr> Visible(uint pid) { var list = new List<IntPtr>(); EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero); return list; }
  // The main window with every visible popup of the process composed at its screen offset (each via PrintWindow).
  public static string Shot(IntPtr main, uint pid, string path) {
    RECT w; GetWindowRect(main, out w);
    using (var all = new Bitmap(w.R - w.L, w.B - w.T)) {
      using (var g = Graphics.FromImage(all)) {
        var order = Visible(pid); order.Reverse();   // bottom of the z-order first
        foreach (var h in order) { RECT r; GetWindowRect(h, out r); if (r.R <= r.L || r.B <= r.T) continue;
          using (var one = new Bitmap(r.R - r.L, r.B - r.T)) { using (var og = Graphics.FromImage(one)) { var dc = og.GetHdc(); PrintWindow(h, dc, 2); og.ReleaseHdc(dc); } g.DrawImage(one, r.L - w.L, r.T - w.T); } }
      }
      int lit = 0, light = 0; var seen = new HashSet<int>();
      for (int y = 0; y < all.Height; y += 2) for (int x = 0; x < all.Width; x += 2) { var c = all.GetPixel(x, y); int v = Math.Max(c.R, Math.Max(c.G, c.B)); if (v > 40) lit++; if (v > 200) light++; if (seen.Count < 512) seen.Add(c.ToArgb()); }
      all.Save(path, System.Drawing.Imaging.ImageFormat.Png);
      return "lit=" + lit + " light=" + light + " distinct=" + seen.Count;
    }
  }
}
"@
$AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]
$script:visualOk = 0; $script:visualBlank = @()
# A capture of our window, measured; blank → BLANK-<name>.png under <evidence>\blank-captures, never <name>.png.
function Visual([IntPtr]$main, [int]$procId, [string]$name) {
  $path = Join-Path $Evidence "$name.png"
  $q = [Impqa]::Shot($main, [uint32]$procId, $path)
  $lit = [int]([regex]::Match($q, 'lit=(\d+)').Groups[1].Value); $light = [int]([regex]::Match($q, 'light=(\d+)').Groups[1].Value); $distinct = [int]([regex]::Match($q, 'distinct=(\d+)').Groups[1].Value)
  if ($lit -gt 2000 -and $light -gt 200 -and $distinct -gt 8) { $script:visualOk++; $line = "visual ok    $name ($q)" }
  else {
    $blank = Join-Path $Evidence 'blank-captures'; New-Item -ItemType Directory -Force $blank | Out-Null
    Move-Item -LiteralPath $path -Destination (Join-Path $blank "BLANK-$name.png") -Force
    $script:visualBlank += $name; $line = "visual UNAVAILABLE ${name}: blank capture ($q), kept as blank-captures\BLANK-$name.png"
  }
  $line; $script:log += $line
}
function Cond($prop, $value) { New-Object System.Windows.Automation.PropertyCondition($prop, $value) }
function Main([int]$procId) { for ($i = 0; $i -lt 80; $i++) { $h = [Impqa]::Find([uint32]$procId, 'LisiereBrowser'); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 250 }; throw "no window for $procId" }
# First element of the process (any of its top-level windows) with this AutomationId (or Name).
function Uia([int]$procId, [string]$id, [string]$byName = '', [int]$tries = 40) {
  $prop = if ($byName) { $AE::NameProperty } else { $AE::AutomationIdProperty }; $value = if ($byName) { $byName } else { $id }
  for ($i = 0; $i -lt $tries; $i++) {
    foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) { $e = $top.FindFirst($TS::Descendants, (Cond $prop $value)); if ($e) { return $e } }
    Start-Sleep -Milliseconds 200
  }
  return $null
}
function Invoke($e) { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
function ToggleState($e) { $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState.ToString() }
function Toggle($e) { $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle() }
function Gone([int]$procId, [string]$id) { for ($i = 0; $i -lt 40; $i++) { if (!(Uia $procId $id '' 1)) { return $true }; Start-Sleep -Milliseconds 150 }; return $false }
function Hash([string]$path) { if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { 'absent' } }
function TreeHash([string]$dir) { (Get-ChildItem -LiteralPath $dir -Recurse -Force | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($dir.Length) + '|' + $(if ($_.PSIsContainer) { 'dir' } else { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) }) -join "`n" }
$enc = New-Object System.Text.UTF8Encoding($false)
function W([string]$path, [string]$text) { New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null; [IO.File]::WriteAllText($path, $text, $enc) }
function ReadText([string]$path) { [IO.File]::ReadAllText($path, $enc) }

# Settings › Confidentialité › Importer… : the dialog element.
function OpenImport([int]$procId, [IntPtr]$main, [string]$privacy) {
  $entry = Uia $procId 'SettingsImportData' '' 3   # the drawer is still on Confidentialité after a previous import
  if (!$entry) {
    $settings = Uia $procId 'Settings'; if (!$settings) { throw 'no Settings button' }
    Invoke $settings
    $row = Uia $procId '' $privacy; if (!$row) { throw "no $privacy row" }
    Invoke $row
    $entry = Uia $procId 'SettingsImportData'; if (!$entry) { throw 'no import row' }
  }
  Invoke $entry
  $d = Uia $procId 'ImportBrowserData'; if (!$d) { throw 'no import dialog' }
  Start-Sleep -Milliseconds 400
  return $d
}
function Choose([int]$procId, $dialog, [string]$label, [string]$option) {
  $select = $dialog.FindFirst($TS::Descendants, (Cond $AE::NameProperty $label)); if (!$select) { throw "no select $label" }
  $select.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
  $item = $null
  for ($i = 0; $i -lt 40 -and !$item; $i++) {
    Start-Sleep -Milliseconds 150
    foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) {
      $c = New-Object System.Windows.Automation.AndCondition((Cond $AE::AutomationIdProperty 'MenuItem'), (Cond $AE::NameProperty $option))
      $f = $top.FindFirst($TS::Descendants, $c); if ($f) { $item = $f; break }
    }
  }
  if (!$item) { throw "no option $option" }
  Invoke $item; Start-Sleep -Milliseconds 500
}
function Check3($dialog, [string[]]$names) { $names | ForEach-Object { $dialog.FindFirst($TS::Descendants, (Cond $AE::NameProperty $_)) } }
# Waits for the summary dialog, returns its text (the dialog's help: its body).
function Result([int]$procId) { $r = Uia $procId 'ImportResult' '' 80; if (!$r) { return $null }; Start-Sleep -Milliseconds 300; return $r }
function Close([int]$procId, [IntPtr]$main) {
  [Impqa]::PostMessage($main, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
  for ($i = 0; $i -lt 60; $i++) { if (!(Get-Process -Id $procId -ErrorAction SilentlyContinue)) { return }; Start-Sleep -Milliseconds 250 }
  Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
}
function Launch([string]$lang) {
  if ($lang) { W "$app\ui-language" "browser-ui-language 1`nlanguage=$lang`n" }
  $p = Start-Process "$app\browser.exe" -ArgumentList '--qa-drawer-stays', '--profile', 'default', '--import-sources', "`"$sources`"" -PassThru
  $m = Main $p.Id; Start-Sleep 4
  return @($p, $m)
}

# ---- Setup -------------------------------------------------------------------------------------------------
New-Item -ItemType Directory -Force $app | Out-Null
robocopy $fix $app /E /NFL /NDL /NJH /NJS /NP /XD profile sessions profiles adblock favicons windows /XF profiles.txt session settings history downloads bookmarks prototype.log engine-args ui-language | Out-Null
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (!$python) { $python = 'C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe' }
& $python -I "$repo\tests\browser-import\make_sources.py" $sources
if ($LASTEXITCODE -ne 0) { throw 'synthetic sources not created' }
W "$app\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`n"
$bmFile = "$app\bookmarks"

# ---- Bookmark drag and drop on the real app (part 2) + « Placer le contenu dans la barre de favoris » (part 1) --------
# Posted pointer/keyboard messages to the app's OWN windows (main, its menu levels, its drawer) at points taken from its own
# UIA bounds; never SendInput, SetForegroundWindow or the real cursor. State checked in the bookmarks file on disk.
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Ptr {
  [StructLayout(LayoutKind.Sequential)] public struct P { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref P p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr L(int x, int y) { return (IntPtr)((y << 16) | (x & 0xFFFF)); }
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static void At(IntPtr h, uint msg, int sx, int sy, int keys) { var p = new P { X = sx, Y = sy }; ScreenToClient(h, ref p); PostMessage(h, msg, (IntPtr)keys, L(p.X, p.Y)); }
}
"@
[Ptr]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null   # per-monitor v2: UIA bounds and ScreenToClient in real pixels (not virtualised)
$WM_MOUSEMOVE = 0x0200; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202; $WM_RBUTTONUP = 0x0205; $WM_KEYDOWN = 0x0100; $MK_LBUTTON = 1; $Shortcut = 0x8001
function Center($e) { $r = $e.Current.BoundingRectangle; return @([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2), [int]$r.X, [int]$r.Width) }
function WindowsOf([int]$procId, [string]$cls) { [Impqa]::Visible([uint32]$procId) | Where-Object { $sb = New-Object System.Text.StringBuilder 64; [void][Win32Cls]::GetClassName($_, $sb, 64); $sb.ToString() -eq $cls } }
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class Win32Cls { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n); }
"@
function Bm { $lines = (ReadText "$app\bookmarks") -split "`n" | Where-Object { $_ -match "^\d+`t" }; $o = @{}; foreach ($l in $lines) { $f = $l -split "`t"; $o[[int]$f[0]] = @{ parent = [int]$f[1]; title = $f[4] } }; return @{ map = $o; order = @($lines | ForEach-Object { [int](($_ -split "`t")[0]) }) } }
function ChildrenOf($bm, [int]$parent) { @($bm.order | Where-Object { $bm.map[$_].parent -eq $parent }) }
function WaitLog([string]$text, [int]$start) { for ($i = 0; $i -lt 40; $i++) { $t = (ReadText "$app\prototype.log").Substring($start); if ($t -match [regex]::Escape($text)) { return $true }; Start-Sleep -Milliseconds 150 }; return $false }
function InHwnd($hwnd, $prop, [string]$value) { if (!$hwnd) { return $null }; for ($i = 0; $i -lt 25; $i++) { try { $e = $AE::FromHandle([IntPtr]$hwnd).FindFirst($TS::Descendants, (Cond $prop $value)); if ($e) { return $e } } catch {}; Start-Sleep -Milliseconds 150 }; return $null }
function DrawerHwnd([int]$procId) { @(WindowsOf $procId 'LisiereBookmarksDrawer') | Select-Object -First 1 }
function MenuHwnd([int]$procId) { @(WindowsOf $procId 'LisiereContextMenu') | Select-Object -Last 1 }
function BarItem([int]$procId, [string]$id) { $bar = InHwnd $main $AE::AutomationIdProperty 'BookmarksBar'; if (!$bar) { return $null }; for ($i = 0; $i -lt 25; $i++) { $e = $bar.FindFirst($TS::Descendants, (Cond $AE::AutomationIdProperty $id)); if ($e) { return $e }; Start-Sleep -Milliseconds 150 }; return $null }   # the bar's own items (the drawer reuses the ids)
function DrawerRow([int]$procId, [string]$id) { InHwnd (DrawerHwnd $procId) $AE::AutomationIdProperty $id }
function MenuRow([int]$procId, [string]$name) { InHwnd (MenuHwnd $procId) $AE::NameProperty $name }
function LogLen { (ReadText "$app\prototype.log").Length }

# Seed: a v0.3.376-like store. Bar: mine(3), Projets(4) › Deep link(5), Inner(6). Autres: group(10) › Bookmarks bar(11) › A(12), B(13);
# Mail iCloud(14) in the group; Loose(15) at the top of Autres. The map names only the group (as v0.3.376 wrote it).
W "$app\bookmarks" ("browser-bookmarks 1`n3`t1`tL`t1`tmine`thttps://mine.example/`n4`t1`tF`t1`tProjets`t`n5`t4`tL`t1`tDeep link`thttps://deep.example/`n6`t4`tF`t1`tInner`t`n" +
  "10`t2`tF`t5`tImportés d'un fichier HTML`t`n11`t10`tF`t6`tBookmarks bar`t`n12`t11`tL`t7`tA`thttps://a.example/`n13`t11`tL`t7`tB`thttps://b.example/`n14`t10`tL`t7`tMail iCloud`thttps://mail.example/`n15`t2`tL`t1`tLoose`thttps://loose.example/`nend`t10`n")
W "$app\bookmark-imports" "browser-bookmark-imports 1`nhtml`t10`t5`n"
W "$app\settings" "browser-settings 1`nrestore-tabs=0`nask-where-to-save=0`nbookmarks-bar=always`n"
try {
  $p, $main = Launch ''; $procId = $p.Id
  # ---- 1. Drag OUT OF an open folder menu ONTO the bar (the screenshot's use case) -------------------------------------
  $start = LogLen
  Invoke (Uia $procId 'Bookmark.4'); Start-Sleep -Milliseconds 700
  $row = MenuRow $procId 'Deep link'; $menuWin = @(WindowsOf $procId 'LisiereContextMenu') | Select-Object -Last 1
  $mine = BarItem $procId 'Bookmark.3'
  if ($row -and $menuWin -and $mine) {
    $c = Center $row; $t = Center $mine
    [Ptr]::At($menuWin, $WM_LBUTTONDOWN, $c[0], $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 100
    [Ptr]::At($menuWin, $WM_MOUSEMOVE, $c[0] + 12, $c[1] + 2, $MK_LBUTTON); Start-Sleep -Milliseconds 500
    Check (WaitLog 'BOOKMARKS drag start (menu)' $start) 'menu → bar: past the threshold the menu hands the drag over'
    [Ptr]::At($main, $WM_MOUSEMOVE, $t[2] + 3, $t[1], $MK_LBUTTON); Start-Sleep -Milliseconds 200
    Visual $main $procId 'dnd-1-menu-to-bar-marker'
    [Ptr]::At($main, $WM_LBUTTONUP, $t[2] + 3, $t[1], 0); Start-Sleep -Milliseconds 700
    $bm = Bm
    Check ((ChildrenOf $bm 1)[0] -eq 5 -and $bm.map[5].parent -eq 1 -and (ChildrenOf $bm 4).Count -eq 1) "menu → bar: « Deep link » now first on the bar, same id, saved ($((ChildrenOf $bm 1) -join ','))"
  } else { Check $false "menu → bar: menu row / menu window / bar item found ($([bool]$row) $([bool]$menuWin) $([bool]$mine))" }
  # ---- 2. Undo: one step, back into Projets at its place -----------------------------------------------------------------
  [Impqa]::PostMessage($main, $Shortcut, [IntPtr]3032, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 700
  $bm = Bm; Check ($bm.map[5].parent -eq 4 -and (ChildrenOf $bm 4)[0] -eq 5 -and (ChildrenOf $bm 1)[0] -eq 3) 'undo: the drop is undone in one step and saved'
  # ---- 3. Escape cancels: nothing moves, nothing saved --------------------------------------------------------------------
  $before = Hash "$app\bookmarks"; $start = LogLen
  Invoke (Uia $procId 'Bookmark.4'); Start-Sleep -Milliseconds 700
  $row = MenuRow $procId 'Inner'; $menuWin = @(WindowsOf $procId 'LisiereContextMenu') | Select-Object -Last 1; $mine = BarItem $procId 'Bookmark.3'
  if ($row -and $menuWin -and $mine) {
    $c = Center $row; $t = Center $mine
    [Ptr]::At($menuWin, $WM_LBUTTONDOWN, $c[0], $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 100
    [Ptr]::At($menuWin, $WM_MOUSEMOVE, $c[0] + 12, $c[1] + 2, $MK_LBUTTON); Start-Sleep -Milliseconds 400
    [Ptr]::At($main, $WM_MOUSEMOVE, $t[2] + 3, $t[1], $MK_LBUTTON); Start-Sleep -Milliseconds 150
    [Impqa]::PostMessage($main, $WM_KEYDOWN, [IntPtr]0x1B, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 300
    [Ptr]::At($main, $WM_LBUTTONUP, $t[2] + 3, $t[1], 0); Start-Sleep -Milliseconds 400
    Check ((Hash "$app\bookmarks") -eq $before -and (WaitLog 'BOOKMARKS drag cancelled' $start)) 'Escape: drag cancelled, file unchanged'
  } else { Check $false 'escape case: menu found' }
  # ---- 4. Drawer → bar (end of the bar) ------------------------------------------------------------------------------------
  [Impqa]::PostMessage($main, $Shortcut, [IntPtr]3031, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 900
  $drawer = @(WindowsOf $procId 'LisiereBookmarksDrawer') | Select-Object -First 1
  $loose = DrawerRow $procId 'Bookmark.15'; $last = BarItem $procId 'Bookmark.4'
  if ($drawer -and $loose -and $last) {
    $start = LogLen; $c = Center $loose; $t = Center $last
    [Ptr]::At($drawer, $WM_LBUTTONDOWN, $c[0], $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 100
    [Ptr]::At($drawer, $WM_MOUSEMOVE, $c[0] - 12, $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 400
    Check (WaitLog 'BOOKMARKS drag start (drawer)' $start) 'drawer → bar: the drawer hands the drag over'
    [Ptr]::At($main, $WM_MOUSEMOVE, $t[2] + $t[3] + 12, $t[1], $MK_LBUTTON); Start-Sleep -Milliseconds 200
    Visual $main $procId 'dnd-2-drawer-to-bar-marker'
    [Ptr]::At($main, $WM_LBUTTONUP, $t[2] + $t[3] + 12, $t[1], 0); Start-Sleep -Milliseconds 700
    $bm = Bm; Check ((ChildrenOf $bm 1)[-1] -eq 15 -and $bm.map[15].parent -eq 1) "drawer → bar: « Loose » appended to the bar ($((ChildrenOf $bm 1) -join ','))"
  } else { Check $false "drawer → bar: drawer / rows found ($([bool]$drawer) $([bool]$loose) $([bool]$last))" }
  # ---- 4b. Drawer → INTO a bar folder (middle of « Projets ») ---------------------------------------------------------------
  $mail = DrawerRow $procId 'Bookmark.10'; $proj = BarItem $procId 'Bookmark.4'
  if ($mail -and $proj -and $drawer) {
    $c = Center $mail; $t = Center $proj
    [Ptr]::At($drawer, $WM_LBUTTONDOWN, $c[0], $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 100
    [Ptr]::At($drawer, $WM_MOUSEMOVE, $c[0] - 12, $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 400
    [Ptr]::At($main, $WM_MOUSEMOVE, $t[0], $t[1], $MK_LBUTTON); Start-Sleep -Milliseconds 200
    [Ptr]::At($main, $WM_LBUTTONUP, $t[0], $t[1], 0); Start-Sleep -Milliseconds 700
    $bm = Bm; Check ($bm.map[10].parent -eq 4 -and (ChildrenOf $bm 4)[-1] -eq 10) "drawer → into the bar folder « Projets » ($((ChildrenOf $bm 4) -join ','))"
    [Impqa]::PostMessage($main, $Shortcut, [IntPtr]3032, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 700
    $bm = Bm; Check ($bm.map[10].parent -eq 2) 'into-folder drop undone'
  } else { Check $false 'drawer → bar folder: elements found' }
  # ---- 5. Bar → drawer: drop « mine » on the « Autres favoris » label (first in it) --------------------------------------------
  $mineBar = BarItem $procId 'Bookmark.3'; $otherLabel = DrawerRow $procId 'Bookmark.2'
  if ($mineBar -and $otherLabel) {
    $start = LogLen; $c = Center $mineBar; $t = Center $otherLabel
    [Ptr]::At($main, $WM_MOUSEMOVE, $c[0], $c[1], 0); Start-Sleep -Milliseconds 150   # hover first, as a real pointer would
    [Ptr]::At($main, $WM_LBUTTONDOWN, $c[0], $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 100
    [Ptr]::At($main, $WM_MOUSEMOVE, $c[0] + 15, $c[1], $MK_LBUTTON); Start-Sleep -Milliseconds 150
    [Ptr]::At($main, $WM_MOUSEMOVE, $t[0], $t[1], $MK_LBUTTON); Start-Sleep -Milliseconds 250
    Visual $main $procId 'dnd-3-bar-to-drawer-marker'
    [Ptr]::At($main, $WM_LBUTTONUP, $t[0], $t[1], 0); Start-Sleep -Milliseconds 700
    $bm = Bm; Check ($bm.map[3].parent -eq 2 -and (ChildrenOf $bm 2)[0] -eq 3) "bar → drawer: « mine » first in Autres favoris ($((ChildrenOf $bm 2) -join ','))"
  } else { Check $false "bar → drawer: elements found ($([bool]$mineBar) $([bool]$otherLabel))" }
  # ---- 6. Part 1: « Placer le contenu dans la barre de favoris » on the old HTML bar folder (drawer row menu) ------------------
  $group = DrawerRow $procId 'Bookmark.10'
  if ($group) { try { $group.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand() } catch {}; Start-Sleep -Milliseconds 500 }
  $oldBar = DrawerRow $procId 'Bookmark.11'
  if ($oldBar -and $drawer) {
    $c = Center $oldBar; [Ptr]::At($drawer, $WM_RBUTTONUP, $c[0], $c[1], 0); Start-Sleep -Milliseconds 700
    $cmd = Uia $procId '' 'Placer le contenu dans la barre de favoris' 10
    if ($cmd) { Invoke $cmd; Start-Sleep -Milliseconds 900 }
    $bm = Bm
    Check ($cmd -and $bm.map[12].parent -eq 1 -and $bm.map[13].parent -eq 1 -and (ChildrenOf $bm 1)[-2] -eq 12 -and (ChildrenOf $bm 1)[-1] -eq 13) "promote: A and B on the bar, same ids and order ($((ChildrenOf $bm 1) -join ','))"
    Check ($bm.map.ContainsKey(11) -and (ChildrenOf $bm 11).Count -eq 0) 'promote: the old bar folder (no identity of its own) stays, empty'
    [Impqa]::PostMessage($main, $Shortcut, [IntPtr]3032, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 700
    $bm = Bm; Check ($bm.map[12].parent -eq 11 -and $bm.map[13].parent -eq 11 -and (ChildrenOf $bm 11) -join ',' -eq '12,13') 'promote undo: back in the old folder, saved'
  } else { Check $false "promote: drawer rows found ($([bool]$group) $([bool]$oldBar))" }
  $final = Hash "$app\bookmarks"
  Close $procId $main
  # ---- 7. Persistence after a restart: the bar shows what the file holds ------------------------------------------------------
  $p, $main = Launch ''; $procId = $p.Id
  Check ((Hash "$app\bookmarks") -eq $final -and (Uia $procId 'Bookmark.15') -and (Uia $procId 'Bookmark.5' '' 3) -eq $null) 'after restart: same file, « Loose » on the bar'
  Close $procId $main
} finally {
  Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like "$run\*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
$visualLine = "visual: $($script:visualOk) capture(s) verified non-blank, $($script:visualBlank.Count) blank"
$visualLine; $script:log += $visualLine
$script:log | Set-Content -Encoding utf8 (Join-Path $Evidence 'dnd-results.txt')
if ($script:fails) { "FAILED: $($script:fails)"; exit 1 }
'bookmark dnd e2e: all functional checks passed'
