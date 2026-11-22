// Change snapshots: timing on a generated project (default 5 000 kept files + ignored folders), in %TEMP%.
//   snapshot_bench.exe [files] [--budget-ms N]
// Prints the measures; exit code 1 when the warm folder reading (median) is over the budget (default 300 ms).
#include "snapshots_test_util.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

using namespace ChangeSnapshots;
using namespace SnapTest;

namespace {

double Ms(const LARGE_INTEGER& from) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return double(now.QuadPart - from.QuadPart) * 1000.0 / double(freq.QuadPart);
}
LARGE_INTEGER Start() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t;
}

struct Series {
    std::vector<double> values;
    void Add(double v) { values.push_back(v); }
    double Min() const { return *std::min_element(values.begin(), values.end()); }
    double Max() const { return *std::max_element(values.begin(), values.end()); }
    double Median() const {
        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::size_t wanted = 5000;
    double budget = 300.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--budget-ms") == 0 && i + 1 < argc) budget = std::atof(argv[++i]);
        else wanted = std::size_t(std::atoll(argv[i]));
    }
    Sandbox box("bench");
    Rng rng(2026);

    // The project: src/mod-XX/part-Y/file-N.ext, 20 files per folder, 1 to 8 KB of text each.
    const char* extensions[] = {".tsx", ".ts", ".css", ".astro", ".json", ".md"};
    std::uint64_t keptBytes = 0;
    std::size_t kept = 0, folders = 0;
    for (std::size_t i = 0; kept < wanted; ++i) {
        const std::string dir = "src/mod-" + std::to_string(i / 5) + "/part-" + std::to_string(i % 5);
        ++folders;
        for (int f = 0; f < 20 && kept < wanted; ++f) {
            const std::string text = rng.Text(30 + rng.Next() % 200);
            WriteFileBytes(box.Path(dir + "/file-" + std::to_string(f) + extensions[rng.Next() % 6]), text);
            keptBytes += text.size();
            ++kept;
        }
        if (i % 25 == 0) WriteFileBytes(box.Path(dir + "/.gitignore"), "*.local\n/generated/\n!keep.local\n");
    }
    WriteFileBytes(box.Path(".gitignore"),
                   "# dependencies\nnode_modules/\n.pnp\n.pnp.js\n\n# testing\n/coverage\n\n# production\n/out/\n*.tsbuildinfo\n"
                   "\n# misc\n.DS_Store\n*.pem\n*.log\nnpm-debug.log*\n.env*.local\n/public/generated/**\n!important.log\n"
                   "**/tmp/\ndocs/**/*.draft.md\n");
    kept += 1 + (folders + 24) / 25;
    // What must be skipped quickly: dependencies, build output, ignored files.
    std::size_t ignored = 0;
    for (int p = 0; p < 150; ++p)
        for (int f = 0; f < 20; ++f, ++ignored)
            WriteFileBytes(box.Path("node_modules/pkg-" + std::to_string(p) + "/lib/f" + std::to_string(f) + ".js"), "module.exports = 1\n");
    for (int f = 0; f < 400; ++f, ++ignored) WriteFileBytes(box.Path("dist/assets/chunk-" + std::to_string(f) + ".js"), "x\n");
    for (int f = 0; f < 200; ++f, ++ignored) WriteFileBytes(box.Path("src/mod-" + std::to_string(f % 20) + "/debug-" + std::to_string(f) + ".log"), "log\n");
    for (int f = 0; f < 100; ++f, ++ignored) WriteFileBytes(box.Path("coverage/r" + std::to_string(f) + ".html"), "c\n");
    AgeTree(box.project);

    std::printf("project: %zu kept files in %zu folders, %.1f MB; %zu files in ignored folders / patterns\n", kept, folders,
                double(keptBytes) / 1048576.0, ignored);
    std::printf("machine: %u logical processors\n", std::thread::hardware_concurrency());

    ScanStats stats;
    TurnSummary summary;
    Usage usage;
    bool ok = true;
    const auto must = [&](const Result& r, const char* what) {
        if (r.ok()) return;
        ok = false;
        std::printf("!! %s: %s (%s)\n", what, StatusName(r.status), r.detail.c_str());
    };

    // 1. First snapshot: every kept file is read, hashed and copied.
    double firstBegin = 0, firstEnd = 0;
    {
        Store store(box.store);
        LARGE_INTEGER t = Start();
        must(store.BeginTurn(box.project, nullptr, &stats), "first BeginTurn");
        firstBegin = Ms(t);
        std::printf("first BeginTurn (copies everything): %8.1f ms   files=%zu read=%zu blobs=%zu (%.1f MB) notKept=%zu\n", firstBegin,
                    stats.files, stats.read, stats.blobsWritten, double(stats.bytesWritten) / 1048576.0, stats.notKept);
        if (stats.files != kept) {
            ok = false;
            std::printf("!! expected %zu kept files, recorded %zu\n", kept, stats.files);
        }
        t = Start();
        must(store.EndTurn(&summary, &stats), "first EndTurn");
        firstEnd = Ms(t);
        std::printf("first EndTurn (nothing changed):     %8.1f ms   scan=%.1f ms changes=%zu\n", firstEnd, stats.milliseconds,
                    summary.changes.size());
    }

