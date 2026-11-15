param([string]$Fixture = 'build/bookmark-rename-app', [string]$Evidence = 'design/bookmark-inline-rename-evidence')
# Folder rename in place on a REAL browser.exe (design/bookmark-inline-rename-plan.md). A fresh copy of the
# fixture build runs in a new temporary root with a seeded bookmarks file (bar: « Paper », folder « Game »);
# nothing of the user's profile is read and only processes of that copy are touched.
# Driven only through UI Automation and window messages to the app's own windows: UIA focus on the band
# item « Game », F2 posted to the window, the name set with WM_SETTEXT, Enter / Escape posted to the editor.
# No SendInput, no SetForegroundWindow, no clipboard. Checks the editor sits on the band title, UIA
# name/value, the file on disk after Enter, nothing written after Escape, and the name after a relaunch.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path
$root = Join-Path $env:TEMP ("browser-renqa-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Drawing; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Renqa {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public static IntPtr Find(uint pid, string cls) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (p == pid && sb.ToString() == cls && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
  // Visible EDIT children of the main window (the address field is hidden while not editing).
  public static List<IntPtr> VisibleEdits(IntPtr main) { var list = new List<IntPtr>(); EnumChildWindows(main, (h, l) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "Edit" && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero); return list; }
  public static void Shot(IntPtr main, int cropHeight, string path) {
    RECT w; GetWindowRect(main, out w);
    using (var all = new Bitmap(w.R - w.L, w.B - w.T)) { using (var g = Graphics.FromImage(all)) { var dc = g.GetHdc(); PrintWindow(main, dc, 2); g.ReleaseHdc(dc); }
      using (var part = all.Clone(new Rectangle(0, 0, Math.Min(all.Width, cropHeight * 6), Math.Min(all.Height, cropHeight)), all.PixelFormat)) part.Save(path, System.Drawing.Imaging.ImageFormat.Png); }
  }
}
"@
$AE = [System.Windows.Automation.AutomationElement]
function Procs { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like "$root\*" } }
function Main([int]$procId) { for ($i = 0; $i -lt 60; $i++) { $h = [Renqa]::Find([uint32]$procId, 'LisiereBrowser'); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 250 }; throw "no window for $procId" }
function Uia([int]$procId, [string]$id) {
  $cond = New-Object System.Windows.Automation.AndCondition((New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $procId)), (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)))
  for ($i = 0; $i -lt 40; $i++) { $e = $AE::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond); if ($e) { return $e }; Start-Sleep -Milliseconds 250 }
  return $null
}
$enc = New-Object System.Text.UTF8Encoding($false)
function W([string]$path, [string]$text) { New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null; [IO.File]::WriteAllText($path, $text, $enc) }
function Bookmarks { [IO.File]::ReadAllText("$root\bookmarks", $enc) }
function Rename([int]$procId, [IntPtr]$main) {
  $item = Uia $procId 'Bookmark.10'; if (!$item) { throw 'UIA Bookmark.10 (Game) not found' }
  $item.SetFocus(); Start-Sleep -Milliseconds 400
  [Renqa]::PostMessage($main, 0x0100, [IntPtr]0x71, [IntPtr]1) | Out-Null   # WM_KEYDOWN F2 to the window
  Start-Sleep -Milliseconds 700
  return @([Renqa]::VisibleEdits($main))
}

