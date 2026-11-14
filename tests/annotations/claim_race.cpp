// Lease concurrency: N processes (and then N threads) claim the same 1000 annotations at the same time.
// Each annotation must have exactly one winner, both for fresh annotations and for expired leases taken over;
// losers' Resolve with a token they do not hold is always refused.
//
//   claim_race.exe                     parent: builds the store, runs every round, prints a summary
//   claim_race.exe child <root> <event> <now> <agent> <out>   one contender (spawned by the parent)
#include <atomic>
#include <map>
#include <set>
#include <thread>
#include <vector>

#include "annotations_test_util.h"

namespace BA = BrowserAnnotations;
using namespace AnnTest;

namespace {

constexpr int Count = 1000;
constexpr int Processes = 2;
constexpr std::int64_t T0 = 1794000000;

// The per-site cap is 500: the 1000 annotations live in two sites.
std::string ProjectOf(int i) { return i % 2 ? "race-b" : "race-a"; }

struct Item {
    std::string project, id;
};
std::vector<Item> Ids(const BA::Store& store) {
    BA::ListQuery q;  // every status, oldest first: all contenders walk the same order
    std::vector<Item> ids;
    for (const auto& e : store.List(q).entries) ids.push_back({e.annotation.project, e.annotation.id});
    return ids;
}

// All contenders meet before every Claim, so each annotation is really disputed at the same instant
// (without it, one contender runs ahead and the other only meets settled leases).
template <class Counter>
void Barrier(Counter& arrived, long target) {
    ++arrived;
    for (unsigned spin = 0; long(arrived) < target; ++spin) {
        if (spin > 2000) SwitchToThread();
        else YieldProcessor();
    }
}

struct SharedCounter {  // a LONG in a named shared section, used like an atomic
    volatile LONG* p;
    SharedCounter& operator++() {
        InterlockedIncrement(p);
        return *this;
    }
    explicit operator long() const { return InterlockedCompareExchange(p, 0, 0); }
};

int Child(const std::wstring& root, const std::wstring& eventName, std::int64_t now, const std::string& agent, const std::wstring& outPath) {
    BA::Store store(root, [now] { return now; });
    const std::vector<Item> ids = Ids(store);
    HANDLE start = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
    HANDLE section = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, (eventName + L"-barrier").c_str());
    if (!start || !section) return 3;
    SharedCounter arrived{static_cast<volatile LONG*>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG)))};
    if (!arrived.p) return 3;
    WaitForSingleObject(start, 30000);
    CloseHandle(start);
    std::string out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const Item& item = ids[i];
        const std::string& id = item.id;
        Barrier(arrived, long(Processes) * long(i + 1));
        BA::ClaimResult c;
        const BA::Outcome o = store.Claim(item.project, id, agent, "", c);
        if (o.ok()) {
            out += "W " + id + " " + c.token + "\n";
        } else {
            out += "L " + id + " " + BA::CodeName(o.code) + "\n";
            const BA::Outcome r = store.Resolve(item.project, id, BA::NewClaimToken(), "resolved", "pas à moi", agent);
            out += "R " + id + " " + BA::CodeName(r.code) + "\n";
        }
    }
    return WriteText(outPath, out) ? 0 : 4;
}

struct RoundResult {
    std::map<std::string, std::vector<std::string>> winners;  // id -> agents
    std::map<std::string, std::string> tokens;                // id -> winning token
    std::map<std::string, int> loserCodes, resolveCodes;
};

bool RunProcesses(const std::wstring& root, std::int64_t now, const std::string& label, RoundResult& result) {
    wchar_t self[4096];
    GetModuleFileNameW(nullptr, self, 4096);
    const std::wstring eventName = L"Local\\annotations-claim-race-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::wstring(label.begin(), label.end());
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, (eventName + L"-barrier").c_str());
    std::vector<PROCESS_INFORMATION> children;
    std::vector<std::wstring> outputs;
    for (int i = 0; i < Processes; ++i) {
        const std::wstring out = root + L"\\..\\" + std::wstring(label.begin(), label.end()) + L"-child" + std::to_wstring(i) + L".txt";
        DeleteFileW(out.c_str());
        outputs.push_back(out);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" child \"" + root + L"\" " + eventName + L" " + std::to_wstring(now) +
                           L" agent" + std::to_wstring(i) + L" \"" + out + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
        children.push_back(pi);
    }
    Sleep(300);  // let every child list the store and block on the event
    SetEvent(start);
    bool ok = true;
    for (auto& pi : children) {
        if (WaitForSingleObject(pi.hProcess, 300000) != WAIT_OBJECT_0) ok = false;
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        if (code != 0) ok = false;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(start);
    CloseHandle(section);
    for (int i = 0; i < Processes; ++i) {
        const std::string text = ReadText(outputs[i]);
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t end = text.find('\n', pos);
            const std::string line = text.substr(pos, end - pos);
            pos = end == std::string::npos ? text.size() : end + 1;
            if (line.size() < 33) continue;
            const std::string id = line.substr(2, 30), rest = line.substr(33);
            if (line[0] == 'W') {
                result.winners[id].push_back("agent" + std::to_string(i));
                result.tokens[id] = rest;
            } else if (line[0] == 'L') {
                ++result.loserCodes[rest];
            } else if (line[0] == 'R') {
                ++result.resolveCodes[rest];
            }
        }
    }
    return ok;
}

