# UI Automation helper for the annotation QA (V2): acts ONLY on the windows of one process (the QA copy of
# the preview build). Usage:
#   powershell -NoProfile -File tests/annotations/uia.ps1 -ProcessId 1234 -List                 # AutomationIds + names
#   powershell -NoProfile -File tests/annotations/uia.ps1 -ProcessId 1234 -Id AnnotationConnectLater -Invoke
#   powershell -NoProfile -File tests/annotations/uia.ps1 -ProcessId 1234 -Name "Supprimer" -Invoke
param([int]$ProcessId, [string]$Id = "", [string]$Name = "", [switch]$Invoke, [switch]$List, [switch]$Expand)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcessId)
$tops = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
$found = $null
foreach ($t in $tops) {
    $all = $t.FindAll([System.Windows.Automation.TreeScope]::Subtree, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($e in $all) {
        $aid = $e.Current.AutomationId; $nm = $e.Current.Name
        if ($List -and ($aid -like "Annotat*" -or ($nm -ne "" -and $e.Current.ControlType.ProgrammaticName -ne "ControlType.Pane" -and $e.Current.ControlType.ProgrammaticName -ne "ControlType.Document"))) { "{0} | {1} | {2} | enabled={3} | {4} | {5}" -f $aid, $nm, $e.Current.ControlType.ProgrammaticName, $e.Current.IsEnabled, $e.Current.HelpText, $e.Current.ItemStatus }
        if (-not $found -and (($Id -ne "" -and $aid -eq $Id) -or ($Name -ne "" -and $nm -eq $Name))) { $found = $e }
    }
}
if ($List) { exit 0 }
if (-not $found) { Write-Output "NOTFOUND"; exit 2 }
if ($Invoke) {
    $p = $null
    if ($found.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); Write-Output ("INVOKED " + $found.Current.Name); exit 0 }
    if ($found.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$p)) { $p.Expand(); Write-Output ("EXPANDED " + $found.Current.Name); exit 0 }
    Write-Output "NOPATTERN"; exit 3
}
Write-Output ("FOUND {0} | {1}" -f $found.Current.AutomationId, $found.Current.Name)
