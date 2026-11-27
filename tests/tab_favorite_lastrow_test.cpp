// Fix's check (run by test-tab-drag.ps1): the grey place a tab dragged onto the side panel's favorites opens
// (TabFavorite::GridOpened, PR #152) must be the grid as Sidebar::MakeGrid lays it out with one more tile, so that the
// place and the tiles land exactly where the new favorite will be: every row sharing the width, the last one too
// (Val 26 nov.). Every count 1 .. 12, every slot, every device pixel of 220 .. 360 at 100 / 125 / 150 %.
#include "../src/tab-favorite-drop.h"
#include <cmath>
#include <cstdio>

namespace S = NativeChrome::Sidebar;
static bool Same(const S::Box& a, const S::Box& b) { return std::fabs(a.left - b.left) < 1e-3f && std::fabs(a.top - b.top) < 1e-3f && std::fabs(a.right - b.right) < 1e-3f && std::fabs(a.bottom - b.bottom) < 1e-3f; }

int main() {
    int cases = 0, bad = 0; const float scales[] = {1, 1.25f, 1.5f};
    for (float s : scales) for (int px = static_cast<int>(std::ceil(220 * s - 1e-3f)); px <= static_cast<int>(std::floor(360 * s + 1e-3f)); ++px) {
        const float w = px / s - 16;
        for (int m = 1; m <= 12; ++m) {
            const auto grid = S::MakeGrid(8, 100, w, m, 5, 0), after = S::MakeGrid(8, 100, w, m + 1, 5, 0);
            for (int slot = 0; slot <= m; ++slot) {
                const auto o = TabFavorite::GridOpened(grid, slot); ++cases;
                bool ok = Same(o.slot, after.tiles[static_cast<std::size_t>(slot)]) && o.tiles.size() == static_cast<std::size_t>(m);
                for (int i = 0; ok && i < m; ++i) ok = Same(o.tiles[static_cast<std::size_t>(i)], after.tiles[static_cast<std::size_t>(i < slot ? i : i + 1)]);
                if (!ok && ++bad <= 4) std::printf("  scale %.2f, %d px, %d favorites, place at %d: place %.1f..%.1f, the grid with it %.1f..%.1f\n", s, px, m, slot, o.slot.left, o.slot.right, after.tiles[static_cast<std::size_t>(slot)].left, after.tiles[static_cast<std::size_t>(slot)].right);
            }
        }
    }
    std::printf("%s: grey place = the grid with one more tile (%d cases, %d wrong)\n", bad ? "FAIL" : "PASS", cases, bad);
    return bad ? 1 : 0;
}
