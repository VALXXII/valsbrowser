// NativeUI::InlineRename on its own (src/inline-rename.h): a real EDIT on our own window, driven by window
// messages and thread-local keyboard state only (no SendInput, no foreground change, no clipboard).
// Checks: placement on the label box, label font, select-all, Enter / Escape / Tab / focus loss, IME
// composition guard, Unicode, text undo, keys kept from the host, UIA name/value (MSAA proxy), 96/144 DPI.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "../../src/inline-rename.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void Expect(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } else std::printf("ok   %s\n", what.c_str()); }
static void Pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
static NativeUI::InlineRename editor;
static int hostKeys = 0;
static LRESULT CALLBACK Host(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CTLCOLOREDIT && editor.Owns(reinterpret_cast<HWND>(l))) return editor.Color(reinterpret_cast<HDC>(w));
    if (msg == WM_KEYDOWN || msg == WM_CHAR) ++hostKeys;
    return DefWindowProcW(h, msg, w, l);
}
static void Keys(bool ctrl, bool shift) { BYTE k[256]; GetKeyboardState(k); k[VK_CONTROL] = k[VK_LCONTROL] = ctrl ? 0x80 : 0; k[VK_SHIFT] = k[VK_LSHIFT] = shift ? 0x80 : 0; SetKeyboardState(k); }
// A key as TranslateMessage would deliver it to the focused EDIT: WM_KEYDOWN then its WM_CHAR, if any.
static void Key(HWND to, WORD vk, wchar_t ch = 0, bool ctrl = false, bool shift = false) {
    Keys(ctrl, shift); SendMessageW(to, WM_KEYDOWN, vk, 1); if (ch) SendMessageW(to, WM_CHAR, ch, 1); SendMessageW(to, WM_KEYUP, vk, 0xC0000001); Keys(false, false); Pump();
}
static std::wstring Text(HWND e) { const int n = GetWindowTextLengthW(e); std::wstring s(n + 1, 0); GetWindowTextW(e, s.data(), n + 1); s.resize(n); return s; }

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = Host; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"InlineRenameTest"; RegisterClassW(&wc);
    for (int dpi : {96, 144}) {
        auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
        const std::string at = " @" + std::to_string(dpi) + "dpi";
        HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP | WS_CLIPCHILDREN, -20000, -20000, S(400), S(40), nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(host, SW_SHOWNOACTIVATE);
        HFONT font = CreateFontW(-S(14), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Inter Display");
        const NativeUI::InlineRename::Style style{font, RGB(255, 255, 255), RGB(51, 51, 51)};
        struct Result { int calls = 0; bool committed = false; std::wstring text; unsigned long long id = 0; } res;
        auto done = [&res](unsigned long long id, bool committed, const std::wstring& text) { ++res.calls; res.committed = committed; res.text = text; res.id = id; };
        const RECT box{S(36), S(8), S(150), S(32)};

        // Start: placed on the label box, vertically centred, focused, whole name selected.
        Expect(editor.Begin(host, 42, box, L"Game", style, L"Nom", done), "editor starts" + at);
        HWND e = editor.Edit(); RECT r{}; GetWindowRect(e, &r); MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        Expect(r.left == box.left && r.right == box.right && r.top >= box.top && r.bottom <= box.bottom && std::abs((r.top + r.bottom) - (box.top + box.bottom)) <= 1, "EDIT on the label box, centred (" + std::to_string(r.left) + "," + std::to_string(r.top) + ")-(" + std::to_string(r.right) + "," + std::to_string(r.bottom) + ")" + at);
        Expect(GetFocus() == e, "focus in the editor" + at);
        DWORD a = 0, b = 0; SendMessageW(e, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
        Expect(a == 0 && b == 4, "whole name selected" + at);
        Expect(reinterpret_cast<HFONT>(SendMessageW(e, WM_GETFONT, 0, 0)) == font, "label font on the EDIT" + at);
        Expect(!(GetWindowLongPtrW(e, GWL_STYLE) & WS_BORDER) && !(GetWindowLongPtrW(e, GWL_EXSTYLE) & WS_EX_CLIENTEDGE), "no border, no frame" + at);
        { HDC dc = GetDC(e); SendMessageW(host, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(e)); Expect(GetTextColor(dc) == style.text && GetBkColor(dc) == style.back, "label colours (text, item surface)" + at); ReleaseDC(e, dc); }
        // UIA (MSAA proxy): name from the annotation, value = the text.
        { IAccessible* acc = nullptr; VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF; BSTR name = nullptr, value = nullptr;
          if (SUCCEEDED(AccessibleObjectFromWindow(e, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible, reinterpret_cast<void**>(&acc))) && acc) { acc->get_accName(self, &name); acc->get_accValue(self, &value); acc->Release(); }
          Expect(name && std::wstring(name) == L"Nom" && value && std::wstring(value) == L"Game", "accessible name « Nom », value « Game »" + at); SysFreeString(name); SysFreeString(value); }
        // Typing replaces the selection; Unicode; text undo; host never sees the keys.
        hostKeys = 0;
        for (wchar_t c : std::wstring(L"Jeux vidéo 🎮")) SendMessageW(e, WM_CHAR, c, 1);
        Expect(Text(e) == L"Jeux vidéo 🎮", "typing replaces the selected name (Unicode, surrogate pair)" + at);
        Key(e, 'Z', 0x1A, true);   // Ctrl+Z inside the EDIT: its own text undo
        Expect(Text(e) == L"Game", "Ctrl+Z is the EDIT's text undo" + at);
        Key(e, 'Z', 0x1A, true); Expect(Text(e) == L"Jeux vidéo 🎮", "Ctrl+Z again redoes (EDIT's single-level undo)" + at);
        Key(e, VK_HOME); Key(e, VK_RIGHT, 0, true, true); SendMessageW(e, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
        Expect(a == 0 && b > 0 && b < 6, "Home, Ctrl+Shift+Right: native word selection" + at);
        Expect(hostKeys == 0, "no key reached the host window" + at);
        Expect(res.calls == 0, "nothing committed while typing" + at);
        // IME composition: Enter / Escape belong to the IME.
        SendMessageW(e, WM_IME_STARTCOMPOSITION, 0, 0); Key(e, VK_RETURN, L'\r'); Key(e, VK_ESCAPE, 27);
        Expect(res.calls == 0 && editor.Active(), "during IME composition Enter / Escape do not commit or cancel" + at);
        SendMessageW(e, WM_IME_ENDCOMPOSITION, 0, 0);
        // Enter commits the typed text; the EDIT goes away and focus returns to the host.
        Key(e, VK_RETURN, L'\r');
        Expect(res.calls == 1 && res.committed && res.text == L"Jeux vidéo 🎮" && res.id == 42, "Enter commits the text with the item id" + at);
        Expect(!editor.Active() && !IsWindow(e) && GetFocus() == host, "EDIT destroyed, focus back on the host" + at);

        // Escape restores: no commit, nothing returned.
        res = {}; editor.Begin(host, 43, box, L"Musique", style, L"Nom", done); e = editor.Edit();
        for (wchar_t c : std::wstring(L"XYZ")) SendMessageW(e, WM_CHAR, c, 1);
        Key(e, VK_ESCAPE, 27);
        Expect(res.calls == 1 && !res.committed && res.text.empty() && !editor.Active(), "Escape cancels (old name kept by the owner)" + at);
        // Tab commits.
        res = {}; editor.Begin(host, 44, box, L"A", style, L"Nom", done); e = editor.Edit(); SendMessageW(e, WM_CHAR, L'B', 1); Key(e, VK_TAB, L'\t');
        Expect(res.calls == 1 && res.committed && res.text == L"B", "Tab commits, no tab character inserted" + at);
        // Focus loss commits (deferred out of WM_KILLFOCUS).
        res = {}; editor.Begin(host, 45, box, L"Old", style, L"Nom", done); e = editor.Edit(); SendMessageW(e, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"New"));
        HWND other = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, host, nullptr, wc.hInstance, nullptr);
        SetFocus(other); Expect(res.calls == 0, "not committed from inside WM_KILLFOCUS" + at); Pump();
        Expect(res.calls == 1 && res.committed && res.text == L"New" && GetFocus() == other, "focus loss commits; focus stays where it went" + at);
        DestroyWindow(other);
        // Empty text is reported as is: the owner keeps the old name (tested with main.cpp).
        res = {}; editor.Begin(host, 46, box, L"Old", style, L"Nom", done); e = editor.Edit(); Key(e, VK_DELETE); Key(e, VK_RETURN, L'\r');
        Expect(res.calls == 1 && res.committed && res.text.empty(), "an emptied name is reported empty (owner decides)" + at);
        // Move follows the label; Cancel() from the owner (relayout) reports a cancel.
        res = {}; editor.Begin(host, 47, box, L"Old", style, L"Nom", done); e = editor.Edit();
        editor.Move({S(60), S(4), S(200), S(36)}); GetWindowRect(e, &r); MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        Expect(r.left == S(60) && r.right == S(200), "Move() follows the label" + at);
        editor.Cancel(); Expect(res.calls == 1 && !res.committed && !IsWindow(e), "owner Cancel() destroys the EDIT, reports a cancel" + at);
        // A callback that starts a new edit (editor the next item) is safe.
        res = {}; editor.Begin(host, 48, box, L"One", style, L"Nom", [&](unsigned long long, bool, const std::wstring&) { ++res.calls; editor.Begin(host, 49, box, L"Two", style, L"Nom", done); });
        Key(editor.Edit(), VK_RETURN, L'\r'); Expect(editor.Active() && editor.Id() == 49 && Text(editor.Edit()) == L"Two", "a new edit can start from the callback" + at); editor.Cancel();
        DestroyWindow(host); DeleteObject(font);
    }
    if (failures) { std::fprintf(stderr, "%d inline rename check(s) failed\n", failures); return 1; }
    std::puts("PASS inline rename editor: placement, font, select-all, Enter/Escape/Tab/blur, IME guard, Unicode, text undo, keys kept, UIA name/value, 96/144 DPI");
    return 0;
}
