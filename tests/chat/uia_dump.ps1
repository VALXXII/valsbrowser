# UI Automation, read only: every top-level window of the process (by window handle, so a window whose UIA root says
# another process id is listed too) and the first levels of its tree, with timings. Diagnosis of the chat bubble's
# provider (tests/chat/chat_qa_comment_switch.py).
param([int]$ProcessId, [int]$Depth = 3)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Tops {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static List<IntPtr> Of(uint pid) { var o = new List<IntPtr>(); EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) o.Add(h); return true; }, IntPtr.Zero); return o; }
}
"@
$walker = [System.Windows.Automation.TreeWalker]::RawViewWalker
function Walk($e, $d) {
    if ($d -gt $Depth) { return }
    $c = $walker.GetFirstChild($e); $i = 0
    while ($c -ne $null -and $i -lt 40) {
        $x = $c.Current; ("  " * $d) + ("{0} | {1} | {2} | pid {3}" -f $x.ClassName, $x.AutomationId, $x.Name, $x.ProcessId)
        Walk $c ($d + 1)
        $c = $walker.GetNextSibling($c); $i++
    }
}
foreach ($h in [Tops]::Of([uint32]$ProcessId)) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try { $e = [System.Windows.Automation.AutomationElement]::FromHandle($h); $x = $e.Current
          "TOP {0} | {1} | pid {2}" -f $x.ClassName, $x.Name, $x.ProcessId; Walk $e 1
          try { $n = $e.FindAll([System.Windows.Automation.TreeScope]::Subtree, [System.Windows.Automation.Condition]::TrueCondition).Count; "  FindAll subtree: $n" } catch { "  FindAll subtree error: " + $_.Exception.InnerException.GetType().FullName + " " + $_.Exception.Message }
          $b = $null; $c = $walker.GetFirstChild($e); while ($c -ne $null) { if ($c.Current.ClassName -like "BrowserChat*") { $b = $c }; $c = $walker.GetNextSibling($c) }
          if ($x.ClassName -like "BrowserChat*") { $b = $e }
          if ($b) { try { $k = $walker.GetFirstChild($b); "  chat element first child: " + $(if ($k) { $k.Current.ClassName + " | " + $k.Current.Name } else { "none" }) } catch { "  chat element first child error: " + $_.Exception.Message }
                    try { $n2 = $b.FindAll([System.Windows.Automation.TreeScope]::Subtree, [System.Windows.Automation.Condition]::TrueCondition).Count; "  chat element FindAll: $n2" } catch { "  chat element FindAll error: " + $_.Exception.Message } } } catch { "TOP {0} error {1}" -f $h, $_.Exception.Message }
    "  ({0} ms)" -f $sw.ElapsedMilliseconds
}
