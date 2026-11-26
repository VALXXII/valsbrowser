// Typing flicker in every native text field (bug « quand on écrit sur la barre d'adresse, des fois le
// texte clignote un peu »). Each field is the real one (its own header, its own window procedures),
// driven by posted keyboard messages, and watched by a probe that records what the field looks like
// at every step a compositor could show:
//   - on entry and on exit of EVERY message the EDIT receives (nested ones included: a parent that
//     answers EN_CHANGE with WM_SETTEXT / EM_SETSEL runs between them), after its WM_ERASEBKGND, when
//     its parent receives WM_COMMAND, and after every WM_PAINT of its parent;
//   - the pixels are the field's box (the EDIT plus a few pixels of the parent's field around it), read
//     from the window surface DWM composes (GetDC + BitBlt), caret hidden.
// A keystroke is flicker-free when the field goes from what it showed before the key straight to what
// it shows once everything has settled, with NO frame in between: not an erased field, not the typed
// text without its inline completion, not a half-drawn placeholder, not the parent's fill over the
// text, not an EDIT whose colour lags behind the field drawn around it.
// Counts per keystroke (WM_ERASEBKGND and WM_PAINT of the EDIT, WM_PAINT of its parent, WM_SETTEXT,
// EDIT draws = WM_CTLCOLOREDIT) are printed with the verdict; the first in-between frame of a surface
// is saved as a BMP. Usage: edit_flicker_test.exe <repo root> [evidence dir]
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include "../../src/chrome.h"
#include "../../src/native-ui.h"
#include "../../src/address.h"
#include "../../src/address-popup.h"
#include "../../src/design-system.h"
#include "../../src/find-bar.h"
#include "../../src/modal.h"
#include "../../src/shortcuts-panel.h"
#include "../../src/bookmark-popover.h"
#include "../../src/bookmarks-panel.h"
#include "../../src/settings-panel.h"
#include "../../src/profile-editor.h"
#include "../../src/profile-dialog.h"
#include "../../src/inline-rename.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static std::wstring evidence;
static void Check(bool ok, const std::string& what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); } else std::printf("ok   %s\n", what.c_str()); }
// Steps whose change involves two surfaces (the parent's D2D field and the EDIT's own colour) are
// watched at the granularity of the message loop: what is on screen each time control comes back
// to it (loopSnap), i.e. whether a half-updated field survives until something else runs.
static void (*loopSnap)() = nullptr;
static void Pump(int ms = 40) { const DWORD end = GetTickCount() + ms; MSG m; do { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (!(m.message == WM_KEYDOWN && ((m.lParam >> 16) & 0xFF) == 0xFF)) TranslateMessage(&m); DispatchMessageW(&m); if (loopSnap) loopSnap(); } Sleep(1); } while (static_cast<int>(end - GetTickCount()) > 0); }
static HWND FindEdit(HWND parent, int index = 0) { HWND e = nullptr; for (int i = 0; i <= index; ++i) e = FindWindowExW(parent, e, L"Edit", nullptr); return e; }
static void Foreground(HWND w) {   // no injected input: the test window asks for the foreground, focus is set in-thread
    SetForegroundWindow(w); SetActiveWindow(w); Pump(30);
}

