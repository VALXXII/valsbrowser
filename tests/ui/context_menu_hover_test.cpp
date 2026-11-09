// Native context menu: the hover fill of a submenu's row follows the pointer, also while
// that submenu is still entering. Isolated owner window, demo items, no browser/WebView2.
// The pointer is driven through the menu's own move/leave handlers (what WM_MOUSEMOVE and
// WM_MOUSELEAVE run), so the real cursor never matters. Steps run inside Track()'s modal loop.
// Usage: context_menu_hover_test.exe <repo root>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <functional>
#include <thread>
#include "../../src/context-menu.h"

using namespace NativeChrome;
namespace CM = NativeChrome::ContextMenu;
static int failures = 0;
static void Expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

constexpr UINT RunMessage = WM_APP + 60;
static HWND helper = nullptr;
static LRESULT CALLBACK HelperProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == RunMessage) { (*reinterpret_cast<std::function<void()>*>(l))(); return 0; }
    return DefWindowProcW(h, msg, w, l);
}
static void OnUi(std::function<void()> f) { SendMessageW(helper, RunMessage, 0, reinterpret_cast<LPARAM>(&f)); }
template <class F> static bool WaitFor(F condition, int ms = 2000) { for (int t = 0; t < ms; t += 5) { if (condition()) return true; Sleep(5); } return condition(); }

static CM::Menu menu;
static const wchar_t* Branch = L"Avec un autre site";
static float Fill(int level, const wchar_t* label) { float f = 0; OnUi([&] { f = menu.TestRowFill(level, label, Motion::Now()); }); return f; }
static int Depth() { int d = 0; OnUi([&] { d = menu.Depth(); }); return d; }
static bool Entering(int level) { bool e = false; OnUi([&] { e = menu.TestEntering(level); }); return e; }
static void Move(int level, const wchar_t* label) { OnUi([&] { menu.TestPointerMove(level, label); }); }
static void Leave(int level) { OnUi([&] { menu.TestPointerLeave(level); }); }

static std::vector<CM::Item> Items() {
    auto item = [](int id, const wchar_t* label) { CM::Item i; i.id = id; i.label = label; return i; };
    CM::Item deeper = item(30, L"Placer…"); deeper.children = {item(31, L"À gauche"), item(32, L"À droite")};
    CM::Item branch = item(20, Branch); branch.children = {item(21, L"Nouvel onglet"), deeper};
    return {item(1, L"Au-dessus"), item(2, L"En dessous"), branch, item(3, L"Imprimer…"), item(4, L"Enregistrer en PDF")};
}

// One Track() with `steps` running on a client thread while the modal loop pumps.
static int Run(bool reduced, std::function<void()> steps) {
    int result = -1; const auto items = Items();
    std::thread client([&] { WaitFor([] { return Depth() == 1; }); steps(); });
    result = menu.Track(items, {200, 200}, nullptr, false, reduced);
    client.join();
    return result;
}

