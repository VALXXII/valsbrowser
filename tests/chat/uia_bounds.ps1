# UI Automation, read only: the screen bounds of elements of ONE process (the QA copy of the browser), by AutomationId,
# and how long the search took. Used by tests/chat/chat_qa_comment_switch.py to aim a real mouse click at the band's
# switch, and to see whether the search itself stalls once the chat bubble exists.
#   powershell -NoProfile -File tests/chat/uia_bounds.ps1 -ProcessId 1234 -Ids AnnotationLayoutComment,AnnotationAskAgent
param([int]$ProcessId, [string[]]$Ids)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$Ids = @($Ids | ForEach-Object { $_ -split "," } | Where-Object { $_ -ne "" })   # -File passes "A,B" as one string
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcessId)
$tops = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
foreach ($t in $tops) {
    $cls = ""; try { $cls = $t.Current.ClassName } catch {}
    $t0 = $sw.ElapsedMilliseconds; $n = 0; $err = ""
    try {
        $all = $t.FindAll([System.Windows.Automation.TreeScope]::Subtree, [System.Windows.Automation.Condition]::TrueCondition); $n = $all.Count
        foreach ($e in $all) {
            $aid = ""; try { $aid = $e.Current.AutomationId } catch {}
            if ($Ids -contains $aid) { $r = $e.Current.BoundingRectangle; "ID {0} {1} {2} {3} {4}" -f $aid, [int]$r.Left, [int]$r.Top, [int]$r.Right, [int]$r.Bottom }
        }
    } catch { $err = $_.Exception.GetType().Name }
    "TOP {0} elements={1} ms={2} {3}" -f $cls, $n, ($sw.ElapsedMilliseconds - $t0), $err
}
"TOTAL ms={0}" -f $sw.ElapsedMilliseconds
