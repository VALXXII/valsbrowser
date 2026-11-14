#pragma once
// « Connecter un agent » onboarding (Paper XAF-0 choose, XCE-0 3a Claude Desktop, XFG-0 3b
// Codex, XHN-0 4 connected; veil XCD-0 #0000008C).
//   Modal 560 wide, raised #292929, border 1 #FFFFFF14, radius 12.
//   Header padding 20/20/16, gap 14: title 20/26 semibold(-> bundled Medium) -0.015em white,
//     subtitle 14/20 #FFFFFFB3 (gap 4); close 28 (glyph 16 at 70 %); steps: circles 18
//     (active accent, done #FFFFFF29 + check 11, todo #FFFFFF1A with « 2 » 11/16 #FFFFFF99),
//     labels 12/16 (active white medium, done #FFFFFFB3, todo #FFFFFF99), gap 6; connectors
//     24x1 #FFFFFF29, gap 8.
//   Choose body padding 4/20/16, gap 8: options (padding 12, gap 12, radius 8; chosen
//     #3E67FF1A + border #3E67FF99 + radio 18 ring 5 accent; others #FFFFFF08 + border
//     #FFFFFF14 + radio 1.5 #FFFFFF66), tile 36 radius 8 #FFFFFF14 (« C » 14/18, « Cx » 13/16,
//     code glyph 18 at 80 %), name 14/20 medium + « Recommandé » (h 18, radius 4, #FFFFFF14,
//     11/14 medium #FFFFFFE6), description 13/18 #FFFFFF99; « Limiter cet agent au projet
//     Boutique » (padding 8/2, gap 10, checkbox 16, 13/18).
//   Install body padding 4/20/18, gap 12: card #FFFFFF08 border #FFFFFF14 radius 8 padding 16
//     gap 12 (title 14/20 medium, text 13/19 #FFFFFFB3); Claude: « Ouvrir dans Claude
//     Desktop » (accent, open-out 16, padding 14, gap 8) + « annotations-browser.mcpb ·
//     1,2 Mo » 12/16 #FFFFFF99; Codex/other: « Commande » / « Arguments » rows (label w 76
//     12/16, value box #171717 border #FFFFFF0F radius 6 h 28 padding 10, JetBrains Mono
//     12/16, « Copier » h 28 padding 10 #333333 12/16 medium). Manual method: caret 14 +
//     13/18 medium + path 12/16 #FFFFFF99; code block #171717 border #FFFFFF0F radius 8
//     padding 12, JetBrains Mono 12/18 #FFFFFFCC, ligatures off, « Copier » with copy 14.
//   Verify: card padding 16 gap 10: « Pour vérifier, demandez dans Claude : » 13/18; quote
//     16/22 medium + Copier; status row radius 6 padding 10/12 gap 10 — waiting: ring 20
//     turning + « En attente de la première lecture… » (+ « Ça ne marche pas ? » after
//     2 min, one-shot); read: #3E67FF1F, accent disc 20 + check 12, « Claude a lu vos
//     annotations » medium + « · il y a quelques secondes · 1 ouverte » #FFFFFFB3.
//     Agents list: top border #FFFFFF0F, « Agents » 12/16 medium #FFFFFF99, rows padding
//     6/4 gap 12: tile 28 radius 6, name 14/20 medium, detail 12/16 #FFFFFF99, action h 28
//     padding 10 #FFFFFF0F 12/16 medium.
//   Footer #FFFFFF05, top border #FFFFFF0F, padding 14/20/16, gap 16: lock 16 at 70 % + 12/17
//     #FFFFFF99 note; buttons h 32 padding 14 (secondary #333333; primary accent + « Entrée »
//     key cap #00000038). One filled accent per view: on the Claude install step the accent
//     is « Ouvrir dans Claude Desktop », so « C'est installé » stays #333333 (as in Paper).
// Nothing is launched and no configuration is written here: the host receives callbacks
// (open the .mcpb, copy a text, help, agent actions) and feeds the verification status
// (it watches readers\ only while this modal is open).
// Motion: veil + modal from the centre (scale 0.96 -> 1, opacity), exit by the same path in
// 150 ms; steps cross-fade (200 ms) while the height follows a 0.35 s critical spring.
// Reduced motion: opacity only (veil and modal fade, steps cross-fade, height jumps).
#include "annotation-kit.h"
#include "annotation-note.h"   // KeyButton, Checkbox, Spinner

