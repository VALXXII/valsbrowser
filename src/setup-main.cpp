// browser-<version>-win-x64-setup.exe and <root>\uninstall.exe (same stub; see src/setup-core.h).
//
//   with its embedded payload  -> install / update / repair window
//   as <root>\uninstall.exe    -> copies itself to %TEMP% and re-runs there (it must remove its own file)
//   --uninstall --parent <pid> -> uninstall window (only from that temp copy)
//   anything else (payload missing: truncated download) -> « download it again », nothing written
//
// UI: one compact window in the browser's dark chrome (canvas 23/255, raised 41/255, accent #3E67FF), real child
// BUTTON controls (owner-drawn) so keyboard (Tab / Space / Enter / Esc), MSAA names and BM_CLICK work as usual.
// French or English: the browser-wide preference <root>\data\ui-language if present, else the Windows UI language.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>

#include "build-info.h"
#include "setup-core.h"
#include "setup-uninstall-key.h"
#include "ui-language.h"

// Build identity of this stub, checked by tools/release/package.py (check_setup_stub) against version.json: the setup
// is only assembled with the stub of the SAME version, commit and channel as the payload it carries.
extern "C" __attribute__((used)) const char kSetupIdentity[] =
    "browser-setup-identity " BROWSER_VERSION_TEXT " " BROWSER_COMMIT_TEXT " " BROWSER_CHANNEL_TEXT ";";

namespace SC = SetupCore;
namespace UP = UpdatePackage;

