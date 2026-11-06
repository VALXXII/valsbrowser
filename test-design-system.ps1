$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$out = 'build/design-system-preview'
New-Item -ItemType Directory -Force $out | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode tests/design-system/design_system_test.cpp -o "$out/design_system_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Design system fixture compilation failed.' }
& "$PSScriptRoot/$out/design_system_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Design system fixture failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static -municode tests/ui/hover_favicon_test.cpp -o "$out/hover_favicon_test.exe" -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid
if ($LASTEXITCODE -ne 0) { throw 'Hover/favicon regression fixture compilation failed.' }
& "$PSScriptRoot/$out/hover_favicon_test.exe" $PSScriptRoot "$PSScriptRoot/$out"
if ($LASTEXITCODE -ne 0) { throw 'Hover/favicon regression fixture failed.' }