namespace NativeChrome { namespace Annotations {
enum class AgentApp { Claude = 0, Codex = 1, Other = 2 };
enum class OnboardingStep { Choose = 0, Install = 1, Verify = 2 };
struct AgentRow { std::wstring monogram, name, detail; bool connected = false; };
struct OnboardingSetup {
    std::wstring projectId, projectLabel;      // « boutique », « Boutique »
    std::wstring bridgePath;                   // real path of browser-annotations-mcp.exe
    std::wstring mcpbName = L"annotations-browser.mcpb", mcpbSize = L"1,2 Mo";   // mcpbName empty: no .mcpb in this build (T3 V1)
    std::wstring serverName = L"browser-annotations";                            // key in mcpServers / [mcp_servers.<name>]
};
struct VerifyStatus {
    bool read = false;
    std::wstring reader;                       // « Claude » (clientInfo label, short)
    std::wstring when = L"il y a quelques secondes", detail;  // detail: « 1 ouverte »
    std::vector<AgentRow> agents;
};

// ---- configuration text (pure; the host copies, never writes another app's config) ----
inline std::wstring JsonString(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) { if (c == L'\\' || c == L'"') { o += L'\\'; o += c; } else if (c == L'\n') o += L"\\n"; else if (c < 0x20) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", unsigned(c)); o += b; } else o += c; }
    return o + L"\"";
}
inline std::wstring TomlString(const std::wstring& s) {
    if (s.find(L'\'') == std::wstring::npos && s.find(L'\n') == std::wstring::npos) return L"'" + s + L"'";
    std::wstring o = L"\""; for (wchar_t c : s) { if (c == L'\\' || c == L'"') o += L'\\'; o += c; } return o + L"\"";
}
inline std::wstring ArgsText(const std::wstring& project) { return project.empty() ? std::wstring() : L"--project " + project; }
inline std::wstring ArgsJson(const std::wstring& project) { return project.empty() ? L"[]" : L"[\"--project\", " + JsonString(project) + L"]"; }
// A complete document (claude_desktop_config.json has the same shape): valid JSON on its own, merged by
// hand into an existing « mcpServers » (T3: the bare `"name": {...}` member was not a valid document).
inline std::wstring GenericConfig(const std::wstring& path, const std::wstring& project, const std::wstring& name = L"browser-annotations") {
    return L"{\n  \"mcpServers\": {\n    " + JsonString(name) + L": {\n      \"command\": " + JsonString(path) + L",\n      \"args\": " + ArgsJson(project) + L"\n    }\n  }\n}";
}
inline std::wstring ClaudeConfig(const std::wstring& path, const std::wstring& project, const std::wstring& name = L"browser-annotations") { return GenericConfig(path, project, name); }
// TOML: bare key when possible (A-Za-z0-9_-), else a quoted key.
inline std::wstring TomlKey(const std::wstring& k) {
    bool bare = !k.empty();
    for (wchar_t c : k) bare = bare && ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_');
    return bare ? k : JsonString(k);
}
inline std::wstring CodexConfig(const std::wstring& path, const std::wstring& project, const std::wstring& name = L"browser-annotations") {
    return L"[mcp_servers." + TomlKey(name) + L"]\ncommand = " + TomlString(path) + L"\nargs = " + ArgsJson(project);
}
inline const wchar_t* AppName(AgentApp a) { return a == AgentApp::Claude ? L"Claude Desktop" : a == AgentApp::Codex ? L"Codex" : L"Votre client MCP"; }
inline const wchar_t* AppShort(AgentApp a) { return a == AgentApp::Claude ? L"Claude" : a == AgentApp::Codex ? L"Codex" : L"votre client"; }

enum ModalHit {
    MClose = 1, MOption = 10, MLimit = 13, MPrimary = 20, MSecondary = 21, MOpenMcpb = 30, MDisclosure = 31,
    MCopyBlock = 32, MCopyCommand = 33, MCopyArgs = 34, MCopyQuote = 35, MHelp = 36, MAgent = 40
};
struct ModalView {
    OnboardingStep step = OnboardingStep::Choose; AgentApp app = AgentApp::Claude; bool limit = true, manual = false, help = false;
    OnboardingSetup setup; VerifyStatus status;
    int focus = 0, copied = 0; bool keyboard = false, reduced = false; float spinner = 0;
    std::map<int, Motion::InteractionVisual> iv;
    Motion::InteractionVisual V(int id) const { auto it = iv.find(id); return it == iv.end() ? Motion::InteractionVisual{} : it->second; }
    int State() const { return step == OnboardingStep::Verify ? (status.read ? 3 : 2) : int(step); }
    std::wstring Project() const { return limit ? setup.projectId : std::wstring(); }
    std::wstring Config() const { const auto p = Project(); return app == AgentApp::Claude ? ClaudeConfig(setup.bridgePath, p, setup.serverName) : app == AgentApp::Codex ? CodexConfig(setup.bridgePath, p, setup.serverName) : GenericConfig(setup.bridgePath, p, setup.serverName); }
    // No .mcpb shipped (V1 preview): Claude gets the manual configuration, open, and no extension button.
    bool NoMcpb() const { return app == AgentApp::Claude && setup.mcpbName.empty(); }
    bool ManualOpen() const { return manual || app == AgentApp::Other || NoMcpb(); }
    bool Disclosable() const { return app != AgentApp::Other && !NoMcpb(); }
    std::wstring Quote() const { return L"« Liste mes annotations" + (setup.projectLabel.empty() ? std::wstring() : L" " + setup.projectLabel) + L". »"; }
    std::wstring Title() const {
        if (step == OnboardingStep::Verify) return status.read ? std::wstring(AppName(app)) + L" est connecté" : L"Vérifier la connexion";
        return L"Connecter un agent";
    }
    std::wstring Subtitle() const {
        if (step == OnboardingStep::Choose) return L"Votre agent lira les annotations depuis cet ordinateur. Aucun compte, aucun serveur à nous.";
        if (step == OnboardingStep::Install) return app == AgentApp::Other ? L"Votre client lance lui-même le connecteur local quand il en a besoin." : std::wstring(AppName(app)) + L" lance lui-même le connecteur local quand il en a besoin.";
        if (status.read) return limit && !setup.projectLabel.empty() ? L"Il lira vos annotations du projet " + setup.projectLabel + L" quand vous le lui demanderez." : L"Il lira vos annotations quand vous le lui demanderez.";
        return std::wstring(L"Demandez à ") + AppShort(app) + L" de lire vos annotations : cette fenêtre se met à jour dès sa première lecture.";
    }
    std::wstring PrimaryLabel() const {
        if (step == OnboardingStep::Choose) return L"Continuer";
        if (step == OnboardingStep::Install) return app == AgentApp::Claude && !NoMcpb() ? L"C'est installé" : L"C'est ajouté";
        return L"Terminé";
    }
    std::wstring SecondaryLabel() const { return step == OnboardingStep::Choose ? L"Annuler" : (step == OnboardingStep::Verify && status.read) ? L"" : L"Retour"; }
    bool PrimaryAccent() const { return !(step == OnboardingStep::Install && app == AgentApp::Claude && !NoMcpb()); }
};
namespace ModalSpec { inline constexpr float width = 560, radius = 12, padX = 20; }
struct ModalLayout {
    float height = 0, headerBottom = 0, footerTop = 0;
    D2D1_RECT_F title{}, subtitle{}, close{}, steps{}, options[3]{}, limit{}, limitLabel{}, card{}, cardTitle{}, cardText{}, openMcpb{}, fileLabel{};
    D2D1_RECT_F rowLabel[2]{}, rowValue[2]{}, rowCopy[2]{}, disclosure{}, disclosurePath{}, block{}, blockText{}, blockCopy{};
    D2D1_RECT_F prompt{}, quote{}, quoteCopy{}, status{}, help{}, agentsTop{}, agentRows[4]{}, agentAction[4]{}; int agents = 0;
    D2D1_RECT_F note{}, noteIcon{}, primary{}, secondary{};
};
inline float CopyWidth(Ink& ink, bool icon, bool copied) { return 10 + (icon ? 14 + 6 : 0) + ink.Width(copied ? L"Copié" : L"Copier", F(12, 16, 500)) + 10; }
inline ModalLayout LayoutModal(Ink& ink, const ModalView& v) {
    using namespace ModalSpec; ModalLayout l; const float x0 = 1 + padX, x1 = width - 1 - padX, inner = x1 - x0;
    float y = 1 + 20;
    const float textW = inner - 28 - 12;
    const float th = ink.Height(v.Title(), F(20, 26, 600, -.015f), textW), sh = ink.Height(v.Subtitle(), F(14, 20), textW);
    l.title = {x0, y, x0 + textW, y + th}; l.subtitle = {x0, y + th + 4, x0 + textW, y + th + 4 + sh}; l.close = {x1 - 28, y, x1, y + 28};
    y += std::max(28.f, th + 4 + sh) + 14; l.steps = {x0, y, x1, y + 18}; y += 18 + 16; l.headerBottom = y;
    y += 4;
    if (v.step == OnboardingStep::Choose) {
        for (int i = 0; i < 3; ++i) { l.options[i] = {x0, y, x1, y + 64}; y += 64 + (i < 2 ? 8 : 0); }
        y += 8 + 8; l.limit = {x0 + 2, y + 1, x0 + 18, y + 17}; l.limitLabel = {x0 + 28, y, x1, y + 18}; y += 18 + 16;
    } else if (v.step == OnboardingStep::Install) {
        const float cw = inner - 32; float cy = y + 16;
        const std::wstring title = v.NoMcpb() ? L"Ajouter dans Claude Desktop" : v.app == AgentApp::Claude ? L"Installer l'extension" : v.app == AgentApp::Codex ? L"Ajouter dans l'app Codex" : L"Ajouter dans votre client MCP";
        const std::wstring text = v.NoMcpb() ? L"L'extension en un clic arrive bientôt. Pour l'instant : Réglages › Développeur › Modifier la config, ajoutez ce bloc (fusionnez « mcpServers » s'il existe déjà), puis redémarrez Claude Desktop."
            : v.app == AgentApp::Claude ? L"Claude Desktop s'ouvre et affiche « Annotations Browser ». Cliquez sur Installer, puis revenez ici."
            : v.app == AgentApp::Codex ? L"Réglages › Serveurs MCP › Ajouter un serveur › STDIO, puis collez ces deux valeurs et redémarrez Codex."
            : L"Déclarez un serveur stdio avec cette commande et ces arguments, puis redémarrez le client.";
        (void)title;
        const float ch = ink.Height(text, F(13, 19), cw);
        l.cardTitle = {x0 + 16, cy, x0 + 16 + cw, cy + 20}; l.cardText = {x0 + 16, cy + 24, x0 + 16 + cw, cy + 24 + ch}; cy += 24 + ch + 12;
        if (v.NoMcpb()) { cy -= 12; }
        else if (v.app == AgentApp::Claude) {
            const float bw = 14 + 16 + 8 + ink.Width(L"Ouvrir dans Claude Desktop", F(14, 18, 500)) + 14;
            l.openMcpb = {x0 + 16, cy, x0 + 16 + bw, cy + 32}; l.fileLabel = {l.openMcpb.right + 12, cy, x0 + 16 + cw, cy + 32}; cy += 32;
        } else {
            for (int i = 0; i < 2; ++i) {
                const float cwid = CopyWidth(ink, false, v.copied == MCopyCommand + i);
                l.rowLabel[i] = {x0 + 16, cy, x0 + 16 + 76, cy + 28}; l.rowCopy[i] = {x0 + 16 + cw - cwid, cy, x0 + 16 + cw, cy + 28};
                l.rowValue[i] = {l.rowLabel[i].right + 10, cy, l.rowCopy[i].left - 10, cy + 28}; cy += 28 + (i == 0 ? 12 : 0);
            }
        }
        l.card = {x0, y, x1, cy + 16}; y = l.card.bottom + 12 + 2;
        l.disclosure = {x0 + 2, y, x1 - 2, y + 18}; y += 18;
        if (v.ManualOpen()) {
            y += 10; const float cwid = CopyWidth(ink, true, v.copied == MCopyBlock);
            const float tw = inner - 4 - 24 - 12 - cwid; const float bh = ink.Height(v.Config(), Mono(12, 18, true), tw);
            l.block = {x0 + 2, y, x1 - 2, y + 24 + bh}; l.blockText = {x0 + 14, y + 12, x0 + 14 + tw, y + 12 + bh}; l.blockCopy = {l.block.right - 12 - cwid, y + 12, l.block.right - 12, y + 40};
            y = l.block.bottom;
        }
        y += 18;
    } else {
        const float cw = inner - 32; float cy = y + 16;
        l.prompt = {x0 + 16, cy, x0 + 16 + cw, cy + 18}; cy += 18 + 10;
        const float qc = CopyWidth(ink, true, v.copied == MCopyQuote);
        l.quoteCopy = {x0 + 16 + cw - qc, cy, x0 + 16 + cw, cy + 28}; l.quote = {x0 + 16, cy, l.quoteCopy.left - 12, cy + 28}; cy += 28 + 10;
        l.status = {x0 + 16, cy, x0 + 16 + cw, cy + 40};
        if (!v.status.read && v.help) { const float hw = ink.Width(L"Ça ne marche pas ?", F(13, 18, 500)); l.help = {l.status.right - 12 - hw, cy + 11, l.status.right - 12, cy + 29}; }
        cy += 40; l.card = {x0, y, x1, cy + 16}; y = l.card.bottom;
        if (!v.status.agents.empty()) {
            y += 12; l.agentsTop = {x0, y, x1, y + 1}; y += 1 + 4 + 8 + 16 + 4;
            l.agents = int(std::min<size_t>(4, v.status.agents.size()));
            for (int i = 0; i < l.agents; ++i) {
                y += 2; l.agentRows[i] = {x0 + 4, y, x1 - 4, y + 48};
                const float aw = 20 + ink.Width(v.status.agents[size_t(i)].connected ? L"Gérer" : L"Connecter", F(12, 16, 500));
                l.agentAction[i] = {l.agentRows[i].right - aw, y + 10, l.agentRows[i].right, y + 38}; y += 48;
            }
        }
        y += 18;
    }
    l.footerTop = y; y += 1 + 14;
    float bx = x1; const Font bf = F(14, 18, 500);
    const float pw = 14 + ink.Width(v.PrimaryLabel(), bf) + 6 + KeycapWidth(ink, L"Entrée") + 14;
    l.primary = {bx - pw, 0, bx, 32}; bx -= pw + 8;
    if (!v.SecondaryLabel().empty()) { const float sw = 14 + ink.Width(v.SecondaryLabel(), bf) + 14; l.secondary = {bx - sw, 0, bx, 32}; bx -= sw; } else bx += 8;
    float rowH = 32;
    if (v.step != OnboardingStep::Verify) {
        const float nw = (bx - 16) - (x0 + 24); const float nh = ink.Height(L"Tout reste local, sans port réseau. Ce que l'agent lit peut ensuite être envoyé à son fournisseur de modèle.", F(12, 17), nw);
        rowH = std::max(32.f, nh); l.noteIcon = {x0, y + (rowH - nh) / 2 + 1, x0 + 16, y + (rowH - nh) / 2 + 17}; l.note = {x0 + 24, y + (rowH - nh) / 2, x0 + 24 + nw, y + (rowH - nh) / 2 + nh};
    }
    const float by = y + (rowH - 32) / 2;
    l.primary.top = by; l.primary.bottom = by + 32; if (l.secondary.right > 0) { l.secondary.top = by; l.secondary.bottom = by + 32; }
    y += rowH + 16 + 1; l.height = y; return l;
}
inline std::vector<int> ModalOrder(const ModalView& v, const ModalLayout& l) {
    std::vector<int> o;
    if (v.step == OnboardingStep::Choose) { o = {MOption + int(v.app), MLimit}; }
    else if (v.step == OnboardingStep::Install) {
        if (v.NoMcpb()) {} else if (v.app == AgentApp::Claude) o.push_back(MOpenMcpb); else { o.push_back(MCopyCommand); if (!v.Project().empty()) o.push_back(MCopyArgs); }
        if (v.Disclosable()) o.push_back(MDisclosure);
        if (v.ManualOpen()) o.push_back(MCopyBlock);
    } else {
        o.push_back(MCopyQuote); if (l.help.right > 0) o.push_back(MHelp);
        for (int i = 0; i < l.agents; ++i) o.push_back(MAgent + i);
    }
    if (!v.SecondaryLabel().empty()) o.push_back(MSecondary);
    o.push_back(MPrimary); o.push_back(MClose); return o;
}
inline int ModalHitAt(const ModalView& v, const ModalLayout& l, float x, float y) {
    if (Inside(l.close, x, y)) return MClose;
    if (Inside(l.primary, x, y)) return MPrimary;
    if (l.secondary.right > 0 && Inside(l.secondary, x, y)) return MSecondary;
    if (v.step == OnboardingStep::Choose) { for (int i = 0; i < 3; ++i) if (Inside(l.options[i], x, y)) return MOption + i; if (Inside(Inflate(l.limit, 4), x, y) || Inside(l.limitLabel, x, y)) return MLimit; }
    if (v.step == OnboardingStep::Install) {
        if (l.openMcpb.right > 0 && Inside(l.openMcpb, x, y)) return MOpenMcpb;
        for (int i = 0; i < 2; ++i) if (l.rowCopy[i].right > 0 && Inside(l.rowCopy[i], x, y) && !(i == 1 && v.Project().empty())) return MCopyCommand + i;
        if (v.Disclosable() && Inside(l.disclosure, x, y)) return MDisclosure;
        if (l.blockCopy.right > 0 && Inside(l.blockCopy, x, y)) return MCopyBlock;
    }
    if (v.step == OnboardingStep::Verify) {
        if (Inside(l.quoteCopy, x, y)) return MCopyQuote;
        if (l.help.right > 0 && Inside(Inflate(l.help, 4), x, y)) return MHelp;
        for (int i = 0; i < l.agents; ++i) if (Inside(l.agentAction[i], x, y)) return MAgent + i;
    }
    return 0;
}
inline void StepDot(Ink& ink, float& x, float cy, int number, int state /*0 todo, 1 active, 2 done, 3 active done*/, const std::wstring& label) {
    const D2D1_RECT_F c{x, cy - 9, x + 18, cy + 9};
    if (state == 1 || state == 3) ink.Fill(c, 9, Accent()); else if (state == 2) ink.Fill(c, 9, Hex(0xFFFFFF29)); else ink.Fill(c, 9, Hex(0xFFFFFF1A));
    if (state >= 2) ink.Icon(Glyphs::check, {x + 3.5f, cy - 5.5f, x + 14.5f, cy + 5.5f}, {1, 1, 1, 1});
    else ink.Text(std::to_wstring(number), F(11, 16, 600), c, state == 1 ? D2D1_COLOR_F{1, 1, 1, 1} : Hex(0xFFFFFF99), false, Align::Center, true);
    x += 18 + 6; const Font f = F(12, 16, state == 1 || state == 3 ? 500 : 400); const float w = ink.Width(label, f);
    ink.Text(label, f, {x, cy - 8, x + w + 1, cy + 8}, state == 1 || state == 3 ? D2D1_COLOR_F{1, 1, 1, 1} : state == 2 ? Hex(0xFFFFFFB3) : Hex(0xFFFFFF99));
    x += w;
}
inline void CopyButton(Ink& ink, D2D1_RECT_F r, bool icon, bool copied, const ModalView& v, int id) {
    const auto iv = v.V(id); ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, v.reduced));
    ink.Fill(r, 6, Hex(Ink_::hover)); ink.Fill(r, 6, {1, 1, 1, .05f * iv.hover + .04f * iv.press});
    float x = r.left + 10; const float cy = (r.top + r.bottom) / 2;
    if (icon) { ink.Icon(copied ? Glyphs::check : Glyphs::copy, {x, cy - 7, x + 14, cy + 7}, {1, 1, 1, 1}); x += 20; }
    ink.Text(copied ? L"Copié" : L"Copier", F(12, 16, 500), {x, r.top, r.right, r.bottom}, {1, 1, 1, 1}, false, Align::Leading, true);
    if (v.focus == id) FocusRing(ink, r, 6, v.keyboard);
}
inline void PlainButton(Ink& ink, D2D1_RECT_F r, const std::wstring& label, D2D1_COLOR_F fill, const ModalView& v, int id, const Font& f) {
    const auto iv = v.V(id); ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, v.reduced));
    ink.Fill(r, 6, fill); ink.Fill(r, 6, {1, 1, 1, .05f * iv.hover + .04f * iv.press});
    ink.Text(label, f, r, {1, 1, 1, 1}, false, Align::Center, true);
    if (v.focus == id) FocusRing(ink, r, 6, v.keyboard);
}
inline void PaintModalContent(Ink& ink, const ModalView& v, const ModalLayout& l) {
    using namespace ModalSpec; const float x0 = 1 + padX, x1 = width - 1 - padX;
    ink.Text(v.Title(), F(20, 26, 600, -.015f), l.title, {1, 1, 1, 1}, true);
    ink.Text(v.Subtitle(), F(14, 20), l.subtitle, Hex(0xFFFFFFB3), true);
    {   const auto iv = v.V(MClose); ScaleScope s(ink.Target(), l.close, Motion::PressScale(iv.press, v.reduced));
        if (iv.hover > 0) ink.Fill(l.close, 6, {1, 1, 1, .08f * iv.hover});
        const float cx = (l.close.left + l.close.right) / 2, cy = (l.close.top + l.close.bottom) / 2;
        ink.Icon(Glyphs::close, {cx - 8, cy - 8, cx + 8, cy + 8}, {1, 1, 1, .7f + .3f * iv.hover});
        if (v.focus == MClose) FocusRing(ink, l.close, 6, v.keyboard); }
    {   float x = l.steps.left; const float cy = (l.steps.top + l.steps.bottom) / 2; const int st = v.State();
        auto connector = [&] { x += 8; ink.Fill({x, cy - .5f, x + 24, cy + .5f}, 0, Hex(0xFFFFFF29)); x += 24 + 8; };
        StepDot(ink, x, cy, 1, st == 0 ? 1 : 2, st == 0 ? std::wstring(L"Choisir l'app") : AppName(v.app)); connector();
        StepDot(ink, x, cy, 2, st == 1 ? 1 : st > 1 ? 2 : 0, st > 1 ? L"Installé" : L"Installer"); connector();
        StepDot(ink, x, cy, 3, st == 2 ? 1 : st == 3 ? 3 : 0, st == 3 ? L"Vérifié" : L"Vérifier"); }
    if (v.step == OnboardingStep::Choose) {
        static const wchar_t* names[3] = {L"Claude Desktop", L"Codex", L"Autre client MCP"};
        static const wchar_t* descs[3] = {L"Extension en un clic, rien à configurer", L"App de bureau, CLI ou extension d'éditeur : une seule configuration", L"Commande locale à ajouter dans sa configuration"};
        for (int i = 0; i < 3; ++i) {
            const auto& r = l.options[i]; const bool chosen = int(v.app) == i; const auto iv = v.V(MOption + i);
            ink.Fill(r, 8, chosen ? Accent(26 / 255.f) : Hex(0xFFFFFF08)); if (!chosen && iv.hover > 0) ink.Fill(r, 8, {1, 1, 1, .03f * iv.hover});
            ink.Stroke(r, 8, chosen ? Accent(.6f) : Hex(0xFFFFFF14), 1);
            const D2D1_RECT_F tile{r.left + 12, r.top + 14, r.left + 48, r.top + 50};
            ink.Fill(tile, 8, Hex(0xFFFFFF14));
            if (i == 0) ink.Text(L"C", F(14, 18, 600), tile, {1, 1, 1, 1}, false, Align::Center, true);
            else if (i == 1) ink.Text(L"Cx", F(13, 16, 600), tile, {1, 1, 1, 1}, false, Align::Center, true);
            else { const float cx = (tile.left + tile.right) / 2, cy = (tile.top + tile.bottom) / 2; ink.Icon(Glyphs::code, {cx - 9, cy - 9, cx + 9, cy + 9}, {1, 1, 1, .8f}); }
            const float tx = tile.right + 12, ty = r.top + 12; const Font nf = F(14, 20, 500);
            ink.Text(names[i], nf, {tx, ty, r.right - 42, ty + 20}, {1, 1, 1, 1});
            if (i == 0) { const float bx = tx + ink.Width(names[i], nf) + 8, bw = ink.Width(L"Recommandé", F(11, 14, 500)) + 12;
                ink.Fill({bx, ty + 1, bx + bw, ty + 19}, 4, Hex(0xFFFFFF14)); ink.Text(L"Recommandé", F(11, 14, 500), {bx + 6, ty + 1, bx + bw, ty + 19}, Hex(0xFFFFFFE6), false, Align::Leading, true); }
            const wchar_t* desc = (i == 0 && v.setup.mcpbName.empty()) ? L"Configuration à ajouter à la main (extension bientôt)" : descs[i];   // honest without a .mcpb (T3)
            ink.Text(desc, F(13, 18), {tx, ty + 22, r.right - 42, ty + 40}, Hex(0xFFFFFF99));
            const float rx = r.right - 12 - 18, ry = (r.top + r.bottom) / 2 - 9;
            if (chosen) ink.Ring(rx + 9, ry + 9, 9, Accent(), 5); else ink.Ring(rx + 9, ry + 9, 9, Hex(0xFFFFFF66), 1.5f);
            if (v.focus == MOption + i) FocusRing(ink, r, 8, v.keyboard);
        }
        { ScaleScope s(ink.Target(), l.limit, Motion::PressScale(v.V(MLimit).press, v.reduced)); Checkbox(ink, l.limit, v.limit, true); }
        if (v.focus == MLimit) FocusRing(ink, l.limit, 3, v.keyboard);
        ink.Spans({{L"Limiter cet agent au projet ", Hex(0xFFFFFFE6), 400}, {v.setup.projectLabel, {1, 1, 1, 1}, 500}}, F(13, 18), l.limitLabel);
    } else if (v.step == OnboardingStep::Install) {
        ink.Fill(l.card, 8, Hex(0xFFFFFF08)); ink.Stroke(l.card, 8, Hex(0xFFFFFF14), 1);
        const std::wstring title = v.NoMcpb() ? L"Ajouter dans Claude Desktop" : v.app == AgentApp::Claude ? L"Installer l'extension" : v.app == AgentApp::Codex ? L"Ajouter dans l'app Codex" : L"Ajouter dans votre client MCP";
        const std::wstring text = v.NoMcpb() ? L"L'extension en un clic arrive bientôt. Pour l'instant : Réglages › Développeur › Modifier la config, ajoutez ce bloc (fusionnez « mcpServers » s'il existe déjà), puis redémarrez Claude Desktop."
            : v.app == AgentApp::Claude ? L"Claude Desktop s'ouvre et affiche « Annotations Browser ». Cliquez sur Installer, puis revenez ici."
            : v.app == AgentApp::Codex ? L"Réglages › Serveurs MCP › Ajouter un serveur › STDIO, puis collez ces deux valeurs et redémarrez Codex."
            : L"Déclarez un serveur stdio avec cette commande et ces arguments, puis redémarrez le client.";
        ink.Text(title, F(14, 20, 500), l.cardTitle, {1, 1, 1, 1}); ink.Text(text, F(13, 19), l.cardText, Hex(0xFFFFFFB3), true);
        if (v.NoMcpb()) {}
        else if (v.app == AgentApp::Claude) {
            const auto iv = v.V(MOpenMcpb);
            { ScaleScope s(ink.Target(), l.openMcpb, Motion::PressScale(iv.press, v.reduced));
              ink.Fill(l.openMcpb, 6, Accent()); ink.Fill(l.openMcpb, 6, {1, 1, 1, .08f * iv.hover + .06f * iv.press});
              const float cy = (l.openMcpb.top + l.openMcpb.bottom) / 2;
              ink.Icon(Glyphs::openOut, {l.openMcpb.left + 14, cy - 8, l.openMcpb.left + 30, cy + 8}, {1, 1, 1, 1});
              ink.Text(L"Ouvrir dans Claude Desktop", F(14, 18, 500), {l.openMcpb.left + 38, l.openMcpb.top, l.openMcpb.right, l.openMcpb.bottom}, {1, 1, 1, 1}, false, Align::Leading, true);
              if (v.focus == MOpenMcpb) FocusRing(ink, l.openMcpb, 6, v.keyboard); }
            ink.Text(v.setup.mcpbName + L" · " + v.setup.mcpbSize, F(12, 16), l.fileLabel, Hex(0xFFFFFF99), false, Align::Leading, true);
        } else {
            const std::wstring values[2] = {v.setup.bridgePath, ArgsText(v.Project())};
            for (int i = 0; i < 2; ++i) {
                ink.Text(i == 0 ? L"Commande" : L"Arguments", F(12, 16), l.rowLabel[i], Hex(0xFFFFFF99), false, Align::Leading, true);
                ink.Fill(l.rowValue[i], 6, Hex(Ink_::canvas)); ink.Stroke(l.rowValue[i], 6, Hex(0xFFFFFF0F), 1);
                const Font mf = Mono(12, 16, true); const D2D1_RECT_F tr{l.rowValue[i].left + 10, l.rowValue[i].top, l.rowValue[i].right - 10, l.rowValue[i].bottom};
                const std::wstring shown = values[i].empty() ? L"(aucun)" : ink.Middle(values[i], mf, tr.right - tr.left);
                ink.Text(shown, mf, tr, values[i].empty() ? Hex(0xFFFFFF66) : Hex(0xFFFFFFCC), false, Align::Leading, true);
                if (!(i == 1 && values[1].empty())) {
                    const auto iv = v.V(MCopyCommand + i); const auto& r = l.rowCopy[i];
                    ScaleScope s(ink.Target(), r, Motion::PressScale(iv.press, v.reduced));
                    ink.Fill(r, 6, Hex(Ink_::hover)); ink.Fill(r, 6, {1, 1, 1, .05f * iv.hover});
                    ink.Text(v.copied == MCopyCommand + i ? L"Copié" : L"Copier", F(12, 16, 500), r, {1, 1, 1, 1}, false, Align::Center, true);
                    if (v.focus == MCopyCommand + i) FocusRing(ink, r, 6, v.keyboard);
                }
            }
        }
        {   const bool open = v.ManualOpen(); const float cy = (l.disclosure.top + l.disclosure.bottom) / 2; float x = l.disclosure.left;
            if (v.Disclosable()) { ink.Icon(Glyphs::caretDown, {x, cy - 7, x + 14, cy + 7}, {1, 1, 1, .7f}, open ? 0.f : -90.f); x += 20; }
            const std::wstring label = v.NoMcpb() ? L"Configuration JSON" : v.app == AgentApp::Claude ? L"Ou ajouter manuellement" : v.app == AgentApp::Codex ? L"Ou modifier config.toml" : L"Configuration JSON";
            const std::wstring path = v.app == AgentApp::Claude ? L"Claude › Réglages › Développeur › Modifier la config" : v.app == AgentApp::Codex ? L"~/.codex/config.toml" : L"mcpServers";
            const Font lf = F(13, 18, 500); const float lw = ink.Width(label, lf);
            ink.Text(label, lf, {x, l.disclosure.top, x + lw + 1, l.disclosure.bottom}, Hex(0xFFFFFFE6)); x += lw + 6;
            ink.Text(path, F(12, 16), {x, cy - 8, l.disclosure.right, cy + 8}, Hex(0xFFFFFF99));
            if (v.focus == MDisclosure) FocusRing(ink, l.disclosure, 4, v.keyboard);
            if (open) {
                ink.Fill(l.block, 8, Hex(Ink_::canvas)); ink.Stroke(l.block, 8, Hex(0xFFFFFF0F), 1);
                ink.Text(v.Config(), Mono(12, 18, true), l.blockText, Hex(0xFFFFFFCC), true);
                CopyButton(ink, l.blockCopy, true, v.copied == MCopyBlock, v, MCopyBlock);
            } }
    } else {
        ink.Fill(l.card, 8, Hex(0xFFFFFF08)); ink.Stroke(l.card, 8, Hex(0xFFFFFF14), 1);
        ink.Text(std::wstring(L"Pour vérifier, demandez dans ") + AppShort(v.app) + L" :", F(13, 18), l.prompt, Hex(0xFFFFFFB3));
        ink.Text(v.Quote(), F(16, 22, 500), l.quote, {1, 1, 1, 1}, false, Align::Leading, true);
        CopyButton(ink, l.quoteCopy, true, v.copied == MCopyQuote, v, MCopyQuote);
        const float cy = (l.status.top + l.status.bottom) / 2;
        if (v.status.read) {
            ink.Fill(l.status, 6, Accent(31 / 255.f));
            ink.Disc(l.status.left + 22, cy, 10, Accent()); ink.Icon(Glyphs::check, {l.status.left + 16, cy - 6, l.status.left + 28, cy + 6}, {1, 1, 1, 1});
            std::wstring tail = L" · " + v.status.when; if (!v.status.detail.empty()) tail += L" · " + v.status.detail;
            ink.Spans({{(v.status.reader.empty() ? std::wstring(AppShort(v.app)) : v.status.reader) + L" a lu vos annotations", {1, 1, 1, 1}, 500}, {tail, Hex(0xFFFFFFB3), 400}}, F(13, 18), {l.status.left + 42, l.status.top, l.status.right - 12, l.status.bottom});
        } else {
            ink.Fill(l.status, 6, Hex(0xFFFFFF0A));
            Spinner(ink, {l.status.left + 12, cy - 10, l.status.left + 32, cy + 10}, v.spinner, Accent());
            ink.Text(L"En attente de la première lecture…", F(13, 18), {l.status.left + 42, l.status.top, l.help.right > 0 ? l.help.left - 8 : l.status.right - 12, l.status.bottom}, Hex(0xFFFFFFB3), false, Align::Leading, true);
            if (l.help.right > 0) {
                const auto iv = v.V(MHelp);
                ink.Text(L"Ça ne marche pas ?", F(13, 18, 500), l.help, Hex(0x9DB3FFFF), false, Align::Leading, true);
                ink.Fill({l.help.left, l.help.bottom - 1, l.help.right, l.help.bottom}, 0, WithAlpha(Hex(0x9DB3FFFF), .5f + .5f * iv.hover));
                if (v.focus == MHelp) FocusRing(ink, l.help, 3, v.keyboard);
            }
        }
        if (l.agents) {
            ink.Fill(l.agentsTop, 0, Hex(0xFFFFFF0F));
            ink.Text(L"Agents", F(12, 16, 500), {x0 + 4, l.agentsTop.bottom + 4 + 8, x1, l.agentsTop.bottom + 28}, Hex(0xFFFFFF99));
            for (int i = 0; i < l.agents; ++i) {
                const auto& a = v.status.agents[size_t(i)]; const auto& r = l.agentRows[i]; const float cy2 = (r.top + r.bottom) / 2;
                const D2D1_RECT_F tile{r.left, cy2 - 14, r.left + 28, cy2 + 14}; ink.Fill(tile, 6, Hex(0xFFFFFF14));
                ink.Text(a.monogram, a.monogram.size() > 1 ? F(11, 14, 600) : F(12, 16, 600), tile, {1, 1, 1, 1}, false, Align::Center, true);
                ink.Text(a.name, F(14, 20, 500), {tile.right + 12, cy2 - 18, l.agentAction[i].left - 12, cy2 + 2}, {1, 1, 1, 1});
                ink.Text(a.detail, F(12, 16), {tile.right + 12, cy2 + 2, l.agentAction[i].left - 12, cy2 + 18}, Hex(0xFFFFFF99));
                PlainButton(ink, l.agentAction[i], a.connected ? L"Gérer" : L"Connecter", Hex(0xFFFFFF0F), v, MAgent + i, F(12, 16, 500));
            }
        }
    }
    // Footer.
    ink.Fill({1, l.footerTop, width - 1, l.height - 1}, 0, Hex(0xFFFFFF05));
    ink.Fill({1, l.footerTop, width - 1, l.footerTop + 1}, 0, Hex(0xFFFFFF0F));
    if (l.note.right > 0) {
        ink.Icon(Glyphs::lock, l.noteIcon, {1, 1, 1, .7f});
        ink.Text(L"Tout reste local, sans port réseau. Ce que l'agent lit peut ensuite être envoyé à son fournisseur de modèle.", F(12, 17), l.note, Hex(0xFFFFFF99), true);
    }
    if (l.secondary.right > 0) PlainButton(ink, l.secondary, v.SecondaryLabel(), Hex(Ink_::hover), v, MSecondary, F(14, 18, 500));
    KeyButton(ink, l.primary, v.PrimaryLabel(), L"Entrée", v.PrimaryAccent() ? Accent() : Hex(Ink_::hover), Hex(0x00000038), {1, 1, 1, 1}, v.V(MPrimary), true, v.focus == MPrimary, v.keyboard, v.reduced);
}
// The whole modal at height h (animated), with the outgoing view cross-fading out.
inline void PaintModal(Ink& ink, float h, const ModalView& now, const ModalLayout& nowLayout, float inAlpha, const ModalView* before, const ModalLayout* beforeLayout, float outAlpha) {
    using namespace ModalSpec; const D2D1_RECT_F all{0, 0, width, h}; auto* t = ink.Target();
    ink.Fill(all, radius, Hex(Ink_::raised));
    ID2D1RoundedRectangleGeometry* clip = nullptr; ID2D1Layer* layer = nullptr;
    const bool clipped = SUCCEEDED(ink.Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(all, radius, radius), &clip)) && SUCCEEDED(t->CreateLayer(nullptr, &layer));
    if (clipped) t->PushLayer(D2D1::LayerParameters(all, clip), layer);
    auto draw = [&](const ModalView& v, const ModalLayout& l, float a) {
        if (a <= .001f) return; ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(a, g); PaintModalContent(ink, v, l); if (pushed) ink.PopOpacity(g);
    };
    if (before && beforeLayout) draw(*before, *beforeLayout, outAlpha);
    draw(now, nowLayout, inAlpha);
    if (clipped) t->PopLayer(); Release(layer); Release(clip);
    ink.Stroke(all, radius, Hex(0xFFFFFF14), 1);
}

