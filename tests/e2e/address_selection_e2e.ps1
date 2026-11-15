param([string]$Fixture = 'build/address-selection-app', [int]$Port = 8771, [string]$Evidence = 'design/profile-address-selection-evidence')
# Address selection in the profile colour on a REAL browser.exe (design/profile-address-selection-report.md).
# A fresh copy of the fixture build runs in a new temporary root (its own profile root, disposable profiles);
# only processes of that copy are touched, nothing of the user's profile is read. The field is focused through
# UI Automation (Address SetFocus = FocusAddress: select all), the window is captured with PrintWindow
# PW_RENDERFULLCONTENT (DWM-composed pixels) and the EDIT's rectangle analysed:
#   - no pixel of the system highlight; the profile's own fill present; brightest selected-text pixel vs fill >= 4.5:1.
# Cases: teal profile (the screenshot's), warm profile, neutral « Personnel » (grey, not blue), private window.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$fix = (Resolve-Path $Fixture).Path
$root = Join-Path $env:TEMP ("browser-selqa-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$script:fails = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:fails++ } }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Drawing; using System.Runtime.InteropServices;
public static class Selqa {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string cls, string name);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern uint GetSysColor(int i);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr h, uint m, IntPtr w, string l);
  public static IntPtr Find(uint pid, string cls) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (p == pid && sb.ToString() == cls && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
  // The EDIT's rectangle out of the composed window (PW_RENDERFULLCONTENT = 2).
  public static Bitmap Field(IntPtr main, IntPtr edit) {
    RECT w, e; GetWindowRect(main, out w); GetWindowRect(edit, out e);
    using (var all = new Bitmap(w.R - w.L, w.B - w.T)) {
      using (var g = Graphics.FromImage(all)) { var dc = g.GetHdc(); PrintWindow(main, dc, 2); g.ReleaseHdc(dc); }
      return all.Clone(new Rectangle(e.L - w.L, e.T - w.T, e.R - e.L, e.B - e.T), all.PixelFormat);
    }
  }
  static double Lin(int c) { double v = c / 255.0; return v <= .04045 ? v / 12.92 : Math.Pow((v + .055) / 1.055, 2.4); }
  public static double Lum(Color c) { return .2126 * Lin(c.R) + .7152 * Lin(c.G) + .0722 * Lin(c.B); }
  public static double Contrast(Color a, Color b) { double x = Lum(a), y = Lum(b); if (x < y) { var t = x; x = y; y = t; } return (x + .05) / (y + .05); }
  // fill: pixels of the exact fill; hi: pixels of the system highlight; best: brightest non-fill pixel inside the fill's bounding box vs the fill.
  public static string Analyse(Bitmap b, Color fill) {
    uint s = GetSysColor(13); var hi = Color.FromArgb((int)(s & 255), (int)((s >> 8) & 255), (int)((s >> 16) & 255));
    int n = 0, h = 0, l = int.MaxValue, r = -1, t = int.MaxValue, bo = -1;
    for (int y = 0; y < b.Height; y++) for (int x = 0; x < b.Width; x++) { var c = b.GetPixel(x, y); if (c.R == fill.R && c.G == fill.G && c.B == fill.B) { n++; l = Math.Min(l, x); r = Math.Max(r, x); t = Math.Min(t, y); bo = Math.Max(bo, y); } if (c.R == hi.R && c.G == hi.G && c.B == hi.B && !(hi.R == fill.R && hi.G == fill.G && hi.B == fill.B)) h++; }
    double best = 0; for (int y = Math.Max(t, 0); y <= bo; y++) for (int x = Math.Max(l, 0); x <= r; x++) { var c = b.GetPixel(x, y); if (!(c.R == fill.R && c.G == fill.G && c.B == fill.B)) best = Math.Max(best, Contrast(c, fill)); }
    return n + " " + h + " " + best.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + " " + l + " " + r;
  }
}
"@
$AE = [System.Windows.Automation.AutomationElement]
function Procs { Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like "$root\*" } }
function Main([int]$procId) { for ($i = 0; $i -lt 60; $i++) { $h = [Selqa]::Find([uint32]$procId, 'LisiereBrowser'); if ($h -ne [IntPtr]::Zero) { return $h }; Start-Sleep -Milliseconds 250 }; throw "no window for $procId" }
function Address([int]$procId) {
  $cond = New-Object System.Windows.Automation.AndCondition((New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $procId)), (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, 'Address')))
  for ($i = 0; $i -lt 40; $i++) { $e = $AE::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond); if ($e) { return $e }; Start-Sleep -Milliseconds 250 }
  throw "UIA Address not found for $procId"
}
$enc = New-Object System.Text.UTF8Encoding($false)
function W([string]$path, [string]$text) { New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null; [IO.File]::WriteAllText($path, $text, $enc) }
$u = "http://127.0.0.1:$Port"
function Session([string]$url) { "browser-session-2`n0`n1`n`"$url`"`n0`n" }

New-Item -ItemType Directory -Force $root | Out-Null
robocopy $fix $root /E /NFL /NDL /NJH /NJS /NP /XD profile sessions profiles adblock favicons windows /XF profiles.txt session settings history downloads bookmarks prototype.log engine-args | Out-Null
# Disposable profiles: Personnel (neutral, default), Sarcelle (teal, the screenshot), Chaud (warm, max intensity).
W "$root\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=pteal0001|briefcase|190|120|0|Sarcelle`np=pwarm0001|users-three|50|160|0|Chaud`n"
W "$root\sessions\wsN" (Session "$u/show")
W "$root\profiles\pteal0001\sessions\wsT" (Session "$u/show")
W "$root\profiles\pwarm0001\sessions\wsW" (Session "$u/show")
foreach ($p in @("$root", "$root\profiles\pteal0001", "$root\profiles\pwarm0001")) { W "$p\settings" "browser-settings 1`nrestore-tabs=1`nask-where-to-save=0`n" }
$server = Start-Process python -ArgumentList "-I `"$PWD\tests\e2e\profiles_switch_server.py`" $Port" -PassThru -WindowStyle Hidden
Start-Sleep 1
"root: $root"
New-Item -ItemType Directory -Force $Evidence | Out-Null
try {
  # Expected fills: ProfileColor::AccentFill(190, .12) / (50, .16), NeutralFill, the reference blue (private).
  $cases = @(
    @{ name = 'teal profile (screenshot)'; args = '--profile pteal0001 --restore-window wsT'; fill = '#00837E'; png = 'app-after-teal.png' },
    @{ name = 'warm profile (max intensity)'; args = '--profile pwarm0001 --restore-window wsW'; fill = '#BD5800'; png = 'app-after-warm.png' },
    @{ name = 'neutral Personnel (grey, not blue)'; args = '--profile default --restore-window wsN'; fill = '#747474'; png = 'app-after-neutral.png' },
    @{ name = 'private window (its own reference blue)'; args = '--private'; fill = '#3E67FF'; png = 'app-after-private.png'; value = 'https://exemple.fr/fenetre-privee' },
    # Independent review counterexamples: Arabic IDN (visual bidi order), maximum-length URL scrolled to its end.
    @{ name = 'teal, Arabic IDN 8..12'; args = '--profile pteal0001 --restore-window wsT'; fill = '#00837E'; png = 'app-after-arabic-idn.png'; value = ('https://' + [char]0x645 + [char]0x62B + [char]0x627 + [char]0x644 + '.' + [char]0x625 + [char]0x62E + [char]0x62A + [char]0x628 + [char]0x627 + [char]0x631 + '/' + [char]0x645 + [char]0x633 + [char]0x627 + [char]0x631); from = 8; to = 12; min = 100 },
    @{ name = 'teal, 7920-char URL select all after end scroll'; args = '--profile pteal0001 --restore-window wsT'; fill = '#00837E'; png = 'app-after-max-url.png'; value = ('https://example.com/' + ('W' * 7900)); scrollEnd = $true }
  )
  foreach ($c in $cases) {
    $p = Start-Process "$root\browser.exe" -ArgumentList $c.args -PassThru
    $main = Main $p.Id; Start-Sleep 5
    $addr = Address $p.Id; $edit = [Selqa]::FindWindowEx($main, [IntPtr]::Zero, 'Edit', $null)
    $addr.SetFocus()   # FocusAddress: select all, as Ctrl+L
    if ($c.value) {   # the private home tab has no address: put one in the focused field (WM_SETTEXT), then select it all
      Start-Sleep -Milliseconds 400
      [Selqa]::SendText($edit, 0x000C, [IntPtr]::Zero, $c.value) | Out-Null
      if ($c.scrollEnd) { $n = $c.value.Length; [Selqa]::SendMessage($edit, 0x00B1, [IntPtr]($n - 5), [IntPtr]$n) | Out-Null; [Selqa]::SendMessage($edit, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }   # EM_SCROLLCARET
      $from = if ($null -ne $c.from) { [IntPtr]$c.from } else { [IntPtr]::Zero }; $to = if ($null -ne $c.to) { [IntPtr]$c.to } else { [IntPtr](-1) }
      [Selqa]::SendMessage($edit, 0x00B1, $from, $to) | Out-Null   # EM_SETSEL
    }
    Start-Sleep -Milliseconds 700
    Check ($edit -ne [IntPtr]::Zero) "$($c.name): native address EDIT found"
    $bmp = [Selqa]::Field($main, $edit); $bmp.Save((Join-Path $Evidence $c.png), [System.Drawing.Imaging.ImageFormat]::Png)
    $f = [System.Drawing.ColorTranslator]::FromHtml($c.fill)
    $r = ([Selqa]::Analyse($bmp, $f)) -split ' '; $bmp.Dispose()
    "     $($c.name): fill $($c.fill) pixels=$($r[0]) x $($r[3])..$($r[4]); system-highlight pixels=$($r[1]); brightest selected text vs fill $($r[2]):1"
    Check ([int]$r[0] -gt $(if ($c.min) { $c.min } else { 200 })) "$($c.name): selection painted in the profile fill $($c.fill)"
    Check ([int]$r[1] -eq 0) "$($c.name): no system-highlight pixel"
    Check ([double]::Parse($r[2], [Globalization.CultureInfo]::InvariantCulture) -ge 4.5) "$($c.name): rendered selected text >= 4.5:1 on the fill"
    Stop-Process -Id $p.Id -Force; Start-Sleep 2
    Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }; Start-Sleep 1
  }
} finally {
  Procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -Force }
  Start-Sleep 1; Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fails) { "$($script:fails) failure(s)"; exit 1 }
"PASS address selection in the real browser follows the window's profile (teal, warm, neutral grey, private)"
