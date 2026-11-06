param([string]$Executable = 'build\v2\browser.exe')
$ErrorActionPreference = 'Stop'
$target = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot $Executable)).Path
$programs = [Environment]::GetFolderPath('Programs')
$shell = New-Object -ComObject WScript.Shell
$shortcutPath = Join-Path $programs 'browser.lnk'
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $target
$shortcut.WorkingDirectory = Split-Path $target
$shortcut.IconLocation = "$target,0"
$shortcut.Description = 'browser'
$shortcut.Save()
Write-Output "Raccourci utilisateur créé : $shortcutPath"
