#pragma once
#include "i18n.h"
// « Connecter un agent » (V2) — Paper « MCP - Design UI UX »: 1.1 10OW-0 (launch), 1.2 ZCC-0 (en
// cours), 1.3 ZDH-0 (connecté + Terminé), 1.4 ZEI-0 (échec et reprise), 1.5 ZFJ-0 (réglage
// manuel déplié), 1.6 ZGK-0 (disque amovible), 1.7 105Y-0 (pastille « local » dépliée); motion
// « Spéc · Connexion des agents » ZJQ-0 and « Spéc · Lancement » 10X1-0.
//   Modal 440, canvas #171717, border 1 #FFFFFF14, radius 12.
//   Header padding 20/20/14, gap 12 (one header for every state, 1.1 included since the
//     2026-11-14 revision: no description, no « Plus tard » button): tree glyph 20 at 50 %, 6,
//     title 18/24 600(-> bundled Medium) -0.015em white; close button 28 (glyph 16 at 70 %).
//     At launch the close button and Échap mean « Plus tard » (ConnectEnd::Later).
//   Apps padding 0/12/12, gap 8: one full-width button per app, h 32 radius 6, 13/16 medium:
//     idle raised #292929 « Connecter avec Claude » + logo 16 (Claude #D97757, OpenAI white);
//     en cours #FFFFFF0A + border #FFFFFF14, ring 14 #FFFFFFB3 + « Cliquez sur Installer dans
//     Claude » #FFFFFFCC; connecté: border #FFFFFF14 only, logo 16 + « Claude est connecté »
//     white (+ a centred 12/16 #FFFFFF99 note); échec: raised + border #FF7B6E66,
//     « Réessayer avec Codex » + a centred message (warning-circle 13 + 12/16 #FF7B6E);
//     disque amovible: « Reconnecter Claude ». « Terminé » (accent, margin-top 4, key cap
//     « Entrée » #00000038) appears once an app is connected.
//   Footer border-top #FFFFFF0F, padding 12/20/14: « Autre app ou réglage manuel » 13/18
//     #FFFFFFB3 + caret 14 at 60 %. Unfolded (1.5) padding 12/20/16, gap 12, centred: title
//     13/18 medium #FFFFFFE6 (caret up), text 13/19 #FFFFFFB3, command (raised box h 32,
//     padding 10, JetBrains Mono 12/16 #FFFFFFCC + « Copier » raised h 32 padding 12, copy
//     14 + 13/16 medium), « Copier au format : » 12/16 #FFFFFF99 + 14 + chips JSON / TOML (raised
//     h 24, padding 6, gap 4, copy 14 at 50 % + 13/16 medium, gap 6).
//   Pill 8 under the modal: canvas, radius 8, padding 6/8, gap 6, info 14 at 60 % + « Reste sur
//     cet ordinateur » 12/16 #FFFFFF99; removable (1.6): border #FFFFFF1F, 12/16 #FFFFFFCC
//     « Navigateur sur disque amovible (E:) · reconnectez si la lettre change »; unfolded
//     (1.7): 340 wide, border #FFFFFF1F, radius 10, padding 10/12/12, gap 6: info 14 white +
//     12/16 medium white title, 12/18 #FFFFFFB3 description.
// Motion: veil 0 -> 0.549 in 200 ms; modal 0.96 -> 1 + opacity 200 ms cubic-bezier(.23,1,.32,1)
// about its centre, out in 150 ms to 0.98; the pill follows 60 ms later (opacity + 4 DIP).
// Button states cross-fade (0.3 s critical spring); blocks below a change glide on the 200 ms
// cubic-bezier(.77,0,.175,1) fold, which also carries the « réglage manuel » height (instant from
// the keyboard). Pill: 150 ms intent on hover/focus, size on a 0.3 s spring, description 60 ms
// later (opacity + 4 DIP), stays open under the pointer, Échap folds it, out in 120 ms.
// Reduced motion: fades only (150 ms in / 120 ms out), instant layout, fixed ring.
// Nothing is written here: « Connecter » calls the host (it configures the app off the UI thread
// and feeds SetStatus); copies go through hooks.copy. No drop shadow (repository rule).
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
enum class ConnectState { Idle = 0, Working = 1, Connected = 2, Failed = 3, Reconnect = 4 };
struct AppStatus {
    ConnectState state = ConnectState::Idle;
    std::wstring detail;   // Working: the hint (default « Cliquez sur Installer dans Claude »); Failed: the message; Connected: a note (« Redémarrez Codex pour l'activer »)
};
struct ConnectSetup {
    bool launch = true;                      // opened at the mode's activation (1.1): the close button / Échap answer « Plus tard »
    std::wstring command;                    // real path of the bridge (manual setup, copied as is)
    std::vector<std::wstring> args;          // its arguments (e.g. --store <path>)
    std::wstring serverName = L"browser-annotations";
    std::wstring removableDrive;             // L"E:" when the browser runs from a removable drive (1.6)
    AppStatus claude, codex;
    bool disconnectable = false;             // integration: a connected app row offers « Déconnecter » (hooks.disconnect)
    bool manual = false;                     // integration: open with « Autre app ou réglage manuel » unfolded (settings)
};
enum class ConnectEnd { Later, Done, Closed };
struct ConnectHooks {
    std::function<void(AgentKind)> connect;              // explicit click only: the host configures, then SetStatus()
    std::function<void(AgentKind)> disconnect;           // « Déconnecter » on a connected row (setup.disconnectable): removes only our entry
    std::function<void(const std::wstring&)> copy;       // the host writes the clipboard
    std::function<void(ConnectEnd)> closed;              // close button / Échap at launch = « Plus tard » (Later), « Terminé » (Done), close later on (Closed)
    std::function<void(const std::wstring&)> announce;
};

