param(
    [Parameter(Mandatory)][string]$BuildDir,   # a built browser (its own portable profile lives there)
    [Parameter(Mandatory)][string]$Label,      # file prefix: before / after
    [string]$OutDir = "$PSScriptRoot/../../build/qa/tooltips"
)
# Real-window check of hover tooltips (user request 2026-11-20: centred on their anchor,
# slid only to stay inside the window). Opens the browser on the two local audio pages of
# tests/audio/live, hovers toolbar buttons, the media panel's info glyph and the history
# drawer's close button with SetCursorPos (no clicks, no keys), measures each tip against its
# anchor and saves a crop per case. A locked session makes captures fail: retried every minute
# without touching anything else. Only this run's browser.exe is ever stopped.
$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out RECT p);
    [DllImport("user32.dll")] public static extern IntPtr SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int n);
    public static List<IntPtr> Visible(int pid, string cls) {
        var list = new List<IntPtr>();
        EnumWindows((h, p) => { int id; GetWindowThreadProcessId(h, out id); if (id == pid && IsWindowVisible(h)) { var s = new StringBuilder(64); GetClassName(h, s, 64); if (s.ToString() == cls) list.Add(h); } return true; }, IntPtr.Zero);
        return list;
    }
    public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
    public static RECT Frame(IntPtr h) { RECT r; if (DwmGetWindowAttribute(h, 9, out r, 16) != 0) GetWindowRect(h, out r); return r; }
}
'@
[W]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null   # per-monitor v2: every rect in physical px

# One real-window suite at a time: wait while a test browser of another worktree runs.
function OtherTestBrowsers { @(Get-Process browser -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -notlike "$BuildDir*" -and ($_.Path -like '*\build\*' -or $_.Path -like '*\scratchpad\*') }) }
while ((OtherTestBrowsers).Count) { Write-Output ("waiting: another test browser runs: " + ((OtherTestBrowsers | ForEach-Object Path) -join ', ')); Start-Sleep 30 }

$A = [System.Windows.Automation.AutomationElement]
$log = Join-Path $BuildDir 'prototype.log'
function Log { for ($i = 0; $i -lt 5; $i++) { try { $f = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite,Delete'); try { return (New-Object IO.StreamReader($f)).ReadToEnd() } finally { $f.Dispose() } } catch [IO.IOException] { Start-Sleep -Milliseconds 50 } }; '' }
function WaitFor([string]$pattern, [int]$seconds = 30) { $d = (Get-Date).AddSeconds($seconds); while ((Get-Date) -lt $d) { if ((Log) -match $pattern) { return } Start-Sleep -Milliseconds 250 }; throw "Timed out waiting for log /$pattern/" }
function Find([string]$id, [int]$seconds = 10) {
    $d = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $d) {
        foreach ($w in $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, (New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, $browser.Id)))) {
            $e = $w.FindFirst([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id)))
            if ($e) { return $e }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "UI Automation element $id not found"
}
function Rect($e) { $b = $e.Current.BoundingRectangle; @{L = [int]$b.Left; T = [int]$b.Top; R = [int]$b.Right; B = [int]$b.Bottom} }
function Park { $w = [W]::Frame($script:main); [W]::SetCursorPos([int](($w.L + $w.R) / 2), [int]($w.T + ($w.B - $w.T) * 0.7)) | Out-Null; Start-Sleep -Milliseconds 700 }
function Grab([int]$x, [int]$y, [int]$w, [int]$h, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h); $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size); $bmp.Save($path) } finally { $g.Dispose(); $bmp.Dispose() }
}
$results = @()
# Hover `anchor` (screen px), wait for a visible window of `cls`, measure it, save a crop.
function Case([string]$name, [hashtable]$anchor, [string]$cls, [int]$waitMs = 1100) {
    while ($true) {
        Park
        [W]::SetCursorPos([int](($anchor.L + $anchor.R) / 2), [int](($anchor.T + $anchor.B) / 2)) | Out-Null
        Start-Sleep -Milliseconds 120
        [W]::SetCursorPos([int](($anchor.L + $anchor.R) / 2) + 1, [int](($anchor.T + $anchor.B) / 2)) | Out-Null
        Start-Sleep -Milliseconds $waitMs
        $tips = [W]::Visible($browser.Id, $cls)
        if (!$tips.Count) {
            $win = [W]::Frame($script:main); try { Grab $win.L $win.T ($win.R - $win.L) ($win.B - $win.T) (Join-Path $OutDir "$Label-$name-FAILED.png") } catch {}
            throw "$name : no visible $cls tooltip (anchor $($anchor.L),$($anchor.T)..$($anchor.R),$($anchor.B))"
        }
        $t = [W]::Rect($tips[0]); $win = [W]::Frame($script:main)
        $delta = (($t.L + $t.R) - ($anchor.L + $anchor.R)) / 2
        $line = "{0,-8} {1,-22} anchor {2}..{3}  tip {4}..{5} ({6} px)  centre offset {7,6} px  left room {8}  right room {9}" -f $Label, $name, $anchor.L, $anchor.R, $t.L, $t.R, ($t.R - $t.L), $delta, ($t.L - $win.L), ($win.R - $t.R)
        $x = [Math]::Max($win.L, [Math]::Min($anchor.L, $t.L) - 60); $r = [Math]::Min($win.R, [Math]::Max($anchor.R, $t.R) + 60)
        $y = [Math]::Max($win.T, [Math]::Min($anchor.T, $t.T) - 24); $b = [Math]::Min($win.B, [Math]::Max($anchor.B, $t.B) + 24)
        try { Grab $x $y ($r - $x) ($b - $y) (Join-Path $OutDir "$Label-$name.png"); Write-Host $line; return $line }
        catch { Write-Host "$name : capture failed ($($_.Exception.Message)): session locked? retrying in 60 s"; Start-Sleep 60 }
    }
}

