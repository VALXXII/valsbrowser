// mTLS E2E host: the browser with the TEST-ONLY fixture pin (src/e2e-fixture-pin.h). Built only by
// tests/e2e/mtls_e2e.ps1 into a fixture folder; never shipped. Release/dev builds do not define
// BROWSER_E2E_FIXTURE_HOOK and contain none of it.
#define BROWSER_E2E_FIXTURE_HOOK 1
// Engine-control host: the vault (active in dev/release) is forced INACTIVE here, so the engine keeps its own password
// autosave; the vault E2E suites use this host as their « vault inactive » control.
#define BROWSER_VAULT_FORCE_INACTIVE 1
#include "../../src/main.cpp"
