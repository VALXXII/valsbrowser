#pragma once
// Interface language (design/browser-languages-plan.md). One application, two languages:
// French (default) and English. Every user-visible message lives in src/i18n-catalog.inc as
// I18N(id, L"français", L"English"); code asks for it by its semantic id:
//   TR(settings_title)                         -> const wchar_t* (static storage, never dangles)
//   TRF(downloads_in_folder, folder)           -> whole message with {0}, {1}… filled in
//   TRP(profiles_count, n)                     -> profiles_count_one / _other by the language's plural rule
// The language is chosen ONCE per process, at startup, before any window (src/ui-language.h reads
// the browser-wide preference); it never changes afterwards, so a window is never half translated.
// Tests set it explicitly with I18n::SetForProcess. Nothing here touches the C locale.
// Pure C++17: no Win32, safe from any thread (an atomic read).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace I18n {

enum class Locale : int { Fr = 0, En = 1 };
inline constexpr int LocaleCount = 2;

inline std::atomic<int>& LocaleSlot() { static std::atomic<int> slot{static_cast<int>(Locale::Fr)}; return slot; }
inline Locale Current() { return LocaleSlot().load(std::memory_order_relaxed) == static_cast<int>(Locale::En) ? Locale::En : Locale::Fr; }
// Startup (wWinMain, before any UI) and tests only.
inline void SetForProcess(Locale l) { LocaleSlot().store(static_cast<int>(l), std::memory_order_relaxed); }

enum class Id : std::uint16_t {
#define I18N(id, fr, en) id,
#include "i18n-catalog.inc"
#undef I18N
    Count_
};
inline constexpr std::size_t Count = static_cast<std::size_t>(Id::Count_);

struct Entry { const wchar_t* fr; const wchar_t* en; const char* name; };
inline const Entry& EntryOf(Id id) {
    static const Entry table[Count + 1] = {
#define I18N(id, fr, en) {fr, en, #id},
#include "i18n-catalog.inc"
#undef I18N
        {L"", L"", ""}
    };
    const std::size_t i = static_cast<std::size_t>(id);
    return table[i < Count ? i : Count];
}
inline const wchar_t* In(Locale l, Id id) { const auto& e = EntryOf(id); return l == Locale::En ? e.en : e.fr; }
inline const wchar_t* T(Id id) { return In(Current(), id); }
// A catalog reference stored in a table (never the text itself): resolved at each use, so a static
// table is never frozen in the language of whoever first touched it.
struct Text { Id id; operator const wchar_t*() const { return T(id); } };

// {0}…{9} replaced by args; any other brace is kept as is. A missing argument leaves the
// placeholder visible (a catalog/call mismatch is caught by tests, never hidden).
inline std::wstring FormatText(const wchar_t* pattern, std::initializer_list<std::wstring> args) {
    std::wstring out; const std::wstring* a = args.begin(); const std::size_t n = args.size();
    for (const wchar_t* p = pattern; *p; ++p) {
        if (p[0] == L'{' && p[1] >= L'0' && p[1] <= L'9' && p[2] == L'}') {
            const std::size_t k = static_cast<std::size_t>(p[1] - L'0');
            if (k < n) { out += a[k]; p += 2; continue; }
        }
        out += *p;
    }
    return out;
}
inline std::wstring Format(Id id, std::initializer_list<std::wstring> args) { return FormatText(T(id), args); }
// French: 0 and 1 take the singular; English: only 1. {0} is the count (digits as is).
inline bool PluralOne(Locale l, long long n) { return l == Locale::En ? n == 1 : (n == 0 || n == 1); }
inline std::wstring Plural(Id one, Id other, long long n, std::initializer_list<std::wstring> rest = {}) {
    std::wstring args[10]; std::size_t k = 0; args[k++] = std::to_wstring(n);
    for (const auto& r : rest) if (k < 10) args[k++] = r;
    const wchar_t* pattern = T(PluralOne(Current(), n) ? one : other);
    switch (k) {
    case 1: return FormatText(pattern, {args[0]});
    case 2: return FormatText(pattern, {args[0], args[1]});
    case 3: return FormatText(pattern, {args[0], args[1], args[2]});
    default: return FormatText(pattern, {args[0], args[1], args[2], args[3]});
    }
}

// UTF-16 -> UTF-8 (HTML pages, JSON, narrow APIs). Unpaired surrogates become U+FFFD.
inline std::string Utf8(const std::wstring& w) {
    std::string out; out.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::uint32_t c = static_cast<std::uint16_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size() && static_cast<std::uint16_t>(w[i + 1]) >= 0xDC00 && static_cast<std::uint16_t>(w[i + 1]) <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<std::uint16_t>(w[++i]) - 0xDC00);
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}
inline std::string U8(Id id) { return Utf8(T(id)); }

// BCP 47 tag for the engine (EngineOptions::put_Language) and the html lang of built-in pages.
inline const wchar_t* Tag() { return Current() == Locale::En ? L"en-US" : L"fr-FR"; }
inline const char* HtmlLang() { return Current() == Locale::En ? "en" : "fr"; }

// Numbers as each language writes them. French: decimal comma and a space before % (the
// existing French UI's « 100 % »); English: decimal point, no space. Digits are never grouped.
inline std::wstring Decimal(double v, int decimals) {
    wchar_t text[64]; std::swprintf(text, 64, L"%.*f", decimals < 0 ? 0 : decimals, v);
    std::wstring s = text;
    if (Current() == Locale::Fr) for (auto& c : s) if (c == L'.') c = L',';
    return s;
}
inline std::wstring Percent(long long v) { return std::to_wstring(v) + (Current() == Locale::En ? L"%" : L" %"); }

} // namespace I18n

#define TR(id) (::I18n::T(::I18n::Id::id))
#define TRF(id, ...) (::I18n::Format(::I18n::Id::id, {__VA_ARGS__}))
#define TRP(id, n, ...) (::I18n::Plural(::I18n::Id::id##_one, ::I18n::Id::id##_other, static_cast<long long>(n), {__VA_ARGS__}))