// ---- probe ---------------------------------------------------------------------------------------
struct Frame { int w = 0, h = 0; std::vector<DWORD> px; std::string where; RECT caret{}; bool operator==(const Frame& o) const { return w == o.w && h == o.h && px == o.px; } };
static void SaveBmp(const Frame& f, const std::wstring& path) {
    if (f.px.empty()) return;
    const int stride = f.w * 3 + (4 - f.w * 3 % 4) % 4; const DWORD size = stride * f.h;
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + size;
    ih.biSize = sizeof(ih); ih.biWidth = f.w; ih.biHeight = f.h; ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = size;
    std::vector<unsigned char> data(size);
    for (int y = 0; y < f.h; ++y) for (int x = 0; x < f.w; ++x) { const DWORD c = f.px[(f.h - 1 - y) * f.w + x]; unsigned char* p = &data[y * stride + x * 3]; p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255; }
    FILE* file = _wfopen(path.c_str(), L"wb"); if (!file) return; fwrite(&fh, sizeof(fh), 1, file); fwrite(&ih, sizeof(ih), 1, file); fwrite(data.data(), 1, size, file); fclose(file);
}
struct Probe {
    HWND edit = nullptr, parent = nullptr; bool armed = false, grabbing = false, strict = true; int pad = 4;
    static inline Probe* current = nullptr;
    RECT box{};   // the region watched during one step (parent client pixels), fixed when the step begins
    std::vector<Frame> frames;
    int erase = 0, editPaint = 0, parentPaint = 0, setText = 0, draws = 0;
    // The field's box: the EDIT and `pad` pixels of the parent around it.
    RECT FieldBox() const {
        RECT r{}; if (!IsWindowVisible(edit)) return r;
        GetWindowRect(edit, &r); MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&r), 2);
        RECT c{}; GetClientRect(parent, &c); InflateRect(&r, pad, pad); IntersectRect(&r, &r, &c); return r;
    }
    // The pixels of `box` on the parent's surface. The caret is left alone (hiding it would make the
    // text services query the field again); where it blinks is recorded and ignored when comparing.
    Frame Grab(const char* where) {
        Frame f; f.where = where; const RECT r = box;
        f.w = r.right - r.left; f.h = r.bottom - r.top; if (f.w <= 0 || f.h <= 0 || !IsWindowVisible(edit)) { f.w = f.h = 0; return f; }
        grabbing = true;
        GUITHREADINFO gti{}; gti.cbSize = sizeof(gti);
        if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret == edit) { f.caret = gti.rcCaret; MapWindowPoints(edit, parent, reinterpret_cast<POINT*>(&f.caret), 2); OffsetRect(&f.caret, -r.left, -r.top); InflateRect(&f.caret, 1, 0); }
        HDC src = GetDC(parent), mem = CreateCompatibleDC(src);
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = f.w; bi.bmiHeader.biHeight = -f.h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr; HBITMAP bmp = CreateDIBSection(src, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp); BitBlt(mem, 0, 0, f.w, f.h, src, r.left, r.top, SRCCOPY); GdiFlush();
        f.px.assign(static_cast<DWORD*>(bits), static_cast<DWORD*>(bits) + f.w * f.h);
        for (auto& p : f.px) p &= 0xFFFFFF;
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(parent, src);
        grabbing = false;
        return f;
    }
    void Snap(const char* where) { if (!armed || grabbing) return; Frame f = Grab(where); if (!frames.empty() && f == frames.back() && EqualRect(&f.caret, &frames.back().caret)) return; frames.push_back(std::move(f)); }
    // Read-only questions (the text services ask them again each time the caret is shown): nothing is
    // drawn there, no frame is taken.
    static bool Query(UINT msg) {
        switch (msg) {
        case EM_GETSEL: case WM_GETTEXTLENGTH: case WM_GETTEXT: case WM_GETFONT: case WM_GETDLGCODE: case WM_GETOBJECT:
        case EM_GETLINECOUNT: case EM_GETLINE: case EM_LINELENGTH: case EM_LINEINDEX: case EM_LINEFROMCHAR: case EM_POSFROMCHAR:
        case EM_CHARFROMPOS: case EM_GETMARGINS: case EM_GETRECT: case EM_GETFIRSTVISIBLELINE: case EM_GETMODIFY: case EM_GETLIMITTEXT:
        case EM_GETPASSWORDCHAR: case EM_CANUNDO: case EM_GETHANDLE: case WM_NCHITTEST: case WM_SETCURSOR: case WM_IME_REQUEST:
        case WM_IME_NOTIFY: case WM_IME_SETCONTEXT: return true;
        default: return false;
        }
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* p = reinterpret_cast<Probe*>(data);
        if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, EditProc, 0xF11C); return DefSubclassProc(h, msg, w, l); }
        if (!p->armed || p->grabbing || Query(msg)) return DefSubclassProc(h, msg, w, l);
        if (msg == WM_ERASEBKGND) ++p->erase; if (msg == WM_PAINT) ++p->editPaint; if (msg == WM_SETTEXT) ++p->setText;
        // WM_PRINTCLIENT / WM_PRINT draw into a memory DC: the screen is the same before and after them.
        const bool watch = p->strict && msg != WM_PRINTCLIENT && msg != WM_PRINT;
        if (watch) p->Snap("edit message");
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        if (watch) p->Snap(msg == WM_ERASEBKGND ? "after the field's erase" : "edit message done");
        return r;
    }
    static LRESULT CALLBACK ParentProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* p = reinterpret_cast<Probe*>(data);
        if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, ParentProc, 0xF11D); return DefSubclassProc(h, msg, w, l); }
        if (!p->armed || p->grabbing) return DefSubclassProc(h, msg, w, l);
        if (msg == WM_CTLCOLOREDIT && reinterpret_cast<HWND>(l) == p->edit) ++p->draws;
        if (msg == WM_PAINT) ++p->parentPaint;
        if (msg == WM_COMMAND && p->strict) p->Snap("EN_CHANGE reaches the parent");
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        if (msg == WM_PAINT && p->strict) p->Snap("after the parent's paint");
        if (msg == WM_COMMAND && p->strict) p->Snap("parent's EN_CHANGE work done");
        return r;
    }
    void Attach(HWND e, int padding = 4) {
        edit = e; parent = GetParent(e); pad = padding;
        SetWindowSubclass(edit, EditProc, 0xF11C, reinterpret_cast<DWORD_PTR>(this));   // last attached: sees every message first
        SetWindowSubclass(parent, ParentProc, 0xF11D, reinterpret_cast<DWORD_PTR>(this));
    }
    void Detach() { if (edit && IsWindow(edit)) RemoveWindowSubclass(edit, EditProc, 0xF11C); if (parent && IsWindow(parent)) RemoveWindowSubclass(parent, ParentProc, 0xF11D); edit = parent = nullptr; }
    struct Result { int distinct = 0, between = 0; std::string trail; const Frame* first = nullptr; };
    Result Measure() {
        // Every place the caret stood during the step is masked in every frame, then repeats collapse.
        std::vector<RECT> carets; for (const auto& f : frames) if (!IsRectEmpty(&f.caret)) carets.push_back(f.caret);
        for (auto& f : frames) for (const auto& c : carets) for (int y = std::max(0L, c.top); y < std::min<LONG>(f.h, c.bottom); ++y) for (int x = std::max(0L, c.left); x < std::min<LONG>(f.w, c.right); ++x) f.px[y * f.w + x] = 0;
        std::vector<Frame> kept; for (auto& f : frames) if (!f.px.empty() && (kept.empty() || !(f == kept.back()))) kept.push_back(std::move(f));
        frames = std::move(kept);
        Result r; if (frames.empty()) return r;
        const Frame& first = frames.front(); const Frame& last = frames.back();
        for (const auto& f : frames) {
            ++r.distinct;
            if (!(f == first) && !(f == last)) { ++r.between; if (!r.first) r.first = &f; if (r.trail.size() < 200) r.trail += " [" + f.where + "]"; }
        }
        return r;
    }
    // strict: every nested step of the EDIT and its parent; otherwise each turn of the message loop.
    void Begin(int settleMs, bool strictly = true) {
        Pump(settleMs); frames.clear(); erase = editPaint = parentPaint = setText = draws = 0; box = FieldBox(); strict = strictly; armed = true; frames.push_back(Grab("before"));
        if (!strict) { current = this; loopSnap = [] { current->Snap("back in the message loop"); }; }
    }
    Result End(int settleMs) { Pump(settleMs); Frame f = Grab("settled"); frames.push_back(std::move(f)); armed = false; loopSnap = nullptr; current = nullptr; strict = true; return Measure(); }
    // One key as the keyboard delivers it (WM_KEYDOWN, the translated WM_CHAR, WM_KEYUP), then
    // everything it caused (EN_CHANGE work, paints, timers) has settled. The character is posted
    // as such (the keydown carries scan code 0xFF so Pump does not translate it a second time).
    Result Key(WPARAM vk, wchar_t ch, int settleMs = 80, bool strictly = true) {
        Begin(settleMs, strictly);
        PostMessageW(edit, WM_KEYDOWN, vk, 1 | (0xFF << 16));
        if (ch) PostMessageW(edit, WM_CHAR, ch, 1 | (0xFF << 16));
        PostMessageW(edit, WM_KEYUP, vk, 0xC0000001 | (0xFF << 16));
        return End(settleMs);
    }
};

