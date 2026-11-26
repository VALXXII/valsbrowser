#pragma once
#include <cstdint>
#include <string>

// Shared presentation contract. Backend, native toast and integration have separate owners.
namespace BrowserUpdate {
enum class Phase { Hidden, Checking, Available, Downloading, Ready, Installing, Installed, Error };
// Later (« Plus tard »): the host hides the card for this session only; the engine keeps its view and saves nothing.
enum class Action { Download, Cancel, RestartInstall, Retry, Dismiss, Later };
struct View {
    Phase phase=Phase::Hidden;
    std::wstring version, error;
    std::uint64_t received=0, total=0;
    double secondsRemaining=-1;
};
}