namespace {

// ---------------------------------------------------------------- strings
bool g_en = false;
struct Text { const wchar_t* fr; const wchar_t* en; };
const wchar_t* T(const Text& t) { return g_en ? t.en : t.fr; }
#define S(name, fr, en) const Text name{fr, en};
S(tInstall, L"Installer browser", L"Install browser")
S(tUpdate, L"Mettre à jour browser", L"Update browser")
S(tRepair, L"Réparer browser", L"Repair browser")
S(tUninstall, L"Désinstaller browser", L"Uninstall browser")
S(tSubtitle, L"Version %s · pour votre compte Windows, sans droits administrateur", L"Version %s · for your Windows account, no administrator rights")
S(tBodyFresh, L"browser sera installé dans votre dossier utilisateur (%LOCALAPPDATA%\\browser) et ajouté au menu Démarrer. Il se met ensuite à jour tout seul.",
  L"browser will be installed in your user folder (%LOCALAPPDATA%\\browser) and added to the Start menu. It then keeps itself up to date.")
S(tBodyUpdate, L"La version %s installée sera remplacée par celle-ci. Vos profils, onglets, favoris, historique et mots de passe sont conservés.",
  L"The installed version %s will be replaced by this one. Your profiles, tabs, bookmarks, history and passwords are kept.")
S(tBodyRepair, L"Cette version est déjà installée. Réparer réinstalle ses fichiers et ses raccourcis ; vos données sont conservées.",
  L"This version is already installed. Repair reinstalls its files and shortcuts; your data is kept.")
S(tBodyNewer, L"Une version plus récente (%s) est déjà installée. Rien ne sera modifié.", L"A newer version (%s) is already installed. Nothing will be changed.")
S(tDesktop, L"Créer un raccourci sur le Bureau", L"Create a desktop shortcut")
S(tLaunch, L"Ouvrir browser une fois installé", L"Open browser when done")
S(tBtnInstall, L"Installer", L"Install")
S(tBtnUpdate, L"Mettre à jour", L"Update")
S(tBtnRepair, L"Réparer", L"Repair")
S(tBtnUninstall, L"Désinstaller", L"Uninstall")
S(tBtnCancel, L"Annuler", L"Cancel")
S(tBtnClose, L"Fermer", L"Close")
S(tBtnRetry, L"Réessayer", L"Retry")
S(tBtnOpen, L"Ouvrir browser", L"Open browser")
S(tPrepare, L"Vérification et préparation des fichiers…", L"Checking and preparing files…")
S(tInstalling, L"Installation…", L"Installing…")
S(tStarting, L"Installation et démarrage de browser…", L"Installing and starting browser…")
S(tFinishing, L"Création des raccourcis…", L"Creating shortcuts…")
S(tDone, L"browser est installé. Vous le retrouverez dans le menu Démarrer.", L"browser is installed. You'll find it in the Start menu.")
S(tDoneStarting, L"browser est installé et démarre. Vous le retrouverez dans le menu Démarrer.", L"browser is installed and starting. You'll find it in the Start menu.")
S(tForeign, L"Un raccourci « browser » du menu Démarrer vers une autre copie existait déjà : il a été laissé tel quel.",
  L"A « browser » Start menu shortcut to another copy already existed: it was left as is.")
S(tWebView2, L"Microsoft Edge WebView2 Runtime n'a pas été détecté. Au premier démarrage, browser proposera de l'installer depuis Microsoft.",
  L"Microsoft Edge WebView2 Runtime was not detected. On first start, browser will offer to install it from Microsoft.")
S(eDamaged, L"Ce fichier d'installation est endommagé ou incomplet. Téléchargez-le à nouveau.", L"This setup file is damaged or incomplete. Please download it again.")
S(eUnsafe, L"Le dossier d'installation contient un lien ou une jonction : l'installation est refusée pour protéger vos fichiers.",
  L"The install folder contains a link or junction: installation refused to protect your files.")
S(eNewer, L"Une version plus récente est déjà installée.", L"A newer version is already installed.")
S(eRunning, L"browser est ouvert. Fermez toutes ses fenêtres, puis cliquez sur Réessayer.", L"browser is open. Close all its windows, then click Retry.")
S(eDisk, L"Impossible d'écrire les fichiers (disque plein ou dossier protégé). L'installation existante et vos données n'ont pas été modifiées.",
  L"Files couldn't be written (disk full or protected folder). The existing install and your data were not changed.")
S(eCancelled, L"Installation annulée. Rien n'a été modifié.", L"Installation canceled. Nothing was changed.")
S(eHelper, L"L'installation n'a pas pu se terminer. La version précédente et vos données sont intactes.",
  L"Installation couldn't finish. The previous version and your data are intact.")
S(eStart, L"browser n'a pas pu démarrer après l'installation ; l'état précédent a été rétabli. Vos données sont intactes.",
  L"browser couldn't start after installing; the previous state was restored. Your data is intact.")
S(eIntegration, L"browser est installé, mais les raccourcis ou l'entrée de désinstallation n'ont pas pu être créés. Réessayez.",
  L"browser is installed, but the shortcuts or the uninstall entry couldn't be created. Please retry.")
S(tDetail, L"Détail : %s", L"Detail: %s")
S(tBodyUninstall, L"browser sera retiré de cet ordinateur (programme, raccourcis, entrée « Applications »). Vos données (profils, historique, favoris, mots de passe) restent dans %LOCALAPPDATA%\\browser\\data, sauf si vous cochez la case.",
  L"browser will be removed from this computer (program, shortcuts, « Apps » entry). Your data (profiles, history, bookmarks, passwords) stays in %LOCALAPPDATA%\\browser\\data unless you tick the box.")
S(tRemoveData, L"Supprimer aussi mes données de navigation", L"Also delete my browsing data")
S(tConfirmData, L"Supprimer définitivement vos profils, historique, favoris, mots de passe et sessions ? Cette action est irréversible.",
  L"Permanently delete your profiles, history, bookmarks, passwords and sessions? This can't be undone.")
S(tRemoving, L"Désinstallation…", L"Uninstalling…")
S(tUninstalled, L"browser a été désinstallé.", L"browser has been uninstalled.")
S(tDataKept, L"Vos données sont conservées dans %LOCALAPPDATA%\\browser\\data.", L"Your data is kept in %LOCALAPPDATA%\\browser\\data.")
S(tDataRemoved, L"Vos données ont été supprimées.", L"Your data was deleted.")
S(tLeftovers, L"Certains fichiers encore utilisés (par exemple le pont des annotations lancé par un agent) n'ont pas pu être supprimés ; ils pourront l'être plus tard.",
  L"Some files still in use (for example the annotations bridge started by an agent) couldn't be removed; they can be removed later.")
S(eUninstallRunning, L"browser est ouvert. Fermez toutes ses fenêtres, puis cliquez sur Réessayer.", L"browser is open. Close all its windows, then click Retry.")
S(eUninstallFailed, L"La désinstallation n'a pas pu se faire. Rien n'a été supprimé.", L"Uninstall couldn't be done. Nothing was removed.")
S(eNotSetup, L"Ce fichier d'installation est endommagé ou incomplet. Téléchargez-le à nouveau depuis la page des versions de browser.",
  L"This setup file is damaged or incomplete. Please download it again from the browser releases page.")
#undef S

std::wstring Format(const Text& t, const std::wstring& arg) {
    std::wstring f = T(t), out;
    const auto pos = f.find(L"%s");
    if (pos == std::wstring::npos) return f;
    return f.substr(0, pos) + arg + f.substr(pos + 2);
}

// ---------------------------------------------------------------- state
constexpr COLORREF Canvas = RGB(23, 23, 23), Raised = RGB(41, 41, 41), Ink = RGB(240, 240, 240), Muted = RGB(160, 160, 160),
                   Accent = RGB(0x3E, 0x67, 0xFF), AccentHover = RGB(0x55, 0x7A, 0xFF), Line = RGB(70, 70, 70), Danger = RGB(255, 120, 110);
enum Id { IdDesktop = 101, IdLaunch, IdRemoveData, IdPrimary, IdSecondary };
enum class Page { Options, Working, Done, Failed };
constexpr UINT MsgProgress = WM_APP + 1, MsgFinished = WM_APP + 2;

struct App {
    bool uninstall = false, unattended = false;
    std::wstring self;
    SetupPayload::Layout layout;
    SC::Env env;
    SC::Inspection inspection;
    SC::Options options;
    bool removeData = false;
    Page page = Page::Options;
    std::wstring status, detail, note;
    bool error = false;
    double progress = 0;
    std::atomic<bool> cancel{false};
    bool working = false, cancellable = false, testCancel = false;
    SC::Result result;
    SC::UninstallReport uninstallReport;
    HWND window = nullptr, desktop = nullptr, launch = nullptr, removeDataBox = nullptr, primary = nullptr, secondary = nullptr;
    HFONT font = nullptr, bold = nullptr, small = nullptr;
    HICON icon = nullptr;
    UINT dpi = 96;
    int exitCode = 0;
} g;

int Px(int dip) { return MulDiv(dip, int(g.dpi), 96); }
std::wstring W(const std::string& s) { return UP::Widen(s); }

void MakeFonts() {
    for (HFONT f : {g.font, g.bold, g.small})
        if (f) DeleteObject(f);
    auto make = [](int dip, int weight) {
        return CreateFontW(-Px(dip), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
    };
    g.font = make(14, FW_NORMAL);
    g.bold = make(20, FW_SEMIBOLD);
    g.small = make(12, FW_NORMAL);
}

std::wstring Title() {
    if (g.uninstall) return T(tUninstall);
    switch (g.inspection.kind) {
        case SC::Kind::Update: return T(tUpdate);
        case SC::Kind::Repair: return T(tRepair);
        default: return T(tInstall);
    }
}

bool WebView2Present() {
    // Microsoft's documented detection: « pv » of the runtime's EdgeUpdate client key (per machine or per user).
    const wchar_t* keys[][2] = {
        {L"HKLM", L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"},
        {L"HKLM", L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"},
        {L"HKCU", L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"}};
#ifdef SETUP_TESTING
    wchar_t v[8];
    if (GetEnvironmentVariableW(L"BROWSER_SETUP_TEST_WEBVIEW2", v, 8)) return v[0] == L'1';
#endif
    for (auto& k : keys) {
        HKEY key;
        if (RegOpenKeyExW(k[0][2] == L'L' ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, k[1], 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) continue;
        std::wstring pv;
        const bool ok = SetupKey::ReadString(key, L"pv", pv) && !pv.empty() && pv != L"0.0.0.0";
        RegCloseKey(key);
        if (ok) return true;
    }
    return false;
}

std::wstring ErrorText(SC::Error e) {
    switch (e) {
        case SC::Error::Damaged: return T(eDamaged);
        case SC::Error::Unsafe: return T(eUnsafe);
        case SC::Error::Newer: return T(eNewer);
        case SC::Error::Running: return T(eRunning);
        case SC::Error::Disk: return T(eDisk);
        case SC::Error::Cancelled: return T(eCancelled);
        case SC::Error::StartFailed: return T(eStart);
        case SC::Error::Integration: return T(eIntegration);
        default: return T(eHelper);
    }
}

// ---------------------------------------------------------------- layout / pages
void Show(HWND h, bool on) { ShowWindow(h, on ? SW_SHOW : SW_HIDE); }

void Layout() {
    RECT rc;
    GetClientRect(g.window, &rc);
    const int w = rc.right, h = rc.bottom, bw = Px(124), bh = Px(34), pad = Px(24);
    SetWindowPos(g.primary, nullptr, w - pad - bw, h - pad - bh, bw, bh, SWP_NOZORDER);
    SetWindowPos(g.secondary, nullptr, w - pad - 2 * bw - Px(8), h - pad - bh, bw, bh, SWP_NOZORDER);
    const int boxW = w - 2 * pad, boxH = Px(26);
    SetWindowPos(g.desktop, nullptr, pad, Px(178), boxW, boxH, SWP_NOZORDER);
    SetWindowPos(g.launch, nullptr, pad, Px(208), boxW, boxH, SWP_NOZORDER);
    SetWindowPos(g.removeDataBox, nullptr, pad, Px(194), boxW, boxH, SWP_NOZORDER);
    for (HWND c : {g.primary, g.secondary, g.desktop, g.launch, g.removeDataBox}) SendMessageW(c, WM_SETFONT, WPARAM(g.font), TRUE);
}

void SetPage(Page page) {
    g.page = page;
    const bool options = page == Page::Options;
    const bool newer = !g.uninstall && g.inspection.kind == SC::Kind::Newer;
    Show(g.desktop, options && !g.uninstall && !newer);
    Show(g.launch, options && !g.uninstall && !newer);
    Show(g.removeDataBox, options && g.uninstall);
    std::wstring primary, secondary;
    if (options) {
        primary = g.uninstall ? T(tBtnUninstall) : newer ? T(tBtnClose)
                  : g.inspection.kind == SC::Kind::Update ? T(tBtnUpdate) : g.inspection.kind == SC::Kind::Repair ? T(tBtnRepair) : T(tBtnInstall);
        secondary = newer ? L"" : T(tBtnCancel);
    } else if (page == Page::Working) {
        secondary = T(tBtnCancel);
    } else if (page == Page::Done) {
        primary = T(tBtnClose);
        if (!g.uninstall && !g.options.launch) secondary = T(tBtnOpen);
    } else {
        primary = g.error && (g.result.error == SC::Error::Damaged || g.result.error == SC::Error::Newer || g.result.error == SC::Error::Unsafe) ? T(tBtnClose) : T(tBtnRetry);
        secondary = primary == T(tBtnRetry) ? T(tBtnClose) : L"";
    }
    SetWindowTextW(g.primary, primary.c_str());
    SetWindowTextW(g.secondary, secondary.c_str());
    Show(g.primary, !primary.empty());
    Show(g.secondary, !secondary.empty());
    EnableWindow(g.secondary, page != Page::Working || g.cancellable);
    if (!primary.empty()) SetFocus(g.primary);
    InvalidateRect(g.window, nullptr, TRUE);
}

std::wstring OptionsBody() {
    if (g.uninstall) return T(tBodyUninstall);
    switch (g.inspection.kind) {
        case SC::Kind::Update: return Format(tBodyUpdate, W(g.inspection.installed));
        case SC::Kind::Repair: return T(tBodyRepair);
        case SC::Kind::Newer: return Format(tBodyNewer, W(g.inspection.installed));
        default: return T(tBodyFresh);
    }
}

// ---------------------------------------------------------------- work
void Finish(bool ok) {
    g.working = false;
    g.error = !ok;
    SetPage(ok ? Page::Done : Page::Failed);
    if (g.unattended) PostMessageW(g.window, WM_CLOSE, 0, 0);
}

void StartWork() {
    g.cancel = g.testCancel;  // fixtures only (--test-cancel, SETUP_TESTING): as if Cancel was clicked at once
    g.working = true;
    g.progress = 0;
    g.detail.clear();
    g.note.clear();
    g.cancellable = !g.uninstall;
    g.status = g.uninstall ? T(tRemoving) : T(tPrepare);
    SetPage(Page::Working);
    HWND window = g.window;
    std::thread([window] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (g.uninstall) {
            g.uninstallReport = SC::Uninstall(g.env, g.removeData);
        } else {
            g.result = SC::Install(g.env, g.self, g.layout, g.options, &g.cancel, [window](SC::Step s, double f) {
                PostMessageW(window, MsgProgress, WPARAM(s), LPARAM(f * 1000));
            });
        }
        CoUninitialize();
        PostMessageW(window, MsgFinished, 0, 0);
    }).detach();
}

void Finished() {
    if (g.uninstall) {
        const auto& r = g.uninstallReport;
        const bool ok = r.error == SC::Error::None;
        g.exitCode = ok ? 0 : int(r.error) + 10;
        if (ok) {
            g.status = T(tUninstalled);
            g.note = std::wstring(r.dataKept ? T(tDataKept) : T(tDataRemoved)) + (r.leftovers ? std::wstring(L"\n") + T(tLeftovers) : L"");
        } else {
            g.status = r.error == SC::Error::Running ? T(eUninstallRunning) : T(eUninstallFailed);
        }
        g.detail = r.detail.empty() ? L"" : Format(tDetail, W(r.detail));
        return Finish(ok);
    }
    const auto& r = g.result;
    g.exitCode = r.Ok() ? 0 : int(r.error) + 10;
    if (r.Ok()) {
        g.status = g.options.launch ? T(tDoneStarting) : T(tDone);
        if (r.startMenuForeign) g.note = T(tForeign);
        if (!WebView2Present()) g.note += (g.note.empty() ? L"" : L"\n") + std::wstring(T(tWebView2));
    } else {
        g.status = ErrorText(r.error);
        g.detail = r.detail.empty() ? L"" : Format(tDetail, W(r.detail));
    }
    Finish(r.Ok());
}

void OpenInstalled() {
    const std::wstring launcher = g.env.root + L"\\browser.exe";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + launcher + L"\"";
    if (CreateProcessW(launcher.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, g.env.root.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void Primary() {
    switch (g.page) {
        case Page::Options:
            if (!g.uninstall && g.inspection.kind == SC::Kind::Newer) {
                g.exitCode = int(SC::Error::Newer) + 10;
                return void(DestroyWindow(g.window));
            }
            if (g.uninstall && g.removeData && !g.unattended &&
                MessageBoxW(g.window, T(tConfirmData), Title().c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
            return StartWork();
        case Page::Failed:
            if (GetWindowTextLengthW(g.primary) && g.result.error != SC::Error::Damaged && g.result.error != SC::Error::Newer &&
                g.result.error != SC::Error::Unsafe) {
                if (!g.uninstall) g.inspection = SC::Inspect(g.env, g.layout.version);
                return StartWork();
            }
            return void(DestroyWindow(g.window));
        case Page::Done: return void(DestroyWindow(g.window));
        default: return;
    }
}

void Secondary() {
    if (g.page == Page::Working) {
        if (g.cancellable) g.cancel = true;
        return;
    }
    if (g.page == Page::Done && !g.uninstall && !g.options.launch) OpenInstalled();
    DestroyWindow(g.window);
}

// ---------------------------------------------------------------- painting
void DrawTextBlock(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT& r, UINT flags = DT_WORDBREAK) {
    if (text.empty()) return;
    SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT measure = r;
    DrawTextW(dc, text.c_str(), -1, &measure, flags | DT_CALCRECT | DT_NOPREFIX);
    DrawTextW(dc, text.c_str(), -1, &r, flags | DT_NOPREFIX);
    r.top = measure.bottom + Px(8);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(Canvas);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const int pad = Px(24);
    if (g.icon) DrawIconEx(dc, pad, pad, g.icon, Px(40), Px(40), 0, nullptr, DI_NORMAL);
    RECT title{pad + Px(52), pad - Px(2), rc.right - pad, pad + Px(28)};
    DrawTextBlock(dc, g.bold, Ink, Title(), title, DT_SINGLELINE);
    RECT sub{pad + Px(52), pad + Px(26), rc.right - pad, pad + Px(46)};
    DrawTextBlock(dc, g.small, Muted, Format(tSubtitle, W(g.uninstall ? std::string(BROWSER_VERSION_TEXT) : g.layout.version)), sub, DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT body{pad, Px(88), rc.right - pad, Px(176)};
    if (g.page == Page::Options) {
        DrawTextBlock(dc, g.font, Ink, OptionsBody(), body);
    } else {
        DrawTextBlock(dc, g.font, g.page == Page::Failed ? Danger : Ink, g.status, body);
        if (g.page == Page::Working) {
            RECT track{pad, body.top + Px(4), rc.right - pad, body.top + Px(10)};
            HBRUSH t = CreateSolidBrush(Raised), a = CreateSolidBrush(Accent);
            FillRect(dc, &track, t);
            RECT fill = track;
            fill.right = fill.left + LONG((track.right - track.left) * (g.progress < 0 ? 0 : g.progress > 1 ? 1 : g.progress));
            FillRect(dc, &fill, a);
            DeleteObject(t);
            DeleteObject(a);
        } else {
            RECT note{pad, body.top + Px(4), rc.right - pad, rc.bottom - Px(70)};
            DrawTextBlock(dc, g.small, Muted, g.note, note);
            DrawTextBlock(dc, g.small, Muted, g.detail, note);
        }
    }
    EndPaint(hwnd, &ps);
}

void DrawItem(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    const int id = int(d->CtlID);
    const bool focus = (d->itemState & ODS_FOCUS) != 0, pressed = (d->itemState & ODS_SELECTED) != 0, disabled = (d->itemState & ODS_DISABLED) != 0;
    HBRUSH bg = CreateSolidBrush(Canvas);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    wchar_t text[256];
    GetWindowTextW(d->hwndItem, text, 256);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g.font);
    if (id == IdDesktop || id == IdLaunch || id == IdRemoveData) {
        const bool on = id == IdDesktop ? g.options.desktop : id == IdLaunch ? g.options.launch : g.removeData;
        const int s = Px(16), y = (r.top + r.bottom - s) / 2;
        RECT box{r.left + Px(2), y, r.left + Px(2) + s, y + s};
        HBRUSH b = CreateSolidBrush(on ? Accent : Raised);
        HPEN pen = CreatePen(PS_SOLID, 1, on ? Accent : Line);
        SelectObject(dc, b);
        SelectObject(dc, pen);
        RoundRect(dc, box.left, box.top, box.right, box.bottom, Px(4), Px(4));
        DeleteObject(pen);
        DeleteObject(b);
        if (on) {
            HPEN tick = CreatePen(PS_SOLID, Px(2), RGB(255, 255, 255));
            SelectObject(dc, tick);
            MoveToEx(dc, box.left + Px(4), box.top + Px(8), nullptr);
            LineTo(dc, box.left + Px(7), box.top + Px(11));
            LineTo(dc, box.left + Px(12), box.top + Px(5));
            DeleteObject(tick);
        }
        RECT label{box.right + Px(10), r.top, r.right, r.bottom};
        SetTextColor(dc, disabled ? Muted : Ink);
        DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (focus) {
            RECT f = label;
            DrawTextW(dc, text, -1, &f, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
            f.top = r.top + Px(2), f.bottom = r.bottom - Px(2), f.left -= Px(3), f.right += Px(3);
            HBRUSH ring = CreateSolidBrush(Accent);
            FrameRect(dc, &f, ring);
            DeleteObject(ring);
        }
        return;
    }
    const bool primary = id == IdPrimary;
    COLORREF fill = primary ? (pressed ? Accent : AccentHover) : (pressed ? Line : Raised);
    if (primary && !pressed) fill = Accent;
    if (disabled) fill = Raised;
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, focus ? Px(2) : 1, focus ? RGB(255, 255, 255) : fill);
    SelectObject(dc, b);
    SelectObject(dc, pen);
    RoundRect(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, Px(8), Px(8));
    DeleteObject(pen);
    DeleteObject(b);
    SetTextColor(dc, disabled ? Muted : RGB(255, 255, 255));
    DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: Paint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: DrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (HIWORD(wp) != BN_CLICKED) return 0;
            if (id == IdDesktop) g.options.desktop = !g.options.desktop;
            else if (id == IdLaunch) g.options.launch = !g.options.launch;
            else if (id == IdRemoveData) g.removeData = !g.removeData;
            else if (id == IdPrimary) Primary();
            else if (id == IdSecondary || id == IDCANCEL) Secondary();
            if (id == IdDesktop || id == IdLaunch || id == IdRemoveData) InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
            return 0;
        }
        case MsgProgress: {
            const auto step = SC::Step(wp);
            const double f = double(lp) / 1000;
            g.progress = step == SC::Step::Prepare ? 0.6 * f : step == SC::Step::Install ? 0.6 + 0.3 * f : 0.9 + 0.1 * f;
            g.cancellable = step == SC::Step::Prepare && f < 1;
            g.status = step == SC::Step::Prepare ? T(tPrepare) : step == SC::Step::Install ? (g.options.launch ? T(tStarting) : T(tInstalling)) : T(tFinishing);
            EnableWindow(g.secondary, g.cancellable);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case MsgFinished: Finished(); return 0;
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            MakeFonts();
            Layout();
            return 0;
        }
        case WM_CLOSE:
            if (g.working) {  // never abandon a running install; cancel only where it is safe
                if (g.cancellable) g.cancel = true;
                if (!g.unattended) return 0;
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunWindow(HINSTANCE hi) {
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&cc);
    g.icon = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = hi;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"BrowserSetupWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    RegisterClassExW(&wc);
    g.dpi = GetDpiForSystem();
    RECT r{0, 0, Px(500), Px(320)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, g.dpi);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = r.right - r.left, h = r.bottom - r.top;
    g.window = CreateWindowExW(0, wc.lpszClassName, Title().c_str(), style, work.left + (work.right - work.left - w) / 2,
                               work.top + (work.bottom - work.top - h) / 2, w, h, nullptr, nullptr, hi, nullptr);
    if (!g.window) return 1;
    g.dpi = GetDpiForWindow(g.window);
    BOOL dark = TRUE;
    {
        using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
            if (auto set = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) set(g.window, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        }
    }
    MakeFonts();
    auto button = [&](int id, const wchar_t* text, DWORD extra = 0) {
        return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | extra, 0, 0, 10, 10, g.window,
                               reinterpret_cast<HMENU>(INT_PTR(id)), hi, nullptr);
    };
    g.desktop = button(IdDesktop, T(tDesktop));
    g.launch = button(IdLaunch, T(tLaunch));
    g.removeDataBox = button(IdRemoveData, T(tRemoveData));
    g.secondary = button(IdSecondary, T(tBtnCancel));
    g.primary = button(IdPrimary, T(tBtnInstall));
    Layout();
    SetPage(Page::Options);
    ShowWindow(g.window, SW_SHOWNORMAL);
    UpdateWindow(g.window);
    if (g.unattended) PostMessageW(g.window, WM_COMMAND, MAKEWPARAM(IdPrimary, BN_CLICKED), 0);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(g.window, WM_COMMAND, MAKEWPARAM(IdSecondary, BN_CLICKED), 0);
            continue;
        }
        if (IsDialogMessageW(g.window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g.exitCode;
}

std::wstring SelfPath() {
    std::wstring b(32768, L'\0');
    b.resize(GetModuleFileNameW(nullptr, b.data(), DWORD(b.size())));
    return b;
}
std::wstring Parent(const std::wstring& p) { return p.substr(0, p.find_last_of(L'\\')); }
std::wstring LowerW(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}

void PickLanguage(const std::wstring& root) {
    if (UP::PathExists(root + L"\\data\\ui-language")) {
        g_en = UiLanguage::Load(root + L"\\data").locale == I18n::Locale::En;
        return;
    }
    g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_FRENCH;
#ifdef SETUP_TESTING
    wchar_t v[8];
    if (GetEnvironmentVariableW(L"BROWSER_SETUP_TEST_LANG", v, 8)) g_en = std::wstring(v) == L"en";
#endif
}

// <root>\uninstall.exe must delete its own file: it re-runs from a temp copy, which waits for this process to exit.
int RelaunchFromTemp() {
    if (g.env.temp.empty() || !(UP::IsDirectory(g.env.temp) || CreateDirectoryW(g.env.temp.c_str(), nullptr))) return 4;
    wchar_t name[64];
    swprintf(name, 64, L"\\browser-uninstall-%08lx%08lx.exe", GetCurrentProcessId(), GetTickCount());
    const std::wstring copy = g.env.temp + name;
    if (!CopyFileW(g.self.c_str(), copy.c_str(), TRUE)) return 4;
    std::wstring cmd = L"\"" + copy + L"\" --uninstall --parent " + std::to_wstring(GetCurrentProcessId());
    if (g.unattended) cmd += L" --unattended";
    if (g.removeData) cmd += L" --remove-data";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, g.env.temp.c_str(), &si, &pi)) return 4;
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);  // exit now: the temp copy waits for this process so it can delete this file
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool uninstallArg = false;
    DWORD parent = 0;
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--uninstall") uninstallArg = true;
        else if (a == L"--parent" && i + 1 < argc) parent = DWORD(wcstoul(argv[++i], nullptr, 10));
        else if (a == L"--remove-data") g.removeData = true;
#ifdef SETUP_TESTING
        else if (a == L"--unattended") g.unattended = true;
        else if (a == L"--desktop") g.options.desktop = true;
        else if (a == L"--no-launch") g.options.launch = false;
        else if (a == L"--test-cancel") g.testCancel = true;
        else if (a == L"--shortcut-target" && i + 2 < argc) {  // fixture readback: <lnk> <out file>
            const std::string target = UP::Narrow(SC::ShortcutTarget(argv[i + 1]));
            const bool ok = UP::AtomicWriteFile(argv[i + 2], target);
            LocalFree(argv);
            return ok ? 0 : 1;
        }
#endif
    }
    if (argv) LocalFree(argv);
    g.self = SelfPath();
    g.env = SC::DefaultEnv();
    PickLanguage(g.env.root);
    std::string error;
    const bool payload = SetupPayload::Open(g.self, BROWSER_VERSION_TEXT, g.layout, error);
    const std::wstring selfLower = LowerW(g.self);
    if (!payload && selfLower == LowerW(g.env.root + L"\\uninstall.exe")) {
        g.uninstall = true;
        return RelaunchFromTemp();
    }
    const std::wstring leaf = selfLower.substr(selfLower.find_last_of(L'\\') + 1);
    if (!payload && uninstallArg && LowerW(Parent(g.self)) == LowerW(g.env.temp) && leaf.rfind(L"browser-uninstall-", 0) == 0) {
        g.uninstall = true;
        if (parent) {  // the in-root copy exits right after starting this one
            if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, parent)) {
                WaitForSingleObject(h, 10000);
                CloseHandle(h);
            }
        }
        const int code = RunWindow(hi);
#ifdef SETUP_TESTING
        if (g.unattended) UP::AtomicWriteFile(SetupKey::TestRoot() + L"\\uninstall-result.txt", std::to_string(code));
#endif
        return code;
    }
    if (!payload) {
        // Truncated / corrupted download (or a renamed stub): nothing is written anywhere.
        if (!g.unattended) MessageBoxW(nullptr, T(eNotSetup), L"browser", MB_OK | MB_ICONERROR);
        return 3;
    }
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\browser-setup");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(single);
        return 5;
    }
    g.inspection = SC::Inspect(g.env, g.layout.version);
    const int code = RunWindow(hi);
    if (single) CloseHandle(single);
    return code;
}
