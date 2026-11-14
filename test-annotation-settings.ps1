param([string]$DependencyRoot = $PSScriptRoot)
# Réglages › « Annotations et agents » (src/annotation-settings.h, src/settings-panel.h, src/settings.h):
#  1. tests/settings_test.cpp (new keys: defaults, round trip, tolerant read), -Werror like test.ps1;
#  2. tests/annotation_settings_test.cpp: pure model, the real drawer (rows, Tab order, hit testing, UIA tree,
#     switches, retention, folder, clear-all confirmation), Connecter / Déconnecter through src/agent-connect.*
#     on TEMPORARY folders only, PNG captures in build/annotations-v2-qa/settings;
#  3. the user's REAL Claude Desktop / Codex configurations fingerprinted before and after: any change fails.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/annotations-v2-qa/settings'
New-Item -ItemType Directory -Force $out | Out-Null
$gxx = "$DependencyRoot/tools/w64devkit/bin/g++.exe"

function Fingerprint {
    $claude = Join-Path $env:APPDATA 'Claude'
    $codex = if ($env:CODEX_HOME) { $env:CODEX_HOME } else { Join-Path $env:USERPROFILE '.codex' }
    $lines = @()
    foreach ($f in @((Join-Path $claude 'claude_desktop_config.json'), (Join-Path $codex 'config.toml'))) {
        if (Test-Path -LiteralPath $f) { $i = Get-Item -LiteralPath $f; $lines += "$f $($i.Length) $($i.LastWriteTimeUtc.Ticks) $((Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash)" } else { $lines += "${f} absent" }
    }
    foreach ($d in @($claude, $codex)) {
        if (Test-Path -LiteralPath $d) {
            $ours = Get-ChildItem -LiteralPath $d -Force | Where-Object { $_.Name -like '*.browser-annotations-*' -or ($_.Name -like '*.tmp-*' -and ($_.Name -like 'claude_desktop_config*' -or $_.Name -like 'config.toml*')) } | ForEach-Object Name
            $lines += "${d} ours: $($ours -join ',')"
            $ext = Join-Path $d 'Claude Extensions'
            if (Test-Path -LiteralPath $ext) { $lines += "${ext}: $((Get-ChildItem -LiteralPath $ext -Force | ForEach-Object Name) -join ',')" }
        } else { $lines += "${d} absent" }
    }
    return ($lines -join "`n")
}
$before = Fingerprint

& $gxx -std=c++17 -Wall -Wextra -Werror -static tests/settings_test.cpp -o "$out/settings_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'settings_test compilation failed.' }
& "./$out/settings_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'settings_test failed.' }
Write-Host 'settings_test: ok'

$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type', '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-DTVG_STATIC', "-I$DependencyRoot/deps/thorvg-1.1.3/inc", "-I$DependencyRoot/deps/webview2/build/native/include")
$libs = @('-lole32', '-luuid', '-lcomctl32', '-lshlwapi', '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi', '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt')
$ErrorActionPreference = 'Continue'
$log = & $gxx @flags -static -municode tests/annotation_settings_test.cpp src/agent-connect.cpp src/annotations.cpp src/native-lottie.cpp "$DependencyRoot/build/lottie-backend/src/libthorvg-1.a" -o "$out/annotation_settings_test.exe" @libs 2>&1 | Out-String
$built = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($built -ne 0 -or $log -match 'warning') { Write-Host $log; throw 'annotation_settings_test compilation failed (or warned).' }
$root = (Resolve-Path .).Path
& "./$out/annotation_settings_test.exe" $root (Resolve-Path $out).Path
$code = $LASTEXITCODE

$after = Fingerprint
if ($before -ne $after) { Write-Host "BEFORE:`n$before`nAFTER:`n$after"; throw 'The REAL Claude / Codex configuration changed during the test.' }
Write-Host "real configurations unchanged:`n$after"
if ($code -ne 0) { throw 'annotation_settings_test failed.' }

