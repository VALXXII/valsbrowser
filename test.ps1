$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& "$PSScriptRoot\build.ps1" -OutputDirectory 'build\smoke-v2'
Set-Content 'build\smoke-v2\prototype.log' ''
$process = Start-Process -FilePath "$PSScriptRoot\build\smoke-v2\browser.exe" -ArgumentList '--smoke-test' -WorkingDirectory "$PSScriptRoot\build\smoke-v2" -PassThru
if (!$process.WaitForExit(30000)) {
    throw 'Test interrompu : délai dépassé. Inspecter la fenêtre et prototype.log ; aucun processus arrêté automatiquement.'
}
$log = Get-Content 'build\smoke-v2\prototype.log' -Encoding UTF8 -Raw
Write-Output $log
if ($process.ExitCode -ne 0 -or $log.Contains('WEBVIEW2 ERROR') -or !$log.Contains('SMOKE PASS:')) {
    throw "Test échoué, code $($process.ExitCode). Ne pas retenter automatiquement en cas de problème WebView2."
}
Write-Output 'Test réussi (code 0).'
