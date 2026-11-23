#pragma once
// One Paper shader node on a Direct2D surface: the frame Onboarding::Shaders::Renderer read back (premultiplied BGRA)
// kept as a bitmap of the render target that draws it. Shared by the onboarding window (src/onboarding.h: the
// halftone logo, the gem, the grain) and the Réglages drawer (src/settings-panel.h: the « navigateur par défaut »
// card, the same halftone logo shader).
#include <windows.h>
#include <d2d1.h>
#include <cstddef>
#include "motion.h"
#include "onboarding-shaders.h"

namespace Onboarding {
namespace Shaders {

struct Layer {
    ID2D1Bitmap* bitmap = nullptr; ID2D1RenderTarget* owner = nullptr; Frame frame; bool fresh = false; NativeChrome::Motion::Time shaded{}; float key = 0;
    void Drop() { if (bitmap) { bitmap->Release(); bitmap = nullptr; } owner = nullptr; fresh = false; shaded = {}; }
    // `part`: the pixels of `frame` that changed (the whole frame when the bitmap is new).
    bool Upload(ID2D1RenderTarget* t, const RECT* part = nullptr) {
        if (!t || !frame.width || !frame.height) return false;
        if (bitmap && owner == t) { const auto size = bitmap->GetPixelSize(); if (size.width != frame.width || size.height != frame.height) Drop(); }
        if (!bitmap || owner != t) {
            Drop();
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            if (FAILED(t->CreateBitmap(D2D1::SizeU(frame.width, frame.height), frame.bgra.data(), frame.width * 4, props, &bitmap))) { bitmap = nullptr; return false; }
            owner = t;
        } else if (part) {
            const D2D1_RECT_U to{static_cast<UINT32>(part->left), static_cast<UINT32>(part->top), static_cast<UINT32>(part->right), static_cast<UINT32>(part->bottom)};
            bitmap->CopyFromMemory(&to, frame.bgra.data() + (static_cast<std::size_t>(part->top) * frame.width + part->left) * 4, frame.width * 4);
        } else bitmap->CopyFromMemory(nullptr, frame.bgra.data(), frame.width * 4);
        fresh = true; return true;
    }
};

} // namespace Shaders
} // namespace Onboarding
