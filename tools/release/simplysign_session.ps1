# Opens / closes a Certum SimplySign Desktop session on a GitHub-hosted Windows runner, so that signtool can use the
# cloud code-signing certificate (tools/release/sign.py, --sign-name). design/installer-signing-options.md.
#
#   ./tools/release/simplysign_session.ps1 -Open     # install SimplySign Desktop, log in, wait for the certificate
#   ./tools/release/simplysign_session.ps1 -Close    # end the session (always run, even after a failure)
#
# STATUS: NOT VALIDATED YET against a real SimplySign account (none existed when this was written). It follows the
# method used by public projects that sign with Certum from GitHub Actions; Certum publishes no unattended method.
# It is only run by the `sign` job of update-release.yml, itself OFF unless the repository variable RELEASE_SIGNING
# is "rehearsal" or "on". The first real runs must be rehearsals (nothing they produce is published).
#
# Inputs, environment only (never arguments, never printed):
#   CERTUM_SIMPLYSIGN_ID            the SimplySign account identifier (the e-mail address of the Certum account)
#   CERTUM_SIMPLYSIGN_TOTP_SECRET   the one-time-code secret of that account: either the otpauth:// text read from the
#                                   activation QR code, or its base32 `secret` alone
#   RELEASE_SIGNER_NAME             the subject name on the certificate (what Windows shows as the publisher)
# One login attempt only: a refused login is never retried here (repeated failures can lock the account), and two
# sessions must never overlap (the job has its own concurrency group).
param([switch]$Open, [switch]$Close)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The installer is pinned: version, SHA-256 and publisher, all three checked before it runs.
$Version = '9.4.4.92'
$InstallerUrl = "https://files.certum.eu/software/SimplySignDesktop/Windows/$Version/SimplySignDesktop-$Version-64-bit-en.msi"
$InstallerSha256 = '8ec420fc27798b86078b7bd02fe7152097e1b3005bab51820eaca8e57df84da3'
$InstallerPublisher = 'Asseco Data Systems S.A.'
$Exe = Join-Path $env:ProgramFiles 'Certum\SimplySign Desktop\SimplySignDesktop.exe'

function Get-Totp([string]$Secret, [long]$At = -1) {   # $At: a fixed Unix time, for the tests only
    # RFC 6238. The QR code's otpauth:// text carries the algorithm, digits and period; a bare secret uses SimplySign's
    # values as reported by the public projects (SHA-256, 6 digits, 30 s).
    $algorithm, $digits, $period, $seed = 'SHA256', 6, 30, $Secret.Trim()
    if ($seed -like 'otpauth://*') {
        $query = @{}
        foreach ($pair in ([uri]$seed).Query.TrimStart('?').Split('&')) { $k, $v = $pair.Split('=', 2); $query[$k.ToLowerInvariant()] = [uri]::UnescapeDataString($v) }
        if (-not $query.ContainsKey('secret')) { throw 'the otpauth text has no secret' }
        $seed = $query['secret']
        if ($query.ContainsKey('algorithm')) { $algorithm = $query['algorithm'].ToUpperInvariant().Replace('-', '') }
        if ($query.ContainsKey('digits')) { $digits = [int]$query['digits'] }
        if ($query.ContainsKey('period')) { $period = [int]$query['period'] }
    }
    $alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567'
    $bits = ''
    foreach ($c in $seed.ToUpperInvariant().Replace(' ', '').TrimEnd('=').ToCharArray()) {
        $i = $alphabet.IndexOf($c); if ($i -lt 0) { throw 'the one-time-code secret is not base32' }
        $bits += [Convert]::ToString($i, 2).PadLeft(5, '0')
    }
    $key = New-Object byte[] ([math]::Floor($bits.Length / 8))
    for ($i = 0; $i -lt $key.Length; $i++) { $key[$i] = [Convert]::ToByte($bits.Substring($i * 8, 8), 2) }
    # Never start with a code about to expire.
    while ($At -lt 0 -and ($period - ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() % $period)) -lt 8) { Start-Sleep -Milliseconds 500 }
    $now = if ($At -ge 0) { $At } else { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
    $counter = [BitConverter]::GetBytes([long][math]::Floor($now / $period)); [array]::Reverse($counter)
    $hmac = switch ($algorithm) { 'SHA1' { New-Object System.Security.Cryptography.HMACSHA1 (, $key) } 'SHA512' { New-Object System.Security.Cryptography.HMACSHA512 (, $key) } default { New-Object System.Security.Cryptography.HMACSHA256 (, $key) } }
    $hash = $hmac.ComputeHash($counter)
    $offset = $hash[$hash.Length - 1] -band 0x0F
    # [int] first: a shift on a [byte] stays a byte in PowerShell.
    $code = (([int]$hash[$offset] -band 0x7F) -shl 24) -bor ([int]$hash[$offset + 1] -shl 16) -bor ([int]$hash[$offset + 2] -shl 8) -bor [int]$hash[$offset + 3]
    return ($code % [math]::Pow(10, $digits)).ToString().PadLeft($digits, '0')
}

