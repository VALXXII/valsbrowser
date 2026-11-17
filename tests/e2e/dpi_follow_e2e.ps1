param([string]$Build = 'build/v2', [string]$Fixture = 'build/e2e-dpi-follow', [string]$Shots = 'build/dpi-follow-evidence')
# Real app, real monitors: the browser window opened on one monitor, Réglages opened there, then the window moved to
# a monitor with another scale and Réglages opened again. Screenshots of both (compare text sizes with the top bar).
# Needs two monitors at different scales (prints SKIP otherwise). Throwaway copy, fresh profile, no site visited.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
}
'@
[void][W]::SetProcessDpiAwarenessContext([IntPtr]-4)
$screens = [System.Windows.Forms.Screen]::AllScreens
if ($screens.Count -lt 2) { 'SKIP: one monitor'; exit 0 }
if (Test-Path $Fixture) { Remove-Item -Recurse -Force $Fixture }
New-Item -ItemType Directory -Force $Fixture, $Shots | Out-Null
Copy-Item -Recurse -Force (Join-Path $Build '*') $Fixture
Get-ChildItem $Fixture -Include 'profile', 'sessions', '*.log', 'session', 'settings' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$fix = (Resolve-Path $Fixture).Path; $exe = Join-Path $fix 'browser.exe'
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
$UIA = [Windows.Automation.AutomationElement]
function Find-Name([string]$name) { foreach ($p in @(Fixture-Pids)) { $c = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($UIA::ProcessIdProperty, [int]$p)), (New-Object Windows.Automation.PropertyCondition($UIA::NameProperty, $name)), (New-Object Windows.Automation.PropertyCondition($UIA::ControlTypeProperty, [Windows.Automation.ControlType]::Button))); $e = $UIA::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants, $c); if ($e) { return $e } }; return $null }
function Shot([IntPtr]$h, [string]$name) {
  $r = New-Object System.Drawing.Rectangle; $e = $UIA::FromHandle($h); $b = $e.Current.BoundingRectangle
  $bmp = New-Object System.Drawing.Bitmap([int]$b.Width, [int]$b.Height); $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen([int]$b.X, [int]$b.Y, 0, 0, $bmp.Size); $g.Dispose(); $p = Join-Path (Resolve-Path $Shots) "$name.png"; $bmp.Save($p); $bmp.Dispose(); "     shot: $p"
}
foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }
try {
  $proc = Start-Process -FilePath $exe -WorkingDirectory $fix -PassThru
  for ($i = 0; $i -lt 60 -and $proc.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 250; $proc.Refresh() }
  $h = $proc.MainWindowHandle; Start-Sleep 3
  $order = @($screens | Sort-Object { -not $_.Primary })
  foreach ($s in $order) {
    $wa = $s.WorkingArea; [void][W]::ShowWindow($h, 9); [void][W]::SetWindowPos($h, [IntPtr]::Zero, $wa.X + 60, $wa.Y + 60, 1200, 800, 0x14); Start-Sleep 1
    [void][W]::SetForegroundWindow($h); Start-Sleep -Milliseconds 500
    $dpi = [W]::GetDpiForWindow($h); $tag = if ($s.Primary) { 'primary' } else { 'second' }
    "monitor $tag dpi $dpi"
    $btn = Find-Name 'Réglages'; if (!$btn) { 'FAIL no Réglages button'; continue }
    $btn.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke(); Start-Sleep 1.5
    Shot $h "$tag-$dpi"
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}'); Start-Sleep 1
  }
} finally { foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
