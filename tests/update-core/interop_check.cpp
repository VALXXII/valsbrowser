// interop_check.exe <package> <manifest> <stagingParent>: C++ validation of a package produced by the Python packer.
#include <windows.h>

#include <cstdio>

#include "../../src/update-package.h"

namespace UP = UpdatePackage;

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    std::string text, error;
    UP::Manifest m;
    if (!UP::ReadSmallFile(argv[2], UP::MaxManifestBytes, text) || !UP::ParseManifest(text, m, error))
        return std::printf("manifest: %s\n", error.c_str()), 3;
    UP::Digest d;
    std::uint64_t size = 0;
    if (!UP::Sha256File(argv[1], d, size) || size != m.size || d != m.sha256) return std::printf("hash/size mismatch\n"), 4;
    std::wstring stage = std::wstring(argv[3]) + L"\\" + UP::Widen(m.version);
    UP::RemoveTree(stage);
    if (!UP::ExtractPackage(argv[1], m.version, stage, nullptr, error)) return std::printf("extract: %s\n", error.c_str()), 5;
    if (!UP::VerifyStagedTree(stage, m.version, error)) return std::printf("verify: %s\n", error.c_str()), 6;
    std::printf("interop ok %s %llu bytes\n", m.version.c_str(), (unsigned long long)m.size);
    return 0;
}
