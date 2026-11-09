#pragma once
// Motion lab: a review-only Win32 tool window (`browser.exe --motion-lab`, toggle with
// Ctrl+Alt+Maj+M) with one slider per real animation parameter (motion-tuning.h), a
// "Rejouer" per icon, and Réinitialiser / Enregistrer / Recharger / Exporter. It never
// exists in a normal launch. Values are saved beside the executable
// (motion-tuning.ini), never in the browsing profile. Standard controls keep it keyboard
// and screen-reader usable (Tab between controls, arrows on sliders).
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include <vector>
#include "motion-tuning.h"

namespace NativeChrome { namespace MotionLab {
class Panel {
    HWND hwnd_ = nullptr, owner_ = nullptr, status_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr;
    HWND sliders_[Tuning::Count] = {}, values_[Tuning::Count] = {};
    std::wstring file_;
    UINT dpi_ = 96;
    enum { SliderBase = 100, ValueBase = 200, ReplayBase = 300, ResetId = 400, SaveId, LoadId, ExportId };
    int S(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }
    static int Ticks(Tuning::Id id) { const auto& p = Tuning::Spec(id); return static_cast<int>(std::lround((p.max - p.min) / p.step)); }
    static int Tick(Tuning::Id id, float v) { const auto& p = Tuning::Spec(id); return static_cast<int>(std::lround((v - p.min) / p.step)); }
    static std::wstring Format(Tuning::Id id, float v) {
        const auto& p = Tuning::Spec(id); wchar_t b[48]; swprintf(b, 48, L"%.*f %ls", p.decimals, v, p.unit); return b;
    }
    void SyncControls() {
        for (int i = 0; i < Tuning::Count; ++i) {
            const auto id = static_cast<Tuning::Id>(i); const float v = Tuning::Current()[id];
            SendMessageW(sliders_[i], TBM_SETPOS, TRUE, Tick(id, v)); SetWindowTextW(values_[i], Format(id, v).c_str());
        }
    }
    void Status(const std::wstring& text) { if (status_) SetWindowTextW(status_, text.c_str()); }
    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), FALSE); return c;
    }
    void Fonts() {
        if (font_) DeleteObject(font_); if (bold_) DeleteObject(bold_);
        NONCLIENTMETRICSW ncm{sizeof ncm}; SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi_);
        font_ = CreateFontIndirectW(&ncm.lfMessageFont); ncm.lfMessageFont.lfWeight = FW_SEMIBOLD; bold_ = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    void Relayout(UINT dpi) {
        const std::wstring status = status_ ? Text(status_) : L"";
        while (HWND child = GetWindow(hwnd_, GW_CHILD)) DestroyWindow(child);
        status_ = nullptr; dpi_ = dpi ? dpi : 96; Fonts(); Build(); Status(status);
    }
    static std::wstring Text(HWND h) { wchar_t b[512]; GetWindowTextW(h, b, 512); return b; }
    // Keep the whole panel on the owner's monitor (right edge of the browser, below its toolbar).
    void Place() {
        RECT o{}, w{}; GetWindowRect(owner_, &o); GetWindowRect(hwnd_, &w);
        MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT a = mi.rcWork; const int width = w.right - w.left, height = w.bottom - w.top;
        const int x = std::clamp(static_cast<int>(o.right - width - S(16)), static_cast<int>(a.left), static_cast<int>(std::max(a.left, a.right - width)));
        const int y = std::clamp(static_cast<int>(o.top + S(90)), static_cast<int>(a.top), static_cast<int>(std::max(a.top, a.bottom - height)));
        SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void Build() {
        int y = 10;
        Make(L"STATIC", L"Réglages d'animation des icônes (aperçu uniquement : aucun rechargement, aucune navigation ni téléchargement).", SS_LEFT, 12, y, 416, 32, 0, font_);
        y += 40;
        for (int g = 0; g < Tuning::Groups; ++g) {
            Make(L"STATIC", Tuning::GroupName(static_cast<Tuning::Group>(g)), SS_LEFT, 12, y + 4, 250, 18, 0, bold_);
            Make(L"BUTTON", L"Rejouer", WS_TABSTOP | BS_PUSHBUTTON, 340, y, 88, 24, ReplayBase + g, font_);
            y += 30;
            for (int i = 0; i < Tuning::Count; ++i) {
                const auto& p = Tuning::Params()[i]; if (p.group != g) continue;
                Make(L"STATIC", p.label, SS_LEFT, 24, y + 4, 150, 18, 0, font_);
                sliders_[i] = Make(TRACKBAR_CLASSW, p.label, WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 176, y, 170, 26, SliderBase + i, font_);
                SendMessageW(sliders_[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, Ticks(p.id)));
                SendMessageW(sliders_[i], TBM_SETLINESIZE, 0, 1); SendMessageW(sliders_[i], TBM_SETPAGESIZE, 0, std::max(1, Ticks(p.id) / 10));
                values_[i] = Make(L"STATIC", L"", SS_RIGHT, 350, y + 4, 78, 18, ValueBase + i, font_);
                y += 28;
            }
            y += 8;
        }
        Make(L"BUTTON", L"Réinitialiser", WS_TABSTOP | BS_PUSHBUTTON, 12, y, 98, 26, ResetId, font_);
        Make(L"BUTTON", L"Enregistrer", WS_TABSTOP | BS_PUSHBUTTON, 116, y, 98, 26, SaveId, font_);
        Make(L"BUTTON", L"Recharger", WS_TABSTOP | BS_PUSHBUTTON, 220, y, 98, 26, LoadId, font_);
        Make(L"BUTTON", L"Exporter", WS_TABSTOP | BS_PUSHBUTTON, 324, y, 104, 26, ExportId, font_);
        y += 34;
        status_ = Make(L"STATIC", L"", SS_LEFT, 12, y, 416, 34, 0, font_);
        y += 42;
        RECT r{0, 0, S(440), S(y)}; AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SyncControls();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (self) switch (m) {
        case WM_HSCROLL: {
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(l)) - SliderBase;
            if (id >= 0 && id < Tuning::Count) {
                const auto tid = static_cast<Tuning::Id>(id); const auto& p = Tuning::Spec(tid);
                const int pos = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(l), TBM_GETPOS, 0, 0));
                Tuning::Current().Set(tid, p.min + pos * p.step);
                SetWindowTextW(self->values_[id], Format(tid, Tuning::Current()[tid]).c_str());
                if (self->changed) self->changed();
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (id >= ReplayBase && id < ReplayBase + Tuning::Groups) { if (self->replay) self->Status(self->replay(static_cast<Tuning::Group>(id - ReplayBase))); return 0; }
            if (id == ResetId) { Tuning::Current().Reset(); self->SyncControls(); if (self->changed) self->changed(); self->Status(L"Valeurs par défaut rétablies (non enregistrées)."); return 0; }
            if (id == SaveId) { self->Status(self->Save() ? L"Enregistré : " + self->file_ : L"Échec de l'enregistrement : " + self->file_); return 0; }
            if (id == LoadId) { const int n = self->Load(); self->Status(n < 0 ? L"Aucun fichier : " + self->file_ : L"Rechargé (" + std::to_wstring(n) + L" valeurs) : " + self->file_); return 0; }
            if (id == ExportId) { self->Status(self->Export() ? L"Réglages copiés dans le presse-papiers (format motion-tuning.ini)." : L"Presse-papiers indisponible."); return 0; }
            break;
        }
        case WM_CTLCOLORSTATIC: break;
        case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
        // Another monitor's DPI: lay every control out again at that scale (not just the frame).
        case WM_DPICHANGED: { const RECT* r = reinterpret_cast<RECT*>(l); SetWindowPos(h, nullptr, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); self->Relayout(HIWORD(w)); return 0; }
        default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }
