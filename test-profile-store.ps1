$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Interprocess profile store: three-way merge and adversarial runs with real processes (decision 2).
New-Item -ItemType Directory -Force build/profile-store | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O1 -Wall -Wextra -static -municode tests/profile_store_test.cpp -o build/profile-store/profile_store_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Profile store test compilation failed.' }
& ./build/profile-store/profile_store_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Profile store checks failed.' }
