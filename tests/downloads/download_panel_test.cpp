// Download history: model checks + offscreen renders of the popover states.
// Usage: download_panel_test.exe <repo root> <output folder>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "../../src/download-panel.h"

using namespace NativeChrome;
using namespace NativeChrome::Downloads;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; } }

static void ModelChecks() {
    Expect(Bytes(52848230, 209715200) == L"50.4/200MB", "bytes in total's unit");
    Expect(Bytes(512, 2048) == L"0.5/2KB", "sub-unit received");
    Expect(Bytes(3 * 1024 * 1024, 0) == L"3MB", "unknown total");
    Expect(Bytes(1536LL * 1024 * 1024, 4LL * 1024 * 1024 * 1024) == L"1.5/4GB", "gigabytes");
    Expect(Remaining(14) == L"00:14" && Remaining(723) == L"12:03" && Remaining(3723) == L"1:02:03" && Remaining(-1).empty(), "remaining time");
    Expect(FileName(L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe") == L"orca-windows-setup.exe", "file name");
    Expect(FolderLabel(L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe") == L"In \u2026/Downloads", "folder label");
    Expect(FolderLabel(L"D:\\file.zip") == L"In D:", "drive root");
    Expect(ParseIsoTime(L"1970-01-01T00:00:14Z") == 14000, "iso epoch");
    Expect(ParseIsoTime(L"2024-05-01T12:00:00.250Z") == 1714564800250LL, "iso fraction");
    Expect(ParseIsoTime(L"2024-05-01T14:00:00+02:00") == 1714564800000LL, "iso offset");
    Expect(ParseIsoTime(L"") == 0 && ParseIsoTime(L"soon") == 0, "iso invalid");
    // History round trip: only finished rows, unicode intact, malformed lines skipped.
    std::vector<Item> items(4);
    items[0].state = State::InProgress; items[0].path = L"C:\\d\\partial.bin";
    items[1].state = State::Completed; items[1].path = L"C:\\d\\Caf\u00e9 \U0001F600.pdf"; items[1].total = 1234; items[1].uri = L"https://x.test/a";
    items[2].state = State::Failed; items[2].path = L"C:\\d\\b.zip"; items[2].received = 10; items[2].reason = L"Network error";
    items[3].state = State::Cancelled; items[3].path = L"C:\\d\\c.iso";
    std::stringstream io; WriteHistory(io, items);
    std::string text = io.str() + "Z\t1\tbad\turi\nC\tx\tC:\\bad\t\nC\t5\t\xff\xfe\t\n";
    std::istringstream in(text); int next = 7; auto read = ReadHistory(in, next);
    Expect(read.size() == 3, "history keeps finished rows only");
    Expect(read.size() == 3 && read[0].path == items[1].path && read[0].total == 1234 && read[0].uri == items[1].uri && read[0].id == 7, "history completed row");
    Expect(read.size() == 3 && read[1].state == State::Failed && read[1].reason == L"Network error" && read[2].state == State::Cancelled, "history states");
    Expect(next == 10, "history ids continue");
    std::istringstream bad("other 2\nC\t1\tC:\\x\t\n"); next = 1; Expect(ReadHistory(bad, next).empty(), "unknown version ignored");
    // Actions per state follow the Paper frame.
    Item item; item.state = State::InProgress; Expect(ActionsFor(item).first == Action::Pause && ActionsFor(item).second == Action::Cancel, "in-progress actions");
    item.state = State::Completed; Expect(ActionsFor(item).first == Action::ShowInFolder && ActionsFor(item).second == Action::Open, "completed actions");
    item.missing = true; Expect(ActionsFor(item).first == Action::None && ActionsFor(item).second == Action::Remove, "deleted file actions");
    // Reveal: staggered entrance, faster joint exit, continuous on interruption.
    RowReveal reveal; const auto t0 = Motion::Time{} + std::chrono::seconds(10);
    reveal.Set(true, t0, false);
    Expect(reveal.Layer(0, t0).opacity == 0 && reveal.Layer(0, t0).blur == Reveal::maxBlur && reveal.Layer(0, t0).offset == Reveal::maskTravel, "reveal starts hidden, blurred, offset");
    const auto mid = t0 + Motion::Milliseconds(60);
    Expect(reveal.Layer(0, mid).opacity > reveal.Layer(1, mid).opacity && reveal.Layer(1, mid).opacity > reveal.Layer(2, mid).opacity, "mask leads, actions follow");
    Expect(reveal.NextWake(mid).frames, "reveal requests frames");
    const auto settled = t0 + Reveal::enter + Reveal::stagger * 2;
    Expect(reveal.Layer(2, settled).opacity == 1 && reveal.Layer(2, settled).blur == 0 && reveal.Layer(2, settled).offset == 0, "reveal settles crisp");
    Expect(!reveal.NextWake(settled).delay, "settled reveal is idle");
    const auto before = reveal.Layer(1, mid).opacity; reveal.Set(false, mid, false);
    Expect(reveal.Layer(1, mid).opacity == before, "exit continues from current value");
    Expect(reveal.Layer(0, mid + Reveal::exit).opacity == 0 && reveal.Layer(2, mid + Reveal::exit).opacity == 0, "exit completes together");
    RowReveal reduced; reduced.Set(true, t0, true);
    Expect(reduced.Layer(2, t0 + Motion::Milliseconds(50)).offset == 0 && reduced.Layer(2, t0 + Motion::Milliseconds(50)).blur == 0, "reduced motion: fade only");
    // Geometry from the Paper frame.
    Expect(Panel::MaskSolid() == 186 && Panel::MaskStart() == 98, "mask spans the name before the actions");
    Expect(Panel::Close().left == 232 && Panel::Close().right == 256, "header close centred on the first line");
}

struct Fixture {
    std::wstring root, out;
    std::vector<Item> items;
    Fixture(std::wstring r, std::wstring o) : root(std::move(r)), out(std::move(o)) {
        Item a; a.id = 1; a.path = L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe"; a.state = State::InProgress; a.received = 52848230; a.total = 209715200;
        Item b = a; b.id = 2;
        Item c; c.id = 3; c.path = a.path; c.state = State::Completed; c.received = c.total = 209715200;
        Item d = c; d.id = 4;
        items = {a, b, c, d};
    }
    // reveals: per row id, progress time since hover began (ms); -1 = settled.
    bool Render(const wchar_t* name, UINT dpi, std::map<int, int> hovered, float entrance = 1, int focus = 0, std::vector<Item>* list = nullptr) {
        auto& rows = list ? *list : items;
        Chrome chrome; const float height = Panel::Height(static_cast<int>(rows.size()));
        if (FAILED(chrome.InitOffscreen(root, dpi, Panel::width, height))) { std::fprintf(stderr, "offscreen init failed\n"); return false; }
        DownloadsUI::View view(chrome);
        std::map<int, RowReveal> reveals; const auto now = Motion::Time{} + std::chrono::seconds(100);
        for (auto [id, ms] : hovered) { auto& r = reveals[id]; if (ms < 0) r.Snap(true); else r.Set(true, now - Motion::Milliseconds(ms), false); }
        DownloadsUI::Scene scene; scene.items = &rows; scene.reveals = &reveals; scene.now = now; scene.entrance = entrance; scene.focus = focus; scene.keyboard = focus != 0;
        scene.nowMs = 1000000; for (auto& item : rows) if (item.state == State::InProgress) item.endTime = scene.nowMs + 14000;
        auto* target = chrome.RenderTarget(); target->BeginDraw(); view.Paint(scene, height); HRESULT hr = target->EndDraw();
        view.Release();
        if (FAILED(hr)) { std::fprintf(stderr, "EndDraw %08lx\n", static_cast<unsigned long>(hr)); return false; }
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
};

int wmain(int argc, wchar_t** argv) {
    ModelChecks();
    if (argc >= 3) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
        Fixture f(argv[1], argv[2]);
        // Same scale as the Paper export (x2.875) for a side-by-side comparison.
        const UINT paper = 276;
        Expect(f.Render(L"panel-paper.png", paper, {{2, -1}, {4, -1}}), "render Paper states");
        Expect(f.Render(L"panel-96.png", 96, {{2, -1}, {4, -1}}), "render 100%");
        Expect(f.Render(L"panel-192.png", 192, {{2, -1}, {4, -1}}), "render 200%");
        for (int ms : {0, 40, 80, 120, 160, 220}) {
            wchar_t name[64]; std::swprintf(name, 64, L"reveal-%03d.png", ms);
            Expect(f.Render(name, paper, {{2, ms}}), "render reveal frame");
        }
        Expect(f.Render(L"entrance-050.png", paper, {}, .5f), "render entrance");
        Expect(f.Render(L"focus.png", paper, {{4, -1}}, 1, DownloadsUI::ActionHit(4, 0)), "render keyboard focus");
        std::vector<Item> states(4);
        states[0].id = 1; states[0].path = L"C:\\d\\video.mp4"; states[0].state = State::Paused; states[0].received = 5000000; states[0].total = 90000000;
        states[1].id = 2; states[1].path = L"C:\\d\\archive.zip"; states[1].state = State::Failed; states[1].reason = L"Network error"; states[1].canResume = true;
        states[2].id = 3; states[2].path = L"C:\\d\\report.pdf"; states[2].state = State::Cancelled;
        states[3].id = 4; states[3].path = L"C:\\d\\gone.txt"; states[3].state = State::Completed; states[3].missing = true;
        Expect(f.Render(L"states.png", paper, {{1, -1}, {2, -1}}, 1, 0, &states), "render other states");
        std::vector<Item> empty; Expect(f.Render(L"empty.png", paper, {}, 1, 0, &empty), "render empty");
        CoUninitialize();
    }
    if (failures) { std::fprintf(stderr, "%d download panel check(s) failed\n", failures); return 1; }
    std::printf("download panel checks passed\n");
    return 0;
}
