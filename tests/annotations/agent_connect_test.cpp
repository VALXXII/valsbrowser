// Agent connection tests (src/agent-connect.*), ONLY on temporary folders:
//   agent_connect_test.exe <bridge.exe> <fake codex.exe> <out.mcpb>
// APPDATA, LOCALAPPDATA, USERPROFILE, CODEX_HOME and PATH of this process are redirected to
// build\annotations-test\tmp\agent-connect\... before anything runs; the test aborts if any of them still
// points elsewhere. run_tests.py fingerprints the real %APPDATA%\Claude and %USERPROFILE%\.codex configs
// before and after this run, and validates <out.mcpb> with Python's zipfile.
#include <algorithm>
#include <cstdlib>

#include "../../src/agent-connect.h"
#include "annotations_test_util.h"

#include <shellapi.h>  // after windows.h

namespace AC = AgentConnect;
namespace BA = BrowserAnnotations;
using namespace AnnTest;

namespace {

std::string N(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
bool StartsWithI(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), int(prefix.size()), prefix.c_str(), int(prefix.size()), TRUE) == CSTR_EQUAL;
}
std::vector<std::wstring> Backups(const std::wstring& dir, const std::wstring& stem) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + stem + L".browser-annotations-*.bak").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do out.push_back(fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}
std::size_t Count(const std::string& s, const std::string& needle) {
    std::size_t n = 0;
    for (auto at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1)) ++n;
    return n;
}
bool CopyTo(const std::wstring& from, const std::wstring& to) { return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0; }

struct Fixture {
    std::wstring base, appData, local, home, codexHome, bin, emptyBin, app, work;
    AC::Environment env;
    AC::Target target;
    std::int64_t now = 1794700000;  // 2026-11-14
    std::vector<std::wstring> opened;
    bool associated = false, openResult = true;
};

void Redirect(Fixture& f, const std::wstring& bridge) {
    f.base = TempDir(L"agent-connect");
    f.appData = f.base + L"\\Roaming";
    f.local = f.base + L"\\Local";
    f.home = f.base + L"\\Users\\Test User";
    f.codexHome = f.base + L"\\codex-home";
    f.bin = f.base + L"\\bin";
    f.emptyBin = f.base + L"\\empty-bin";
    f.app = f.base + L"\\Browser O'Brien Données";
    f.work = f.base + L"\\work";
    for (const auto* d : {&f.appData, &f.local, &f.bin, &f.emptyBin, &f.app, &f.work}) CreateDirectoryW(d->c_str(), nullptr);
    CreateDirectoryW((f.base + L"\\Users").c_str(), nullptr);
    CreateDirectoryW(f.home.c_str(), nullptr);
    SetEnvironmentVariableW(L"APPDATA", f.appData.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", f.local.c_str());
    SetEnvironmentVariableW(L"USERPROFILE", f.home.c_str());
    SetEnvironmentVariableW(L"CODEX_HOME", nullptr);
    SetEnvironmentVariableW(L"PATH", f.emptyBin.c_str());
    f.env = AC::Environment::FromProcess();
    // Hard guard: nothing below may run against real folders.
    for (const std::wstring* v : {&f.env.appData, &f.env.localAppData, &f.env.userProfile, &f.env.path})
        if (!StartsWithI(*v, f.base)) {
            std::printf("ABORT: environment not redirected (%s)\n", N(*v).c_str());
            std::exit(5);
        }
    f.env.now = [&f] { return f.now; };
    f.env.mcpbAssociated = [&f] { return f.associated; };
    f.env.openFile = [&f](const std::wstring& file) {
        f.opened.push_back(file);
        return f.openResult;
    };
    f.env.driveType = [](const std::wstring& root) { return unsigned(root == L"E:\\" ? DRIVE_REMOVABLE : DRIVE_FIXED); };
    f.target.bridgeExe = f.app + L"\\browser-annotations-mcp.exe";
    f.target.storeRoot = f.app + L"\\annotations";
    f.target.version = "0.3.242";
    CHECK(CopyTo(bridge, f.target.bridgeExe));
}

void TestPure() {
    CHECK(AC::Crc32("123456789") == 0xCBF43926u);
    CHECK(AC::Crc32("") == 0);
    CHECK(AC::QuoteArg(L"plain") == L"plain");
    CHECK(AC::QuoteArg(L"") == L"\"\"");
    CHECK(AC::QuoteArg(L"a b") == L"\"a b\"");
    CHECK(AC::QuoteArg(L"C:\\x y\\") == L"\"C:\\x y\\\\\"");
    CHECK(AC::QuoteArg(L"a\"b") == L"\"a\\\"b\"");
    // Round trip through the system parser.
    const std::vector<std::wstring> args = {L"mcp", L"add", L"browser-annotations", L"--", L"C:\\Program Files\\A&B\\b.exe", L"--store",
                                            L"C:\\x y\\O'Brien\\annotations\\", L"q\"uote", L"", L"%PATH%^!"};
    std::wstring cmd = L"codex.exe";
    for (const auto& a : args) cmd += L" " + AC::QuoteArg(a);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &argc);
    CHECK(argv && argc == int(args.size()) + 1);
    for (int i = 1; argv && i < argc && i <= int(args.size()); ++i) CHECK(args[std::size_t(i - 1)] == argv[i]);
    LocalFree(argv);
    // Zip: two entries, central directory, end record.
    const std::string zip = AC::ZipStored({{"a.txt", "hello"}, {"dir/b.bin", std::string(3, '\0')}}, 1794700000);
    CHECK(zip.compare(0, 4, "PK\x03\x04") == 0 && Count(zip, "PK\x01\x02") == 2 && Count(zip, std::string("PK\x05\x06", 4)) == 1);
    CHECK(zip.size() == 2 * 30 + 5 + 9 + 5 + 3 + 2 * 46 + 5 + 9 + 22);
}

