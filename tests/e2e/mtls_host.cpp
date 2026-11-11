// mTLS E2E host: the browser with the TEST-ONLY fixture pin (src/e2e-fixture-pin.h). Built only by
// tests/e2e/mtls_e2e.ps1 into a fixture folder; never shipped. Release/dev builds do not define
// BROWSER_E2E_FIXTURE_HOOK and contain none of it.
#define BROWSER_E2E_FIXTURE_HOOK 1
#include "../../src/main.cpp"