static void Interrupted() {
    // Quick departure: the branch opens, and the pointer leaves its row while the submenu is still entering.
    Move(0, Branch);
    Expect(WaitFor([] { return Depth() == 2; }), "hovering the branch opens its submenu after the delay");
    const bool entering = Entering(1);
    Move(0, L"Enregistrer en PDF");
    Sleep(30);
    const float branchMid = Fill(0, Branch), pdfMid = Fill(0, L"Enregistrer en PDF");
    Expect(entering, "the pointer left while the submenu was still entering");
    Expect(Depth() == 2, "the submenu stays open for the trajectory (no early close)");
    Expect(branchMid < 1.f && pdfMid > 0.f, "30 ms after leaving: the branch fill is already fading, the new row filling");
    Sleep(140);
    Expect(Fill(0, Branch) == 0.f && Fill(0, L"Enregistrer en PDF") == 1.f, "120 ms fade: branch row empty, new row full, submenu or not");
    Expect(WaitFor([] { return Depth() == 1; }), "the submenu closes after the hover delay on another row");
    // Between two rows: the old fill retargets from where it is, the new one starts at once.
    Move(0, L"Imprimer…"); Sleep(40); Move(0, L"Au-dessus"); Sleep(20);
    Expect(Fill(0, L"Imprimer…") < 1.f && Fill(0, L"Au-dessus") > 0.f, "row to row: interrupted fade, no wait");
    // Parent -> child: the open branch does not hold a fill the pointer left.
    Move(0, Branch); WaitFor([] { return Depth() == 2; }); Sleep(130);
    Leave(0); Move(1, L"Nouvel onglet"); Sleep(150);
    Expect(Depth() == 2 && Fill(0, Branch) == 0.f && Fill(1, L"Nouvel onglet") == 1.f, "pointer in the submenu: its row fills, the branch row does not");
    // Child -> parent: back on the branch row it fills again and the submenu stays.
    Leave(1); Move(0, Branch); Sleep(150);
    Expect(Depth() == 2 && Fill(0, Branch) == 1.f, "back on the branch row: fill returns, submenu kept");
    // Several cascades: a deeper branch behaves the same.
    Leave(0); Move(1, L"Placer…"); Expect(WaitFor([] { return Depth() == 3; }), "a second-level branch opens");
    Move(1, L"Nouvel onglet"); Sleep(30);
    Expect(Depth() == 3 && Fill(1, L"Placer…") < 1.f, "second level: leaving the branch row fades it while its submenu is open");
    // Leaving every menu: nothing stays filled.
    Leave(1); Leave(0); Sleep(150);
    Expect(Fill(0, Branch) == 0.f && Fill(1, L"Placer…") == 0.f && Fill(1, L"Nouvel onglet") == 0.f, "outside every menu: no fill anywhere");
    // Keyboard: Right opens and walks in, Left returns focus (fill + ring) to the branch row.
    OnUi([] { menu.TestHover(1, L"Placer…", true); menu.TestKey(VK_RIGHT); });
    Expect(Depth() == 3, "Right opens the submenu");
    OnUi([] { menu.TestKey(VK_LEFT); }); Sleep(150);
    Expect(Depth() == 2 && Fill(1, L"Placer…") == 1.f, "Left closes it and the keyboard focus is back on its row");
    // Closing mid-entrance: no stale timer brings a hover back.
    Leave(1); Move(0, L"Enregistrer en PDF"); WaitFor([] { return Depth() == 1; });
    Move(0, Branch); WaitFor([] { return Depth() == 2; });
    Expect(Entering(1), "closing while the submenu is still entering");
    OnUi([] { menu.TestKey(VK_ESCAPE); menu.TestKey(VK_ESCAPE); });
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { std::fputs("usage: context_menu_hover_test <root>\n", stderr); return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW hc{}; hc.lpfnWndProc = HelperProc; hc.hInstance = instance; hc.lpszClassName = L"MenuHoverTestHelper"; RegisterClassW(&hc);
    helper = CreateWindowW(hc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    WNDCLASSW oc{}; oc.lpfnWndProc = DefWindowProcW; oc.hInstance = instance; oc.lpszClassName = L"MenuHoverTestOwner"; RegisterClassW(&oc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, oc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    menu.Init(instance, owner, std::wstring(argv[1]));
    menu.ignorePointer = true; // the real cursor, wherever it is, must not take part

    const int dismissed = Run(false, Interrupted);
    Expect(dismissed == 0 && Depth() == 0, "Escape during the cascade closes every level, nothing chosen");
    Sleep(300);
    Expect(Depth() == 0, "no stale submenu timer reopens anything after the close");

    // Reduced motion: no entrance, fills switch at once.
    Run(true, [] {
        Move(0, Branch); WaitFor([] { return Depth() == 2; });
        Expect(!Entering(1), "reduced motion: the submenu arrives without an entrance");
        Move(0, L"Enregistrer en PDF");
        Expect(Fill(0, Branch) == 0.f && Fill(0, L"Enregistrer en PDF") == 1.f, "reduced motion: the fill follows the pointer instantly");
        OnUi([] { menu.TestKey(VK_ESCAPE); menu.TestKey(VK_ESCAPE); });
    });

    menu.Dispose(); DestroyWindow(owner); DestroyWindow(helper); CoUninitialize();
    if (failures) { std::fprintf(stderr, "%d context menu hover check(s) failed\n", failures); return 1; }
    std::puts("PASS: submenu row hover follows the pointer mid-entrance; cascade, keyboard, close and reduced motion intact");
    return 0;
}