void TestDetectionAndPaths(Fixture& f) {
    AC::Detection d = AC::Detect(f.env);
    CHECK(!d.claude && !d.codex && d.codexCli.empty() && !d.mcpbAssociated);
    CHECK(AC::ConnectClaude(f.env, f.target, f.work).result == AC::Result::NoClient);
    CHECK(AC::ConnectCodex(f.env, f.target).result == AC::Result::NoClient);
    CHECK(AC::DisconnectCodex(f.env, f.target).result == AC::Result::NoClient);
    CHECK(!Exists(f.home + L"\\.codex") && !Exists(f.appData + L"\\Claude"));  // detection never creates folders
    // Codex: %USERPROFILE%\.codex by default, CODEX_HOME when set.
    CHECK(AC::CodexHome(f.env) == f.home + L"\\.codex");
    CreateDirectoryW((f.home + L"\\.codex").c_str(), nullptr);
    CHECK(AC::Detect(f.env).codex && AC::Detect(f.env).codexConfig == f.home + L"\\.codex\\config.toml");
    SetEnvironmentVariableW(L"CODEX_HOME", f.codexHome.c_str());
    f.env.codexHome = AC::Environment::FromProcess().codexHome;
    CHECK(AC::CodexHome(f.env) == f.codexHome && !AC::Detect(f.env).codex);
    CreateDirectoryW(f.codexHome.c_str(), nullptr);
    CHECK(AC::Detect(f.env).codex);
    // Claude: %APPDATA%\Claude, or the MSIX copy when present.
    CreateDirectoryW((f.appData + L"\\Claude").c_str(), nullptr);
    CHECK(AC::ClaudeConfigPath(f.env) == f.appData + L"\\Claude\\claude_desktop_config.json");
    const std::wstring msix = f.local + L"\\Packages\\Claude_pzs8sxrjxfjjc\\LocalCache\\Roaming\\Claude";
    for (const wchar_t* p : {L"\\Packages", L"\\Packages\\Claude_pzs8sxrjxfjjc", L"\\Packages\\Claude_pzs8sxrjxfjjc\\LocalCache",
                             L"\\Packages\\Claude_pzs8sxrjxfjjc\\LocalCache\\Roaming", L"\\Packages\\Claude_pzs8sxrjxfjjc\\LocalCache\\Roaming\\Claude"})
        CreateDirectoryW((f.local + p).c_str(), nullptr);
    CHECK(AC::ClaudeConfigDir(f.env) == msix);
    RemoveTree(f.local + L"\\Packages");
    CHECK(AC::ClaudeConfigDir(f.env) == f.appData + L"\\Claude");
    // Stable bridge path: installed => bin copy when present; portable => next to browser.exe.
    const std::wstring inst = f.base + L"\\inst";
    for (const wchar_t* p : {L"", L"\\versions", L"\\versions\\0.3.242", L"\\bin"}) CreateDirectoryW((inst + p).c_str(), nullptr);
    CHECK(AC::StableBridgePath(inst + L"\\versions\\0.3.242\\browser.exe", inst) == inst + L"\\versions\\0.3.242\\browser-annotations-mcp.exe");
    CHECK(WriteText(inst + L"\\bin\\browser-annotations-mcp.exe", "MZ"));
    CHECK(AC::StableBridgePath(inst + L"\\versions\\0.3.242\\browser.exe", inst) == inst + L"\\bin\\browser-annotations-mcp.exe");
    CHECK(AC::StableBridgePath(f.app + L"\\browser.exe", inst) == f.app + L"\\browser-annotations-mcp.exe");
    // Removable drive (simulated GetDriveType).
    CHECK(AC::IsOnRemovableDrive(f.env, L"E:\\Browser\\browser-annotations-mcp.exe"));
    CHECK(!AC::IsOnRemovableDrive(f.env, L"C:\\Browser\\browser-annotations-mcp.exe"));
    CHECK(!AC::IsOnRemovableDrive(f.env, L"\\\\server\\share\\x.exe"));
}

