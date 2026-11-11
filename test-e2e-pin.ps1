$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Test-only mTLS fixture pin: negative matrix on real throwaway certificates (files only, nothing installed).
$d = 'build/e2e-pin'; Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $d | Out-Null
$o = 'C:\Program Files\Git\usr\bin\openssl.exe'
$ErrorActionPreference = 'Continue'
foreach ($c in @(@('good', 'IP:127.0.0.1'), @('other', 'IP:127.0.0.1'), @('dns', 'DNS:localhost'), @('two', 'IP:127.0.0.1,DNS:localhost'))) {
  & $o req -x509 -newkey rsa:2048 -nodes -keyout "$d/$($c[0]).key" -out "$d/$($c[0]).crt" -days 1 -subj '/CN=127.0.0.1' -addext "subjectAltName=$($c[1])" 2>&1 | Out-Null
  & $o x509 -in "$d/$($c[0]).crt" -outform DER -out "$d/$($c[0]).der" 2>&1 | Out-Null
}
$ErrorActionPreference = 'Stop'
& tools/w64devkit/bin/g++.exe -std=c++17 -O1 -Wall -Wextra -static -municode '-Ideps/webview2/build/native/include' tests/e2e/e2e_pin_test.cpp -o "$d/e2e_pin_test.exe" -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw 'E2E pin test compilation failed.' }
& "$d/e2e_pin_test.exe" (Resolve-Path $d).Path
$code = $LASTEXITCODE
Remove-Item "$d/*.key" -Force   # throwaway keys of this run
if ($code -ne 0) { throw 'E2E pin checks failed.' }