    // 2. Warm: nothing changed. A new Store each time (index read from disk), then the same Store again.
    Series beginFresh, beginSame, scanOnly, endSame;
    for (int i = 0; i < 15; ++i) {
        Store store(box.store);
        LARGE_INTEGER t = Start();
        must(store.BeginTurn(box.project, nullptr, &stats), "warm BeginTurn");
        beginFresh.Add(Ms(t));
        scanOnly.Add(stats.milliseconds);
        if (stats.read != 0) {
            ok = false;
            std::printf("!! warm BeginTurn read %zu files (expected 0)\n", stats.read);
        }
        t = Start();
        must(store.EndTurn(&summary), "warm EndTurn");
        endSame.Add(Ms(t));
        t = Start();
        must(store.BeginTurn(box.project), "warm BeginTurn (same store)");
        beginSame.Add(Ms(t));
        must(store.EndTurn(), "warm EndTurn (same store)");
    }
    std::printf("warm BeginTurn, new Store:           %8.1f ms median (min %.1f, max %.1f)   <- the measure of the contract\n",
                beginFresh.Median(), beginFresh.Min(), beginFresh.Max());
    std::printf("  of which listing + comparing:      %8.1f ms median (min %.1f, max %.1f)\n", scanOnly.Median(), scanOnly.Min(),
                scanOnly.Max());
    std::printf("warm BeginTurn, same Store:          %8.1f ms median (min %.1f, max %.1f)\n", beginSame.Median(), beginSame.Min(),
                beginSame.Max());
    std::printf("warm EndTurn, nothing changed:       %8.1f ms median (min %.1f, max %.1f)   (reading + purge)\n", endSame.Median(),
                endSame.Min(), endSame.Max());

    // 3. A real turn: 10 files edited, 2 created, 1 deleted.
    {
        Store store(box.store);
        std::string turnId;
        must(store.BeginTurn(box.project, &turnId), "BeginTurn");
        std::size_t edited = 0;
        for (const auto& item : Tree(box.Path("src/mod-3/part-1"))) {
            if (edited >= 10) break;
            if (item.first == ".gitignore") continue;
            WriteFileBytes(box.Path("src/mod-3/part-1/" + item.first), item.second + "one more line\nand another\n");
            ++edited;
        }
        WriteFileBytes(box.Path("src/new/a.tsx"), "export const A = 1\n");
        WriteFileBytes(box.Path("src/new/b.css"), ".b { color: red }\n");
        std::string deleted;
        for (const auto& item : Tree(box.Path("src/mod-4/part-1"))) {
            if (item.first == ".gitignore") continue;
            deleted = "src/mod-4/part-1/" + item.first;
            break;
        }
        DeleteFileW(box.Path(deleted).c_str());
        LARGE_INTEGER t = Start();
        must(store.EndTurn(&summary, &stats), "EndTurn");
        const double end = Ms(t);
        std::printf("EndTurn, 13 files changed:           %8.1f ms   changes=%zu +%d -%d read=%zu\n", end, summary.changes.size(),
                    summary.additions, summary.deletions, stats.read);
        if (summary.changes.size() != 13) ok = false;

        FileDiff diff;
        t = Start();
        for (const auto& c : summary.changes) must(store.Diff(c.path, summary.checkpointId, &diff), "Diff");
        std::printf("Diff of the 13 files:                %8.1f ms\n", Ms(t));

        t = Start();
        const Result undone = store.UndoTurn(turnId);
        const double undo = Ms(t);
        must(undone, "UndoTurn");
        std::printf("UndoTurn, 13 files:                  %8.1f ms   restored=%zu\n", undo, undone.restored.size());

        t = Start();
        must(store.Purge(&usage), "Purge");
        std::printf("Purge (lists every content):         %8.1f ms   turns=%zu blobs=%zu (%.1f MB)\n", Ms(t), usage.turns, usage.blobs,
                    double(usage.blobBytes) / 1048576.0);
    }

    const double measured = beginFresh.Median();
    std::printf("RESULT warm_begin_ms_median=%.1f warm_begin_ms_max=%.1f budget_ms=%.0f first_begin_ms=%.1f files=%zu %s\n", measured,
                beginFresh.Max(), budget, firstBegin, kept, (ok && measured <= budget) ? "PASS" : "FAIL");
    return (ok && measured <= budget) ? 0 : 1;
}