static std::map<std::string, int> saved;
static std::string Narrow(const std::wstring& s) { std::string o; for (wchar_t c : s) o += c < 128 ? static_cast<char>(c) : '?'; return o; }
static void Report(Probe& probe, const Probe::Result& r, const std::string& surface, const std::string& step) {
    std::printf("     %s %s: erase %d, edit paint %d, parent paint %d, settext %d, edit draws %d, distinct frames %d, in-between %d%s\n",
                surface.c_str(), step.c_str(), probe.erase, probe.editPaint, probe.parentPaint, probe.setText, probe.draws, r.distinct, r.between, r.trail.c_str());
    if (r.first && !evidence.empty() && saved[surface]++ == 0) {
        std::wstring name(surface.begin(), surface.end()); for (auto& c : name) if (c == L' ' || c == L'/') c = L'-';
        SaveBmp(probe.frames.front(), evidence + L"/" + name + L"-1-before.bmp");
        SaveBmp(*r.first, evidence + L"/" + name + L"-2-in-between.bmp");
        SaveBmp(probe.frames.back(), evidence + L"/" + name + L"-3-after.bmp");
    }
    Check(r.between == 0, surface + ": " + step + " shows no frame between before and after");
}
static void Type(Probe& probe, const std::string& surface, const std::wstring& text) {
    for (wchar_t c : text) {
        const WPARAM vk = (c >= L'a' && c <= L'z') ? (c - L'a' + 'A') : (c >= L'0' && c <= L'9') ? c : (c == L' ' ? VK_SPACE : VK_OEM_PERIOD);
        const auto r = probe.Key(vk, c);
        Report(probe, r, surface, std::string("typing '") + static_cast<char>(c) + "'");
    }
}
static void Backspace(Probe& probe, const std::string& surface, int n) {
    for (int i = 0; i < n; ++i) { const auto r = probe.Key(VK_BACK, L'\b'); Report(probe, r, surface, "Backspace " + std::to_string(i + 1)); }
}
static void Run(HWND edit, const std::string& surface, const std::wstring& text, int backspaces, int pad = 4) {
    if (!edit) { Check(false, surface + ": field found"); return; }
    SetFocus(edit); Pump(250);
    Probe probe; probe.Attach(edit, pad);
    Type(probe, surface, text);
    if (!evidence.empty() && !probe.frames.empty()) { std::wstring name(surface.begin(), surface.end()); for (auto& c : name) if (c == L' ' || c == L'/') c = L'-'; SaveBmp(probe.frames.back(), evidence + L"/sample-" + name + L".bmp"); }
    { const int n = GetWindowTextLengthW(edit); std::wstring s(n + 1, 0); GetWindowTextW(edit, s.data(), n + 1); s.resize(n); std::printf("     %s now reads \"%s\"\n", surface.c_str(), Narrow(s).c_str()); }
    Backspace(probe, surface, backspaces);
    probe.Detach();
}

