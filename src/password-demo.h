#pragma once
// B05 « Réglages › Mots de passe » as a stand-alone DEMONSTRATION surface (Paper p-D-0, EX3-0):
// list → detail → inline delete confirmation, fed ONLY by fictitious accounts held in memory.
// Nothing is stored, nothing is read from a page, nothing is captured; every view carries the
// « Démonstration » badge. Paper draws it as a 332 DIP sub-view of the Réglages panel; the
// Réglages panel is not touched here (file ownership), so it is a Modal::Dialog (same material,
// zero shadow, 332 DIP, centred over the window scrim) reusing the settings vocabulary.
//
// API:
//   NativeChrome::PasswordDemo::Window (alias of Demo)
//     bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks{wake, announce})
//     void Dispose()
//     bool Show(RECT area, std::function<void()> closed = {}) // list view; false if already open
//     void Hide()                                              // closes (closed() runs once)
//     bool Visible() const; HWND Window() const; const std::vector<Account>& Accounts() const;
//     Motion::Wake NextWake(Motion::Time) const; void Frame(Motion::Time); void SetReducedMotion(bool)
// Keyboard: Tab cycles, Entrée/Espace open a row or press a button, Échap closes the surface
// (from a detail or confirmation it closes too: one surface, one Échap). Focus returns to
// whatever had it before Show.
#include "modal.h"