void CheckRound(const BA::Store& store, const std::vector<Item>& items, RoundResult& r, const char* label, std::int64_t now) {
    int exactlyOne = 0;
    std::map<std::string, int> perAgent;
    for (const auto& item : items) {
        if (r.winners[item.id].size() == 1) ++exactlyOne;
        for (const auto& agent : r.winners[item.id]) ++perAgent[agent];
    }
    std::printf("%s: %d/%d annotations with exactly one winner (wins:", label, exactlyOne, int(items.size()));
    for (const auto& kv : perAgent) std::printf(" %s=%d", kv.first.c_str(), kv.second);
    std::printf("); losers:");
    for (const auto& kv : r.loserCodes) std::printf(" %s=%d", kv.first.c_str(), kv.second);
    std::printf("; foreign-token resolves:");
    for (const auto& kv : r.resolveCodes) std::printf(" %s=%d", kv.first.c_str(), kv.second);
    std::printf("\n");
    CHECK(exactlyOne == int(items.size()));
    CHECK(perAgent.size() == std::size_t(Processes));  // the race was real: every process won some annotations
    CHECK(r.resolveCodes.size() == 1 && r.resolveCodes.count("wrong_token"));
    BA::Store view(store.Root(), [now] { return now; });
    int consistent = 0;
    for (const auto& item : items) {
        const std::string& id = item.id;
        BA::Entry e;
        if (r.winners[id].size() != 1 || !view.Read(item.project, id, e).ok()) continue;
        const std::string claim = ReadText(store.Root() + L"\\" + std::wstring(item.project.begin(), item.project.end()) + L"\\" +
                                           std::wstring(id.begin(), id.end()) + L"\\claim");
        if (e.state.status == BA::Status::Claimed && e.state.claimedBy && e.state.claimedBy->agent == r.winners[id][0] &&
            claim.find(r.tokens[id]) != std::string::npos)
            ++consistent;
    }
    CHECK(consistent == int(items.size()));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 7 && std::wstring(argv[1]) == L"child") {
        std::string agent;
        for (wchar_t c : std::wstring(argv[5])) agent += char(c);
        return Child(argv[2], argv[3], std::stoll(argv[4]), agent, argv[6]);
    }
    const std::wstring root = TempDir(L"claim-race") + L"\\annotations";
    std::int64_t now = T0 - 10000;
    BA::Store setup(root, [&now] { return now; });
    for (int i = 0; i < Count; ++i) {
        BA::Annotation a = Sample(ProjectOf(i), "course " + std::to_string(i));
        ++now;
        if (!setup.WriteAnnotation(a, nullptr, false).ok()) {
            std::printf("setup failed at %d\n", i);
            return 1;
        }
    }
    const std::vector<Item> ids = Ids(setup);
    CHECK(ids.size() == std::size_t(Count));

    // Round 1: fresh annotations, two processes.
    RoundResult fresh;
    CHECK(RunProcesses(root, T0, "fresh", fresh));
    CheckRound(setup, ids, fresh, "processes/fresh", T0);

    // Round 2: every lease expired; two processes race to take them over. The round-1 tokens are dead.
    const std::int64_t later = T0 + BA::LeaseSeconds + 60;
    RoundResult takeover;
    CHECK(RunProcesses(root, later, "takeover", takeover));
    CheckRound(setup, ids, takeover, "processes/expired-takeover", later);
    BA::Store atLater(root, [later] { return later; });
    int refused = 0;
    for (const auto& item : ids)
        if (atLater.Resolve(item.project, item.id, fresh.tokens[item.id], "resolved", "ancien jeton", "x").code == BA::Code::WrongToken) ++refused;
    std::printf("stale round-1 tokens refused by Resolve: %d/%d\n", refused, Count);
    CHECK(refused == Count);

    // Round 3: threads in one process, after the round-2 leases expired too.
    const std::int64_t latest = later + BA::LeaseSeconds + 60;
    BA::Store atLatest(root, [latest] { return latest; });
    std::vector<std::atomic<int>> wins(ids.size()), winner(ids.size());
    std::atomic<int> otherErrors{0};
    std::atomic<long> arrived{0};
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            WaitForSingleObject(go, INFINITE);
            for (std::size_t i = 0; i < ids.size(); ++i) {
                Barrier(arrived, 8L * long(i + 1));
                BA::ClaimResult c;
                const BA::Outcome o = atLatest.Claim(ids[i].project, ids[i].id, "thread" + std::to_string(t), "", c);
                if (o.ok()) {
                    ++wins[i];
                    winner[i] = t;
                } else if (o.code != BA::Code::Conflict) {
                    ++otherErrors;
                }
            }
        });
    }
    SetEvent(go);
    for (auto& th : threads) th.join();
    CloseHandle(go);
    int exactlyOne = 0;
    std::map<int, int> perThread;
    for (std::size_t i = 0; i < wins.size(); ++i) {
        if (wins[i].load() == 1) ++exactlyOne;
        ++perThread[winner[i].load()];
    }
    std::printf("threads/expired-takeover (8 threads): %d/%d annotations with exactly one winner (wins:", exactlyOne, Count);
    for (const auto& kv : perThread) std::printf(" t%d=%d", kv.first, kv.second);
    std::printf("), %d non-conflict errors\n", otherErrors.load());
    CHECK(exactlyOne == Count);
    CHECK(perThread.size() > 1);  // the race was real: more than one thread won annotations
    CHECK(otherErrors.load() == 0);
    return Finish("claim_race");
}
