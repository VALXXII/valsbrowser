param([string]$OutputDirectory = 'build\v2', [ValidateSet('dev','release')][string]$Channel = 'dev')
# Thin wrapper: the canonical, reproducible build (also used by CI and by machines
# whose PowerShell policy forbids scripts) is tools/release/build.py. It stamps the
# monotonic build identity (0.3.<git rev-list --count HEAD>), builds browser.exe and
# update-helper.exe, and only replaces payload files in the output directory: an
# existing profile/session beside an old build is never touched.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (!$python) { $python = 'C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe' }
& $python "$PSScriptRoot/tools/release/build.py" --out $OutputDirectory --channel $Channel
if ($LASTEXITCODE -ne 0) { throw "Compilation échouée : code $LASTEXITCODE" }