struct OnboardingHooks {
    std::function<void()> openMcpb;                                  // « Ouvrir dans Claude Desktop »: the host opens the .mcpb
    std::function<void(const std::wstring& text)> copy;              // the host puts text on the clipboard
    std::function<void(AgentApp)> verifyStarted, verifyStopped;      // watch readers\ only between these two
    std::function<void(AgentApp)> help;                              // « Ça ne marche pas ? »
    std::function<void(int agent)> agentAction;                      // « Gérer » / « Connecter » on the agents list
    std::function<void(bool completed)> closed;                      // focus goes back to the host
    std::function<void(const std::wstring&)> announce;
};
class Onboarding : public Animator::Client {
    static constexpr long long DialogKey = 1, StepsKey = 2, StatusKey = 3, BlockKey = 4, OptionsKey = 5;
    static constexpr UINT_PTR HelpTimer = 9;
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_; OnboardingHooks hooks_;
    Layer modal_; HWND veil_ = nullptr;
    ModalView view_, before_; ModalLayout layout_, beforeLayout_; bool hasBefore_ = false;
    Presence presence_; Crossfade fade_; Motion::Spring height_; Motion::State motion_;
    RECT client_{}; int top_ = 0; bool open_ = false, reduced_ = false; int hover_ = 0, pressed_ = 0; bool verifying_ = false;