void TestSnippets(Fixture& f) {
    StrictJson::Value v;
    const std::string json = AC::ClaudeJsonSnippet(f.target);
    CHECK(StrictJson::Parse(json, v) && v.Get("mcpServers") && v.Get("mcpServers")->Get("browser-annotations"));
    const StrictJson::Value* server = v.Get("mcpServers")->Get("browser-annotations");
    CHECK(server && server->Get("command")->string == N(f.target.bridgeExe) && server->Get("args")->items.size() == 2 &&
          server->Get("args")->items[1].string == N(f.target.storeRoot));
    const std::string toml = AC::CodexTomlSnippet(f.target);
    CHECK(toml.compare(0, 33, "[mcp_servers.browser-annotations]") == 0 && toml.find("O'Brien Donn\xC3\xA9" "es") != std::string::npos);
    CHECK(AC::CodexAddCommandLine(f.target).find(L"codex mcp add browser-annotations -- \"" + f.target.bridgeExe + L"\" --store \"") == 0);
}

void TestClaudeJson(Fixture& f) {
    const std::wstring dir = f.appData + L"\\Claude", cfg = dir + L"\\claude_desktop_config.json";
    // 1. Absent file: created with only our entry, no backup.
    AC::Report r = AC::MergeClaudeConfig(cfg, f.target, f.now);
    CHECK(r.result == AC::Result::Connected && r.backup.empty() && ReadText(cfg) == AC::ClaudeJsonSnippet(f.target));
    CHECK(AC::ReadClaudeEntry(cfg, "browser-annotations").present);
    // 2. Already connected: nothing written, no backup.
    const std::string before = ReadText(cfg);
    r = AC::MergeClaudeConfig(cfg, f.target, f.now);
    CHECK(r.result == AC::Result::AlreadyConnected && r.backup.empty() && ReadText(cfg) == before && Backups(dir, L"claude_desktop_config.json").empty());
    // 3. Empty file.
    CHECK(WriteText(cfg, ""));
    r = AC::MergeClaudeConfig(cfg, f.target, f.now);
    CHECK(r.result == AC::Result::Connected && !r.backup.empty() && ReadText(r.backup).empty());
    CHECK(Backups(dir, L"claude_desktop_config.json").size() == 1 &&
          Backups(dir, L"claude_desktop_config.json")[0] == L"claude_desktop_config.json.browser-annotations-20261114-234640.bak");
    // 4. Invalid JSON (trailing comma, comment, duplicate key, truncated): refused, file untouched, no backup.
    for (const char* bad : {"{\"a\": 1,}", "{// x\n}", "{\"a\":1,\"a\":2}", "{\"mcpServers\": {", "[1,2]", "\"text\""}) {
        CHECK(WriteText(cfg, bad));
        const std::size_t backups = Backups(dir, L"claude_desktop_config.json").size();
        r = AC::MergeClaudeConfig(cfg, f.target, f.now);
        CHECK(r.result == AC::Result::InvalidConfig && ReadText(cfg) == bad && Backups(dir, L"claude_desktop_config.json").size() == backups);
    }
    CHECK(WriteText(cfg, "{\"mcpServers\": [1]}"));
    CHECK(AC::MergeClaudeConfig(cfg, f.target, f.now).result == AC::Result::InvalidConfig && ReadText(cfg) == "{\"mcpServers\": [1]}");
    // 5. Other servers and keys (CRLF, 4 spaces, BOM, non-ASCII, big and decimal numbers): spliced, round trip exact.
    const std::string original =
        "\xEF\xBB\xBF{\r\n    \"mcpServers\": {\r\n        \"filesystem\": {\r\n            \"command\": \"npx\",\r\n"
        "            \"args\": [\"-y\", \"@modelcontextprotocol/server-filesystem\", \"C:\\\\Users\\\\Andr\xC3\xA9\"]\r\n        },\r\n"
        "        \"other\": {\"command\": \"x.exe\", \"env\": {\"TOKEN\": \"\\u00e9t\\u00e9\"}}\r\n    },\r\n"
        "    \"preferences\": {\"big\": 12345678901234567890, \"ratio\": 1.50, \"exp\": 1E+2, \"menuBarEnabled\": false}\r\n}\r\n";
    CHECK(WriteText(cfg, original));
    f.now += 1;
    r = AC::MergeClaudeConfig(cfg, f.target, f.now);
    const std::string merged = ReadText(cfg);
    CHECK(r.result == AC::Result::Connected && ReadText(r.backup) == original);
    CHECK(merged.compare(0, 3, "\xEF\xBB\xBF") == 0 && Count(merged, "\n") == Count(merged, "\r\n"));
    CHECK(merged.find("12345678901234567890") != std::string::npos && merged.find("1.50") != std::string::npos && merged.find("1E+2") != std::string::npos);
    CHECK(merged.find("\"\\u00e9t\\u00e9\"") != std::string::npos && merged.find("\r\n        \"browser-annotations\": {\r\n            \"command\": ") != std::string::npos);
    CHECK(AC::ReadClaudeEntry(cfg, "browser-annotations").present && AC::ReadClaudeEntry(cfg, "other").present &&
          AC::ReadClaudeEntry(cfg, "filesystem").args.size() == 3);
    f.now += 1;
    r = AC::RemoveClaudeConfig(cfg, "browser-annotations", f.now);
    CHECK(r.result == AC::Result::Disconnected && ReadText(cfg) == original && ReadText(r.backup) == merged);
    CHECK(AC::RemoveClaudeConfig(cfg, "browser-annotations", f.now).result == AC::Result::NotConnected && ReadText(cfg) == original);
    // 6. Compact single-line file, our entry first of two: round trip exact.
    const std::string compact = "{\"a\":1,\"mcpServers\":{\"x\":{\"command\":\"y\"}},\"z\":[true,null]}";
    CHECK(WriteText(cfg, compact));
    CHECK(AC::MergeClaudeConfig(cfg, f.target, f.now).ok());
    CHECK(ReadText(cfg).find('\n') == std::string::npos && AC::ReadClaudeEntry(cfg, "x").present);
    CHECK(AC::RemoveClaudeConfig(cfg, "browser-annotations", f.now).ok() && ReadText(cfg) == compact);
    const std::string oursFirst = "{\"mcpServers\": {\"browser-annotations\": {\"command\": \"old.exe\"}, \"keep\": {\"command\": \"k\"}}}";
    CHECK(WriteText(cfg, oursFirst));
    CHECK(AC::RemoveClaudeConfig(cfg, "browser-annotations", f.now).ok() && ReadText(cfg) == "{\"mcpServers\": {\"keep\": {\"command\": \"k\"}}}");
    // 7. No mcpServers yet; then a stale entry (old path) is replaced in place.
    const std::string noServers = "{\n  \"globalShortcut\": \"Ctrl+Space\"\n}\n";
    CHECK(WriteText(cfg, noServers));
    CHECK(AC::MergeClaudeConfig(cfg, f.target, f.now).ok());
    CHECK(ReadText(cfg).find("  \"globalShortcut\": \"Ctrl+Space\",\n  \"mcpServers\": {\n    \"browser-annotations\": {\n      \"command\": ") == 2);
    AC::Target moved = f.target;
    moved.bridgeExe = L"E:\\Browser\\browser-annotations-mcp.exe";
    AC::Status st = AC::ClientStatus(f.env, AC::Client::Claude, moved, {});
    CHECK(st.configured && st.stalePath && st.removableDrive && !st.recentlyRead && st.connected());
    CHECK(AC::MergeClaudeConfig(cfg, moved, f.now).result == AC::Result::Connected);
    CHECK(AC::ReadClaudeEntry(cfg, "browser-annotations").command == moved.bridgeExe && Count(ReadText(cfg), "\"browser-annotations\":") == 1);
    st = AC::ClientStatus(f.env, AC::Client::Claude, moved, {});
    CHECK(st.configured && !st.stalePath);
    // 8. Empty mcpServers object, pretty.
    CHECK(WriteText(cfg, "{\n  \"mcpServers\": {}\n}"));
    CHECK(AC::MergeClaudeConfig(cfg, f.target, f.now).ok());
    CHECK(ReadText(cfg).rfind("{\n  \"mcpServers\": {\n    \"browser-annotations\": {\n      \"command\": ", 0) == 0);
    // 9. Read-only file: fails, untouched, no backup left behind.
    CHECK(WriteText(cfg, "{}"));
    SetFileAttributesW(cfg.c_str(), FILE_ATTRIBUTE_READONLY);
    const std::size_t backupsBefore = Backups(dir, L"claude_desktop_config.json").size();
    f.now += 100;
    r = AC::MergeClaudeConfig(cfg, f.target, f.now);
    CHECK(r.result == AC::Result::Failed && ReadText(cfg) == "{}" && r.backup.empty());
    CHECK(Backups(dir, L"claude_desktop_config.json").size() == backupsBefore);  // useless backup removed
    SetFileAttributesW(cfg.c_str(), FILE_ATTRIBUTE_NORMAL);
    // No temporary file is ever left behind.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.tmp-*").c_str(), &fd);
    CHECK(h == INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
}

void TestClaudeConnectFlow(Fixture& f, const std::wstring& mcpbOut) {
    const std::wstring dir = f.appData + L"\\Claude", cfg = dir + L"\\claude_desktop_config.json";
    CHECK(WriteText(cfg, "{\"keep\": true}"));
    // .mcpb associated: bundle built in workDir and opened; the config is not touched.
    f.associated = true;
    AC::Report r = AC::ConnectClaude(f.env, f.target, f.work);
    CHECK(r.result == AC::Result::PendingInstall && f.opened.size() == 1 && f.opened[0] == f.work + L"\\browser-annotations.mcpb");
    CHECK(Exists(f.work + L"\\browser-annotations.mcpb") && ReadText(cfg) == "{\"keep\": true}");
    // Opening fails => falls back to the config merge.
    f.openResult = false;
    r = AC::ConnectClaude(f.env, f.target, f.work);
    CHECK(r.result == AC::Result::Connected && AC::ReadClaudeEntry(cfg, "browser-annotations").present);
    CHECK(AC::ConnectClaude(f.env, f.target, f.work).result == AC::Result::AlreadyConnected);
    // No association => config merge directly.
    f.associated = false;
    f.openResult = true;
    CHECK(AC::DisconnectClaude(f.env, f.target).result == AC::Result::Disconnected && ReadText(cfg) == "{\"keep\": true,\"mcpServers\": {}}");
    r = AC::ConnectClaude(f.env, f.target, f.work);
    CHECK(r.result == AC::Result::Connected && f.opened.size() == 2);
    // Installed extension (as Claude would unpack it): status + ManualStep on disconnect, never edited.
    const std::wstring ext = dir + L"\\Claude Extensions\\local.mcpb.browser.browser-annotations";
    CreateDirectoryW((dir + L"\\Claude Extensions").c_str(), nullptr);
    CreateDirectoryW(ext.c_str(), nullptr);
    CHECK(WriteText(ext + L"\\manifest.json", AC::McpbManifest(f.target)));
    CHECK(AC::ClaudeExtensionInstalled(f.env, "browser-annotations"));
    AC::Status st = AC::ClientStatus(f.env, AC::Client::Claude, f.target, {});
    CHECK(st.extensionInstalled && st.configured && !st.stalePath);
    r = AC::DisconnectClaude(f.env, f.target);
    CHECK(r.result == AC::Result::ManualStep && r.message.find("Extensions") != std::string::npos && Exists(ext + L"\\manifest.json"));
    CHECK(AC::ConnectClaude(f.env, f.target, f.work).result == AC::Result::AlreadyConnected);
    RemoveTree(dir + L"\\Claude Extensions");
    // Readers: recent Claude read => connected even without config.
    CHECK(WriteText(cfg, "{}"));
    std::vector<BA::Reader> readers = {{"claude-ai", "Claude Desktop", "", f.now - 3600}, {"codex-mcp-client", "Codex", "", f.now - 8 * 86400}};
    st = AC::ClientStatus(f.env, AC::Client::Claude, f.target, readers);
    CHECK(!st.configured && st.recentlyRead && st.connected() && st.readerLabel == "Claude Desktop" && !st.removableDrive);
    AC::Status cx = AC::ClientStatus(f.env, AC::Client::Codex, f.target, readers);
    CHECK(!cx.recentlyRead && cx.lastReadAt == f.now - 8 * 86400 && !cx.connected());
    // The bundle for Python's zipfile check.
    std::string error;
    CHECK(AC::BuildMcpb(f.target, mcpbOut, &error));
    const std::string zip = ReadText(mcpbOut);
    CHECK(zip.compare(0, 4, "PK\x03\x04") == 0 && zip.find("manifest.json") != std::string::npos);
    AC::Target missing = f.target;
    missing.bridgeExe = f.base + L"\\nope.exe";
    CHECK(!AC::BuildMcpb(missing, f.work + L"\\x.mcpb", &error) && !Exists(f.work + L"\\x.mcpb"));
    StrictJson::Value m;
    CHECK(StrictJson::Parse(AC::McpbManifest(f.target), m) && m.Get("manifest_version")->string == "0.3" &&
          m.Get("server")->Get("type")->string == "binary" && m.Get("server")->Get("mcp_config")->Get("args")->items[1].string == N(f.target.storeRoot));
}

const char* kToml =
    "# Codex settings\r\nmodel = \"o3\"  # inline\r\napproval_policy = 'on-request'\r\n"
    "notes = \"\"\"\r\n[mcp_servers.browser-annotations]\r\ncommand = \"not a table\"\r\n\"\"\"\r\n"
    "lits = '''\r\n[mcp_servers.browser-annotations]\r\n'''\r\n"
    "matrix = [\r\n  [1, 2],\r\n  [3, 4], # [mcp_servers.x]\r\n]\r\n\r\n"
    "[mcp_servers.other]\r\ncommand = \"x.exe\"\r\nargs = [\"a\", \"b]\"]\r\n\r\n"
    "# profile comment stays with [profiles.p]\r\n[profiles.p]\r\nmodel = \"y\"\r\n"
    "[mcp_servers.\"quoted name\"]\r\ncommand = 'q.exe'\r\n";

void TestCodexToml(Fixture& f) {
    const std::wstring cfg = f.codexHome + L"\\config.toml";
    // 1. Absent file.
    AC::Report r = AC::MergeCodexToml(cfg, f.target, f.now);
    CHECK(r.result == AC::Result::Connected && r.backup.empty() && ReadText(cfg) == AC::CodexTomlSnippet(f.target));
    CHECK(AC::MergeCodexToml(cfg, f.target, f.now).result == AC::Result::AlreadyConnected);
    AC::ConfiguredEntry e = AC::ReadCodexEntry(cfg, "browser-annotations");
    CHECK(e.present && e.command == f.target.bridgeExe && e.args.size() == 2 && e.args[1] == f.target.storeRoot);
    CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).result == AC::Result::Disconnected && ReadText(cfg).empty());
    // 2. Other tables, comments, CRLF, multi-line strings that LOOK like our header, nested arrays: round trip exact.
    CHECK(WriteText(cfg, kToml));
    CHECK(!AC::ReadCodexEntry(cfg, "browser-annotations").present);
    f.now += 10;
    r = AC::MergeCodexToml(cfg, f.target, f.now);
    const std::string merged = ReadText(cfg);
    CHECK(r.result == AC::Result::Connected && ReadText(r.backup) == kToml);
    CHECK(merged.compare(0, std::strlen(kToml), kToml) == 0 && Count(merged, "\n") == Count(merged, "\r\n"));
    CHECK(merged.substr(std::strlen(kToml)) == "\r\n[mcp_servers.browser-annotations]\r\ncommand = \"" +
                                                   [&] { std::string s; for (char c : N(f.target.bridgeExe)) { if (c == '\\') s += '\\'; s += c; } return s; }() + "\"\r\n" +
                                                   "args = [\"--store\", \"" +
                                                   [&] { std::string s; for (char c : N(f.target.storeRoot)) { if (c == '\\') s += '\\'; s += c; } return s; }() + "\"]\r\n");
    CHECK(AC::ReadCodexEntry(cfg, "other").args.size() == 2 && AC::ReadCodexEntry(cfg, "other").args[1] == L"b]");
    CHECK(AC::ReadCodexEntry(cfg, "quoted name").command == L"q.exe");
    r = AC::RemoveCodexToml(cfg, "browser-annotations", f.now);
    CHECK(r.result == AC::Result::Disconnected && ReadText(cfg) == kToml);
    CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).result == AC::Result::NotConnected && ReadText(cfg) == kToml);
    // 3. Already present in codex-cli style (literal strings) => AlreadyConnected, nothing written.
    const std::string cliStyle = "[mcp_servers.browser-annotations]\ncommand = '" + N(f.target.bridgeExe) + "'\nargs = [\"--store\", '" +
                                 N(f.target.storeRoot) + "']\n";
    // (a literal string cannot hold "'": this app folder has one, so codex would have used a basic string)
    const std::string basicStyle = "[mcp_servers.\"browser-annotations\"]\ncommand = \"" + [&] { std::string s; for (char c : N(f.target.bridgeExe)) { if (c == '\\') s += '\\'; s += c; } return s; }() +
                                   "\"\nargs = [\n  \"--store\",  # store\n  \"" + [&] { std::string s; for (char c : N(f.target.storeRoot)) { if (c == '\\') s += '\\'; s += c; } return s; }() + "\",\n]\n";
    CHECK(WriteText(cfg, basicStyle));
    CHECK(AC::MergeCodexToml(cfg, f.target, f.now).result == AC::Result::AlreadyConnected && ReadText(cfg) == basicStyle);
    AC::Target simple = f.target;
    simple.bridgeExe = f.base + L"\\simple\\b.exe";
    simple.storeRoot = f.base + L"\\simple\\annotations";
    const std::string literal = "[mcp_servers.browser-annotations]\ncommand = '" + N(simple.bridgeExe) + "'\nargs = ['--store', '" + N(simple.storeRoot) + "']\n";
    CHECK(WriteText(cfg, literal));
    CHECK(AC::MergeCodexToml(cfg, simple, f.now).result == AC::Result::AlreadyConnected);
    (void)cliStyle;
    // 4. Stale entry with a subtable: replaced in place, subtable gone, neighbours kept.
    const std::string stale = "a = 1\n\n[mcp_servers.browser-annotations]\ncommand = 'D:\\\\old.exe'\nstartup_timeout_sec = 5\n\n"
                              "[mcp_servers.browser-annotations.env]\nX = \"1\"\n\n[tui]\nnotifications = true\n";
    CHECK(WriteText(cfg, stale));
    AC::Status st = AC::ClientStatus(f.env, AC::Client::Codex, f.target, {});
    CHECK(st.configured && st.stalePath);
    r = AC::MergeCodexToml(cfg, f.target, f.now);
    const std::string replaced = ReadText(cfg);
    CHECK(r.result == AC::Result::Connected && Count(replaced, "[mcp_servers.browser-annotations") == 1 && replaced.find("old.exe") == std::string::npos &&
          replaced.find("[mcp_servers.browser-annotations.env]") == std::string::npos);
    CHECK(replaced.compare(0, 7, "a = 1\n\n") == 0 && replaced.find("\n\n[tui]\nnotifications = true\n") != std::string::npos);
    CHECK(!AC::ClientStatus(f.env, AC::Client::Codex, f.target, {}).stalePath);
    // 5. Forms we do not edit: refused, file untouched, no backup.
    for (const char* foreign : {"mcp_servers = { browser-annotations = { command = \"x\" } }\n",
                                "mcp_servers.browser-annotations.command = \"x\"\n",
                                "[mcp_servers]\nbrowser-annotations = { command = \"x\" }\n",
                                "[[mcp_servers]]\nname = \"x\"\n"}) {
        CHECK(WriteText(cfg, foreign));
        const std::size_t backups = Backups(f.codexHome, L"config.toml").size();
        r = AC::MergeCodexToml(cfg, f.target, f.now);
        CHECK(r.result == AC::Result::InvalidConfig && ReadText(cfg) == foreign && Backups(f.codexHome, L"config.toml").size() == backups);
    }
    // A [mcp_servers] table holding other servers is fine.
    CHECK(WriteText(cfg, "[mcp_servers]\nother = { command = \"x\" }\n"));
    CHECK(AC::MergeCodexToml(cfg, f.target, f.now).result == AC::Result::Connected);
    CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).ok() && ReadText(cfg) == "[mcp_servers]\nother = { command = \"x\" }\n");
    // 6. Not scannable TOML: refused.
    for (const char* bad : {"a = \"unterminated\n", "a = [1, 2\n", "a = '''never closed\n", "this is not toml\n", "[broken\n"}) {
        CHECK(WriteText(cfg, bad));
        CHECK(AC::MergeCodexToml(cfg, f.target, f.now).result == AC::Result::InvalidConfig && ReadText(cfg) == bad);
        CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).result == AC::Result::InvalidConfig && ReadText(cfg) == bad);
    }
    // 7. File without final newline.
    CHECK(WriteText(cfg, "model = \"o3\""));
    CHECK(AC::MergeCodexToml(cfg, f.target, f.now).ok() && ReadText(cfg).compare(0, 14, "model = \"o3\"\n\n") == 0);
    CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).ok() && ReadText(cfg) == "model = \"o3\"\n");
    DeleteFileW(cfg.c_str());
}

