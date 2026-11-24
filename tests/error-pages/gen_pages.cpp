// tests/error-pages: writes the #121 error pages exactly as the browser builds them (src/error-pages.h, the real
// bundled fonts as data: URIs, like main's PageFonts) with the texts of the Paper boards, one UTF-8 file per case:
//
//   gen_pages.exe <repository root> <output directory>
//
// run.py (no window) and window_qa.py (real window, at night) compare what these pages render with the boards.
#include "../../src/error-pages.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace NativeChrome;
using ErrorPages::Kind;

namespace {
std::wstring root, out;
std::wstring FontUri(const wchar_t* file) {
    std::ifstream in((root + L"\\assets\\fonts\\" + file).c_str(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) { std::fprintf(stderr, "missing font %ls\n", file); std::exit(2); }
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const std::wstring name = file;
    std::wstring r = std::wstring(L"data:font/") + (name.size() > 6 && name.compare(name.size() - 6, 6, L".woff2") == 0 ? L"woff2" : L"ttf") + L";base64,";
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) | (i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0) | (i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0);
        r += static_cast<wchar_t>(digits[(n >> 18) & 63]); r += static_cast<wchar_t>(digits[(n >> 12) & 63]);
        r += i + 1 < bytes.size() ? static_cast<wchar_t>(digits[(n >> 6) & 63]) : L'='; r += i + 2 < bytes.size() ? static_cast<wchar_t>(digits[n & 63]) : L'=';
    }
    return r;
}
void Write(const wchar_t* name, const std::wstring& html) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), bytes.data(), n, nullptr, nullptr);
    std::ofstream((out + L"\\" + name + L".html").c_str(), std::ios::binary) << bytes;
    std::printf("%ls %d\n", name, n);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: gen_pages.exe <root> <out>\n"); return 2; }
    root = argv[1]; out = argv[2];
    const std::wstring token = L"{00000000-0000-4000-8000-000000000121}";
    // The files the browser embeds (main's PageFonts).
    ErrorPages::Extras x; x.fontRegular = FontUri(L"InterDisplay-Regular.woff2"); x.fontMedium = FontUri(L"InterDisplay-Medium.woff2");
    x.fontSemiBold = FontUri(L"InterDisplay-SemiBold.woff2"); x.fontMono = FontUri(L"JetBrainsMono-Codes.ttf");
    x.reducedMotion = true;   // no entrance animation: measured and captured at rest
    // Boards 1LXF-0, 1LZP-0, 1M1W-0 (DNS, refusée, délai, reprise) and 1M3Q-0 (5 certificates), their own hosts.
    Write(L"offline", ErrorPages::Html(Kind::Offline, L"https://www.lemonde.fr/planete/", token, false, x));
    Write(L"insecure", ErrorPages::InsecureHtml(L"bibliotheque-municipale.fr", token, x));
    Write(L"dns", ErrorPages::Html(Kind::NameNotResolved, L"https://lemond.fr/", token, false, x));
    ErrorPages::Extras refused = x; refused.code = L"ERR_CONNECTION_REFUSED"; refused.address = ErrorPages::AddressOf(L"http://localhost:3000/"); refused.tries = ErrorPages::TriesText(2, L"14:32");
    Write(L"refused", ErrorPages::Html(Kind::Refused, L"http://localhost:3000/", token, false, refused));
    Write(L"timeout", ErrorPages::Html(Kind::TimedOut, L"https://impots.gouv.fr/", token, false, x));
    ErrorPages::Extras back = x; back.restored = true;
    Write(L"restored", ErrorPages::Html(Kind::Offline, L"https://www.lemonde.fr/planete/", token, true, back));
    struct Cert { const wchar_t* name; int status; const wchar_t* host; const wchar_t* issuer; };
    for (const Cert& c : {Cert{L"cert-invalid", COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, L"https://intranet.example.com/", L"Example Internal CA"},
                          Cert{L"cert-expired", COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED, L"https://archives.example.org/", L"Example Issuing CA"},
                          Cert{L"cert-wrong-name", COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT, L"https://boutique.example.com/", L"Example Issuing CA"},
                          Cert{L"cert-revoked", COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED, L"https://paiement.example.com/", L"Example Issuing CA"},
                          Cert{L"cert-client", COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS, L"https://intranet.example.com/", L""}}) {
        ErrorPages::Extras e = x; e.certStatus = c.status; e.issuer = c.issuer;
        Write(c.name, ErrorPages::Html(Kind::Certificate, c.host, token, false, e));
    }
    return 0;
}
