// Vault PRODUCT-PATH host (review R-5): the browser's production code with the vault ACTIVE and nothing
// else changed: every foreground condition (B04/B03 arming, key arming, fill choice) stays in force, unlike
// tests/e2e/vault_host.cpp. The fixture pin is kept only so the local HTTPS fixture is accepted.
// Built by tests/e2e/vault_product_e2e.ps1 into a fixture folder, synthetic profile and fictitious accounts
// only; never shipped. Release/dev builds define neither macro, so the vault stays inactive there.
#define BROWSER_E2E_FIXTURE_HOOK 1
#define BROWSER_E2E_VAULT_ACTIVE 1
#include "../../src/main.cpp"
