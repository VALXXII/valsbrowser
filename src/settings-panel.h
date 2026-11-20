#pragma once
// Native "Réglages" drawer, painted with Direct2D through its own Chrome
// instance (shared fonts, icons, helpers). Layout from the Paper page
// "Réglages v4": a short root (search, the two most used settings, four
// categories with a summary of their state) and one sub-view per category. It docks to the window's right edge
// under the toolbar like the download history drawer, but always arrives with
// a slide and leaves the same way. Layout from the Paper frames "Réglages —
// panneau v2", "Autorisations des sites (sous-vue)" and "Autorisations ›
// meet.google.com".
//
// Motion (Apple / Emil): every lane retargets from the value on screen, so a
// toggle flicked twice or a drawer reopened mid-exit never jumps.
//  - Drawer: critically damped springs, exit snappier than enter.
//  - Engine choice: the selection plate and its check glide to the new row.
//  - Switches: the knob slides on a short spring and stretches while pressed.
//  - "Voir plus…": the extra engines unfold like a download section.
//  - Sub-views (Autorisations › site) push in from the right; the view below
//    slides 30% the other way and dims (iOS navigation), and pops back the same way.
//  - Value rows open the native menu as their list of choices (described
//    options for anti-tracking, sleeping tabs and site permissions).
//  - Address fields (custom search engine, custom home page) unfold under the
//    choice that needs them; editing happens in a native EDIT laid over the field.
// Reduced motion keeps every state change and drops the travel.
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <array>
#include <ctime>
#include <map>
#include <functional>
#include <vector>
#include "chrome.h"
#include "downloads.h"
#include "settings.h"
#include "context-menu.h"
#include "native-ui.h"
#include "tooltip-placement.h"
#include "modal.h"
#include "accessibility.h"
#include "svg-path.h"
#include "annotation-settings.h"
#include "profile-editor.h"
#include "i18n.h"
#include "ui-language.h"