$port = 8766
$session = "browser-session-2`n0`n2`n`"http://127.0.0.1:$port/music.html`"`n`"http://localhost:$port/sfx.html`"`n0`n"
[IO.File]::WriteAllText((Join-Path $BuildDir 'session'), $session)
[IO.File]::WriteAllText($log, '')
# Agent configs (Claude / Codex) of the real user are never read or written by this run.
$sandbox = Join-Path $BuildDir 'qa-env'; New-Item -ItemType Directory -Force "$sandbox/AppData", "$sandbox/codex" | Out-Null
$server = Start-Process python -ArgumentList '-m', 'http.server', "$port", '--bind', '127.0.0.1', '--directory', (Resolve-Path "$PSScriptRoot/../audio/live").Path -PassThru -WindowStyle Hidden
$browser = $null
$cursor = New-Object W+RECT; [W]::GetCursorPos([ref]$cursor) | Out-Null
try {
    $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--autoplay-policy=no-user-gesture-required'
    $keep = @{APPDATA = $env:APPDATA; CODEX_HOME = $env:CODEX_HOME}
    $env:APPDATA = "$sandbox\AppData"; $env:CODEX_HOME = "$sandbox\codex"
    $browser = Start-Process (Join-Path $BuildDir 'browser.exe') -WorkingDirectory $BuildDir -PassThru
    Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS; $env:APPDATA = $keep.APPDATA; if ($keep.CODEX_HOME) { $env:CODEX_HOME = $keep.CODEX_HOME } else { Remove-Item Env:CODEX_HOME }
    WaitFor 'AUDIO toolbar button shown' 40
    WaitFor 'AUDIO audible localhost' 40
    $script:main = [IntPtr]$browser.MainWindowHandle
    for ($i = 0; $i -lt 40 -and $script:main -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 250; $browser.Refresh(); $script:main = [IntPtr]$browser.MainWindowHandle }
    Start-Sleep 1
    $results += Case 'toolbar-back' (Rect (Find 'Back')) 'LisiereTooltip'
    $results += Case 'toolbar-reload' (Rect (Find 'Reload')) 'LisiereTooltip'
    $results += Case 'toolbar-site-audio' (Rect (Find 'SiteAudio')) 'LisiereTooltip'
    $results += Case 'toolbar-settings' (Rect (Find 'Settings')) 'LisiereTooltip'
    Park
    (Find 'SiteAudio').GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    WaitFor 'AUDIO panel opened'
    Start-Sleep -Milliseconds 600
    $info = $null; foreach ($i in 0, 1) { try { $info = Find "AudioInfo$i" 2; break } catch {} }
    if (!$info) { throw 'media panel: no info glyph' }
    $results += Case 'media-panel-info' (Rect $info) 'LisiereTooltip'
    (Find 'AudioClose').GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 600
    # History drawer: its close button carries a comctl tooltip (TOOLTIPS_CLASS).
    (Find 'History').GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 1200
    $drawer = [W]::Visible($browser.Id, 'LisiereHistoryDrawer')
    if (!$drawer.Count) { throw 'history drawer not visible' }
    $d = [W]::Rect($drawer[0]); $s = [W]::GetDpiForWindow($drawer[0]) / 96.0
    # downloads.h Panel::Close(): {244 - 12, 12, 244 + 12, 36} DIP in the drawer's client area.
    $close = @{L = [int]($d.L + 232 * $s); T = [int]($d.T + 12 * $s); R = [int]($d.L + 256 * $s); B = [int]($d.T + 36 * $s)}
    $results += Case 'history-close' $close 'tooltips_class32' 1600
    Park
}
finally {
    if ($browser -and !$browser.HasExited) { Stop-Process -Id $browser.Id -Force }
    if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id }
    [W]::SetCursorPos($cursor.L, $cursor.T) | Out-Null
}
$results | Set-Content -Encoding utf8 (Join-Path $OutDir "$Label-measures.txt")