public:
    std::function<void()> changed;                       // a value moved: re-apply to the models
    std::function<std::wstring(Tuning::Group)> replay;   // replay one icon; returns a status line
    HWND Window() const { return hwnd_; }
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& file) {
        owner_ = owner; file_ = file;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserMotionLab";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        RECT o{}; GetWindowRect(owner, &o);
        // Created on the owner's monitor so it starts at that monitor's DPI.
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Animations des icônes — labo", WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                        o.left + 40, o.top + 40, 200, 200, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        dpi_ = GetDpiForWindow(hwnd_); if (!dpi_) dpi_ = 96;
        Fonts(); Build(); Place();
        Status(Load() >= 0 ? L"Réglages chargés depuis " + file_ : L"Valeurs par défaut. Enregistrer écrit " + file_);
        return true;
    }
    void Toggle() { if (!hwnd_) return; ShowWindow(hwnd_, Visible() ? SW_HIDE : SW_SHOWNOACTIVATE); }
    void Show() { if (hwnd_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }
    void SetStatus(const std::wstring& s) { Status(s); }
    bool Save() const {
        const std::string text = Tuning::Current().Serialize();
        HANDLE f = CreateFileW(file_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0; const BOOL ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr); CloseHandle(f);
        return ok && written == text.size();
    }
    // Number of values applied, -1 when the file is missing.
    int Load() {
        HANDLE f = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return -1;
        std::string text; char buffer[4096]; DWORD read = 0;
        while (ReadFile(f, buffer, sizeof buffer, &read, nullptr) && read) { text.append(buffer, read); if (text.size() > 65536) break; }
        CloseHandle(f);
        const int n = Tuning::Current().Parse(text);
        if (hwnd_) SyncControls();
        if (changed) changed();
        return n;
    }
    bool Export() const {
        const std::string text = Tuning::Current().Serialize();
        const std::wstring wide(text.begin(), text.end());
        if (!OpenClipboard(hwnd_)) return false;
        bool ok = false; const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* p = GlobalLock(mem)) { memcpy(p, wide.c_str(), bytes); GlobalUnlock(mem); EmptyClipboard(); ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr; }
            if (!ok) GlobalFree(mem);
        }
        CloseClipboard(); return ok;
    }
    void Dispose() { if (hwnd_) DestroyWindow(hwnd_); hwnd_ = nullptr; if (font_) DeleteObject(font_); if (bold_) DeleteObject(bold_); font_ = bold_ = nullptr; }
};
}} // namespace NativeChrome::MotionLab
