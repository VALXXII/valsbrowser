param([string]$Build = 'build/v2', [string]$Fixture = 'build/e2e-select-all')
# Real app: Ctrl+L, type « bonjour monde », Ctrl+A, type « x » -> the address field holds « x ». Throwaway copy, fresh
# profile, no site visited; only this fixture's processes are stopped (exact path).
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, int m, IntPtr w, System.Text.StringBuilder l);
  public static string EditText(IntPtr main) { IntPtr e = FindWindowExW(main, IntPtr.Zero, "Edit", null); if (e == IntPtr.Zero) return null; var sb = new System.Text.StringBuilder(4096); SendMessageW(e, 0x000D, (IntPtr)4096, sb); return sb.ToString(); } }
'@
if (Test-Path $Fixture) { Remove-Item -Recurse -Force $Fixture }
New-Item -ItemType Directory -Force $Fixture | Out-Null
Copy-Item -Recurse -Force (Join-Path $Build '*') $Fixture
Get-ChildItem $Fixture -Include 'profile', 'sessions', '*.log', 'session', 'settings' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$exe = Join-Path (Resolve-Path $Fixture).Path 'browser.exe'
function Fixture-Pids { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $exe) } | ForEach-Object { $_.ProcessId } }
$UIA = [Windows.Automation.AutomationElement]
function Address-Text { [W]::EditText($proc.MainWindowHandle) }
$fails = 0; function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
try {
  $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
  for ($i = 0; $i -lt 60 -and $proc.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 250; $proc.Refresh() }
  Start-Sleep 3; [void][W]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 500
  [System.Windows.Forms.SendKeys]::SendWait('^l'); Start-Sleep -Milliseconds 400
  [System.Windows.Forms.SendKeys]::SendWait('bonjour monde'); for ($i = 0; $i -lt 20 -and (Address-Text) -notlike 'bonjour monde*'; $i++) { Start-Sleep -Milliseconds 150 }
  $typed = Address-Text; Check ($typed -like 'bonjour monde*') "typed in the address field: « $typed »"
  [System.Windows.Forms.SendKeys]::SendWait('^a'); Start-Sleep -Milliseconds 300
  [System.Windows.Forms.SendKeys]::SendWait('x'); Start-Sleep -Milliseconds 400
  $after = Address-Text; Check ($after -eq 'x') "after Ctrl+A then « x »: « $after »"
  [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
} finally { foreach ($p in Fixture-Pids) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
if ($fails) { "$fails failure(s)"; exit 1 }
'select-all E2E: all checks passed'
