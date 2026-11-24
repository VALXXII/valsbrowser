// Change snapshots (src/change-snapshots.*): the whole contract C on temporary folders only.
//   snapshot_test.exe            runs every section, prints one line per section, exit code = number of failures
// No git, no network, no real project, no real Claude / Codex configuration.
#include "snapshots_test_util.h"

using namespace ChangeSnapshots;
using namespace SnapTest;

namespace {

void Section(const char* name) {
    g_section = name;
    std::printf("- %s\n", name);
    std::fflush(stdout);
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        out.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return out;
}

// Applies a unified diff to `before` (lines without their ending). Returns false when the diff does not fit.
bool ApplyUnified(const std::vector<std::string>& before, const std::string& unified, std::vector<std::string>& after) {
    after.clear();
    std::size_t at = 0;  // next line of `before` not yet copied
    for (const std::string& line : Lines(unified)) {
        if (line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0 || line.rfind("\\ ", 0) == 0) continue;
        if (line.rfind("@@ ", 0) == 0) {
            unsigned a = 0, an = 0, b = 0, bn = 0;
            if (std::sscanf(line.c_str(), "@@ -%u,%u +%u,%u @@", &a, &an, &b, &bn) != 4) return false;
            const std::size_t start = an ? a - 1 : a;
            if (start < at || start > before.size()) return false;
            while (at < start) after.push_back(before[at++]);
            if (after.size() != (bn ? b - 1 : b)) return false;
            continue;
        }
        if (line.empty()) return false;
        const std::string body = line.substr(1);
        if (line[0] == ' ' || line[0] == '-') {
            if (at >= before.size() || before[at] != body) return false;
            if (line[0] == ' ') after.push_back(body);
            ++at;
        } else if (line[0] == '+') {
            after.push_back(body);
        } else {
            return false;
        }
    }
    while (at < before.size()) after.push_back(before[at++]);
    return true;
}

std::size_t Lcs(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::vector<std::size_t>> t(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
            t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
    return t[a.size()][b.size()];
}

std::string Join(const std::vector<std::string>& lines, const char* eol = "\n") {
    std::string out;
    for (const auto& l : lines) out += l + eol;
    return out;
}

std::string Utf16Le(const std::wstring& text) {
    std::string out = "\xFF\xFE";
    for (const wchar_t c : text) {
        out.push_back(char(c & 0xFF));
        out.push_back(char((c >> 8) & 0xFF));
    }
    return out;
}
std::string Utf16Be(const std::wstring& text) {
    std::string out = "\xFE\xFF";
    for (const wchar_t c : text) {
        out.push_back(char((c >> 8) & 0xFF));
        out.push_back(char(c & 0xFF));
    }
    return out;
}

bool ValidUtf8(const std::string& s) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0) > 0 || s.empty();
}

bool StoreHoldsText(const std::wstring& store, const std::string& needle) {
    for (const auto& item : Tree(store))
        if (item.second.find(needle) != std::string::npos) return true;
    return false;
}

std::wstring RealName(const std::wstring& path) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(Long(path).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    FindClose(h);
    return fd.cFileName;
}

bool MakeJunction(const std::wstring& link, const std::wstring& target) {
    const std::wstring command = L"cmd /c mklink /J \"" + link + L"\" \"" + target + L"\" >nul 2>&1";
    return _wsystem(command.c_str()) == 0 && PathExists(link);
}

std::vector<TurnSummary> TurnsOf(Store& store) {
    std::vector<TurnSummary> turns;
    CHECK(store.Turns(&turns).ok());
    return turns;
}

// ---------------------------------------------------------------- pure: .gitignore
void TestGitignorePatterns() {
    Section("gitignore patterns");
    struct Case {
        const char* rules;
        const char* path;
        bool dir;
        bool ignored;
    };
    const Case cases[] = {
        {"*.log\n", "a.log", false, true},
        {"*.log\n", "src/deep/A.LOG", false, true},
        {"*.log\n", "a.log.txt", false, false},
        {"build/\n", "build", true, true},
        {"build/\n", "build", false, false},
        {"build/\n", "src/build/x.o", false, true},
        {"/out\n", "out", true, true},
        {"/out\n", "src/out", true, false},
        {"docs/*.md\n", "docs/a.md", false, true},
        {"docs/*.md\n", "docs/sub/a.md", false, false},
        {"docs/*.md\n", "other/docs/a.md", false, false},
        {"**/tmp\n", "tmp", true, true},
        {"**/tmp\n", "a/b/tmp", true, true},
        {"a/**/z\n", "a/z", false, true},
        {"a/**/z\n", "a/b/c/z", false, true},
        {"a/**/z\n", "b/a/z", false, false},
        {"cache/**\n", "cache/x/y.bin", false, true},
        {"cache/**\n", "cache", true, false},
        {"*.txt\n!keep.txt\n", "keep.txt", false, false},
        {"*.txt\n!keep.txt\n", "drop.txt", false, true},
        {"!keep.txt\n*.txt\n", "keep.txt", false, true},
        {"# comment\n\n   \nfoo\n", "foo", false, true},
        {"# comment\n\n   \nfoo\n", "comment", false, false},
        {"\\#hash\n", "#hash", false, true},
        {"\\!bang\n", "!bang", false, true},
        {"file[0-9].txt\n", "file7.txt", false, true},
        {"file[0-9].txt\n", "filex.txt", false, false},
        {"file[!0-9].txt\n", "filex.txt", false, true},
        {"fo?.c\n", "foo.c", false, true},
        {"fo?.c\n", "fo.c", false, false},
        {"trail   \n", "trail", false, true},
        {"*.LOG\r\nnode\r\n", "x.log", false, true},
        {"dir\n", "dir/sub/file.txt", false, true},
        {"/dir/sub\n", "dir/sub/file.txt", false, true},
        {"/dir/sub\n", "x/dir/sub/file.txt", false, false},
        {"logs/*\n!logs/keep/\n", "logs/keep", true, false},
        {"logs/*\n!logs/keep/\n", "logs/other", true, true},
        {"*\n!*.md\n", "readme.md", false, false},
        {"*\n!*.md\n", "main.c", false, true},
        {"\xEF\xBB\xBF" "bom.txt\n", "bom.txt", false, true},
        {"a*b*c*d*e*f*g*h*i*j*k\n", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false, false},
        {"**/**/**/tmp\n", "tmp", true, true},          // SEC-27: repeated **/ is one
        {"**/**/**/tmp\n", "a/b/c/d/tmp", true, true},
        {"a/**/**/z\n", "a/z", false, true},
        {"x**/**/z\n", "x/q/z", false, true},         // "x**" is not a leading **: kept as written
    };
    for (const Case& c : cases) {
        const bool got = GitignoreIgnores(c.rules, c.path, c.dir);
        ++g_checks;
        if (got != c.ignored) {
            ++g_failures;
            std::printf("  FAIL gitignore: rules %s path %s dir=%d -> %d, expected %d\n", c.rules, c.path, c.dir, got, c.ignored);
        }
    }

    {   // SEC-27: 20 000 x "**/" (60 KB, once ~2 MB of stack) and 4 000 wildcard rules stay quick and never crash.
        std::string deep;
        for (int i = 0; i < 20000; ++i) deep += "**/";
        std::string path;
        for (int i = 0; i < 60; ++i) path += "d/";
        const ULONGLONG t0 = GetTickCount64();
        CHECK(GitignoreIgnores(deep + "x\n", path + "x", false));   // collapsed to "**/x"
        CHECK(!GitignoreIgnores(std::string(2000, 'a') + "*\n", std::string(2000, 'a'), false));   // over-long: not a rule
        CHECK(GitignoreIgnores("**/**/**/**/**/**/**/**/x\n", path + "x", false));
        std::string many;
        for (int i = 0; i < 4000; ++i) many += "a*a*a*a*a*a*a*a*a*b" + std::to_string(i) + "\n";
        const std::string name(200, 'a');
        for (int i = 0; i < 20; ++i) CHECK(!GitignoreIgnores(many, name, false));
        CHECK(GetTickCount64() - t0 < 5000);
    }
    CHECK(MayHoldSecrets(".env"));
    CHECK(MayHoldSecrets(".env.local"));
    CHECK(MayHoldSecrets(".ENV.Production"));
    CHECK(!MayHoldSecrets(".env.example"));
    CHECK(!MayHoldSecrets(".env.sample"));
    CHECK(MayHoldSecrets("server.pem"));
    CHECK(MayHoldSecrets("id_rsa"));
    CHECK(MayHoldSecrets(".npmrc"));
    CHECK(!MayHoldSecrets("environment.ts"));
    CHECK(!MayHoldSecrets("keys.ts"));
    CHECK(!MayHoldSecrets("index.html"));

    const std::string key = ProjectKey(L"C:\\Dev\\Mon Site (v2)");
    CHECK_EQ(key, ProjectKey(L"c:\\dev\\mon site (v2)"));
    CHECK(key.rfind("mon-site-v2-", 0) == 0);
    CHECK_EQ(key.size(), std::string("mon-site-v2-").size() + 16);
    CHECK(ProjectKey(L"C:\\Dev\\autre") != key);
}