// ---- configuration texts (pure) ----
inline std::wstring JsonQuote(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) { if (c == L'\\' || c == L'"') { o += L'\\'; o += c; } else if (c == L'\n') o += L"\\n"; else if (c < 0x20) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", unsigned(c)); o += b; } else o += c; }
    return o + L"\"";
}
inline std::wstring TomlQuote(const std::wstring& s) {
    if (s.find(L'\'') == std::wstring::npos && s.find(L'\n') == std::wstring::npos) return L"'" + s + L"'";
    std::wstring o = L"\""; for (wchar_t c : s) { if (c == L'\\' || c == L'"') o += L'\\'; if (c == L'\n') { o += L"\\n"; continue; } o += c; } return o + L"\"";
}
inline std::wstring ConnectJson(const ConnectSetup& s) {
    std::wstring args = L"[";
    for (size_t i = 0; i < s.args.size(); ++i) args += (i ? L", " : L"") + JsonQuote(s.args[i]);
    args += L"]";
    return L"{\n  \"mcpServers\": {\n    " + JsonQuote(s.serverName) + L": {\n      \"command\": " + JsonQuote(s.command) + L",\n      \"args\": " + args + L"\n    }\n  }\n}";
}
inline std::wstring ConnectToml(const ConnectSetup& s) {
    bool bare = !s.serverName.empty();
    for (wchar_t c : s.serverName) if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-')) bare = false;
    std::wstring args = L"[";
    for (size_t i = 0; i < s.args.size(); ++i) args += (i ? L", " : L"") + TomlQuote(s.args[i]);
    args += L"]";
    return L"[mcp_servers." + (bare ? s.serverName : JsonQuote(s.serverName)) + L"]\ncommand = " + TomlQuote(s.command) + L"\nargs = " + args + L"\n";
}
// « C:\…\bin\browser-annotations-mcp.exe »: drive, ellipsis, the last two segments (then one).
inline std::wstring ShortPath(Ink& ink, const std::wstring& path, const Font& f, float width) {
    if (ink.Width(path, f) <= width) return path;
    std::vector<std::wstring> parts; size_t a = 0;
    for (size_t i = 0; i <= path.size(); ++i) if (i == path.size() || path[i] == L'\\' || path[i] == L'/') { parts.push_back(path.substr(a, i - a)); a = i + 1; }
    if (parts.size() >= 4) { const std::wstring s = parts.front() + L"\\…\\" + parts[parts.size() - 2] + L"\\" + parts.back(); if (ink.Width(s, f) <= width) return s; }
    if (parts.size() >= 2) { const std::wstring s = parts.front() + L"\\…\\" + parts.back(); if (ink.Width(s, f) <= width) return s; }
    return ink.Middle(path, f, width);
}
inline std::wstring AppLong(AgentKind a) { return a == AgentKind::Claude ? L"Claude Desktop" : AgentName(a); }
inline std::wstring AppButtonLabel(AgentKind a, const AppStatus& s) {
    const std::wstring n = AgentName(a);
    switch (s.state) {
    case ConnectState::Working: return s.detail.empty() ? (a == AgentKind::Claude ? std::wstring(TR(connect_click_install_claude)) : TRF(connect_connecting_to, n)) : s.detail;
    case ConnectState::Connected: return TRF(connect_is_connected, n);
    case ConnectState::Failed: return TRF(connect_retry_with, n);
    case ConnectState::Reconnect: return TRF(connect_reconnect, n);
    default: return TRF(connect_with, n);
    }
}

