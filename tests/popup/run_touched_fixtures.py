"""Compile and run ONLY the existing native fixtures that compile src/main.cpp or a header this repair touched
(elements-integration.h, audio-integration.h, profile-workspace.h): a narrow compile-integration check, not the broad
suite. Same command lines as tests/update-integration/run_all.py (its fixture() helper) and test-elements-integration.ps1.

    python tests/popup/run_touched_fixtures.py
"""
import subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'update-integration'))
import run_all as R  # noqa: E402

ROOT = R.ROOT
R.OUT.mkdir(parents=True, exist_ok=True)
evidence = ROOT / 'build/browser-languages/captures/settings'; evidence.mkdir(parents=True, exist_ok=True)
R.fixture('language_settings_test', [*R.LOOSE, '-Werror', '-municode', *R.TVG, R.WV2, 'tests/language_settings_test.cpp', 'src/native-lottie.cpp', R.THORVG,
                                     *R.UI_LIBS, '-loleaut32', '-lshell32', '-lmsimg32', '-lbcrypt'], [ROOT, evidence])
bookmarks = ROOT / 'build/bookmarks-ui-qa'; bookmarks.mkdir(parents=True, exist_ok=True)
R.fixture('bookmarks_ui_test', [*R.LOOSE, '-municode', *R.TVG, R.WV2, 'tests/bookmarks/bookmarks_ui_test.cpp', 'src/native-lottie.cpp', R.THORVG,
                                *R.UI_LIBS, '-loleaut32', '-lshell32'], [ROOT, bookmarks])
R.fixture('address_selection_test', [*R.STRICT, 'tests/address_selection_test.cpp', '-lgdi32', '-luser32', '-ldwmapi', '-lcomctl32'])
R.fixture('accessibility_test', [*R.LOOSE, *R.TVG, R.WV2, 'tests/accessibility/accessibility_test.cpp', 'src/native-lottie.cpp', R.THORVG, *R.UI_LIBS, '-loleaut32'])
out = ROOT / 'build/elements-integration'; out.mkdir(parents=True, exist_ok=True)
R.run('compile elements_integration_test', [R.GXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unknown-pragmas', '-Wno-cast-function-type',
      '-Wno-misleading-indentation', '-Wno-missing-field-initializers', '-static', *R.TVG, R.WV2, 'tests/elements_integration_test.cpp',
      'src/native-lottie.cpp', R.THORVG, '-o', out / 'elements_integration_test.exe', '-lole32', '-luuid', '-lcomctl32', '-lshlwapi',
      '-lgdi32', '-ld2d1', '-ldwrite', '-lwindowscodecs', '-ldwmapi'])
R.run('run elements_integration_test', [out / 'elements_integration_test.exe'])
print('TOUCHED FIXTURES PASSED')
for label, seconds in R.results:
    print(f'  ok  {seconds:6.1f}s  {label}')