void TestCodexCli(Fixture& f, const std::wstring& fakeCodex) {
    const std::wstring cfg = f.codexHome + L"\\config.toml";
    // PATH: a relative entry and a .cmd shim come first; neither is used.
    const std::wstring shim = f.base + L"\\shim";
    CreateDirectoryW(shim.c_str(), nullptr);
    CHECK(WriteText(shim + L"\\codex.cmd", "@echo off\r\nexit /b 7\r\n"));
    CHECK(CopyTo(fakeCodex, f.bin + L"\\codex.exe"));
    f.env.path = L"bin;" + shim + L";\"" + f.bin + L"\";" + f.emptyBin;
    CHECK(AC::FindCodexCli(f.env) == f.bin + L"\\codex.exe");
    CHECK(WriteText(cfg, "model = \"o3\"\n"));
    AC::Report r = AC::ConnectCodex(f.env, f.target);
    CHECK(r.result == AC::Result::Connected && r.usedCli && ReadText(r.backup) == "model = \"o3\"\n");
    const std::string log = ReadText(f.codexHome + L"\\fake-codex.log");
    CHECK(log == "cwd=" + N(f.codexHome) + "|CODEX_HOME=" + N(f.codexHome) + "|mcp|add|browser-annotations|--|" + N(f.target.bridgeExe) + "|--store|" +
                     N(f.target.storeRoot) + "\n");
    // The fake writes literal strings like codex-cli; our reader verified it. A "'" in the path is not
    // representable in a TOML literal string, so the fake's output is checked through ReadCodexEntry.
    CHECK(AC::ConnectCodex(f.env, f.target).result == AC::Result::AlreadyConnected);
    r = AC::DisconnectCodex(f.env, f.target);
    CHECK(r.result == AC::Result::Disconnected && r.usedCli && !AC::ReadCodexEntry(cfg, "browser-annotations").present);
    CHECK(AC::DisconnectCodex(f.env, f.target).result == AC::Result::NotConnected);
    // CLI failing or doing nothing: verified, then our own edit (one backup only).
    for (const char* mode : {"fail", "noop"}) {
        CHECK(WriteText(f.codexHome + L"\\fake-mode", mode));
        CHECK(WriteText(cfg, "model = \"o3\"\n"));
        const std::size_t backups = Backups(f.codexHome, L"config.toml").size();
        f.now += 2;
        r = AC::ConnectCodex(f.env, f.target);
        CHECK(r.result == AC::Result::Connected && r.usedCli && AC::ReadCodexEntry(cfg, "browser-annotations").present);
        CHECK(Backups(f.codexHome, L"config.toml").size() == backups + 1);
        r = AC::DisconnectCodex(f.env, f.target);
        CHECK(r.result == AC::Result::Disconnected && ReadText(cfg) == "model = \"o3\"\n");
    }
    DeleteFileW((f.codexHome + L"\\fake-mode").c_str());
    // Invalid file: refused before the CLI ever runs.
    const std::size_t calls = Count(ReadText(f.codexHome + L"\\fake-codex.log"), "\n");
    CHECK(WriteText(cfg, "a = \"unterminated\n"));
    CHECK(AC::ConnectCodex(f.env, f.target).result == AC::Result::InvalidConfig && Count(ReadText(f.codexHome + L"\\fake-codex.log"), "\n") == calls);
    // Without the CLI: direct edit.
    f.env.path = f.emptyBin;
    CHECK(WriteText(cfg, "model = \"o3\"\n"));
    r = AC::ConnectCodex(f.env, f.target);
    CHECK(r.result == AC::Result::Connected && !r.usedCli);
    AC::Status st = AC::ClientStatus(f.env, AC::Client::Codex, f.target, {{"codex-mcp-client", "Codex", "", f.now}});
    CHECK(st.configured && st.recentlyRead && st.readerLabel == "Codex" && !st.stalePath);
    CHECK(AC::DisconnectCodex(f.env, f.target).result == AC::Result::Disconnected && ReadText(cfg) == "model = \"o3\"\n");
}