// ---------------------------------------------------------------- pure: line diff
void TestDiffContents() {
    Section("line diff");
    {
        const std::string before = "a\nb\nc\n", after = "a\nB\nc\nd\n";
        const FileDiff d = DiffContents("f.txt", &before, &after);
        CHECK_EQ(d.additions, 2);
        CHECK_EQ(d.deletions, 1);
        CHECK(!d.binary);
        CHECK(d.kind == ChangeKind::Modified);
        CHECK_EQ(d.unified, std::string("--- a/f.txt\n+++ b/f.txt\n@@ -1,3 +1,4 @@\n a\n-b\n+B\n c\n+d\n"));
        CHECK_EQ(d.encodingBefore, std::string("utf-8"));
    }
    {
        const std::string before = "a\nb", after = "a\nb\n";
        const FileDiff d = DiffContents("f.txt", &before, &after);
        CHECK_EQ(d.additions, 1);
        CHECK_EQ(d.deletions, 1);
        CHECK_EQ(d.unified, std::string("--- a/f.txt\n+++ b/f.txt\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n"));
    }
    {
        const std::string after = "x\ny\n";
        const FileDiff created = DiffContents("n.txt", nullptr, &after);
        CHECK(created.kind == ChangeKind::Created);
        CHECK_EQ(created.additions, 2);
        CHECK_EQ(created.deletions, 0);
        CHECK_EQ(created.unified, std::string("--- /dev/null\n+++ b/n.txt\n@@ -0,0 +1,2 @@\n+x\n+y\n"));
        const FileDiff deleted = DiffContents("n.txt", &after, nullptr);
        CHECK(deleted.kind == ChangeKind::Deleted);
        CHECK_EQ(deleted.deletions, 2);
        CHECK_EQ(deleted.unified, std::string("--- a/n.txt\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-x\n-y\n"));
        const std::string empty;
        const FileDiff nothing = DiffContents("n.txt", nullptr, &empty);
        CHECK_EQ(nothing.additions, 0);
        CHECK(nothing.unified.empty());
    }
    {  // CRLF -> LF: every line counts, and it is flagged
        const std::string before = "one\r\ntwo\r\nthree\r\n", after = "one\ntwo\nthree\n";
        const FileDiff d = DiffContents("eol.txt", &before, &after);
        CHECK_EQ(d.additions, 3);
        CHECK_EQ(d.deletions, 3);
        CHECK(d.lineEndingsChanged);
        CHECK(d.unified.find('\r') == std::string::npos);
        const std::string edited = "one\r\nTWO\r\nthree\r\n";
        const FileDiff e = DiffContents("eol.txt", &before, &edited);
        CHECK_EQ(e.additions, 1);
        CHECK_EQ(e.deletions, 1);
        CHECK(!e.lineEndingsChanged);
        CHECK_EQ(e.unified, std::string("--- a/eol.txt\n+++ b/eol.txt\n@@ -1,3 +1,3 @@\n one\n-two\n+TWO\n three\n"));
    }
    {  // binary
        const std::string before = std::string("PNG\0\x01\x02", 6), after = std::string("PNG\0\x03\x04\x05", 7);
        const FileDiff d = DiffContents("i.png", &before, &after);
        CHECK(d.binary);
        CHECK_EQ(d.additions, 0);
        CHECK_EQ(d.deletions, 0);
        CHECK(d.unified.empty());
        CHECK_EQ(d.encodingAfter, std::string("binary"));
        const std::string text = "plain\n";
        CHECK(DiffContents("i.png", &text, &after).binary);
        CHECK(DiffContents("i.png", nullptr, &after).binary);
    }
    {  // encodings: what is shown is always valid UTF-8
        const std::string b16 = Utf16Le(L"caf\u00e9\r\nligne deux\r\n"), a16 = Utf16Le(L"caf\u00e9\r\nligne 2 \U0001F3A8\r\n");
        const FileDiff d16 = DiffContents("u16.txt", &b16, &a16);
        CHECK(!d16.binary);
        CHECK_EQ(d16.additions, 1);
        CHECK_EQ(d16.deletions, 1);
        CHECK_EQ(d16.encodingBefore, std::string("utf-16le"));
        CHECK(ValidUtf8(d16.unified));
        CHECK(d16.unified.find(" caf\xC3\xA9\n") != std::string::npos);
        CHECK(d16.unified.find("+ligne 2 \xF0\x9F\x8E\xA8\n") != std::string::npos);

        const std::string bbe = Utf16Be(L"un\ndeux\n"), abe = Utf16Be(L"un\ntrois\n");
        const FileDiff dbe = DiffContents("be.txt", &bbe, &abe);
        CHECK_EQ(dbe.encodingAfter, std::string("utf-16be"));
        CHECK(dbe.unified.find("-deux\n+trois\n") != std::string::npos);

        const std::string blatin = "caf\xE9 cr\xE8me\n", alatin = "caf\xE9 cr\xE8me \x80\n";
        const FileDiff dl = DiffContents("latin.txt", &blatin, &alatin);
        CHECK_EQ(dl.encodingBefore, std::string("windows-1252"));
        CHECK(ValidUtf8(dl.unified));
        CHECK(dl.unified.find("-caf\xC3\xA9 cr\xC3\xA8me\n") != std::string::npos);
        CHECK(dl.unified.find("+caf\xC3\xA9 cr\xC3\xA8me \xE2\x82\xAC\n") != std::string::npos);

        const std::string bbom = "\xEF\xBB\xBFtitre\ncorps\n", abom = "\xEF\xBB\xBFtitre!\ncorps\n";
        const FileDiff db = DiffContents("bom.txt", &bbom, &abom);
        CHECK_EQ(db.encodingBefore, std::string("utf-8-bom"));
        CHECK_EQ(db.additions, 1);
        CHECK(db.unified.find("\xEF\xBB\xBF") == std::string::npos);
        CHECK(db.unified.find("-titre\n+titre!\n") != std::string::npos);
        const std::string nobom = "titre\ncorps\n";  // adding a BOM is a change of the first line
        const FileDiff added = DiffContents("bom.txt", &nobom, &bbom);
        CHECK_EQ(added.additions, 1);
        CHECK_EQ(added.deletions, 1);
    }
    {  // hunks: far apart = two hunks, close = one
        std::vector<std::string> lines;
        for (int i = 1; i <= 30; ++i) lines.push_back("l" + std::to_string(i));
        auto apart = lines, together = lines;
        apart[1] = "X";
        apart[24] = "Y";
        together[9] = "X";
        together[13] = "Y";
        const std::string b = Join(lines), f = Join(apart), c = Join(together);
        const FileDiff dFar = DiffContents("h.txt", &b, &f), dClose = DiffContents("h.txt", &b, &c);
        const auto count = [](const std::string& s) {
            std::size_t n = 0;
            for (std::size_t p = s.find("\n@@ "); p != std::string::npos; p = s.find("\n@@ ", p + 1)) ++n;
            return n;
        };
        CHECK_EQ(count(dFar.unified), std::size_t(2));
        CHECK_EQ(count(dClose.unified), std::size_t(1));
        CHECK(dFar.unified.find("@@ -1,5 +1,5 @@\n l1\n-l2\n+X\n l3\n l4\n l5\n@@ -22,7 +22,7 @@\n") != std::string::npos);
        std::vector<std::string> rebuilt;
        CHECK(ApplyUnified(lines, dFar.unified, rebuilt) && rebuilt == apart);
        CHECK(ApplyUnified(lines, dClose.unified, rebuilt) && rebuilt == together);
    }
    {  // property: the diff always rebuilds the new file, and it is minimal on small inputs
        Rng rng(7);
        int bad = 0;
        for (int round = 0; round < 400; ++round) {
            std::vector<std::string> a, b;
            const std::size_t na = rng.Next() % 40, nb = rng.Next() % 40;
            for (std::size_t i = 0; i < na; ++i) a.push_back("v" + std::to_string(rng.Next() % 6));
            if (round % 3 == 0) {  // b = a with a few edits
                b = a;
                for (int e = 0; e < 4 && !b.empty(); ++e) {
                    const std::size_t at = rng.Next() % b.size();
                    if (rng.Next() % 2) b.erase(b.begin() + std::ptrdiff_t(at));
                    else b.insert(b.begin() + std::ptrdiff_t(at), "n" + std::to_string(rng.Next() % 6));
                }
            } else {
                for (std::size_t i = 0; i < nb; ++i) b.push_back("v" + std::to_string(rng.Next() % 6));
            }
            const std::string sa = Join(a), sb = Join(b);
            const FileDiff d = DiffContents("p.txt", &sa, &sb);
            std::vector<std::string> rebuilt;
            const bool ok = ApplyUnified(a, d.unified, rebuilt) && rebuilt == b &&
                            std::size_t(d.additions + d.deletions) == a.size() + b.size() - 2 * Lcs(a, b) &&
                            d.additions - d.deletions == int(b.size()) - int(a.size());
            if (!ok) ++bad;
        }
        CHECK_EQ(bad, 0);
    }
    {  // big files: a small edit stays exact; a full rewrite falls back to "all removed, all added"
        Rng rng(11);
        std::vector<std::string> big = Lines(rng.Text(30000));
        auto edited = big;
        edited[15000] = "changed";
        edited.insert(edited.begin() + 20000, "inserted");
        const std::string b = Join(big), e = Join(edited);
        const FileDiff small = DiffContents("big.txt", &b, &e);
        CHECK_EQ(small.additions, 2);
        CHECK_EQ(small.deletions, 1);
        std::vector<std::string> rebuilt;
        CHECK(ApplyUnified(big, small.unified, rebuilt) && rebuilt == edited);

        const std::string other = Rng(12).Text(6000);
        const std::string first = Rng(13).Text(6000);
        const FileDiff rewrite = DiffContents("big.txt", &first, &other, true, 1 << 26);
        CHECK(rewrite.additions <= 6000 && rewrite.additions >= 5000);
        CHECK(rewrite.deletions <= 6000 && rewrite.deletions >= 5000);
        CHECK(ApplyUnified(Lines(first), rewrite.unified, rebuilt) && rebuilt == Lines(other));

        const FileDiff cut = DiffContents("big.txt", &first, &other, true, 2000);
        CHECK(cut.truncated);
        CHECK(cut.unified.size() <= 2000);
        CHECK(!cut.unified.empty() && cut.unified.back() == '\n');
        CHECK_EQ(cut.additions, rewrite.additions);
    }
}

