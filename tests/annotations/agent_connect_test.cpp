// Agent connection tests (src/agent-connect.*), ONLY on temporary folders:
//   agent_connect_test.exe <bridge.exe> <fake codex.exe> <out.mcpb>
// APPDATA, LOCALAPPDATA, USERPROFILE, CODEX_HOME and PATH of this process are redirected to
// build\annotations-test\tmp\agent-connect\... before anything runs; the test aborts if any of them still
// points elsewhere. run_tests.py fingerprints the real %APPDATA%\Claude and %USERPROFILE%\.codex configs
// before and after this run, and validates <out.mcpb> with Python's zipfile.
#include <algorithm>
#include <cstdlib>

#include "../../src/agent-connect.h"
#include "../../src/i18n.h"
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
    // Bridge location (plan B, item 5): installed => ALWAYS the stable <root>\bin copy, present or not (never the
    // versioned copy the helper prunes); portable => assets\bin in the release payload, next to browser.exe otherwise.
    const std::wstring inst = f.base + L"\\inst";
    for (const wchar_t* p : {L"", L"\\versions", L"\\versions\\0.3.242", L"\\bin"}) CreateDirectoryW((inst + p).c_str(), nullptr);
    const std::wstring stable = inst + L"\\bin\\browser-annotations-mcp.exe", versioned = inst + L"\\versions\\0.3.242\\browser.exe";
    CHECK(CopyTo(f.target.bridgeExe, inst + L"\\versions\\0.3.242\\browser-annotations-mcp.exe"));   // a versioned copy exists...
    for (auto layout : {AC::BridgeLayout::Sibling, AC::BridgeLayout::AssetsBin}) {
        const auto at = AC::LocateBridge(versioned, layout, inst);
        CHECK(at.installed && at.path == stable);   // ...but bin is absent: still bin, no silent fall back
    }
    CHECK(AC::StableBridgePath(versioned, inst) == stable);
    CHECK(WriteText(stable, "MZ"));
    CHECK(AC::LocateBridge(versioned, AC::BridgeLayout::AssetsBin, inst).path == stable);
    CHECK(DeleteFileW(stable.c_str()));
    CHECK(!AC::LocateBridge(f.app + L"\\browser.exe", AC::BridgeLayout::AssetsBin, inst).installed);
    CHECK(AC::LocateBridge(f.app + L"\\browser.exe", AC::BridgeLayout::AssetsBin, inst).path == f.app + L"\\assets\\bin\\browser-annotations-mcp.exe");
    CHECK(AC::LocateBridge(f.app + L"\\browser.exe", AC::BridgeLayout::Sibling, inst).path == f.app + L"\\browser-annotations-mcp.exe");
    CHECK(AC::StableBridgePath(f.app + L"\\browser.exe", inst) == f.app + L"\\browser-annotations-mcp.exe");
    // An exe in versions\ of ANOTHER root, or versions\<v>\sub\: portable rules.
    CHECK(!AC::LocateBridge(f.base + L"\\other\\versions\\1.0.0\\browser.exe", AC::BridgeLayout::AssetsBin, inst).installed);
    CHECK(!AC::LocateBridge(inst + L"\\versions\\0.3.242\\sub\\browser.exe", AC::BridgeLayout::AssetsBin, inst).installed);
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
    {   // 8. SEC-28: backups are full copies of the user's configuration: only the newest 3 are kept, others' files untouched
        for (const auto& old : Backups(f.codexHome, L"config.toml")) DeleteFileW((f.codexHome + L"\\" + old).c_str());
        for (const wchar_t* stamp : {L"20200101-000000", L"20200101-000001", L"20200101-000001-2", L"20200102-000000"}) CHECK(WriteText(f.codexHome + L"\\config.toml.browser-annotations-" + stamp + L".bak", "old"));
        CHECK(WriteText(f.codexHome + L"\\config.toml.browser-annotations-notes.bak", "mine"));   // not named like ours: kept
        f.now += 10;
        r = AC::MergeCodexToml(cfg, f.target, f.now);
        const auto kept = Backups(f.codexHome, L"config.toml");
        CHECK(r.result == AC::Result::Connected && ReadText(r.backup) == "model = \"o3\"\n");
        CHECK(kept.size() == 4 && std::find(kept.begin(), kept.end(), L"config.toml.browser-annotations-notes.bak") != kept.end()
              && std::find(kept.begin(), kept.end(), L"config.toml.browser-annotations-20200102-000000.bak") != kept.end()
              && std::find(kept.begin(), kept.end(), L"config.toml.browser-annotations-20200101-000001-2.bak") != kept.end()
              && std::find(kept.begin(), kept.end(), L"config.toml.browser-annotations-20200101-000001.bak") == kept.end());
        CHECK(AC::RemoveCodexToml(cfg, "browser-annotations", f.now).ok());
        DeleteFileW((f.codexHome + L"\\config.toml.browser-annotations-notes.bak").c_str());
    }
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
        CHECK(Backups(f.codexHome, L"config.toml").size() == std::min<std::size_t>(backups + 1, 3));   // SEC-28: the newest 3 only
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
// A configuration file that exists but cannot be read (held open without sharing by another program):
// nothing is written, and the message the Settings row and the connect panel show is in the interface
// language (it was a narrow French literal in both languages). Temporary folders only (redirected roots).
void TestUnreadableConfig(Fixture& f) {
    const std::wstring dir = f.appData + L"\\Claude", claude = dir + L"\\claude_desktop_config.json", codex = f.codexHome + L"\\config.toml";
    CreateDirectoryW(dir.c_str(), nullptr); CreateDirectoryW(f.codexHome.c_str(), nullptr);
    const std::string body = "{\"mcpServers\": {}}", toml = "model = \"o3\"\n";
    CHECK(WriteText(claude, body) && WriteText(codex, toml));
    const auto claudeBackups = Backups(dir, L"claude_desktop_config.json"), codexBackups = Backups(f.codexHome, L"config.toml");
    HANDLE lockClaude = CreateFileW(claude.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE lockCodex = CreateFileW(codex.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lockClaude != INVALID_HANDLE_VALUE && lockCodex != INVALID_HANDLE_VALUE);
    for (const auto locale : {I18n::Locale::Fr, I18n::Locale::En}) {
        I18n::SetForProcess(locale);
        const bool en = locale == I18n::Locale::En;
        const std::string claudeMsg = en ? "Claude\xE2\x80\x99s configuration can\xE2\x80\x99t be read." : "Configuration de Claude illisible.";
        const std::string codexMsg = en ? "Codex\xE2\x80\x99s configuration can\xE2\x80\x99t be read." : "Configuration de Codex illisible.";
        AC::Report r = AC::MergeClaudeConfig(claude, f.target, f.now);
        CHECK(r.result == AC::Result::Failed && r.message == claudeMsg);
        r = AC::RemoveClaudeConfig(claude, f.target.serverName, f.now);
        CHECK(r.result == AC::Result::Failed && r.message == claudeMsg);
        r = AC::MergeCodexToml(codex, f.target, f.now);
        CHECK(r.result == AC::Result::Failed && r.message == codexMsg);
        r = AC::RemoveCodexToml(codex, f.target.serverName, f.now);
        CHECK(r.result == AC::Result::Failed && r.message == codexMsg);
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    CloseHandle(lockClaude); CloseHandle(lockCodex);
    CHECK(ReadText(claude) == body && ReadText(codex) == toml);   // nothing written
    CHECK(Backups(dir, L"claude_desktop_config.json") == claudeBackups && Backups(f.codexHome, L"config.toml") == codexBackups);   // no new backup
    DeleteFileW(claude.c_str()); DeleteFileW(codex.c_str());
}

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

// Annotations are global: the agent's --store is <data root>\annotations whatever the profile. For the default
// profile that is the path existing installs already have (nothing to reconnect); a config written by an
// older build for another profile's own store (<data root>\profiles\<id>\annotations) shows « Reconnecter »,
// and reconnecting rewrites it to the global store.
// BridgeUsable: what may be written into an agent configuration (plan B: no config to a missing / foreign bridge).
std::string Patched(const std::string& pe, std::size_t at, unsigned value) {
    std::string out = pe;
    const std::uint32_t off = std::uint32_t(std::uint8_t(pe[0x3C])) | (std::uint32_t(std::uint8_t(pe[0x3D])) << 8);
    out[off + at] = char(value & 255); out[off + at + 1] = char((value >> 8) & 255);
    return out;
}
// A directory junction made by cmd's mklink (no privilege needed); true when the link exists as a reparse point.
bool Junction(const std::wstring& link, const std::wstring& target) {
    std::wstring line = L"cmd /c mklink /J \"" + link + L"\" \"" + target + L"\" >nul";
    STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; wchar_t comspec[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
    if (CreateProcessW(comspec, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    const DWORD a = GetFileAttributesW(link.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}
std::string WithLfanew(std::string bytes, std::uint32_t offset) {
    if (bytes.size() < 0x40) bytes.resize(0x40, '\0');
    for (int i = 0; i < 4; ++i) bytes[0x3C + i] = char((offset >> (8 * i)) & 255);
    return bytes;
}
void TestBridgeUsable(Fixture& f) {
    std::string why;
    CHECK(AC::BridgeUsable(f.target.bridgeExe, &why));   // the real bridge: console x64 PE
    const std::string pe = ReadText(f.target.bridgeExe);
    const std::wstring dir = f.base + L"\\bridges";
    CreateDirectoryW(dir.c_str(), nullptr);
    struct Bad { const wchar_t* name; std::string bytes; };
    const Bad bad[] = {{L"\\text.exe", "MZ"}, {L"\\empty.exe", ""}, {L"\\gui.exe", Patched(pe, 24 + 68, 2)}, {L"\\x86.exe", Patched(pe, 4, 0x14C)},
                       {L"\\nope-pe.exe", Patched(pe, 0, 0x4C4E)}};
    for (const auto& b : bad) {
        CHECK(WriteText(dir + b.name, b.bytes));
        why.clear();
        CHECK(!AC::BridgeUsable(dir + b.name, &why) && !why.empty());
    }
    CHECK(!AC::BridgeUsable(dir + L"\\absent.exe", &why) && why.find("introuvable") != std::string::npos);
    CreateDirectoryW((dir + L"\\folder.exe").c_str(), nullptr);
    CHECK(!AC::BridgeUsable(dir + L"\\folder.exe", &why));
    CHECK(!AC::BridgeUsable(L"browser-annotations-mcp.exe", &why) && !AC::BridgeUsable(L"\\\\server\\share\\b.exe", &why));
    CHECK(!AC::BridgeUsable(dir + L"\\..\\bridges\\text.exe", &why) && !AC::BridgeUsable(L"", &why));
    // A bin folder that is a junction (reparse point) to a real bridge: refused.
    const std::wstring real = f.base + L"\\real-bin", link = f.base + L"\\link-bin";
    CreateDirectoryW(real.c_str(), nullptr);
    CHECK(CopyTo(f.target.bridgeExe, real + L"\\browser-annotations-mcp.exe"));
    const std::wstring cmd = L"cmd /c mklink /J \"" + link + L"\" \"" + real + L"\" >nul";
    STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; std::wstring line = cmd; wchar_t comspec[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
    const bool made = CreateProcessW(comspec, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (made) { WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    const DWORD attr = GetFileAttributesW(link.c_str());
    CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT));   // the junction exists
    CHECK(AC::BridgeUsable(real + L"\\browser-annotations-mcp.exe", &why));
    CHECK(!AC::BridgeUsable(link + L"\\browser-annotations-mcp.exe", &why) && why.find("lien") != std::string::npos);
    RemoveDirectoryW(link.c_str());
    // Review of 65adadd, P2: a junction ABOVE bin (bin and the file themselves plain) was followed. Every ancestor counts.
    const std::wstring realRoot = f.base + L"\\real-root", aliasRoot = f.base + L"\\alias-root", deep = f.base + L"\\deep";
    for (const std::wstring& d : {realRoot, realRoot + L"\\bin", deep, deep + L"\\x"}) CreateDirectoryW(d.c_str(), nullptr);
    CHECK(CopyTo(f.target.bridgeExe, realRoot + L"\\bin\\browser-annotations-mcp.exe"));
    CHECK(AC::BridgeUsable(realRoot + L"\\bin\\browser-annotations-mcp.exe", &why));
    CHECK(Junction(aliasRoot, realRoot) && Junction(deep + L"\\x\\link", realRoot));
    const DWORD binAttr = GetFileAttributesW((aliasRoot + L"\\bin").c_str());
    CHECK(binAttr != INVALID_FILE_ATTRIBUTES && !(binAttr & FILE_ATTRIBUTE_REPARSE_POINT));   // bin itself is plain
    why.clear();
    CHECK(!AC::BridgeUsable(aliasRoot + L"\\bin\\browser-annotations-mcp.exe", &why) && why.find("lien") != std::string::npos);
    CHECK(!AC::BridgeUsable(deep + L"\\x\\link\\bin\\browser-annotations-mcp.exe", &why) && why.find("lien") != std::string::npos);
    RemoveDirectoryW((deep + L"\\x\\link").c_str());
    // One spelling only: ".", empty component, trailing separator.
    CHECK(!AC::BridgeUsable(realRoot + L"\\.\\bin\\browser-annotations-mcp.exe", &why));
    CHECK(!AC::BridgeUsable(realRoot + L"\\\\bin\\browser-annotations-mcp.exe", &why));
    CHECK(!AC::BridgeUsable(realRoot + L"\\bin\\", &why));
    // Review of 65adadd, P2: e_lfanew near UINT32_MAX wrapped the 32-bit bound and crashed. Extreme and edge offsets are
    // refused (no crash), the real bridge truncated just before / at the end of the checked header behaves accordingly.
    for (std::uint32_t off : {0xFFFFFFF0u, 0xFFFFFFFFu, 0xFFFFFFA0u, 0xFFFFFF00u, 0x80000000u, 0x7FFFFFFFu, 0x10000u, 0x1000u, 0xFFFu, 0x3Fu}) {
        CHECK(WriteText(dir + L"\\lfanew.exe", WithLfanew(std::string("MZ") + std::string(126, '\0'), off)));
        why.clear();
        CHECK(!AC::BridgeUsable(dir + L"\\lfanew.exe", &why) && why.find("programme valide") != std::string::npos);
    }
    const std::uint32_t lfanew = std::uint32_t(std::uint8_t(pe[0x3C])) | (std::uint32_t(std::uint8_t(pe[0x3D])) << 8);
    CHECK(WriteText(dir + L"\\cut93.exe", pe.substr(0, lfanew + 93)) && !AC::BridgeUsable(dir + L"\\cut93.exe", &why));
    CHECK(WriteText(dir + L"\\cut94.exe", pe.substr(0, lfanew + 94)) && AC::BridgeUsable(dir + L"\\cut94.exe", &why));   // a header screen
    std::string big = pe; big.resize(8192, '\0');
    for (std::uint32_t off : {4096u - 94u, 4096u - 93u, 4096u - 4u, 4096u}) {
        CHECK(WriteText(dir + L"\\edge.exe", WithLfanew(big, off)) && !AC::BridgeUsable(dir + L"\\edge.exe", &why));
    }
}
// Connect with an unusable bridge: refused BEFORE anything is written (no config, no backup, no CLI, no .mcpb).
void TestConnectRefusesMissingBridge(Fixture& f) {
    AC::Target gone = f.target;
    gone.bridgeExe = f.base + L"\\inst\\bin\\browser-annotations-mcp.exe";   // installed, bin not published
    CreateDirectoryW((f.appData + L"\\Claude").c_str(), nullptr);
    const std::wstring claude = AC::ClaudeConfigPath(f.env), codex = AC::CodexConfigPath(f.env), log = f.codexHome + L"\\fake-codex.log";
    CHECK(WriteText(claude, "{\n  \"mine\": true\n}\n"));
    const std::string claudeBefore = ReadText(claude), codexBefore = ReadText(codex), logBefore = ReadText(log);
    const std::size_t opened = f.opened.size();
    DeleteFileW((f.work + L"\\browser-annotations.mcpb").c_str());   // left by the Claude flow above
    f.associated = true;   // even with .mcpb associated: no bundle is built for a missing bridge
    AC::Report r = AC::ConnectClaude(f.env, gone, f.work);
    CHECK(r.result == AC::Result::Failed && !r.ok() && r.message.find("introuvable") != std::string::npos && r.backup.empty());
    CHECK(ReadText(claude) == claudeBefore && f.opened.size() == opened && !Exists(f.work + L"\\browser-annotations.mcpb"));
    r = AC::ConnectCodex(f.env, gone);
    CHECK(r.result == AC::Result::Failed && !r.usedCli && r.backup.empty());
    CHECK(ReadText(codex) == codexBefore && ReadText(log) == logBefore);
    // The same with a real bridge reached through an ANCESTOR junction (review of 65adadd): refused, nothing written.
    const std::wstring viaLink = f.base + L"\\alias-root\\bin\\browser-annotations-mcp.exe";
    CHECK(Exists(viaLink));
    AC::Target linked = f.target; linked.bridgeExe = viaLink;
    r = AC::ConnectClaude(f.env, linked, f.work);
    CHECK(r.result == AC::Result::Failed && r.message.find("lien") != std::string::npos && r.backup.empty());
    CHECK(ReadText(claude) == claudeBefore && f.opened.size() == opened && !Exists(f.work + L"\\browser-annotations.mcpb"));
    r = AC::ConnectCodex(f.env, linked);
    CHECK(r.result == AC::Result::Failed && !r.usedCli && r.backup.empty() && ReadText(codex) == codexBefore && ReadText(log) == logBefore);
    RemoveDirectoryW((f.base + L"\\alias-root").c_str());
    f.associated = false;   // the merge path below (never Claude's installer here)
    // The same bridge once published: accepted (the merge path, in test mode never Claude's installer).
    CreateDirectoryW((f.base + L"\\inst\\bin").c_str(), nullptr);
    CHECK(CopyTo(f.target.bridgeExe, gone.bridgeExe));
    r = AC::ConnectClaude(f.env, gone, f.work);
    CHECK(r.result == AC::Result::Connected && AC::ReadClaudeEntry(claude, gone.serverName).command == gone.bridgeExe);
    CHECK(AC::RemoveClaudeConfig(claude, gone.serverName, f.now).ok());
}

void TestGlobalStorePath(Fixture& f) {
    CreateDirectoryW((f.appData + L"\\Claude").c_str(), nullptr);
    const std::wstring cfg = AC::ClaudeConfigPath(f.env);
    CHECK(!cfg.empty());
    AC::Target global = f.target;
    global.storeRoot = BA::GlobalStoreRoot(f.app);
    CHECK(global.storeRoot == f.app + L"\\annotations");
    AC::Target legacyDefault = f.target;   // what a pre-profiles build (or the default profile) wrote: the same folder
    legacyDefault.storeRoot = BA::StoreRootForDataDir(f.app);
    CHECK(WriteText(cfg, "{}") && AC::MergeClaudeConfig(cfg, legacyDefault, f.now).ok());
    AC::Status st = AC::ClientStatus(f.env, AC::Client::Claude, global, {});
    CHECK(st.configured && !st.stalePath && st.connected());   // unchanged path: no « Reconnecter » for existing installs
    AC::Target perProfile = f.target;
    perProfile.storeRoot = BA::StoreRootForDataDir(f.app + L"\\profiles\\ptravail01");
    CHECK(AC::MergeClaudeConfig(cfg, perProfile, f.now).ok());
    st = AC::ClientStatus(f.env, AC::Client::Claude, global, {});
    CHECK(st.configured && st.stalePath && st.connected());    // « Connecté à un ancien emplacement » + Reconnecter
    CHECK(AC::MergeClaudeConfig(cfg, global, f.now).result == AC::Result::Connected);
    const auto entry = AC::ReadClaudeEntry(cfg, "browser-annotations");
    CHECK(entry.args.size() == 2 && entry.args[1] == global.storeRoot);
    st = AC::ClientStatus(f.env, AC::Client::Claude, global, {});
    CHECK(st.configured && !st.stalePath);
    // Codex: the same rule on config.toml.
    CreateDirectoryW(AC::CodexHome(f.env).c_str(), nullptr);
    const std::wstring toml = AC::CodexConfigPath(f.env);
    CHECK(WriteText(toml, "# mine\n") && AC::MergeCodexToml(toml, perProfile, f.now).ok());
    st = AC::ClientStatus(f.env, AC::Client::Codex, global, {});
    CHECK(st.configured && st.stalePath);
    CHECK(AC::MergeCodexToml(toml, global, f.now).ok());
    st = AC::ClientStatus(f.env, AC::Client::Codex, global, {});
    CHECK(st.configured && !st.stalePath && ReadText(toml).rfind("# mine\n", 0) == 0);
    CHECK(AC::RemoveCodexToml(toml, f.target.serverName, f.now).ok() && AC::RemoveClaudeConfig(cfg, f.target.serverName, f.now).ok());
}

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
    TestBridgeUsable(f);
    TestConnectRefusesMissingBridge(f);
    TestGlobalStorePath(f);
    TestUnreadableConfig(f);
    TestGuard(f);
    return Finish("agent_connect_test");
}
