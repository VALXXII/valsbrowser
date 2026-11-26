#pragma once
// Trial of the update card in a design-preview copy ONLY (tools/release/build.py --preview defines BROWSER_PREVIEW;
// release and dev builds never compile this file in): `browser.exe --carte-maj` plays a fake update source through the
// real card — the same views the engine publishes (src/update-core.cpp), the same actions. Nothing is downloaded,
// nothing is installed, nothing is saved. `--carte-maj=erreur` makes the fake download fail at 40 % (the error card).
//   1.5 s after start      « Mise à jour disponible »
//   Télécharger            « Téléchargement en cours… »: 200,0 Mo at ~20 Mo/s, remaining time from a smoothed rate
//                          (the engine's: rate × 0.7 + new × 0.3 every 250 ms, shown after 1 s and 64 Ko)
//   Annuler                back to « disponible »
//   end of the download    « Prête à installer »
//   Redémarrer et installer / Plus tard / Ignorer: the card leaves, and (demo only) comes back as « disponible »
//                          after 4 s, so the trial can be replayed (the real « Plus tard » waits for the next start).
#include "update-types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BrowserUpdate {
class CardDemo {
public:
    static constexpr std::uint64_t Total = 200ull * 1048576;   // Paper « 200,0 Mo »
    static constexpr double Rate = 20.0 * 1048576;             // bytes per second, about 10 s for the whole package
    void Start(std::uint64_t ms, bool failing) { failing_ = failing; phase_ = Phase::Hidden; offerAt_ = ms + 1500; }
    void Act(Action a, std::uint64_t ms) {
        switch (a) {
        case Action::Download: if (phase_ == Phase::Available) Begin(ms); break;
        case Action::Retry: if (phase_ == Phase::Error) Begin(ms); break;
        case Action::Cancel: if (phase_ == Phase::Downloading) { phase_ = Phase::Available; } break;
        case Action::RestartInstall: case Action::Later: case Action::Dismiss:
            if (phase_ == Phase::Available || phase_ == Phase::Ready || phase_ == Phase::Error) { phase_ = Phase::Hidden; offerAt_ = ms + 4000; }
            break;
        }
    }
    View Step(std::uint64_t ms) {
        if (phase_ == Phase::Hidden && offerAt_ && ms >= offerAt_) { phase_ = Phase::Available; offerAt_ = 0; }
        if (phase_ == Phase::Downloading) Advance(ms);
        View v; v.phase = phase_; v.version = L"démo";   // i18n-exempt: never drawn (the card shows no version)
        v.total = Total;
        if (phase_ == Phase::Downloading) { v.received = received_; v.secondsRemaining = eta_; }
        if (phase_ == Phase::Ready) v.received = Total;
        if (phase_ == Phase::Error) v.error = L"Démonstration : le téléchargement a échoué à 40 %.";   // i18n-exempt: preview-copy trial only, never in a release
        return v;
    }
private:
    Phase phase_ = Phase::Hidden;
    bool failing_ = false;
    std::uint64_t offerAt_ = 0, start_ = 0, last_ = 0, rateAt_ = 0, rateBytes_ = 0, received_ = 0;
    double rate_ = 0, eta_ = -1;
    void Begin(std::uint64_t ms) { phase_ = Phase::Downloading; start_ = last_ = rateAt_ = ms; received_ = rateBytes_ = 0; rate_ = 0; eta_ = -1; }
    void Advance(std::uint64_t ms) {
        // A network that breathes a little (±15 %), so the smoothed time moves like a real one.
        const double dt = (ms - last_) / 1000.0, wobble = 1 + .15 * std::sin((ms - start_) / 700.0);
        last_ = ms;
        received_ = std::min<std::uint64_t>(Total, received_ + static_cast<std::uint64_t>(Rate * wobble * dt));
        if (ms - rateAt_ >= 250) {
            const double inst = double(received_ - rateBytes_) * 1000.0 / double(ms - rateAt_);
            rate_ = rate_ <= 0 ? inst : rate_ * 0.7 + inst * 0.3;
            rateAt_ = ms; rateBytes_ = received_;
        }
        eta_ = (ms - start_ >= 1000 && received_ >= 64 * 1024 && rate_ > 1) ? double(Total - received_) / rate_ : -1;
        if (failing_ && received_ >= Total * 2 / 5) { phase_ = Phase::Error; failing_ = false; return; }
        if (received_ >= Total) phase_ = Phase::Ready;
    }
};
} // namespace BrowserUpdate
