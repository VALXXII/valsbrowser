# Dépendances portables uniquement. Aucun installateur ni modification du PATH.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force tools,deps | Out-Null
$archive = Join-Path $PSScriptRoot 'tools\w64devkit-x64-2.11.0.7z.exe'
if (!(Test-Path 'tools\w64devkit\bin\g++.exe')) {
    Invoke-WebRequest 'https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe' -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '54C2BB9962E8A30C8270E34C5EFC3D5FEE579F53087FA7D435B95FBBA2AF7B2B') { throw 'Empreinte w64devkit inattendue.' }
    # L'archive auto-extractible officielle accepte les options 7-Zip.
    & $archive x "-o$PSScriptRoot\tools" -y
    if ($LASTEXITCODE -ne 0) { throw "Extraction échouée : $LASTEXITCODE" }
}
if (!(Test-Path 'deps\webview2\build\native\include\WebView2.h')) {
    Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg' -OutFile deps/webview2.zip
    if ((Get-FileHash deps/webview2.zip -Algorithm SHA256).Hash -ne '90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4') { throw 'Empreinte du SDK inattendue.' }
    Expand-Archive deps/webview2.zip deps/webview2 -Force
}
Write-Output 'Dépendances locales prêtes : w64devkit 2.11.0 ; WebView2 SDK 1.0.4314.35.'