    Ink& I() { return *Ink::Instance(); }
    float Scale() const { return modal_.Scale(); }
    int Px(float d) const { return int(std::lround(d * Scale())); }
    void Relayout() { layout_ = LayoutModal(I(), view_); }
    RECT ModalRect(Motion::Time now) const {
        const float h = std::max(1.f, height_.Value(now)); const int w = Px(ModalSpec::width), hp = Px(h);
        const int x = (client_.left + client_.right - w) / 2; int y = top_;
        if (y + hp > client_.bottom - Px(16)) y = std::max<int>(client_.top + Px(16), client_.bottom - Px(16) - hp);
        return {x, y, x + w, y + hp};
    }
    ModalView Visual(Motion::Time now) const {
        ModalView v = view_; v.reduced = reduced_; v.spinner = SpinnerAngle(now, reduced_);
        for (int id : {int(MClose), int(MOption), int(MOption) + 1, int(MOption) + 2, int(MLimit), int(MPrimary), int(MSecondary), int(MOpenMcpb), int(MDisclosure), int(MCopyBlock), int(MCopyCommand), int(MCopyArgs), int(MCopyQuote), int(MHelp), int(MAgent), int(MAgent) + 1, int(MAgent) + 2, int(MAgent) + 3})
            v.iv[id] = motion_.Visual(id, now);
        return v;
    }
    void Render(Motion::Time now) {
        if (!modal_.Window()) return;
        const Pose p = presence_.Sample(now, reduced_);
        const float h = std::max(1.f, height_.Value(now));
        const ModalView v = Visual(now); const ModalLayout l = layout_;
        const bool fading = hasBefore_ && fade_.Moving(now); const ModalView b = before_; const ModalLayout bl = beforeLayout_;
        const float in = fading ? fade_.Incoming(now) : 1.f, out = fading ? fade_.Outgoing(now) : 0.f;
        modal_.paint = [=](Ink& ink, float w, float hh) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, {w / 2, hh / 2}) * base);
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(p.opacity, group);
            PaintModal(ink, h, v, l, in, fading ? &b : nullptr, fading ? &bl : nullptr, out);
            if (pushed) ink.PopOpacity(group); t->SetTransform(base);
        };
        modal_.Present(ModalRect(now), true);
        if (veil_) {
            SetLayeredWindowAttributes(veil_, 0, BYTE(std::lround(255 * Spec::veilOpacity * presence_.Progress(now))), LWA_ALPHA);
            SetWindowPos(veil_, modal_.Window(), client_.left, client_.top, client_.right - client_.left, client_.bottom - client_.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    void Announce(const std::wstring& text) { if (hooks_.announce) hooks_.announce(text); else modal_.uia.Notify(DialogKey, text.c_str(), L"AnnotationOnboarding"); }
    // Moves to a new view (step, app, verify state): cross-fade + height spring.
    void Change(const std::function<void()>& mutate, bool focusFirst = true) {
        const auto now = Motion::Now(); const int stateBefore = view_.State();
        before_ = Visual(now); beforeLayout_ = layout_;
        mutate(); view_.copied = 0; Relayout();
        if (view_.State() != stateBefore) { hasBefore_ = true; fade_.Go(view_.State(), now); }
        height_.Set(layout_.height, now, Spec::stepHeightResponse, reduced_);
        if (focusFirst && view_.State() != stateBefore) { const auto o = ModalOrder(view_, layout_); view_.focus = o.front(); }
        Render(now); Animator::Get().Request(this);
        if (modal_.uia.Listening()) modal_.uia.ChildrenChanged(DialogKey);
    }
    void SetStep(OnboardingStep step) {
        const OnboardingStep was = view_.step;
        Change([&] { view_.step = step; view_.help = false; });
        if (step == OnboardingStep::Verify && was != OnboardingStep::Verify) {
            verifying_ = true; if (hooks_.verifyStarted) hooks_.verifyStarted(view_.app);
            SetTimer(modal_.Window(), HelpTimer, Spec::verifyHelpMs, nullptr);
        } else if (was == OnboardingStep::Verify && step != OnboardingStep::Verify) StopVerify();
        static const wchar_t* names[3] = {L"Choisir l'app", L"Installer", L"Vérifier"};
        Announce(std::wstring(L"Étape ") + std::to_wstring(int(step) + 1) + L" sur 3 : " + names[int(step)]);
    }
    void StopVerify() { if (!verifying_) return; verifying_ = false; if (modal_.Window()) KillTimer(modal_.Window(), HelpTimer); if (hooks_.verifyStopped) hooks_.verifyStopped(view_.app); }
    void Copy(int id, const std::wstring& text) {
        if (hooks_.copy) hooks_.copy(text);
        view_.copied = id; Relayout(); height_.Set(layout_.height, Motion::Now(), Spec::stepHeightResponse, reduced_); Render(Motion::Now()); Announce(L"Copié");
    }
    void Activate(int hit, bool keyboard) {
        view_.keyboard = keyboard;
        if (hit >= MOption && hit < MOption + 3) { view_.app = AgentApp(hit - MOption); view_.focus = keyboard ? hit : view_.focus; Relayout(); Render(Motion::Now()); return; }
        if (hit >= MAgent && hit < MAgent + 4) { if (hooks_.agentAction) hooks_.agentAction(hit - MAgent); return; }
        switch (hit) {
        case MClose: Close(true, false); break;
        case MLimit: view_.limit = !view_.limit; Relayout(); Render(Motion::Now()); Announce(view_.limit ? L"Limité au projet" : L"Tous les projets"); break;
        case MPrimary:
            if (view_.step == OnboardingStep::Choose) SetStep(OnboardingStep::Install);
            else if (view_.step == OnboardingStep::Install) SetStep(OnboardingStep::Verify);
            else Close(true, view_.status.read);
            break;
        case MSecondary:
            if (view_.step == OnboardingStep::Choose) Close(true, false);
            else SetStep(OnboardingStep(int(view_.step) - 1));
            break;
        case MOpenMcpb: if (hooks_.openMcpb) hooks_.openMcpb(); break;
        case MDisclosure: Change([&] { view_.manual = !view_.manual; }, false); break;
        case MCopyBlock: Copy(MCopyBlock, view_.Config()); break;
        case MCopyCommand: Copy(MCopyCommand, view_.setup.bridgePath); break;
        case MCopyArgs: Copy(MCopyArgs, ArgsText(view_.Project())); break;
        case MCopyQuote: Copy(MCopyQuote, view_.Quote().substr(2, view_.Quote().size() - 4)); break;
        case MHelp: if (hooks_.help) hooks_.help(view_.app); break;
        }
    }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Render(now); Animator::Get().Request(this); }
    void Cycle(bool back) {
        const auto o = ModalOrder(view_, layout_); auto it = std::find(o.begin(), o.end(), view_.focus);
        int i = it == o.end() ? 0 : int(it - o.begin()) + (back ? -1 : 1); i = (i + int(o.size())) % int(o.size());
        view_.focus = o[size_t(i)]; view_.keyboard = true; motion_.ClearInteractions(); Render(Motion::Now());
        if (modal_.uia.Listening()) modal_.uia.Raise(view_.focus, UIA_AutomationFocusChangedEventId);
    }
    bool Key(WPARAM key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (key) {
        case VK_ESCAPE: Close(true, false); return true;
        case VK_TAB: Cycle(shift); return true;
        case VK_RETURN:
            if (view_.focus && view_.focus != MOption + int(view_.app) && view_.focus != MLimit) Activate(view_.focus, true); else Activate(MPrimary, true);
            return true;
        case VK_SPACE: if (view_.focus) Activate(view_.focus, true); return true;
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
            if (view_.step == OnboardingStep::Choose && view_.focus >= MOption && view_.focus < MOption + 3) {
                const int d = (key == VK_UP || key == VK_LEFT) ? -1 : 1; const int a = (int(view_.app) + d + 3) % 3;
                view_.app = AgentApp(a); view_.focus = MOption + a; view_.keyboard = true; Relayout(); Render(Motion::Now());
                Announce(AppName(view_.app)); return true;
            }
            return false;
        }
        return false;
    }
    bool Handle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, modal_.Window(), 0}; TrackMouseEvent(&t); const int h = ModalHitAt(view_, layout_, x, y); if (h != hover_) { hover_ = h; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: pressed_ = ModalHitAt(view_, layout_, x, y); view_.keyboard = false; if (pressed_) SetCapture(modal_.Window()); Interact(); r = 0; return true;
        case WM_LBUTTONUP: { const int h = ModalHitAt(view_, layout_, x, y), was = pressed_; pressed_ = 0; if (GetCapture() == modal_.Window()) ReleaseCapture(); Interact(); if (was && was == h) Activate(h, false); r = 0; return true; }
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: if (Key(w)) { r = 0; return true; } return false;
        case WM_TIMER: if (w == HelpTimer) { KillTimer(modal_.Window(), HelpTimer); if (!view_.status.read && view_.step == OnboardingStep::Verify) { view_.help = true; Relayout(); Render(Motion::Now()); } r = 0; return true; } return false;
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
        Accessibility::Node d; d.key = DialogKey; d.role = Accessibility::Role::Group; d.name = view_.Title(); d.help = view_.Subtitle(); d.automationId = L"AnnotationOnboarding"; d.focusable = false;
        GetWindowRect(modal_.Window(), &d.bounds); n.push_back(d);
        auto node = [&](long long key, Accessibility::Role role, const std::wstring& name, D2D1_RECT_F r) -> Accessibility::Node& {
            Accessibility::Node x; x.key = key; x.parent = DialogKey; x.role = role; x.name = name; x.bounds = modal_.Screen(r); n.push_back(x); return n.back(); };
        static const wchar_t* steps[3] = {L"Choisir l'app", L"Installer", L"Vérifier"};
        auto& st = node(StepsKey, Accessibility::Role::Text, std::wstring(L"Étape ") + std::to_wstring(int(view_.step) + 1) + L" sur 3 : " + steps[int(view_.step)], layout_.steps); st.focusable = false;
        if (view_.step == OnboardingStep::Choose) {
            auto& list = node(OptionsKey, Accessibility::Role::List, L"Application", {layout_.options[0].left, layout_.options[0].top, layout_.options[2].right, layout_.options[2].bottom}); list.focusable = false;
            static const wchar_t* names[3] = {L"Claude Desktop (recommandé)", L"Codex", L"Autre client MCP"};
            for (int i = 0; i < 3; ++i) { Accessibility::Node o; o.key = MOption + i; o.parent = OptionsKey; o.role = Accessibility::Role::ListItem; o.name = names[i]; o.selected = int(view_.app) == i; o.position = i + 1; o.count = 3; o.bounds = modal_.Screen(layout_.options[i]); n.push_back(o); }
            auto& lim = node(MLimit, Accessibility::Role::Button, L"Limiter cet agent au projet " + view_.setup.projectLabel, layout_.limit); lim.toggle = view_.limit ? 2 : 1;
        } else if (view_.step == OnboardingStep::Install) {
            if (view_.NoMcpb()) {}
            else if (view_.app == AgentApp::Claude) { auto& o = node(MOpenMcpb, Accessibility::Role::Button, L"Ouvrir dans Claude Desktop", layout_.openMcpb); o.help = view_.setup.mcpbName + L" · " + view_.setup.mcpbSize; }
            else { auto& c = node(MCopyCommand, Accessibility::Role::Button, L"Copier la commande", layout_.rowCopy[0]); c.help = view_.setup.bridgePath;
                   if (!view_.Project().empty()) { auto& a = node(MCopyArgs, Accessibility::Role::Button, L"Copier les arguments", layout_.rowCopy[1]); a.help = ArgsText(view_.Project()); } }
            if (view_.Disclosable()) { auto& d2 = node(MDisclosure, Accessibility::Role::Button, view_.app == AgentApp::Claude ? L"Ou ajouter manuellement" : L"Ou modifier config.toml", layout_.disclosure); d2.expand = view_.manual ? 2 : 1; }
            if (view_.ManualOpen()) {
                auto& b = node(BlockKey, Accessibility::Role::Text, L"Configuration", layout_.block); b.value = view_.Config(); b.focusable = false;
                node(MCopyBlock, Accessibility::Role::Button, L"Copier la configuration", layout_.blockCopy);
            }
        } else {
            auto& q = node(MCopyQuote, Accessibility::Role::Button, L"Copier la demande", layout_.quoteCopy); q.help = view_.Quote();
            auto& s = node(StatusKey, Accessibility::Role::Text, view_.status.read ? (view_.status.reader.empty() ? std::wstring(AppShort(view_.app)) : view_.status.reader) + L" a lu vos annotations · " + view_.status.when : L"En attente de la première lecture", layout_.status); s.focusable = false;
            if (layout_.help.right > 0) node(MHelp, Accessibility::Role::Button, L"Ça ne marche pas ?", layout_.help);
            for (int i = 0; i < layout_.agents; ++i) { const auto& a = view_.status.agents[size_t(i)]; auto& b = node(MAgent + i, Accessibility::Role::Button, (a.connected ? L"Gérer " : L"Connecter ") + a.name, layout_.agentAction[i]); b.help = a.detail; }
        }
        if (!view_.SecondaryLabel().empty()) { auto& s2 = node(MSecondary, Accessibility::Role::Button, view_.SecondaryLabel(), layout_.secondary); s2.accelerator = view_.step == OnboardingStep::Choose ? L"Échap" : L""; }
        auto& p = node(MPrimary, Accessibility::Role::Button, view_.PrimaryLabel(), layout_.primary); p.accelerator = L"Entrée";
        auto& c = node(MClose, Accessibility::Role::Button, L"Fermer", layout_.close); c.accelerator = L"Échap";
        return n;
    }
    bool Ensure() {
        if (modal_.Window()) return true;
        if (!Ink::Shared(root_)) return false;
        WNDCLASSW wc{}; wc.lpfnWndProc = VeilProc; wc.hInstance = instance_; wc.lpszClassName = L"BrowserAnnotationVeil"; RegisterClassW(&wc);
        veil_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, nullptr);
        if (veil_) SetLayeredWindowAttributes(veil_, 0, 0, LWA_ALPHA);
        Layer::Options o; o.activatable = true; o.title = L"Connecter un agent";
        if (!modal_.Create(instance_, owner_, o)) return false;
        modal_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Handle(m, w, l, r); };
        Accessibility::Host host; host.window = modal_.Window(); host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return view_.focus; };
        host.invoke = [this](long long key) { PostMessageW(modal_.Window(), WM_APP + 1, WPARAM(key), 0); };
        host.select = host.invoke; host.toggle = host.invoke;
        host.expand = [this](long long key, bool) { PostMessageW(modal_.Window(), WM_APP + 1, WPARAM(key), 0); };
        modal_.uia.Attach(std::move(host));
        motion_.EnablePolish();
        return true;
    }
