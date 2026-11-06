"""Native-only fixture runner; never enters browser main or loads WebView2."""
from pathlib import Path
import subprocess, sys
ROOT=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(ROOT/'build-lottie.py')],check=True)
out=ROOT/'build/lottie-polish'
evidence=out/'evidence'
evidence.mkdir(parents=True,exist_ok=True)
cc=str(ROOT/'tools/w64devkit/bin/g++.exe')
fixtures=[('tests/lottie/native_lottie_test.cpp','native-lottie-test'),
          ('tests/lottie/clipboard_gate_test.cpp','clipboard-gate-test'),
          ('tests/design-system/design_system_test.cpp','design-system-test'),
          ('tests/ui/hover_favicon_test.cpp','hover-favicon-test'),
          ('tests/ui/ui_test.cpp','ui-test')]
for source,name in fixtures:
    flags=['-std=c++17','-O2','-static','-municode','-DTVG_STATIC',
           '-Ideps/thorvg-1.1.3/inc','-Ideps/webview2/build/native/include']
    if name!='clipboard-gate-test': flags+=['-Wall','-Wextra','-Werror','-Wno-missing-field-initializers']
    subprocess.run([cc,*flags,source,'src/native-lottie.cpp','build/lottie-backend/src/libthorvg-1.a',
                    '-o',str(out/(name+'.exe')),'-lole32','-luuid','-lcomctl32','-lshlwapi',
                    '-lgdi32','-ld2d1','-ldwrite','-lwindowscodecs','-ldwmapi'],cwd=ROOT,check=True)
    subprocess.run([str(out/(name+'.exe')),str(ROOT),str(evidence)],cwd=ROOT,check=True)