// A posted job runs on a later turn of the message loop, as a WebView2 answer would.
static HWND jobs = nullptr; static std::vector<std::function<void()>> pending;
static LRESULT CALLBACK JobProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APP) { auto list = std::move(pending); pending.clear(); for (auto& f : list) f(); return 0; }
    return DefWindowProcW(h, msg, w, l);
}
static void Later(std::function<void()> f) { pending.push_back(std::move(f)); PostMessageW(jobs, WM_APP, 0, 0); }

// ---- address field: main.cpp's wiring (WM_CREATE, AddressProc's EN_CHANGE path, WM_CTLCOLOREDIT) ----
namespace AddressHost {
HWND window = nullptr, address = nullptr; HFONT font = nullptr;
NativeUI::EditSelection selection; NativeUI::AddressBackground background;
AddressBar::History history; bool setting = false, deleting = false;
AddressUI::Suggestions* popup = nullptr;
NativeUI::EditSelection::Colors Colors() { return {RGB(0x3E, 0x67, 0xFF), RGB(255, 255, 255)}; }
std::wstring Text() { const int n = GetWindowTextLengthW(address); std::wstring s(n + 1, 0); GetWindowTextW(address, s.data(), n + 1); s.resize(n); return s; }
// main.cpp SetAddressText / AddressEdited.
void SetAddressText(const std::wstring& s, int from, int to) { setting = true; SetWindowTextW(address, s.c_str()); SendMessageW(address, EM_SETSEL, from, to); setting = false; }
void Edited() {
    if (setting) return;
    const auto text = Text(); DWORD from = 0, to = 0; SendMessageW(address, EM_GETSEL, reinterpret_cast<WPARAM>(&from), reinterpret_cast<LPARAM>(&to));
    const bool atEnd = from == to && to == text.size();
    auto data = AddressBar::Suggest(text, history, atEnd && !deleting); deleting = false;
    if (!data.completion.empty()) SetAddressText(text + data.completion, static_cast<int>(text.size()), -1);
    if (popup) { RECT r; GetWindowRect(address, &r); popup->Show(std::move(data), 0, L"DuckDuckGo", r.left, r.bottom, r.right, GetDpiForWindow(window)); }
}
LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && (w == VK_BACK || w == VK_DELETE)) deleting = true;
    return DefSubclassProc(h, msg, w, l);
}
LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CTLCOLOREDIT: {
        using namespace NativeChrome::Design;
        HDC dc = reinterpret_cast<HDC>(w); RECT rect; GetClientRect(address, &rect);
        SetBkMode(dc, TRANSPARENT); SetBkColor(dc, NativeGray(Surface::raised));
        SetDCBrushColor(dc, NativeGray(Surface::raised)); SetBrushOrgEx(dc, 0, 0, nullptr);
        SetTextColor(dc, NativeGray(Surface::raised + (1 - Surface::raised) * Text::address));
        selection.Clip(reinterpret_cast<HWND>(l), dc, NativeGray(Surface::raised));
        return reinterpret_cast<LRESULT>(background.Brush(rect.right, 0, NativeGray(Surface::raised), NativeGray(Surface::raised)));
    }
    case WM_COMMAND: if (reinterpret_cast<HWND>(l) == address && HIWORD(w) == EN_CHANGE) Edited(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); HBRUSH b = CreateSolidBrush(NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::raised)); FillRect(dc, &ps.rcPaint, b); DeleteObject(b); EndPaint(h, &ps); return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}
}

