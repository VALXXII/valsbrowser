#pragma once
// The command line of browser.exe split into its switches and the ONE link or file it was asked to open
// (pure; unit-tested in tests/default_browser_test.cpp).
//   browser.exe https://example.com            -> target « https://example.com »
//   browser.exe --profile travail C:\x\a.html  -> switches « --profile travail », target « C:\x\a.html »
//   browser.exe --single-argument "%1"        -> what Windows runs for a link or a file (src/default-browser.h):
//                                                 everything after --single-argument is the target, never a switch.
// The host checks its switches in `switches` only (wcsstr on the raw command line would read « --private » inside
// a link such as https://site/?--private as a switch).
#include <string>
#include <vector>

namespace LaunchArgs {

inline constexpr const wchar_t* SingleArgument = L"--single-argument";
// Switches followed by a value (`--profile <id>`): the value is never the target.
inline bool TakesValue(const std::wstring& s) {
    for (const wchar_t* v : {L"--profile", L"--restore-window", L"--delete-profile", L"--import-sources", L"--import-hold"})
        if (s == v) return true;
    return false;
}
// One command-line token as CommandLineToArgvW reads it back (backslashes doubled only before a quote).
inline std::wstring Quoted(const std::wstring& token) {
    if (!token.empty() && token.find_first_of(L" \t\"") == std::wstring::npos) return token;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : token) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

struct Parsed {
    std::wstring switches;   // the switches, as a command line (same spacing rules the host already reads)
    std::wstring target;     // the link or path to open ("" when none)
    bool single = false;     // came after --single-argument (from Windows: a link or a file)
};

// `tokens`: the arguments after the program name (CommandLineToArgvW).
inline Parsed Parse(const std::vector<std::wstring>& tokens) {
    Parsed p; std::vector<std::wstring> kept;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::wstring& t = tokens[i];
        if (t == SingleArgument) {
            std::wstring rest;
            for (size_t j = i + 1; j < tokens.size(); ++j) rest += (rest.empty() ? L"" : L" ") + tokens[j];
            p.target = rest; p.single = true; break;
        }
        if (t.size() > 2 && t[0] == L'-' && t[1] == L'-') {
            kept.push_back(t);
            if (TakesValue(t) && i + 1 < tokens.size() && !(tokens[i + 1].size() > 1 && tokens[i + 1][0] == L'-')) kept.push_back(tokens[++i]);
            continue;
        }
        if (p.target.empty() && !t.empty()) p.target = t;   // the first plain argument; any other is ignored
    }
    for (const auto& k : kept) p.switches += (p.switches.empty() ? L"" : L" ") + Quoted(k);
    return p;
}

}  // namespace LaunchArgs
