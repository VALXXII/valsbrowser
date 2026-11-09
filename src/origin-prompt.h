#pragma once
// Origin prompts (Paper p-D-0, B01–B08): requests that come from a site, drawn by the
// browser. One Modal::Dialog surface (same material, zero shadow) in two placements:
//   * anchored popover under the address bar's origin indicator (B01/B02 permissions,
//     B03/B04 passwords, B07 dangerous download): 340/360 DIP, no scrim, light dismiss;
//   * 400 DIP window centred in the page area over a 40 % scrim, blocking that tab only
//     (B06 HTTP authentication and external application, B08 concepts).
//
// API (final):
//   NativeChrome::OriginPrompt::Prompt
//     bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks{wake, choose})
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
enum class Kind { Permission, BasicAuth, ExternalApp, DangerousDownload, ClientCertificate, ScreenShare, KeepFile, PasswordSave, PasswordUpdate, PasswordFill };
enum class Action { Dismiss, Allow, AllowOnce, Block, Submit, Cancel, Open, Keep, Delete, Choose, Save, Never, Fill, Manage };
// COREWEBVIEW2_PERMISSION_KIND values (no WebView2 header needed here).
namespace Permission {
inline constexpr int Unknown = 0, Microphone = 1, Camera = 2, Geolocation = 3, Notifications = 4, OtherSensors = 5, ClipboardRead = 6,
    MultipleDownloads = 7, FileReadWrite = 8, Autoplay = 9, LocalFonts = 10, Midi = 11, WindowManagement = 12;
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
    // Extras (all optional):
    std::wstring frame;                  // ExternalApp: third-party frame host (no remembering offered)
    std::wstring app;                    // ExternalApp: application name (« Zoom Workplace »), else generic
    std::wstring reason;                 // DangerousDownload: « Microsoft Defender SmartScreen le signale… »
    bool insecure = false;               // BasicAuth over plain HTTP
    std::vector<std::wstring> sources;   // ScreenShare: « Onglet », « Fenêtre », « Écran entier »
};
struct Result { Action action = Action::Dismiss; bool remember = false; std::wstring user, password; int choice = -1; };

namespace Text {
inline std::wstring Noun(int p) {
    switch (p) {
    case Permission::Camera: return L"Caméra"; case Permission::Microphone: return L"Micro"; case Permission::Geolocation: return L"Position";
    case Permission::Notifications: return L"Notifications"; case Permission::ClipboardRead: return L"Presse-papiers"; case Permission::Midi: return L"Appareils MIDI";
    case Permission::MultipleDownloads: return L"Téléchargements multiples"; case Permission::OtherSensors: return L"Capteurs de mouvement";
    case Permission::FileReadWrite: return L"Fichiers"; case Permission::Autoplay: return L"Lecture automatique"; case Permission::LocalFonts: return L"Polices installées";
    case Permission::WindowManagement: return L"Gestion des fenêtres"; default: return L"Autorisation";
    }
}
// Single-permission titles. Paper exports: position, notifications, clipboard (B02); the others
// follow the same « <site> veut … » grammar.
inline std::wstring Title(const std::wstring& o, int p) {
    switch (p) {
    case Permission::Camera: return o + L" veut utiliser votre caméra";
    case Permission::Microphone: return o + L" veut utiliser votre micro";
    case Permission::Geolocation: return o + L" veut connaître votre position";
    case Permission::Notifications: return o + L" veut vous envoyer des notifications";
    case Permission::ClipboardRead: return o + L" veut lire votre presse‑papiers";
    case Permission::Midi: return o + L" veut contrôler vos appareils MIDI";
    case Permission::MultipleDownloads: return o + L" veut télécharger plusieurs fichiers";
    case Permission::OtherSensors: return o + L" veut utiliser les capteurs de mouvement";
    case Permission::FileReadWrite: return o + L" veut modifier des fichiers sur cet appareil";
    case Permission::Autoplay: return o + L" veut lire du son automatiquement";
    case Permission::LocalFonts: return o + L" veut utiliser les polices installées";
    case Permission::WindowManagement: return o + L" veut gérer les fenêtres sur vos écrans";
    default: return o + L" demande une autorisation";
    }
}
inline std::wstring Subtitle(int p) {
    switch (p) {
    case Permission::Geolocation: return L"Position fournie par Windows, à quelques centaines de mètres près.";
    case Permission::Notifications: return L"Elles s'affichent dans Windows, même onglet fermé.";
    case Permission::ClipboardRead: return L"Le site verra le texte et les images que vous avez copiés.";
    case Permission::MultipleDownloads: return L"Le site pourra enregistrer plusieurs fichiers à la suite.";
    case Permission::Midi: return L"Le site pourra envoyer des messages système à vos instruments.";
    default: return L"";
    }
}
inline const char* Glyph(int p) {
    switch (p) {
    case Permission::Camera: return DialogKit::Glyph::camera; case Permission::Microphone: return DialogKit::Glyph::microphone;
    case Permission::Geolocation: return DialogKit::Glyph::position; case Permission::Notifications: return DialogKit::Glyph::bell;
    case Permission::ClipboardRead: return DialogKit::Glyph::clipboard; case Permission::MultipleDownloads: return DialogKit::Glyph::download;
    case Permission::FileReadWrite: return DialogKit::Glyph::file; case Permission::WindowManagement: return DialogKit::Glyph::screen;
    default: return DialogKit::Glyph::info;
    }
}
}

