$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Password vault core (inactive until the independent security review): fictitious accounts only.
New-Item -ItemType Directory -Force build/password-vault | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O1 -Wall -Wextra -static tests/password_vault_test.cpp -o build/password-vault/password_vault_test.exe -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw 'Password vault test compilation failed.' }
& ./build/password-vault/password_vault_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Password vault checks failed.' }