New-Item -ItemType Directory -Force $root | Out-Null
robocopy $fix $root /E /NFL /NDL /NJH /NJS /NP /XD profile sessions profiles adblock favicons windows /XF profiles.txt session settings history downloads bookmarks prototype.log engine-args | Out-Null
W "$root\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`n"
W "$root\settings" "browser-settings 1`nrestore-tabs=0`nask-where-to-save=0`n"
W "$root\bookmarks" "browser-bookmarks 1`n3`t1`tL`t1794100000000`tPaper`thttps://paper.design/`n10`t1`tF`t1794100000000`tGame`t`n11`t10`tL`t1794100000000`titch.io`thttps://itch.io/`nend`t3`n"
New-Item -ItemType Directory -Force $Evidence | Out-Null
"root: $root"
try {
  $p = Start-Process "$root\browser.exe" -ArgumentList '--profile default' -PassThru
  $main = Main $p.Id; Start-Sleep 5
  $dpi = [Renqa]::GetDpiForWindow($main); $band = [int](125 * $dpi / 96)
  "     window DPI $dpi"
  [Renqa]::Shot($main, $band, (Join-Path $Evidence "app-band-before-$dpi.png"))
  # ---- Enter commits ---------------------------------------------------------------------------
  $edits = Rename $p.Id $main
  Check ($edits.Count -eq 1) 'F2 on the bar folder « Game »: one editor shown (no popover window)'
  $edit = if ($edits.Count) { $edits[0] } else { [IntPtr]::Zero }
  if ($edit -ne [IntPtr]::Zero) {
    $er = New-Object Renqa+RECT; $wr = New-Object Renqa+RECT; [Renqa]::GetWindowRect($edit, [ref]$er) | Out-Null; [Renqa]::GetWindowRect($main, [ref]$wr) | Out-Null
    $top = ($er.T - $wr.T) * 96 / $dpi; $left = ($er.L - $wr.L) * 96 / $dpi
    "     editor at x=$([math]::Round($left,1)) y=$([math]::Round($top,1)) DIP, $($er.R - $er.L)x$($er.B - $er.T) px"
    Check ($top -ge 84 -and $top -le 116 -and $left -gt 6 -and $left -lt 220) 'editor inside the bookmarks band, on the item (not a floating popover)'
    $focused = $AE::FocusedElement
    $fromHwnd = $AE::FromHandle($edit)
    "     UIA focused: pid $($focused.Current.ProcessId) type $($focused.Current.ControlType.ProgrammaticName) class '$($focused.Current.ClassName)' name '$($focused.Current.Name)'; editor HWND element: type $($fromHwnd.Current.ControlType.ProgrammaticName) name '$($fromHwnd.Current.Name)' focus $($fromHwnd.Current.HasKeyboardFocus)"
    # Name and keyboard focus through UIA; the control type and Value pattern are asserted with the native
    # COM UIA client (what Narrator / NVDA use) in tests/bookmarks/bookmark_rename_test.cpp: this managed
    # System.Windows.Automation client applies its own legacy proxies (compared below with the address EDIT).
    Check ($focused -and $focused.Current.ProcessId -eq $p.Id -and $focused.Current.ClassName -eq 'Edit' -and $focused.Current.Name -eq 'Nom' -and $fromHwnd.Current.HasKeyboardFocus) 'UIA: keyboard focus on the editor, named « Nom »'
    $len = [Renqa]::SendText($edit, 0x000E, [IntPtr]::Zero, $null)   # WM_GETTEXTLENGTH
    Check ($len.ToInt64() -eq 4) 'the editor holds « Game » (4 characters) before typing'
    [Renqa]::Shot($main, $band, (Join-Path $Evidence "app-band-renaming-$dpi.png"))
    [Renqa]::SendText($edit, 0x000C, [IntPtr]::Zero, 'Jeux vidéo') | Out-Null
    [Renqa]::PostMessage($edit, 0x0100, [IntPtr]0x0D, [IntPtr]1) | Out-Null   # WM_KEYDOWN Enter to the editor
    Start-Sleep -Milliseconds 800
    Check (@([Renqa]::VisibleEdits($main)).Count -eq 0) 'Enter: the editor is gone'
    Check ((Bookmarks) -match "10`t1`tF`t\d+`tJeux vidéo`t") 'Enter: « Jeux vidéo » written to the bookmarks file'
    $item = Uia $p.Id 'Bookmark.10'; Check ($item -and $item.Current.Name -eq 'Jeux vidéo') 'UIA band item now « Jeux vidéo »'
    [Renqa]::Shot($main, $band, (Join-Path $Evidence "app-band-after-$dpi.png"))
  }
  # ---- Escape restores ----------------------------------------------------------------------------
  $before = Bookmarks
  $edits = Rename $p.Id $main
  if ($edits.Count -eq 1) {
    [Renqa]::SendText($edits[0], 0x000C, [IntPtr]::Zero, 'Ne pas garder') | Out-Null
    [Renqa]::PostMessage($edits[0], 0x0100, [IntPtr]0x1B, [IntPtr]1) | Out-Null   # Escape
    Start-Sleep -Milliseconds 800
    Check (@([Renqa]::VisibleEdits($main)).Count -eq 0 -and (Bookmarks) -eq $before) 'Escape: editor gone, file unchanged'
    $item = Uia $p.Id 'Bookmark.10'; Check ($item -and $item.Current.Name -eq 'Jeux vidéo') 'Escape: the name shown is still « Jeux vidéo »'
  } else { Check $false 'second F2 shows the editor' }
  Stop-Process -Id $p.Id -Force; Start-Sleep 2
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }; Start-Sleep 1
  # ---- Relaunch: the name came from the file -------------------------------------------------------
  $p = Start-Process "$root\browser.exe" -ArgumentList '--profile default' -PassThru
  $main = Main $p.Id; Start-Sleep 5
  $item = Uia $p.Id 'Bookmark.10'; Check ($item -and $item.Current.Name -eq 'Jeux vidéo') 'relaunch: band shows « Jeux vidéo » (read back from disk)'
  # Same managed client on the address bar's own EDIT (the project's reference for a labelled child EDIT).
  $addr = Uia $p.Id 'Address'; if ($addr) { $addr.SetFocus(); Start-Sleep -Milliseconds 500; $f = $AE::FocusedElement; "     reference, address EDIT in this managed client: type $($f.Current.ControlType.ProgrammaticName) class '$($f.Current.ClassName)' name '$($f.Current.Name)'" }
  Stop-Process -Id $p.Id -Force; Start-Sleep 1
} finally {
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Start-Sleep 1; Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "$($script:fails) failure(s)"; exit 1 }
"PASS folder rename in place in the real browser: F2 on the bar folder, UIA Edit « Nom », Enter writes, Escape keeps, relaunch reads"
