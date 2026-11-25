// tests/print: our print window's entrance driven by the browser's own motion loop (src/main.cpp: ScheduleMotion and
// the motion timer's InvalidateMotion), while nothing else in the browser moves. Windows are created, never shown.
//
//   print_motion_test.exe <repo root>
//
// Release run 25/11 (real window): the print window and its scrim stayed at alpha 0. ScheduleMotion left at
// « nothing moves » before it read the print window's wake, so the entrance fade never got a frame. The dialog test
// (print_dialog_test.cpp) calls Frame() itself and could not see it; this one goes through the browser's loop.
#include "../../src/main.cpp"
#include <cstdio>

static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

int wmain(int argc, wchar_t** argv) {
    using namespace std::chrono_literals;
    instance = GetModuleHandleW(nullptr);
    base = argc > 1 ? argv[1] : L".";   // the checkout: the print window's fonts (assets/fonts)
    window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1180, 815, nullptr, nullptr, instance, nullptr);   // never shown
    Check(window != nullptr, "browser window stand-in created (never shown)");
    CreatePrintSurfaces();   // the production hooks: wake = the browser's ScheduleMotion
    Check(printDialog.Window() && !IsWindowVisible(printDialog.Window()), "print window created, not shown");

    // Nothing moves in the browser: no frame, no timer.
    ScheduleMotion(NativeChrome::Motion::Now());
    Check(!motionDeadline.has_value(), "nothing moves: the motion loop asks for nothing");

    NativeChrome::PrintDialog::State st; st.printers = {L"Enregistrer en PDF", L"QA Laser"}; st.pdf = 0;
    Check(printDialog.OpenHeadlessForTest(st, RECT{0, 0, 1180, 760}, 8), "print window opened (headless)");
    const auto t0 = NativeChrome::Motion::Now();
    printDialog.StartEntranceForTest(t0);   // Show's entrance, then its wake hook
    Check(printDialog.AlphaForTest() == 0, "entrance starts transparent");
    Check(motionDeadline.has_value(), "the print window's entrance gets the browser's motion loop going (release 25/11: it did not)");

    // The motion timer's handler (src/main.cpp, MotionTimerId), frame after frame, as long as the loop asks for one.
    int frames = 0;
    while (motionDeadline && frames < 100) {
        const auto due = *motionDeadline;
        while (NativeChrome::Motion::Now() < due) Sleep(1);
        KillTimer(window, MotionTimerId); motionDeadline.reset();
        const auto now = NativeChrome::Motion::Now(); InvalidateMotion(now); ScheduleMotion(now); ++frames;
    }
    std::printf("     %d frame(s), %lld ms\n", frames, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(NativeChrome::Motion::Now() - t0).count());
    Check(frames >= 2 && frames < 100, "the loop ran the fade, then stopped by itself");
    Check(printDialog.AlphaForTest() == 255 && printDialog.ScrimAlphaForTest() == 128 && printDialog.SettledForTest(), "entrance lands 100 % (panel) and 50 % (scrim) through the browser's loop");
    Check(!motionDeadline.has_value(), "settled: no more frames asked");

    // The exit: Échap, then the loop hides the windows once the 120 ms fade is over.
    printDialog.KeyForTest(VK_ESCAPE);
    Check(motionDeadline.has_value(), "the exit fade gets the motion loop going");
    frames = 0;
    while (motionDeadline && frames < 100) {
        const auto due = *motionDeadline;
        while (NativeChrome::Motion::Now() < due) Sleep(1);
        KillTimer(window, MotionTimerId); motionDeadline.reset();
        const auto now = NativeChrome::Motion::Now(); InvalidateMotion(now); ScheduleMotion(now); ++frames;
    }
    Check(!printDialog.Visible() && !printDialog.Closing() && !motionDeadline.has_value(), "exit: the loop ends the fade-out and stops");

    DisposePrintSurfaces(); DestroyWindow(window);
    std::printf("print motion loop: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