namespace ConnectSpec {
inline constexpr float width = 440, radius = 12, button = 32, gap = 8, pillGap = 8, pillW = 340;
inline const wchar_t* manualText() { return TR(connect_manual_text); }
inline const wchar_t* localTitle() { return TR(connect_local_title); }
inline const wchar_t* localText() { return TR(connect_local_text); }
}
enum ConnectHit { CNone = 0, CHeader = 1, CClaude = 2, CCodex = 3, CDone = 4, CManual = 5, CCopyCommand = 6, CCopyJson = 7, CCopyToml = 8, CLocal = 9, CClaudeOff = 10, CCodexOff = 11 };
enum ConnectBlock { BHeader = 1, BClaude = 10, BClaudeMsg = 11, BCodex = 20, BCodexMsg = 21, BDone = 30, BFooter = 40 };
struct ConnectView {
    ConnectSetup setup; bool manual = false; int copied = 0, focus = 0; bool keyboard = false, reduced = false; float spinner = 0;
    std::map<int, Motion::InteractionVisual> iv;
    Motion::InteractionVisual V(int id) const { auto it = iv.find(id); return it == iv.end() ? Motion::InteractionVisual{} : it->second; }
    const AppStatus& App(AgentKind a) const { return a == AgentKind::Claude ? setup.claude : setup.codex; }
    static bool Resting(ConnectState s) { return s == ConnectState::Idle || s == ConnectState::Reconnect; }
    // Closing (cross, Échap) is a « Plus tard » answer: opened at launch and nothing started yet.
    bool ClosesAsLater() const { return setup.launch && !manual && Resting(setup.claude.state) && Resting(setup.codex.state); }
    bool LaunchHeader() const { return ClosesAsLater(); }   // V2-B name (the launch header variant is gone)
    bool AnyConnected() const { return setup.claude.state == ConnectState::Connected || setup.codex.state == ConnectState::Connected; }
    bool AnyWorking() const { return setup.claude.state == ConnectState::Working || setup.codex.state == ConnectState::Working; }
    bool DoneVisible() const { return AnyConnected() && !AnyWorking(); }
    // V3 (Paper 5.6): both apps connected and both to restart => one line under the last button,
    // « Redémarrez Claude et Codex pour les activer ».
    // The message agent-connect writes is TR(agent_restart_to_enable): compare with its leading words in this language.
    static bool Restart(const AppStatus& s) { const std::wstring t = TR(agent_restart_to_enable); const std::wstring lead = t.substr(0, t.find(L"{0}")); return s.state == ConnectState::Connected && !lead.empty() && s.detail.rfind(lead, 0) == 0; }
    bool BothRestart() const { return Restart(setup.claude) && Restart(setup.codex); }
    std::wstring Message(AgentKind a) const {
        if (BothRestart()) return a == AgentKind::Codex ? std::wstring(TR(connect_restart_both)) : std::wstring();
        const auto& s = App(a); return (s.state == ConnectState::Failed || s.state == ConnectState::Connected) ? s.detail : std::wstring();
    }
    bool HasMessage(AgentKind a) const { return !Message(a).empty(); }
    bool Disconnectable(AgentKind a) const { return setup.disconnectable && App(a).state == ConnectState::Connected; }
};
struct ConnectLayout {
    struct Block { int key; float y, h; size_t state; };
    float height = 0; std::vector<Block> blocks;
    D2D1_RECT_F header{}, headerButton{}, claude{}, codex{}, claudeMsg{}, codexMsg{}, done{}, footer{}, manual{}, manualText{}, command{}, copy{}, json{}, toml{}, formatsLabel{};
    D2D1_RECT_F claudeOff{}, codexOff{};   // « Déconnecter » inside a connected row (right end)
    const Block* Find(int key) const { for (auto& b : blocks) if (b.key == key) return &b; return nullptr; }
};
inline size_t Hash(const std::wstring& s) { return std::hash<std::wstring>{}(s); }
inline ConnectLayout LayoutConnect(Ink& ink, const ConnectView& v) {
    using namespace ConnectSpec; ConnectLayout l; const float x0 = 1, inner = width - 2;
    float y = 1;
    {   const float h = 20 + 28 + 14;   // one header for every state (1.1 to 1.7)
        l.header = {x0, y, x0 + inner, y + h};
        l.headerButton = {x0 + inner - 20 - 28, y + 20, x0 + inner - 20, y + 48};
        l.blocks.push_back({BHeader, y, h, 2u}); y += h;
    }
    const float bx = x0 + 12, br = x0 + inner - 12;
    auto app = [&](AgentKind a, int key, int msgKey, D2D1_RECT_F& rect, D2D1_RECT_F& msg) {
        const auto& s = v.App(a);
        rect = {bx, y, br, y + button};
        auto& off = a == AgentKind::Claude ? l.claudeOff : l.codexOff;
        if (v.Disconnectable(a)) { const float w = 10 + ink.Width(TR(agent_disconnect), F(12, 16, 500)) + 10; off = {br - 4 - w, y + 4, br - 4, y + button - 4}; } else off = {};
        l.blocks.push_back({key, y, button, size_t(s.state) * 1000003u ^ Hash(AppButtonLabel(a, s))}); y += button;
        if (v.HasMessage(a)) { y += gap; msg = {bx, y, br, y + 16}; l.blocks.push_back({msgKey, y, 16, Hash(v.Message(a)) ^ size_t(s.state)}); y += 16; }
        else msg = {};
    };
    app(AgentKind::Claude, BClaude, BClaudeMsg, l.claude, l.claudeMsg); y += gap;
    app(AgentKind::Codex, BCodex, BCodexMsg, l.codex, l.codexMsg);
    if (v.DoneVisible()) { y += gap; l.done = {bx, y + 4, br, y + 4 + button}; l.blocks.push_back({BDone, y, 4 + button, 1}); y += 4 + button; }
    else l.done = {};
    y += 12;
    {   const float top = y;
        if (!v.manual) {
            const float tw = ink.Width(TR(settings_manual_connect), F(13, 18)) + 6 + 14;
            l.manual = {x0 + inner / 2 - tw / 2 - 4, y + 1 + 12 - 3, x0 + inner / 2 + tw / 2 + 4, y + 1 + 12 + 18 + 3};
            y += 1 + 12 + 18 + 14;
            l.command = l.copy = l.json = l.toml = l.manualText = l.formatsLabel = D2D1_RECT_F{};
        } else {
            const float tw = ink.Width(TR(settings_manual_connect), F(13, 18, 500)) + 6 + 14;
            l.manual = {x0 + inner / 2 - tw / 2 - 4, y + 1 + 12 - 3, x0 + inner / 2 + tw / 2 + 4, y + 1 + 12 + 18 + 3};
            y += 1 + 12 + 18 + 12;
            l.manualText = {x0 + 20, y, x0 + inner - 20, y + 19}; y += 19 + 12;
            const float cw = 12 + 14 + 6 + ink.Width(v.copied == CCopyCommand ? TR(connect_copied) : TR(cmd_copy), F(13, 16, 500)) + 12;
            l.copy = {x0 + inner - 20 - cw, y, x0 + inner - 20, y + 32}; l.command = {x0 + 20, y, l.copy.left - 8, y + 32}; y += 32 + 12;
            const float lw = ink.Width(TR(connect_copy_format), F(12, 16));
            auto chip = [&](const wchar_t* t, int id) { return 6 + 14 + 4 + ink.Width(v.copied == id ? TR(connect_copied) : t, F(13, 16, 500)) + 6; };
            const float jw = chip(L"JSON", CCopyJson), tw2 = chip(L"TOML", CCopyToml), total = lw + 14 + jw + 6 + tw2;
            float x = x0 + inner / 2 - total / 2;
            l.formatsLabel = {x, y, x + lw, y + 24}; x += lw + 14;
            l.json = {x, y, x + jw, y + 24}; x += jw + 6; l.toml = {x, y, x + tw2, y + 24};
            y += 24 + 16;
        }
        l.footer = {x0, top, x0 + inner, y};
        l.blocks.push_back({BFooter, top, y - top, size_t(v.manual ? 1 : 0) + size_t(v.copied) * 7});
    }
    l.height = y + 1; return l;
}
inline int ConnectHitAt(const ConnectLayout& l, const ConnectView& v, float x, float y) {
    if (Inside(l.headerButton, x, y)) return CHeader;
    if (v.Disconnectable(AgentKind::Claude) && Inside(l.claudeOff, x, y)) return CClaudeOff;
    if (v.Disconnectable(AgentKind::Codex) && Inside(l.codexOff, x, y)) return CCodexOff;
    if (Inside(l.claude, x, y)) return CClaude;
    if (Inside(l.codex, x, y)) return CCodex;
    if (v.DoneVisible() && Inside(l.done, x, y)) return CDone;
    if (Inside(l.manual, x, y)) return CManual;
    if (v.manual) { if (Inside(l.copy, x, y)) return CCopyCommand; if (Inside(l.json, x, y)) return CCopyJson; if (Inside(l.toml, x, y)) return CCopyToml; }
    return CNone;
}
inline bool AppEnabled(const AppStatus& s) { return s.state != ConnectState::Working && s.state != ConnectState::Connected; }
inline void PaintAppButton(Ink& ink, const ConnectView& v, AgentKind a, D2D1_RECT_F r, int id) {
    const auto& s = v.App(a); const auto iv = v.V(id); const bool enabled = AppEnabled(s);
    ScaleScope scale(ink.Target(), r, Motion::PressScale(iv.press, v.reduced, enabled));
    const Font f = F(13, 16, 500); const std::wstring label = AppButtonLabel(a, s);
    switch (s.state) {
    case ConnectState::Working:
        ink.Fill(r, 6, Hex(0xFFFFFF0A)); ink.Stroke(r, 6, Hex(0xFFFFFF14));
        { Piece ring = PIcon(Glyphs::ring, 14, Hex(0xFFFFFFB3), 6, v.spinner); Pieces(ink, {ring, PText(label, f, Hex(0xFFFFFFCC))}, r); }
        break;
    case ConnectState::Connected:
        ink.Stroke(r, 6, Hex(0xFFFFFF14));
        Pieces(ink, {PLogo(a, 16), PText(label, f, {1, 1, 1, 1})}, r);
        if (v.Disconnectable(a)) {   // quiet text button at the right end; the status keeps the centre
            const int off = a == AgentKind::Claude ? CClaudeOff : CCodexOff; const auto ov = v.V(off);
            const D2D1_RECT_F b{r.right - 4 - (10 + ink.Width(TR(agent_disconnect), F(12, 16, 500)) + 10), r.top + 4, r.right - 4, r.bottom - 4};
            if (ov.hover > 0 || ov.press > 0) ink.Fill(b, 4, {1, 1, 1, .06f * ov.hover + .04f * ov.press});
            ink.Text(TR(agent_disconnect), F(12, 16, 500), b, Hex(0xFFFFFFB3), false, Align::Center, true);
            if (v.focus == off) FocusRing(ink, b, 4, v.keyboard);
        }
        break;
    case ConnectState::Failed:
        ink.Fill(r, 6, RaisedFill(iv)); ink.Stroke(r, 6, Hex(0xFF7B6E66));
        Pieces(ink, {PText(label, f, {1, 1, 1, 1}), PLogo(a, 16)}, r);
        break;
    default:
        ink.Fill(r, 6, RaisedFill(iv));
        Pieces(ink, {PText(label, f, {1, 1, 1, 1}), PLogo(a, 16)}, r);
    }
    if (v.focus == id) FocusRing(ink, r, 6, v.keyboard);
}
inline void PaintAppMessage(Ink& ink, const ConnectView& v, AgentKind a, D2D1_RECT_F r) {
    const auto& s = v.App(a);
    if (s.state == ConnectState::Failed) Pieces(ink, {PIcon(Glyphs::warningCircle, 13, Hex(Ink_::danger)), PText(s.detail, F(12, 16), Hex(Ink_::danger))}, r);
    else ink.Text(v.Message(a), F(12, 16), r, Hex(0xFFFFFF99), false, Align::Center, true);
}
inline void PaintConnectHeader(Ink& ink, const ConnectView& v, const ConnectLayout& l) {
    const float x = l.header.left + 20, y = l.header.top + 20, right = l.headerButton.left - 12;
    ink.Icon(Glyphs::tree, {x, y + 4, x + 20, y + 24}, {1, 1, 1, .5f});
    ink.Text(TR(annotation_connect_agent), F(18, 24, 600, -.015f), {x + 26, y + 2, right, y + 26}, {1, 1, 1, 1});
    CloseButton(ink, l.headerButton, 16, v.V(CHeader), v.focus == CHeader, v.keyboard, v.reduced);
}
inline void PaintConnectFooter(Ink& ink, const ConnectView& v, const ConnectLayout& l, float caretTurn) {
    const auto& f = l.footer; ink.Fill({f.left, f.top, f.right, f.top + 1}, 0, Hex(0xFFFFFF0F));
    const auto mv = v.V(CManual);
    {   ScaleScope s(ink.Target(), l.manual, Motion::PressScale(mv.press, v.reduced));
        if (mv.hover > 0) ink.Fill(l.manual, 6, {1, 1, 1, .05f * mv.hover});
        Pieces(ink, {PText(TR(settings_manual_connect), v.manual ? F(13, 18, 500) : F(13, 18), Hex(v.manual ? 0xFFFFFFE6 : 0xFFFFFFB3)), PIcon(Glyphs::caretDown, 14, {1, 1, 1, .6f}, 6, 180 * caretTurn)}, l.manual);
        if (v.focus == CManual) FocusRing(ink, l.manual, 6, v.keyboard); }
    if (!v.manual) return;
    ink.Text(ConnectSpec::manualText(), F(13, 19), l.manualText, Hex(0xFFFFFFB3), false, Align::Center);
    ink.Fill(l.command, 6, Hex(Ink_::raised));
    const Font mono = Mono(12, 16, true);
    ink.Text(ShortPath(ink, v.setup.command, mono, l.command.right - l.command.left - 20), mono, {l.command.left + 10, l.command.top, l.command.right - 10, l.command.bottom}, Hex(0xFFFFFFCC), false, Align::Leading, true);
    auto chip = [&](int id, D2D1_RECT_F r, const wchar_t* label, float iconAlpha, float pad) {
        const auto iv = v.V(id); ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, v.reduced));
        ink.Fill(r, 6, RaisedFill(iv)); const bool done = v.copied == id;
        Pieces(ink, {PIcon(done ? Glyphs::check : Glyphs::copy, 14, {1, 1, 1, done ? 1.f : iconAlpha}, pad == 12 ? 6.f : 4.f), PText(done ? TR(connect_copied) : label, F(13, 16, 500), {1, 1, 1, 1})}, r);
        if (v.focus == id) FocusRing(ink, r, 6, v.keyboard);
    };
    chip(CCopyCommand, l.copy, TR(cmd_copy), 1, 12);
    ink.Text(TR(connect_copy_format), F(12, 16), l.formatsLabel, Hex(0xFFFFFF99), false, Align::Leading, true);
    chip(CCopyJson, l.json, L"JSON", .5f, 6); chip(CCopyToml, l.toml, L"TOML", .5f, 6);
}
inline void PaintConnectBlock(Ink& ink, const ConnectView& v, const ConnectLayout& l, int key, float caretTurn) {
    switch (key) {
    case BHeader: PaintConnectHeader(ink, v, l); break;
    case BClaude: PaintAppButton(ink, v, AgentKind::Claude, l.claude, CClaude); break;
    case BCodex: PaintAppButton(ink, v, AgentKind::Codex, l.codex, CCodex); break;
    case BClaudeMsg: PaintAppMessage(ink, v, AgentKind::Claude, l.claudeMsg); break;
    case BCodexMsg: PaintAppMessage(ink, v, AgentKind::Codex, l.codexMsg); break;
    case BDone: {
        const auto iv = v.V(CDone); ScaleScope s(ink.Target(), l.done, Motion::PressScale(iv.press, v.reduced));
        ink.Fill(l.done, 6, AccentButton(iv.hover, iv.press));
        Pieces(ink, {PText(TR(common_done), F(13, 16, 500), {1, 1, 1, 1}), PKey(TR(key_enter), Hex(0x00000038), {1, 1, 1, 1})}, l.done);
        if (v.focus == CDone) FocusRing(ink, l.done, 6, v.keyboard);
        break; }
    case BFooter: PaintConnectFooter(ink, v, l, caretTurn); break;
    }
}
// The « local » pill: geometry for a fold amount (0 collapsed .. 1 unfolded), top-centred at (cx, top).
struct LocalPill { D2D1_RECT_F rect{}; float radius = 8; };
inline std::wstring LocalLabel(const ConnectSetup& s) { return s.removableDrive.empty() ? std::wstring(ConnectSpec::localTitle()) : TRF(connect_removable_drive, s.removableDrive); }
inline D2D1_SIZE_F LocalCollapsed(Ink& ink, const ConnectSetup& s) { const float b = s.removableDrive.empty() ? 0.f : 2.f; return {8 + 14 + 6 + ink.Width(LocalLabel(s), F(12, 16)) + 8 + b, 28 + b}; }
inline float LocalExpandedH(Ink& ink) { return 1 + 10 + 16 + 6 + ink.Height(ConnectSpec::localText(), F(12, 18), ConnectSpec::pillW - 26) + 12 + 1; }
inline LocalPill LayoutLocal(Ink& ink, const ConnectSetup& s, float cx, float top, float fold) {
    const auto c = LocalCollapsed(ink, s); const float w = Lerp(c.width, ConnectSpec::pillW, fold), h = Lerp(c.height, LocalExpandedH(ink), fold);
    LocalPill p; p.rect = {cx - w / 2, top, cx + w / 2, top + h}; p.radius = Lerp(8, 10, fold); return p;
}
inline void PaintLocal(Ink& ink, const ConnectView& v, const LocalPill& p, float fold, float desc, bool focus) {
    const bool removable = !v.setup.removableDrive.empty();
    ink.Fill(p.rect, p.radius, Hex(Ink_::canvas));
    if (removable || fold > 0) ink.Stroke(p.rect, p.radius, WithAlpha(Hex(0xFFFFFF1F), removable ? 1.f : Motion::Clamp(fold * 2)));
    const float a = Motion::Clamp(1 - fold * 2.5f);
    if (a > 0) {
        ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(a, g);
        Pieces(ink, {PIcon(Glyphs::infoLocal, 14, {1, 1, 1, .6f}), PText(LocalLabel(v.setup), F(12, 16), Hex(removable ? 0xFFFFFFCC : 0xFFFFFF99))}, {p.rect.left, p.rect.top, p.rect.right, p.rect.top + (p.rect.bottom - p.rect.top) * (1 - fold) + 28 * fold}, Align::Center);
        if (pushed) ink.PopOpacity(g);
    }
    if (fold > 0) {
        ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, p.rect, p.radius, layer, clip);
        const float x = p.rect.left + 12, top = p.rect.top + 11, b = Motion::Clamp(fold * 2 - .6f);
        if (b > 0) { ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(b, g);
            Pieces(ink, {PIcon(Glyphs::infoLocal, 14, {1, 1, 1, 1}), PText(ConnectSpec::localTitle(), F(12, 16, 500), {1, 1, 1, 1})}, {x, top, p.rect.right - 12, top + 16}, Align::Leading);
            if (pushed) ink.PopOpacity(g); }
        if (desc > 0) { const float dy = (1 - desc) * Spec::pillLift;
            ink.Text(ConnectSpec::localText(), F(12, 18), {x, top + 22 + dy, x + ConnectSpec::pillW - 26, top + 22 + dy + 60}, WithAlpha(Hex(0xFFFFFFB3), desc), true); }
        PopClip(ink, layer, clip);
    }
    if (focus) FocusRing(ink, p.rect, p.radius, v.keyboard);
}