// Test guard (incident 14 Nov 2026): with testMode, every write under a protected root is refused and nothing is
// touched. The "real" folders are SIMULATED here (temporary folders declared as protected), never the user's.
void TestGuard(Fixture& f) {
    const std::wstring fake = f.base + L"\\guard";
    const std::wstring roaming = fake + L"\\Roaming", profile = fake + L"\\Profile";
    for (const auto& d : {fake, roaming, roaming + L"\\Claude", profile, profile + L"\\.codex", fake + L"\\work"}) CreateDirectoryW(d.c_str(), nullptr);
    const std::wstring claudeCfg = roaming + L"\\Claude\\claude_desktop_config.json", codexCfg = profile + L"\\.codex\\config.toml";
    const std::string claudeText = "{\n  \"mcpServers\": {}\n}\n", codexText = "model = \"o3\"\n";
    CHECK(WriteText(claudeCfg, claudeText) && WriteText(codexCfg, codexText));
    AC::Environment env = f.env;
    env.appData = roaming; env.localAppData = fake + L"\\Local"; env.userProfile = profile; env.codexHome.clear();
    env.testMode = true;
    env.protectedRoots = [roaming, profile] { return std::vector<std::wstring>{roaming + L"\\Claude", profile + L"\\.codex"}; };
    f.associated = true; f.opened.clear();
    AC::Report r = AC::ConnectClaude(env, f.target, fake + L"\\work");
    CHECK(r.result == AC::Result::Failed && r.message.find("Mode test") != std::string::npos && r.backup.empty());
    CHECK(f.opened.empty());   // no .mcpb handed to Claude's installer either
    CHECK(AC::DisconnectClaude(env, f.target).result == AC::Result::Failed);
    CHECK(AC::ConnectCodex(env, f.target).result == AC::Result::Failed && AC::DisconnectCodex(env, f.target).result == AC::Result::Failed);
    CHECK(ReadText(claudeCfg) == claudeText && ReadText(codexCfg) == codexText);
    CHECK(Backups(roaming + L"\\Claude", L"claude_desktop_config.json").empty() && Backups(profile + L"\\.codex", L"config.toml").empty());
    std::string why;
    CHECK(!AC::WriteAllowed(env, roaming + L"\\CLAUDE\\x.json", &why) && !why.empty());   // case-insensitive, inside
    CHECK(AC::WriteAllowed(env, roaming + L"\\Claude2\\x.json"));                       // a sibling is not inside
    AC::Environment none = env; none.protectedRoots = [] { return std::vector<std::wstring>{}; };
    CHECK(!AC::WriteAllowed(none, f.base + L"\\anything"));                              // roots unknown: refuse
    AC::Environment off = env; off.testMode = false;
    CHECK(AC::WriteAllowed(off, claudeCfg));                                             // the real product (explicit click)
    // Test mode with a redirected (non-protected) folder works normally.
    AC::Environment redirected = env; redirected.protectedRoots = [] { return std::vector<std::wstring>{L"C:\\NotHere\\Claude"}; };
    r = AC::ConnectClaude(redirected, f.target, fake + L"\\work");
    CHECK(r.result == AC::Result::Connected && f.opened.empty() && ReadText(claudeCfg).find("browser-annotations") != std::string::npos);
    f.associated = false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) {
        std::printf("usage: agent_connect_test <bridge.exe> <fake-codex.exe> <out.mcpb>\n");
        return 2;
    }
    const std::wstring bridge = FullPath(argv[1]), fakeCodex = FullPath(argv[2]), mcpbOut = FullPath(argv[3]);
    Fixture f;
    Redirect(f, bridge);
    std::printf("agent_connect: APPDATA=%s USERPROFILE=%s PATH=%s\n", N(f.env.appData).c_str(), N(f.env.userProfile).c_str(), N(f.env.path).c_str());
    TestPure();
    TestDetectionAndPaths(f);
    TestSnippets(f);
    TestClaudeJson(f);
    TestClaudeConnectFlow(f, mcpbOut);
    TestCodexToml(f);
    TestCodexCli(f, fakeCodex);
    TestGuard(f);
    return Finish("agent_connect_test");
}
