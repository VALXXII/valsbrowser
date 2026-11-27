$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build/tab-drag | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Ideps/webview2/build/native/include tests/ui/tab_drag_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/tab-drag/tab_drag_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi
if ($LASTEXITCODE -ne 0) { throw 'Tab drag fixture compilation failed.' }
& ./build/tab-drag/tab_drag_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab drag regression failed.' }
# Atelier B: a tab dropped on the favorites. Pure rules, then the real window procedure on a window that is never shown
# (no input sent anywhere, cursor / capture / foreground shimmed) with offscreen renders of the 3 layouts in build/tab-drag/favorites.
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static tests/tab_favorite_drop_test.cpp -o build/tab-drag/tab_favorite_drop_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite drop rules compilation failed.' }
& ./build/tab-drag/tab_favorite_drop_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite drop rules failed.' }
& tools/w64devkit/bin/g++.exe -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -static tests/tab_favorite_lastrow_test.cpp -o build/tab-drag/tab_favorite_lastrow_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite last-row compilation failed.' }
& ./build/tab-drag/tab_favorite_lastrow_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite last-row check failed (the grey place must be the grid with one more tile).' }
New-Item -ItemType Directory -Force build/tab-drag/favorites | Out-Null
& tools/w64devkit/bin/g++.exe -std=c++17 -O1 -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -Wno-misleading-indentation -Wno-missing-field-initializers -static -municode -DTVG_STATIC '-Ideps/thorvg-1.1.3/inc' -Ideps/webview2/build/native/include tests/ui/tab_favorite_drag_test.cpp src/native-lottie.cpp build/lottie-backend/src/libthorvg-1.a -o build/tab-drag/tab_favorite_drag_test.exe -lole32 -luuid -lcomctl32 -lshlwapi -lgdi32 -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -loleaut32 -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite drag fixture compilation failed.' }
& ./build/tab-drag/tab_favorite_drag_test.exe $PSScriptRoot "$PSScriptRoot\build\tab-drag\favorites"
if ($LASTEXITCODE -ne 0) { throw 'Tab favorite drag regression failed.' }