static void AddressField(HINSTANCE hi, const std::wstring& root) {
    using namespace AddressHost;
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = L"FlickerAddressHost"; RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"address", WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 120, 120, 700, 90, nullptr, nullptr, hi, nullptr);
    address = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 40, 30, 600, 24, window, nullptr, hi, nullptr);
    SendMessageW(address, EM_SETLIMITTEXT, 8192, 0); SendMessageW(address, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SetWindowSubclass(address, EditProc, 1, 0);
    selection.Attach(address, Colors);
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Inter Display");
    SendMessageW(address, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    history.Record(L"https://yourrr.example/", L"Yourrr", 1); history.Record(L"https://yourrr.example/", L"Yourrr", 2);
    history.Record(L"https://youtube.com/", L"YouTube", 3);
    AddressUI::Suggestions suggestions; AddressUI::Hooks hooks;
    Check(suggestions.Create(hi, window, root, hooks), "address: suggestions popup created");
    popup = &suggestions;
    ShowWindow(address, SW_SHOW); Foreground(window);
    Run(address, "address bar", L"yourrr", 2, 0);
    // The list: when a keystroke changes its size, it is painted at the new size before the keystroke
    // returns (a window grown or shown but not yet painted shows stale or empty rows for a frame).
    {
        SetWindowTextW(address, L""); suggestions.Hide(); Pump();
        SendMessageW(address, WM_CHAR, L'y', 1);
        const bool pending = GetUpdateRect(suggestions.Window(), nullptr, FALSE) != FALSE;
        std::printf("     suggestions shown by a keystroke: visible %d, unpainted region left %d\n", suggestions.Visible(), pending);
        Check(suggestions.Visible() && !pending, "address bar: the suggestion list is painted within the keystroke that shows it");
        Pump();
        SendMessageW(address, WM_CHAR, L'o', 1); SendMessageW(address, WM_CHAR, L'u', 1); SendMessageW(address, WM_CHAR, L'r', 1);
        const bool pending2 = GetUpdateRect(suggestions.Window(), nullptr, FALSE) != FALSE;
        Check(suggestions.Visible() && !pending2, "address bar: the suggestion list is repainted within the keystroke that resizes it");
    }
    popup = nullptr; suggestions.Hide(); DestroyWindow(window); DeleteObject(font);
}

static RECT PageScreen() { return {100, 100, 1100, 800}; }

static void ModalField(HINSTANCE hi, HWND owner, const std::wstring& root) {
    Modal::Dialog d; Check(d.Create(hi, owner, root, {}), "modal: created");
    auto spec = Modal::PromptSpec(L"exemple.fr", L"Nom de la liste :", L""); spec.fields[0].placeholder = L"Nom";
    d.Show(spec, PageScreen(), [](const Modal::Result&) {}); d.FinishAnimationsForTest(); Foreground(d.Window());
    Run(d.EditForTest(0), "modal prompt", L"abc", 3);
    d.KeyForTest(VK_ESCAPE); Pump(); d.FinishAnimationsForTest();
}

static void FindField(HINSTANCE hi, HWND owner, const std::wstring& root) {
    // The page answers later, as WebView2 does: each answer is its own step (the counter appearing, the
    // field turning red on « Aucun résultat » are changes, not flicker), measured like a keystroke.
    FindBar::Bar bar; FindBar::Hooks hooks; std::function<void()> answer;
    hooks.query = [&](const std::wstring& text, const FindBar::Options&) {
        if (text.empty()) { answer = nullptr; return; }
        const bool none = text.find(L'z') != std::wstring::npos;
        answer = [&bar, none] { bar.SetResult(none ? 0 : 1, none ? 0 : 3, none ? FindBar::State::NoMatch : FindBar::State::Found); };
    };
    Check(bar.Create(hi, owner, root, hooks), "find bar: created");
    bar.Show(PageScreen(), L""); Pump(400); Foreground(bar.Window());
    HWND edit = bar.Edit(); SetFocus(edit); Pump(250);
    Probe probe; probe.Attach(edit);
    auto answered = [&](const std::string& after) {
        if (!answer) return;
        auto a = std::move(answer); answer = nullptr;
        probe.Begin(80, false); Later(std::move(a)); const auto r = probe.End(80);   // two surfaces change: message-loop granularity
        Report(probe, r, "find in page", "the page's answer after " + after);
    };
    for (wchar_t c : std::wstring(L"abz")) { const auto r = probe.Key(c - L'a' + 'A', c); Report(probe, r, "find in page", std::string("typing '") + static_cast<char>(c) + "'"); answered(std::string("'") + static_cast<char>(c) + "'"); }
    // The first Backspace leaves « Aucun résultat »: the field's colour changes with the text (loop granularity).
    for (int i = 0; i < 3; ++i) { const auto r = probe.Key(VK_BACK, L'\b', 80, i != 0); Report(probe, r, "find in page", "Backspace " + std::to_string(i + 1)); answered("Backspace " + std::to_string(i + 1)); }
    probe.Detach(); bar.Hide(); Pump();
}

static void ShortcutsField(HINSTANCE hi, HWND owner, const std::wstring& root) {
    ShortcutsPanel::Panel panel; Check(panel.Create(hi, owner, root), "shortcuts: created");
    panel.Show(PageScreen()); Pump(400); Foreground(panel.Window());
    Run(FindEdit(panel.Window()), "shortcuts search", L"tab", 3);
    panel.Hide(); Pump();
}

static void BookmarkFields(HINSTANCE hi, HWND owner, const std::wstring& root) {
    BookmarkPopover::Popover pop; BookmarkPopover::Hooks hooks;
    hooks.folders = [](unsigned) { return std::vector<std::tuple<unsigned, int, std::wstring>>{}; };
    Check(pop.Create(hi, owner, root, hooks), "bookmark popover: created");
    BookmarkPopover::Request request; request.id = 1; request.title = L"Exemple"; request.url = L"https://exemple.fr/";
    pop.Show(request, {600, 120, 640, 150}, PageScreen(), true, false, true); Pump(400); Foreground(pop.Window());
    Run(FindEdit(pop.Window(), 0), "bookmark name", L"ab", 2);
    // The address field turns invalid (danger) and valid again as it is typed.
    HWND url = FindEdit(pop.Window(), 1); SetWindowTextW(url, L"h"); SendMessageW(url, EM_SETSEL, 1, 1);
    Run(url, "bookmark address", L" t", 2);
    pop.Hide(); Pump();
}

static void SettingsSearch(HINSTANCE hi, HWND owner, const std::wstring& root) {
    BrowserSettings::State state; SettingsUI::Drawer panel; SettingsUI::Drawer::Hooks hooks;
    hooks.schedule = [&panel](Motion::Time) { Later([&panel] { panel.Frame(Motion::Now()); }); };   // display-paced frames, as main.cpp
    Check(panel.Create(hi, owner, root, &state, hooks), "settings: created");
    panel.Show(); Pump(700); Foreground(panel.Window());
    { BYTE keys[256]; GetKeyboardState(keys); keys[VK_CONTROL] = 0x80; SetKeyboardState(keys); panel.KeyForTest('F'); keys[VK_CONTROL] = 0; SetKeyboardState(keys); }   // Ctrl+F: the search field
    Pump(400);
    Run(panel.EditForTest(), "settings search", L"lan", 3);
    panel.Hide(false, false); Pump(300);
}

static void ProfileDialogField(HINSTANCE hi, HWND owner, const std::wstring& root) {
    ProfileDialog::Dialog dialog; dialog.Init(hi, owner, root);
    ProfileDialog::Spec spec; spec.seed.name = L""; spec.submit = [](const Profiles::Profile&, Profiles::NameError&) { return std::wstring(); };
    static ProfileDialog::Dialog* running = nullptr; running = &dialog;
    SetTimer(nullptr, 0, 400, [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        HWND w = running->Window(); Foreground(w);
        HWND edit = GetFocus();   // the dialog opens on its name field
        Run(edit, "profile name", L"abc", 3);
        // A repaint of the dialog itself (hover, the preview following the name): the field stays drawn.
        Probe probe; probe.Attach(edit); SetWindowTextW(edit, L"Travail"); Pump(200);
        probe.Begin(80); InvalidateRect(w, nullptr, FALSE); const auto r = probe.End(80);
        Report(probe, r, "profile name", "a repaint of the dialog"); probe.Detach();
        PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    });
    dialog.Run(spec);
    Pump();
}

static void FolderRename(HINSTANCE hi, HWND owner, const std::wstring& root) {
    Bookmarks::Store store; NativeUI::InlineRename rename;
    BookmarksUI::Drawer::Hooks hooks;
    hooks.editColor = [&](HDC dc, HWND e) -> LRESULT { return rename.Owns(e) ? rename.Color(dc) : 0; };
    BookmarksUI::Drawer panel; Check(panel.Create(hi, owner, root, &store, hooks), "bookmarks drawer: created");
    panel.Show(false); Pump(500); Foreground(panel.Window());
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
    NativeUI::InlineRename::Style style; style.font = font; style.text = RGB(0xE6, 0xE6, 0xE6); style.back = RGB(0x24, 0x24, 0x24);
    Check(rename.Begin(panel.Window(), 7, {40, 120, 260, 140}, L"Dossier", style, L"Nom", [](unsigned long long, bool, const std::wstring&) {}), "bookmarks drawer: rename started");
    SendMessageW(rename.Edit(), EM_SETSEL, 7, 7);
    Run(rename.Edit(), "folder rename (drawer)", L"ab", 2, 0);
    // The drawer repaints around the editor (hover, a list change): the editor's text stays on screen.
    Probe probe; probe.Attach(rename.Edit(), 0);
    probe.Begin(80); InvalidateRect(panel.Window(), nullptr, FALSE); const auto r = probe.End(80);
    Report(probe, r, "folder rename (drawer)", "a repaint of the drawer"); probe.Detach();
    rename.Cancel(); panel.Hide(false, false); Pump(300); DeleteObject(font);
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    const std::wstring root = argc > 1 ? argv[1] : L".";
    if (argc > 2) { evidence = argv[2]; CreateDirectoryW(evidence.c_str(), nullptr); }
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW jc{}; jc.lpfnWndProc = JobProc; jc.hInstance = hi; jc.lpszClassName = L"FlickerJobs"; RegisterClassW(&jc);
    jobs = CreateWindowExW(0, jc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hi, nullptr);
    WNDCLASSW oc{}; oc.lpfnWndProc = DefWindowProcW; oc.hInstance = hi; oc.lpszClassName = L"FlickerOwner"; oc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)); RegisterClassW(&oc);
    HWND owner = CreateWindowExW(0, oc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1000, 700, nullptr, nullptr, hi, nullptr);
    AddressField(hi, root);
    ModalField(hi, owner, root);
    FindField(hi, owner, root);
    ShortcutsField(hi, owner, root);
    BookmarkFields(hi, owner, root);
    SettingsSearch(hi, owner, root);
    ProfileDialogField(hi, owner, root);
    FolderRename(hi, owner, root);
    DestroyWindow(owner);
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