namespace NativeChrome {
namespace PasswordDemo {
struct Account { std::wstring site, user, password /* fictitious */, page, modified; std::uint64_t id = 0; /* real mode: the vault entry (review F1) */ };
// Paper B05 sample rows, with every identifier and secret fictitious. (Paper's ameli.fr row
// shows a number shaped like a real social security number: replaced by an obvious fake.)
inline std::vector<Account> SampleAccounts() {
    return {
        {L"github.com", L"valxxii", L"Lumen-atelier-2026!", L"github.com/login", L"Modifié le 12 mars 2026"},
        {L"github.com", L"atelier-lumen-bot", L"exemple-robot-0000", L"github.com/login", L"Modifié le 2 février 2026"},
        {L"ameli.fr", L"0 00 00 00 000 000", L"exemple-ameli-0000", L"ameli.fr/assure", L"Modifié le 5 janvier 2026"},
        {L"sncf-connect.com", L"v.mercier@exemple.fr", L"exemple-train-0000", L"sncf-connect.com/connexion", L"Modifié le 9 octobre 2025"},
        {L"mail.proton.me", L"v.mercier@exemple.fr", L"exemple-mail-0000", L"account.proton.me/login", L"Modifié le 21 août 2025"},
    };
}
class Demo {
public:
    struct Hooks { std::function<void()> wake; std::function<void(const std::wstring&)> announce; };
    enum class View { List, Detail, Confirm };
private:
    enum : int { BackButton = 20, DeleteButton = 21, ConfirmButton = 22, KeepButton = 23 };
    Modal::Dialog dialog_; Hooks hooks_; std::vector<Account> accounts_ = SampleAccounts();
    // B05 REAL mode (E8, vault): the vault's accounts WITHOUT any decrypted secret (no reveal), removal
    // through `remove_` (the vault transaction; false = not removed, said so). No « Démonstration » badge.
    bool real_ = false, readOnly_ = false; std::function<bool(std::uint64_t)> remove_;
    std::function<void()> closed_; View view_ = View::List; size_t current_ = 0;
    Modal::Spec Base() const {
        Modal::Spec s; s.kind = Modal::Kind::Passwords; s.scope = Modal::Scope::Window; s.width = 332; s.demo = !real_; s.closeButton = true;
        s.onButton = [this](int id) { const_cast<Demo*>(this)->OnButton(id); };
        return s;
    }
    Modal::Spec ListSpec() const {
        Modal::Spec s = Base(); s.automationId = real_ ? L"PasswordList" : L"PasswordDemoList"; s.title = L"Mots de passe";
        s.subtitle = readOnly_ ? L"Lecture seule dans une fenêtre privée : rien ne peut être modifié ici." : real_ ? L"Enregistrés par Browser, chiffrés pour votre session Windows. Les mots de passe enregistrés auparavant restent proposés par le moteur." : L"Démonstration : comptes fictifs en mémoire, rien n'est enregistré.";
        s.accountsTitle = L"Enregistrés · " + std::to_wstring(accounts_.size()); s.accountsNavigate = true;
        for (auto& a : accounts_) s.accounts.push_back({a.site + L" · " + a.user, std::wstring(12, L'\x2022')});
        if (accounts_.empty()) s.accountsTitle = real_ ? L"Aucun mot de passe enregistré par Browser" : L"Aucun mot de passe enregistré";
        return s;
    }
    Modal::Spec DetailSpec(bool confirm) const {
        const Account& a = accounts_[current_];
        Modal::Spec s = Base(); s.automationId = confirm ? L"PasswordDemoConfirm" : L"PasswordDemoDetail"; s.title = a.site;
        Modal::Field user; user.label = L"Identifiant"; user.value = a.user; user.automationId = L"PasswordDemoUser";
        Modal::Field pass; pass.label = L"Mot de passe"; pass.value = real_ ? std::wstring(12, L'\x2022') : a.password; pass.password = true; pass.reveal = !real_; pass.automationId = L"PasswordDemoPassword";   // real: never decrypted here
        s.fields = {user, pass};
        s.notices.push_back({DialogKit::Glyph::info, real_ ? a.modified : L"Utilisé sur " + a.page + L" · " + a.modified, false});   // real: no usage trace is kept
        if (confirm) {   // Paper F7Q-0: confirmation inside the panel, no modal; focus on « Supprimer »
            s.danger = {L"Supprimer ce mot de passe ?", L"", a.user + L" sur " + a.site + L". Votre compte n'est pas supprimé.", true};
            Modal::Button keep = Modal::Make(KeepButton, L"Annuler", Modal::Role::Secondary, false, false, false, L"PasswordDemoKeep"); keep.closes = false;
            Modal::Button del = Modal::Make(ConfirmButton, L"Supprimer", Modal::Role::Danger, false, false, true, L"PasswordDemoDelete"); del.closes = false;
            s.buttons = {keep, del}; s.stretchButtons = true;
        } else {
            Modal::Button del = Modal::Make(DeleteButton, L"Supprimer ce mot de passe…", Modal::Role::Danger, false, false, false, L"PasswordDemoAskDelete"); del.closes = false; del.leading = true;
            Modal::Button back = Modal::Make(BackButton, L"Retour", Modal::Role::Secondary, false, false, true, L"PasswordDemoBack"); back.closes = false;
            s.buttons = {del, back}; s.softButtons = true;
            if (readOnly_) { s.buttons = {back}; s.notices.push_back({DialogKit::Glyph::info, L"Suppression disponible dans une fenêtre normale.", false}); }   // §6: no inert action
        }
        return s;
    }
    void Go(View v) { view_ = v; dialog_.Update(v == View::List ? ListSpec() : DetailSpec(v == View::Confirm)); }
    void OnButton(int id) {
        if (id >= Modal::Dialog::AccountHit(0) && id < Modal::Dialog::AccountHit(0) + 700) { current_ = static_cast<size_t>(id - Modal::Dialog::AccountHit(0)); if (current_ < accounts_.size()) Go(View::Detail); return; }
        switch (id) {
        case BackButton: Go(View::List); break;
        case DeleteButton: Go(View::Confirm); break;
        case KeepButton: Go(View::Detail); break;
        case ConfirmButton: {
            if (current_ < accounts_.size()) {
                const std::wstring what = accounts_[current_].user + L" sur " + accounts_[current_].site;
                if (real_ && !(remove_ && remove_(accounts_[current_].id))) { if (hooks_.announce) hooks_.announce(L"Le mot de passe n'a pas pu être supprimé : " + what); Go(View::Detail); break; }
                SecureZeroMemory(accounts_[current_].password.data(), accounts_[current_].password.size() * sizeof(wchar_t));
                accounts_.erase(accounts_.begin() + static_cast<std::ptrdiff_t>(current_));
                if (hooks_.announce) hooks_.announce(real_ ? L"Mot de passe supprimé : " + what : L"Mot de passe supprimé (démonstration) : " + what);
            }
            Go(View::List); break;
        }
        }
    }
public:
    ~Demo() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks = {}) {
        hooks_ = std::move(hooks);
        Modal::Dialog::Hooks h; h.wake = hooks_.wake;
        return dialog_.Create(instance, owner, root, std::move(h));
    }
    void Dispose() { dialog_.Dispose(); for (auto& a : accounts_) SecureZeroMemory(a.password.data(), a.password.size() * sizeof(wchar_t)); closed_ = nullptr; }
    bool Show(RECT area, std::function<void()> closed = {}) {
        if (dialog_.Visible()) return false;
        closed_ = std::move(closed); view_ = View::List;
        return dialog_.Show(ListSpec(), area, [this](const Modal::Result&) { auto c = std::move(closed_); closed_ = nullptr; if (c) c(); });
    }
    void Hide() { dialog_.Abort(); }
    bool Visible() const { return dialog_.Visible(); }
    HWND Window() const { return dialog_.Window(); }
    View CurrentView() const { return view_; }
    const std::vector<Account>& Accounts() const { return accounts_; }
    // Real mode: `accounts` carry no secret (password empty); `remove(i)` removes the i-th from the vault.
    void SetReadOnly(bool readOnly) { readOnly_ = readOnly; }
    void SetReal(std::vector<Account> accounts, std::function<bool(std::uint64_t)> remove) { for (auto& a : accounts_) SecureZeroMemory(a.password.data(), a.password.size() * sizeof(wchar_t)); accounts_ = std::move(accounts); for (auto& a : accounts_) a.password.clear(); remove_ = std::move(remove); real_ = true; }
    bool Real() const { return real_; }
    // Back to the fictitious demonstration (the sandbox): never shows real accounts.
    void SetDemo() { for (auto& a : accounts_) SecureZeroMemory(a.password.data(), a.password.size() * sizeof(wchar_t)); accounts_ = SampleAccounts(); remove_ = nullptr; real_ = false; readOnly_ = false; }
    Modal::Dialog& Surface() { return dialog_; }   // tests
    Motion::Wake NextWake(Motion::Time now) const { return dialog_.NextWake(now); }
    void Frame(Motion::Time now) { dialog_.Frame(now); }
    void SetReducedMotion(bool reduced) { dialog_.SetReducedMotion(reduced); }
};
// The lead's integration names the type Window (a class cannot have a Window() member).
using Window = Demo;
} // namespace PasswordDemo
} // namespace NativeChrome