// The modal at rest (no glide, no cross-fade) + its pill: QA captures and the harness.
inline float ConnectGroupHeight(Ink& ink, const ConnectView& v, const ConnectLayout& l) { return l.height + ConnectSpec::pillGap + LocalCollapsed(ink, v.setup).height; }
inline void PaintConnectStatic(Ink& ink, const ConnectView& v, const ConnectLayout& l, float fold = 0, float desc = 0) {
    const D2D1_RECT_F box{0, 0, ConnectSpec::width, l.height};
    ink.Fill(box, ConnectSpec::radius, Hex(Ink_::canvas));
    for (const auto& b : l.blocks) PaintConnectBlock(ink, v, l, b.key, v.manual ? 1.f : 0.f);
    ink.Stroke(box, ConnectSpec::radius, Hex(0xFFFFFF14));
    PaintLocal(ink, v, LayoutLocal(ink, v.setup, ConnectSpec::width / 2, l.height + ConnectSpec::pillGap, fold), fold, desc, v.focus == CLocal);
}

class ConnectModal : public Animator::Client {
    // UIA keys: distinct from the ConnectHit ids used as node keys (CHeader = 1…), or the tree nests into itself.
    static constexpr long long DialogKey = 501, StatusKey = 502, LocalTextKey = 503;
    static constexpr UINT_PTR IntentTimer = 21, CopiedTimer = 22;
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_; ConnectHooks hooks_;
    Layer modal_; HWND veil_ = nullptr;
    ConnectView view_, before_; ConnectLayout layout_, beforeLayout_;
    std::map<int, float> fromY_; float fromH_ = 0; Ramp glide_; Crossfade fade_; int fadeStamp_ = 0;
    Presence presence_{ModalTiming()}; Ramp veil_t_, pillIn_, desc_; Motion::Spring fold_{.002f}; Ramp caret_;
    Motion::State motion_;
    RECT client_{}; int top_ = 0; bool open_ = false, reduced_ = false, localOpen_ = false, localHover_ = false; int hover_ = 0, pressed_ = 0;

