param([string]$Fixture = 'build/import-app', [string]$Evidence = 'design/browser-data-import-evidence', [switch]$RequireVisual)
# « Importer depuis un navigateur » on a REAL browser.exe, French then English (design/browser-data-import-plan.md).
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
$run = Join-Path $repo ("build\import-e2e\run-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
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
  $p = Start-Process "$app\browser.exe" -ArgumentList '--profile', 'default', '--import-sources', "`"$sources`"" -PassThru
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
W "$app\settings" "browser-settings 1`nrestore-tabs=0`nask-where-to-save=0`n"
W "$app\bookmarks" "browser-bookmarks 1`n3`t1`tL`t1794100000000`tMien`thttps://mine.example/`nend`t1`n"
W "$app\history" "browser-history 1`n3`t1763000000000`thttps://mine.example/`tMine`n"
$sourcesBefore = TreeHash $sources
"run: $run"
$bmFile = "$app\bookmarks"; $hFile = "$app\history"
try {
  # ================= French =================================================================================
  $p, $main = Launch ''
  $procId = $p.Id
  $d = OpenImport $procId $main 'Confidentialité'
  $fav, $hist, $cook = Check3 $d @('Favoris', 'Historique', 'Cookies et connexions')
  Check ($d.Current.Name -eq 'Importer depuis un navigateur') "FR dialog title: $($d.Current.Name)"
  Check ($d.Current.HelpText -like '*« Personnel »*') "FR destination names this window's profile: $($d.Current.HelpText)"
  $select = $d.FindFirst($TS::Descendants, (Cond $AE::NameProperty 'Depuis'))
  Check ($select.Current.HelpText -eq 'Chrome') "FR first source is the synthetic Chrome profile: $($select.Current.HelpText)"
  Check ((ToggleState $fav) -eq 'On' -and $fav.Current.IsEnabled -and $fav.Current.HelpText -eq 'Dans la barre de favoris et Autres favoris, comme dans Chrome') "FR bookmarks on, destination folder named: $($fav.Current.HelpText)"
  Check ((ToggleState $hist) -eq 'On' -and $hist.Current.IsEnabled) 'FR history on'
  Check ((ToggleState $cook) -eq 'Off' -and !$cook.Current.IsEnabled -and $cook.Current.HelpText -eq 'Non disponibles : reconnectez-vous aux sites.') "FR cookies disabled with the reason: $($cook.Current.HelpText)"
  Visual $main $procId 'fr-1-dialog-chrome'
  # Cancel: nothing written.
  $bm0 = Hash $bmFile; $h0 = Hash $hFile
  Invoke (Uia $procId 'DialogCancel'); Check (Gone $procId 'ImportBrowserData') 'FR cancel closes the dialog'
  Start-Sleep -Milliseconds 500
  Check ((Hash $bmFile) -eq $bm0 -and (Hash $hFile) -eq $h0) 'FR cancel: bookmarks and history files unchanged'

  # Import Chrome (bookmarks + history).
  $d = OpenImport $procId $main 'Confidentialité'
  Invoke (Uia $procId 'DialogImport')
  $r = Result $procId
  Check ($null -ne $r) 'FR import: summary shown'
  if ($r) {
    $text = $r.Current.HelpText; "     summary: $($r.Current.Name) / $text"
    Check ($r.Current.Name -eq 'Importation terminée') 'FR summary title: terminée'
    Check ($text -like '*Favoris : 6 importés · 1 adresse non prise en charge*') 'FR bookmarks: 6 imported, 1 unsupported (javascript:)'
    Check ($text -like '*Historique : 2 importés · 2 adresses non prises en charge*') 'FR history: 2 imported, 2 unsupported (chrome:, search page); hidden row not offered'
    Check ($text -like '*Cookies : non transférés. Reconnectez-vous aux sites.*') 'FR summary says cookies were not transferred'
    Visual $main $procId 'fr-2-summary-chrome'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null
  }
  $bm = ReadText $bmFile; $hs = ReadText $hFile
  Check ($bm -like "*`tMien`thttps://mine.example/*") 'FR existing bookmark kept'
  Check ($bm -notlike "*`tImportés de Chrome`t*" -and $bm -like "*`t1`tF`t*`tTravail 🛠 日本`t*" -and $bm -like "*`tTravail 🛠 日本`t*" -and $bm -like "*`tAccueil — été ☀`thttps://home.example/*") 'FR imported tree with Unicode titles on disk, straight on the bar (no « Importés de Chrome » group)'
  Check (([regex]::Matches($bm, 'https://docs\.example/a')).Count -eq 3) 'FR intentional duplicates kept (2 in Travail, 1 in Perso)'
  Check ($bm -notlike '*javascript:*') 'FR javascript: bookmark not imported'
  Check ($hs -like '*https://mine.example/*' -and $hs -like '*https://news.example/today*' -and $hs -notlike '*chrome://*' -and $hs -notlike '*hidden.example*' -and $hs -notlike '*google.com/search*') 'FR history on disk: existing kept, imported added, internal/hidden/search excluded'
  Check ($hs -like "*`t1763100000000`thttps://news.example/today`t*") 'FR history visit time converted from the Chromium epoch'

  # Repeat: nothing added, files byte-identical.
  $bm1 = Hash $bmFile; $h1 = Hash $hFile
  $d = OpenImport $procId $main 'Confidentialité'; Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  if ($r) { $text = $r.Current.HelpText; "     repeat: $text"
    Check ($text -like '*Favoris : 0 importé · 6 déjà présents*' -and $text -like '*Historique : 0 importé · 2 déjà présents*') 'FR repeat import: everything already there'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
  Start-Sleep -Milliseconds 500
  Check ((Hash $bmFile) -eq $bm1 -and (Hash $hFile) -eq $h1) 'FR repeat import: files unchanged'

  # Source in use (a running Chrome holds History for writing): dialog stays open with the reason; retry works.
  $holder = [IO.File]::Open("$sources\Local\Google\Chrome\User Data\Default\History", 'Open', 'ReadWrite', 'ReadWrite')
  $d = OpenImport $procId $main 'Confidentialité'
  $fav = $d.FindFirst($TS::Descendants, (Cond $AE::NameProperty 'Favoris')); Toggle $fav
  Check ((ToggleState $fav) -eq 'Off') 'FR bookmarks unticked (history only)'
  Invoke (Uia $procId 'DialogImport'); Start-Sleep -Milliseconds 1500
  $d = Uia $procId 'ImportBrowserData'
  Check ($d -and $d.Current.HelpText -eq 'Historique : fermez Chrome, puis réessayez.') "FR source busy: dialog stays, says to close Chrome: $($d.Current.HelpText)"
  Visual $main $procId 'fr-3-source-busy'
  $holder.Close()
  Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  Check ($r -and $r.Current.HelpText -like 'Historique : 0 importé · 2 déjà présents*') 'FR retry after closing the source: imported (nothing duplicated)'
  if ($r) { Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }

  # Destination save failure on one category: bookmarks file read-only → partial, honest per category.
  $d = OpenImport $procId $main 'Confidentialité'
  Choose $procId $d 'Depuis' 'Firefox'
  $fav, $hist, $cook = Check3 $d @('Favoris', 'Historique', 'Cookies et connexions')
  Check ($fav.Current.HelpText -eq 'Dans la barre de favoris et Autres favoris, comme dans Firefox' -and (ToggleState $hist) -eq 'On') 'FR Firefox source: bookmarks onto the bar / Autres favoris, history on'
  $bmBefore = Hash $bmFile
  Set-ItemProperty -LiteralPath $bmFile -Name IsReadOnly -Value $true
  Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  Set-ItemProperty -LiteralPath $bmFile -Name IsReadOnly -Value $false
  if ($r) { $text = $r.Current.HelpText; "     partial: $($r.Current.Name) / $text"
    Check ($r.Current.Name -eq 'Importation partielle') 'FR save failure: « Importation partielle »'
    Check ($text -like '*Favoris : enregistrement impossible, rien n''a changé. Réessayez.*' -and $text -like '*Historique : 1 importé*') 'FR save failure: bookmarks failed honestly, history imported'
    Visual $main $procId 'fr-4-partial-save-failure'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
  Check ((Hash $bmFile) -eq $bmBefore) 'FR save failure: bookmarks file untouched'
  $d = OpenImport $procId $main 'Confidentialité'; Choose $procId $d 'Depuis' 'Firefox'; Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  if ($r) { $text = $r.Current.HelpText
    Check ($text -like '*Favoris : 1 importé*' -and $text -like '*Historique : 0 importé · 1 déjà présent*') "FR retry after the failure: bookmarks now saved, history not doubled: $text"
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }

  # HTML bookmark file through the real file dialog (UIA Value + Invoke on its own controls).
  $d = OpenImport $procId $main 'Confidentialité'
  Choose $procId $d 'Depuis' 'Fichier de favoris HTML…'
  $fav, $hist, $cook = Check3 $d @('Favoris', 'Historique', 'Cookies et connexions')
  Check (!$hist.Current.IsEnabled -and $hist.Current.HelpText -eq 'Un fichier HTML ne contient que des favoris.') 'FR HTML source: history unavailable, said why'
  Invoke (Uia $procId 'DialogImport')
  # The common file dialog (a window of this process): name set with WM_SETTEXT on its Edit, « Ouvrir » with a
  # posted BM_CLICK. Its controls are reached by AutomationId (1148 = file name, 1 = Open), never by focus or keys.
  $name = $null; $open = $null
  for ($i = 0; $i -lt 50 -and !($name -and $open); $i++) {
    Start-Sleep -Milliseconds 200
    foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) {
      foreach ($e in $top.FindAll($TS::Descendants, (Cond $AE::AutomationIdProperty '1148'))) { if ($e.Current.ClassName -eq 'Edit') { $name = $e } }
      foreach ($e in $top.FindAll($TS::Descendants, (Cond $AE::AutomationIdProperty '1'))) { if ($e.Current.ClassName -eq 'Button') { $open = $e } }
    }
  }
  if ($name -and $open) {
    [Impqa]::SendText([IntPtr]$name.Current.NativeWindowHandle, 0x000C, [IntPtr]0, "$sources\export é#.html") | Out-Null
    Start-Sleep -Milliseconds 300
    [Impqa]::PostMessage([IntPtr]$open.Current.NativeWindowHandle, 0x00F5, [IntPtr]0, [IntPtr]0) | Out-Null
    $r = Result $procId
    if ($r) { $text = $r.Current.HelpText; "     html: $text"
      Check ($text -eq 'Favoris : 2 importés · 1 adresse non prise en charge') 'FR HTML import: 2 imported, javascript: refused, script content never read as a bookmark, no cookie line'
      Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
    $bm = ReadText $bmFile
    Check ($bm -notlike "*`tImportés d'un fichier HTML`t*" -and $bm -like "*`t1`tF`t*`tExporté & rangé`t*" -and $bm -like "*`tUn — ①`thttps://html.example/one*" -and $bm -notlike '*script.example*') 'FR HTML bookmarks on disk, entities decoded'
  } else { Check $false 'FR HTML: file dialog controls not found' }
  $logText = ReadText "$app\prototype.log"
  Check ($logText -like '*IMPORT bookmarks saved imported=6*' -and $logText -notmatch 'home\.example|docs\.example|news\.example|html\.example|Travail|sources') 'FR log: counts only, no URL, title or source path'
  Close $procId $main

  # ================= English ================================================================================
  $p, $main = Launch 'en'
  $procId = $p.Id
  $d = OpenImport $procId $main 'Privacy'
  $fav, $hist, $cook = Check3 $d @('Bookmarks', 'History', 'Cookies and sign-ins')
  Check ($d.Current.Name -eq 'Import from a browser' -and $d.Current.HelpText -like '*“Personnel”*') "EN dialog: $($d.Current.Name) / $($d.Current.HelpText)"
  Check ($cook -and !$cook.Current.IsEnabled -and $cook.Current.HelpText -eq 'Not available: sign in to sites again.') 'EN cookies unavailable, said why'
  Choose $procId $d 'From' 'Edge'
  $fav, $hist, $cook = Check3 $d @('Bookmarks', 'History', 'Cookies and sign-ins')
  Check (!$hist.Current.IsEnabled -and $hist.Current.HelpText -eq 'No history in this profile.' -and $fav.Current.HelpText -eq 'Into the bookmarks bar and Other bookmarks, as in Edge') 'EN Edge profile: bookmarks only, history said missing'
  Visual $main $procId 'en-1-dialog-edge'
  Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  if ($r) { $text = $r.Current.HelpText; "     en: $($r.Current.Name) / $text"
    Check ($r.Current.Name -eq 'Import complete' -and $text -like 'Bookmarks: 1 imported*' -and $text -like '*Cookies: not transferred. Sign in to sites again.*') 'EN summary'
    Visual $main $procId 'en-2-summary-edge'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
  # The Chrome profile imported in French, again in English: same folders, nothing added (stable identity).
  $bmFr = Hash $bmFile; $groups = ([regex]::Matches((ReadText $bmFile), "`t2`tF`t")).Count
  $d = OpenImport $procId $main 'Privacy'; $fav = $d.FindFirst($TS::Descendants, (Cond $AE::NameProperty 'History')); Toggle $fav
  Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  if ($r) { $text = $r.Current.HelpText; "     en repeat: $text"
    Check ($text -like 'Bookmarks: 0 imported · 6 already there · 1 unsupported address*') 'EN repeat of the French Chrome import: everything already there'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
  Check ((Hash $bmFile) -eq $bmFr -and ([regex]::Matches((ReadText $bmFile), "`t2`tF`t")).Count -eq $groups) 'EN repeat: bookmarks file unchanged, no new folder'
  Close $procId $main
  # ================= Profile switch while the dialog is open =========================================================
  # The dialog names « Personnel » as the destination. Its scrim stops pointer and keyboard, but UI Automation can
  # still reach the profile menu under it (pre-existing behaviour of the window modal): the window switches to
  # « Travail », then « Importer » is pressed. Nothing may be written to either profile; the dialog says why.
  # (The same check runs when the worker's read comes back; a switch landing exactly during the read is not
  # reproducible deterministically here: the 30 MB read below takes well under a second.)
  $big = Join-Path $run 'sources-big'
  & $python -I "$repo\tests\browser-import\make_sources.py" $big --big
  W "$app\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`n"
  W "$app\profiles\ptravail01\settings" "browser-settings 1`nrestore-tabs=0`nask-where-to-save=0`n"
  Remove-Item -LiteralPath "$app\ui-language" -ErrorAction SilentlyContinue
  function BigLaunch { $q = Start-Process "$app\browser.exe" -ArgumentList '--profile', 'default', '--import-sources', "`"$big`"" -PassThru; $m = Main $q.Id; Start-Sleep 4; return @($q, $m) }
  $p, $main = BigLaunch; $procId = $p.Id
  $bmA = Hash $bmFile; $bmB = Hash "$app\profiles\ptravail01\bookmarks"; $logStart = (ReadText "$app\prototype.log").Length
  $d = OpenImport $procId $main 'Confidentialité'
  Invoke (Uia $procId 'ProfileButton')
  $travail = $null
  for ($i = 0; $i -lt 30 -and !$travail; $i++) { Start-Sleep -Milliseconds 150; foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) { foreach ($e in $top.FindAll($TS::Descendants, (Cond $AE::AutomationIdProperty 'ProfileMenuProfile'))) { if ($e.Current.Name -like 'Travail*') { $travail = $e } } } }
  if ($travail) { Invoke $travail }
  Start-Sleep 5
  $shown = (Uia $procId 'ProfileButton').Current.Name
  "     profile shown after the switch: $shown"
  Check ($travail -and $shown -like '*Travail*') 'switch: the window switched to « Travail » with the dialog open'
  $import = Uia $procId 'DialogImport' '' 10
  if ($import) { Invoke $import; Start-Sleep 3 }
  $d = Uia $procId 'ImportBrowserData' '' 10
  Check ($d -and $d.Current.HelpText -eq 'Le profil a changé : rien n''a été importé.') "switch: « Importer » refused, the dialog says why ($(if ($d) { $d.Current.HelpText }))"
  Visual $main $procId 'fr-5-profile-changed'
  $logs = (ReadText "$app\prototype.log").Substring($logStart) + $(if (Test-Path "$app\profiles\ptravail01\browser.log") { ReadText "$app\profiles\ptravail01\browser.log" } else { '' })
  Check ((Hash $bmFile) -eq $bmA -and (Hash "$app\profiles\ptravail01\bookmarks") -eq $bmB -and $logs -match 'IMPORT not started: the profile changed' -and $logs -notmatch 'IMPORT started from Chrome') 'switch: nothing read, nothing written to « Personnel » nor to « Travail »'
  if ($d) { Invoke (Uia $procId 'DialogCancel') }
  Close $procId $main

  # ================= Capacity (same large source, no switch) =========================================================
  W "$app\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`n"
  $p, $main = BigLaunch; $procId = $p.Id
  $d = OpenImport $procId $main 'Confidentialité'
  Invoke (Uia $procId 'DialogImport'); $r = Result $procId
  if ($r) { $text = $r.Current.HelpText; "     big: $($r.Current.Name) / $text"
    Check ($r.Current.Name -eq 'Importation partielle' -and $text -match 'Favoris : \d+ importés .*non importés \(limite atteinte\)') 'capacity: « partielle », the store limit reported, never silently cut'
    Visual $main $procId 'fr-6-capacity'
    Invoke (Uia $procId 'DialogOk'); Gone $procId 'ImportResult' | Out-Null }
  else { Check $false 'capacity: summary shown' }
  $bmText = ReadText $bmFile
  Check ((([regex]::Matches($bmText, "`n")).Count - 2) -le 10000 -and $bmText -like "*`tMien`thttps://mine.example/*") 'capacity: at most 10 000 entries, existing bookmarks kept'
  Close $procId $main
  # ================= Deterministic mid-read events (test seam --import-hold, dev builds only) =======================
  # The worker parks on Local\browser-import-reached-<pid> / -release-<pid>: right after the read (« read ») or right
  # after the private database snapshot is taken (« snapshot »). Each case then acts at that exact point.
  function HoldLaunch([string]$profile, [string]$phase) { $q = Start-Process "$app\browser.exe" -ArgumentList '--profile', $profile, '--import-sources', "`"$sources`"", '--import-hold', $phase -PassThru; $m = Main $q.Id; Start-Sleep 4; return @($q, $m) }
  function Reached([int]$procId) { for ($i = 0; $i -lt 100; $i++) { try { $e = [System.Threading.EventWaitHandle]::OpenExisting("Local\browser-import-reached-$procId"); $ok = $e.WaitOne(20000); $e.Dispose(); return $ok } catch { Start-Sleep -Milliseconds 100 } }; return $false }
  function Release([int]$procId) { try { $e = [System.Threading.EventWaitHandle]::OpenExisting("Local\browser-import-release-$procId"); $e.Set() | Out-Null; $e.Dispose(); return $true } catch { return $false } }
  function Snapshots([int]$procId) { @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter "browser-import-$procId-*" -ErrorAction SilentlyContinue) }
  function SwitchTo([int]$procId, [string]$name) {
    Invoke (Uia $procId 'ProfileButton'); $t = $null
    for ($i = 0; $i -lt 30 -and !$t; $i++) { Start-Sleep -Milliseconds 150; foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) { foreach ($e in $top.FindAll($TS::Descendants, (Cond $AE::AutomationIdProperty 'ProfileMenuProfile'))) { if ($e.Current.Name -like "$name*") { $t = $e } } } }
    if ($t) { Invoke $t; Start-Sleep 4; return $true }; return $false
  }
  W "$app\profiles.txt" "browser-profiles 1`nlast=default`np=default|house|0|0|1|Personnel`np=ptravail01|briefcase|150|140|0|Travail`n"
  $states = { @{ bmA = Hash $bmFile; hA = Hash $hFile; bmB = Hash "$app\profiles\ptravail01\bookmarks"; hB = Hash "$app\profiles\ptravail01\history"; map = Hash "$app\bookmark-imports" } }

  # (a) Profile switch while the read is parked: the finished read is dropped, nothing written anywhere.
  $p, $main = HoldLaunch 'default' 'read'; $procId = $p.Id
  $before = & $states; $logStart = (ReadText "$app\prototype.log").Length
  $d = OpenImport $procId $main 'Confidentialité'; Invoke (Uia $procId 'DialogImport')
  $parked = Reached $procId
  $switched = SwitchTo $procId 'Travail'
  $released = Release $procId; Start-Sleep 3
  $after = & $states; $appLog = (ReadText "$app\prototype.log").Substring($logStart)
  Check ($parked -and $switched -and $released) 'seam switch: read parked, window switched to « Travail », read released'
  Check (($before.Values -join ',') -eq ($after.Values -join ',')) 'seam switch: nothing written to « Personnel » nor « Travail » (bookmarks, history, folder map)'
  Check ($appLog -match 'IMPORT cancelled: the profile changed' -and $appLog -notmatch 'IMPORT (bookmarks|history) saved') 'seam switch: the read came back after the switch and was dropped'
  Check ((Snapshots $procId).Count -eq 0) 'seam switch: no private snapshot left'
  Visual $main $procId 'fr-7-seam-switch'
  Close $procId $main

  # (b) Window closed while the private snapshot exists: nothing written; the copy left by the ended process is
  #     removed by the next snapshot (next import).
  $p, $main = HoldLaunch 'default' 'snapshot'; $procId = $p.Id
  $before = & $states
  $d = OpenImport $procId $main 'Confidentialité'
  $fav = $d.FindFirst($TS::Descendants, (Cond $AE::NameProperty 'Favoris')); Toggle $fav   # history only: the database path
  Invoke (Uia $procId 'DialogImport')
  $parked = Reached $procId
  $left = (Snapshots $procId).Count
  [Impqa]::PostMessage($main, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
  $closed = $false; for ($i = 0; $i -lt 40; $i++) { if (!(Get-Process -Id $procId -ErrorAction SilentlyContinue)) { $closed = $true; break }; Start-Sleep -Milliseconds 250 }
  $how = if ($closed) { 'closed by WM_CLOSE' } else { Stop-Process -Id $procId -Force; Start-Sleep 1; 'WM_CLOSE refused while the import runs; process ended (crash path)' }
  "     close during the snapshot: $how"
  $after = & $states
  Check ($parked -and $left -eq 1) 'seam close: snapshot taken and parked'
  Check (($before.Values -join ',') -eq ($after.Values -join ',')) "seam close: nothing written ($how)"
  Check ((Snapshots $procId).Count -eq 1) 'seam close: the ended process could not clean its copy'
  $p2, $main2 = Launch ''; $proc2 = $p2.Id
  $d = OpenImport $proc2 $main2 'Confidentialité'; Invoke (Uia $proc2 'DialogImport'); $r = Result $proc2
  if ($r) { Invoke (Uia $proc2 'DialogOk'); Gone $proc2 'ImportResult' | Out-Null }
  Check ((Snapshots $procId).Count -eq 0 -and (Snapshots $proc2).Count -eq 0) 'seam close: the next import removed the stale copy, and its own'
  Close $proc2 $main2

  # (c) Destination profile deleted while the read is parked. « Travail » imports; the window moves to « Personnel »
  #     and « Travail » is deleted through Gérer les profils, all BEFORE the read is released. Counted as a deletion
  #     test only if the deletion (Travail gone from profiles.txt) is observed before the release; otherwise
  #     reported « UNVERIFIED » (not a pass). The dead-owner guard itself is also proven without UI timing by the
  #     reviewer's controlled harness (import999-completion-guard).
  function LogText { if (Test-Path "$app\prototype.log") { ReadText "$app\prototype.log" } else { '' } }
  $p, $main = HoldLaunch 'ptravail01' 'read'; $procId = $p.Id
  $before = & $states; $logStart = (LogText).Length
  $d = OpenImport $procId $main 'Confidentialité'; Invoke (Uia $procId 'DialogImport')
  $parked = Reached $procId
  $switched = SwitchTo $procId 'Personnel'
  Invoke (Uia $procId 'ProfileButton'); Start-Sleep -Milliseconds 700
  $manage = Uia $procId 'ProfileMenuManage' '' 10; if ($manage) { Invoke $manage; Start-Sleep -Milliseconds 1300 }
  $item = $null; foreach ($top in $AE::RootElement.FindAll($TS::Children, (Cond $AE::ProcessIdProperty $procId))) { foreach ($e in $top.FindAll($TS::Descendants, (Cond $AE::AutomationIdProperty 'ProfileListItem'))) { if ($e.Current.Name -like 'Travail*') { $item = $e } } }
  if ($item) { Invoke $item; Start-Sleep -Milliseconds 1000 }
  $del = Uia $procId 'ProfileDelete' '' 10; if ($del) { Invoke $del; Start-Sleep -Milliseconds 1100 }
  $confirm = Uia $procId 'ProfileDeleteConfirm' '' 10
  if ($confirm) { Invoke $confirm }
  # Observed BEFORE releasing the worker: Travail gone from the profile registry. (The window's log lived in Travail's
  # own folder, removed with it, so the outcome is read from the dialog's UIA text instead.)
  $deletedFirst = $false
  for ($i = 0; $i -lt 60 -and !$deletedFirst; $i++) { Start-Sleep -Milliseconds 250; $deletedFirst = -not ((ReadText "$app\profiles.txt") -match 'ptravail01') }
  $stillParked = $false; try { $e = [System.Threading.EventWaitHandle]::OpenExisting("Local\browser-import-release-$procId"); $stillParked = -not $e.WaitOne(0); $e.Dispose() } catch {}
  "     delete confirmation reached: $([bool]$confirm); deleted before release: $deletedFirst; worker still parked: $stillParked"
  $released = Release $procId; Start-Sleep 3
  $after = & $states; $appLog = (LogText).Substring($logStart)
  Check ($parked -and $switched -and $released) 'seam delete: read parked, window moved to « Personnel », read released'
  if ($confirm -and $deletedFirst -and $stillParked) {
    Check $true 'seam delete: « Travail » deleted while the read was still parked'
    $dlg = Uia $procId 'ImportBrowserData' '' 10
    Check ($dlg -and $dlg.Current.HelpText -eq 'Le profil a changé : rien n''a été importé.') "seam delete: the read came back to a deleted destination and was dropped ($(if ($dlg) { $dlg.Current.HelpText }))"
    Check (!(Test-Path "$app\profiles\ptravail01")) 'seam delete: nothing recreated for the deleted « Travail »'
  } else {
    $line = 'seam delete: UNVERIFIED (the deletion could not be completed before the release; not counted as a deletion pass)'; $line; $script:log += $line
  }
  Check ($after.bmA -eq $before.bmA -and $after.hA -eq $before.hA -and $after.map -eq $before.map) 'seam delete: nothing written to « Personnel » (bookmarks, history, folder map)'
  Check ((Snapshots $procId).Count -eq 0) 'seam delete: no private snapshot left'
  Close $procId $main

  Check ((TreeHash $sources) -eq $sourcesBefore) 'synthetic sources byte-identical after every import (no sidecar created)'
  $tmpLeft = @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'browser-import-*' -Directory -ErrorAction SilentlyContinue)
  Check ($tmpLeft.Count -eq 0) 'no private snapshot left in the temporary folder'
} finally {
  Get-CimInstance Win32_Process -Filter "Name='browser.exe'" | Where-Object { $_.ExecutablePath -like "$run\*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
$visualLine = "visual: $($script:visualOk) capture(s) verified non-blank, $($script:visualBlank.Count) blank (real-app capture unavailable: $($script:visualBlank -join ', '))"
$visualLine; $script:log += $visualLine
$script:log | Set-Content -Encoding utf8 (Join-Path $Evidence 'e2e-results.txt')
if ($script:fails) { "FAILED: $($script:fails)"; exit 1 }
if ($RequireVisual -and $script:visualBlank.Count) { 'FAILED: blank captures with -RequireVisual'; exit 1 }
'browser import e2e: all functional checks passed'
Start-Sleep -Milliseconds 500
Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
