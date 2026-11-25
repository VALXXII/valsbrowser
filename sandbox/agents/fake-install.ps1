# FAKE « official installer » for Claude Code / Codex (« Ton agent », design/ton-agent-interface.md). Never the real one:
# used by tests/acp/agent_status_test.cpp and by the fake-profile trial launchers (BROWSER_AGENT_INSTALL_SCRIPT, only
# honoured by non-release builds under BROWSER_ANNOTATIONS_TEST). It copies the fake CLI (tests/acp/fake_cli.cpp) where
# the real installer would put the real one, inside the FAKE profile folders the browser was started with.
#   BROWSER_AGENT_INSTALL_AGENT   claude | codex (set by the browser)
#   BROWSER_FAKE_CLI              the fake-cli.exe to copy
#   BROWSER_FAKE_INSTALL_MODE     ok (default) | nocli | policy | inuse | network | fail | hang
#   BROWSER_FAKE_INSTALL_DELAY_MS a pause before it ends (to watch the « Installation… » animation in a trial)
#   FAKE_HOME / FAKE_LOCAL        override %USERPROFILE% / %LOCALAPPDATA% (the unit test sets them; the browser's are already fake)
$agent = $env:BROWSER_AGENT_INSTALL_AGENT
$mode = if ($env:BROWSER_FAKE_INSTALL_MODE) { $env:BROWSER_FAKE_INSTALL_MODE } else { 'ok' }
$homeDir = if ($env:FAKE_HOME) { $env:FAKE_HOME } else { $env:USERPROFILE }
$localDir = if ($env:FAKE_LOCAL) { $env:FAKE_LOCAL } else { $env:LOCALAPPDATA }
Write-Output "Fake installer: $agent ($mode)"
if ($env:BROWSER_FAKE_INSTALL_DELAY_MS) { Start-Sleep -Milliseconds ([int]$env:BROWSER_FAKE_INSTALL_DELAY_MS) }
switch ($mode) {
    'policy'  { Write-Output 'File install.ps1 cannot be loaded because running scripts is disabled on this system.'; exit 1 }
    'inuse'   { Write-Output "The process cannot access the file 'claude.exe' because it is being used by another process."; exit 1 }
    'network' { Write-Output 'Invoke-WebRequest : Unable to connect to the remote server'; exit 1 }
    'fail'    { Write-Output 'Something unexpected went wrong.'; exit 7 }
    'hang'    { Start-Sleep -Seconds 600; exit 0 }
}
if ($agent -eq 'claude') { $dir = Join-Path $homeDir '.local\bin'; $exe = 'claude.exe'; $version = '2.1.325 (Claude Code)' }
else { $dir = Join-Path $localDir 'Programs\OpenAI\Codex\bin'; $exe = 'codex.exe'; $version = 'codex-cli 0.166.0' }
if ($mode -ne 'nocli') {
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item -Force $env:BROWSER_FAKE_CLI (Join-Path $dir $exe)
    Set-Content -NoNewline -Path (Join-Path $dir "$exe.version") -Value $version
    Set-Content -NoNewline -Path (Join-Path $dir "$exe.login") -Value '1'   # installed, signed out
}
Write-Output 'Installed.'
exit 0
