# Agent guard for the PowerShell test entry points that start browser.exe (test.ps1, test-split-view.ps1,
# test-audio.ps1, test-e2e-pin.ps1). Dot-source it first:   . "$PSScriptRoot\tests\agent-guard\agent-guard.ps1"
# Why, and the other layers: tests/agent-guard/agent_guard.py (incident of 25 Nov 2026: a test browser started the
# computer's own, signed-in Claude Code). The browsers of these scripts need no agent at all.
#
# From the dot-sourcing on, this PowerShell process and everything it starts has:
#   - no API key nor agent credential in its environment (ANTHROPIC*, OPENAI*, CLAUDE*, CODEX*, *_API_KEY, tokens...),
#     the same list as agent_guard.py (SECRET_PREFIXES / SECRET_SUFFIXES);
#   - no BROWSER_CHAT_FAKE_AGENT, BROWSER_CHAT_REAL_AGENT nor BROWSER_TEST_REAL_AGENT.
# Use-AgentGuardBrowser { <the browser's Start-Process> } runs the launch with BROWSER_ANNOTATIONS_TEST=1 and
# CLAUDE_CONFIG_DIR / CODEX_HOME on an empty folder under build\agent-guard: the browser, in its test mode with no fake
# agent named, refuses every agent (src/agent-runtime.cpp: no CLI looked for, run or downloaded; the log says
# "CHAT agent runtime (test): every agent refused" if the chat ever asks), and agent-connect refuses any write in the
# real Claude / Codex folders. The test switch is set only around the launch: other programs started by the same script
# (C++ test hosts of tests/annotations) keep their own branch.

$AgentGuardPrefixes = @('ANTHROPIC', 'OPENAI', 'CLAUDE', 'CODEX', 'AZURE_OPENAI', 'GEMINI', 'GOOGLE_GENAI', 'GOOGLE_API_KEY',
    'GOOGLE_APPLICATION_CREDENTIALS', 'VERTEX', 'MISTRAL', 'OPENROUTER', 'GROQ', 'XAI_', 'DEEPSEEK', 'PERPLEXITY', 'COHERE',
    'TOGETHER_', 'FIREWORKS', 'AWS_BEARER_TOKEN', 'BEDROCK', 'GH_TOKEN', 'GITHUB_TOKEN', 'GH_ENTERPRISE_TOKEN', 'NPM_TOKEN',
    'NODE_AUTH_TOKEN', 'HF_TOKEN', 'HUGGING')
$AgentGuardSuffixes = @('_API_KEY', '_APIKEY', '_AUTH_TOKEN', '_ACCESS_TOKEN', '_OAUTH_TOKEN', '_SESSION_TOKEN', '_REFRESH_TOKEN',
    '_SECRET', '_SECRET_KEY', '_CLIENT_SECRET', '_CREDENTIALS', '_PASSWORD')

function Test-AgentGuardSecret([string]$Name) {
    $n = $Name.ToUpperInvariant()
    foreach ($p in $AgentGuardPrefixes) { if ($n.StartsWith($p)) { return $true } }
    foreach ($s in $AgentGuardSuffixes) { if ($n.EndsWith($s)) { return $true } }
    return $false
}

$AgentGuardStripped = @()
foreach ($v in @(Get-ChildItem Env:)) {
    if ((Test-AgentGuardSecret $v.Name) -or $v.Name -in @('BROWSER_CHAT_FAKE_AGENT', 'BROWSER_CHAT_REAL_AGENT', 'BROWSER_TEST_REAL_AGENT')) {
        [Environment]::SetEnvironmentVariable($v.Name, $null, 'Process')
        $AgentGuardStripped += $v.Name
    }
}
$AgentGuardHome = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\build\agent-guard\home'))
"     agent guard: no agent for the test browser (stripped: $(if ($AgentGuardStripped) { $AgentGuardStripped -join ', ' } else { 'nothing' }))"

function Use-AgentGuardBrowser([scriptblock]$Launch) {
    $names = @('BROWSER_ANNOTATIONS_TEST', 'CLAUDE_CONFIG_DIR', 'CODEX_HOME')
    $saved = @{}
    foreach ($n in $names) { $saved[$n] = [Environment]::GetEnvironmentVariable($n, 'Process') }
    $claude = Join-Path $AgentGuardHome '.claude'
    $codex = Join-Path $AgentGuardHome '.codex'
    New-Item -ItemType Directory -Force -Path $claude, $codex | Out-Null
    try {
        [Environment]::SetEnvironmentVariable('BROWSER_ANNOTATIONS_TEST', '1', 'Process')
        [Environment]::SetEnvironmentVariable('CLAUDE_CONFIG_DIR', $claude, 'Process')
        [Environment]::SetEnvironmentVariable('CODEX_HOME', $codex, 'Process')
        & $Launch
    } finally {
        foreach ($n in $names) { [Environment]::SetEnvironmentVariable($n, $saved[$n], 'Process') }
    }
}
