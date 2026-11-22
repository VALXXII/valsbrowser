// Ctrl+A selects the whole text of our native text fields (address bar, settings search, find bar, dialogs...).
// Bug (2026-11-21): the plain Win32 single-line EDIT has no Ctrl+A, so it did nothing in the address bar.
// NativeUI::SelectAllOnCtrlA() adds it to an EDIT; this checks it on a real EDIT with Ctrl held.
#include <windows.h>
#include <cstdio>
#include <string>
#include "../../src/native-ui.h"

static int failures = 0;
static void Check(bool ok, const char* what) { if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }

static void HoldKeys(bool ctrl, bool shift, bool alt) {
    BYTE keys[256]{}; GetKeyboardState(keys);
    keys[VK_CONTROL] = keys[VK_LCONTROL] = ctrl ? 0x80 : 0;
    keys[VK_SHIFT] = keys[VK_LSHIFT] = shift ? 0x80 : 0;
    keys[VK_MENU] = keys[VK_LMENU] = alt ? 0x80 : 0;
    SetKeyboardState(keys);
}
static void Selection(HWND e, DWORD& from, DWORD& to) { SendMessageW(e, EM_GETSEL, reinterpret_cast<WPARAM>(&from), reinterpret_cast<LPARAM>(&to)); }
// What a real Ctrl+A delivers to the focused field: WM_KEYDOWN 'A', then WM_CHAR 0x01 (from TranslateMessage).
static void CtrlA(HWND e) { SendMessageW(e, WM_KEYDOWN, 'A', 0x001E0001); SendMessageW(e, WM_CHAR, 1, 0x001E0001); }

int main() {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    HWND host = CreateWindowExW(0, L"STATIC", L"host", WS_POPUP, 0, 0, 400, 100, nullptr, nullptr, hi, nullptr);
    const std::wstring text = L"https://exemple.test/chemin";
    for (int variant = 0; variant < 2; ++variant) {
        const bool multi = variant == 1;   // the annotation note is a multi-line EDIT
        HWND e = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | ES_AUTOHSCROLL | (multi ? ES_MULTILINE : 0), 0, 0, 300, 24, host, nullptr, hi, nullptr);
        Check(NativeUI::SelectAllOnCtrlA(e), multi ? "multi-line field: Ctrl+A support attached" : "single-line field: Ctrl+A support attached");
        SendMessageW(e, EM_SETSEL, 3, 3);
        HoldKeys(true, false, false); CtrlA(e);
        DWORD from = 0, to = 0; Selection(e, from, to);
        Check(from == 0 && to == text.size(), multi ? "multi-line: Ctrl+A selects all" : "single-line: Ctrl+A selects all");
        wchar_t buf[128]{}; GetWindowTextW(e, buf, 128);
        Check(text == buf, "Ctrl+A leaves the text unchanged (no control character typed)");
        // Ctrl+Shift+A and Ctrl+Alt+A are other shortcuts (Ctrl+Maj+A: mode Annotation): untouched.
        SendMessageW(e, EM_SETSEL, 3, 3); HoldKeys(true, true, false); SendMessageW(e, WM_KEYDOWN, 'A', 0x001E0001);
        Selection(e, from, to); Check(from == 3 && to == 3, "Ctrl+Shift+A does not select all");
        SendMessageW(e, EM_SETSEL, 3, 3); HoldKeys(true, false, true); SendMessageW(e, WM_KEYDOWN, 'A', 0x001E0001);
        Selection(e, from, to); Check(from == 3 && to == 3, "Ctrl+Alt+A (AltGr) does not select all");
        // A plain « a » still types.
        HoldKeys(false, false, false); SendMessageW(e, EM_SETSEL, 0, 0); SendMessageW(e, WM_CHAR, L'a', 0x001E0001);
        GetWindowTextW(e, buf, 128); Check(std::wstring(buf) == L"a" + text, "a plain « a » is still typed");
        DestroyWindow(e);
    }
    HoldKeys(false, false, false);
    DestroyWindow(host);
    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
