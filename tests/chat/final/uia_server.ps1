# UI Automation, read only, kept running for a whole real-window run (tests/chat/final): one command per line on stdin,
# one JSON line + "END" on stdout. Starting PowerShell and loading UIA once saves ~1.5 s per check.
#   probe <pid> [needle]      every visible top-level window of <pid>: class, elements of its subtree, ms, error; the chat
#                             window(s) (class BrowserChat*): their element count and whether a Name holds <needle>
#   list <pid>                the main window's (LisiereBrowser) elements in FindAll's order: "class|AutomationId|name"
#   bounds <pid> <id,id,...>  the screen bounds of elements by AutomationId
#   quit
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class QaTops {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static List<IntPtr> Of(uint pid) { var o = new List<IntPtr>(); EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) o.Add(h); return true; }, IntPtr.Zero); return o; }
}
"@
$all = [System.Windows.Automation.Condition]::TrueCondition
$sub = [System.Windows.Automation.TreeScope]::Subtree
function Esc([string]$s) { if ($s -eq $null) { return "" }; return ($s -replace '\\', '\\' -replace '"', '\"' -replace "[\r\n\t]", ' ') }
while ($true) {
    $line = [Console]::In.ReadLine()
    if ($line -eq $null -or $line -eq "quit") { break }
    $parts = $line -split " ", 3
    $out = @()
    try {
        if ($parts[0] -eq "probe") {
            $needle = if ($parts.Count -ge 3) { $parts[2] } else { "" }
            foreach ($h in [QaTops]::Of([uint32]$parts[1])) {
                $sw = [System.Diagnostics.Stopwatch]::StartNew(); $cls = ""; $n = -1; $err = ""; $found = $false
                try {
                    $e = [System.Windows.Automation.AutomationElement]::FromHandle($h); $cls = $e.Current.ClassName
                    $els = $e.FindAll($sub, $all); $n = $els.Count
                    if ($needle -ne "") { foreach ($x in $els) { $nm = ""; try { $nm = $x.Current.Name } catch {}; if ($nm -and $nm.Contains($needle)) { $found = $true; break } } }
                } catch { $err = $_.Exception.GetType().Name }
                $out += ('{{"cls":"{0}","n":{1},"ms":{2},"err":"{3}","found":{4}}}' -f (Esc $cls), $n, $sw.ElapsedMilliseconds, (Esc $err), $found.ToString().ToLower())
            }
            "[" + ($out -join ",") + "]"
        } elseif ($parts[0] -eq "list") {
            # F7: what a short walk of the main window holds (class | AutomationId | name), in FindAll's order
            foreach ($h in [QaTops]::Of([uint32]$parts[1])) {
                try { $e = [System.Windows.Automation.AutomationElement]::FromHandle($h); if ($e.Current.ClassName -ne "LisiereBrowser") { continue }
                      foreach ($x in $e.FindAll($sub, $all)) { $c = ""; $a = ""; $nm = ""; try { $c = $x.Current.ClassName; $a = $x.Current.AutomationId; $nm = $x.Current.Name } catch {}
                          if ($nm.Length -gt 40) { $nm = $nm.Substring(0, 40) }; $out += ('"{0}|{1}|{2}"' -f (Esc $c), (Esc $a), (Esc $nm)) } } catch { $out += ('"error {0}"' -f (Esc $_.Exception.GetType().Name)) }
            }
            "[" + ($out -join ",") + "]"
        } elseif ($parts[0] -eq "bounds") {
            $ids = $parts[2] -split ","
            $root = [System.Windows.Automation.AutomationElement]::RootElement
            $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$parts[1])
            foreach ($t in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
                try { foreach ($x in $t.FindAll($sub, $all)) { $aid = ""; try { $aid = $x.Current.AutomationId } catch {}
                    if ($ids -contains $aid) { $r = $x.Current.BoundingRectangle; $out += ('"{0}":[{1},{2},{3},{4}]' -f $aid, [int]$r.Left, [int]$r.Top, [int]$r.Right, [int]$r.Bottom) } } } catch {}
            }
            "{" + ($out -join ",") + "}"
        } else { '{"error":"unknown command"}' }
    } catch { '{"error":"' + (Esc $_.Exception.Message) + '"}' }
    "END"
    [Console]::Out.Flush()
}