namespace NativeChrome {
namespace SettingsUI {
using Downloads::Lane; using Downloads::Spring; using Downloads::Easing; using Downloads::Settle;
using BrowserSettings::Engine;

// ---- Hit identifiers ----------------------------------------------------------
enum Hit : int {
    None = 0, Close = 1, Back = 2, EngineBase = 10, More = 20,
    RestoreTabs = 30, Folder = 31, AskWhere = 32, ReduceMotion = 33, Suggestions = 34, Passwords = 35, Autofill = 36, ClearOnExit = 37,
    ClearData = 40, CheckUpdates = 41, ResetAll = 42, ResetSite = 43, ManagePasswords = 44, ImportData = 46,
    Tracking = 50, Sleep = 51, Zoom = 52, Permissions = 53, HomePageHit = 54, TabPositionHit = 55, AutoplayHit = 56, BookmarksBarHit = 57,
    ConfirmClose = 38, CloseWithLastTab = 39, AdBlock = 60, HttpsOnly = 61, ThirdPartyCookies = 62, MemorySaver = 63, MuteBackground = 64, GrayscaleText = 65,
    CustomSearchField = 70, HomepageField = 71, SearchField = 72, SearchDismiss = 73,
    CatEngine = 80, CatTabs = 81, CatPrivacy = 82, CatDownloads = 83, CatDisplay = 84,
    CatProfiles = 85, ProfileDelete = 86, ProfileEditorHit = 87, ProfileClearData = 88, ProfileRowBase = 700,   // Réglages › Profils (Paper p-I-0 P02)
    CatAnnotations = 89,   // « Annotations » (85..88 are Profils)
    LanguageHit = 66,      // « Langue » (root, under Profils): browser-wide interface language, applied at next start
    // « Annotations »: one button per agent row, the manual fallback, the prompt switch, retention, folder, clear all.
    AgentBase = 90, ManualConnect = 92, AnnotationPrompt = 67, AnnotationRetention = 58, AnnotationFolder = 59, AnnotationClear = 45,
    SiteBase = 100, PermissionBase = 400, ResultBase = 600,
};
inline constexpr int AgentHit(AnnotationSettings::App a) { return AgentBase + static_cast<int>(a); }
inline constexpr bool IsAgent(int hit) { return hit >= AgentBase && hit < AgentBase + AnnotationSettings::AppCount; }
inline bool IsField(int hit) { return hit == CustomSearchField || hit == HomepageField || hit == SearchField; }
inline bool IsResult(int hit) { return hit >= ResultBase && hit < ResultBase + 32; }
inline int EngineHit(int index) { return EngineBase + index; }
inline bool IsEngine(int hit) { return hit >= EngineBase && hit < EngineBase + BrowserSettings::EngineCount; }
inline bool IsSite(int hit) { return hit >= SiteBase && hit < PermissionBase; }
inline bool IsPermission(int hit) { return hit >= PermissionBase && hit < PermissionBase + 100; }
inline bool IsProfileRow(int hit) { return hit >= ProfileRowBase && hit < ProfileRowBase + 64; }
constexpr int SwitchHits[] = {RestoreTabs, AskWhere, ReduceMotion, Suggestions, Passwords, Autofill, ClearOnExit,
                              ConfirmClose, CloseWithLastTab, AdBlock, HttpsOnly, ThirdPartyCookies, MemorySaver, MuteBackground,
                              GrayscaleText, AnnotationPrompt};
constexpr int SwitchCount = 16;
inline int SwitchIndex(int hit) { for (int i = 0; i < SwitchCount; ++i) if (SwitchHits[i] == hit) return i; return -1; }

// ---- Geometry (DIP, panel-local; x = 0 is the drawer's leading edge) -----------
namespace Layout {
using Design::Box;
inline constexpr float width = 332, top = Downloads::DrawerLayout::top;                          // under the toolbar
inline constexpr float inset = 12, rowLeft = inset, rowRight = width - inset;                    // rows: 12..320
inline constexpr float textLeft = rowLeft + 8;                                                    // 20: labels, names
inline constexpr float slotRight = rowRight - 4, slot = Design::Size::compact;                   // trailing 24 DIP slot: ..316
inline constexpr float iconLeft = textLeft, iconSize = Design::Size::icon;                        // favicon / caret: 20..36
inline constexpr float nameLeft = iconLeft + iconSize + Design::Space::md;                        // 44
inline constexpr float padTop = 16, headerHeight = 24, padBottom = 16, groupGap = 32;
inline constexpr float viewTop = padTop + headerHeight + groupGap;                                 // 72: content scrolls under the header
inline constexpr float label = 24, rowGap = 6, row = 32, pitch = row + rowGap;                    // label: 20 + 4 below
inline constexpr float folderRow = 54, twoLinePad = 8, descriptionLine = 18, siteRow = 54;
// Address field: a 32 DIP field, 6 DIP, one 18 DIP hint line.
inline constexpr float field = 32, fieldHint = 6, fieldRow = field + fieldHint + descriptionLine;
inline constexpr float TwoLine(size_t lines) { return 2 * twoLinePad + 20 + 2 + lines * descriptionLine; }
// Footer of every settings view: hairline, 12, version (24), 16.
inline constexpr float footer = 1 + 12 + slot + padBottom;                                        // 53
// Search results: a card 6 DIP under the field; rows of title + path (8 / 10 / 12 padding).
inline constexpr float resultGap = 6, resultRow = 8 + 20 + 2 + 18 + 8, resultPadLeft = 10, resultPadRight = 12, maxResults = 7;
inline constexpr float searchDim = .32f;                                                           // the root behind results (--opacity-browser-disabled)
inline constexpr float switchWidth = 28, switchHeight = 16, knob = 12, knobInset = 2, knobStretch = 3;
inline constexpr float topFade = 16, bottomFade = 48;
inline constexpr Box Title(bool sub) { return {sub ? nameLeft : textLeft, padTop, rowRight - slot - 8, padTop + headerHeight}; }
inline constexpr Box BackButton() { return {rowLeft, padTop, rowLeft + slot, padTop + headerHeight}; }
inline constexpr Box CloseButton() { return {rowRight - slot, padTop, rowRight, padTop + headerHeight}; }
inline constexpr Box Row(float y, float h = row) { return {rowLeft, y, rowRight, y + h}; }
inline constexpr Box Trailing(float y, float h = row) { return {slotRight - slot, y + (h - slot) / 2, slotRight, y + (h + slot) / 2}; }
inline constexpr Box Switch(float y, float h = row) { return {slotRight - switchWidth, y + (h - switchHeight) / 2, slotRight, y + (h + switchHeight) / 2}; }
inline constexpr Box Icon(float y, float h = row) { return {iconLeft, y + (h - iconSize) / 2, iconLeft + iconSize, y + (h + iconSize) / 2}; }
inline constexpr Box Chevron(float y, float h = row) { return {slotRight - 16, y + (h - 24) / 2, slotRight, y + (h + 24) / 2}; }
// Two-line switch rows wrap their description up to 16 DIP before the switch.
inline constexpr float descriptionWidth = slotRight - switchWidth - 16 - textLeft;
// Agent rows (« Annotations »): logo in the icon slot, name on the first line, the action as a
// compact raised button (the update toast's Raised button at the drawer's 24 DIP control height) on the
// same line, the status wrapped under the name across the row (it starts where the button ends).
inline constexpr float actionHeight = Design::Size::compact, actionPad = 8;
inline constexpr float statusWidth = rowRight - 8 - nameLeft;
inline constexpr Box Action(float top, float w) { return {slotRight - w, top + twoLinePad - 2, slotRight, top + twoLinePad - 2 + actionHeight}; }
}
static_assert(Layout::nameLeft == Layout::rowLeft + Layout::slot + 8, "sub-view title: 8 DIP after the back button");
static_assert(Layout::viewTop == 72, "content starts 32 DIP under the 24 DIP header");

namespace Timing {
inline constexpr Spring selectSpring{.26f}, switchSpring{.2f}, pageSpring{.34f};
inline constexpr Motion::Milliseconds select = Settle(.26f), toggle = Settle(.2f), press{120}, spin{900}, page = Settle(.34f);
}

enum class View { Main, Engine, Tabs, Privacy, Downloads, Display, Sites, Site, Profiles, Profile, Annotations };
inline const wchar_t* Title(View v) {
    switch (v) {
    case View::Engine: return TR(settings_engine); case View::Tabs: return TR(settings_tabs); case View::Privacy: return TR(settings_privacy);
    case View::Downloads: return TR(settings_downloads); case View::Display: return TR(settings_display); case View::Sites: return TR(settings_permissions);
    case View::Profiles: return TR(settings_profiles);
    case View::Annotations: return TR(settings_annotations);
    default: return TR(settings_title);
    }
}
// The row that opened a view (focus lands back on it when the view is popped).
inline int Opener(View v) {
    switch (v) {
    case View::Engine: return CatEngine; case View::Tabs: return CatTabs; case View::Privacy: return CatPrivacy;
    case View::Downloads: return CatDownloads; case View::Display: return CatDisplay; case View::Sites: return Permissions; case View::Profiles: return CatProfiles;
    case View::Annotations: return CatAnnotations; default: return None;
    }
}
// One laid-out element of a view's scrolling list (content coordinates).
enum class Kind { Label, Engine, Switch, Folder, Value, Nav, SiteRow, Note, Field, Search, Danger, ProfileRow, Editor, Agent };
struct Slot {
    Kind kind = Kind::Label; int hit = 0, index = -1;
    float top = 0, height = Layout::row, opacity = 1;
    float clipTop = -1e6f, clipBottom = 1e6f;
    std::wstring text, value;              // label; value/summary/description text (Agent: the action's label)
    std::vector<std::wstring> lines;       // wrapped description (two-line switches; Agent: its status)
    const wchar_t* icon = nullptr;         // leading 16 DIP glyph (root rows): the label moves to nameLeft
    const char* path = nullptr;            // or a 256-unit Phosphor path (Profils row, profile rows)
    const char* valueGlyph = nullptr;      // a glyph before the value (« Profils » → the current profile)
    D2D1_COLOR_F ink{1, 1, 1, 1}; bool current = false;   // profile rows: identity ink, this window's profile
    const AnnotationSettings::Glyph* glyph = nullptr; // or a vector glyph in its own viewBox (annotation mark)
    bool disabled = false;                 // shown, not actionable (no hover, not in the Tab order)
    bool error = false;                    // Agent: the status is an error (danger ink)
};
// Key in LayoutInput::lines of the section's shortcut reminder (no hit: it is read, not focused).
inline constexpr int ShortcutsNoteKey = -2;
// One profile as the drawer lists it (Paper P02 « Profils · liste »).
struct ProfileItem { std::wstring id, name; const char* glyph = nullptr; D2D1_COLOR_F ink{1, 1, 1, 1}; bool current = false; };
// Per-frame inputs of the pure layout.
struct LayoutInput {
    const std::map<int, std::vector<std::wstring>>* lines = nullptr; // wrapped descriptions by hit
    std::vector<std::wstring> noSitesLines;                     // "Autorisations des sites" with nothing to manage
    float customReveal = 0, homeReveal = 0;                     // address fields unfolding (0..1)
    std::wstring folderName;                                     // the downloads folder's own name (root summary)
    bool autoplayPending = false, cookiesPending = false, grayscalePending = false; // differ from what the engine started with
    const BrowserSettings::State* state = nullptr;
    const std::vector<BrowserSettings::Site>* sites = nullptr; bool sitesLoaded = false;
    bool sitesQuiet = false;   // first load still within its grace period: no « Chargement… » flash
    // « Annotations » (only when the annotation mode is compiled in: dev / preview builds).
    bool annotations = false;
    const AnnotationSettings::Agents* agents = nullptr; bool agentsKnown = false;
    std::array<std::vector<std::wstring>, AnnotationSettings::AppCount> agentLines;   // wrapped status per app
    long long now = 0;                                          // UTC seconds (status « il y a … »)
    std::size_t annotationCount = SIZE_MAX;                     // SIZE_MAX: not counted yet
    std::wstring annotationFolder;                              // the store root (second line of the folder row)
    bool vaultManager = false; std::size_t vaultCount = 0;   // E8: the vault is active (entry shown) and its account count
    bool importData = false;                                   // « Importer depuis un navigateur… » (not in a private window)
    int site = -1;                                             // Site view: index into sites
    const std::vector<ProfileItem>* profiles = nullptr;        // Réglages › Profils
    float editorHeight = 0;                                    // Profils › <profil>: the editor block
    // « Langue » (shown when the host can save the preference): the saved language, and whether the last save failed.
    bool language = false; I18n::Locale languageSaved = I18n::Locale::Fr; bool languageFailed = false;
};
// Phosphor Bold « translate » (official @phosphor-icons/core path, Paper p-K-0), drawn like the Profils glyph.
inline constexpr const char* TranslateGlyph = "M250.73,210.63l-56-112a12,12,0,0,0-21.46,0l-20.52,41A84.2,84.2,0,0,1,114,126.22,107.48,107.48,0,0,0,139.33,68H160a12,12,0,0,0,0-24H108V32a12,12,0,0,0-24,0V44H32a12,12,0,0,0,0,24h83.13A83.69,83.69,0,0,1,96,110.35,84,84,0,0,1,83.6,91a12,12,0,1,0-21.81,10A107.55,107.55,0,0,0,78,126.24,83.54,83.54,0,0,1,32,140a12,12,0,0,0,0,24,107.47,107.47,0,0,0,64-21.07,108.4,108.4,0,0,0,45.39,19.44l-24.13,48.26a12,12,0,1,0,21.46,10.73L151.41,196h65.17l12.68,25.36a12,12,0,1,0,21.47-10.73ZM163.41,172,184,130.83,204.58,172Z";
inline std::wstring Host(const std::wstring& origin) {
    std::wstring h = origin; const auto scheme = h.find(L"://"); if (scheme != std::wstring::npos) h = h.substr(scheme + 3);
    while (!h.empty() && h.back() == L'/') h.pop_back();
    return h;
}

inline std::vector<Slot> ContentSlots(View view, const LayoutInput& in, float scroll, float* contentBottom = nullptr) {
    using namespace Layout;
    std::vector<Slot> slots; float y = viewTop - scroll;
    auto labelAt = [&](std::wstring text) { Slot s; s.kind = Kind::Label; s.top = y; s.height = label; s.text = std::move(text); slots.push_back(s); y += label + rowGap; };
    auto add = [&](Kind kind, int hit, float h, std::wstring text = L"", std::wstring value = L"") {
        Slot s; s.kind = kind; s.hit = hit; s.top = y; s.height = h; s.text = std::move(text); s.value = std::move(value); slots.push_back(s); y += h + rowGap; return &slots.back();
    };
    auto endGroup = [&] { y += groupGap - rowGap; };
    const auto* st = in.state;
    auto described = [&](int hit) -> std::vector<std::wstring> {
        if (!in.lines) return {};
        const auto it = in.lines->find(hit); return it == in.lines->end() ? std::vector<std::wstring>{} : it->second;
    };
    auto twoLine = [&](int hit, const wchar_t* label) { auto lines = described(hit); auto* s = add(Kind::Switch, hit, TwoLine(lines.size()), label); s->lines = std::move(lines); };
    // An address field unfolding under its choice: clipped to its share of the reveal.
    auto fieldAt = [&](int hit, float reveal, float clipTop = -1e6f, float clipBottom = 1e6f, float opacity = 1) {
        const float r = Motion::Clamp(reveal); if (r <= .001f) return;
        Slot s; s.kind = Kind::Field; s.hit = hit; s.top = y - (1 - r) * 8; s.height = fieldRow; s.opacity = r * opacity;
        s.clipTop = std::max(clipTop, y); s.clipBottom = std::min(clipBottom, y + r * fieldRow);
        slots.push_back(s); y += r * (fieldRow + rowGap);
    };
    auto lead = [&](Kind kind, int hit, const wchar_t* icon, const wchar_t* text, std::wstring value) { auto* s = add(kind, hit, row, text, std::move(value)); s->icon = icon; return s; };
    auto pending = [&](int hit, bool note) { auto lines = described(hit); if (note) lines.push_back(TR(language_pending)); return lines; };
    if (view == View::Main) {
        add(Kind::Search, SearchField, field);
        endGroup();
        lead(Kind::Nav, CatEngine, L"favicon-mask", TR(settings_engine), st ? std::wstring(BrowserSettings::Info(st->engine).name) : L"");
        lead(Kind::Value, HomePageHit, L"home-mask", TR(settings_homepage), st ? BrowserSettings::Info(st->newTabPage).label : L"");
        fieldAt(HomepageField, in.homeReveal);
        // « Profils » is always listed, even with one profile (Paper P02 XTM-0): its value is the
        // current profile, glyph in its ink then its name.
        if (in.profiles && !in.profiles->empty()) {
            const ProfileItem* cur = &in.profiles->front(); for (const auto& p : *in.profiles) if (p.current) cur = &p;
            auto* s = add(Kind::Nav, CatProfiles, row, TR(settings_profiles), cur->name); s->path = ProfileIcons::Bold::userCircle; s->valueGlyph = cur->glyph; s->ink = cur->ink;
        }
        // « Langue » (Paper p-K-0): the saved language by its own name; a note while it differs from this
        // process's language (applied at the next start), or why it could not be saved (danger ink).
        if (in.language) {
            std::vector<std::wstring> lines;
            if (in.languageFailed) lines.push_back(TR(language_save_failed));
            else if (in.languageSaved != I18n::Current()) lines.push_back(TR(language_pending));
            auto* s = add(Kind::Value, LanguageHit, lines.empty() ? row : TwoLine(lines.size()), TR(language_label), UiLanguage::Autonym(in.languageSaved));
            s->path = TranslateGlyph; s->lines = std::move(lines); s->error = in.languageFailed;
        }
        endGroup();
        lead(Kind::Nav, CatTabs, L"tabs-mask", TR(settings_tabs), st ? (st->restoreTabs ? TR(settings_tabs_restored) : TR(settings_tabs_not_restored)) : L"");
        lead(Kind::Nav, CatPrivacy, L"shield-mask", TR(settings_privacy), st ? BrowserSettings::Info(st->tracking).label : L"");
        lead(Kind::Nav, CatDownloads, L"downloads-mask", TR(settings_downloads), st ? (st->askWhereToSave ? std::wstring(TR(settings_downloads_ask)) : in.folderName) : L"");
        lead(Kind::Nav, CatDisplay, L"eye-mask", TR(settings_display), st ? BrowserSettings::ZoomLabel(st->zoomPercent) : L"");
        if (in.annotations) { auto* s = lead(Kind::Nav, CatAnnotations, nullptr, TR(settings_annotations), in.agents ? AnnotationSettings::Summary(*in.agents, in.agentsKnown) : L""); s->glyph = &AnnotationSettings::Mark; }
        y -= rowGap;
    } else if (view == View::Engine) {
        for (int i = 0; i < BrowserSettings::EngineCount; ++i) { auto* s = add(Kind::Engine, EngineHit(i), row); s->index = i; }
        fieldAt(CustomSearchField, in.customReveal);
        endGroup();
        twoLine(Suggestions, TR(settings_suggestions));
        y -= rowGap;
    } else if (view == View::Tabs) {
        labelAt(TR(settings_group_opening));
        twoLine(RestoreTabs, TR(settings_restore_tabs));
        add(Kind::Value, TabPositionHit, row, TR(settings_new_tabs), st ? BrowserSettings::Info(st->tabPosition).label : L"");
        endGroup();
        labelAt(TR(settings_group_closing));
        twoLine(ConfirmClose, TR(settings_confirm_close));
        twoLine(CloseWithLastTab, TR(settings_close_with_last_tab));
        endGroup();
        labelAt(TR(settings_group_sleep));
        add(Kind::Value, Sleep, row, TR(settings_sleeping_tabs), st ? BrowserSettings::SleepLabel(st->sleepMinutes) : L"");
        twoLine(MemorySaver, TR(settings_memory_saver));
        y -= rowGap;
    } else if (view == View::Privacy) {
        labelAt(TR(settings_group_protection));
        add(Kind::Value, Tracking, row, TR(settings_tracking), st ? BrowserSettings::Info(st->tracking).label : L"");
        twoLine(AdBlock, TR(settings_ad_blocker));
        twoLine(HttpsOnly, TR(settings_https_only));
        { auto lines = pending(ThirdPartyCookies, in.cookiesPending); auto* s = add(Kind::Switch, ThirdPartyCookies, TwoLine(lines.size()), TR(settings_third_party_cookies)); s->lines = std::move(lines); }
        endGroup();
        labelAt(TR(settings_group_forms));
        twoLine(Passwords, TR(settings_passwords));
        // E8: the vault's manager, a separate entry from the switch (shown only while the vault is active).
        if (in.vaultManager) add(Kind::Nav, ManagePasswords, row, TR(settings_manage_passwords), in.vaultCount == SIZE_MAX ? std::wstring(TR(settings_vault_unreadable)) : std::to_wstring(in.vaultCount));
        twoLine(Autofill, TR(settings_autofill));
        endGroup();
        labelAt(TR(settings_group_data));
        if (in.sitesLoaded && in.sites && in.sites->empty()) {
            // Nothing to manage yet: an inert row says why instead of opening an empty view.
            auto* s = add(Kind::Nav, None, TwoLine(in.noSitesLines.size()), TR(settings_site_permissions)); s->lines = in.noSitesLines;
        } else add(Kind::Nav, Permissions, row, TR(settings_site_permissions), in.sitesLoaded && in.sites ? std::to_wstring(in.sites->size()) : L"");
        if (in.importData) add(Kind::Nav, ImportData, row, TR(settings_import_data));
        twoLine(ClearOnExit, TR(settings_clear_on_exit));
        add(Kind::Danger, ClearData, row, TR(settings_clear_data));
        y -= rowGap;
    } else if (view == View::Downloads) {
        add(Kind::Folder, Folder, folderRow);
        twoLine(AskWhere, TR(settings_ask_where));
        y -= rowGap;
    } else if (view == View::Display) {
        labelAt(TR(settings_group_pages));
        add(Kind::Value, Zoom, row, TR(settings_zoom), st ? BrowserSettings::ZoomLabel(st->zoomPercent) : L"");
        add(Kind::Value, BookmarksBarHit, row, TR(settings_bookmarks_bar), st ? BrowserSettings::ShortLabel(st->bookmarksBar) : L"");
        twoLine(ReduceMotion, TR(settings_reduce_motion));
        { auto lines = pending(GrayscaleText, in.grayscalePending); auto* s = add(Kind::Switch, GrayscaleText, TwoLine(lines.size()), TR(settings_grayscale_text)); s->lines = std::move(lines); }
        endGroup();
        labelAt(TR(settings_group_sound));
        {
            auto* s = add(Kind::Value, AutoplayHit, in.autoplayPending ? TwoLine(1) : row, TR(settings_autoplay), st ? BrowserSettings::Info(st->autoplay).label : L"");
            if (in.autoplayPending) s->lines = {TR(language_pending)};
        }
        twoLine(MuteBackground, TR(settings_mute_background));
        y -= rowGap;
    } else if (view == View::Annotations) {
        namespace AS = AnnotationSettings;
        labelAt(TR(settings_group_agents));
        for (int i = 0; i < AS::AppCount; ++i) {
            const auto app = AS::AppAt(i); const AS::Agent agent = in.agents ? (*in.agents)[app] : AS::Agent{};
            auto lines = in.agentLines[static_cast<size_t>(i)]; if (lines.empty()) lines.push_back(AS::StatusText(agent, in.now));
            const auto action = AS::ActionFor(agent);
            auto* s = add(Kind::Agent, action == AS::Action::None ? None : AgentHit(app), TwoLine(lines.size()), AS::AppName(app), AS::ActionLabel(action));
            s->index = i; s->lines = std::move(lines); s->disabled = !AS::ActionEnabled(agent); s->error = AS::StatusIsError(agent);
        }
        add(Kind::Nav, ManualConnect, row, TR(settings_manual_connect));
        endGroup();
        labelAt(TR(settings_group_annotation_mode));
        twoLine(AnnotationPrompt, TR(settings_connect_prompt));
        { auto lines = described(ShortcutsNoteKey); const size_t n = std::max<size_t>(1, lines.size()); auto* s = add(Kind::Note, None, n * 20.f, AS::ShortcutsNote()); s->lines = std::move(lines); }
        endGroup();
        labelAt(TR(settings_group_history));
        add(Kind::Value, AnnotationRetention, row, TR(settings_keep_done), st ? BrowserSettings::RetentionLabel(st->annotationRetentionDays) : L"");
        add(Kind::Folder, AnnotationFolder, folderRow, TR(settings_open_annotation_folder), in.annotationFolder);
        { auto* s = add(Kind::Danger, AnnotationClear, row, TR(settings_clear_annotations)); s->disabled = in.annotationCount == 0 || in.annotationCount == SIZE_MAX; }
        y -= rowGap;
    } else if (view == View::Sites) {
        const size_t n = in.sites ? in.sites->size() : 0;
        if (!in.sitesLoaded) { if (!in.sitesQuiet) add(Kind::Note, None, row, TR(settings_loading)); }
        else if (!n) { add(Kind::Note, None, 2 * descriptionLine, TR(settings_no_sites)); }
        else {
            labelAt(TRP(settings_sites_count, n));
            for (size_t i = 0; i < n; ++i) {
                auto* s = add(Kind::SiteRow, SiteBase + static_cast<int>(i), siteRow, Host((*in.sites)[i].origin), BrowserSettings::SiteSummary((*in.sites)[i]));
                s->index = static_cast<int>(i);
            }
        }
        y -= rowGap;
    } else if (view == View::Profiles) {
        const size_t n = in.profiles ? in.profiles->size() : 0;
        labelAt(TRP(settings_profiles_count, n));
        for (size_t i = 0; i < n; ++i) {   // one line, 40 high, 2 apart (Paper XSN-0)
            const auto& p = (*in.profiles)[i];
            Slot s; s.kind = Kind::ProfileRow; s.hit = ProfileRowBase + static_cast<int>(i); s.index = static_cast<int>(i); s.top = y; s.height = 40;
            s.text = p.name; s.path = p.glyph; s.ink = p.ink; s.current = p.current; slots.push_back(s); y += 40 + 2;
        }
        y += rowGap - 2;
        y -= rowGap;
    } else if (view == View::Profile) {
        Slot s; s.kind = Kind::Editor; s.hit = ProfileEditorHit; s.top = y; s.height = in.editorHeight; slots.push_back(s); y += s.height;
    } else if (in.sites && in.site >= 0 && in.site < static_cast<int>(in.sites->size())) {
        labelAt(TR(settings_permissions));
        const auto& site = (*in.sites)[in.site];
        for (size_t i = 0; i < site.permissions.size(); ++i) {
            const auto& p = site.permissions[i];
            auto* s = add(Kind::Value, PermissionBase + static_cast<int>(i), row, BrowserSettings::PermissionName(p.kind), BrowserSettings::PermissionValue(p.kind, p.state));
            s->index = static_cast<int>(i);
        }
        y -= rowGap;
    }
    if (contentBottom) *contentBottom = y + scroll;
    return slots;
}

// "…\Downloads": the folder's own name, the rest elided; a drive root stays whole.
inline std::wstring FolderText(const std::wstring& path) {
    std::wstring p = path; while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const auto slash = p.find_last_of(L"\\/");
    if (p.empty()) return TR(settings_downloads_folder_default);
    if (slash == std::wstring::npos || slash + 1 >= p.size()) return p;
    if (slash <= 2) return p; // "C:\Users" stays readable
    return L"…\\" + p.substr(slash + 1);   // i18n-exempt: path elision mark
}
// Second lines of the two-line switches.
inline const std::pair<int, I18n::Text> Descriptions[] = {
    {Suggestions, {I18n::Id::settings_suggestions_note}},
    {RestoreTabs, {I18n::Id::settings_restore_tabs_note}},
    {ConfirmClose, {I18n::Id::settings_confirm_close_note}},
    {CloseWithLastTab, {I18n::Id::settings_close_with_last_tab_note}},
    {MemorySaver, {I18n::Id::settings_memory_saver_note}},
    {AdBlock, {I18n::Id::settings_ad_blocker_note}},
    {HttpsOnly, {I18n::Id::settings_https_only_note}},
    {ThirdPartyCookies, {I18n::Id::settings_third_party_cookies_note}},
    {Passwords, {I18n::Id::settings_passwords_note}},
    {Autofill, {I18n::Id::settings_autofill_note}},
    {ClearOnExit, {I18n::Id::settings_clear_on_exit_note}},
    {AskWhere, {I18n::Id::settings_ask_where_note}},
    {ReduceMotion, {I18n::Id::settings_reduce_motion_note}},
    {MuteBackground, {I18n::Id::settings_mute_background_note}},
    {GrayscaleText, {I18n::Id::settings_grayscale_text_note}},
    {AnnotationPrompt, {I18n::Id::settings_connect_prompt_note}},
};
// Settings search: every row a query can reach, with where it lives and the words it answers to.
// Titles, paths and keywords are catalog ids (I18n::Text converts at use): the index is never frozen in one language.
struct Searchable { int hit; I18n::Text title, path; View view; I18n::Text keywords; };
using I = I18n::Id;
inline const Searchable SearchIndex[] = {
    {CatEngine, {I::settings_engine}, {I::settings_title}, View::Main, {I::search_kw_engine}},
    {Suggestions, {I::settings_suggestions}, {I::settings_engine}, View::Engine, {I::search_kw_suggestions}},
    {HomePageHit, {I::settings_homepage}, {I::settings_title}, View::Main, {I::search_kw_homepage}},
    {LanguageHit, {I::language_label}, {I::settings_title}, View::Main, {I::language_keywords}},
    {RestoreTabs, {I::settings_restore_tabs}, {I::settings_path_tabs_opening}, View::Tabs, {I::search_kw_restore_tabs}},
    {TabPositionHit, {I::settings_new_tabs}, {I::settings_path_tabs_opening}, View::Tabs, {I::search_kw_new_tabs}},
    {ConfirmClose, {I::settings_confirm_close}, {I::settings_path_tabs_closing}, View::Tabs, {I::search_kw_confirm_close}},
    {CloseWithLastTab, {I::settings_close_with_last_tab}, {I::settings_path_tabs_closing}, View::Tabs, {I::search_kw_close_with_last_tab}},
    {Sleep, {I::settings_sleeping_tabs}, {I::settings_path_tabs_sleep}, View::Tabs, {I::search_kw_sleep}},
    {MemorySaver, {I::settings_memory_saver}, {I::settings_path_tabs_sleep}, View::Tabs, {I::search_kw_memory_saver}},
    {Tracking, {I::settings_tracking}, {I::settings_path_privacy_protection}, View::Privacy, {I::search_kw_tracking}},
    {AdBlock, {I::settings_ad_blocker}, {I::settings_path_privacy_protection}, View::Privacy, {I::search_kw_ad_blocker}},
    {HttpsOnly, {I::settings_https_only}, {I::settings_path_privacy_protection}, View::Privacy, {I::search_kw_https_only}},
    {ThirdPartyCookies, {I::settings_third_party_cookies}, {I::settings_path_privacy_protection}, View::Privacy, {I::search_kw_third_party_cookies}},
    {Passwords, {I::settings_passwords}, {I::settings_path_privacy_forms}, View::Privacy, {I::search_kw_passwords}},
    {Autofill, {I::settings_autofill}, {I::settings_path_privacy_forms}, View::Privacy, {I::search_kw_autofill}},
    {Permissions, {I::settings_site_permissions}, {I::settings_path_privacy_data}, View::Privacy, {I::search_kw_site_permissions}},
    {ClearOnExit, {I::settings_clear_on_exit}, {I::settings_path_privacy_data}, View::Privacy, {I::search_kw_clear_on_exit}},
    {ImportData, {I::settings_import_data}, {I::settings_path_privacy_data}, View::Privacy, {I::search_kw_import_data}},
    {ClearData, {I::settings_clear_data}, {I::settings_path_privacy_data}, View::Privacy, {I::search_kw_clear_data}},
    {Folder, {I::settings_save_in}, {I::settings_downloads}, View::Downloads, {I::search_kw_folder}},
    {AskWhere, {I::settings_ask_where}, {I::settings_downloads}, View::Downloads, {I::search_kw_folder}},
    {Zoom, {I::settings_zoom}, {I::settings_path_display_pages}, View::Display, {I::search_kw_zoom}},
    {BookmarksBarHit, {I::settings_bookmarks_bar}, {I::settings_path_display_pages}, View::Display, {I::search_kw_bookmarks_bar}},
    {ReduceMotion, {I::settings_reduce_motion}, {I::settings_path_display_pages}, View::Display, {I::search_kw_reduce_motion}},
    {GrayscaleText, {I::settings_grayscale_text}, {I::settings_path_display_pages}, View::Display, {I::search_kw_grayscale_text}},
    {AutoplayHit, {I::settings_autoplay}, {I::settings_path_display_sound}, View::Display, {I::search_kw_autoplay}},
    {MuteBackground, {I::settings_mute_background}, {I::settings_path_display_sound}, View::Display, {I::search_kw_mute_background}},
    // « Annotations » (found only when the annotation mode is compiled in).
    {CatAnnotations, {I::settings_annotations}, {I::settings_title}, View::Main, {I::search_kw_annotations}},
    {AgentHit(AnnotationSettings::App::Claude), {I::brand_claude_desktop}, {I::settings_path_annotations_agents}, View::Annotations, {I::search_kw_claude}},
    {AgentHit(AnnotationSettings::App::Codex), {I::brand_codex}, {I::settings_path_annotations_agents}, View::Annotations, {I::search_kw_codex}},
    {ManualConnect, {I::settings_manual_connect}, {I::settings_path_annotations_agents}, View::Annotations, {I::search_kw_manual_connect}},
    {AnnotationPrompt, {I::settings_connect_prompt}, {I::settings_path_annotations_mode}, View::Annotations, {I::search_kw_connect_prompt}},
    {AnnotationRetention, {I::settings_keep_done}, {I::settings_path_annotations_history}, View::Annotations, {I::search_kw_keep_done}},
    {AnnotationFolder, {I::settings_open_annotation_folder}, {I::settings_path_annotations_history}, View::Annotations, {I::search_kw_annotation_folder}},
    {AnnotationClear, {I::settings_clear_annotations}, {I::settings_path_annotations_history}, View::Annotations, {I::search_kw_clear_annotations}},
};
inline bool IsAnnotationEntry(const Searchable& e) { return e.view == View::Annotations || e.hit == CatAnnotations; }
// Case- and accent-insensitive form for matching ("Économiseur" ~ "economiseur").
inline std::wstring Folded(const std::wstring& text) {
    std::wstring out; out.reserve(text.size());
    for (wchar_t c : text) {
        c = static_cast<wchar_t>(towlower(c));
        switch (c) {
        case L'à': case L'â': case L'ä': c = L'a'; break; case L'é': case L'è': case L'ê': case L'ë': c = L'e'; break;
        case L'î': case L'ï': c = L'i'; break; case L'ô': case L'ö': c = L'o'; break; case L'ù': case L'û': case L'ü': c = L'u'; break;
        case L'ç': c = L'c'; break; case L'’': c = L'\''; break; default: break;
        }
        out += c;
    }
    return out;
}
// Results: title matches first (with the matched span), then keyword matches.
struct SearchResult { const Searchable* entry; size_t at = std::wstring::npos, length = 0; };
inline std::vector<SearchResult> SearchSettings(const std::wstring& query, bool annotations = false, bool importData = true) {
    std::vector<SearchResult> titled, keyed; const std::wstring q = Folded(BrowserSettings::TrimText(query));
    if (q.empty()) return {};
    for (const auto& e : SearchIndex) {
        if (!annotations && IsAnnotationEntry(e)) continue;
        if (!importData && e.hit == ImportData) continue;   // a private window has no import row
        const size_t at = Folded(I18n::T(e.title.id)).find(q);
        if (at != std::wstring::npos) titled.push_back({&e, at, q.size()});
        else if (Folded(I18n::T(e.keywords.id)).find(q) != std::wstring::npos) keyed.push_back({&e});
    }
    titled.insert(titled.end(), keyed.begin(), keyed.end());
    if (titled.size() > static_cast<size_t>(Layout::maxResults)) titled.resize(static_cast<size_t>(Layout::maxResults));
    return titled;
}
// Address fields: placeholder, hint, error.
struct FieldText { const wchar_t* placeholder; const wchar_t* hint; const wchar_t* error; };
inline FieldText FieldTexts(int hit) {
    if (hit == SearchField) return {TR(settings_search_placeholder), L"", L""};
    if (hit == CustomSearchField) return {TR(settings_custom_search_placeholder), TR(settings_custom_search_hint), TR(settings_custom_search_error)};
    return {TR(settings_homepage_placeholder), TR(settings_homepage_hint), TR(settings_homepage_error)};
}
inline const wchar_t* NoSitesDescription() { return TR(settings_no_sites_description); }
inline const wchar_t* SitesNote() { return TR(settings_sites_note); }
// « Effacer toutes les annotations… »: the host shows this confirmation (src/modal.h, A6 vocabulary like
// « Supprimer N favoris ? »): it names the count, focus starts on Annuler, the destructive action is never Entrée.
inline Modal::Spec ClearAnnotationsSpec(std::size_t count) {
    Modal::Spec s; s.kind = Modal::Kind::Generic; s.scope = Modal::Scope::Window; s.automationId = L"ClearAnnotations";
    s.title = AnnotationSettings::ClearTitle(count); s.body = AnnotationSettings::ClearBody(count);
    s.buttons = {Modal::Make(Modal::CancelButton, TR(common_cancel), Modal::Role::Secondary, false, true, true, L"DialogCancel"),
                 Modal::Make(Modal::AcceptButton, AnnotationSettings::ClearAction(count), Modal::Role::Danger, true, false, false, L"DialogClearAnnotations")};
    return s;
}

class Drawer {
public:
    struct Version { std::wstring text; bool checking = false, canCheck = false; };
    struct Hooks {
        std::function<void(Motion::Time)> schedule;                 // display-paced frames
        std::function<void(bool)> closed;                           // true when closed from the keyboard
        std::function<void(BrowserSettings::Change)> changed;       // the state changed: apply + save
        std::function<void()> pickFolder;                           // modal folder picker (may change the state)
        std::function<void(bool keyboard)> managePasswords;          // E8: the vault manager (B05)
        std::function<std::pair<bool, std::size_t>()> vaultInfo;     // E8: {vault active here, account count}
        std::function<void(bool keyboard)> clearData;               // modal confirmation + clear; `keyboard`: how the row was activated (focus ring)
        std::function<void(bool keyboard)> importData;              // « Importer depuis un navigateur… » (src/browser-import.h); absent: no row
        std::function<void()> checkUpdates;
        std::function<Version()> version;                           // "browser 0.3.x · À jour"
        std::function<std::wstring()> downloadFolder;               // effective folder (default resolved)
        std::function<bool()> reducedMotion;                        // system || setting
        std::function<void(std::function<void(std::vector<BrowserSettings::Site>)>)> loadSites; // async, UI thread
        std::function<void(const std::wstring&, BrowserSettings::Permission, BrowserSettings::PermissionState)> setPermission;
        std::function<void(const std::wstring&)> resetSites;                                       // empty origin: all
        std::function<int(const std::vector<ContextMenu::Item>&, RECT, bool)> choose;            // modal native menu, 0 = dismissed
        // Réglages › Profils (Paper p-I-0 P02). Empty `profiles` hides the row (private window).
        std::function<std::vector<ProfileItem>()> profiles;
        std::function<Profiles::Profile(const std::wstring&)> profile;                          // full record of one profile
        std::function<bool(const Profiles::Profile&, Profiles::NameError*)> saveProfile;          // name/icon/colour edit
        std::function<Profiles::NameError(const std::wstring& name, const std::wstring& id)> validateProfileName;
        std::function<void(const std::wstring&, bool keyboard)> deleteProfile;                  // P04 confirmation + undo
        std::function<bool()> profilesReadOnly;                                                   // registry not writable: no edit/delete
        // ---- « Annotations » (every hook optional; the section exists only when `annotations` says so).
        // Nothing here touches the store or another app's configuration: the host does, off the UI thread,
        // and answers on the UI thread. See design/annotations-settings.md for the main.cpp wiring.
        std::function<bool()> annotations;                                                         // the annotation mode is compiled in
        std::function<void(std::function<void(AnnotationSettings::Agents)>)> loadAgents;            // detection (async)
        std::function<void(AnnotationSettings::App, bool connect, std::function<void(bool ok, std::wstring message)>)> connectAgent; // explicit click only
        std::function<void()> openConnect;                                                         // the connect modal on its manual fallback
        std::function<std::wstring()> annotationFolder;                                            // the store root (shown, tooltip)
        std::function<void()> openAnnotationFolder;                                                // Explorer on it
        std::function<void(std::function<void(std::size_t)>)> countAnnotations;                    // published + drafts (async)
        std::function<void(std::size_t count, bool keyboard)> clearAnnotations;                    // ClearAnnotationsSpec(count), then clear; call AnnotationsChanged()
        std::function<long long()> now;                                                            // UTC seconds (tests); default time()
        // « Langue » (both optional; no row without saveLanguage). savedLanguage: the preference as stored now;
        // saveLanguage: write it (true only once it is on disk). The process keeps its language until restarted.
        std::function<I18n::Locale()> savedLanguage;
        std::function<bool(I18n::Locale)> saveLanguage;
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, tip_ = nullptr;
    std::vector<UINT_PTR> tips_; std::vector<std::wstring> tipTexts_;
    Chrome chrome_; Chrome* pc_ = &chrome_; Motion::State motion_;   // pc_: the surface painted (chrome_, or an offscreen one for RenderForTest)
    BrowserSettings::State* state_ = nullptr; Hooks hooks_;
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    Lane slide_, plate_, page_;
    static constexpr int MaxDepth = 4;
    std::array<Lane, MaxDepth> scrolls_;       // one per view depth
    std::array<Lane, SwitchCount> switches_, knobPress_;
    std::array<View, MaxDepth> views_{View::Main, View::Main, View::Main, View::Main}; int depth_ = 0; // navigation stack
    // Search: the query typed in the root's field and what it finds.
    std::wstring query_; std::vector<SearchResult> results_; NativeUI::AccentCaret caret_;
    std::vector<BrowserSettings::Site> sites_; bool sitesLoaded_ = false; int site_ = -1; unsigned loadTicket_ = 0;
    // « Annotations »: what the host last said, read when the section (or the root) is shown.
    AnnotationSettings::Agents agents_; bool agentsKnown_ = false; unsigned agentsTicket_ = 0, countTicket_ = 0;
    std::array<std::vector<std::wstring>, AnnotationSettings::AppCount> agentLines_;
    std::size_t annotationCount_ = SIZE_MAX;
    std::map<const char*, ID2D1PathGeometry*> glyphs_;   // logos and the annotation mark (factory resources)
    float heightOverride_ = 0;                            // RenderForTest: the offscreen page's height (DIP)
    // A first permissions load answered within this grace shows the list (or « Aucun site ») directly.
    static constexpr int loadGraceMs = 250; Motion::Time loadStart_{};
    bool LoadQuiet(Motion::Time now) const { return !sitesLoaded_ && now < loadStart_ + Motion::Milliseconds(loadGraceMs); }
    std::map<int, std::vector<std::wstring>> lines_; std::vector<std::wstring> noSitesLines_, sitesNoteLines_;
    // Address fields: reveal lanes, a choice waiting for its address, the edit in progress.
    Lane customReveal_, homeReveal_; bool customPending_ = false, homePending_ = false;
    HWND edit_ = nullptr; int editing_ = 0; RECT editRect_{}; bool editShown_ = false; bool cancelEdit_ = false; HFONT editFont_ = nullptr; UINT editDpi_ = 0;
    HBRUSH editBrush_ = nullptr; std::map<int, std::wstring> drafts_; std::map<int, bool> errors_;
    BrowserSettings::Autoplay launchAutoplay_ = BrowserSettings::Autoplay::Muted; bool launchCookies_ = false, launchGrayscale_ = false; // engine flags of this run
    int hover_ = 0, pressed_ = 0, focus_ = 0;
    I18n::Locale languageSaved_ = I18n::Current(); bool languageFailed_ = false;   // « Langue »: preference as saved, last save failed
    // Profiles: the list as last read, the profile open in the detail view and its editor block.
    std::vector<ProfileItem> profiles_; int profileIndex_ = -1; std::wstring profileId_; ProfileEditor::Editor editor_;
    std::vector<std::wstring> profileNoteLines_, lastProfileNoteLines_;
    bool LastProfile() const { return profiles_.size() < 2; }
    bool ProfilesReadOnly() const { return hooks_.profilesReadOnly && hooks_.profilesReadOnly(); }
    bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false, modal_ = false, animatingPainted_ = false, spinning_ = false;
    int appliedVisible_ = -1; RECT appliedOwner_{}, appliedWindow_{};
    Motion::Time spinStart_{}, landStart_{};
    // Update spinner: a constant counter-clockwise turn while a check runs. When
    // it ends, the glyph keeps its speed and eases into the next full turn (cubic
    // Hermite: the spin's velocity in, zero out), so it settles on its rest pose.
    bool landing_ = false; float landFrom_ = 0, landDistance_ = 0, landSeconds_ = 0;
    static constexpr float spinSpeed = 360.f * 1000 / Timing::spin.count(); // degrees per second
    float Seconds(Motion::Time from, Motion::Time to) const { return static_cast<float>(std::chrono::duration<double>(to - from).count()); }
    bool Landing(Motion::Time now) const { return landing_ && Seconds(landStart_, now) < landSeconds_; }
    bool Spinning(Motion::Time now) const { return !Reduced() && (spinning_ || Landing(now)); }
    float SpinDegrees(Motion::Time now) const {
        if (spinning_) return std::fmod(Seconds(spinStart_, now) * spinSpeed, 360.f);
        if (!Landing(now)) return 0;
        const float u = Motion::Clamp(Seconds(landStart_, now) / landSeconds_), u2 = u * u, u3 = u2 * u;
        return std::fmod(landFrom_ + (3 * u2 - 2 * u3) * landDistance_ + (u - 2 * u2 + u3) * spinSpeed * landSeconds_, 360.f);
    }
    void SetChecking(bool checking, Motion::Time now) {
        if (checking && !spinning_) { spinStart_ = now - std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(SpinDegrees(now) / spinSpeed)); landing_ = false; }
        else if (!checking && spinning_ && !Reduced()) {
            landFrom_ = SpinDegrees(now); landDistance_ = 360 - landFrom_;
            if (landDistance_ < 60) landDistance_ += 360; // too close to rest to brake smoothly: one more turn
            landSeconds_ = std::clamp(2 * landDistance_ / spinSpeed, .25f, .7f); landStart_ = now; landing_ = true;
        }
        spinning_ = checking;
    }
    bool* SwitchField(int hit) const {
        if (!state_) return nullptr;
        switch (hit) {
        case RestoreTabs: return &state_->restoreTabs; case AskWhere: return &state_->askWhereToSave; case ReduceMotion: return &state_->reduceMotion;
        case Suggestions: return &state_->searchSuggestions; case Passwords: return &state_->passwords; case Autofill: return &state_->autofill;
        case ClearOnExit: return &state_->clearOnExit; case ConfirmClose: return &state_->confirmCloseTabs; case CloseWithLastTab: return &state_->closeWithLastTab;
        case AdBlock: return &state_->adBlock; case HttpsOnly: return &state_->httpsOnly; case ThirdPartyCookies: return &state_->blockThirdPartyCookies;
        case MemorySaver: return &state_->memorySaver; case MuteBackground: return &state_->muteBackground;
        case GrayscaleText: return &state_->grayscaleText; default: return nullptr;
        }
    }
    static BrowserSettings::Change SwitchChange(int hit) {
        using C = BrowserSettings::Change;
        switch (hit) {
        case RestoreTabs: return C::RestoreTabs; case AskWhere: return C::AskWhereToSave; case ReduceMotion: return C::ReduceMotion;
        case Suggestions: return C::SearchSuggestions; case Passwords: return C::Passwords; case Autofill: return C::Autofill;
        case ConfirmClose: return C::ConfirmCloseTabs; case CloseWithLastTab: return C::CloseWithLastTab; case AdBlock: return C::AdBlock;
        case HttpsOnly: return C::HttpsOnly; case ThirdPartyCookies: return C::ThirdPartyCookies; case MemorySaver: return C::MemorySaver;
        case MuteBackground: return C::MuteBackground; case GrayscaleText: return C::GrayscaleText;
        case AnnotationPrompt: return C::AnnotationConnectPrompt; default: return C::ClearOnExit;
        }
    }
    // « Proposer de connecter un agent » shows whether the modal would still open by itself (BrowserSettings::ConnectPromptActive).
    bool SwitchValue(int hit) const { if (hit == AnnotationPrompt) return state_ && BrowserSettings::ConnectPromptActive(*state_); auto* f = SwitchField(hit); return f && *f; }
    bool AnnotationsOn() const { return hooks_.annotations && hooks_.annotations(); }
    long long NowSeconds() const { return hooks_.now ? hooks_.now() : static_cast<long long>(std::time(nullptr)); }
    bool Reduced() const { return motion_.ReducedMotion(); }
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { if (heightOverride_ > 0) return heightOverride_; RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    View Current() const { return views_[depth_]; }
    int DepthOf(View v) const { for (int d = depth_; d >= 0; --d) if (views_[d] == v) return d; return -1; }
    bool Searching() const { return !BrowserSettings::TrimText(query_).empty(); }
    // Footer height per view (DIP): main = clear + version; sites = note + "Tout retirer"; site = one danger row.
    float FooterHeight(View v) const {
        if (v == View::Profile) { const auto& note = LastProfile() ? lastProfileNoteLines_ : profileNoteLines_; return 1 + 12 + note.size() * Layout::descriptionLine + 8 + Layout::row + Layout::padBottom; }
        return v == View::Sites ? 1 + 12 + 40 + 8 + Layout::row + Layout::padBottom : v == View::Site ? 1 + 12 + Layout::row + Layout::padBottom : Layout::footer;
    }
    float ViewBottom(View v) const { return Height() - FooterHeight(v); }
    LayoutInput Input() const {
        LayoutInput in; in.lines = &lines_;
        if (state_) { std::wstring f = FolderText(hooks_.downloadFolder ? hooks_.downloadFolder() : state_->downloadFolder); if (f.rfind(L"…\\", 0) == 0) f = f.substr(2); in.folderName = f; } in.noSitesLines = noSitesLines_;   // i18n-exempt: path elision mark
        in.customReveal = customRevealValue_; in.homeReveal = homeRevealValue_;
        in.language = static_cast<bool>(hooks_.saveLanguage); in.languageSaved = languageSaved_; in.languageFailed = languageFailed_;
        in.autoplayPending = state_ && state_->autoplay != launchAutoplay_;
        in.cookiesPending = state_ && state_->blockThirdPartyCookies != launchCookies_;
        in.grayscalePending = state_ && state_->grayscaleText != launchGrayscale_;
        in.profiles = &profiles_; in.editorHeight = editor_.Height();
        in.state = state_; in.sites = &sites_; in.sitesLoaded = sitesLoaded_; in.sitesQuiet = LoadQuiet(Motion::Now()); in.site = site_;
        in.annotations = AnnotationsOn(); in.agents = &agents_; in.agentsKnown = agentsKnown_; in.agentLines = agentLines_; in.now = NowSeconds();
        in.annotationCount = annotationCount_; if (in.annotations && hooks_.annotationFolder) in.annotationFolder = FolderText(hooks_.annotationFolder());
        in.importData = static_cast<bool>(hooks_.importData);
        if (hooks_.vaultInfo) { const auto v = hooks_.vaultInfo(); in.vaultManager = v.first; in.vaultCount = v.second; } return in;
    }
    mutable float customRevealValue_ = 0, homeRevealValue_ = 0;
    void Reveals(Motion::Time now, bool target) const {
        customRevealValue_ = target ? customReveal_.Target() : customReveal_.Value(now);
        homeRevealValue_ = target ? homeReveal_.Target() : homeReveal_.Value(now);
    }
    std::vector<Slot> Slots(View v, Motion::Time now, bool target = false) const {
        const int d = std::max(0, DepthOf(v)); Reveals(now, target);
        return ContentSlots(v, Input(), target ? scrolls_[d].Target() : scrolls_[d].Value(now));
    }
    float MaxScroll(View v, Motion::Time now, bool target) const {
        float bottom = 0; Reveals(now, target); ContentSlots(v, Input(), 0, &bottom);
        return std::max(0.f, bottom + Layout::groupGap - ViewBottom(v));
    }
    int SelectedIndex() const { return state_ ? static_cast<int>(state_->engine) : 0; }
    // Selection plate: content y of the selected row (animated), unscrolled.
    float EngineTop(int index) const {
        Reveals(Motion::Now(), true);
        for (const auto& s : ContentSlots(View::Engine, Input(), 0)) if (s.kind == Kind::Engine && s.index == index) return s.top;
        return Layout::viewTop + Layout::label + Layout::rowGap; // hidden engine: tucked under the first row
    }
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void PushOpacity(float opacity) {
        ID2D1Layer* layer = nullptr; auto* t = pc_->RenderTarget();
        if (FAILED(t->CreateLayer(&layer))) return;
        t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer);
        layer->Release();
    }
    void Fill(Design::Box b, float radius, float r, float g, float bl, float a) {
        auto* brush = pc_->SolidBrush(); brush->SetColor(D2D1::ColorF(r, g, bl, a));
        pc_->RenderTarget()->FillRoundedRectangle(D2D1::RoundedRect({b.left, b.top, b.right, b.bottom}, radius, radius), brush);
    }
    void White(Design::Box b, float radius, float a) { if (a > .001f) Fill(b, radius, 1, 1, 1, a); }
    static D2D1_RECT_F R(Design::Box b) { return {b.left, b.top, b.right, b.bottom}; }
    Motion::InteractionVisual Interaction(int hit, Motion::Time now) const { return motion_.Visual(hit, now); }
    // Balanced wrap (CSS text-wrap: balance): the greedy line count, at the
    // narrowest width that keeps it, so lines come out even instead of a short last one.
    std::vector<std::wstring> Wrap(const std::wstring& text, float width) {
        auto lines = Greedy(text, width);
        if (lines.size() < 2) return lines;
        float lo = width / lines.size(), hi = width;
        for (int k = 0; k < 10 && hi - lo > .5f; ++k) { const float mid = (lo + hi) / 2; if (Greedy(text, mid).size() == lines.size()) hi = mid; else lo = mid; }
        return Greedy(text, hi);
    }
    std::vector<std::wstring> Greedy(const std::wstring& text, float width) {
        std::vector<std::wstring> lines; std::wstring line, word;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != L' ') { word += text[i]; continue; }
            if (word.empty()) continue;
            const std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && pc_->TextWidth(candidate, 14, false, false, -.14f) > width) { lines.push_back(line); line = word; } else line = candidate;
            word.clear();
        }
        if (!line.empty()) lines.push_back(line);
        return lines;
    }
    void MeasureDescriptions() {
        if (!pc_->RenderTarget()) pc_->Resize();
        lines_.clear();
        for (const auto& [hit, text] : Descriptions) lines_[hit] = Wrap(I18n::T(text.id), Layout::descriptionWidth);
        noSitesLines_ = Wrap(NoSitesDescription(), Layout::rowRight - 8 - Layout::textLeft);
        sitesNoteLines_ = Wrap(SitesNote(), Layout::rowRight - Layout::textLeft - 4);
        profileNoteLines_ = Wrap(TR(settings_profile_delete_note), Layout::rowRight - Layout::textLeft - 4);
        lastProfileNoteLines_ = Wrap(TR(settings_last_profile_note), Layout::rowRight - Layout::textLeft - 4);
        lines_[ShortcutsNoteKey] = Wrap(AnnotationSettings::ShortcutsNote(), Layout::rowRight - Layout::textLeft);
        MeasureAgents();
    }
    // Agent status lines: re-wrapped whenever the state or the clock (« il y a … ») may have changed them.
    void MeasureAgents() {
        if (!pc_->RenderTarget()) pc_->Resize();
        const long long now = NowSeconds();
        for (int i = 0; i < AnnotationSettings::AppCount; ++i)
            agentLines_[static_cast<size_t>(i)] = Wrap(AnnotationSettings::StatusText(agents_[AnnotationSettings::AppAt(i)], now), Layout::statusWidth);
    }
    // A vector glyph (Paper path, its own viewBox) fitted in `box` at `size` DIP.
    void PaintGlyph(const AnnotationSettings::Glyph& g, Design::Box box, float size, D2D1_COLOR_F color) {
        auto* t = pc_->RenderTarget(); auto* brush = pc_->SolidBrush(); if (!t || !brush) return;
        auto& geometry = glyphs_[g.d];
        if (!geometry) { ID2D1Factory* f = nullptr; t->GetFactory(&f); geometry = DialogKit::SvgGeometry(f, g.d); if (f) f->Release(); }
        if (!geometry) return;
        const float k = size / std::max(g.w, g.h), x = (box.left + box.right) / 2 - (g.x + g.w / 2) * k, y = (box.top + box.bottom) / 2 - (g.y + g.h / 2) * k;
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * base);
        brush->SetColor(color); t->FillGeometry(geometry, brush); t->SetTransform(base);
    }

    // Ghost icon button (14 DIP glyph at 70%, shared button radius): fills and brightens on hover.
    void IconButton(const wchar_t* icon, Design::Box box, int hit, Motion::Time now, float rotation = 0, bool enabled = true) {
        auto* t = pc_->RenderTarget(); const auto v = Interaction(hit, now);
        const float h = enabled ? v.hover : 0, p = enabled ? v.press : 0;
        if (h > 0 || p > 0) {
            const float c = Design::Surface::canvas, hovered = c + (Design::Surface::hover - c) * h, fill = hovered + (Design::Surface::pressed - hovered) * p;
            pc_->Round(R(box), Design::Radius::button, fill);
        }
        if (focus_ == hit) pc_->FocusRing(R(box), Design::Radius::button);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        if (rotation != 0) t->SetTransform(D2D1::Matrix3x2F::Rotation(rotation, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
        // Full-opacity 14 px masks (the regular icons bake in 50% at 16 px): Paper's exact 70%, crisp bold strokes.
        const float size = 14 * Motion::PressScale(p, Reduced(), enabled), a = enabled ? .7f + .3f * h : Design::Text::disabled;
        pc_->TintedIcon((std::wstring(icon) + L"-mask").c_str(), R(box), size, D2D1::ColorF(1, 1, 1, a));
        t->SetTransform(base);
    }
    // Opaque equivalent of `amount` white over the canvas: row fills never stack
    // translucent layers where the selection plate crosses a hovered row.
    static float Tint(float amount) { const float c = Design::Surface::canvas; return c + (1 - c) * amount; }
    static constexpr float hoverTint = .04f, pressTint = .03f, plateTint = .04f;
    void RowSurface(Design::Box box, int hit, Motion::Time now) {
        if (!hit) return;
        const auto v = Interaction(hit, now); const float amount = hoverTint * v.hover + pressTint * v.press;
        if (amount > .0005f) pc_->Round(R(box), Design::Radius::control, Tint(amount));
    }
    void RowFocus(Design::Box box, int hit) { if (focus_ == hit) pc_->FocusRing(R(box), Design::Radius::control); }
    void Label16(const std::wstring& text, float left, float top, float right, float opacity) { pc_->Text(text, {left, top, right, top + 20}, 16, opacity, false, false, -.16f); }
    void Label14(const std::wstring& text, float left, float top, float right, float opacity, float height = 18) { pc_->Text(text, {left, top, right, top + height}, 14, opacity, false, false, -.14f); }
    // The engine's own favicon (assets\engines\<key>.png), dimmed like its name until selected.
    void EngineTile(int index, float y, float strength) {
        const auto box = Layout::Icon(y);
        if (static_cast<Engine>(index) == Engine::Custom) { pc_->TintedIcon(L"favicon-mask", R(box), 16, D2D1::ColorF(1, 1, 1, .6f + .3f * strength)); return; }
        const std::string key = BrowserSettings::Info(static_cast<Engine>(index)).key;
        if (auto* favicon = pc_->AssetBitmap(L"engines\\" + std::wstring(key.begin(), key.end()) + L".png")) {
            pc_->RenderTarget()->DrawBitmap(favicon, R(box), .75f + .25f * strength, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
        White(box, 4, .7f + .3f * strength);
    }
    void EngineRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), s.hit);
        const float plateTop = plate_.Value(now), closeness = Motion::Clamp(1 - std::abs(plateTop - (s.top + paintScroll_)) / Layout::pitch);
        EngineTile(s.index, s.top, closeness);
        pc_->Text(BrowserSettings::Info(static_cast<Engine>(s.index)).name, {Layout::nameLeft, s.top, Layout::slotRight - Layout::slot - 4, s.top + Layout::row}, 16, .7f + .3f * closeness, false, false, -.16f);
    }
    float paintScroll_ = 0; // scroll of the view being painted (the plate rides on it)
    // Leading glyph of a root row: full-opacity 16 DIP mask at Paper's 70%, brighter on hover.
    void Lead(const Slot& s, float hover) {
        if (s.icon) pc_->TintedIcon(s.icon, R(Layout::Icon(s.top)), 16, D2D1::ColorF(1, 1, 1, .7f + .2f * hover));
        else if (s.path) pc_->PathGlyph(s.path, R(Layout::Icon(s.top)), 16, D2D1::ColorF(1, 1, 1, .7f + .2f * hover));
        else if (s.glyph) PaintGlyph(*s.glyph, Layout::Icon(s.top), 14, D2D1::ColorF(1, 1, 1, .7f + .2f * hover)); // the mark at the masks' optical size
    }
    // « Annotations » › Agents: logo, name, status; the action as a compact raised button.
    float ActionWidth(const std::wstring& label) const { return std::ceil(pc_->TextWidth(label, 14, false, false, -.14f)) + 2 * Layout::actionPad; }
    Design::Box AgentAction(const Slot& s) const { return Layout::Action(s.top, ActionWidth(s.value)); }
    void AgentRow(const Slot& s, Motion::Time now) {
        namespace AS = AnnotationSettings; const auto app = AS::AppAt(s.index);
        const auto& logo = AS::Logo(app); const Design::Box icon = Layout::Icon(s.top + Layout::twoLinePad, 20);
        if (app == AS::App::Claude) PaintGlyph(logo, icon, 16, D2D1::ColorF(AS::ClaudeRgb));
        else PaintGlyph(logo, icon, 16, D2D1::ColorF(1, 1, 1, .9f));
        const bool action = !s.value.empty(); const auto box = action ? AgentAction(s) : Design::Box{Layout::slotRight, 0, Layout::slotRight, 0};
        Label16(s.text, Layout::nameLeft, s.top + Layout::twoLinePad, box.left - 8, .9f);
        if (s.error) pc_->ink = D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, 1);
        for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], Layout::nameLeft, s.top + Layout::twoLinePad + 22 + k * Layout::descriptionLine, Layout::rowRight - 8, s.error ? 1.f : .5f);
        pc_->ink = D2D1::ColorF(1, 1, 1, 1);
        if (!action) return;
        const auto v = Interaction(s.hit, now); const bool enabled = !s.disabled;
        const auto style = Design::Resolve(Design::ButtonVariant::Raised, {enabled ? v.hover : 0, enabled ? v.press : 0, !enabled, focus_ == s.hit});
        auto* t = pc_->RenderTarget(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        const float scale = Motion::PressScale(enabled ? v.press : 0, Reduced(), enabled);
        if (scale != 1) t->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
        pc_->Round(R(box), style.radius, style.surface);
        pc_->Text(s.value, {box.left + Layout::actionPad - 1, box.top + 2, box.right - Layout::actionPad + 1, box.top + 22}, 14, enabled ? .9f : Design::Text::disabled, false, false, -.14f);
        if (focus_ == s.hit) pc_->FocusRing(R(box), Design::Radius::button);
        t->SetTransform(base);
    }
    void SwitchRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, s.height), s.hit);
        const int i = SwitchIndex(s.hit); const float on = Motion::Clamp(switches_[i].Value(now));
        const auto track = Layout::Switch(s.top, s.height);
        if (s.lines.empty()) Label16(s.text, Layout::textLeft, s.top + (s.height - 20) / 2, track.left - 16, .7f + .3f * on);
        else {
            // Two-line row (Paper): label 16 DIP, description 14 DIP at 50%, 2 DIP apart, 8 DIP padding.
            Label16(s.text, Layout::textLeft, s.top + Layout::twoLinePad, track.left - 16, .7f + .3f * on);
            for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], Layout::textLeft, s.top + Layout::twoLinePad + 22 + k * Layout::descriptionLine, track.left - 8, .5f);
        }
        // Track: 10% white off, 70% on. Knob: 70% white off, the canvas on.
        const auto v = Interaction(s.hit, now);
        White(track, Layout::switchHeight / 2, .10f + .60f * on + .05f * v.hover * (1 - on));
        const float c = Design::Surface::canvas, offTrack = c + (1 - c) * .10f, offKnob = offTrack + (1 - offTrack) * .7f;
        const float knobGray = offKnob + (c - offKnob) * on;
        const float stretch = Reduced() ? 0 : Layout::knobStretch * Motion::Clamp(knobPress_[i].Value(now));
        const float travel = Layout::switchWidth - 2 * Layout::knobInset - Layout::knob;
        float left = track.left + Layout::knobInset + travel * on - stretch * on;
        const Design::Box knob{left, track.top + Layout::knobInset, left + Layout::knob + stretch, track.top + Layout::knobInset + Layout::knob};
        Fill(knob, Layout::knob / 2, knobGray, knobGray, knobGray, 1);
    }
    // Label + trailing value on one line (value and nav rows): where each ends. The value is right-aligned
    // before the chevron (8 DIP), the label stops 8 DIP before the value (and its glyph).
    struct TrailingSpan { float labelLeft, labelRight, valueLeft, valueRight; };
    TrailingSpan Trailing(const Slot& s) const {
        const bool counted = s.hit == Permissions; const float right = Layout::Chevron(s.top).left;
        float w = pc_->TextWidth(s.value, 14, counted, false, counted ? 0.f : -.14f);
        TrailingSpan t; t.labelLeft = (s.icon || s.path || s.glyph) ? Layout::nameLeft : Layout::textLeft;
        // The label keeps its natural width; the value (a profile name, a folder) gets the rest and trims. When even
        // that leaves under 48 DIP, the value keeps 48 and the label trims instead.
        const float labelW = pc_->TextWidth(s.text, 16, false, false, -.16f);
        w = std::min(w, std::max(std::min(w, 48.f), right - 8 - (s.valueGlyph ? 16 + 6 : 0) - 8 - t.labelLeft - labelW));
        t.valueRight = right - 8; t.valueLeft = t.valueRight - w; t.labelRight = right - w - (s.valueGlyph ? 16 + 6 : 0) - 16; return t;
    }
    // Label + current value + chevron: opens its list of choices (native menu).
    void ValueRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, s.height), s.hit);
        const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top);
        const auto t = Trailing(s); const float w = t.valueRight - t.valueLeft;
        Lead(s, v.hover);
        Label16(s.text, t.labelLeft, s.top + 6, t.labelRight, .7f + .3f * v.hover);
        pc_->Text(s.value, {chevron.left - 8 - w - 1, s.top + 6, chevron.left - 8 + 1, s.top + 26}, 14, .5f + .2f * v.hover, false, false, -.14f);
        pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        // A pending note (applied at the next launch) or a save error under the label: aligned with the label
        // (x 44 when the row has a glyph, Paper p-K-0), danger ink for an error.
        const float noteLeft = (s.icon || s.path || s.glyph) ? Layout::nameLeft : Layout::textLeft;
        if (s.error) pc_->ink = D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, 1);
        for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], noteLeft, s.top + 6 + 22 + k * Layout::descriptionLine, Layout::rowRight - 8, s.error ? 1.f : .5f);
        pc_->ink = D2D1::ColorF(1, 1, 1, 1);
    }
    // Address field: the value (or its placeholder) in a quiet well, a hint under it.
    // While editing, a native EDIT sits on the text and this paints the frame around it.
    void FieldRow(const Slot& s, Motion::Time now) {
        const auto v = Interaction(s.hit, now); const bool editing = editing_ == s.hit, error = errors_[s.hit];
        const Design::Box box{Layout::rowLeft, s.top, Layout::rowRight, s.top + Layout::field};
        pc_->Round(R(box), Design::Radius::control, Tint(FieldFill(v.hover, editing)));
        auto* brush = pc_->SolidBrush(); auto* t = pc_->RenderTarget();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        brush->SetColor(error ? D2D1::ColorF(1, .482f, .431f, .5f) : D2D1::ColorF(1, 1, 1, editing ? .2f : .06f + .04f * v.hover));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(box.left + px / 2, box.top + px / 2, box.right - px / 2, box.bottom - px / 2), Design::Radius::control, Design::Radius::control), brush, px);
        if (focus_ == s.hit && !editing) pc_->FocusRing(R(box), Design::Radius::control);
        const auto texts = FieldTexts(s.hit);
        if (!editing) {
            const std::wstring value = FieldValue(s.hit);
            if (value.empty()) Label14(texts.placeholder, Layout::textLeft, s.top + 7, Layout::rowRight - 8, .35f);
            else pc_->Text(value, {Layout::textLeft, s.top + 7, Layout::rowRight - 8, s.top + 25}, 14, .9f, false, false, -.14f);
        }
        const float hint = s.top + Layout::field + Layout::fieldHint;
        if (error) { pc_->ink = D2D1::ColorF(1, .482f, .431f, 1); Label14(texts.error, Layout::textLeft, hint, Layout::rowRight, 1); pc_->ink = D2D1::ColorF(1, 1, 1, 1); }
        else Label14(texts.hint, Layout::textLeft, hint, Layout::rowRight, .5f);
    }
    static float FieldFill(float hover, bool editing) { return editing ? .02f : .03f + .02f * hover; }
    // Root search (Paper "Rechercher un réglage"): a 4% well; focused, a 10% hairline;
    // a typed query shows a clear button. The EDIT sits on the text while editing.
    static constexpr float searchFill = .04f;
    void SearchRow(const Slot& s, Motion::Time now) {
        const auto v = Interaction(s.hit, now); const bool editing = editing_ == SearchField;
        const Design::Box box{Layout::rowLeft, s.top, Layout::rowRight, s.top + Layout::field};
        pc_->Round(R(box), Design::Radius::control, Tint(searchFill + .02f * v.hover * (editing ? 0 : 1)));
        auto* brush = pc_->SolidBrush(); auto* t = pc_->RenderTarget();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        if (editing) { brush->SetColor(D2D1::ColorF(1, 1, 1, .1f)); t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(box.left + px / 2, box.top + px / 2, box.right - px / 2, box.bottom - px / 2), Design::Radius::control, Design::Radius::control), brush, px); }
        if (focus_ == s.hit && !editing) pc_->FocusRing(R(box), Design::Radius::control);
        const Design::Box glass{box.left + 8, s.top + 9, box.left + 22, s.top + 23};
        pc_->TintedIcon(L"search-mask", R(glass), 14, D2D1::ColorF(1, 1, 1, editing || Searching() ? .7f : .5f));
        if (!editing) {
            if (query_.empty()) Label14(FieldTexts(SearchField).placeholder, SearchTextLeft(), s.top + 7, Layout::rowRight - 8, .4f);
            else pc_->Text(query_, {SearchTextLeft(), s.top + 7, ClearBox(s.top).left - 4, s.top + 25}, 14, .9f, false, false, -.14f);
        }
        if (Searching()) pc_->TintedIcon(L"close-mask", R(ClearBox(s.top)), 12, D2D1::ColorF(1, 1, 1, .5f + .3f * Interaction(SearchDismiss, now).hover));
    }
    static constexpr float SearchTextLeft() { return Layout::rowLeft + 8 + 14 + 8; } // 42
    static Design::Box ClearBox(float top) { return {Layout::rowRight - 4 - 20, top + 4, Layout::rowRight - 4, top + 28}; }
    // Results card: opaque (#202020, the field's own 4%), 6 DIP under the field, over the dimmed root.
    Design::Box ResultCard(float fieldTop) const {
        const float top = fieldTop + Layout::field + Layout::resultGap;
        return {Layout::rowLeft, top, Layout::rowRight, top + std::max<size_t>(1, results_.size()) * Layout::resultRow};
    }
    void ResultCardPaint(float fieldTop, Motion::Time now) {
        const auto card = ResultCard(fieldTop);
        pc_->Round(R(card), Design::Radius::control, Tint(searchFill));
        if (results_.empty()) { Label14(TRF(settings_search_no_result, BrowserSettings::TrimText(query_)), card.left + Layout::resultPadLeft, card.top + 19, card.right - Layout::resultPadRight, .5f); return; }
        for (size_t i = 0; i < results_.size(); ++i) {
            const auto& r = results_[i]; const int hit = ResultBase + static_cast<int>(i);
            const Design::Box row{card.left, card.top + i * Layout::resultRow, card.right, card.top + (i + 1) * Layout::resultRow};
            const auto v = Interaction(hit, now);
            if (v.hover > 0 || v.press > 0) pc_->Round(R(row), Design::Radius::control, Tint(searchFill + .04f * v.hover + .03f * v.press));
            if (focus_ == hit) pc_->FocusRing(R(row), Design::Radius::control);
            const int sw = SwitchIndex(r.entry->hit);
            const float right = row.right - Layout::resultPadRight - (sw >= 0 ? Layout::switchWidth : 16) - 16, x = row.left + Layout::resultPadLeft, y = row.top + 8;
            // Title at 70%, the matched span in full white.
            const std::wstring title = I18n::T(r.entry->title.id);
            if (r.at == std::wstring::npos) Label16(title, x, y, right, .7f + .2f * v.hover);
            else {
                const std::wstring a = title.substr(0, r.at), m = title.substr(r.at, r.length), b = title.substr(r.at + r.length);
                const float wa = pc_->TextWidth(a, 16, false, false, -.16f), wm = pc_->TextWidth(m, 16, false, false, -.16f);
                Label16(a, x, y, x + wa + 1, .7f + .2f * v.hover); Label16(m, x + wa, y, x + wa + wm + 1, 1); Label16(b, x + wa + wm, y, right, .7f + .2f * v.hover);
            }
            Label14(I18n::T(r.entry->path.id), x, y + 22, right, .5f);
            if (sw >= 0) {
                // The switch works from the result itself.
                const float on = Motion::Clamp(switches_[sw].Value(now));
                const Design::Box track{row.right - Layout::resultPadRight - Layout::switchWidth, row.top + (Layout::resultRow - Layout::switchHeight) / 2, row.right - Layout::resultPadRight, row.top + (Layout::resultRow + Layout::switchHeight) / 2};
                const float base = Tint(searchFill), offTrack = base + (1 - base) * .10f, onTrack = base + (1 - base) * .70f;
                const float trackGray = offTrack + (onTrack - offTrack) * on;
                Fill(track, Layout::switchHeight / 2, trackGray, trackGray, trackGray, 1);
                const float offKnob = offTrack + (1 - offTrack) * .7f, c = Design::Surface::canvas, knobGray = offKnob + (c - offKnob) * on;
                const float travel = Layout::switchWidth - 2 * Layout::knobInset - Layout::knob, left = track.left + Layout::knobInset + travel * on;
                Fill({left, track.top + Layout::knobInset, left + Layout::knob, track.top + Layout::knobInset + Layout::knob}, Layout::knob / 2, knobGray, knobGray, knobGray, 1);
            } else {
                auto* t = pc_->RenderTarget(); const Design::Box chevron{row.right - Layout::resultPadRight - 16, row.top + (Layout::resultRow - 24) / 2, row.right - Layout::resultPadRight, row.top + (Layout::resultRow + 24) / 2};
                D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
                pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover); t->SetTransform(base);
            }
        }
    }
    // Label + count + right chevron: pushes a sub-view.
    void NavRow(const Slot& s, Motion::Time now) {
        if (!s.hit) { // inert: label + why, no chevron, no hover
            Label16(s.text, Layout::textLeft, s.top + Layout::twoLinePad, Layout::rowRight - 8, .5f);
            for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], Layout::textLeft, s.top + Layout::twoLinePad + 22 + k * Layout::descriptionLine, Layout::rowRight - 8, .4f);
            return;
        }
        RowFocus(Layout::Row(s.top), s.hit);
        auto* t = pc_->RenderTarget(); const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top);
        const bool counted = s.hit == Permissions; // a count reads in tabular figures; a summary in text
        const auto tr = Trailing(s); const float w = tr.valueRight - tr.valueLeft;
        Lead(s, v.hover);
        Label16(s.text, tr.labelLeft, s.top + 6, tr.labelRight, .7f + .3f * v.hover);
        if (!s.value.empty()) pc_->Text(s.value, {chevron.left - 8 - w - 1, s.top + 6, chevron.left - 8 + 1, s.top + 26}, 14, .5f + .2f * v.hover, counted, false, counted ? 0.f : -.14f);
        if (s.valueGlyph) { const float x = chevron.left - 8 - w - 6 - 16; pc_->PathGlyph(s.valueGlyph, {x, s.top + 8, x + 16, s.top + 24}, 16, D2D1::ColorF(s.ink.r, s.ink.g, s.ink.b, 1)); }
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
        pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        t->SetTransform(base);
    }
    // A folder row: « Enregistrer dans » (downloads) or « Ouvrir le dossier des annotations » (s.text / s.value).
    void FolderRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, Layout::folderRow), s.hit);
        Label16(s.text.empty() ? std::wstring(TR(settings_save_in)) : s.text, Layout::textLeft, s.top + 8, Layout::Trailing(s.top).left - 8, .7f);
        const std::wstring folder = s.hit != Folder ? s.value : FolderText(hooks_.downloadFolder ? hooks_.downloadFolder() : (state_ ? state_->downloadFolder : L""));
        pc_->Text(folder, {Layout::textLeft, s.top + 30, Layout::Trailing(s.top).left - 8, s.top + 46}, 14, .5f, false, false, -.14f);
        // Closed at rest, it opens on hover: cross-fade on the row's own hover tween.
        const auto v = Interaction(s.hit, now); const auto box = Layout::Trailing(s.top, Layout::folderRow);
        const float h = Motion::Clamp(v.hover);
        pc_->FolderGlyph(R(box), 14 * Motion::PressScale(v.press, Reduced()), .7f + .3f * h, h, Reduced());
    }
    void SiteRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, s.height), s.hit);
        auto* t = pc_->RenderTarget(); const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top, s.height);
        pc_->Icon(L"favicon", R(Layout::Icon(s.top + 8, 20)), 16, .6f + .3f * v.hover);
        Label16(s.text, Layout::nameLeft, s.top + 8, chevron.left - 8, .8f + .2f * v.hover);
        Label14(s.value, Layout::nameLeft, s.top + 30, chevron.left - 8, .5f, 16);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
        pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        t->SetTransform(base);
    }
    // Profile row (Paper XSN-0): one line, 40 high; hover or this window's profile: its ink at 10 %;
    // glyph in its ink, name 16 (white when current or hovered, else 70 %), check lane 16, chevron 24.
    void ProfileRow(const Slot& s, Motion::Time now) {
        auto* t = pc_->RenderTarget(); const auto v = Interaction(s.hit, now); const auto row = Layout::Row(s.top, s.height);
        const float lit = std::max(s.current ? 1.f : 0.f, v.hover);
        if (lit > 0 || v.press > 0) Fill(row, Design::Radius::r8, s.ink.r, s.ink.g, s.ink.b, .10f * lit + .06f * v.press);
        if (focus_ == s.hit) pc_->FocusRing(R(row), Design::Radius::r8);
        const float cy = s.top + s.height / 2;
        if (s.path) pc_->PathGlyph(s.path, {Layout::textLeft, cy - 8, Layout::textLeft + 16, cy + 8}, 16, D2D1::ColorF(s.ink.r, s.ink.g, s.ink.b, 1));
        const Design::Box chevron{Layout::rowRight - 4 - 24, cy - 12, Layout::rowRight - 4, cy + 12};
        const Design::Box check{chevron.left - 8 - 16, cy - 8, chevron.left - 8, cy + 8};
        Label16(s.text, Layout::textLeft + 16 + 8, cy - 10, check.left - 8, s.current ? 1.f : .7f + .3f * v.hover);
        if (s.current) pc_->Icon(L"check", R(check), 14, .9f);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
        pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        t->SetTransform(base);
    }
    void Plate(Motion::Time now, float scroll) {
        // One plate for the selection: it glides between rows, the check rides on it.
        const float y = plate_.Value(now) - scroll;
        pc_->Round(R(Layout::Row(y)), Design::Radius::control, Tint(plateTint)); // opaque: covers a hover fill instead of adding to it
        pc_->Icon(L"check", R(Layout::Trailing(y)), 14, .9f);
    }
    void DangerRow(const wchar_t* text, float top, int hit, Motion::Time now, bool enabled) {
        const auto v = Interaction(hit, now); const auto row = Layout::Row(top);
        if (enabled && (v.hover > 0 || v.press > 0)) Fill(row, Design::Radius::control, 1, .482f, .431f, .08f * v.hover + .05f * v.press);
        if (focus_ == hit) pc_->FocusRing(R(row), Design::Radius::control);
        pc_->ink = D2D1::ColorF(1, .482f, .431f, 1);
        Label16(text, Layout::textLeft, top + 6, Layout::rowRight, enabled ? 1.f : Design::Text::disabled);
        pc_->ink = D2D1::ColorF(1, 1, 1, 1);
    }
    void DangerContent(const Slot& s, Motion::Time now) { DangerRow(s.text.c_str(), s.top, s.hit, now, !s.disabled); }
    void Footer(View view, Motion::Time now) {
        const float h = Height(), top = h - FooterHeight(view);
        auto* t = pc_->RenderTarget(); auto* brush = pc_->SolidBrush();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        pc_->Color(1, .04f); t->FillRectangle(D2D1::RectF(Layout::rowLeft, top, Layout::rowRight, top + px), brush);
        if (view == View::Profile) {
            const bool last = LastProfile(); const auto& lines = last ? lastProfileNoteLines_ : profileNoteLines_;
            for (size_t k = 0; k < lines.size(); ++k) Label14(lines[k], Layout::textLeft, top + 13 + k * Layout::descriptionLine, Layout::rowRight, .5f);
            const float y = top + 13 + lines.size() * Layout::descriptionLine + 8;
            if (last) { // « Effacer les données… »: the last profile is never deleted (Paper XXY-0)
                const auto v = Interaction(ProfileClearData, now); const auto row = Layout::Row(y);
                if (v.hover > 0 || v.press > 0) pc_->Round(R(row), Design::Radius::r8, Tint(hoverTint * v.hover + pressTint * v.press));
                if (focus_ == ProfileClearData) pc_->FocusRing(R(row), Design::Radius::r8);
                Label16(TR(settings_clear_data), Layout::textLeft, y + 6, Layout::rowRight - 32, .9f + .1f * v.hover);
                const auto chevron = Layout::Chevron(y); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
                pc_->Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover); t->SetTransform(base);
            } else DangerRow(TR(settings_delete_profile), y, ProfileDelete, now, !ProfilesReadOnly());
        } else if (view != View::Sites && view != View::Site) {
            // Version + check for updates. The glyph spins while a check runs.
            const auto version = hooks_.version ? hooks_.version() : Version{};
            const float y = h - Layout::padBottom - Layout::slot;
            pc_->Text(version.text, {Layout::textLeft, y, Layout::Trailing(y, Layout::slot).left - 4, y + Layout::slot}, 14, .5f, true, false, -.14f);
            float turn = 0;
            if (Spinning(now)) turn = -SpinDegrees(now); // counter-clockwise, the way the glyph's arrow points
            IconButton(L"reload", Layout::Trailing(y, Layout::slot), CheckUpdates, now, turn, version.canCheck && !version.checking);
        } else if (view == View::Sites) {
            const auto& lines = sitesNoteLines_;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) Label14(lines[k], Layout::textLeft, top + 13 + k * 20, Layout::rowRight, .5f, 20);
            DangerRow(TR(settings_remove_all), top + 13 + 40 + 8, ResetAll, now, !sites_.empty());
        } else DangerRow(TR(settings_remove_permissions), top + 13, ResetSite, now, true);
    }
    // Scroll edge fades (DIP): reach grows with the content hidden on that side, capped by Layout.
    static std::pair<float, float> FadeReach(float scroll, float max) {
        return {std::clamp(scroll, 0.f, Layout::topFade), std::clamp(max - scroll, 0.f, Layout::bottomFade)};
    }
    // One view at a horizontal offset (navigation push/pop).
    void PaintView(int depth, float x, float opacity, Motion::Time now) {
        auto* target = pc_->RenderTarget(); const View view = views_[depth];
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        target->SetTransform(D2D1::Matrix3x2F::Translation(x, 0) * base);
        const bool layered = opacity < .998f; if (layered) PushOpacity(opacity);
        const float c = Design::Surface::canvas, h = Height();
        pc_->Round({0, 0, Layout::width, h}, 0, c); // opaque page: the view beneath never shows through
        // Header
        if (view == View::Main) pc_->Text(TR(settings_title), R(Layout::Title(false)), 16, .9f, false, true, -.16f);
        else {
            IconButton(L"back", Layout::BackButton(), Back, now);
            const std::wstring title = view == View::Site ? (site_ >= 0 && site_ < static_cast<int>(sites_.size()) ? Host(sites_[site_].origin) : L"") : view == View::Profile ? editor_.Value().name : Title(view);
            pc_->Text(title, R(Layout::Title(true)), 16, .9f, false, true, -.16f);
        }
        IconButton(L"close", Layout::CloseButton(), Close, now);
        // Content, clipped between the header and the footer.
        // Clip and fades share device-pixel-aligned edges (no unfaded sliver at 125/150 %).
        const float dip = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
        const float viewTop = std::ceil((Layout::viewTop - Layout::groupGap / 2) / dip) * dip, viewBottom = std::floor(ViewBottom(view) / dip) * dip;
        const float scroll = scrolls_[depth].Value(now); paintScroll_ = scroll;
        const bool dim = view == View::Main && Searching(); float searchTop = Layout::viewTop - scroll;
        target->PushAxisAlignedClip(D2D1::RectF(0, viewTop, Layout::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        const auto slots = Slots(view, now);
        // Three passes: row fills, then the selection plate over them, then row contents.
        auto pass = [&](bool surfaces) { for (const auto& s : slots) {
            if (s.top + s.height < viewTop || s.top > viewBottom || s.opacity <= .002f || (surfaces && (s.kind == Kind::Label || s.kind == Kind::Note || s.kind == Kind::Field || s.kind == Kind::Search || s.kind == Kind::Danger || s.kind == Kind::ProfileRow || s.kind == Kind::Editor || s.kind == Kind::Agent))) continue;
            if (s.kind == Kind::Search) searchTop = s.top;
            const float opacity = s.opacity * (dim && s.kind != Kind::Search ? Layout::searchDim : 1.f); // results own the root while a query stands
            const bool clipped = s.clipTop > -1e5f, fading = opacity < .998f;
            if (clipped) target->PushAxisAlignedClip(D2D1::RectF(0, std::max(s.clipTop, viewTop), Layout::width, std::min(s.clipBottom, viewBottom)), D2D1_ANTIALIAS_MODE_ALIASED);
            if (fading) PushOpacity(opacity);
            if (surfaces) RowSurface(Layout::Row(s.top, s.height), s.hit, now);
            else switch (s.kind) {
            case Kind::Label: pc_->Text(s.text, {Layout::textLeft, s.top, Layout::rowRight, s.top + 20}, 14, .5f, false, true, -.14f); break;
            case Kind::Note: { const auto lines = s.lines.empty() ? Wrap(s.text, Layout::rowRight - Layout::textLeft) : s.lines; for (size_t k = 0; k < lines.size(); ++k) Label14(lines[k], Layout::textLeft, s.top + k * 20, Layout::rowRight, .5f, 20); break; }
            case Kind::Engine: EngineRow(s, now); break;
            case Kind::Search: SearchRow(s, now); break;
            case Kind::Danger: DangerContent(s, now); break;
            case Kind::Switch: SwitchRow(s, now); break;
            case Kind::Folder: FolderRow(s, now); break;
            case Kind::Value: ValueRow(s, now); break;
            case Kind::Nav: NavRow(s, now); break;
            case Kind::SiteRow: SiteRow(s, now); break;
            case Kind::Field: FieldRow(s, now); break;
            case Kind::ProfileRow: ProfileRow(s, now); break;
            case Kind::Editor: editor_.Paint(*pc_, {Layout::textLeft, s.top}); break;
            case Kind::Agent: AgentRow(s, now); break;
            }
            if (fading) target->PopLayer();
            if (clipped) target->PopAxisAlignedClip();
        } };
        pass(true); if (view == View::Engine) Plate(now, scroll); pass(false);
        if (dim) ResultCardPaint(searchTop, now);
        // Scroll edge effects: content dissolves into the canvas where more lies beyond.
        const float max = MaxScroll(view, now, false);
        // The fade is always opaque canvas AT the edge (what the clip cuts is never visible) and only
        // its reach follows the distance left to scroll, so it eases away continuously at either end.
        // (It used to dim the whole overlay instead — by the top fade's 16 DIP — which near the bottom
        // let half-clipped rows show through, cut by a hard line at the footer.)
        const auto [topReach, bottomReach] = FadeReach(scroll, max);
        if (fade_ && topReach > 0) {
            fade_->SetStartPoint({0, viewTop + topReach}); fade_->SetEndPoint({0, viewTop});
            target->FillRectangle(D2D1::RectF(0, viewTop, Layout::width, viewTop + topReach), fade_);
        }
        if (fade_ && bottomReach > 0) {
            fade_->SetStartPoint({0, viewBottom - bottomReach}); fade_->SetEndPoint({0, viewBottom});
            target->FillRectangle(D2D1::RectF(0, viewBottom - bottomReach, Layout::width, viewBottom), fade_);
        }
        target->PopAxisAlignedClip();
        Footer(view, now);
        if (layered) target->PopLayer();
        target->SetTransform(base);
    }
    void Paint(Motion::Time now) {
        auto* target = pc_->RenderTarget(); auto* brush = pc_->SolidBrush(); if (!target || !brush) return;
        if (!fade_) {
            const float c = Design::Surface::canvas;
            D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(c, c, c, 0)}, {1, D2D1::ColorF(c, c, c, 1)}};
            if (FAILED(target->CreateGradientStopCollection(stops, 2, &stops_)) ||
                FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), stops_, &fade_))) { Drop(fade_); Drop(stops_); }
        }
        const float c = Design::Surface::canvas, h = Height();
        target->Clear(D2D1::ColorF(c, c, c, 1));
        pc_->keyboardModality = keyboard_;
        // Navigation: views below the current position slide 30% left and dim; the
        // one above enters from the right edge (iOS push). Reduced motion: no travel.
        const float pos = page_.Value(now); const int lo = std::clamp(static_cast<int>(std::floor(pos)), 0, MaxDepth - 1), hi = std::min(MaxDepth - 1, lo + 1);
        target->PushAxisAlignedClip(D2D1::RectF(0, 0, Layout::width, h), D2D1_ANTIALIAS_MODE_ALIASED);
        const float t = pos - lo;
        if (t < .001f || lo == hi) PaintView(lo, 0, 1, now);
        else { PaintView(lo, -.3f * Layout::width * t, 1 - .5f * t, now); PaintView(hi, Layout::width * (1 - t), 1, now); }
        target->PopAxisAlignedClip();
        // Leading edge: one physical pixel, a touch lighter than the canvas (as the download drawer).
        const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
        pc_->Color(1, .02f); target->FillRectangle(D2D1::RectF(0, 0, px, h), brush);
    }
    void PaintNow() {
        if (!pc_->RenderTarget()) pc_->Resize();
        auto* target = pc_->RenderTarget(); if (!target) return;
        target->BeginDraw(); Paint(Motion::Now());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { Drop(fade_); Drop(stops_); pc_->RecreateTarget(); }
    }

    // ---- Input -----------------------------------------------------------------
    int HitAt(float x, float y) const {
        const View view = Current(); const float h = Height();
        auto in = [x, y](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        if (page_.Moving(Motion::Now())) return None;
        if (in(Layout::CloseButton())) return Close;
        if (view != View::Main && in(Layout::BackButton())) return Back;
        const float footerTop = h - FooterHeight(view);
        if (view == View::Profile) {
            const float y = footerTop + 13 + (LastProfile() ? lastProfileNoteLines_ : profileNoteLines_).size() * Layout::descriptionLine + 8;
            if (in(Layout::Row(y))) return LastProfile() ? ProfileClearData : ProfilesReadOnly() ? None : ProfileDelete;
        } else if (view != View::Sites && view != View::Site) {
            if (in(Layout::Trailing(h - Layout::padBottom - Layout::slot, Layout::slot))) return CheckUpdates;
        } else if (view == View::Sites) { if (!sites_.empty() && in(Layout::Row(footerTop + 13 + 40 + 8))) return ResetAll; }
        else if (in(Layout::Row(footerTop + 13))) return ResetSite;
        if (y < Layout::viewTop - Layout::groupGap / 2 || y >= footerTop) return None;
        if (view == View::Main && Searching()) {
            // While results stand, only the field and the card answer; the dimmed root dismisses.
            const float fieldTop = Layout::viewTop - scrolls_[0].Value(Motion::Now());
            if (in(ClearBox(fieldTop))) return SearchDismiss;
            if (in(Layout::Row(fieldTop, Layout::field))) return SearchField;
            const auto card = ResultCard(fieldTop);
            if (in(card)) { const int i = static_cast<int>((y - card.top) / Layout::resultRow); return i < static_cast<int>(results_.size()) ? ResultBase + i : None; }
            return SearchDismiss;
        }
        for (const auto& s : Slots(view, Motion::Now())) {
            if (!s.hit || s.opacity < .5f || s.disabled) continue;
            if (s.kind == Kind::Agent) { if (in(AgentAction(s))) return s.hit; continue; }   // only the button answers
            if (in(Layout::Row(s.top, s.height)) && y >= s.clipTop && y < s.clipBottom) return s.hit;
        }
        return None;
    }
    std::vector<int> FocusOrder() const {
        const View view = Current(); std::vector<int> order;
        if (view != View::Main) order.push_back(Back);
        order.push_back(Close);
        if (view == View::Main && Searching()) { order.push_back(SearchField); for (size_t i = 0; i < results_.size(); ++i) order.push_back(ResultBase + static_cast<int>(i)); return order; }
        for (const auto& s : Slots(view, Motion::Now())) if (s.hit && s.opacity > .5f && (!s.disabled || s.kind == Kind::Agent)) order.push_back(s.hit);   // a busy agent button keeps the focus
        if (view == View::Profile) { if (LastProfile()) order.push_back(ProfileClearData); else if (!ProfilesReadOnly()) order.push_back(ProfileDelete); }
        else if (view != View::Sites && view != View::Site) { if (hooks_.version && hooks_.version().canCheck) order.push_back(CheckUpdates); }
        else if (view == View::Sites) { if (!sites_.empty()) order.push_back(ResetAll); }
        else order.push_back(ResetSite);
        return order;
    }
    void Interact(int hover, int pressed) {
        hover_ = hover; pressed_ = pressed; const auto now = Motion::Now();
        motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now);
        for (int i = 0; i < SwitchCount; ++i)
            knobPress_[i].Set(pressed_ == SwitchHits[i] && hover_ == SwitchHits[i] ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::press, Downloads::Curves::emphasized);
        Invalidate(); Schedule(now);
    }
    void Repoint() {
        if (!hwnd_ || !visible_) return;
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); RECT client; GetClientRect(hwnd_, &client);
        const float s = 96.f / Dpi(); Interact(PtInRect(&client, p) ? HitAt(p.x * s, p.y * s) : 0, pressed_);
    }
    void Schedule(Motion::Time now) { if (hooks_.schedule) hooks_.schedule(now); }
    void ScrollTo(float target, Motion::Time now) {
        target = std::clamp(target, 0.f, MaxScroll(Current(), now, true));
        scrolls_[depth_].Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::scroll);
    }
    void Reveal(int hit) {
        const auto now = Motion::Now(); const float scroll = scrolls_[depth_].Target();
        for (const auto& s : Slots(Current(), now, true)) {
            if (s.hit != hit) continue;
            // Clear of the edge fades, so a focused row never sits half dissolved.
            const float top = Layout::viewTop + (scroll > 0 ? Layout::topFade / 2 : 0), bottom = ViewBottom(Current()) - Layout::bottomFade;
            if (s.top < top) ScrollTo(scroll - (top - s.top), now);
            else if (s.top + s.height > bottom) ScrollTo(scroll + (s.top + s.height - bottom), now);
            return;
        }
    }
    // Modal work (folder picker, confirmation, choice menus) must not close the drawer it came from.
    template<class F> void Modal(F f) { modal_ = true; f(); modal_ = false; if (visible_ && !closing_) { Activated(); Changed(); } }
    // A value row's choices, right-aligned under it (screen pixels).
    int Choose(const std::vector<ContextMenu::Item>& items, int hit, bool keyboard) {
        if (!hooks_.choose) return 0;
        for (const auto& s : Slots(Current(), Motion::Now())) {
            if (s.hit != hit) continue;
            const auto row = Layout::Row(s.top, s.height);
            RECT r{Px(row.left), Px(row.top), Px(row.right), Px(row.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
            int chosen = 0; Modal([&] { chosen = hooks_.choose(items, r, keyboard); });
            return chosen;
        }
        return 0;
    }
    void Navigate(int depth, Motion::Time now) {
        depth = std::clamp(depth, 0, MaxDepth - 1);
        if (depth > depth_) scrolls_[depth].Snap(0);
        // Popping back lands keyboard focus on the row that pushed the view.
        const int from = depth_; const View left = views_[from]; depth_ = depth; motion_.ClearInteractions();
        if (editing_) Commit(false);
        if (left == View::Profile && depth < from) { editor_.Blur(); editor_.Hide(); }
        focus_ = depth < from && keyboard_ ? (left == View::Site ? SiteBase + std::max(0, site_) : left == View::Profile ? ProfileRowBase + std::max(0, profileIndex_) : Opener(left)) : 0;
        if (focus_) { const auto order = FocusOrder(); if (std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0; }
        if (uia_.Listening()) uia_.ChildrenChanged(DrawerKey);
        page_.Set(static_cast<float>(depth), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::page, Timing::pageSpring);
    }
    void Push(View v, Motion::Time now) {
        if (depth_ + 1 >= MaxDepth) return;
        views_[depth_ + 1] = v;
        if (v == View::Engine) plate_.Snap(EngineTop(SelectedIndex()));
        Navigate(depth_ + 1, now);
    }
    // Search ---------------------------------------------------------------------------
    void SetQuery(std::wstring q) {
        query_ = std::move(q); results_ = SearchSettings(query_, AnnotationsOn(), static_cast<bool>(hooks_.importData));
        motion_.ClearInteractions(); if (IsResult(focus_)) focus_ = 0;
        Invalidate(); Schedule(Motion::Now());
    }
    void ClearSearch() { if (edit_ && editing_ == SearchField) SetWindowTextW(edit_, L""); SetQuery(L""); }
    // A result: a switch toggles where it is; anything else opens its view on the row.
    void OpenResult(int index, bool keyboard) {
        if (index < 0 || index >= static_cast<int>(results_.size())) return;
        const auto& e = *results_[index].entry; const auto now = Motion::Now();
        if (SwitchIndex(e.hit) >= 0) { Activate(e.hit, keyboard); return; }
        if (editing_) EndEdit();
        if (e.view != View::Main) Push(e.view, now);
        else ClearSearch();
        keyboard_ = true; focus_ = e.hit; Reveal(e.hit);
        Repoint(); RefreshTips(); Invalidate(); Schedule(now);
    }
    void LoadSites() {
        if (!hooks_.loadSites) { sitesLoaded_ = true; return; }
        const unsigned ticket = ++loadTicket_; loadStart_ = Motion::Now();
        hooks_.loadSites([this, ticket](std::vector<BrowserSettings::Site> sites) {
            if (ticket != loadTicket_ || !hwnd_) return; // a newer load superseded this one
            sites_ = std::move(sites); sitesLoaded_ = true;
            if (Current() == View::Site && (site_ < 0 || site_ >= static_cast<int>(sites_.size()))) Navigate(depth_ - 1, Motion::Now());
            if (visible_) { auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0; RefreshTips(); Invalidate(); Schedule(Motion::Now()); }
        });
    }
    // ---- « Annotations » ---------------------------------------------------
    // Asks the host for the agents' state and the annotation count (both async, off the UI thread there).
    void LoadAnnotations() {
        if (!AnnotationsOn()) return;
        if (hooks_.loadAgents) {
            const unsigned ticket = ++agentsTicket_;
            hooks_.loadAgents([this, ticket](AnnotationSettings::Agents agents) { if (ticket == agentsTicket_ && hwnd_) ApplyAgents(agents); });
        }
        if (hooks_.countAnnotations) {
            const unsigned ticket = ++countTicket_;
            hooks_.countAnnotations([this, ticket](std::size_t n) { if (ticket == countTicket_ && hwnd_) { annotationCount_ = n; AnnotationsRelaid(); } });
        }
    }
    // The host's detection; what the drawer owns (a running action, the last result) stays.
    void ApplyAgents(const AnnotationSettings::Agents& from) {
        for (int i = 0; i < AnnotationSettings::AppCount; ++i) {
            const auto app = AnnotationSettings::AppAt(i); auto& mine = agents_[app]; AnnotationSettings::Agent next = from[app];
            next.known = true; next.activity = mine.activity; next.failed = mine.failed; next.message = mine.message;
            if (next.connected && !next.failed) next.message.clear();   // the step it waited for is done (« Cliquez sur Installer »)
            mine = next;
        }
        agentsKnown_ = true; AnnotationsRelaid();
    }
    void AnnotationsRelaid() {
        MeasureAgents();
        if (!visible_) return;
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RefreshTips(); Invalidate(); Schedule(Motion::Now());
    }
    void Announce(const std::wstring& text) { if (uia_.Listening()) uia_.Notify(HitBase + Close, text.c_str(), L"Reglages"); }   // i18n-exempt: UIA activity id
    // Connecter / Déconnecter / Reconnecter: only on this explicit click; the host does the work (same path,
    // backups and atomic writes as the connect modal) and answers once.
    void AgentAction(AnnotationSettings::App app) {
        namespace AS = AnnotationSettings; auto& a = agents_[app];
        if (!AS::ActionEnabled(a) || !hooks_.connectAgent) return;
        const bool connect = AS::ActionFor(a) != AS::Action::Disconnect;
        a.activity = connect ? AS::Activity::Connecting : AS::Activity::Disconnecting; a.failed = false; a.message.clear();
        AnnotationsRelaid();
        Announce(connect ? TRF(settings_announce_connecting, AS::AppName(app)) : TRF(settings_announce_disconnecting, AS::AppName(app)));
        hooks_.connectAgent(app, connect, [this, app, connect](bool ok, std::wstring message) {
            if (!hwnd_) return;
            auto& a = agents_[app]; a.activity = AS::Activity::Idle; a.failed = !ok; a.message = ok && !connect ? L"" : std::move(message);
            AnnotationsRelaid();
            Announce(!ok ? AS::StatusText(a, NowSeconds()) : (connect ? TRF(settings_announce_connected, AS::AppName(app)) : TRF(settings_announce_disconnected, AS::AppName(app))));
            LoadAnnotations();
        });
    }
    // ---- UI Automation: the whole drawer ----------------------------------------------------------------
    // The drawer (Pane) and its header (Retour / Fermer); Réglages › Profils: the profile list (List +
    // ListItems, current one selected), the detail editor (Edit, List/ListItem radios, Sliders with RangeValue,
    // hex Edit) and its footer action; every other view (root « Profils » row included): one node per row
    // (SlotNodes), toggles, values, folders, agents… Controls are keyed HitBase + hit.
    Accessibility::Bridge uia_;
    static constexpr long long DrawerKey = 1, ListKey = 2, EditorBase = 3000, HitBase = 10000, TextBase = 100000;
    static constexpr UINT UiaInvoke = WM_APP + 52;
    long long FocusKey() const {
        if (focus_ == ProfileEditorHit) { const int f = editor_.Focus(); return f ? EditorBase + (f == ProfileEditor::Icons ? 100 + editor_.IconIndex() : f) : 0; }
        return focus_ ? HitBase + focus_ : 0;
    }
    void RaiseFocus() { if (uia_.Listening() && FocusKey()) uia_.Raise(FocusKey(), UIA_AutomationFocusChangedEventId); }
    RECT ScreenOf(Design::Box b) const {
        RECT w{}; GetWindowRect(hwnd_, &w); const float k = Dpi() / 96.f;
        return {w.left + static_cast<LONG>(b.left * k), w.top + static_cast<LONG>(b.top * k), w.left + static_cast<LONG>(b.right * k), w.top + static_cast<LONG>(b.bottom * k)};
    }
    std::vector<Accessibility::Node> UiaNodes() const {
        std::vector<Accessibility::Node> nodes; if (!hwnd_ || !visible_) return nodes;
        const View view = Current(); RECT win{}; GetWindowRect(hwnd_, &win);
        Accessibility::Node pane; pane.key = DrawerKey; pane.role = Accessibility::Role::Group; pane.focusable = false; pane.bounds = win; pane.automationId = L"SettingsDrawer";
        pane.name = view == View::Profile ? editor_.Value().name : Title(view); nodes.push_back(pane);
        auto button = [&](int hit, const std::wstring& name, Design::Box box, const wchar_t* id) {
            Accessibility::Node n; n.key = HitBase + hit; n.parent = DrawerKey; n.role = Accessibility::Role::Button; n.name = name; n.automationId = id; n.bounds = ScreenOf(box); nodes.push_back(n);
        };
        if (view != View::Main) { button(Back, TR(common_back), Layout::BackButton(), L"SettingsBack"); nodes.back().accelerator = TR(key_backspace); }
        button(Close, TR(settings_close), Layout::CloseButton(), L"SettingsClose"); nodes.back().accelerator = TR(key_escape);
        const auto now = Motion::Now();
        if (view != View::Profiles && view != View::Profile) SlotNodes(nodes, view);
        else if (view == View::Profiles) {
            Accessibility::Node list; list.key = ListKey; list.parent = DrawerKey; list.role = Accessibility::Role::List; list.name = TRP(settings_profiles_count, profiles_.size()); list.focusable = false; list.automationId = L"ProfileList"; list.bounds = win; nodes.push_back(list);
            for (const auto& sl : Slots(View::Profiles, now)) {
                if (sl.kind != Kind::ProfileRow) continue;
                Accessibility::Node n; n.key = HitBase + sl.hit; n.parent = ListKey; n.role = Accessibility::Role::ListItem; n.name = sl.current ? TRF(settings_profile_this_window, sl.text) : sl.text;
                n.selected = sl.current; n.position = sl.index + 1; n.count = static_cast<int>(profiles_.size()); n.automationId = L"ProfileListItem"; n.help = TR(settings_profile_open_detail); n.bounds = ScreenOf(Layout::Row(sl.top, sl.height)); nodes.push_back(n);
            }
        } else if (view == View::Profile) {
            auto ed = editor_.Nodes(EditorBase, DrawerKey, win); nodes.insert(nodes.end(), ed.begin(), ed.end());
            const float y = Height() - FooterHeight(View::Profile) + 13 + (LastProfile() ? lastProfileNoteLines_ : profileNoteLines_).size() * Layout::descriptionLine + 8;
            if (LastProfile()) button(ProfileClearData, TR(settings_clear_data), Layout::Row(y), L"ProfileClearData");
            else { button(ProfileDelete, TR(settings_delete_profile), Layout::Row(y), L"ProfileDelete"); nodes.back().enabled = !ProfilesReadOnly(); nodes.back().help = TR(settings_profile_delete_note); }
        }
        return nodes;
    }
    // Every view but Profils: one node per row of the view on screen, in reading order (UiaNodes adds the
    // pane and the header). Static text is keyed from TextBase, controls HitBase + hit.
    void SlotNodes(std::vector<Accessibility::Node>& out, View view) const {
        using Accessibility::Role;
        auto add = [&](long long hitOrText, Role role, std::wstring name, Design::Box b) -> Accessibility::Node& {
            Accessibility::Node n; n.key = hitOrText >= TextBase ? hitOrText : HitBase + hitOrText; n.parent = DrawerKey; n.role = role; n.name = std::move(name); n.bounds = ScreenOf(b); out.push_back(std::move(n)); return out.back();
        };
        auto join = [](const std::vector<std::wstring>& lines) { std::wstring t; for (const auto& l : lines) t += (t.empty() ? L"" : L" ") + l; return t; };
        long long textKey = TextBase;
        const float top = Layout::viewTop - Layout::groupGap / 2, bottom = ViewBottom(view);
        if (view == View::Main && Searching()) {
            const float fieldTop = Layout::viewTop - scrolls_[0].Value(Motion::Now());
            auto& f = add(SearchField, Role::Edit, TR(settings_search_placeholder), Layout::Row(fieldTop, Layout::field)); f.value = query_;
            const auto card = ResultCard(fieldTop);
            for (size_t i = 0; i < results_.size(); ++i) {
                auto& n = add(ResultBase + static_cast<int>(i), Role::Button, I18n::T(results_[i].entry->title.id), {card.left, card.top + i * Layout::resultRow, card.right, card.top + (i + 1) * Layout::resultRow});
                n.help = I18n::T(results_[i].entry->path.id); if (SwitchIndex(results_[i].entry->hit) >= 0) n.toggle = SwitchValue(results_[i].entry->hit) ? 2 : 1;
            }
            return;
        }
        for (const auto& s : Slots(view, Motion::Now())) {
            if (s.opacity < .5f) continue;
            const auto box = Layout::Row(s.top, s.height); const bool offscreen = s.top + s.height <= top || s.top >= bottom;
            Accessibility::Node* n = nullptr;
            switch (s.kind) {
            case Kind::Label: n = &add(textKey++, Role::Text, s.text, box); n->focusable = false; break;
            case Kind::Note: n = &add(textKey++, Role::Text, s.text, box); n->focusable = false; break;
            case Kind::Search: n = &add(s.hit, Role::Edit, TR(settings_search_placeholder), box); n->value = query_; break;
            case Kind::Field: n = &add(s.hit, Role::Edit, s.hit == CustomSearchField ? TR(settings_custom_search_name) : TR(settings_homepage_name), box); n->value = FieldValue(s.hit); n->help = FieldTexts(s.hit).hint; break;
            case Kind::Engine: n = &add(s.hit, Role::Button, BrowserSettings::Info(static_cast<Engine>(s.index)).name, box); n->selected = s.index == SelectedIndex(); break;
            case Kind::Switch: n = &add(s.hit, Role::Button, s.text, box); n->toggle = SwitchValue(s.hit) ? 2 : 1; n->help = join(s.lines); break;
            case Kind::Value: n = &add(s.hit, Role::Button, s.text, box); n->status = s.value; n->expand = 1; n->help = join(s.lines); break;
            case Kind::Nav: if (s.hit == CatProfiles) { n = &add(s.hit, Role::Button, TRF(settings_profiles_name, s.value), box); n->automationId = L"SettingsProfiles"; n->help = TR(settings_profiles_help); }
                else if (!s.hit) { n = &add(textKey++, Role::Text, s.text + (s.lines.empty() ? L"" : L" : " + join(s.lines)), box); n->focusable = false; } else { n = &add(s.hit, Role::Button, s.text, box); n->status = s.value; if (s.hit == ImportData) n->automationId = L"SettingsImportData"; } break;
            case Kind::Folder: n = &add(s.hit, Role::Button, s.text.empty() ? std::wstring(TR(settings_save_in)) : s.text, box);
                n->help = s.hit == Folder ? (hooks_.downloadFolder ? hooks_.downloadFolder() : L"") : (hooks_.annotationFolder ? hooks_.annotationFolder() : L""); break;
            case Kind::SiteRow: n = &add(s.hit, Role::Button, s.text, box); n->help = s.value; break;
            case Kind::Danger: n = &add(s.hit, Role::Button, s.text, box); n->enabled = !s.disabled; n->focusable = !s.disabled;
                if (s.hit == AnnotationClear && annotationCount_ != SIZE_MAX) n->help = annotationCount_ == 0 ? std::wstring(TR(settings_no_annotation)) : AnnotationSettings::ClearAction(annotationCount_); break;
            case Kind::ProfileRow: case Kind::Editor: break;   // Profils views: described by UiaNodes
            case Kind::Agent: {
                // The row reads as one sentence; its button names the app it acts on.
                n = &add(textKey++, Role::Text, s.text + L" : " + join(s.lines), box); n->focusable = false; n->offscreen = offscreen;
                if (!s.hit) break;
                n = &add(s.hit, Role::Button, s.value + L" " + s.text, AgentAction(s)); n->help = join(s.lines); n->enabled = !s.disabled;
                break;
            }
            }
            if (n) n->offscreen = offscreen;
        }
        const float h = Height(), footerTop = h - FooterHeight(view);
        if (view != View::Sites && view != View::Site) {
            const auto version = hooks_.version ? hooks_.version() : Version{};
            if (!version.text.empty()) add(textKey++, Role::Text, version.text, {Layout::textLeft, h - Layout::padBottom - Layout::slot, Layout::rowRight, h - Layout::padBottom}).focusable = false;
            if (version.canCheck) add(CheckUpdates, Role::Button, TR(settings_check_updates), Layout::Trailing(h - Layout::padBottom - Layout::slot, Layout::slot)).enabled = !version.checking;
        } else if (view == View::Sites) { auto& n = add(ResetAll, Role::Button, TR(settings_remove_all), Layout::Row(footerTop + 13 + 40 + 8)); n.enabled = n.focusable = !sites_.empty(); }
        else add(ResetSite, Role::Button, TR(settings_remove_permissions), Layout::Row(footerTop + 13));
    }
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return UiaNodes(); };
        host.focused = [this]() -> long long { return visible_ ? FocusKey() : 0; };
        // Actions never run inside the provider call: they are posted back to the drawer.
        host.invoke = [this](long long key) { PostMessageW(hwnd_, UiaInvoke, 0, static_cast<LPARAM>(key)); };
        host.select = [this](long long key) { PostMessageW(hwnd_, UiaInvoke, 1, static_cast<LPARAM>(key)); };
        host.focus = [this](long long key) { PostMessageW(hwnd_, UiaInvoke, 2, static_cast<LPARAM>(key)); };
        host.toggle = [this](long long key) { PostMessageW(hwnd_, UiaInvoke, 0, static_cast<LPARAM>(key)); };            // switches
        host.expand = [this](long long key, bool) { PostMessageW(hwnd_, UiaInvoke, 0, static_cast<LPARAM>(key)); };   // value rows open their list
        host.setRange = [this](long long key, double v) { if (key > EditorBase && key < HitBase) { editor_.Accessible(EditorBase, key, 2, v); Invalidate(); } };
        host.setValue = [this](long long key, const std::wstring& v) { if (key > EditorBase && key < HitBase) { editor_.AccessibleValue(EditorBase, key, v); Invalidate(); } };
        uia_.Attach(std::move(host));
    }
    void UiaAction(int kind, long long key) {
        keyboard_ = true;
        if (key > EditorBase && key < HitBase) { focus_ = ProfileEditorHit; editor_.Accessible(EditorBase, key, kind == 1 ? 1 : 0, 0); Invalidate(); return; }
        if (key <= HitBase) return; const int hit = static_cast<int>(key - HitBase);
        if (kind == 2) { focus_ = hit; Reveal(hit); Interact(hover_, pressed_); return; }
        focus_ = hit; Activate(hit, true); if (uia_.Listening()) uia_.ChildrenChanged(DrawerKey); RaiseFocus();
    }
    // ---- Profiles -----------------------------------------------------------------------
    void RefreshProfiles() {
        profiles_ = hooks_.profiles ? hooks_.profiles() : std::vector<ProfileItem>{};
        profileIndex_ = -1; for (size_t i = 0; i < profiles_.size(); ++i) if (profiles_[i].id == profileId_) profileIndex_ = static_cast<int>(i);
        if (!profileId_.empty() && hooks_.profile && editor_.Focus() == ProfileEditor::None && !editor_.Dragging()) {
            if (profileIndex_ >= 0) editor_.SetValue(hooks_.profile(profileId_));
        }
        // The profile shown was deleted elsewhere: back to the list.
        if (profileIndex_ < 0 && !profileId_.empty() && DepthOf(View::Profile) >= 0 && Current() == View::Profile) { profileId_.clear(); editor_.Hide(); Navigate(depth_ - 1, Motion::Now()); }
    }
    void OpenProfile(int index) {
        if (index < 0 || index >= static_cast<int>(profiles_.size()) || !hooks_.profile) { profileIndex_ = -1; return; }
        profileIndex_ = index; profileId_ = profiles_[index].id; editor_.SetValue(hooks_.profile(profileId_));
    }
    D2D1_POINT_2F EditorOrigin() const {
        for (const auto& s : Slots(View::Profile, Motion::Now())) if (s.kind == Kind::Editor) return {Layout::textLeft, s.top};
        return {Layout::textLeft, Layout::viewTop};
    }
    // The editor's native edits sit on their painted fields only while the detail view is still
    // and the block is within the visible content (never during a push/pop or a slide).
    void SyncEditor(Motion::Time now) {
        if (!visible_ || closing_ || Current() != View::Profile || page_.Moving(now) || slide_.Moving(now)) { editor_.Hide(); return; }
        const auto o = EditorOrigin(); const float bottom = ViewBottom(View::Profile);
        editor_.Place(o, Dpi() / 96.f, o.y >= Layout::viewTop - Layout::groupGap / 2 && o.y + editor_.Height() <= bottom + .5f);
    }
    void AttachEditor() {
        ProfileEditor::Editor::Hooks h;
        h.invalidate = [this] { Invalidate(); Schedule(Motion::Now()); };
        h.validateName = [this](const std::wstring& n) { return hooks_.validateProfileName ? hooks_.validateProfileName(n, profileId_) : Profiles::NameError::None; };
        h.changed = [this](const Profiles::Profile& p, ProfileEditor::Stop) {
            if (ProfilesReadOnly() || !hooks_.saveProfile) return;
            Profiles::NameError ne = Profiles::NameError::None;
            if (!hooks_.saveProfile(p, &ne) && ne != Profiles::NameError::None) editor_.ShowNameError(ne);
            profiles_ = hooks_.profiles ? hooks_.profiles() : profiles_; Invalidate();
        };
        h.focusChanged = [this](ProfileEditor::Stop s) { if (s != ProfileEditor::None) { focus_ = ProfileEditorHit; Invalidate(); RaiseFocus(); } };
        h.tabOut = [this](int dir) {
            const auto order = FocusOrder(); auto it = std::find(order.begin(), order.end(), static_cast<int>(ProfileEditorHit)); if (it == order.end() || order.empty()) return;
            int index = static_cast<int>(it - order.begin()) + dir; index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
            focus_ = order[index]; keyboard_ = true; if (hwnd_) SetFocus(hwnd_); Reveal(focus_); Interact(hover_, pressed_); RaiseFocus();
        };
        editor_.Attach(hwnd_, std::move(h), ProfileEditor::Style::Drawer);
        editor_.SetWidth(Layout::rowRight - 8 - Layout::textLeft);
    }
    void Activate(int hit, bool keyboard) {
        if (!state_) return;
        const auto now = Motion::Now(); using namespace BrowserSettings;
        if (hit == Close) { Hide(keyboard, true); return; }
        if (hit == Back) { Navigate(depth_ - 1, now); }
        else if (IsEngine(hit)) {
            const auto engine = static_cast<Engine>(hit - EngineBase);
            // The custom engine needs its address first: unfold the field and edit it.
            if (engine == Engine::Custom && SearchTemplate(state_->customSearch).empty()) { customPending_ = true; SyncReveals(now); StartEdit(CustomSearchField); }
            else if (state_->engine != engine) {
                state_->engine = engine;
                plate_.Set(EngineTop(static_cast<int>(engine)), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
                if (hooks_.changed) hooks_.changed(Change::Engine);
                if (engine != Engine::Custom) customPending_ = false;
                SyncReveals(now);
            }
        } else if (SwitchIndex(hit) >= 0) {
            if (hit == AnnotationPrompt) BrowserSettings::SetConnectPrompt(*state_, !SwitchValue(hit));   // on again: the « Plus tard » count restarts
            else { bool* value = SwitchField(hit); if (!value) return; *value = !*value; }
            switches_[SwitchIndex(hit)].Set(SwitchValue(hit) ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
            if (hooks_.changed) hooks_.changed(SwitchChange(hit));
            if (hit == ReduceMotion) SyncReducedMotion();
        } else if (hit == Tracking) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < TrackingCount; ++i) {
                const auto t = static_cast<BrowserSettings::Tracking>(i);
                auto item = ContextMenu::Command(i + 1, i == 2 ? TRF(settings_choice_recommended, Info(t).label) : std::wstring(Info(t).label), L"", true, state_->tracking == t);
                item.description = Info(t).description; items.push_back(item);
            }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && static_cast<BrowserSettings::Tracking>(chosen - 1) != state_->tracking) { state_->tracking = static_cast<BrowserSettings::Tracking>(chosen - 1); if (hooks_.changed) hooks_.changed(Change::Tracking); }
        } else if (hit == Sleep) {
            const wchar_t* notes[SleepCount] = {TR(sleep_never_note), TR(sleep_30_note), TR(sleep_120_note), TR(sleep_360_note)};
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < SleepCount; ++i) { auto item = ContextMenu::Command(i + 1, SleepLabel(SleepChoices[i]), L"", true, state_->sleepMinutes == SleepChoices[i]); item.description = notes[i]; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && SleepChoices[chosen - 1] != state_->sleepMinutes) { state_->sleepMinutes = SleepChoices[chosen - 1]; if (hooks_.changed) hooks_.changed(Change::Sleep); }
        } else if (hit == HomePageHit) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < NewTabPageCount; ++i) { const auto p = static_cast<NewTabPage>(i); auto item = ContextMenu::Command(i + 1, Info(p).label, L"", true, state_->newTabPage == p); item.description = Info(p).description; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0) {
                const auto page = static_cast<NewTabPage>(chosen - 1);
                if (page == NewTabPage::Custom && HomepageUrl(state_->homepage).empty()) { homePending_ = true; SyncReveals(Motion::Now()); StartEdit(HomepageField); }
                else { homePending_ = false; if (page != state_->newTabPage) { state_->newTabPage = page; if (hooks_.changed) hooks_.changed(Change::NewTabPage); } SyncReveals(Motion::Now()); }
            }
        } else if (hit == TabPositionHit) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < TabPositionCount; ++i) { const auto p = static_cast<TabPosition>(i); auto item = ContextMenu::Command(i + 1, Info(p).label, L"", true, state_->tabPosition == p); item.description = Info(p).description; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && static_cast<TabPosition>(chosen - 1) != state_->tabPosition) { state_->tabPosition = static_cast<TabPosition>(chosen - 1); if (hooks_.changed) hooks_.changed(Change::TabPosition); }
        } else if (hit == LanguageHit && hooks_.saveLanguage) {
            // Two autonyms, the saved one checked; nothing switches now (applied at the next start).
            // The preference is browser-wide: another window may have saved it since this drawer read it. The
            // check mark shows what is on disk now, and an explicit choice is always written (atomically), even
            // when it equals what this drawer last saw: the file may have changed while the menu was open.
            const auto onDisk = [&] { return hooks_.savedLanguage ? hooks_.savedLanguage() : languageSaved_; };
            const auto before = languageSaved_; languageSaved_ = onDisk();
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < I18n::LocaleCount; ++i) { const auto l = static_cast<I18n::Locale>(i); items.push_back(ContextMenu::Command(i + 1, UiLanguage::Autonym(l), L"", true, languageSaved_ == l)); }
            const int chosen = Choose(items, hit, keyboard);
            bool changed = languageSaved_ != before;
            if (chosen > 0) {
                const auto want = static_cast<I18n::Locale>(chosen - 1);
                if (hooks_.saveLanguage(want)) { changed |= languageSaved_ != want || languageFailed_; languageSaved_ = want; languageFailed_ = false; }
                else { languageSaved_ = onDisk(); languageFailed_ = true; changed = true; }   // never « saved » on failure: the value stays what is on disk
            }
            if (changed) { if (uia_.Listening()) uia_.ChildrenChanged(DrawerKey); Invalidate(); }
        } else if (hit == AutoplayHit) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < AutoplayCount; ++i) { const auto a = static_cast<BrowserSettings::Autoplay>(i); auto item = ContextMenu::Command(i + 1, Info(a).label, L"", true, state_->autoplay == a); item.description = Info(a).description; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && static_cast<BrowserSettings::Autoplay>(chosen - 1) != state_->autoplay) { state_->autoplay = static_cast<BrowserSettings::Autoplay>(chosen - 1); if (hooks_.changed) hooks_.changed(Change::Autoplay); }
        } else if (IsField(hit)) { if (editing_ != hit) StartEdit(hit); return; }
        else if (hit == SearchDismiss) { if (editing_) EndEdit(); ClearSearch(); }
        else if (IsResult(hit)) { OpenResult(hit - ResultBase, keyboard); if (Searching()) results_ = SearchSettings(query_, AnnotationsOn(), static_cast<bool>(hooks_.importData)); }
        else if (hit == CatEngine) Push(View::Engine, now);
        else if (hit == CatTabs) Push(View::Tabs, now);
        else if (hit == CatPrivacy) { LoadSites(); Push(View::Privacy, now); }
        else if (hit == CatDownloads) Push(View::Downloads, now);
        else if (hit == CatDisplay) Push(View::Display, now);
        else if (hit == CatAnnotations) { LoadAnnotations(); Push(View::Annotations, now); }
        else if (IsAgent(hit)) AgentAction(static_cast<AnnotationSettings::App>(hit - AgentBase));
        else if (hit == ManualConnect) { if (hooks_.openConnect) hooks_.openConnect(); }
        else if (hit == AnnotationFolder) { if (hooks_.openAnnotationFolder) hooks_.openAnnotationFolder(); }
        else if (hit == AnnotationClear) {
            if (hooks_.clearAnnotations && annotationCount_ != SIZE_MAX && annotationCount_ > 0) { const std::size_t n = annotationCount_; Modal([&] { hooks_.clearAnnotations(n, keyboard); }); }
        } else if (hit == AnnotationRetention) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < RetentionCount; ++i) { const int d = RetentionChoices[i]; auto item = ContextMenu::Command(i + 1, RetentionLabel(d), L"", true, state_->annotationRetentionDays == d); item.description = AnnotationSettings::RetentionNote(d); items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && RetentionChoices[chosen - 1] != state_->annotationRetentionDays) { state_->annotationRetentionDays = RetentionChoices[chosen - 1]; if (hooks_.changed) hooks_.changed(Change::AnnotationRetention); }
        }
        else if (hit == Zoom) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < ZoomCount; ++i) items.push_back(ContextMenu::Command(i + 1, ZoomLabel(ZoomChoices[i]), L"", true, state_->zoomPercent == ZoomChoices[i]));
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && ZoomChoices[chosen - 1] != state_->zoomPercent) { state_->zoomPercent = ZoomChoices[chosen - 1]; if (hooks_.changed) hooks_.changed(Change::Zoom); }
        } else if (hit == BookmarksBarHit) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < BookmarksBarCount; ++i) { const auto b = static_cast<BrowserSettings::BookmarksBar>(i); auto item = ContextMenu::Command(i + 1, Info(b).label, L"", true, state_->bookmarksBar == b); item.description = Info(b).description; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && static_cast<BrowserSettings::BookmarksBar>(chosen - 1) != state_->bookmarksBar) { state_->bookmarksBar = static_cast<BrowserSettings::BookmarksBar>(chosen - 1); if (hooks_.changed) hooks_.changed(Change::BookmarksBar); }
        } else if (hit == Permissions) { LoadSites(); Push(View::Sites, now); }
        else if (IsSite(hit)) { site_ = hit - SiteBase; Push(View::Site, now); }
        else if (IsPermission(hit) && site_ >= 0 && site_ < static_cast<int>(sites_.size())) {
            auto& site = sites_[site_]; const int index = hit - PermissionBase;
            if (index < 0 || index >= static_cast<int>(site.permissions.size())) return;
            auto& perm = site.permissions[index];
            static const PermissionState order[] = {PermissionState::Allow, PermissionState::Ask, PermissionState::Block};
            const wchar_t* labels[] = {TR(permission_choice_allow), TR(permission_ask), TR(permission_choice_block)};
            const wchar_t* notes[] = {TR(permission_choice_allow_note), TR(permission_choice_ask_note), TR(permission_choice_block_note)};
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < 3; ++i) { auto item = ContextMenu::Command(i + 1, labels[i], L"", true, perm.state == order[i]); item.description = notes[i]; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && order[chosen - 1] != perm.state) {
                perm.state = order[chosen - 1];
                if (hooks_.setPermission) hooks_.setPermission(site.origin, perm.kind, perm.state);
                LoadSites();
            }
        } else if (hit == ResetSite && site_ >= 0 && site_ < static_cast<int>(sites_.size())) {
            if (hooks_.resetSites) hooks_.resetSites(sites_[site_].origin);
            Navigate(depth_ - 1, now); LoadSites();
        } else if (hit == ResetAll && !sites_.empty()) { if (hooks_.resetSites) hooks_.resetSites(L""); sites_.clear(); LoadSites(); }
        else if (hit == CatProfiles) { RefreshProfiles(); Push(View::Profiles, now); }
        else if (IsProfileRow(hit)) { OpenProfile(hit - ProfileRowBase); if (profileIndex_ >= 0) Push(View::Profile, now); }
        else if (hit == ProfileDelete) { if (hooks_.deleteProfile && !profileId_.empty() && !LastProfile() && !ProfilesReadOnly()) { const std::wstring id = profileId_; Modal([&] { hooks_.deleteProfile(id, keyboard); }); } }
        else if (hit == ProfileClearData) { if (hooks_.clearData) Modal([&] { hooks_.clearData(keyboard); }); }
        else if (hit == ProfileEditorHit) { editor_.SetFocusStop(ProfileEditor::Name, keyboard); }
        else if (hit == Folder) { if (hooks_.pickFolder) Modal([&] { hooks_.pickFolder(); }); }
        else if (hit == ClearData) { if (hooks_.clearData) Modal([&] { hooks_.clearData(keyboard); }); }
        else if (hit == ManagePasswords) { if (hooks_.managePasswords) Modal([&] { hooks_.managePasswords(keyboard); }); }
        else if (hit == ImportData) { if (hooks_.importData) Modal([&] { hooks_.importData(keyboard); }); }
        else if (hit == CheckUpdates) { if (hooks_.version && hooks_.version().canCheck && hooks_.checkUpdates) hooks_.checkUpdates(); Changed(); }
        Repoint(); RefreshTips(); Invalidate(); Schedule(Motion::Now());
    }
    // ---- Address fields -----------------------------------------------------------
    std::wstring FieldValue(int hit) const {
        if (hit == SearchField) return query_;
        const auto d = drafts_.find(hit); if (d != drafts_.end()) return d->second;
        if (!state_) return L"";
        return hit == CustomSearchField ? state_->customSearch : state_->homepage;
    }
    bool CustomVisible() const { return state_ && (state_->engine == Engine::Custom || customPending_); }
    bool HomeVisible() const { return state_ && (state_->newTabPage == BrowserSettings::NewTabPage::Custom || homePending_); }
    void SyncReveals(Motion::Time now, bool snap = false) {
        const auto d = snap || Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold;
        if (snap) { customReveal_.Snap(CustomVisible() ? 1.f : 0.f); homeReveal_.Snap(HomeVisible() ? 1.f : 0.f); plate_.Snap(EngineTop(SelectedIndex())); return; }
        customReveal_.Set(CustomVisible() ? 1.f : 0.f, now, Motion::Milliseconds(0), d, Downloads::DrawerMotion::foldSpring);
        homeReveal_.Set(HomeVisible() ? 1.f : 0.f, now, Motion::Milliseconds(0), d, Downloads::DrawerMotion::foldSpring);
        plate_.Set(EngineTop(SelectedIndex()), now, Motion::Milliseconds(0), d, Downloads::DrawerMotion::foldSpring);
        Invalidate(); Schedule(now); // the lanes need frames even when no other motion runs
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Drawer*>(data);
        switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, w, l);
        case WM_KEYDOWN:
            if (self->editing_ == SearchField) {
                // Enter opens the first result, Down walks into them, Escape clears then leaves.
                if (w == VK_RETURN) { if (!self->results_.empty()) self->OpenResult(0, true); return 0; }
                if (w == VK_DOWN && !self->results_.empty()) { self->keyboard_ = true; self->EndEdit(); self->focus_ = ResultBase; self->Invalidate(); return 0; }
                if (w == VK_ESCAPE) { if (GetWindowTextLengthW(h)) self->ClearSearch(); else self->EndEdit(); return 0; }
                if (w == VK_TAB) { self->keyboard_ = true; self->EndEdit(); return 0; }
                break;
            }
            if (w == VK_RETURN) { self->Commit(true); return 0; }
            if (w == VK_ESCAPE) { self->cancelEdit_ = true; self->EndEdit(); return 0; }
            if (w == VK_TAB) { self->Commit(false); return 0; }
            break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->ApplyCaret(); return r; }
        case WM_KILLFOCUS: if (self->editing_ && !self->cancelEdit_) self->Commit(false); break;
        case WM_PAINT: {
            // No comctl32 v6 (no cue banner): the placeholder is painted over the empty field, as when it rests.
            const LRESULT r = DefSubclassProc(h, msg, w, l);
            if (GetWindowTextLengthW(h) == 0 && self->editing_) {
                HDC dc = GetDC(h); RECT rc; GetClientRect(h, &rc);
                const auto old = SelectObject(dc, self->editFont_); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, self->Gray(.35f * .9f));
                DrawTextW(dc, FieldTexts(self->editing_).placeholder, -1, &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, old); ReleaseDC(h, dc);
            }
            return r;
        }
        case WM_CHAR: if (w == L'\r' || w == 27 || w == L'\t') return 0; [[fallthrough]];
        case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO: case EM_REPLACESEL: {
            // Placeholder and error follow the text as it is typed.
            const LRESULT r = DefSubclassProc(h, msg, w, l);
            if (self->editing_ == SearchField) { // live: results follow every keystroke
                const int n = GetWindowTextLengthW(h); std::wstring text(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(h, text.data(), n + 1); text.resize(static_cast<size_t>(n));
                self->SetQuery(std::move(text));
            }
            if (self->errors_[self->editing_]) { self->errors_[self->editing_] = false; self->Invalidate(); }
            InvalidateRect(h, nullptr, TRUE); return r;
        }
        case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 0); break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
    COLORREF Gray(float amount) const { const int v = static_cast<int>(std::lround(Tint(amount) * 255)); return RGB(v, v, v); }
    float EditFill() const { return editing_ == SearchField ? searchFill : FieldFill(0, true); }
    // The accent caret (#3E67FF at 90%), exact over the field's flat fill; danger while an address is invalid.
    void ApplyCaret() {
        if (!edit_) return;
        const bool error = editing_ != SearchField && errors_[editing_];
        const auto c = error ? Design::DangerRgb() : Design::Accent::Get();
        const COLORREF fill = Gray(EditFill());
        caret_.Apply(edit_, fill, NativeUI::AccentCaret::Mix(fill, RGB(std::lround(c.r * 255), std::lround(c.g * 255), std::lround(c.b * 255)), Design::Accent::caretOpacity), std::max(1, Px(1)), Px(18));
    }
    void StartEdit(int hit) {
        if (!hwnd_ || !state_) return;
        if (!edit_) {
            edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 1, 1, hwnd_, nullptr, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)), nullptr);
            if (!edit_) return;
            SendMessageW(edit_, EM_SETLIMITTEXT, 2048, 0);
            SetWindowSubclass(edit_, EditProc, 0, reinterpret_cast<DWORD_PTR>(this));
        }
        const UINT dpi = GetDpiForWindow(hwnd_);
        if (!editFont_ || editDpi_ != dpi) {
            if (editFont_) DeleteObject(editFont_);
            editFont_ = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display"); editDpi_ = dpi;
            SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), FALSE);
            SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        }
        editing_ = hit; cancelEdit_ = false; focus_ = hit;
        const std::wstring value = FieldValue(hit);
        SetWindowTextW(edit_, value.c_str());
        SendMessageW(edit_, EM_SETSEL, hit == SearchField ? value.size() : 0, -1); // a query resumes at its end
        editRect_ = {}; editShown_ = false; PlaceEdit(Motion::Now());
        SetFocus(edit_);
        Reveal(hit); Invalidate(); Schedule(Motion::Now());
    }
    // Lays the EDIT on the field's text line (follows scroll and the unfolding).
    void PlaceEdit(Motion::Time now) {
        if (!edit_ || !editing_) return;
        for (const auto& s : Slots(Current(), now)) {
            if (s.hit != editing_) continue;
            const float top = s.top + 6, bottom = s.top + 26;
            const bool shown = !page_.Moving(now) && top >= s.clipTop - .5f && bottom <= s.clipBottom + .5f &&
                               top >= Layout::viewTop - Layout::groupGap / 2 && bottom <= ViewBottom(Current());
            const bool search = editing_ == SearchField;
            const RECT r{Px(search ? SearchTextLeft() : Layout::textLeft), Px(top), Px(search ? ClearBox(s.top).left - 4 : Layout::rowRight - 8), Px(bottom)};
            if (EqualRect(&r, &editRect_) && shown == editShown_) return; // moving it repaints: only when it changes
            editRect_ = r; editShown_ = shown;
            SetWindowPos(edit_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | (shown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            return;
        }
        ShowWindow(edit_, SW_HIDE); editShown_ = false;
    }
    // Enter / Tab / focus loss: keep a valid address, flag an invalid one.
    void Commit(bool stay) {
        if (!editing_ || !state_) return;
        if (editing_ == SearchField) { EndEdit(); return; } // the query stays; nothing to validate
        const int hit = editing_; const auto now = Motion::Now(); using namespace BrowserSettings;
        const int n = GetWindowTextLengthW(edit_); std::wstring text(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(edit_, text.data(), n + 1); text.resize(static_cast<size_t>(n));
        if (TrimText(text).empty()) { // emptied: drop a pending choice, keep the saved one
            drafts_.erase(hit); errors_[hit] = false;
            if (hit == CustomSearchField) customPending_ = false; else homePending_ = false;
            EndEdit(); SyncReveals(now); return;
        }
        const std::wstring clean = hit == CustomSearchField ? SearchTemplate(text) : HomepageUrl(text);
        if (clean.empty()) { drafts_[hit] = text; errors_[hit] = true; if (!stay) EndEdit(); else ApplyCaret(); Invalidate(); Schedule(now); return; }
        drafts_.erase(hit); errors_[hit] = false;
        if (hit == CustomSearchField) {
            const bool changedText = clean != state_->customSearch; state_->customSearch = clean; customPending_ = false;
            if (changedText && hooks_.changed) hooks_.changed(Change::CustomSearch);
            if (state_->engine != Engine::Custom) { state_->engine = Engine::Custom; if (hooks_.changed) hooks_.changed(Change::Engine); }
        } else {
            const bool changedText = clean != state_->homepage; state_->homepage = clean; homePending_ = false;
            if (changedText && hooks_.changed) hooks_.changed(Change::Homepage);
            if (state_->newTabPage != NewTabPage::Custom) { state_->newTabPage = NewTabPage::Custom; if (hooks_.changed) hooks_.changed(Change::NewTabPage); }
        }
        EndEdit(); SyncReveals(now);
    }
    void EndEdit() {
        if (!editing_) return;
        const int hit = editing_; editing_ = 0;
        if (cancelEdit_ && hit != SearchField) { drafts_.erase(hit); errors_[hit] = false; if (hit == CustomSearchField) customPending_ = false; else homePending_ = false; SyncReveals(Motion::Now()); }
        cancelEdit_ = false;
        if (edit_) { ShowWindow(edit_, SW_HIDE); editShown_ = false; if (GetFocus() == edit_ && hwnd_) SetFocus(hwnd_); }
        focus_ = keyboard_ ? hit : 0; Invalidate(); Schedule(Motion::Now());
    }
    void RefreshTips() {
        if (!tip_) return;
        for (auto id : tips_) { TOOLINFOW t{}; t.cbSize = sizeof(t); t.hwnd = hwnd_; t.uId = id; SendMessageW(tip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&t)); }
        tips_.clear(); tipTexts_.clear(); tipTexts_.reserve(8);
        auto add = [&](UINT_PTR id, Design::Box b, std::wstring text) {
            tipTexts_.push_back(std::move(text));
            TOOLINFOW t{}; t.cbSize = sizeof(t); t.uFlags = TTF_SUBCLASS; t.hwnd = hwnd_; t.uId = id;
            t.rect = {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; t.lpszText = const_cast<wchar_t*>(tipTexts_.back().c_str());
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t)); tips_.push_back(id);
        };
        add(Close, Layout::CloseButton(), TR(settings_close_tip));
        const View view = Current();
        if (view != View::Main) add(Back, Layout::BackButton(), TR(common_back));
        if (view == View::Sites || view == View::Site) return;
        add(CheckUpdates, Layout::Trailing(Height() - Layout::padBottom - Layout::slot, Layout::slot), TR(settings_check_updates));
        for (const auto& s : Slots(view, Motion::Now())) if (s.kind == Kind::Folder && s.top >= Layout::viewTop - 1 && s.top + s.height <= ViewBottom(view)) {
            if (s.hit == Folder) add(Folder, Layout::Row(s.top, s.height), hooks_.downloadFolder ? hooks_.downloadFolder() : std::wstring(TR(settings_change_folder)));
            else add(static_cast<UINT_PTR>(s.hit), Layout::Row(s.top, s.height), hooks_.annotationFolder ? hooks_.annotationFolder() : L"");
        }
    }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Activated() { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); }
    void SyncReducedMotion() { motion_.SetReducedMotion(hooks_.reducedMotion ? hooks_.reducedMotion() : Motion::PreferredReducedMotion()); }

    // ---- Window ----------------------------------------------------------------
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    // The 332 DIP column docked to the owner's right edge under the toolbar. The
    // slide moves the window itself; the region clips it to the owner's client
    // area and rounds the corner it shares with it.
    void ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(Layout::width), top = owner.top + OwnerPx(Layout::top), height = std::max(1, static_cast<int>(owner.bottom - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        if (!force && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return;
        const bool resized = !EqualRect(&owner, &appliedOwner_);
        appliedVisible_ = shown; appliedOwner_ = owner;
        const RECT window{owner.right - shown, top, owner.right - shown + column, top + height};
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, column, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        HRGN region = nullptr;
        if (shown > 0) {
            const int r = OwnerPx(20);
            region = IsZoomed(owner_) ? CreateRectRgn(0, 0, shown, height) : CreateRoundRectRgn(-r * 2, -r * 2, shown + 1, height + 1, r, r);
        } else region = CreateRectRgn(0, 0, 0, 0);
        SetWindowRgn(hwnd_, region, FALSE);
        if (resized) { for (int d = 0; d <= depth_; ++d) scrolls_[d].Snap(std::clamp(scrolls_[d].Target(), 0.f, MaxScroll(views_[d], now, true))); RefreshTips(); Invalidate(); }
    }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = -1; appliedWindow_ = {};
        ShowWindow(hwnd_, SW_HIDE);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Drawer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Drawer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_NOTIFY: if (TooltipPlacement::OnNotify(tip_, l)) return TRUE; break; // tips centred on their tool
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case UiaInvoke: UiaAction(static_cast<int>(w), static_cast<long long>(l)); return 0;
        case WM_CTLCOLOREDIT: {
            { LRESULT r; if (editor_.CtlColor(reinterpret_cast<HWND>(l), reinterpret_cast<HDC>(w), r)) return r; }
            // The EDIT blends into the field: its exact fill, text at 90% white.
            HDC dc = reinterpret_cast<HDC>(w); const COLORREF fill = Gray(EditFill());
            SetTextColor(dc, Gray(.9f)); SetBkColor(dc, fill);
            if (editBrush_) DeleteObject(editBrush_);
            editBrush_ = CreateSolidBrush(fill); return reinterpret_cast<LRESULT>(editBrush_);
        }
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: pc_->Resize(); Invalidate(); return 0;
        case WM_DPICHANGED: ApplyGeometry(Motion::Now(), true); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !modal_) Hide(false, true); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            if (Current() == View::Profile) { const auto o = EditorOrigin(); if (editor_.Dragging()) { editor_.PointerMove(GET_X_LPARAM(l) * scale - o.x, GET_Y_LPARAM(l) * scale - o.y); return 0; } editor_.PointerMove(GET_X_LPARAM(l) * scale - o.x, GET_Y_LPARAM(l) * scale - o.y); }
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            if (hit != hover_) Interact(hit, pressed_);
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; editor_.PointerLeave(); Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: {
            if (Current() == View::Profile && !page_.Moving(Motion::Now())) {
                const auto o = EditorOrigin(); keyboard_ = false;
                if (editor_.PointerDown(GET_X_LPARAM(l) * scale - o.x, GET_Y_LPARAM(l) * scale - o.y)) { if (editor_.Dragging()) SetCapture(h); focus_ = ProfileEditorHit; Invalidate(); return 0; }
                if (editor_.Focus()) editor_.Blur();
            }
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); keyboard_ = false; if (editing_ && hit != editing_) Commit(false); if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: {
            if (editor_.Dragging()) { editor_.PointerUp(); if (GetCapture() == h) ReleaseCapture(); return 0; }
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale), was = pressed_;
            if (GetCapture() == h) ReleaseCapture();
            Interact(hit, 0); if (was && was == hit) Activate(hit, false);
            return 0;
        }
        case WM_XBUTTONUP: if (GET_XBUTTON_WPARAM(w) == XBUTTON1 && depth_ > 0) { Navigate(depth_ - 1, Motion::Now()); Repoint(); RefreshTips(); Schedule(Motion::Now()); } return TRUE;
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: { const auto now = Motion::Now(); ScrollTo(scrolls_[depth_].Target() - GET_WHEEL_DELTA_WPARAM(w) / 120.f * Layout::pitch * 1.5f, now); Repoint(); Schedule(now); return 0; }
        case WM_KEYDOWN: {
            if (focus_ == ProfileEditorHit && Current() == View::Profile && w != VK_ESCAPE && w != VK_BACK && editor_.Key(w)) { keyboard_ = true; Invalidate(); return 0; }
            // Escape and Backspace step back out of a sub-view; Escape on the main view closes.
            if (w == VK_ESCAPE && depth_ == 0 && Searching()) { keyboard_ = true; ClearSearch(); Interact(hover_, pressed_); return 0; }
            if ((w == VK_ESCAPE || w == VK_BACK || (w == VK_LEFT && GetKeyState(VK_MENU) < 0)) && depth_ > 0) { keyboard_ = true; Navigate(depth_ - 1, Motion::Now()); Interact(hover_, pressed_); RefreshTips(); return 0; }
            if (w == VK_ESCAPE) { Hide(true, true); return 0; }
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const auto order = FocusOrder();
                const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; Reveal(focus_); RaiseFocus();
                if (focus_ == ProfileEditorHit) editor_.SetFocusStop(back ? ProfileEditor::Hex : ProfileEditor::Name, true);
                Interact(hover_, pressed_); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            // Typing on the root starts a search (as Ctrl+F / F3 would).
            if (depth_ == 0 && ((w == 'F' && GetKeyState(VK_CONTROL) < 0) || w == VK_F3)) { keyboard_ = true; StartEdit(SearchField); return 0; }
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    Drawer() { slide_.Snap(0); page_.Snap(0); customReveal_.Snap(0); homeReveal_.Snap(0); }
    // Tests: the navigation stack and page position (to prove which view each frame paints).
    View CurrentViewForTest() const { return Current(); }
    void ActivateForTest(int hit, bool keyboard) { Activate(hit, keyboard); }   // as a click (false) or Entrée / Espace / UIA (true)
    int DepthForTest() const { return depth_; }
    float PageForTest(Motion::Time now) const { return page_.Value(now); }
    float SlideForTest(Motion::Time now) const { return slide_.Value(now); }
    bool PageMovingForTest(Motion::Time now) const { return page_.Moving(now); }
    // The views a frame at `now` paints (same rule as Paint): one, or two during a push / pop.
    std::vector<View> PaintedViewsForTest(Motion::Time now) const {
        const float pos = page_.Value(now); const int lo = std::clamp(static_cast<int>(std::floor(pos)), 0, MaxDepth - 1), hi = std::min(MaxDepth - 1, lo + 1);
        if (pos - lo < .001f || lo == hi) return {views_[lo]}; return {views_[lo], views_[hi]};
    }
    static std::pair<float, float> FadeReachForTest(float scroll, float max) { return FadeReach(scroll, max); }
    bool SitesNoteForTest(Motion::Time now) const { return !sitesLoaded_ && !LoadQuiet(now); }   // « Chargement… » drawn
    // « Annotations » tests: the laid-out rows, the Tab order, hit testing, the UIA tree, the agents.
    std::vector<Slot> SlotsForTest() const { return Slots(Current(), Motion::Now(), true); }
    std::vector<int> FocusOrderForTest() const { return FocusOrder(); }
    int HitForTest(float x, float y) const { return HitAt(x, y); }
    int FocusForTest() const { return focus_; }
    void KeyForTest(WPARAM key) { if (hwnd_) Handle(hwnd_, WM_KEYDOWN, key, 0); }
    std::vector<Accessibility::Node> AccessibleNodesForTest() const { return UiaNodes(); }
    static constexpr long long UiaKey(int hit) { return HitBase + hit; }   // a control's UIA key
    const AnnotationSettings::Agents& AgentsForTest() const { return agents_; }
    std::size_t AnnotationCountForTest() const { return annotationCount_; }
    Design::Box AgentActionForTest(const Slot& s) const { return AgentAction(s); }
    TrailingSpan TrailingForTest(const Slot& s) const { return Trailing(s); }
    float TextWidthForTest(const std::wstring& text, float size) const { return pc_->TextWidth(text, size, false, false, size >= 16 ? -.16f : -.14f); }
    void SettleForTest() { slide_.Snap(slide_.Target()); page_.Snap(page_.Target()); for (auto& l : scrolls_) l.Snap(l.Target()); for (int i = 0; i < SwitchCount; ++i) { switches_[i].Snap(switches_[i].Target()); knobPress_[i].Snap(0); } }
    void ScrollForTest(float y) { scrolls_[depth_].Snap(std::clamp(y, 0.f, MaxScroll(Current(), Motion::Now(), true))); }
    void HoverForTest(int hit) { Interact(hit, 0); }
    // Paints the drawer as it stands into a PNG (offscreen Direct2D: works on a locked desktop).
    bool RenderForTest(const std::wstring& png, UINT dpi, float height, const std::wstring& root) {
        Chrome off; if (FAILED(off.InitOffscreen(root, dpi, Layout::width, height))) return false;
        off.SetMotion(&motion_); off.textCacheLimit = 384;
        auto* keepFade = fade_; auto* keepStops = stops_; fade_ = nullptr; stops_ = nullptr; auto keepGlyphs = std::move(glyphs_); glyphs_.clear();
        pc_ = &off; heightOverride_ = height;
        off.RenderTarget()->BeginDraw(); Paint(Motion::Now()); const HRESULT drawn = off.RenderTarget()->EndDraw();
        const bool ok = SUCCEEDED(drawn) && SUCCEEDED(off.SavePng(png.c_str()));
        for (auto& g : glyphs_) Drop(g.second); glyphs_ = std::move(keepGlyphs); Drop(fade_); Drop(stops_); fade_ = keepFade; stops_ = keepStops;
        pc_ = &chrome_; heightOverride_ = 0; off.Dispose();
        return ok;
    }
    ~Drawer() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, BrowserSettings::State* state, Hooks hooks) {
        owner_ = owner; state_ = state; hooks_ = std::move(hooks);
        if (state_) { launchAutoplay_ = state_->autoplay; launchCookies_ = state_->blockThirdPartyCookies; launchGrayscale_ = state_->grayscaleText; } // engine flags in effect for this run
        motion_.EnablePolish(); pc_->SetMotion(&motion_); pc_->textCacheLimit = 384;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereSettingsDrawer";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, TR(settings_title), WS_POPUP | WS_CLIPCHILDREN, 0, 0, Px(Layout::width), Px(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        NativeUI::StripTooltipShadow(tip_);
        NativeUI::ConfigureShadowlessPopup(hwnd_); // DWMWCP_DONOTROUND, no shadow: the region shapes it
        AttachEditor(); AttachUia();
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() {
        if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
        if (editBrush_) { DeleteObject(editBrush_); editBrush_ = nullptr; }
        edit_ = nullptr; editing_ = 0; uia_.Detach(); editor_.Detach();
        for (auto& g : glyphs_) Drop(g.second); glyphs_.clear();
        Drop(fade_); Drop(stops_); chrome_.Dispose(); hwnd_ = tip_ = nullptr; tips_.clear(); visible_ = false; }
    HWND Window() const { return hwnd_; }
    UINT RenderDpiForTest() const { return chrome_.dpi; }   // the DPI the drawer is drawn at (tests/ui/address_popup_dpi_test.cpp)
    bool Visible() const { return visible_; }
    bool Closing() const { return closing_; }
    bool Open() const { return visible_ && !closing_; }
    // Slides in from the window's right edge; reopening mid-exit continues from
    // where the drawer is on screen.
    void Show() { Show(View::Main); }
    // Opens on `target`. Closed (or leaving): the stack is rebuilt as Main > target and the page
    // position snapped there BEFORE the first paint and the slide, so the drawer enters already on
    // its view (no root page sliding away under it). Already open: the normal push / pop.
    void Show(View target) {
        if (!hwnd_ || !state_) return;
        SyncReducedMotion();
        auto now = Motion::Now(); const bool reduced = Reduced();
        const bool cold = !visible_ || closing_;
        if (visible_ && !closing_ && target != View::Main && Current() != target) {
            if (target == View::Sites) LoadSites();
            if (target == View::Annotations) LoadAnnotations();
            if (depth_ >= 1 && views_[1] == target) Navigate(1, now);          // e.g. a site's detail: back to the list
            else if (depth_ + 1 < MaxDepth) Push(target, now);               // from what is on screen, like a row
            else { views_[1] = target; depth_ = 1; page_.Snap(1); scrolls_[1].Snap(0); focus_ = 0; motion_.ClearInteractions(); }
            Repoint(); RefreshTips(); Invalidate(); Schedule(now);
            return;
        }
        if (cold && visible_ && target != View::Main) {   // re-shown while leaving: straight onto the target
            if (editing_) Commit(false);
            views_.fill(View::Main); views_[1] = target; depth_ = 1; page_.Snap(1); scrolls_[1].Snap(0); site_ = -1; focus_ = 0; motion_.ClearInteractions();
            if (target == View::Sites) LoadSites();
        }
        if (!visible_) {
            slide_.Snap(0); for (auto& lane : scrolls_) lane.Snap(0); focus_ = 0; keyboard_ = false; hover_ = pressed_ = 0; motion_.ClearInteractions();
            depth_ = 0; views_.fill(View::Main); page_.Snap(0); site_ = -1; query_.clear(); results_.clear();
            customPending_ = homePending_ = false; drafts_.clear(); errors_.clear(); SyncReveals(now, true);
            MeasureDescriptions();
            for (int i = 0; i < SwitchCount; ++i) { switches_[i].Snap(SwitchValue(SwitchHits[i]) ? 1.f : 0.f); knobPress_[i].Snap(0); }
            LoadSites(); // the Autorisations row shows how many sites
            LoadAnnotations(); // the « Annotations » row shows the connected agents
            if (hooks_.savedLanguage) { languageSaved_ = hooks_.savedLanguage(); languageFailed_ = false; }   // « Langue »: the preference as stored now
            if (target != View::Main) { views_[1] = target; depth_ = 1; page_.Snap(1); scrolls_[1].Snap(0); }
        }
        { const auto version = hooks_.version ? hooks_.version() : Version{}; SetChecking(version.checking, now); }
        closing_ = false; visible_ = true;
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); Activated();
        now = Motion::Now();
        slide_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Downloads::DrawerMotion::enter, Downloads::DrawerMotion::enterSpring);
        ApplyGeometry(now); Invalidate();
        Repoint(); RefreshTips(); Schedule(now);
    }
    // Opens straight on « Autorisations des sites » (B02 « Autorisations du site » link).
    void ShowSites() { Show(View::Sites); }
    // Opens on Réglages › Profils (profile menu « Gérer les profils… »).
    void ShowProfiles() { RefreshProfiles(); Show(View::Profiles); }
    // Opens straight on « Annotations » (no-op section when the annotation mode is compiled out).
    void ShowAnnotations() { if (AnnotationsOn()) Show(View::Annotations); else Show(); }
    // The agents or the store changed outside the drawer (connect modal, clear all done, …): ask again.
    void AnnotationsChanged() { if (hwnd_) LoadAnnotations(); }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64();
        if (editing_) Commit(false);
        editor_.Blur(); editor_.Hide();
        if (GetCapture() == hwnd_) ReleaseCapture();
        motion_.ClearInteractions(); hover_ = pressed_ = 0; tracking_ = false;
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !Reduced() ? Downloads::DrawerMotion::exit : Motion::Milliseconds(0), Downloads::DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_) Schedule(now);
    }
    void Toggle() { if (Open()) Hide(false, true); else Show(); }
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    // The state or the version status changed outside the drawer: follow it.
    void Changed() {
        if (!hwnd_ || !visible_ || !state_) return;
        RefreshProfiles();
        const auto now = Motion::Now(); const bool reduced = Reduced();
        for (int i = 0; i < SwitchCount; ++i) switches_[i].Set(SwitchValue(SwitchHits[i]) ? 1.f : 0.f, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
        SyncReveals(now);
        plate_.Set(EngineTop(SelectedIndex()), now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
        SetChecking(hooks_.version && hooks_.version().checking, now);
        if (AnnotationsOn()) MeasureAgents();   // « il y a … » follows the clock
        if (Searching()) results_ = SearchSettings(query_, AnnotationsOn(), static_cast<bool>(hooks_.importData));
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RefreshTips(); Invalidate(); Schedule(now);
    }
    void SetReducedMotion() { SyncReducedMotion(); Invalidate(); }
    bool Moving(Motion::Time now) const {
        bool moving = slide_.Moving(now) || plate_.Moving(now) || page_.Moving(now) || Spinning(now) || customReveal_.Moving(now) || homeReveal_.Moving(now);
        for (auto& lane : scrolls_) moving = moving || lane.Moving(now);
        for (auto& lane : switches_) moving = moving || lane.Moving(now);
        for (auto& lane : knobPress_) moving = moving || lane.Moving(now);
        return moving;
    }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        auto wake = Motion::Earlier(motion_.NextWake(now), Moving(now) ? Motion::Frame() : Motion::Wake{});
        if (LoadQuiet(now)) wake = Motion::Earlier(wake, Motion::Deadline(now, loadStart_ + Motion::Milliseconds(loadGraceMs)));   // then « Chargement… »
        return wake;
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    void Frame(Motion::Time now) {
        if (!visible_) return;
        ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        PlaceEdit(now); SyncEditor(now);
        // A window keeps only what it is painted with while its region shows it:
        // the entering slide repaints as it uncovers; the exit moves finished pixels.
        const bool content = (Moving(now) && !(closing_ && !page_.Moving(now))) || motion_.NextWake(now).delay.has_value();
        if (content || animatingPainted_) Invalidate();
        // Rows moved under a still pointer (fold, scroll, navigation): hover follows the layout.
        if (!content && animatingPainted_) { RefreshTips(); Repoint(); }
        animatingPainted_ = content;
    }
};
} // namespace SettingsUI
} // namespace NativeChrome
