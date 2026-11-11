#pragma once
// TEST-ONLY (mTLS E2E fixture). Compiled solely into the dedicated E2E host
// (tests/e2e/mtls_host.cpp defines BROWSER_E2E_FIXTURE_HOOK); never part of the release or dev
// browser, which have no way to enable it (no option, no environment variable, no command line).
//
// It lets the fixture's ONE local test server certificate through WebView2's supported API
// (ICoreWebView2_14 ServerCertificateErrorDetected → ALWAYS_ALLOW) and nothing else: https only,
// host exactly the numeric 127.0.0.1, the exact fixture port, engine status « authority not trusted »
// only (never expired, revoked, wrong name, client-cert errors), certificate currently valid, its
// SAN exactly IP 127.0.0.1, and the SHA-256 of the leaf DER equal to the pin written by the harness
// in its own manifest. Subject matching alone never counts. The page or the bridge never provide a pin.
// This pins a fixture PKI; it is not a validation of any production PKI.
#ifndef BROWSER_E2E_FIXTURE_HOOK
#error "e2e-fixture-pin.h is test-only: include it only from the E2E host"
#endif
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <cstdint>
#include <fstream>
#include <string>
#include "WebView2.h"

namespace E2EFixturePin {

struct Manifest { std::wstring host; int port = 0; std::string sha256; bool loaded = false; };
// Harness manifest: three lines « host=127.0.0.1 », « port=18445 », « sha256=<64 lowercase hex> ».
inline Manifest Load(const std::wstring& path) {
    Manifest m; std::ifstream in(path.c_str()); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "host") m.host.assign(v.begin(), v.end()); else if (k == "port") m.port = std::atoi(v.c_str()); else if (k == "sha256") m.sha256 = v;
    }
    m.loaded = m.host == L"127.0.0.1" && m.port > 1024 && m.port < 65536 && m.sha256.size() == 64;
    return m;
}
inline std::string Sha256Hex(const std::string& der) {
    unsigned char h[32]{}; DWORD n = 32;
    if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(der.data())), static_cast<ULONG>(der.size()), h, n))) return {};
    static const char* x = "0123456789abcdef"; std::string o; for (unsigned char c : h) { o += x[c >> 4]; o += x[c & 15]; } return o;
}
// SAN must be exactly one entry: IP 127.0.0.1.
inline bool SanIsLoopbackOnly(const std::string& der) {
    PCCERT_CONTEXT c = CertCreateCertificateContext(X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(der.data()), static_cast<DWORD>(der.size()));
    if (!c) return false; bool ok = false;
    if (PCERT_EXTENSION e = CertFindExtension(szOID_SUBJECT_ALT_NAME2, c->pCertInfo->cExtension, c->pCertInfo->rgExtension)) {
        CERT_ALT_NAME_INFO* info = nullptr; DWORD size = 0;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ALTERNATE_NAME, e->Value.pbData, e->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &size) && info) {
            ok = info->cAltEntry == 1 && info->rgAltEntry[0].dwAltNameChoice == CERT_ALT_NAME_IP_ADDRESS && info->rgAltEntry[0].IPAddress.cbData == 4 &&
                 info->rgAltEntry[0].IPAddress.pbData[0] == 127 && info->rgAltEntry[0].IPAddress.pbData[1] == 0 && info->rgAltEntry[0].IPAddress.pbData[2] == 0 && info->rgAltEntry[0].IPAddress.pbData[3] == 1;
            LocalFree(info);
        }
    }
    CertFreeCertificateContext(c); return ok;
}
inline bool DerFromPem(const std::wstring& pem, std::string& der) {
    std::string b64; bool in = false;
    for (size_t i = 0; i < pem.size();) {
        size_t e = pem.find(L'\n', i); if (e == std::wstring::npos) e = pem.size();
        std::wstring l = pem.substr(i, e - i); if (!l.empty() && l.back() == L'\r') l.pop_back(); i = e + 1;
        if (l.find(L"-----BEGIN CERTIFICATE-----") == 0) { if (in) return false; in = true; continue; }
        if (l.find(L"-----END CERTIFICATE-----") == 0) break;
        if (in) for (wchar_t c : l) b64 += static_cast<char>(c);
    }
    DWORD n = 0; if (b64.empty() || !CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return false;
    der.resize(n); return CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(der.data()), &n, nullptr, nullptr) != FALSE;
}
// Pure decision (tested). `now`, `validFrom`, `validTo`: seconds since the Unix epoch.
enum class Verdict { Allow, Refuse };
inline Verdict Decide(const Manifest& m, const std::wstring& requestUri, int status, const std::string& der, double validFrom, double validTo, double now, std::wstring* why = nullptr) {
    auto no = [&](const wchar_t* w) { if (why) *why = w; return Verdict::Refuse; };
    if (!m.loaded) return no(L"no harness manifest");
    if (requestUri.rfind(L"https://", 0) != 0) return no(L"not https");
    const std::wstring rest = requestUri.substr(8); const auto end = rest.find_first_of(L"/?#"); const std::wstring auth = rest.substr(0, end);
    if (auth != m.host + L":" + std::to_wstring(m.port)) return no(L"host or port differs");
    if (status != COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID) return no(L"status is not « authority not trusted »");
    if (!(now >= validFrom && now <= validTo)) return no(L"certificate not currently valid");
    if (der.empty() || Sha256Hex(der) != m.sha256) return no(L"fingerprint differs");
    if (!SanIsLoopbackOnly(der)) return no(L"SAN is not exactly IP 127.0.0.1");
    if (why) why->clear();
    return Verdict::Allow;
}

} // namespace E2EFixturePin
