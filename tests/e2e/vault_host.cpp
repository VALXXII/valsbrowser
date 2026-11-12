// Vault E2E host: the browser with the vault ACTIVE and the fixture pin, for a synthetic profile and
// fictitious accounts only. Built by tests/e2e/vault_e2e.ps1 into a fixture folder; never shipped.
// Release/dev builds define neither macro, so the vault stays inactive there.
#define BROWSER_E2E_FIXTURE_HOOK 1
#define BROWSER_E2E_VAULT_HOST 1
#include "../../src/main.cpp"
