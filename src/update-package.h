#pragma once
// Update manifest + package (.lsrupd) parsing, path policy, SHA-256 and staged-tree verification.
// Shared by browser.exe (update-core) and update-helper.exe. Win32 + BCrypt only.
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace UpdatePackage {

constexpr std::uint64_t MaxManifestBytes = 1024;
constexpr std::uint64_t MaxPackageBytes = 512ull * 1024 * 1024;
constexpr std::uint64_t MaxEntryBytes = 256ull * 1024 * 1024;
constexpr std::uint32_t MaxEntries = 4096;
constexpr std::size_t MaxPathBytes = 240;
constexpr const char* Arch = "x64";
constexpr const wchar_t* StageListName = L".stage";

using Digest = std::array<std::uint8_t, 32>;

struct Version {
    std::uint32_t major = 0, minor = 0, build = 0;
};
// Strict "M.m.B": decimal, no leading zeros except "0", 1..9 digits per field.
bool ParseVersion(const std::string& text, Version& out);
bool ParseVersion(const std::wstring& text, Version& out);
int CompareVersion(const Version& a, const Version& b);
bool IsZero(const Version& v);

struct Manifest {
    std::string version, arch, package;
    std::uint64_t size = 0;
    Digest sha256{};
};
std::string PackageFileName(const std::string& version);  // browser-<v>-win-x64.lsrupd
constexpr const char* ManifestAssetName = "update-manifest-win-x64.txt";
bool ParseManifest(const std::string& text, Manifest& out, std::string& error);
std::string FormatManifest(const Manifest& m);

// "owner/repo" with [A-Za-z0-9._-], 1..100 chars per part, no "." / ".." parts.
bool ValidRepository(const std::wstring& repo);

// SHA-256 (BCrypt).
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    bool Ok() const { return hash_ != nullptr; }
    void Update(const void* data, std::size_t size);
    Digest Final();
private:
    void* alg_ = nullptr;
    void* hash_ = nullptr;
    std::vector<std::uint8_t> object_;
};
Digest Sha256Of(const std::string& data);
bool Sha256File(const std::wstring& path, Digest& out, std::uint64_t& size);
std::string Hex(const Digest& d);
bool ParseHex(const std::string& hex, Digest& out);

// Path policy for package entries ('/'-separated relative paths).
bool ValidEntryPath(const std::string& path, std::string& error);
bool AllowedPayloadPath(const std::string& path);

struct Entry {
    std::string path;
    std::uint64_t size = 0, offset = 0;
    Digest sha256{};
};
struct PackageIndex {
    std::string version, arch;
    std::vector<Entry> entries;
};
// Validates the full structure (header, entry paths, allowlist, duplicates, sizes, trailer, exact EOF).
bool ReadPackageIndex(const std::wstring& file, PackageIndex& out, std::string& error);

// Extracts into stagingDir (must not exist; its parent must exist and not be a reparse point).
// Every created file is verified to resolve inside stagingDir and to match its entry hash.
// Writes the StageListName file last. On failure the partial directory is removed.
bool ExtractPackage(const std::wstring& file, const std::string& expectedVersion, const std::wstring& stagingDir,
                    const std::atomic<bool>* cancel, std::string& error);

// Re-verifies a staged/installed tree: no reparse points, files exactly == stage list, hashes match.
bool VerifyStagedTree(const std::wstring& dir, const std::string& expectedVersion, std::string& error);

// Test/tool helper: builds a package file from (path, content) pairs. No policy checks beyond format.
bool WritePackage(const std::wstring& out, const std::string& version, const std::string& arch,
                  const std::vector<std::pair<std::string, std::string>>& files);

// Small filesystem helpers.
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);
bool ReadSmallFile(const std::wstring& path, std::uint64_t maxBytes, std::string& out);
bool AtomicWriteFile(const std::wstring& path, const std::string& content);
bool IsReparsePoint(const std::wstring& path);
bool PathExists(const std::wstring& path);
bool IsDirectory(const std::wstring& path);
bool RemoveTree(const std::wstring& path);  // never follows reparse points
// Renames a directory; never replaces an existing destination. On-access scanners and the search indexer briefly
// open files just written into `from`, and NTFS refuses to rename a directory while any file under it is open:
// ERROR_ACCESS_DENIED / ERROR_SHARING_VIOLATION / ERROR_LOCK_VIOLATION are retried until `timeoutMs`, any other
// error fails at once. On failure nothing moved and GetLastError() is the last rename error. `attempts` (optional)
// receives the number of rename calls made.
bool MoveDirectory(const std::wstring& from, const std::wstring& to, unsigned long timeoutMs, int* attempts = nullptr);
bool IsUnder(const std::wstring& child, const std::wstring& root);  // lexical, case-insensitive, normalized
std::wstring FinalPath(const std::wstring& path);  // GetFinalPathNameByHandle without \\?\ prefix, "" on failure
}  // namespace UpdatePackage