    Ink& I() { return *Ink::Instance(); }
    float Scale() const { return modal_.Scale(); }
    int Px(float d) const { return int(std::lround(d * Scale())); }
    float ModalH(Motion::Time now) const { return Lerp(fromH_, layout_.height, glide_.Value(now)); }
    float BlockY(int key, Motion::Time now) const {
        const auto* b = layout_.Find(key); if (!b) return 0;
        auto it = fromY_.find(key); return it == fromY_.end() ? b->y : Lerp(it->second, b->y, glide_.Value(now));
    }
    float PillTop(Motion::Time now) const { return ModalH(now) + ConnectSpec::pillGap; }
    float LayerH() const { return std::max(layout_.height, fromH_) + ConnectSpec::pillGap + LocalExpandedH(*Ink::Instance()) + 8; }
    RECT LayerRect() const {
        const int w = Px(ConnectSpec::width), x = (client_.left + client_.right - w) / 2;
        return {x, top_, x + w, top_ + Px(LayerH())};
    }
    LocalPill Local(Motion::Time now) { return LayoutLocal(I(), view_.setup, ConnectSpec::width / 2, PillTop(now), Motion::Clamp(fold_.Value(now))); }
    void Relayout() { layout_ = LayoutConnect(I(), view_); }
    ConnectView Visual(const ConnectView& base, Motion::Time now) const {
        ConnectView v = base; v.reduced = reduced_; v.spinner = SpinnerAngle(now, reduced_);
        for (int id = CHeader; id <= CLocal; ++id) v.iv[id] = motion_.Visual(id, now);
        return v;
    }
    void Render(Motion::Time now) {
        if (!modal_.Window()) return;
        const Pose p = presence_.Sample(now, reduced_);
        const float h = ModalH(now), t = glide_.Value(now), in = fade_.Incoming(now), out = fade_.Outgoing(now);
        const ConnectView v = Visual(view_, now), b = before_; const ConnectLayout l = layout_, bl = beforeLayout_;
        std::map<int, float> ys; for (auto& blk : l.blocks) ys[blk.key] = BlockY(blk.key, now);
        std::map<int, float> from = fromY_;
        const LocalPill pill = Local(now); const float fold = Motion::Clamp(fold_.Value(now)), desc = Motion::Clamp(desc_.Value(now));
        const float pillA = Motion::Clamp(pillIn_.Value(now)), caret = caret_.Value(now);
        const bool fading = fade_.Moving(now) && fade_.Previous() >= 0, gliding = glide_.Moving(now);
        const bool localFocus = v.focus == CLocal;
        modal_.paint = [=](Ink& ink, float w, float) {
            const D2D1_POINT_2F origin{w / 2, h / 2};
            Posed(ink, p, origin, [&] {
                const D2D1_RECT_F box{0, 0, ConnectSpec::width, h};
                ink.Fill(box, ConnectSpec::radius, Hex(Ink_::canvas));
                ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, box, ConnectSpec::radius, layer, clip);
                auto* tg = ink.Target(); D2D1_MATRIX_3X2_F base; tg->GetTransform(&base);
                for (const auto& blk : l.blocks) {
                    const auto* old = bl.Find(blk.key); const bool changed = old && old->state != blk.state, added = !old && from.find(blk.key) == from.end() && gliding;
                    const float dy = ys.at(blk.key) - blk.y;
                    const float alpha = added ? t : (changed && fading ? in : 1.f);
                    tg->SetTransform(D2D1::Matrix3x2F::Translation(0, dy) * base);
                    if (changed && fading && out > 0) {   // outgoing state, at the same glide offset
                        const float ody = ys.at(blk.key) - old->y;
                        tg->SetTransform(D2D1::Matrix3x2F::Translation(0, ody) * base);
                        ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(out, g); PaintConnectBlock(ink, b, bl, blk.key, b.manual ? 1.f : 0.f); if (pushed) ink.PopOpacity(g);
                        tg->SetTransform(D2D1::Matrix3x2F::Translation(0, dy) * base);
                    }
                    ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(alpha, g); PaintConnectBlock(ink, v, l, blk.key, blk.key == BFooter ? caret : 0.f); if (pushed) ink.PopOpacity(g);
                }
                if (gliding) for (const auto& old : bl.blocks) if (!l.Find(old.key)) {   // removed blocks fade where they were
                    auto it = from.find(old.key); if (it == from.end()) continue;
                    tg->SetTransform(D2D1::Matrix3x2F::Translation(0, it->second - old.y) * base);
                    ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(1 - t, g); PaintConnectBlock(ink, b, bl, old.key, 0); if (pushed) ink.PopOpacity(g);
                }
                tg->SetTransform(base);
                PopClip(ink, layer, clip);
                ink.Stroke(box, ConnectSpec::radius, Hex(0xFFFFFF14));
            });
            if (pillA > 0) {   // follows 60 ms later: opacity + 4 DIP
                Pose pp = p; pp.opacity = std::min(p.opacity, pillA); if (!v.reduced) pp.dy = p.dy - (1 - pillA) * Spec::pillLift;
                Posed(ink, pp, origin, [&] { PaintLocal(ink, v, pill, fold, desc, localFocus); });
            }
        };
        modal_.Present(LayerRect(), true);
        if (veil_) {
            SetLayeredWindowAttributes(veil_, 0, BYTE(std::lround(255 * Spec::veilOpacity * Motion::Clamp(veil_t_.Value(now)))), LWA_ALPHA);
            SetWindowPos(veil_, modal_.Window(), client_.left, client_.top, client_.right - client_.left, client_.bottom - client_.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    void Announce(const std::wstring& text) { if (text.empty()) return; if (hooks_.announce) hooks_.announce(text); else modal_.uia.Notify(DialogKey, text.c_str(), L"AnnotationConnect"); }
    // A change of state or layout: blocks glide from where they are shown, changed blocks cross-fade.
    template<class F> void Change(F&& mutate, bool instant = false) {
        const auto now = Motion::Now();
        std::map<int, float> presented; for (auto& b : layout_.blocks) presented[b.key] = BlockY(b.key, now);
        const float h = ModalH(now);
        before_ = Visual(view_, now); beforeLayout_ = layout_;
        mutate(); Relayout();
        fromY_ = presented; fromH_ = h;
        const bool jump = instant || reduced_;
        glide_.Snap(0); glide_.Set(1, now, jump ? 0 : Spec::fold, Curves::inOut);
        caret_.Set(view_.manual ? 1.f : 0.f, now, jump ? 0 : Spec::fold, Curves::inOut);
        bool changed = false; for (auto& b : layout_.blocks) if (auto* o = beforeLayout_.Find(b.key)) if (o->state != b.state) changed = true;
        if (changed) fade_.Go(++fadeStamp_, now);
        if (Order().end() == std::find(Order().begin(), Order().end(), view_.focus)) view_.focus = 0;
        Render(now); Animator::Get().Request(this);
        if (modal_.uia.Listening()) modal_.uia.ChildrenChanged(DialogKey);
    }
    std::vector<int> Order() const {
        std::vector<int> o{CHeader};
        for (AgentKind a : {AgentKind::Claude, AgentKind::Codex}) { o.push_back(a == AgentKind::Claude ? CClaude : CCodex); if (view_.Disconnectable(a)) o.push_back(a == AgentKind::Claude ? CClaudeOff : CCodexOff); }
        if (view_.DoneVisible()) o.push_back(CDone);
        o.push_back(CManual);
        if (view_.manual) { o.push_back(CCopyCommand); o.push_back(CCopyJson); o.push_back(CCopyToml); }
        o.push_back(CLocal); return o;
    }
    void SetLocal(bool open) {
        if (open == localOpen_) return; localOpen_ = open; const auto now = Motion::Now();
        if (open) { fold_.Set(1, now, Spec::stateResponse, reduced_); desc_.Set(1, now, reduced_ ? 150 : 200, Curves::out, reduced_ ? 0 : Spec::localDescDelay); }
        else { fold_.Set(0, now, reduced_ ? 0.f : .2f, reduced_); desc_.Set(0, now, Spec::localOut, Curves::out); }
        Render(now); Animator::Get().Request(this);
        if (modal_.uia.Listening()) modal_.uia.ChildrenChanged(DialogKey);
    }
    void Copy(int id, const std::wstring& text) {
        if (hooks_.copy) hooks_.copy(text);
        Change([&] { view_.copied = id; });
        KillTimer(modal_.Window(), CopiedTimer); SetTimer(modal_.Window(), CopiedTimer, 1600, nullptr);
        Announce(TR(connect_copied));
    }
    void Activate(int hit, bool keyboard) {
        view_.keyboard = keyboard;
        switch (hit) {
        case CHeader: Close(true, view_.ClosesAsLater() ? ConnectEnd::Later : ConnectEnd::Closed); break;
        case CClaude: case CCodex: {
            const AgentKind a = hit == CClaude ? AgentKind::Claude : AgentKind::Codex;
            if (!AppEnabled(view_.App(a))) return;
            if (hooks_.connect) hooks_.connect(a);
            break; }
        case CClaudeOff: case CCodexOff: {
            const AgentKind a = hit == CClaudeOff ? AgentKind::Claude : AgentKind::Codex;
            if (view_.Disconnectable(a) && hooks_.disconnect) hooks_.disconnect(a);
            break; }
        case CDone: Close(true, ConnectEnd::Done); break;
        case CManual: Change([&] { view_.manual = !view_.manual; view_.copied = 0; }, keyboard); Announce(view_.manual ? TR(connect_manual_shown) : TR(connect_manual_hidden)); break;
        case CCopyCommand: Copy(CCopyCommand, view_.setup.command); break;
        case CCopyJson: Copy(CCopyJson, ConnectJson(view_.setup)); break;
        case CCopyToml: Copy(CCopyToml, ConnectToml(view_.setup)); break;
        case CLocal: SetLocal(!localOpen_); break;
        }
    }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Render(now); Animator::Get().Request(this); }
    void Focus(int id) {
        view_.focus = id; view_.keyboard = true; motion_.ClearInteractions();
        KillTimer(modal_.Window(), IntentTimer);
        if (id == CLocal) SetTimer(modal_.Window(), IntentTimer, Spec::localIntent, nullptr); else if (!localHover_) SetLocal(false);
        Render(Motion::Now()); if (modal_.uia.Listening()) modal_.uia.Raise(id, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        const auto o = Order(); auto it = std::find(o.begin(), o.end(), view_.focus);
        int i = it == o.end() ? (back ? int(o.size()) - 1 : 0) : int(it - o.begin()) + (back ? -1 : 1); i = (i + int(o.size())) % int(o.size());
        Focus(o[size_t(i)]);
    }
    bool Key(WPARAM key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (key) {
        case VK_ESCAPE: if (localOpen_) { SetLocal(false); return true; } Close(true, view_.ClosesAsLater() ? ConnectEnd::Later : ConnectEnd::Closed); return true;
        case VK_TAB: Cycle(shift); return true;
        case VK_RETURN: if (view_.focus) Activate(view_.focus, true); else if (view_.DoneVisible()) Activate(CDone, true); return true;
        case VK_SPACE: if (view_.focus) Activate(view_.focus, true); return true;
        }
        return false;
    }
    int HitAt(float x, float y) {
        const auto now = Motion::Now(); const auto pill = Local(now);
        if (Inside(pill.rect, x, y)) return CLocal;
        if (y > ModalH(now)) return CNone;
        return ConnectHitAt(layout_, view_, x, y);
    }
    bool Handle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, modal_.Window(), 0}; TrackMouseEvent(&t);
            const int h = HitAt(x, y);
            const bool overLocal = h == CLocal;
            if (overLocal != localHover_) {
                localHover_ = overLocal; KillTimer(modal_.Window(), IntentTimer);
                if (overLocal) SetTimer(modal_.Window(), IntentTimer, Spec::localIntent, nullptr);
                else if (view_.focus != CLocal) SetLocal(false);
            }
            if (h != hover_) { hover_ = h; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: hover_ = 0; if (localHover_) { localHover_ = false; KillTimer(modal_.Window(), IntentTimer); if (view_.focus != CLocal) SetLocal(false); } Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: pressed_ = HitAt(x, y); view_.keyboard = false; if (pressed_) SetCapture(modal_.Window()); Interact(); r = 0; return true;
        case WM_LBUTTONUP: { const int h = HitAt(x, y), was = pressed_; pressed_ = 0; if (GetCapture() == modal_.Window()) ReleaseCapture(); Interact(); if (was && was == h) Activate(h, false); r = 0; return true; }
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: if (Key(w)) { r = 0; return true; } return false;
        case WM_TIMER:
            if (w == IntentTimer) { KillTimer(modal_.Window(), IntentTimer); if (localHover_ || view_.focus == CLocal) SetLocal(true); r = 0; return true; }
            if (w == CopiedTimer) { KillTimer(modal_.Window(), CopiedTimer); if (view_.copied) Change([&] { view_.copied = 0; }); r = 0; return true; }
            return false;
        case WM_APP + 1: Activate(int(w), true); r = 0; return true;
        case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && open_) SetFocus(modal_.Window()); return false;
        }
        return false;
    }
    static LRESULT CALLBACK VeilProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATEANDEAT;
        if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE; }
        if (m == WM_ERASEBKGND) { RECT r; GetClientRect(h, &r); FillRect(reinterpret_cast<HDC>(w), &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); return 1; }
        return DefWindowProcW(h, m, w, l);
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!open_) return n;
        const auto now = Motion::Now();
        Accessibility::Node d; d.key = DialogKey; d.role = Accessibility::Role::Group; d.name = TR(annotation_connect_agent);
        d.automationId = L"AnnotationConnect"; d.focusable = false; GetWindowRect(modal_.Window(), &d.bounds); n.push_back(d);
        auto node = [&](long long key, Accessibility::Role role, const std::wstring& name, D2D1_RECT_F r) -> Accessibility::Node& {
            Accessibility::Node x; x.key = key; x.parent = DialogKey; x.role = role; x.name = name; x.bounds = modal_.Screen(r); n.push_back(x); return n.back(); };
        if (view_.ClosesAsLater()) { auto& b = node(CHeader, Accessibility::Role::Button, TR(connect_close_later), layout_.headerButton); b.accelerator = TR(key_escape); b.automationId = L"AnnotationConnectLater"; }
        else { auto& b = node(CHeader, Accessibility::Role::Button, TR(common_close), layout_.headerButton); b.accelerator = TR(key_escape); b.automationId = L"AnnotationConnectClose"; }
        for (AgentKind a : {AgentKind::Claude, AgentKind::Codex}) {
            const auto& s = view_.App(a); const int id = a == AgentKind::Claude ? CClaude : CCodex;
            auto& b = node(id, Accessibility::Role::Button, AppButtonLabel(a, s), a == AgentKind::Claude ? layout_.claude : layout_.codex);
            b.enabled = AppEnabled(s); b.automationId = a == AgentKind::Claude ? L"AnnotationConnectClaude" : L"AnnotationConnectCodex";
            b.status = s.state == ConnectState::Working ? TR(demo_in_progress) : s.state == ConnectState::Connected ? TR(agent_connected) : s.state == ConnectState::Failed ? TR(downloads_failed) : L"";
            if (view_.HasMessage(a)) b.help = view_.Message(a);
            if (view_.Disconnectable(a)) { auto& d = node(a == AgentKind::Claude ? CClaudeOff : CCodexOff, Accessibility::Role::Button, TRF(connect_disconnect_app, AppLong(a)), a == AgentKind::Claude ? layout_.claudeOff : layout_.codexOff);
                d.help = TRF(connect_disconnect_help, AppLong(a)); d.automationId = a == AgentKind::Claude ? L"AnnotationDisconnectClaude" : L"AnnotationDisconnectCodex"; }
        }
        if (view_.DoneVisible()) { auto& b = node(CDone, Accessibility::Role::Button, TR(common_done), layout_.done); b.accelerator = TR(key_enter); b.automationId = L"AnnotationConnectDone"; }
        { auto& b = node(CManual, Accessibility::Role::Button, TR(settings_manual_connect), layout_.manual); b.expand = view_.manual ? 2 : 1; b.automationId = L"AnnotationConnectManual"; }
        if (view_.manual) {
            auto& t = node(StatusKey, Accessibility::Role::Text, ConnectSpec::manualText(), layout_.command); t.value = view_.setup.command; t.focusable = false;
            auto& c = node(CCopyCommand, Accessibility::Role::Button, view_.copied == CCopyCommand ? TR(connect_copied) : TR(connect_copy_command), layout_.copy); c.help = view_.setup.command;
            node(CCopyJson, Accessibility::Role::Button, view_.copied == CCopyJson ? TR(connect_copied) : TR(connect_copy_json), layout_.json);
            node(CCopyToml, Accessibility::Role::Button, view_.copied == CCopyToml ? TR(connect_copied) : TR(connect_copy_toml), layout_.toml);
        }
        const auto pill = Local(now);
        auto& p = node(CLocal, Accessibility::Role::Button, LocalLabel(view_.setup), pill.rect); p.expand = localOpen_ ? 2 : 1; p.help = ConnectSpec::localText(); p.automationId = L"AnnotationLocalPill";
        return n;
    }
    bool Ensure() {
        if (modal_.Window()) return true;
        if (!Ink::Shared(root_)) return false;
        WNDCLASSW wc{}; wc.lpfnWndProc = VeilProc; wc.hInstance = instance_; wc.lpszClassName = L"BrowserAnnotationVeil"; RegisterClassW(&wc);
        veil_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, nullptr);
        if (veil_) SetLayeredWindowAttributes(veil_, 0, 0, LWA_ALPHA);
        Layer::Options o; o.activatable = true; o.title = TR(annotation_connect_agent);
        if (!modal_.Create(instance_, owner_, o)) return false;
        modal_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Handle(m, w, l, r); };
        Accessibility::Host host; host.window = modal_.Window(); host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return view_.focus; };
        host.invoke = [this](long long key) { PostMessageW(modal_.Window(), WM_APP + 1, WPARAM(key), 0); };
        host.expand = [this](long long key, bool) { PostMessageW(modal_.Window(), WM_APP + 1, WPARAM(key), 0); };
        modal_.uia.Attach(std::move(host));
        motion_.EnablePolish();
        return true;
    }