# 4. Real UI Automation client, out of process: the section's controls as a screen reader sees them.
$before = Fingerprint
$stdout = Join-Path $out 'uia-hold.txt'
$p = Start-Process -FilePath "$out/annotation_settings_test.exe" -ArgumentList "`"$root`"", "`"$((Resolve-Path $out).Path)`"", '--hold' -RedirectStandardOutput $stdout -PassThru
$deadline = (Get-Date).AddSeconds(30)
while (-not ((Test-Path $stdout) -and (Select-String -Path $stdout -Pattern 'READY' -Quiet)) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]
$cond = New-Object System.Windows.Automation.PropertyCondition($A::ClassNameProperty, 'LisiereSettingsDrawer')
# The drawer is an owned popup: UIA lists it under its owner, hence a Descendants search.
$drawer = $null
for ($i = 0; $i -lt 25 -and -not $drawer; $i++) { $drawer = $A::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond); if (-not $drawer) { Start-Sleep -Milliseconds 200 } }
if (-not $drawer) { $p.Kill(); throw 'UIA: settings drawer window not found.' }
$items = $drawer.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
$rows = foreach ($e in $items) {
    $c = $e.Current; $toggle = ''
    $tp = $null; if ($e.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { $toggle = $tp.Current.ToggleState }
    $ex = $null; $expand = ''; if ($e.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$ex)) { $expand = $ex.Current.ExpandCollapseState }
    [pscustomobject]@{ Type = $c.ControlType.ProgrammaticName.Replace('ControlType.', ''); Name = $c.Name; Help = $c.HelpText; Status = $c.ItemStatus; Enabled = $c.IsEnabled; Focusable = $c.IsKeyboardFocusable; Toggle = $toggle; Expand = $expand }
}
$rows | Format-Table -AutoSize | Out-String -Width 250 | Tee-Object -FilePath (Join-Path $out 'uia-tree.txt') | Write-Host
function Need($ok, $what) { if (-not $ok) { $p.Kill(); throw "UIA: $what" } ; Write-Host "ok   UIA client: $what" }
Need ($rows | Where-Object { $_.Type -eq 'Button' -and $_.Name -eq 'Connecter Claude Desktop' -and $_.Help -eq 'Non connecté' }) 'button « Connecter Claude Desktop », status as help text'
Need ($rows | Where-Object { $_.Type -eq 'Text' -and $_.Name -eq 'Codex : Non installé' }) 'text « Codex : Non installé »'
Need (-not ($rows | Where-Object { $_.Name -eq "Afficher le lanceur d'annotations" })) 'no launcher switch (V3: the launcher only exists in the mode)'
Need ($rows | Where-Object { $_.Name -eq 'Proposer de connecter un agent' -and "$($_.Toggle)" -eq 'On' }) 'prompt switch exposes Toggle = On'
Need ($rows | Where-Object { $_.Name -eq 'Conserver les terminées' -and $_.Status -eq '30 jours' -and "$($_.Expand)" -eq 'Collapsed' }) 'retention: value « 30 jours », ExpandCollapse'
Need ($rows | Where-Object { $_.Name -eq 'Effacer toutes les annotations…' -and $_.Enabled }) 'clear-all button'
Need ($rows | Where-Object { $_.Name -eq 'Fermer les réglages' }) 'close button'
$switch = $items | Where-Object { $_.Current.Name -eq 'Proposer de connecter un agent' } | Select-Object -First 1
$switch.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle()
Start-Sleep -Milliseconds 600
$state = $switch.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
Need ("$state" -eq 'Off') 'Toggle() through UIA turns the prompt switch off'
$p.WaitForExit(30000) | Out-Null
$tail = Get-Content $stdout | Select-Object -Last 1
Need ($tail -eq 'prompt=0 changes=1') "the drawer applied it ($tail)"
$after = Fingerprint
if ($before -ne $after) { throw 'The REAL Claude / Codex configuration changed during the UIA run.' }
Write-Host 'UIA client checks: ok; real configurations unchanged'
