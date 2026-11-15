#pragma once
#include "i18n.h"
// Origin prompts (Paper p-D-0, B01–B08): requests that come from a site, drawn by the
// browser. One Modal::Dialog surface (same material, zero shadow) in two placements:
//   * anchored popover under the address bar's origin indicator (B01/B02 permissions,
//     B03/B04 passwords, B07 dangerous download): 340/360 DIP, no scrim, light dismiss;
//   * 400 DIP window centred in the page area over a 40 % scrim, blocking that tab only
//     (B06 HTTP authentication and external application, B08 concepts).
//
// API (final):
//   NativeChrome::OriginPrompt::Prompt
//     bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks{wake, choose, menu, dismiss, download})
//     void Dispose()                       // done({Dismiss}) if still pending
//     bool Show(Spec, RECT anchor, std::function<void(Result&)> done)  // false if one is open
//         `anchor`: screen rect of the origin indicator (centred kinds use SetPageArea's rect,
//         or the owner's client area when none was given).
//     void SetPageArea(RECT pageScreen)    // page rect for the centred kinds (B06/B08)
//     void Abort()                         // done({Dismiss}), then closes
//     void Reposition(RECT anchor)
//     void AuthRefused()                   // B06: « Identifiant ou mot de passe refusé », focus password
//     bool Visible() const; HWND Window() const; Motion::Wake NextWake(Motion::Time) const;
//     void Frame(Motion::Time); void SetReducedMotion(bool)
// `done` is called exactly once. Result::password is wiped (SecureZeroMemory) right after
// `done` returns, and the EDIT buffer is overwritten when the prompt closes: copy what you
// need inside the callback, never keep a reference.
// Password kinds (B03/B04) and the B08 concepts are demonstrations: fictitious accounts held
// by the caller in memory, a « Démonstration » badge, nothing stored, nothing read from a page.
#include "modal.h"