public:
    ~ConnectModal() { if (Animator::Exists()) Animator::Get().Forget(this); if (veil_ && IsWindow(veil_)) DestroyWindow(veil_); }
    void Init(HINSTANCE instance, HWND owner, std::wstring root, ConnectHooks hooks) { instance_ = instance; owner_ = owner; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return modal_.Window() != nullptr; }
    bool Visible() const { return open_; }
    void AccentChanged() { if (modal_.Window() && presence_.Visible(Motion::Now())) Render(Motion::Now()); }   // « Terminé »
    HWND Window() const { return modal_.Window(); }
    HWND Veil() const { return veil_; }
    // ownerClient: the browser window's client area in screen pixels (veil + centring).
    // delayMs: « Spéc · Lancement » — the band fades first, the veil and modal follow 80 ms later.
    bool Show(const ConnectSetup& setup, RECT ownerClient, bool reduced, int delayMs = 0) {
        if (!Ensure()) return false;
        const auto now = Motion::Now(); reduced_ = reduced; motion_.SetReducedMotion(reduced); client_ = ownerClient;
        view_ = ConnectView{}; view_.setup = setup; view_.manual = setup.manual && !setup.launch; view_.keyboard = true; Relayout(); before_ = view_; beforeLayout_ = layout_;
        fromY_.clear(); fromH_ = layout_.height; glide_.Snap(1); caret_.Snap(0); fade_.Reset(0);
        localOpen_ = localHover_ = false; fold_.Snap(0); desc_.Snap(0); hover_ = pressed_ = 0; motion_.ClearInteractions();
        const float group = layout_.height + ConnectSpec::pillGap + LocalCollapsed(I(), setup).height;
        top_ = std::max<int>(client_.top + Px(16), (client_.top + client_.bottom - Px(group)) / 2);
        Timing t = ModalTiming(); t.delay = delayMs; presence_.Configure(t);
        open_ = true; presence_.Show(now, true, reduced);
        veil_t_.Snap(0); veil_t_.Set(1, now, reduced ? Spec::modalReducedIn : Spec::modalIn, Curves::out, delayMs);
        pillIn_.Snap(0); pillIn_.Set(1, now, reduced ? Spec::modalReducedIn : Spec::modalIn, Curves::out, delayMs + (reduced ? 0 : Spec::pillDelay));
        view_.focus = 0;
        if (owner_) EnableWindow(owner_, FALSE);   // modal: the browser behind waits
        Render(now);
        SetForegroundWindow(modal_.Window()); SetFocus(modal_.Window());
        Animator::Get().Request(this);
        if (modal_.uia.Listening()) modal_.uia.Raise(DialogKey, UIA_Window_WindowOpenedEventId);
        Announce(TR(annotation_connect_agent));
        return true;
    }
    // From the host's connection work (explicit click) or its detection of readers\ / configs.
    void SetStatus(AgentKind a, const AppStatus& status) {
        if (a == AgentKind::Other) return;
        const AppStatus was = view_.App(a);
        if (was.state == status.state && was.detail == status.detail) return;
        if (!modal_.Window()) { (a == AgentKind::Claude ? view_.setup.claude : view_.setup.codex) = status; return; }
        Change([&] { (a == AgentKind::Claude ? view_.setup.claude : view_.setup.codex) = status; });
        switch (status.state) {
        case ConnectState::Working: Announce(AppButtonLabel(a, status)); break;
        case ConnectState::Connected: Announce(AppLong(a) + L" connecté" + (status.detail.empty() ? L"" : L". " + status.detail)); if (!view_.focus || view_.focus == CClaude || view_.focus == CCodex) view_.focus = view_.DoneVisible() && view_.keyboard ? CDone : view_.focus; break;
        case ConnectState::Failed: Announce(TRF(connect_failed_announce, status.detail.empty() ? std::wstring(TR(connect_impossible)) : status.detail)); break;
        default: break;
        }
    }
    void SetRemovable(const std::wstring& drive) { if (drive == view_.setup.removableDrive) return; Change([&] { view_.setup.removableDrive = drive; }); }
    void Close(bool animate = true, ConnectEnd end = ConnectEnd::Closed) {
        if (!open_) return; const auto now = Motion::Now(); open_ = false;
        if (modal_.Window()) { KillTimer(modal_.Window(), IntentTimer); KillTimer(modal_.Window(), CopiedTimer); }
        if (owner_) { EnableWindow(owner_, TRUE); SetForegroundWindow(owner_); }
        presence_.Hide(now, animate, reduced_); hover_ = pressed_ = 0;
        veil_t_.Set(0, now, animate ? (reduced_ ? Spec::modalReducedOut : Spec::modalOut) : 0, Curves::out);
        pillIn_.Set(0, now, animate ? (reduced_ ? Spec::modalReducedOut : Spec::modalOut) : 0, Curves::out);
        if (!presence_.Visible(now)) { modal_.Hide(); if (veil_) ShowWindow(veil_, SW_HIDE); } else { Render(now); Animator::Get().Request(this); }
        if (modal_.uia.Listening()) modal_.uia.Raise(DialogKey, UIA_Window_WindowClosedEventId);
        if (hooks_.closed) hooks_.closed(end);
    }
    bool Frame(Motion::Time now) override {
        if (!presence_.Visible(now) && !veil_t_.Moving(now)) { modal_.Hide(); if (veil_) ShowWindow(veil_, SW_HIDE); return false; }
        Render(now);
        const bool spin = open_ && view_.AnyWorking() && !reduced_;
        return presence_.Moving(now) || veil_t_.Moving(now) || pillIn_.Moving(now) || glide_.Moving(now) || fade_.Moving(now) || fold_.Moving(now) || desc_.Moving(now) || caret_.Moving(now) || spin || motion_.NextWake(now).delay.has_value();
    }
    // ---- tests / QA
    ConnectView& ViewForTest() { return view_; }
    const ConnectLayout& LayoutForTest() const { return layout_; }
    Layer& LayerForTest() { return modal_; }
    Presence& PresenceForTest() { return presence_; }
    bool KeyForTest(WPARAM key) { return Key(key); }
    void ActivateForTest(int hit) { Activate(hit, true); }
    void LocalForTest(bool open) { SetLocal(open); }
    bool LocalOpenForTest() const { return localOpen_; }
    int FocusForTest() const { return view_.focus; }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    float ModalHeightForTest(Motion::Time now) const { return ModalH(now); }
};
}} // namespace NativeChrome::Annotations