// ---------------------------------------------------------------- created / modified / deleted / renamed
void TestBasicTurn() {
    Section("created / modified / deleted / renamed");
    Sandbox box("basic");
    box.Put("index.html", "<h1>Bonjour</h1>\n<p>un</p>\n<p>deux</p>\n");
    box.Put("src/app.css", "body { margin: 0; }\n.a { color: red; }\n");
    box.Put("src/old/name.txt", "same content\nline two\n");
    box.Put("src/remove-me.js", "console.log(1)\nconsole.log(2)\nconsole.log(3)\n");
    box.Put("untouched.md", "# Titre\n");
    const auto original = box.Files();

    Store store(box.store);
    CHECK(!store.HasProject());
    ScanStats first;
    std::string turnId;
    CHECK(store.BeginTurn(box.project, &turnId, &first).ok());
    CHECK(store.HasProject() && store.TurnIsOpen());
    CHECK_EQ(first.files, std::size_t(5));
    CHECK_EQ(first.read, std::size_t(5));
    CHECK_EQ(first.blobsWritten, std::size_t(5));
    CHECK(turnId.rfind("trn_", 0) == 0 && turnId.size() == 30);

    box.Put("index.html", "<h1>Bonjour</h1>\n<p>UN</p>\n<p>deux</p>\n<p>trois</p>\n", false);
    box.Put("src/new/page.astro", "---\n---\n<main/>\n", false);
    box.Remove("src/remove-me.js");
    MakeDirs(box.Path("src/moved"));
    CHECK(MoveFileW(box.Path("src/old/name.txt").c_str(), box.Path("src/moved/name.txt").c_str()));
    RemoveDirectoryW(box.Path("src/old").c_str());

    TurnSummary summary;
    ScanStats second;
    CHECK(store.EndTurn(&summary, &second).ok());
    CHECK(!store.TurnIsOpen());
    CHECK_EQ(summary.turnId, turnId);
    CHECK(summary.checkpointId.rfind("chk_", 0) == 0);
    CHECK_EQ(summary.sequence, 1ull);
    CHECK(!summary.interrupted && !summary.undone);
    CHECK_EQ(summary.changes.size(), std::size_t(5));
    CHECK_EQ(second.read, std::size_t(3));  // index.html, page.astro, moved/name.txt: only what changed is read

    const FileChange* modified = FindChange(summary, "index.html");
    CHECK(modified && modified->kind == ChangeKind::Modified && modified->additions == 2 && modified->deletions == 1);
    CHECK(modified && modified->restorable && !modified->binary);
    const FileChange* created = FindChange(summary, "src/new/page.astro");
    CHECK(created && created->kind == ChangeKind::Created && created->additions == 3 && created->deletions == 0);
    const FileChange* deleted = FindChange(summary, "src/remove-me.js");
    CHECK(deleted && deleted->kind == ChangeKind::Deleted && deleted->additions == 0 && deleted->deletions == 3);
    const FileChange* from = FindChange(summary, "src/old/name.txt");
    const FileChange* to = FindChange(summary, "src/moved/name.txt");
    CHECK(from && from->kind == ChangeKind::Deleted && from->renamedTo == "src/moved/name.txt");
    CHECK(to && to->kind == ChangeKind::Created && to->renamedFrom == "src/old/name.txt");
    CHECK_EQ(summary.additions, 2 + 3 + 2);
    CHECK_EQ(summary.deletions, 1 + 3 + 2);
    CHECK(!FindChange(summary, "untouched.md"));
    CHECK(!FindChange(summary, "src/app.css"));

    // Diff of one file of that turn.
    FileDiff diff;
    CHECK(store.Diff("index.html", summary.checkpointId, &diff).ok());
    CHECK_EQ(diff.additions, 2);
    CHECK_EQ(diff.deletions, 1);
    CHECK_EQ(diff.unified, std::string("--- a/index.html\n+++ b/index.html\n@@ -1,3 +1,4 @@\n <h1>Bonjour</h1>\n-<p>un</p>\n+<p>UN</p>\n"
                                       " <p>deux</p>\n+<p>trois</p>\n"));
    CHECK(store.Diff("src/new/page.astro", summary.checkpointId, &diff).ok());
    CHECK(diff.kind == ChangeKind::Created && diff.unified.rfind("--- /dev/null\n", 0) == 0);
    CHECK(store.Diff("src/remove-me.js", summary.checkpointId, &diff).ok());
    CHECK(diff.kind == ChangeKind::Deleted && diff.unified.find("+++ /dev/null\n") != std::string::npos);
    CHECK_EQ(store.Diff("untouched.md", summary.checkpointId, &diff).status, Status::NotFound);
    CHECK_EQ(store.Diff("index.html", "chk_aaaaaaaaaaaaaaaaaaaaaaaaaa", &diff).status, Status::NotFound);

    // Undo: the folder is byte for byte what it was, the folders the turn created are gone.
    const Result undone = store.UndoTurn(turnId);
    CHECK_EQ(undone.status, Status::Ok);
    CHECK(undone.conflicts.empty() && undone.failed.empty());
    CHECK_EQ(undone.restored.size(), std::size_t(5));
    CHECK(Contains(undone.undoneTurns, turnId));
    CHECK(box.Files() == original);
    CHECK(!PathExists(box.Path("src/new")));
    CHECK(!PathExists(box.Path("src/moved")));
    CHECK(IsDir(box.Path("src/old")));
    CHECK_EQ(CountFiles(box.project, L".snaptmp"), std::size_t(0));
    CHECK(!PathExists(store.ProjectStoreDir() + L"\\restore.json"));

    const auto turns = TurnsOf(store);
    CHECK_EQ(turns.size(), std::size_t(1));
    CHECK(!turns.empty() && turns[0].undone && turns[0].changes.size() == 5);
    CHECK_EQ(store.UndoTurn(turnId).status, Status::AlreadyUndone);
    CHECK_EQ(store.Restore(summary.checkpointId).status, Status::AlreadyUndone);
    CHECK_EQ(store.UndoTurn("trn_aaaaaaaaaaaaaaaaaaaaaaaaaa").status, Status::NotFound);

    // A turn without changes still has a checkpoint.
    const TurnSummary empty = RunTurn(store, box, [] {});
    CHECK(empty.changes.empty() && !empty.checkpointId.empty());
    CHECK_EQ(empty.sequence, 2ull);
    CHECK_EQ(store.Restore(empty.checkpointId).status, Status::Ok);
    CHECK(box.Files() == original);
}

// ---------------------------------------------------------------- binary, line endings, encodings
void TestBytesAreExact() {
    Section("binary / CRLF / encodings restored byte for byte");
    Sandbox box("bytes");
    Rng rng(3);
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + rng.Bytes(4096);
    const std::string crlf = "un\r\ndeux\r\ntrois\r\n";
    const std::string mixed = "a\r\nb\nc\r\n\r\nd";
    const std::string u16 = Utf16Le(L"\u00e9t\u00e9\r\nhiver\r\n");
    const std::string latin = "d\xE9j\xE0 vu\r\n\xA9 2026\r\n";
    const std::string bom = "\xEF\xBB\xBF{\"a\": 1}\n";
    box.Put("img/logo.png", png);
    box.Put("crlf.txt", crlf);
    box.Put("mixed.txt", mixed);
    box.Put("u16.txt", u16);
    box.Put("latin.txt", latin);
    box.Put("bom.json", bom);
    box.Put("empty.txt", "");
    const auto original = box.Files();

    Store store(box.store);
    const TurnSummary t = RunTurn(store, box, [&] {
        box.Put("img/logo.png", std::string("\x89PNG\r\n\x1a\n", 8) + Rng(4).Bytes(5000), false);
        box.Put("crlf.txt", "un\ndeux\ntrois\n", false);                       // the agent normalised line endings
        box.Put("mixed.txt", "a\r\nB\nc\r\n\r\nd\n", false);
        box.Put("u16.txt", Utf16Le(L"\u00e9t\u00e9\r\nprintemps\r\n"), false);
        box.Put("latin.txt", "d\xC3\xA9j\xC3\xA0 vu\n\xC2\xA9 2026\n", false);  // re-encoded as UTF-8
        box.Put("bom.json", "{\"a\": 2}\n", false);                             // BOM dropped
        box.Put("empty.txt", "now with text\n", false);
        box.Put("img/new.ico", std::string("\0\0\1\0", 4) + Rng(5).Bytes(300), false);
    });
    CHECK_EQ(t.changes.size(), std::size_t(8));
    const FileChange* logo = FindChange(t, "img/logo.png");
    CHECK(logo && logo->binary && logo->kind == ChangeKind::Modified && logo->additions == 0 && logo->deletions == 0 && logo->restorable);
    const FileChange* ico = FindChange(t, "img/new.ico");
    CHECK(ico && ico->binary && ico->kind == ChangeKind::Created);
    const FileChange* eol = FindChange(t, "crlf.txt");
    CHECK(eol && !eol->binary && eol->additions == 3 && eol->deletions == 3);
    const FileChange* u = FindChange(t, "u16.txt");
    CHECK(u && !u->binary && u->additions == 1 && u->deletions == 1);
    const FileChange* e = FindChange(t, "empty.txt");
    CHECK(e && e->kind == ChangeKind::Modified && e->additions == 1 && e->deletions == 0);

    FileDiff diff;
    CHECK(store.Diff("img/logo.png", t.checkpointId, &diff).ok());
    CHECK(diff.binary && diff.unified.empty());
    CHECK(store.Diff("crlf.txt", t.checkpointId, &diff).ok());
    CHECK(diff.lineEndingsChanged && diff.additions == 3);
    CHECK(store.Diff("latin.txt", t.checkpointId, &diff).ok());
    CHECK_EQ(diff.encodingBefore, std::string("windows-1252"));
    CHECK_EQ(diff.encodingAfter, std::string("utf-8"));
    CHECK(ValidUtf8(diff.unified));
    CHECK(store.Diff("u16.txt", t.checkpointId, &diff).ok());
    CHECK(diff.unified.find("-hiver\n+printemps\n") != std::string::npos);

    CHECK_EQ(store.UndoTurn(t.turnId).status, Status::Ok);
    CHECK(box.Files() == original);
    CHECK(box.Get("crlf.txt") == crlf);
    CHECK(box.Get("mixed.txt") == mixed);
    CHECK(box.Get("u16.txt") == u16);
    CHECK(box.Get("latin.txt") == latin);
    CHECK(box.Get("bom.json") == bom);
    CHECK(box.Get("img/logo.png") == png);
    CHECK(box.Get("empty.txt") == std::string());
    CHECK(!box.Has("img/new.ico"));
}