namespace NativeChrome {
namespace OriginPrompt {
enum class Kind { Permission, BasicAuth, ExternalApp, DangerousDownload, ClientCertificate, ScreenShare, KeepFile, PasswordSave, PasswordUpdate, PasswordFill, SiteStatus };
enum class Action { Dismiss, Allow, AllowOnce, Block, Submit, Cancel, Open, Keep, Delete, Choose, Save, Never, Fill, Manage };
// COREWEBVIEW2_PERMISSION_KIND values (no WebView2 header needed here).
namespace Permission {
inline constexpr int Unknown = 0, Microphone = 1, Camera = 2, Geolocation = 3, Notifications = 4, OtherSensors = 5, ClipboardRead = 6,
    MultipleDownloads = 7, FileReadWrite = 8, Autoplay = 9, LocalFonts = 10, Midi = 11, WindowManagement = 12, PersistentStorage = 13;
}
struct DemoAccount { std::wstring user, hint; bool disabled = false; };
struct Spec {
    Kind kind = Kind::Permission;
    std::wstring origin;                 // host of the requesting top-level site
    std::wstring detail;                 // BasicAuth: server challenge (realm); ExternalApp: target URI;
                                         // DangerousDownload/KeepFile: file name; ScreenShare: unused
    std::vector<int> permissions;        // Permission values, merged per origin by the host
    bool userInitiated = true, demo = false;
    std::vector<DemoAccount> accounts;   // PasswordFill (fictitious), ClientCertificate (name + issuer)
    std::wstring user;                   // BasicAuth / PasswordSave|Update: pre-filled identifier
    std::wstring secret;                 // PasswordSave|Update, real path only: the password just submitted (wiped by the host after)
    // Extras (all optional):
    std::wstring frame;                  // ExternalApp: third-party frame host (no remembering offered)
    std::wstring app;                    // ExternalApp: application name (« Zoom Workplace »), else generic
    std::wstring reason;                 // DangerousDownload: « Microsoft Defender SmartScreen le signale… »
    std::wstring blockedTitle;           // DangerousDownload card header by engine reason (empty: demo « fichier dangereux »)
    bool reportable = true;              // DangerousDownload: « Signaler une erreur à Microsoft » applies (FILE_MALICIOUS only)
    bool insecure = false;               // BasicAuth over plain HTTP
    // DangerousDownload: « Conserver quand même… » only where keeping is real (the sandbox demo):
    // WebView2 has no API to override a SmartScreen / security verdict (no danger type, no keep).
    bool keepAvailable = true;
    std::wstring link;                   // DangerousDownload: the download's own URI (« Copier le lien du fichier »)
    std::vector<std::wstring> sources;   // ScreenShare: category names (« Onglets », « Fenêtres », « Écrans »)
    std::vector<Modal::Source> shareSources;   // ScreenShare: fictitious sources (title, category, thumbnail art)
    // SiteStatus (B02 « Clic sur la puce », EAC-0): what this page may use. `active` only from a
    // verified in-use source (the sandbox demonstration), `revoked` turned off from this popover.
    std::vector<int> granted, active, blocked, revoked, remembered;
    // Origin prompts come from pages (or the address-bar chip): pointer unless the host knows better.
    Modal::Spec::Modality modality = Modal::Spec::Modality::Pointer;
};
struct Result { Action action = Action::Dismiss; bool remember = false; std::wstring user, password; int choice = -1; };

namespace Text {
inline std::wstring Noun(int p) {
    switch (p) {
    case Permission::Camera: return TR(perm_noun_camera); case Permission::Microphone: return TR(perm_noun_microphone); case Permission::Geolocation: return TR(perm_noun_location);
    case Permission::Notifications: return TR(perm_noun_notifications); case Permission::ClipboardRead: return TR(perm_noun_clipboard); case Permission::Midi: return TR(perm_noun_midi);
    case Permission::MultipleDownloads: return TR(perm_noun_downloads); case Permission::OtherSensors: return TR(perm_noun_sensors);
    case Permission::FileReadWrite: return TR(perm_noun_files); case Permission::Autoplay: return TR(perm_noun_autoplay); case Permission::LocalFonts: return TR(perm_noun_fonts);
    case Permission::WindowManagement: return TR(perm_noun_windows); case Permission::PersistentStorage: return TR(perm_noun_storage); default: return TR(perm_noun_generic);
    }
}
// Single-permission titles. Paper exports: position, notifications, clipboard (B02); the others
// follow the same « <site> veut … » grammar.
inline std::wstring Title(const std::wstring& o, int p) {
    switch (p) {
    case Permission::Camera: return TRF(perm_title_camera, o);
    case Permission::Microphone: return TRF(perm_title_microphone, o);
    case Permission::Geolocation: return TRF(perm_title_location, o);
    case Permission::Notifications: return TRF(perm_title_notifications, o);
    case Permission::ClipboardRead: return TRF(perm_title_clipboard, o);
    case Permission::Midi: return TRF(perm_title_midi, o);
    case Permission::MultipleDownloads: return TRF(perm_title_downloads, o);
    case Permission::OtherSensors: return TRF(perm_title_sensors, o);
    case Permission::FileReadWrite: return TRF(perm_title_files, o);
    case Permission::Autoplay: return TRF(perm_title_autoplay, o);
    case Permission::LocalFonts: return TRF(perm_title_fonts, o);
    case Permission::WindowManagement: return TRF(perm_title_windows, o);
    case Permission::PersistentStorage: return TRF(perm_title_storage, o);
    default: return TRF(perm_title_generic, o);
    }
}
inline std::wstring Subtitle(int p) {
    switch (p) {
    case Permission::Geolocation: return TR(perm_sub_location);
    case Permission::Notifications: return TR(perm_sub_notifications);   // no proof for « même onglet fermé » (design/origin-prompt-help.md §2)
    case Permission::ClipboardRead: return TR(perm_sub_clipboard);
    case Permission::MultipleDownloads: return TR(perm_sub_downloads);
    case Permission::Midi: return TR(perm_sub_midi);
    case Permission::Camera: return TR(perm_sub_camera);
    case Permission::Microphone: return TR(perm_sub_microphone);
    case Permission::OtherSensors: return TR(perm_sub_sensors);
    case Permission::FileReadWrite: return TR(perm_sub_files);
    case Permission::Autoplay: return TR(perm_sub_autoplay);
    case Permission::LocalFonts: return TR(perm_sub_fonts);
    case Permission::WindowManagement: return TR(perm_sub_windows);
    case Permission::PersistentStorage: return TR(perm_sub_storage);
    case Permission::Unknown: return TR(perm_sub_unknown);
    default: return TRF(perm_sub_unrecognized, std::to_wstring(p));   // a newer engine kind: never guessed
    }
}
inline const char* Glyph(int p) {
    switch (p) {
    case Permission::Camera: return DialogKit::Glyph::camera; case Permission::Microphone: return DialogKit::Glyph::microphone;
    case Permission::Geolocation: return DialogKit::Glyph::position; case Permission::Notifications: return DialogKit::Glyph::bell;
    case Permission::ClipboardRead: return DialogKit::Glyph::clipboard; case Permission::MultipleDownloads: return DialogKit::Glyph::download;
    case Permission::FileReadWrite: return DialogKit::Glyph::file; case Permission::WindowManagement: return DialogKit::Glyph::screen;
    case Permission::PersistentStorage: return DialogKit::Glyph::storage;
    default: return DialogKit::Glyph::info;
    }
}
}

// Button ids inside the dialog (Modal::Dialog ids; mapped back to Action).
namespace Id { inline constexpr int Cancel = Modal::CancelButton, Accept = Modal::AcceptButton, Never = 10, Later = 11, More = 12, Manage = 13, CopyLink = 14; }

// The Modal::Spec for an origin prompt (pure: unit-testable without a window).
inline Modal::Spec Build(const Spec& s, bool anchored) {
    using namespace Modal;
    Modal::Spec m; m.kind = Modal::Kind::Origin; m.demo = s.demo;
    auto button = [](int id, std::wstring label, Role role, bool def, bool cancel, bool initial = false, std::wstring aid = L"") { return Make(id, std::move(label), role, def, cancel, initial, std::move(aid)); };
    switch (s.kind) {
    case Kind::Permission: {
        m.automationId = L"PermissionPrompt"; m.scope = Scope::Anchor; m.width = 340; m.closeButton = true;
        const bool many = s.permissions.size() > 1;
        const int p = s.permissions.empty() ? Permission::Unknown : s.permissions[0];
        if (many) {
            m.title = TRF(perm_title_many, s.origin);
            for (int q : s.permissions) m.rows.push_back({Text::Glyph(q), Text::Noun(q), q == Permission::Geolocation ? TR(perm_approximate) : L""});
        } else { m.title = Text::Title(s.origin, p); m.subtitle = Text::Subtitle(p); m.headGlyph = Text::Glyph(p); }
        const bool always = !many && p == Permission::Notifications;   // B02: notifications are always remembered
        // Paper B02 (user edit 2026-11-12): notifications = title, subtitle, buttons; no note, no check (always remembered).
        if (!always) { m.checkLabel = TR(perm_remember); m.originCheck = true; }
        m.stretchButtons = true; m.softButtons = true;
        m.buttons = {button(Id::Cancel, TR(permission_choice_block), Role::Secondary, false, false, false, L"PermissionBlock"), button(Id::Accept, TR(permission_choice_allow), Role::Primary, true, false, false, L"PermissionAllow")};
        m.checkValue = always;
        break;
    }
    case Kind::BasicAuth: {
        // Popover title role 16/20 Medium (user override of Paper's 20/24 SemiBold, as B06).
        m.automationId = L"HttpAuthentication"; m.scope = Scope::Page; m.title = TR(auth_title);
        m.subtitle = TRF(auth_subtitle, s.origin);
        if (!s.detail.empty()) m.subtitle += L" « " + s.detail + L" »";   // the site's words, quoted (B06 note)
        if (s.insecure) m.notices.push_back({DialogKit::Glyph::lockOpen, TR(auth_insecure), true});
        Field user; user.label = TR(auth_user); user.value = s.user; user.automationId = L"AuthUser"; user.maxLength = 1024;
        Field pass; pass.label = TR(settings_passwords_singular); pass.password = true; pass.automationId = L"AuthPassword"; pass.maxLength = 1024;
        m.fields = {user, pass};
        m.softButtons = true;
        m.buttons = {button(Id::Cancel, TR(common_cancel), Role::Secondary, false, true, false, L"AuthCancel"), button(Id::Accept, TR(auth_sign_in), Role::Primary, true, false, false, L"AuthSubmit")};
        break;
    }
    case Kind::ExternalApp: {
        // Title in the popovers' 16/20 role (user override of Paper's 20/24), tile on the title line.
        m.automationId = L"ExternalApplication"; m.scope = Scope::Page; m.tileGlyph = DialogKit::Glyph::external; m.softButtons = true;
        std::wstring scheme = s.detail.substr(0, s.detail.find(L':'));
        if (!s.frame.empty()) {
            m.title = TR(external_title);
            m.subtitle = TRF(external_frame_subtitle, s.origin, s.frame, scheme);
            m.notices.push_back({DialogKit::Glyph::info, TR(external_frame_notice), false});
            // B06 note: « Annuler » is the default when a third-party frame asks.
            m.buttons = {button(Id::Cancel, TR(common_cancel), Role::Secondary, true, true, true, L"ExternalCancel"), button(Id::Accept, TR(downloads_action_open), Role::Secondary, false, false, false, L"ExternalOpen")};
        } else {
            m.title = s.app.empty() ? std::wstring(TR(external_title)) : TRF(external_title_app, s.app);
            m.subtitle = TRF(external_subtitle, s.origin);
            if (!scheme.empty()) { m.checkLabel = TRF(external_always, scheme); m.originCheck = true; m.checkInHeadLane = true; }
            const std::wstring short_ = s.app.empty() ? std::wstring(TR(downloads_action_open)) : TRF(external_open_app, s.app.substr(0, s.app.find(L' ')));
            m.buttons = {button(Id::Cancel, TR(common_cancel), Role::Secondary, false, true, false, L"ExternalCancel"), button(Id::Accept, short_, Role::Primary, true, false, true, L"ExternalOpen")};
        }
        break;
    }
    case Kind::DangerousDownload: {
        m.automationId = L"DangerousDownload"; m.scope = Scope::Anchor; m.anchorRight = true; m.width = 360; m.gap = 12;
        m.title = TR(settings_downloads);
        m.danger = {s.detail, s.blockedTitle.empty() ? std::wstring(TR(download_danger_title)) : s.blockedTitle, s.reason.empty() ? std::wstring(TR(download_danger_reason)) : s.reason, true};
        m.stretchButtons = false;
        Button del = button(Id::Accept, TR(download_delete_file), Role::Danger, false, false, true, L"DownloadDelete");
        // Decision 4: only a malware verdict can be contested at Microsoft; otherwise the one real extra
        // action, « Copier le lien », is a direct Quiet button (Paper KV2-0 / KVQ-0), no one-row menu.
        Button more = s.reportable ? button(Id::More, TR(download_more_options), Role::Quiet, false, false, false, L"DownloadMore")
                                   : button(Id::CopyLink, TR(chrome_tip_copy_link), Role::Quiet, false, false, false, L"DownloadCopyLink");
        more.closes = false;
        m.buttons = {del, more}; m.stretchButtons = true;
        break;
    }
    case Kind::KeepFile: {   // B08: confirmation after « Conserver quand même… »
        m.automationId = L"KeepDangerousFile"; m.scope = Scope::Page; m.title = TR(keep_danger_title);
        m.body = TR(keep_danger_body);
        if (!s.detail.empty()) m.infos.push_back({s.detail, L""});
        m.buttons = {button(Id::Cancel, TR(common_cancel), Role::Secondary, true, true, true, L"KeepCancel"), button(Id::Accept, TR(keep_danger_action), Role::Danger, false, false, false, L"KeepConfirm")};
        break;
    }
    case Kind::ClientCertificate: {   // B08: ICoreWebView2_5::ClientCertificateRequested, the engine's mutually trusted certificates
        m.automationId = L"ClientCertificate"; m.scope = Scope::Page; m.title = TR(cert_title);
        m.subtitle = TRF(cert_subtitle, s.origin);
        m.accountsTitle = TR(cert_available);
        for (auto& a : s.accounts) m.accounts.push_back({a.user, a.hint, true, a.disabled});   // issuer line: plain text, no dot tracking; expired = disabled
        // E1 (planche validée): rows only select (radios); « Envoyer » alone transmits, disabled until a
        // valid, non-expired certificate is selected; « Ne pas envoyer » / Échap continue without one.
        m.accountsSelect = true; m.accountSelected = -1;
        m.buttons = {button(Id::Cancel, TR(cert_dont_send), Role::Secondary, false, true, false, L"CertificateCancel"), button(Id::Accept, TR(cert_send), Role::Primary, true, false, false, L"CertificateSend")};
        m.buttons[1].disabled = true;
        break;
    }
    case Kind::ScreenShare: {   // B08 concept (Démonstration): the engine's picker cannot be replaced
        // Same 16/20 title as every popover: the 640 DIP picker's hierarchy comes from its thumbnails.
        m.automationId = L"ScreenShare"; m.scope = Scope::Page; m.title = TRF(share_title, s.origin); m.demo = true;
        // A larger picker with thumbnails (user request 2026-11-09): pick a category, select a source,
        // then « Partager ». Thumbnails are drawn locally for the demonstration: nothing is captured.
        m.width = 640;
        m.sources.shown = true; m.sources.categories = s.sources.empty() ? std::vector<std::wstring>{TR(settings_tabs), TR(share_windows), TR(share_screens)} : s.sources;
        m.sources.items = s.shareSources; m.sources.category = 0; m.sources.selected = -1; m.sources.acceptId = Id::Accept;
        for (size_t c = 0; c < m.sources.categories.size(); ++c) { bool any = false; for (auto& i : m.sources.items) any = any || i.category == static_cast<int>(c); if (any) { m.sources.category = static_cast<int>(c); break; } }
        m.notices.push_back({DialogKit::Glyph::info, TR(share_demo_notice), false});
        m.softButtons = true;
        Button share = button(Id::Accept, TR(share_action), Role::Primary, true, false, false, L"ShareConfirm"); share.disabled = true;   // until a source is selected
        m.buttons = {button(Id::Cancel, TR(common_cancel), Role::Secondary, false, true, false, L"ShareCancel"), share};
        break;
    }
    case Kind::SiteStatus: {   // B02 EAC-0 / EBA-0: opened from the address-bar chip, never asks anything
        m.automationId = L"SitePermissions"; m.scope = Scope::Anchor; m.width = 340; m.demo = s.demo;
        const bool inUse = !s.active.empty();
        m.title = inUse ? TRF(site_status_uses, s.origin) : !s.granted.empty() ? TRF(site_status_may_use, s.origin) : TRF(site_status_refused, s.origin);
        m.subtitle = s.demo ? std::wstring(inUse ? TR(site_status_demo_in_use) : TR(site_status_demo)) : std::wstring(TR(site_status_engine_note));
        for (int k : s.granted) {
            Row r{Text::Glyph(k), Text::Noun(k), std::find(s.active.begin(), s.active.end(), k) != s.active.end() ? TR(demo_in_progress) : L"", 1};
            const bool kept = std::find(s.remembered.begin(), s.remembered.end(), k) != s.remembered.end();
            r.detail = s.demo ? std::wstring(TR(site_status_demo_toggle)) : std::wstring(kept ? TR(site_status_kept_toggle) : TR(site_status_page_toggle));
            m.rows.push_back(r);
        }
        for (int k : s.revoked) { Row r{Text::Glyph(k), Text::Noun(k), TR(site_status_revoked), 0}; r.detail = TR(site_status_revoked_note); m.rows.push_back(r); }
        for (int k : s.blocked) m.rows.push_back(Row{Text::Glyph(k), Text::Noun(k), TR(permission_blocked), -1});
        m.links = {{Id::Manage, TR(site_status_manage)}};
        break;
    }
    case Kind::PasswordSave: case Kind::PasswordUpdate: {   // B04, Démonstration: nothing is stored
        const bool update = s.kind == Kind::PasswordUpdate;
        m.automationId = update ? L"PasswordUpdate" : L"PasswordSave"; m.scope = Scope::Anchor; m.width = 360; m.closeButton = true; m.demo = s.demo;
        m.title = update ? TR(password_update_title) : TR(password_save_title);
        m.subtitle = s.demo ? TRF(password_subtitle_demo, s.origin) : TRF(password_subtitle, s.origin);
        Field user; user.label = TR(auth_user); user.value = s.user; user.automationId = L"PasswordUser"; user.required = true;
        Field pass; pass.label = TR(settings_passwords_singular); pass.password = true; pass.reveal = true; pass.automationId = L"PasswordValue";
        pass.value = !s.demo ? s.secret : s.accounts.empty() ? L"exemple-fictif-2026" : s.accounts[0].hint;   // real: the submitted password; demo: fictitious
        m.fields = {user, pass};
        m.validate = [](const std::vector<std::wstring>& v) { return v.empty() || v[0].empty() ? std::wstring(TR(password_need_user)) : std::wstring(); };
        // D1: an update from a form without an identifier, with several accounts for this origin: the
        // user picks which one (radio rows, « Mettre à jour » disabled until then); never the last one silently.
        if (!s.demo && update && s.user.empty() && s.accounts.size() > 1) {
            m.fields = {pass}; m.validate = nullptr; m.accountsTitle = TR(password_which_account);
            for (auto& a : s.accounts) m.accounts.push_back({a.user, a.hint, true, false});
            m.accountsSelect = true; m.accountSelected = -1;
        }
        m.softButtons = true;
        Button never = button(Id::Never, TR(password_never_site), Role::Quiet, false, false, false, L"PasswordNever"); never.leading = true;
        if (!s.demo) {   // real path: no « Jamais pour ce site » until a real exclusion list exists (no inert action)
            // Review F3: armed (500 ms, foreground) and, for an update, no default Entrée: replacing a stored
            // password always takes a deliberate click or Tab + Espace on « Mettre à jour ».
            m.armMs = 500;
            m.buttons = {button(Id::Later, TR(common_later), Role::Secondary, false, true, false, L"PasswordLater"), button(Id::Accept, update ? TR(password_update) : TR(password_save), Role::Primary, !update, false, false, L"PasswordSaveConfirm")};
            if (m.accountsSelect) m.buttons[1].disabled = true;
            break;
        }
        m.buttons = {never, button(Id::Later, TR(common_later), Role::Secondary, false, true, false, L"PasswordLater"), button(Id::Accept, update ? TR(password_update) : TR(password_save), Role::Primary, true, false, false, L"PasswordSave")};
        break;
    }
    case Kind::PasswordFill: {   // B03/B04, Démonstration: fictitious accounts held in memory
        m.automationId = L"PasswordFill"; m.scope = Scope::Anchor; m.width = 360; m.padding = 6; m.gap = 2; m.demo = s.demo;   // real: the vault's accounts for the EFFECTIVE origin, no secret here
        if (!s.demo) m.armMs = 500;   // V5: a row cannot be chosen by a click the page arranged
        m.accountsTitle = s.accounts.empty() ? TRF(password_no_account, s.origin) : TRF(password_accounts, s.origin);
        for (auto& a : s.accounts) m.accounts.push_back({a.user, std::wstring(std::max<size_t>(8, std::min<size_t>(14, a.hint.size())), L'\x2022')});
        m.manageLabel = TR(settings_manage_passwords); m.manageId = Id::Manage;
        break;
    }
    }
    if (!anchored && m.scope == Scope::Anchor) m.scope = Scope::Page;
    return m;
}
inline Result Map(const Spec& s, const Modal::Result& r) {
    Result out; out.remember = r.checked;
    switch (s.kind) {
    case Kind::Permission:
        if (r.button == Id::Accept) out.action = Action::Allow; else if (r.button == Id::Cancel) out.action = Action::Block; else { out.action = Action::Dismiss; out.remember = false; }
        if (s.permissions.size() == 1 && s.permissions[0] == Permission::Notifications && out.action != Action::Dismiss) out.remember = true;
        break;
    case Kind::BasicAuth:
        if (r.button == Id::Accept) { out.action = Action::Submit; if (r.values.size() > 1) { out.user = r.values[0]; out.password = r.values[1]; } } else out.action = Action::Cancel;
        out.remember = false; break;
    case Kind::ExternalApp: out.action = r.button == Id::Accept ? Action::Open : Action::Cancel; if (!s.frame.empty()) out.remember = false; break;
    // « Plus d'options » never answers by itself: Keep only comes from the B08 confirmation (KeepFile).
    case Kind::DangerousDownload: out.action = r.button == Id::Accept ? Action::Delete : Action::Dismiss; break;
    case Kind::KeepFile: out.action = r.button == Id::Accept ? Action::Keep : Action::Cancel; break;
    case Kind::SiteStatus: out.action = r.button == Id::Manage ? Action::Manage : Action::Dismiss; break;
    // « Ne pas envoyer » / Échap (the cancel button) = continue without; an abort (tab closed, navigation) = Dismiss.
    case Kind::ClientCertificate: if (r.button == Id::Accept && r.select >= 0) { out.action = Action::Choose; out.choice = r.select; } else out.action = r.button == Id::Cancel ? Action::Cancel : Action::Dismiss; break;
    case Kind::ScreenShare: if (r.button == Id::Accept && r.select >= 0) { out.action = Action::Choose; out.choice = r.select; } else out.action = Action::Cancel; break;
    case Kind::PasswordSave: case Kind::PasswordUpdate:
        out.action = r.button == Id::Accept ? Action::Save : r.button == Id::Never ? Action::Never : r.button == Id::Later ? Action::Cancel : Action::Dismiss;
        if (r.button == Id::Accept && !r.values.empty()) {
            if (s.demo) out.user = r.values[0];   // the (fictitious) password is never returned
            else if (!s.accounts.empty() && s.user.empty() && s.kind == Kind::PasswordUpdate && s.accounts.size() > 1) { if (r.select >= 0 && r.select < static_cast<int>(s.accounts.size())) { out.user = s.accounts[static_cast<size_t>(r.select)].user; out.password = r.values[0]; out.choice = r.select; } else out.action = Action::Dismiss; }
            else { out.user = r.values[0]; if (r.values.size() > 1) out.password = r.values[1]; }
        }
        break;
    case Kind::PasswordFill:
        if (r.button == Id::Accept && r.select >= 0) { out.action = Action::Fill; out.choice = r.select; if (r.select < static_cast<int>(s.accounts.size())) out.user = s.accounts[static_cast<size_t>(r.select)].user; }
        else out.action = r.button == Id::Manage ? Action::Manage : Action::Dismiss;
        break;
    }
    return out;
}

class Prompt {
public:
    // B07 « Plus d'options » commands (Paper FYS-0): two plain rows, a separator, the danger row.
    enum Command { CopyLink = 1, Report = 2, KeepAnyway = 3 };
    static std::vector<ContextMenu::Item> MoreItems(bool keep = true) {
        std::vector<ContextMenu::Item> items{ContextMenu::Command(CopyLink, TR(download_copy_file_link)), ContextMenu::Command(Report, TR(download_report_microsoft))};
        if (keep) { auto k = ContextMenu::Command(KeepAnyway, TR(download_keep_anyway)); k.danger = true; items.push_back(ContextMenu::Separator()); items.push_back(k); }
        return items;
    }
    struct Hooks {
        std::function<void()> wake;
        // Native list of a select (B08 certificate / screen share): chosen index or -1.
        std::function<int(const std::vector<std::wstring>&, int current, RECT anchorScreen, bool keyboard)> choose;
        // B07 « Plus d'options » command list: chosen Command, 0 when dismissed (shared host menu).
        std::function<int(const std::vector<ContextMenu::Item>&, RECT anchorScreen, bool keyboard)> menu;
        std::function<void()> dismiss;   // end the host list now (the prompt closed while it was open)
        // CopyLink / Report chosen: the popover stays open (non-terminal commands).
        std::function<void(Command, const Spec&)> download;
        std::function<bool()> keyboard;   // Modality::Host prompts (the address-bar chip)
        // SiteStatus: a toggle switched off (revoke this kind for this page / site).
        std::function<void(int kind, const Spec&)> revoke;
    };
private:
    Modal::Dialog dialog_; Hooks hooks_; Spec spec_; std::function<void(Result&)> done_;
    RECT page_{}; bool hasPage_ = false;
    Spec parent_; bool confirming_ = false;   // B08 confirmation shown in place of the B07 popover
    // B07/B08 content for the open popover: same anchor, same width, onButton wired.
    Modal::Spec Content(const Spec& s) {
        Modal::Spec m = Build(s, true);
        const auto& now = dialog_.CurrentSpec();
        m.scope = Modal::Scope::Anchor; m.anchor = now.anchor; m.anchorRight = now.anchorRight; m.width = now.width;
        m.onButton = [this](int id) { OnButton(id); };
        return m;
    }
    void OnButton(int id) {
        if (spec_.kind == Kind::SiteStatus && Modal::Dialog::IsRow(id)) {
            const size_t i = static_cast<size_t>(id - 9000); if (i >= spec_.granted.size()) return;   // rows: granted first
            const int k = spec_.granted[i]; const Spec before = spec_;
            spec_.granted.erase(spec_.granted.begin() + static_cast<long>(i));
            spec_.active.erase(std::remove(spec_.active.begin(), spec_.active.end(), k), spec_.active.end()); spec_.revoked.push_back(k);
            if (dialog_.Visible()) { dialog_.Update(Content(spec_)); dialog_.FocusControl(spec_.granted.empty() ? Modal::Dialog::LinkHit(0) : Modal::Dialog::RowHit(std::min(i, spec_.granted.size() - 1))); }
            if (hooks_.revoke) hooks_.revoke(k, before);   // last: the host may reload the page (the popover then closes)
            return;
        }
        if (confirming_) {   // « Annuler » of the confirmation: back to the blocked download, nothing kept
            if (id == Id::Cancel) { confirming_ = false; spec_ = parent_; dialog_.Update(Content(spec_)); dialog_.FocusControl(Modal::Dialog::ButtonHit(1)); }
            return;
        }
        if (id == Id::CopyLink && spec_.kind == Kind::DangerousDownload) { if (hooks_.download) hooks_.download(CopyLink, spec_); return; }
        if (id != Id::More || spec_.kind != Kind::DangerousDownload || !spec_.reportable) return;
        const int chosen = dialog_.TrackMenu(MoreItems(spec_.keepAvailable), Modal::Dialog::ButtonHit(1), dialog_.KeyboardModality());
        if (chosen == CopyLink || chosen == Report) { if (hooks_.download) hooks_.download(static_cast<Command>(chosen), spec_); return; }
        if (chosen != KeepAnyway || !spec_.keepAvailable) return;   // Échap / click outside: the popover stays as it was
        // B08 « Confirmation dans le popover — Annuler par défaut »: Keep only after « Conserver ».
        parent_ = spec_; confirming_ = true;
        Spec keep = spec_; keep.kind = Kind::KeepFile; spec_ = keep;
        Modal::Spec m = Content(spec_);
        for (auto& b : m.buttons) if (b.id == Id::Cancel) b.closes = false;
        dialog_.Update(std::move(m));
    }
public:
    ~Prompt() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        hooks_ = std::move(hooks);
        Modal::Dialog::Hooks h; h.wake = hooks_.wake; h.choose = hooks_.choose; h.menu = hooks_.menu; h.dismiss = hooks_.dismiss; h.keyboard = hooks_.keyboard;
        return dialog_.Create(instance, owner, root, std::move(h));
    }
    void Dispose() { dialog_.Dispose(); done_ = nullptr; }
    void SetPageArea(RECT page) { page_ = page; hasPage_ = true; if (dialog_.Visible() && dialog_.CurrentSpec().scope == Modal::Scope::Page) dialog_.Reposition(page); }
    bool Show(Spec spec, RECT anchor, std::function<void(Result&)> done) {
        if (dialog_.Visible() || !dialog_.Window()) return false;
        spec_ = std::move(spec); done_ = std::move(done); confirming_ = false;
        Modal::Spec m = Build(spec_, true); m.anchor = anchor; m.modality = spec_.modality;
        m.onButton = [this](int id) { OnButton(id); };
        RECT area = hasPage_ ? page_ : RECT{};
        if (!hasPage_) { HWND owner = GetWindow(dialog_.Window(), GW_OWNER); if (owner) { GetClientRect(owner, &area); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&area), 2); } }
        return dialog_.Show(std::move(m), area, [this](const Modal::Result& r) {
            auto done = std::move(done_); done_ = nullptr; confirming_ = false;
            Result out = Map(spec_, r);
            if (!spec_.secret.empty()) { SecureZeroMemory(spec_.secret.data(), spec_.secret.size() * sizeof(wchar_t)); spec_.secret.clear(); }   // F6
            for (auto& v : const_cast<Modal::Result&>(r).values) if (!v.empty()) SecureZeroMemory(v.data(), v.size() * sizeof(wchar_t));   // F6: the modal's copies
            if (done) done(out);
            SecureZeroMemory(out.password.data(), out.password.size() * sizeof(wchar_t)); out.password.clear();
        });
    }
    void Abort() { dialog_.Abort(); }
    void Reposition(RECT anchor) { if (dialog_.Visible()) { if (dialog_.CurrentSpec().scope == Modal::Scope::Anchor) dialog_.SetAnchor(anchor); else if (hasPage_) dialog_.Reposition(page_); else dialog_.Reposition({}); } }
    void AuthRefused() { if (dialog_.Visible() && spec_.kind == Kind::BasicAuth) dialog_.SetFieldError(1, TR(auth_refused)); }
    bool Visible() const { return dialog_.Visible(); }
    HWND Window() const { return dialog_.Window(); }
    const Spec& Current() const { return spec_; }
    Modal::Dialog& Surface() { return dialog_; }   // tests
    bool ConfirmingForTest() const { return confirming_; }
    Motion::Wake NextWake(Motion::Time now) const { return dialog_.NextWake(now); }
    void Frame(Motion::Time now) { dialog_.Frame(now); }
    void SetReducedMotion(bool reduced) { dialog_.SetReducedMotion(reduced); }
};
} // namespace OriginPrompt
} // namespace NativeChrome
