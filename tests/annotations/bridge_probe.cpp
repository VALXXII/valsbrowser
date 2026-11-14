// BridgeUsable probe in its own process (review of 65adadd): a crash shows as an exit code, not as a lost test run.
// Usage: bridge_probe.exe <path>  ->  prints « BridgeUsable=<0|1> reason=<…> », exit 0 (usable) / 1 (refused).
#include "agent-connect.h"
#include <cstdio>
#include <windows.h>
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc != 2) return 2;
    std::string why;
    const bool ok = AgentConnect::BridgeUsable(argv[1], &why);
    std::printf("BridgeUsable=%d reason=%s\n", int(ok), why.c_str());
    return ok ? 0 : 1;
}