// ---------------------------------------------------------------- what is left out
void TestExclusions() {
    Section("gitignore / default exclusions / big files / secrets / links");
    Sandbox box("exclusions");
    box.Put(".gitignore", "*.log\n/generated/\nsecret-notes.txt\n!important.log\n");
    box.Put("src/.gitignore", "*.tmp\n!keep.tmp\nlocal/\n");
    box.Put("app.js", "let a = 1\n");
    box.Put("debug.log", "ignored\n");
    box.Put("important.log", "kept\n");
    box.Put("generated/out.js", "ignored\n");
    box.Put("src/generated/out.js", "kept: /generated is anchored at the root\n");
    box.Put("src/cache.tmp", "ignored\n");
    box.Put("src/keep.tmp", "kept\n");
    box.Put("src/local/data.txt", "ignored\n");
    box.Put("src/deep/more.tmp", "ignored by the nested file\n");
    box.Put("other/x.tmp", "kept: the nested .gitignore does not reach here\n");
    box.Put("secret-notes.txt", "ignored\n");
    for (const char* dir : {"node_modules/pkg", ".git", "dist", "build", ".next/cache", ".astro", "sub/node_modules/p", "sub/Dist"})
        box.Put(std::string(dir) + "/file.txt", std::string("excluded ") + dir + "\n");
    box.Put(".git/config", "[core]\n\trepositoryformatversion = 0\n");
    box.Put(".env", "TOKEN=super-secret-value-123\n");
    box.Put(".env.example", "TOKEN=\n");
    box.Put("certs/server.pem", "-----BEGIN PRIVATE KEY-----\nMIIsecretkeymaterial\n");
    const std::string big(5 * 1024 * 1024 + 1, 'x');
    const std::string edge(5 * 1024 * 1024, 'y');
    box.Put("video.bin", big);
    box.Put("edge.bin", edge);
    // A junction to a folder outside the project.
    const std::wstring outside = box.root + L"\\outside";
    WriteFileBytes(outside + L"\\private.txt", "outside the project\n");
    const bool junction = MakeJunction(box.Path("linked"), outside);
    if (!junction) std::printf("    (junction not available here: link checks skipped)\n");

    Store store(box.store);
    ScanStats stats;
    CHECK(store.BeginTurn(box.project, nullptr, &stats).ok());
    // Kept: .gitignore, src/.gitignore, app.js, important.log, src/generated/out.js, src/keep.tmp, other/x.tmp,
    //       .env.example, edge.bin
    CHECK_EQ(stats.files, std::size_t(9));
    CHECK_EQ(stats.notKept, std::size_t(3));  // .env, certs/server.pem, video.bin
    if (junction) CHECK_EQ(stats.skippedLinks, std::size_t(1));
    CHECK(!StoreHoldsText(box.store, "super-secret-value-123"));
    CHECK(!StoreHoldsText(box.store, "MIIsecretkeymaterial"));
    CHECK(!StoreHoldsText(box.store, "outside the project"));
    CHECK(!StoreHoldsText(box.store, "excluded "));
    CHECK(!StoreHoldsText(box.store, "repositoryformatversion"));
    CHECK(!StoreHoldsText(box.store, "ignored"));

    const std::string gitConfig = *box.Get(".git/config");
    // The agent touches everything.
    box.Put("app.js", "let a = 2\n", false);
    box.Put("debug.log", "ignored 2\n", false);
    box.Put("generated/out.js", "ignored 2\n", false);
    box.Put("src/cache.tmp", "ignored 2\n", false);
    box.Put("src/local/data.txt", "ignored 2\n", false);
    box.Put("src/deep/more.tmp", "ignored 2\n", false);
    box.Put("secret-notes.txt", "ignored 2\n", false);
    box.Put("node_modules/pkg/file.txt", "changed\n", false);
    box.Put("dist/file.txt", "changed\n", false);
    box.Put("build/new.o", "changed\n", false);
    box.Put(".next/cache/file.txt", "changed\n", false);
    box.Put(".astro/file.txt", "changed\n", false);
    box.Put("sub/Dist/file.txt", "changed\n", false);
    box.Put(".env", "TOKEN=another-secret-456\n", false);
    box.Put("video.bin", big + "more", false);
    box.Put("huge-new.bin", std::string(6 * 1024 * 1024, 'z'), false);
    box.Put("src/keep.tmp", "kept 2\n", false);
    if (junction) WriteFileBytes(box.Path("linked") + L"\\private.txt", "written through the link\n");

    TurnSummary t;
    CHECK(store.EndTurn(&t).ok());
    std::string listed;
    for (const auto& c : t.changes) listed += c.path + " ";
    CHECK_EQ(listed, std::string(".env app.js huge-new.bin src/keep.tmp video.bin "));
    const FileChange* env = FindChange(t, ".env");
    CHECK(env && !env->restorable && env->notKept == NotKept::Secret && env->kind == ChangeKind::Modified);
    const FileChange* video = FindChange(t, "video.bin");
    CHECK(video && !video->restorable && video->notKept == NotKept::TooLarge);
    const FileChange* huge = FindChange(t, "huge-new.bin");
    CHECK(huge && huge->kind == ChangeKind::Created && huge->restorable && huge->notKept == NotKept::TooLarge);
    CHECK(!StoreHoldsText(box.store, "another-secret-456"));
    FileDiff diff;
    CHECK(store.Diff(".env", t.checkpointId, &diff).ok());
    CHECK(diff.unified.empty() && !diff.restorable && diff.notKept == NotKept::Secret);

    // Undo: kept files go back; what was not kept stays as the agent left it; the big file the turn CREATED goes.
    const Result undone = store.UndoTurn(t.turnId);
    CHECK_EQ(undone.status, Status::Ok);
    CHECK(box.Get("app.js") == std::string("let a = 1\n"));
    CHECK(box.Get("src/keep.tmp") == std::string("kept\n"));
    CHECK(!box.Has("huge-new.bin"));
    CHECK(box.Get(".env") == std::string("TOKEN=another-secret-456\n"));
    CHECK(box.Get("video.bin") == big + "more");
    CHECK(Contains(undone.skipped, ".env") && Contains(undone.skipped, "video.bin"));
    CHECK(box.Get("debug.log") == std::string("ignored 2\n"));
    CHECK(box.Get("node_modules/pkg/file.txt") == std::string("changed\n"));
    CHECK(box.Get(".git/config") == gitConfig);
    if (junction) {
        CHECK(ReadFileBytes(outside + L"\\private.txt") == std::string("written through the link\n"));
        RemoveDirectoryW(box.Path("linked").c_str());
    }

    // A folder the turn created, later swapped by the user for a link: undo never writes or deletes through it.
    const TurnSummary made = RunTurn(store, box, [&] { box.Put("made/x.txt", "agent\n", false); });
    CHECK(FindChange(made, "made/x.txt"));
    if (junction) {
        box.Remove("made/x.txt");
        RemoveDirectoryW(box.Path("made").c_str());
        const std::wstring elsewhere = box.root + L"\\elsewhere";
        WriteFileBytes(elsewhere + L"\\x.txt", "agent\n");
        CHECK(MakeJunction(box.Path("made"), elsewhere));
        const Result refused = store.UndoTurn(made.turnId);
        CHECK_EQ(refused.status, Status::Conflict);
        CHECK(HasConflict(refused, "made/x.txt", ConflictReason::Blocked));
        const Result forced = store.UndoTurn(made.turnId, OnConflict::Overwrite);
        CHECK_EQ(forced.status, Status::Ok);
        CHECK(Contains(forced.skipped, "made/x.txt"));
        CHECK(ReadFileBytes(elsewhere + L"\\x.txt") == std::string("agent\n"));
        CHECK(PathExists(box.Path("made")));
        RemoveDirectoryW(box.Path("made").c_str());
    }
}