function Find-Certificate([string]$Name) {
    Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like "*$Name*" -and $_.HasPrivateKey -and ($_.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3') -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
}

if ($Close) {
    # Ends the session: the application is closed (only the one this job installed and started on this runner).
    Get-Process -Name 'SimplySignDesktop' -ErrorAction SilentlyContinue | ForEach-Object { try { $_.CloseMainWindow() | Out-Null; if (-not $_.WaitForExit(5000)) { $_.Kill() } } catch {} }
    Write-Host 'SimplySign session closed.'
    exit 0
}
if (-not $Open) { throw 'use -Open or -Close' }
if (-not $env:GITHUB_ACTIONS) { throw 'this script only runs on a GitHub Actions runner (it installs software and types credentials into a window)' }
foreach ($name in 'CERTUM_SIMPLYSIGN_ID', 'CERTUM_SIMPLYSIGN_TOTP_SECRET', 'RELEASE_SIGNER_NAME') {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "$name is not set (environment `"release`" secrets / repository variable)" }
}

# 1. SimplySign Desktop, pinned and checked before it runs.
if (-not (Test-Path $Exe)) {
    $msi = Join-Path $env:RUNNER_TEMP "SimplySignDesktop-$Version.msi"
    Invoke-WebRequest -Uri $InstallerUrl -OutFile $msi -UseBasicParsing
    if ((Get-FileHash $msi -Algorithm SHA256).Hash -ne $InstallerSha256) { throw 'SimplySign Desktop installer: unexpected SHA-256 (not the pinned file)' }
    $signature = Get-AuthenticodeSignature $msi
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike "*O=$InstallerPublisher*") { throw 'SimplySign Desktop installer: signature not valid or not from its publisher' }
    $install = Start-Process msiexec.exe -ArgumentList '/i', "`"$msi`"", '/qn', '/norestart' -Wait -PassThru
    if ($install.ExitCode -ne 0) { throw "SimplySign Desktop installation failed ($($install.ExitCode))" }
}
if (Find-Certificate $env:RELEASE_SIGNER_NAME) { Write-Host 'The certificate is already available.'; exit 0 }

# 2. One login: identifier + one-time code typed into the application's login window.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
$process = Start-Process $Exe -PassThru
$window = $null
for ($i = 0; $i -lt 120 -and -not $window; $i++) {
    Start-Sleep -Milliseconds 500
    $window = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children,
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)))
}
if (-not $window) { throw 'SimplySign Desktop showed no login window (it may start in the notification area only: adjust this step in a rehearsal)' }
$edits = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)))
if ($edits.Count -lt 2) {
    # What the window is made of, for the next rehearsal (control types and names only: nothing secret is on screen yet).
    $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
        ForEach-Object { Write-Host ('  control: ' + $_.Current.ControlType.ProgrammaticName + ' "' + $_.Current.Name + '"') }
    throw "the login window has $($edits.Count) text fields, 2 expected: nothing was typed"
}
$fill = { param($element, [string]$text)
    $pattern = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) { $pattern.SetValue($text) }
    else { $element.SetFocus(); [System.Windows.Forms.SendKeys]::SendWait(($text -replace '([+^%~(){}\[\]])', '{$1}')) } }
& $fill $edits[0] $env:CERTUM_SIMPLYSIGN_ID
& $fill $edits[1] (Get-Totp $env:CERTUM_SIMPLYSIGN_TOTP_SECRET)
$edits[1].SetFocus(); [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')

# 3. The session is open when the certificate (with its key in the cloud) shows up in the user's store.
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep -Seconds 1
    $certificate = Find-Certificate $env:RELEASE_SIGNER_NAME
    if ($certificate) { Write-Host "SimplySign session open: certificate valid until $($certificate.NotAfter.ToString('yyyy-MM-dd'))."; exit 0 }
}
throw 'the certificate did not appear within 90 s: login refused, wrong signer name, or the window changed. Not retried.'