// Button ids inside the dialog (Modal::Dialog ids; mapped back to Action).
namespace Id { inline constexpr int Cancel = Modal::CancelButton, Accept = Modal::AcceptButton, Never = 10, Later = 11, More = 12, Manage = 13; }

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
            m.title = s.origin + L" veut utiliser";
            for (int q : s.permissions) m.rows.push_back({Text::Glyph(q), Text::Noun(q), q == Permission::Geolocation ? L"Approximative" : L""});
        } else { m.title = Text::Title(s.origin, p); m.subtitle = Text::Subtitle(p); m.headGlyph = Text::Glyph(p); }
        const bool always = !many && p == Permission::Notifications;   // B02: notifications are always remembered
        if (always) m.notices.push_back({DialogKit::Glyph::info, L"Choix toujours mémorisé. Modifiable dans Réglages › Autorisations des sites.", false});
        else { m.checkLabel = L"Mémoriser pour ce site"; m.originCheck = true; }
        m.stretchButtons = true; m.softButtons = true;
        m.buttons = {button(Id::Cancel, L"Bloquer", Role::Secondary, false, false, false, L"PermissionBlock"), button(Id::Accept, L"Autoriser", Role::Primary, true, false, false, L"PermissionAllow")};
        m.checkValue = always;
        break;
    }
    case Kind::BasicAuth: {
        m.automationId = L"HttpAuthentication"; m.scope = Scope::Page; m.largeTitle = true; m.title = L"Connexion requise";
        m.subtitle = s.origin + L" demande un identifiant.";
        if (!s.detail.empty()) m.subtitle += L" « " + s.detail + L" »";   // the site's words, quoted (B06 note)
        if (s.insecure) m.notices.push_back({DialogKit::Glyph::lockOpen, L"Connexion non sécurisée : ces identifiants seront envoyés en clair.", true});
        Field user; user.label = L"Identifiant"; user.value = s.user; user.automationId = L"AuthUser"; user.maxLength = 1024;
        Field pass; pass.label = L"Mot de passe"; pass.password = true; pass.automationId = L"AuthPassword"; pass.maxLength = 1024;
        m.fields = {user, pass};
        m.softButtons = true;
        m.buttons = {button(Id::Cancel, L"Annuler", Role::Secondary, false, true, false, L"AuthCancel"), button(Id::Accept, L"Se connecter", Role::Primary, true, false, false, L"AuthSubmit")};
        break;
    }
    case Kind::ExternalApp: {
        m.automationId = L"ExternalApplication"; m.scope = Scope::Page; m.largeTitle = true; m.tileGlyph = DialogKit::Glyph::external; m.softButtons = true;
        std::wstring scheme = s.detail.substr(0, s.detail.find(L':'));
        if (!s.frame.empty()) {
            m.title = L"Ouvrir une application ?";
            m.subtitle = L"Un contenu intégré à " + s.origin + L", venant de " + s.frame + L", veut ouvrir un lien " + scheme + L".";
            m.notices.push_back({DialogKit::Glyph::info, L"Si vous ne l'attendiez pas, annulez. Ce choix ne peut pas être mémorisé pour un contenu intégré.", false});
            // B06 note: « Annuler » is the default when a third-party frame asks.
            m.buttons = {button(Id::Cancel, L"Annuler", Role::Secondary, true, true, true, L"ExternalCancel"), button(Id::Accept, L"Ouvrir", Role::Secondary, false, false, false, L"ExternalOpen")};
        } else {
            m.title = s.app.empty() ? L"Ouvrir une application ?" : L"Ouvrir " + s.app + L" ?";
            m.subtitle = s.origin + L" veut ouvrir une application installée sur cet ordinateur.";
            if (!scheme.empty()) { m.checkLabel = L"Toujours autoriser ce site à ouvrir les liens " + scheme; m.originCheck = true; }
            const std::wstring short_ = s.app.empty() ? L"Ouvrir" : L"Ouvrir " + s.app.substr(0, s.app.find(L' '));
            m.buttons = {button(Id::Cancel, L"Annuler", Role::Secondary, false, true, false, L"ExternalCancel"), button(Id::Accept, short_, Role::Primary, true, false, true, L"ExternalOpen")};
        }
        break;
    }
    case Kind::DangerousDownload: {
        m.automationId = L"DangerousDownload"; m.scope = Scope::Anchor; m.anchorRight = true; m.width = 360; m.gap = 12;
        m.title = L"Téléchargements";
        m.danger = {s.detail, L"Bloqué : fichier dangereux", s.reason.empty() ? L"Microsoft Defender SmartScreen le signale comme malveillant. Il n'a pas été ouvert." : s.reason, true};
        m.stretchButtons = false;
        Button del = button(Id::Accept, L"Supprimer le fichier", Role::Danger, false, false, true, L"DownloadDelete");
        Button more = button(Id::More, L"Plus d'options", Role::Quiet, false, false, false, L"DownloadMore"); more.closes = false;
        m.buttons = {del, more}; m.stretchButtons = true;
        break;
    }
    case Kind::KeepFile: {   // B08: confirmation after « Conserver quand même… »
        m.automationId = L"KeepDangerousFile"; m.scope = Scope::Page; m.title = L"Conserver ce fichier dangereux ?";
        m.body = L"Il pourrait voler vos données ou endommager Windows. Il ne sera pas ouvert automatiquement.";
        if (!s.detail.empty()) m.infos.push_back({s.detail, L""});
        m.buttons = {button(Id::Cancel, L"Annuler", Role::Secondary, true, true, true, L"KeepCancel"), button(Id::Accept, L"Conserver", Role::Danger, false, false, false, L"KeepConfirm")};
        break;
    }
    case Kind::ClientCertificate: {   // B08 concept: WebView2 exposes ClientCertificateRequested, list as accounts
        m.automationId = L"ClientCertificate"; m.scope = Scope::Page; m.largeTitle = true; m.title = L"Choisir un certificat";
        m.subtitle = s.origin + L" demande un certificat pour vous identifier.";
        m.accountsTitle = L"Certificats disponibles";
        for (auto& a : s.accounts) m.accounts.push_back({a.user, a.hint});
        m.softButtons = true;
        m.buttons = {button(Id::Cancel, L"Ne pas envoyer", Role::Secondary, true, true, false, L"CertificateCancel")};
        break;
    }
    case Kind::ScreenShare: {   // B08 concept (Démonstration): the engine's picker cannot be replaced
        m.automationId = L"ScreenShare"; m.scope = Scope::Page; m.largeTitle = true; m.title = L"Partager avec " + s.origin; m.demo = true;
        m.hasSelect = true; m.select.label = L"Source"; m.select.options = s.sources.empty() ? std::vector<std::wstring>{L"Onglet", L"Fenêtre", L"Écran entier"} : s.sources; m.select.index = 0;
        m.softButtons = true;
        m.buttons = {button(Id::Cancel, L"Annuler", Role::Secondary, false, true, true, L"ShareCancel"), button(Id::Accept, L"Partager", Role::Primary, true, false, false, L"ShareConfirm")};
        break;
    }
    case Kind::PasswordSave: case Kind::PasswordUpdate: {   // B04, Démonstration: nothing is stored
        const bool update = s.kind == Kind::PasswordUpdate;
        m.automationId = update ? L"PasswordUpdate" : L"PasswordSave"; m.scope = Scope::Anchor; m.width = 360; m.closeButton = true; m.demo = true;
        m.title = update ? L"Mettre à jour le mot de passe ?" : L"Enregistrer le mot de passe ?";
        m.subtitle = L"Pour " + s.origin + L". Démonstration : rien n'est enregistré.";
        Field user; user.label = L"Identifiant"; user.value = s.user; user.automationId = L"PasswordUser"; user.required = true;
        Field pass; pass.label = L"Mot de passe"; pass.password = true; pass.reveal = true; pass.automationId = L"PasswordValue";
        pass.value = s.accounts.empty() ? L"exemple-fictif-2026" : s.accounts[0].hint;   // fictitious only
        m.fields = {user, pass};
        m.validate = [](const std::vector<std::wstring>& v) { return v.empty() || v[0].empty() ? std::wstring(L"Saisissez un identifiant.") : std::wstring(); };
        m.softButtons = true;
        Button never = button(Id::Never, L"Jamais pour ce site", Role::Quiet, false, false, false, L"PasswordNever"); never.leading = true;
        m.buttons = {never, button(Id::Later, L"Plus tard", Role::Secondary, false, true, false, L"PasswordLater"), button(Id::Accept, update ? L"Mettre à jour" : L"Enregistrer", Role::Primary, true, false, false, L"PasswordSave")};
        break;
    }
    case Kind::PasswordFill: {   // B03/B04, Démonstration: fictitious accounts held in memory
        m.automationId = L"PasswordFill"; m.scope = Scope::Anchor; m.width = 360; m.padding = 6; m.gap = 2; m.demo = true;
        m.accountsTitle = s.accounts.empty() ? L"Aucun compte enregistré pour " + s.origin : L"Comptes enregistrés pour " + s.origin;
        for (auto& a : s.accounts) m.accounts.push_back({a.user, std::wstring(std::max<size_t>(8, std::min<size_t>(14, a.hint.size())), L'\x2022')});
        m.manageLabel = L"Gérer les mots de passe"; m.manageId = Id::Manage;
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
    case Kind::DangerousDownload: out.action = r.button == Id::Accept ? Action::Delete : r.button == Id::More ? Action::Keep : Action::Dismiss; break;
    case Kind::KeepFile: out.action = r.button == Id::Accept ? Action::Keep : Action::Cancel; break;
    case Kind::ClientCertificate: if (r.button == Id::Accept && r.select >= 0) { out.action = Action::Choose; out.choice = r.select; } else out.action = Action::Cancel; break;
    case Kind::ScreenShare: if (r.button == Id::Accept) { out.action = Action::Choose; out.choice = r.select; } else out.action = Action::Cancel; break;
    case Kind::PasswordSave: case Kind::PasswordUpdate:
        out.action = r.button == Id::Accept ? Action::Save : r.button == Id::Never ? Action::Never : r.button == Id::Later ? Action::Cancel : Action::Dismiss;
        if (r.button == Id::Accept && !r.values.empty()) out.user = r.values[0];   // the (fictitious) password is never returned
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
    struct Hooks {
        std::function<void()> wake;
        // B07 « Plus d'options » menu (Copier le lien du fichier, Signaler une erreur à Microsoft,
        // Conserver quand même…): chosen index or -1. Without it « Plus d'options » means Keep.
        std::function<int(const std::vector<std::wstring>&, int current, RECT anchorScreen, bool keyboard)> choose;
    };
private:
    Modal::Dialog dialog_; Hooks hooks_; Spec spec_; std::function<void(Result&)> done_;
    RECT page_{}; bool hasPage_ = false;
    void OnButton(int id) {
        if (id != Id::More) return;
        int chosen = 2;   // « Conserver quand même… » when no menu is wired
        if (hooks_.choose) {
            const auto box = dialog_.BoxForTest(Modal::Dialog::ButtonHit(1)); (void)box;
            RECT r{}; GetWindowRect(dialog_.Window(), &r);
            chosen = hooks_.choose({L"Copier le lien du fichier", L"Signaler une erreur à Microsoft", L"Conserver quand même…"}, -1, r, false);
        }
        if (chosen == 2) dialog_.Finish(Id::More);
    }
public:
    ~Prompt() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        hooks_ = std::move(hooks);
        Modal::Dialog::Hooks h; h.wake = hooks_.wake; h.choose = hooks_.choose;
        return dialog_.Create(instance, owner, root, std::move(h));
    }
    void Dispose() { dialog_.Dispose(); done_ = nullptr; }
    void SetPageArea(RECT page) { page_ = page; hasPage_ = true; if (dialog_.Visible() && dialog_.CurrentSpec().scope == Modal::Scope::Page) dialog_.Reposition(page); }
    bool Show(Spec spec, RECT anchor, std::function<void(Result&)> done) {
        if (dialog_.Visible() || !dialog_.Window()) return false;
        spec_ = std::move(spec); done_ = std::move(done);
        Modal::Spec m = Build(spec_, true); m.anchor = anchor;
        m.onButton = [this](int id) { OnButton(id); };
        RECT area = hasPage_ ? page_ : RECT{};
        if (!hasPage_) { HWND owner = GetWindow(dialog_.Window(), GW_OWNER); if (owner) { GetClientRect(owner, &area); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&area), 2); } }
        return dialog_.Show(std::move(m), area, [this](const Modal::Result& r) {
            auto done = std::move(done_); done_ = nullptr;
            Result out = Map(spec_, r);
            if (done) done(out);
            SecureZeroMemory(out.password.data(), out.password.size() * sizeof(wchar_t)); out.password.clear();
        });
    }
    void Abort() { dialog_.Abort(); }
    void Reposition(RECT anchor) { if (dialog_.Visible()) { if (dialog_.CurrentSpec().scope == Modal::Scope::Anchor) dialog_.SetAnchor(anchor); else if (hasPage_) dialog_.Reposition(page_); else dialog_.Reposition({}); } }
    void AuthRefused() { if (dialog_.Visible() && spec_.kind == Kind::BasicAuth) dialog_.SetFieldError(1, L"Identifiant ou mot de passe refusé par le site."); }
    bool Visible() const { return dialog_.Visible(); }
    HWND Window() const { return dialog_.Window(); }
    const Spec& Current() const { return spec_; }
    Modal::Dialog& Surface() { return dialog_; }   // tests
    Motion::Wake NextWake(Motion::Time now) const { return dialog_.NextWake(now); }
    void Frame(Motion::Time now) { dialog_.Frame(now); }
    void SetReducedMotion(bool reduced) { dialog_.SetReducedMotion(reduced); }
};
} // namespace OriginPrompt
} // namespace NativeChrome