public:
    ~Onboarding() { if (Animator::Exists()) Animator::Get().Forget(this); if (veil_ && IsWindow(veil_)) DestroyWindow(veil_); }
    void Init(HINSTANCE instance, HWND owner, std::wstring root, OnboardingHooks hooks) { instance_ = instance; owner_ = owner; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return modal_.Window() != nullptr; }
    bool Visible() const { return open_; }
    HWND Window() const { return modal_.Window(); }
    HWND Veil() const { return veil_; }
    // ownerClient: the browser window's client area in screen pixels (veil + centring).
    bool Show(const OnboardingSetup& setup, RECT ownerClient, bool reduced, AgentApp app = AgentApp::Claude) {
        if (!Ensure()) return false;
        const auto now = Motion::Now(); reduced_ = reduced; motion_.SetReducedMotion(reduced); client_ = ownerClient;
        view_ = ModalView{}; view_.setup = setup; view_.app = app; view_.keyboard = true; hasBefore_ = false; fade_.Reset(0);
        Relayout(); view_.focus = ModalOrder(view_, layout_).front();
        height_.Snap(layout_.height);
        const int hp = Px(layout_.height); top_ = std::max<int>(client_.top + Px(24), (client_.top + client_.bottom - hp) / 2);
        open_ = true; presence_.Show(now);
        if (owner_) EnableWindow(owner_, FALSE);   // modal: the browser behind waits
        Render(now);
        SetForegroundWindow(modal_.Window()); SetFocus(modal_.Window());
        Animator::Get().Request(this);
        if (modal_.uia.Listening()) modal_.uia.Raise(DialogKey, UIA_Window_WindowOpenedEventId);
        Announce(L"Connecter un agent. Étape 1 sur 3 : Choisir l'app");
        return true;
    }
    // From the host's readers\ watch (only between verifyStarted and verifyStopped).
    void SetStatus(const VerifyStatus& status) {
        if (!modal_.Window()) return;
        const bool newly = status.read && !view_.status.read;
        if (newly && view_.step == OnboardingStep::Verify) { Change([&] { view_.status = status; }); KillTimer(modal_.Window(), HelpTimer);
            Announce((status.reader.empty() ? std::wstring(AppShort(view_.app)) : status.reader) + L" a lu vos annotations"); }
        else { view_.status = status; Relayout(); height_.Set(layout_.height, Motion::Now(), Spec::stepHeightResponse, reduced_); Render(Motion::Now()); Animator::Get().Request(this); }
    }
    void Close(bool animate = true, bool completed = false) {
        if (!open_) return; const auto now = Motion::Now(); open_ = false; StopVerify();
        if (owner_) { EnableWindow(owner_, TRUE); SetForegroundWindow(owner_); }
        presence_.Hide(now, animate); hover_ = pressed_ = 0;
        if (!presence_.Visible(now)) { modal_.Hide(); if (veil_) ShowWindow(veil_, SW_HIDE); } else { Render(now); Animator::Get().Request(this); }
        if (modal_.uia.Listening()) modal_.uia.Raise(DialogKey, UIA_Window_WindowClosedEventId);
        if (hooks_.closed) hooks_.closed(completed);
    }
    bool Frame(Motion::Time now) override {
        if (!presence_.Visible(now)) { modal_.Hide(); if (veil_) ShowWindow(veil_, SW_HIDE); return false; }
        Render(now);
        const bool spin = open_ && view_.step == OnboardingStep::Verify && !view_.status.read && !reduced_;
        if (hasBefore_ && !fade_.Moving(now)) hasBefore_ = false;
        return presence_.Moving(now) || fade_.Moving(now) || height_.Moving(now) || spin || motion_.NextWake(now).delay.has_value();
    }
    // ---- tests / QA
    ModalView& ViewForTest() { return view_; }
    const ModalLayout& LayoutForTest() const { return layout_; }
    Layer& LayerForTest() { return modal_; }
    Presence& PresenceForTest() { return presence_; }
    bool KeyForTest(WPARAM key) { return Key(key); }
    void ActivateForTest(int hit) { Activate(hit, true); }
    void HelpTimeoutForTest() { LRESULT r; Handle(WM_TIMER, HelpTimer, 0, r); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
};
}} // namespace NativeChrome::Annotations