// ---------------------------------------------------------------- conflicts
void TestConflicts() {
    Section("conflicts: named, nothing overwritten");
    Sandbox box("conflicts");
    box.Put("a.txt", "a0\n");
    box.Put("b.txt", "b0\n");
    box.Put("c.txt", "c0\n");
    box.Put("gone.txt", "g0\n");
    box.Put("ro.txt", "r0\n");
    Store store(box.store);
    const TurnSummary t = RunTurn(store, box, [&] {
        box.Put("a.txt", "a1\n", false);
        box.Put("b.txt", "b1\n", false);
        box.Put("c.txt", "c1\n", false);
        box.Put("created.txt", "new\n", false);
        box.Remove("gone.txt");
        box.Put("ro.txt", "r1\n", false);
    });
    CHECK_EQ(t.changes.size(), std::size_t(6));

    // The user: edits a.txt, deletes b.txt, re-creates gone.txt, puts c.txt back by hand. created.txt and ro.txt
    // are untouched.
    box.Put("a.txt", "a1 + my edit\n", false);
    box.Remove("b.txt");
    box.Put("gone.txt", "mine\n", false);
    box.Put("c.txt", "c0\n", false);
    const auto beforeUndo = box.Files();

    const Result refused = store.UndoTurn(t.turnId);
    CHECK_EQ(refused.status, Status::Conflict);
    CHECK_EQ(refused.conflicts.size(), std::size_t(3));
    CHECK(HasConflict(refused, "a.txt", ConflictReason::ModifiedSinceTurn));
    CHECK(HasConflict(refused, "b.txt", ConflictReason::DeletedSinceTurn));
    CHECK(HasConflict(refused, "gone.txt", ConflictReason::CreatedSinceTurn));
    CHECK(refused.restored.empty() && refused.undoneTurns.empty());
    CHECK(box.Files() == beforeUndo);  // nothing touched, not even the files without conflict
    CHECK(!TurnsOf(store)[0].undone);
    CHECK(!PathExists(store.ProjectStoreDir() + L"\\restore.json"));

    // Restore to the previous state by checkpoint is refused the same way (same turn to undo).
    const TurnSummary noop = RunTurn(store, box, [] {});
    CHECK_EQ(store.Restore(noop.checkpointId).status, Status::Ok);  // nothing after it
    CHECK(box.Files() == beforeUndo);

    // KeepMine: the user's three files stay; the rest goes back.
    const Result kept = store.UndoTurn(t.turnId, OnConflict::KeepMine);
    CHECK_EQ(kept.status, Status::Ok);
    CHECK_EQ(kept.conflicts.size(), std::size_t(3));
    CHECK(Contains(kept.skipped, "a.txt") && Contains(kept.skipped, "b.txt") && Contains(kept.skipped, "gone.txt"));
    CHECK(Contains(kept.restored, "created.txt") && Contains(kept.restored, "ro.txt"));
    CHECK(!Contains(kept.restored, "c.txt"));  // already as wanted: left alone, no conflict
    CHECK(box.Get("a.txt") == std::string("a1 + my edit\n"));
    CHECK(!box.Has("b.txt"));
    CHECK(box.Get("gone.txt") == std::string("mine\n"));
    CHECK(box.Get("c.txt") == std::string("c0\n"));
    CHECK(!box.Has("created.txt"));
    CHECK(box.Get("ro.txt") == std::string("r0\n"));
    CHECK(TurnsOf(store)[0].undone);

    // Overwrite: the old content comes back even over the user's changes.
    const TurnSummary t2 = RunTurn(store, box, [&] {
        box.Put("a.txt", "a2\n", false);
        box.Put("c.txt", "c2\n", false);
        box.Remove("ro.txt");
    });
    box.Put("a.txt", "a2 + my edit\n", false);
    box.Put("ro.txt", "recreated by me\n", false);
    CHECK_EQ(store.UndoTurn(t2.turnId).status, Status::Conflict);
    const Result forced = store.UndoTurn(t2.turnId, OnConflict::Overwrite);
    CHECK_EQ(forced.status, Status::Ok);
    CHECK(forced.conflicts.empty());
    CHECK(box.Get("a.txt") == std::string("a1 + my edit\n"));
    CHECK(box.Get("c.txt") == std::string("c0\n"));
    CHECK(box.Get("ro.txt") == std::string("r0\n"));

    // A file held open by another program, and a read-only file: named, nothing touched.
    const TurnSummary t3 = RunTurn(store, box, [&] {
        box.Put("a.txt", "a3\n", false);
        box.Put("c.txt", "c3\n", false);
    });
    const HANDLE held = CreateFileW(box.Path("a.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    SetFileAttributesW(box.Path("c.txt").c_str(), FILE_ATTRIBUTE_READONLY);
    const Result inUse = store.UndoTurn(t3.turnId);
    CHECK_EQ(inUse.status, Status::Conflict);
    CHECK(HasConflict(inUse, "a.txt", ConflictReason::InUse));
    CHECK(HasConflict(inUse, "c.txt", ConflictReason::InUse));
    CHECK(box.Get("a.txt") == std::string("a3\n") && box.Get("c.txt") == std::string("c3\n"));
    // Overwrite still cannot replace them: reported as failed, the turn is undone, nothing is half-written.
    const Result partial = store.UndoTurn(t3.turnId, OnConflict::Overwrite);
    CHECK_EQ(partial.status, Status::Partial);
    CHECK(Contains(partial.failed, "a.txt") && Contains(partial.failed, "c.txt"));
    CHECK(box.Get("a.txt") == std::string("a3\n") && box.Get("c.txt") == std::string("c3\n"));
    CHECK_EQ(CountFiles(box.project, L".snaptmp"), std::size_t(0));
    CloseHandle(held);
    SetFileAttributesW(box.Path("c.txt").c_str(), FILE_ATTRIBUTE_NORMAL);

    // The kept content itself is gone: named, never replaced by something else.
    const TurnSummary t4 = RunTurn(store, box, [&] { box.Put("gone.txt", "agent\n", false); });
    RemoveTree(store.ProjectStoreDir() + L"\\blobs");
    const Result lost = store.UndoTurn(t4.turnId);
    CHECK_EQ(lost.status, Status::Conflict);
    CHECK(HasConflict(lost, "gone.txt", ConflictReason::SnapshotLost));
    const Result lostForced = store.UndoTurn(t4.turnId, OnConflict::Overwrite);
    CHECK(Contains(lostForced.skipped, "gone.txt"));
    CHECK(box.Get("gone.txt") == std::string("agent\n"));
}

// ---------------------------------------------------------------- several turns
void TestMultiTurnRestore() {
    Section("restore across several turns");
    Sandbox box("multi");
    box.Put("base.txt", "B0\n");
    box.Put("note.txt", "U0\n");
    box.Put("x.txt", "X0\n");
    Store store(box.store);
    const TurnSummary t1 = RunTurn(store, box, [&] {
        box.Put("a.txt", "A1\n", false);
        box.Put("base.txt", "B1\n", false);
    });
    box.Put("note.txt", "U1 (mine, between turns)\n", false);
    const TurnSummary t2 = RunTurn(store, box, [&] {
        box.Put("a.txt", "A2\n", false);
        box.Put("deep/dir/c.txt", "C2\n", false);
    });
    CHECK(!FindChange(t2, "note.txt"));  // the user's edit between turns belongs to no turn
    const TurnSummary t3 = RunTurn(store, box, [&] {
        box.Put("base.txt", "B3\n", false);
        box.Remove("a.txt");
        box.Put("deep/dir/d.txt", "D3\n", false);
    });
    CHECK_EQ(t3.sequence, 3ull);

    // Back to how turn 1 left the folder: turns 2 and 3 are undone, the user's own file is untouched.
    const Result r = store.Restore(t1.checkpointId);
    CHECK_EQ(r.status, Status::Ok);
    CHECK_EQ(r.undoneTurns.size(), std::size_t(2));
    CHECK(box.Get("a.txt") == std::string("A1\n"));
    CHECK(box.Get("base.txt") == std::string("B1\n"));
    CHECK(!box.Has("deep/dir/c.txt") && !box.Has("deep/dir/d.txt") && !PathExists(box.Path("deep")));
    CHECK(box.Get("note.txt") == std::string("U1 (mine, between turns)\n"));
    auto turns = TurnsOf(store);
    CHECK(turns.size() == 3 && !turns[0].undone && turns[1].undone && turns[2].undone);
    CHECK_EQ(store.Restore(t1.checkpointId).status, Status::Ok);  // already there
    CHECK_EQ(store.UndoTurn(t2.turnId).status, Status::AlreadyUndone);
    CHECK_EQ(store.Restore(t3.checkpointId).status, Status::AlreadyUndone);

    // New turns after an undo; then all the way back before turn 1.
    const TurnSummary t4 = RunTurn(store, box, [&] { box.Put("a.txt", "A4\n", false); });
    const TurnSummary t5 = RunTurn(store, box, [&] { box.Put("base.txt", "B5\n", false); });
    CHECK_EQ(t5.sequence, 5ull);
    const Result all = store.UndoTurn(t1.turnId);
    CHECK_EQ(all.status, Status::Ok);
    CHECK_EQ(all.undoneTurns.size(), std::size_t(3));  // 1, 4, 5 (2 and 3 were undone before)
    CHECK(!box.Has("a.txt"));
    CHECK(box.Get("base.txt") == std::string("B0\n"));
    CHECK(box.Get("note.txt") == std::string("U1 (mine, between turns)\n"));
    (void)t4;

    // Edited between two turns, then changed again by the later turn: undoing both would lose that edit.
    const TurnSummary e1 = RunTurn(store, box, [&] { box.Put("x.txt", "X1\n", false); });
    box.Put("x.txt", "X-mine\n", false);
    const TurnSummary e2 = RunTurn(store, box, [&] {
        box.Put("x.txt", "X2\n", false);
        box.Put("y.txt", "Y2\n", false);
    });
    const Result between = store.UndoTurn(e1.turnId);
    CHECK_EQ(between.status, Status::Conflict);
    CHECK(HasConflict(between, "x.txt", ConflictReason::EditedBetweenTurns));
    CHECK(box.Get("x.txt") == std::string("X2\n") && box.Has("y.txt"));
    // Undoing only the last turn is clean: x.txt goes back to the user's own version.
    CHECK_EQ(store.UndoTurn(e2.turnId).status, Status::Ok);
    CHECK(box.Get("x.txt") == std::string("X-mine\n") && !box.Has("y.txt"));
    // And now turn e1 alone: the user's edit after it is a plain conflict.
    const Result after = store.UndoTurn(e1.turnId);
    CHECK_EQ(after.status, Status::Conflict);
    CHECK(HasConflict(after, "x.txt", ConflictReason::ModifiedSinceTurn));
    CHECK_EQ(store.UndoTurn(e1.turnId, OnConflict::Overwrite).status, Status::Ok);
    CHECK(box.Get("x.txt") == std::string("X0\n"));
}

// ---------------------------------------------------------------- shapes: folders, case, unicode, long paths
void TestShapes() {
    Section("file <-> folder, case-only rename, unicode and long paths");
    Sandbox box("shapes");
    box.Put("thing", "a file named thing\n");
    box.Put("dir/a.txt", "inside dir\n");
    box.Put("Readme.md", "# readme\n");
    const std::string accents = "dossier \xC3\xA9t\xC3\xA9/\xE9\xA1\xB5\xE9\x9D\xA2/\xF0\x9F\x8E\xA8 fichier.txt";  // été / 页面 / 🎨
    box.Put(accents, "unicode\n");
    std::string longRel;
    for (int i = 0; i < 18; ++i) longRel += "segment-" + std::to_string(i) + "-0123456789abcdef/";
    longRel += "deep-file.txt";
    CHECK(longRel.size() > 400);
    box.Put(longRel, "very deep\n");
    const auto original = box.Files();
    CHECK_EQ(original.size(), std::size_t(5));

    Store store(box.store);
    const TurnSummary t = RunTurn(store, box, [&] {
        box.Remove("thing");  // file -> folder
        box.Put("thing/inner.txt", "now a folder\n", false);
        box.Remove("dir/a.txt");  // folder -> file
        RemoveDirectoryW(box.Path("dir").c_str());
        box.Put("dir", "now a file\n", false);
        CHECK(MoveFileW(box.Path("Readme.md").c_str(), box.Path("README.md").c_str()));  // case only
        box.Put(accents, "unicode 2\n", false);
        box.Put(longRel, "very deep 2\n", false);
    });
    CHECK(FindChange(t, "thing") && FindChange(t, "thing")->kind == ChangeKind::Deleted);
    CHECK(FindChange(t, "thing/inner.txt") && FindChange(t, "dir") && FindChange(t, "dir/a.txt"));
    const FileChange* upper = FindChange(t, "README.md");
    CHECK(upper && upper->kind == ChangeKind::Created && upper->renamedFrom == "Readme.md");
    CHECK(FindChange(t, accents) && FindChange(t, longRel));
    FileDiff diff;
    CHECK(store.Diff(accents, t.checkpointId, &diff).ok() && diff.additions == 1);

    const Result undone = store.UndoTurn(t.turnId);
    CHECK_EQ(undone.status, Status::Ok);
    CHECK(undone.failed.empty() && undone.conflicts.empty());
    CHECK(box.Files() == original);
    CHECK(!IsDir(box.Path("thing")));
    CHECK(IsDir(box.Path("dir")));
    CHECK(RealName(box.Path("readme.md")) == L"Readme.md");
}

// ---------------------------------------------------------------- hot scan, deduplication, racy dates
void TestScanReuse() {
    Section("hot scan reads nothing; deduplication; same-size same-date edit");
    Sandbox box("reuse");
    Rng rng(21);
    for (int i = 0; i < 200; ++i) box.Put("src/f" + std::to_string(i) + ".txt", rng.Text(20));
    for (int i = 0; i < 50; ++i) box.Put("copies/c" + std::to_string(i) + ".txt", "exactly the same content\n");
    Store store(box.store);
    ScanStats cold, hot;
    CHECK(store.BeginTurn(box.project, nullptr, &cold).ok());
    CHECK_EQ(cold.files, std::size_t(250));
    CHECK_EQ(cold.read, std::size_t(250));
    CHECK_EQ(cold.blobsWritten, std::size_t(201));  // 200 different + 1 shared by the 50 copies
    CHECK(store.EndTurn().ok());
    Usage usage;
    CHECK(store.GetUsage(&usage).ok());
    CHECK_EQ(usage.blobs, std::size_t(201));
    CHECK(store.BeginTurn(box.project, nullptr, &hot).ok());
    CHECK_EQ(hot.files, std::size_t(250));
    CHECK_EQ(hot.read, std::size_t(0));
    CHECK_EQ(hot.blobsWritten, std::size_t(0));

    // Same size, same date, different content, written right after the folder was recorded: still seen.
    box.Put("fresh.txt", "AAAA\n", false);
    TurnSummary t;
    CHECK(store.EndTurn(&t).ok());
    CHECK(FindChange(t, "fresh.txt"));
    AgeFile(box.Path("fresh.txt"), 0);  // dated "now": the case a date check alone cannot see
    WIN32_FILE_ATTRIBUTE_DATA data;
    CHECK(GetFileAttributesExW(box.Path("fresh.txt").c_str(), GetFileExInfoStandard, &data));
    CHECK(store.BeginTurn(box.project).ok());
    WriteFileBytes(box.Path("fresh.txt"), "BBBB\n");
    const HANDLE h = CreateFileW(box.Path("fresh.txt").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    SetFileTime(h, nullptr, nullptr, &data.ftLastWriteTime);
    CloseHandle(h);
    CHECK(store.EndTurn(&t).ok());
    const FileChange* fresh = FindChange(t, "fresh.txt");
    CHECK(fresh && fresh->kind == ChangeKind::Modified && fresh->additions == 1 && fresh->deletions == 1);
    CHECK_EQ(store.UndoTurn(t.turnId).status, Status::Ok);
    CHECK(box.Get("fresh.txt") == std::string("AAAA\n"));
}

// ---------------------------------------------------------------- purge
void TestPurge() {
    Section("purge: last N turns, size cap");
    {
        Sandbox box("purge-count");
        box.Put("f.txt", "v0\n");
        Options options;
        options.maxTurns = 3;
        Store store(box.store, options);
        std::vector<TurnSummary> made;
        for (int i = 1; i <= 5; ++i)
            made.push_back(RunTurn(store, box, [&] { box.Put("f.txt", "v" + std::to_string(i) + "\n", false); }));
        auto turns = TurnsOf(store);
        CHECK_EQ(turns.size(), std::size_t(3));
        CHECK(turns.size() == 3 && turns[0].turnId == made[2].turnId && turns[2].turnId == made[4].turnId);
        Usage usage;
        CHECK(store.GetUsage(&usage).ok());
        CHECK_EQ(usage.blobs, std::size_t(4));  // v2 v3 v4 v5: v0 and v1 are referred to by nothing any more
        CHECK_EQ(store.UndoTurn(made[0].turnId).status, Status::NotFound);
        CHECK_EQ(store.Restore(made[1].checkpointId).status, Status::NotFound);
        CHECK_EQ(store.Restore(made[2].checkpointId).status, Status::Ok);
        CHECK(box.Get("f.txt") == std::string("v3\n"));

        // Turns without changes do not push real ones out.
        store.SetLimits(2, DefaultMaxStoreBytes);
        RunTurn(store, box, [&] { box.Put("f.txt", "w1\n", false); });
        RunTurn(store, box, [] {});
        RunTurn(store, box, [] {});
        const TurnSummary w2 = RunTurn(store, box, [&] { box.Put("f.txt", "w2\n", false); });
        RunTurn(store, box, [] {});
        turns = TurnsOf(store);
        CHECK_EQ(turns.size(), std::size_t(5));  // w1, empty, empty, w2, empty
        const TurnSummary w3 = RunTurn(store, box, [&] { box.Put("f.txt", "w3\n", false); });
        // w1 is gone (2 turns with changes are kept: w2, w3); the empty turns after it stay, and their
        // checkpoint ("as w1 left the folder") still works.
        turns = TurnsOf(store);
        CHECK_EQ(turns.size(), std::size_t(5));  // empty, empty, w2, empty, w3
        CHECK(turns.size() == 5 && turns[0].changes.empty() && turns[2].turnId == w2.turnId && turns[4].turnId == w3.turnId);
        CHECK_EQ(store.Restore(turns.empty() ? "" : turns[0].checkpointId).status, Status::Ok);
        CHECK(box.Get("f.txt") == std::string("w1\n"));

        // Forget: every copy of the project goes; the project itself is untouched.
        const auto files = box.Files();
        CHECK(store.Forget().ok());
        CHECK(TurnsOf(store).empty());
        CHECK(store.GetUsage(&usage).ok() && usage.blobs == 0);
        CHECK(box.Files() == files);
        const TurnSummary again = RunTurn(store, box, [&] { box.Put("f.txt", "after forget\n", false); });
        CHECK_EQ(again.sequence, 1ull);
        CHECK_EQ(store.UndoTurn(again.turnId).status, Status::Ok);
        CHECK(box.Get("f.txt") == std::string("w1\n"));
    }
    {
        Sandbox box("purge-size");
        Rng rng(31);
        box.Put("blob.bin", rng.Bytes(100 * 1024));
        Options options;
        options.maxStoreBytes = 350 * 1024;
        Store store(box.store, options);
        std::vector<TurnSummary> made;
        std::vector<std::string> versions;
        for (int i = 0; i < 6; ++i) {
            versions.push_back(rng.Bytes(100 * 1024));
            made.push_back(RunTurn(store, box, [&] { box.Put("blob.bin", versions.back(), false); }));
            Usage usage;
            CHECK(store.GetUsage(&usage).ok());
            CHECK(usage.blobBytes <= 350 * 1024);
        }
        const auto turns = TurnsOf(store);
        CHECK_EQ(turns.size(), std::size_t(2));  // 3 contents fit: the last two turns
        CHECK_EQ(store.UndoTurn(made[0].turnId).status, Status::NotFound);
        CHECK_EQ(store.UndoTurn(made[5].turnId).status, Status::Ok);  // the latest turn is always undoable
        CHECK(box.Get("blob.bin") == versions[4]);

        // A cap smaller than one turn: the latest turn with changes is still kept.
        store.SetLimits(50, 10 * 1024);
        Usage usage;
        CHECK(store.Purge(&usage).ok());
        CHECK(usage.turns >= 1);
        // And a project that cannot fit at all is refused before anything is copied.
        std::string turnId;
        CHECK_EQ(store.BeginTurn(box.project, &turnId).status, Status::ProjectTooLarge);
        CHECK(turnId.empty() && !store.TurnIsOpen());
        CHECK_EQ(store.EndTurn().status, Status::NoTurnOpen);
    }
}

// ---------------------------------------------------------------- a cut in the middle of a restore
struct CrashScenario {
    Sandbox box;
    std::map<std::string, std::string> original;
    std::string turnId;
    std::wstring storeDir;  // <store>\<projectKey>

    explicit CrashScenario(const char* name) : box(name) {
        box.Put("a.txt", "a0\n");
        box.Put("b.txt", "b0\n");
        box.Put("c.txt", "c0\n");
        box.Put("gone.txt", "g0\n");
        box.Put("keep.txt", "k0\n");
        original = box.Files();
        Store store(box.store);
        const TurnSummary t = RunTurn(store, box, [&] {
            box.Put("a.txt", "a1\n", false);
            box.Put("b.txt", "b1\n", false);
            box.Put("c.txt", "c1\n", false);
            box.Remove("gone.txt");
            box.Put("new/created.txt", "n1\n", false);
        });
        turnId = t.turnId;
        storeDir = store.ProjectStoreDir();
        CHECK_EQ(t.changes.size(), std::size_t(5));
    }
    Options Faulty(const char* point, int nth) const {
        Options options;
        auto count = std::make_shared<int>(0);
        const std::string wanted = point;
        options.fault = [count, wanted, nth](std::string_view at) { return at == wanted && ++*count == nth; };
        return options;
    }
    std::wstring Journal() const { return storeDir + L"\\restore.json"; }
};

void TestCrashDuringRestore() {
    Section("cut in the middle of a restore: safe resume");
    struct Point {
        const char* name;
        int nth;
    };
    // Order of the operations: remove new/created.txt, then write a.txt, b.txt, c.txt, gone.txt.
    const Point points[] = {{"restore.journal", 1}, {"restore.op", 1}, {"restore.op", 2}, {"restore.op", 3}, {"restore.op", 5},
                            {"restore.temp", 1},    {"restore.temp", 3}, {"restore.applied", 1}, {"restore.records", 1}};
    for (const Point& point : points) {
        CrashScenario s((std::string("crash-") + point.name + "-" + std::to_string(point.nth)).c_str());
        {
            Store dying(s.box.store, s.Faulty(point.name, point.nth));
            CHECK(dying.Open(s.box.project).ok());
            const Result cut = dying.UndoTurn(s.turnId);
            CHECK_EQ(cut.status, Status::IoError);
        }
        CHECK(PathExists(s.Journal()));
        if (std::string(point.name) == "restore.temp") CHECK(CountFiles(s.box.project, L".snaptmp") >= 1);
        // A new browser: the next call on the project finishes the job.
        Store store(s.box.store);
        const Result resumed = store.Open(s.box.project);
        CHECK(resumed.resumedRestore);
        CHECK_EQ(resumed.status, Status::Ok);
        CHECK(resumed.conflicts.empty() && resumed.failed.empty());
        CHECK(s.box.Files() == s.original);
        CHECK(!PathExists(s.box.Path("new")));
        CHECK(!PathExists(s.Journal()));
        CHECK_EQ(CountFiles(s.box.project, L".snaptmp"), std::size_t(0));
        const auto turns = TurnsOf(store);
        CHECK(turns.size() == 1 && turns[0].undone);
        CHECK(!store.Open(s.box.project).resumedRestore);  // only once
        // The store is in working order afterwards.
        const TurnSummary next = RunTurn(store, s.box, [&] { s.box.Put("a.txt", "a-next\n", false); });
        CHECK_EQ(next.changes.size(), std::size_t(1));
        CHECK_EQ(store.UndoTurn(next.turnId).status, Status::Ok);
        CHECK(s.box.Files() == s.original);
    }

    // The user edits a file the cut restore had not reached yet: it is left alone and named.
    {
        CrashScenario s("crash-then-edit");
        {
            Store dying(s.box.store, s.Faulty("restore.op", 3));  // created.txt removed, a.txt restored; b, c, gone pending
            CHECK(dying.Open(s.box.project).ok());
            CHECK_EQ(dying.UndoTurn(s.turnId).status, Status::IoError);
        }
        CHECK(s.box.Get("a.txt") == std::string("a0\n"));
        CHECK(s.box.Get("b.txt") == std::string("b1\n"));
        s.box.Put("c.txt", "c1 edited after the crash\n", false);
        s.box.Put("gone.txt", "recreated after the crash\n", false);
        s.box.Put("a.txt", "a0 edited after the crash\n", false);  // already restored, then edited: not ours any more
        Store store(s.box.store);
        std::string turnId;
        const Result resumed = store.BeginTurn(s.box.project, &turnId);  // BeginTurn resumes too
        CHECK_EQ(resumed.status, Status::Ok);
        CHECK(resumed.resumedRestore);
        CHECK(HasConflict(resumed, "c.txt", ConflictReason::ModifiedSinceTurn));
        CHECK(HasConflict(resumed, "gone.txt", ConflictReason::ModifiedSinceTurn));
        CHECK(s.box.Get("a.txt") == std::string("a0 edited after the crash\n"));
        CHECK(s.box.Get("b.txt") == std::string("b0\n"));
        CHECK(s.box.Get("c.txt") == std::string("c1 edited after the crash\n"));
        CHECK(s.box.Get("gone.txt") == std::string("recreated after the crash\n"));
        CHECK(!s.box.Has("new/created.txt"));
        CHECK(!PathExists(s.Journal()));
        TurnSummary t;
        CHECK(store.EndTurn(&t).ok());
        CHECK(t.changes.empty());  // the user's edits made before the turn are not the turn's
    }

    // A damaged journal is dropped; nothing is touched.
    {
        CrashScenario s("crash-bad-journal");
        const auto before = s.box.Files();
        WriteFileBytes(s.Journal(), "{\"schema\":\"browser-chat-snapshot-restore/1\",\"ops\":[{\"path\":\"..\\\\..\\\\evil\"");
        Store store(s.box.store);
        const Result opened = store.Open(s.box.project);
        CHECK(opened.ok() && !opened.resumedRestore);
        CHECK(!PathExists(s.Journal()));
        CHECK(s.box.Files() == before);
        // A well-formed journal that points outside the project is refused whole.
        WriteFileBytes(s.Journal(),
                       "{\"schema\":\"browser-chat-snapshot-restore/1\",\"id\":\"abcdefgh\",\"turns\":[],\"removeDirs\":[],"
                       "\"ops\":[{\"path\":\"../outside.txt\",\"target\":null,\"from\":null,\"temp\":\"~abcdefgh-0.snaptmp\"}]}");
        WriteFileBytes(s.box.root + L"\\outside.txt", "must survive\n");
        CHECK(!store.Open(s.box.project).resumedRestore);
        CHECK(ReadFileBytes(s.box.root + L"\\outside.txt") == std::string("must survive\n"));
        CHECK(s.box.Files() == before);
    }
}

// ---------------------------------------------------------------- turns that end badly, several stores
void TestTurnLifecycle() {
    Section("interrupted turn, two stores on one project, refusals");
    Sandbox box("lifecycle");
    box.Put("a.txt", "a0\n");
    box.Put("b.txt", "b0\n");
    const auto original = box.Files();
    std::string interruptedId;
    {
        Store dying(box.store);
        CHECK(dying.BeginTurn(box.project, &interruptedId).ok());
        box.Put("a.txt", "a1 by the agent\n", false);
        box.Put("c.txt", "c1\n", false);
    }  // the browser stops here: no EndTurn
    Store store(box.store);
    std::string turnId;
    CHECK(store.BeginTurn(box.project, &turnId).ok());
    auto turns = TurnsOf(store);
    CHECK_EQ(turns.size(), std::size_t(1));
    CHECK(!turns.empty() && turns[0].turnId == interruptedId && turns[0].interrupted && turns[0].changes.size() == 2);

    // While this turn runs: a second store on the same project is told so, and nothing can be undone.
    Store other(box.store);
    CHECK_EQ(other.BeginTurn(box.project).status, Status::TurnOpen);
    CHECK(other.Open(box.project).ok());
    CHECK_EQ(other.UndoTurn(interruptedId).status, Status::TurnOpen);
    CHECK_EQ(store.UndoTurn(interruptedId).status, Status::TurnOpen);
    CHECK_EQ(store.BeginTurn(box.project).status, Status::TurnOpen);
    TurnSummary t;
    CHECK(store.EndTurn(&t).ok());
    CHECK(t.changes.empty());
    CHECK_EQ(store.EndTurn().status, Status::NoTurnOpen);
    CHECK_EQ(t.sequence, 2ull);

    // The second store can now work on the project, and undo the interrupted turn.
    const Result undone = other.UndoTurn(interruptedId);
    CHECK_EQ(undone.status, Status::Ok);
    CHECK(box.Files() == original);

    // A cut while the turn was being recorded: closed as interrupted at the next opening, still undoable.
    Options faulty;
    faulty.fault = [](std::string_view at) { return at == "end.record"; };
    {
        Store dying(box.store, faulty);
        CHECK(dying.BeginTurn(box.project).ok());
        box.Put("b.txt", "b1\n", false);
        CHECK_EQ(dying.EndTurn().status, Status::IoError);
    }
    Store after(box.store);
    CHECK(after.Open(box.project).ok());
    turns = TurnsOf(after);
    CHECK_EQ(turns.size(), std::size_t(3));
    CHECK(turns.size() == 3 && turns[2].interrupted && turns[2].changes.size() == 1 && turns[2].sequence == 3);
    CHECK_EQ(after.UndoTurn(turns[2].turnId).status, Status::Ok);
    CHECK(box.Files() == original);

    // Refusals.
    Store fresh(box.store);
    CHECK_EQ(fresh.UndoTurn(interruptedId).status, Status::NoProject);
    CHECK_EQ(fresh.EndTurn().status, Status::NoTurnOpen);
    CHECK_EQ(fresh.BeginTurn(box.root + L"\\does-not-exist").status, Status::InvalidProject);
    CHECK_EQ(fresh.BeginTurn(box.Path("a.txt")).status, Status::InvalidProject);
    CHECK_EQ(fresh.BeginTurn(L"C:\\").status, Status::InvalidProject);
    CHECK_EQ(fresh.BeginTurn(box.store).status, Status::InvalidProject);
    CHECK(!fresh.HasProject());

    Options tiny;
    tiny.maxFiles = 1;
    Store limited(box.store, tiny);
    CHECK_EQ(limited.BeginTurn(box.project).status, Status::ProjectTooLarge);
    CHECK(!limited.TurnIsOpen());

    // The same folder through another spelling is the same project.
    Store spelled(box.store);
    std::wstring upper = box.project + L"\\.\\";
    CharUpperBuffW(upper.data(), DWORD(upper.size()));
    CHECK(spelled.Open(upper).ok());
    CHECK(spelled.ProjectStoreDir() == after.ProjectStoreDir());
    CHECK_EQ(TurnsOf(spelled).size(), std::size_t(3));
}

// ---------------------------------------------------------------- the store inside the project (portable build)
void TestStoreInsideProject() {
    Section("store inside the project is never recorded");
    Sandbox box("inside");
    box.Put("page.html", "<p>0</p>\n");
    const std::wstring storeRoot = box.project + L"\\.browser\\chat-snapshots";
    Store store(storeRoot);
    ScanStats stats;
    CHECK(store.BeginTurn(box.project, nullptr, &stats).ok());
    CHECK_EQ(stats.files, std::size_t(1));
    box.Put("page.html", "<p>1</p>\n", false);
    TurnSummary t;
    CHECK(store.EndTurn(&t, &stats).ok());
    CHECK_EQ(t.changes.size(), std::size_t(1));
    CHECK_EQ(stats.files, std::size_t(1));
    const TurnSummary empty = RunTurn(store, box, [] {});
    CHECK(empty.changes.empty());
    CHECK_EQ(store.UndoTurn(t.turnId).status, Status::Ok);
    CHECK(box.Get("page.html") == std::string("<p>0</p>\n"));
    CHECK(PathExists(storeRoot));
}

// ---------------------------------------------------------------- options, and what no option can switch off
void TestOptions() {
    Section("options; .git is never recorded; a deleted project is not re-created");
    Sandbox box("options");
    box.Put(".gitignore", "*.log\n");
    box.Put("app.js", "1\n");
    box.Put("trace.log", "log\n");
    box.Put(".env", "KEY=value\n");
    box.Put("node_modules/p/index.js", "dep\n");
    box.Put("vendor/lib.js", "vendor\n");
    box.Put(".git/HEAD", "ref: refs/heads/main\n");
    box.Put("sub/.git/config", "[core]\n");
    box.Put("big.txt", std::string(3000, 'x'));

    Options options;
    options.excludedDirectories = {"vendor"};  // the caller's own list: node_modules is now recorded
    options.respectGitignore = false;
    options.skipSecretFiles = false;
    options.maxFileBytes = 2000;
    Store store(box.store, options);
    ScanStats stats;
    CHECK(store.BeginTurn(box.project, nullptr, &stats).ok());
    // .gitignore, app.js, trace.log, .env, node_modules/p/index.js ; big.txt is listed but not kept
    CHECK_EQ(stats.files, std::size_t(5));
    CHECK_EQ(stats.notKept, std::size_t(1));
    CHECK(!StoreHoldsText(box.store, "refs/heads/main"));
    CHECK(!StoreHoldsText(box.store, "vendor"));
    CHECK(StoreHoldsText(box.store, "KEY=value"));
    box.Put(".git/HEAD", "ref: refs/heads/other\n", false);
    box.Put("sub/.git/config", "[core]\nx = 1\n", false);
    box.Put("vendor/lib.js", "vendor 2\n", false);
    box.Put("trace.log", "log 2\n", false);
    box.Put(".env", "KEY=other\n", false);
    TurnSummary t;
    CHECK(store.EndTurn(&t).ok());
    CHECK_EQ(t.changes.size(), std::size_t(2));
    CHECK(FindChange(t, "trace.log") && FindChange(t, ".env") && FindChange(t, ".env")->restorable);
    CHECK_EQ(store.UndoTurn(t.turnId).status, Status::Ok);
    CHECK(box.Get(".env") == std::string("KEY=value\n"));
    CHECK(box.Get(".git/HEAD") == std::string("ref: refs/heads/other\n"));
    CHECK(box.Get("sub/.git/config") == std::string("[core]\nx = 1\n"));

    // The project folder disappears (moved, deleted): undo touches nothing and does not bring a folder back.
    const TurnSummary last = RunTurn(store, box, [&] { box.Put("app.js", "2\n", false); });
    RemoveTree(box.project);
    const Result gone = store.UndoTurn(last.turnId);
    CHECK_EQ(gone.status, Status::InvalidProject);
    CHECK(!PathExists(box.project));
    std::vector<TurnSummary> turns;
    CHECK(store.Turns(&turns).ok() && turns.size() == 2);  // the history is still readable
    CHECK_EQ(store.BeginTurn(box.project).status, Status::InvalidProject);
    CHECK(!PathExists(box.project));
    Store other(box.store);
    CHECK_EQ(other.Open(box.project).status, Status::InvalidProject);
}

}  // namespace

// Fix « 1 fichier modifié +7 −0 » (installed 0.3.690): another program writes in the folder during the turn. Only the
// files the agent said it wrote count, and only they are undone; the other file stays as that program left it.
void TestOnlyAgentFiles() {
    Section("only the agent's files count and are undone");
    Sandbox box("agent-files");
    box.Put("web/app.js", "const a = 1;\n");
    box.Put("notes.md", "# Notes\n");
    box.Put("Shared.css", "body {}\n");
    Store store(box.store);
    CHECK(store.BeginTurn(box.project).ok());
    box.Put("web/app.js", "const a = 2;\n", false);                                    // the agent
    box.Put("notes.md", "# Notes\n1\n2\n3\n4\n5\n6\n7\n", false);                    // another agent, same folder
    MakeDirs(box.Path("other/new"));
    box.Put("other/new/log.txt", "x\n", false);                                       // …and a new folder of its own
    box.Put("Shared.css", "body { margin: 0 }\n", false);                              // the agent (named in another letter case)
    TurnSummary t;
    const std::vector<std::string> agent{"web/app.js", "shared.css", "outside-never-written.txt"};
    CHECK(store.EndTurn(&t, nullptr, &agent).ok());
    CHECK_EQ(t.changes.size(), std::size_t(2));
    CHECK(FindChange(t, "web/app.js") && FindChange(t, "Shared.css"));
    CHECK(!FindChange(t, "notes.md") && !FindChange(t, "other/new/log.txt"));
    CHECK_EQ(t.additions, 2);
    CHECK_EQ(t.deletions, 2);
    const Result undo = store.UndoTurn(t.turnId);
    CHECK(undo.ok());
    CHECK(box.Get("web/app.js") == std::optional<std::string>("const a = 1;\n"));
    CHECK(box.Get("Shared.css") == std::optional<std::string>("body {}\n"));
    CHECK(box.Get("notes.md") == std::optional<std::string>("# Notes\n1\n2\n3\n4\n5\n6\n7\n"));   // never touched
    CHECK(box.Has("other/new/log.txt"));

    // An empty list (a turn that wrote nothing): nothing counted, even with files changed meanwhile.
    CHECK(store.BeginTurn(box.project).ok());
    box.Put("notes.md", "# Notes changed by someone else\n", false);
    const std::vector<std::string> none;
    TurnSummary empty;
    CHECK(store.EndTurn(&empty, nullptr, &none).ok());
    CHECK(empty.changes.empty());

    // The agent's file, changed again by someone else after the turn: undo names the conflict and touches nothing.
    CHECK(store.BeginTurn(box.project).ok());
    box.Put("web/app.js", "const a = 3;\n", false);
    TurnSummary third;
    const std::vector<std::string> mine{"web/app.js"};
    CHECK(store.EndTurn(&third, nullptr, &mine).ok());
    CHECK_EQ(third.changes.size(), std::size_t(1));
    box.Put("web/app.js", "const a = 3; // someone else\n", false);
    const Result clash = store.UndoTurn(third.turnId);
    CHECK(clash.status == Status::Conflict && clash.conflicts.size() == 1 && clash.conflicts[0].path == "web/app.js");
    CHECK(box.Get("web/app.js") == std::optional<std::string>("const a = 3; // someone else\n"));
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    TestGitignorePatterns();
    TestDiffContents();
    TestBasicTurn();
    TestBytesAreExact();
    TestExclusions();
    TestConflicts();
    TestMultiTurnRestore();
    TestShapes();
    TestScanReuse();
    TestPurge();
    TestCrashDuringRestore();
    TestTurnLifecycle();
    TestStoreInsideProject();
    TestOptions();
    TestOnlyAgentFiles();

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring base = temp;
    while (!base.empty() && base.back() == L'\\') base.pop_back();
    RemoveTree(base + L"\\browser-snapshots-test-" + std::to_wstring(GetCurrentProcessId()));

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
