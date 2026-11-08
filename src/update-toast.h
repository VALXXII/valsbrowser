#pragma once
// Native update toast: one opaque, owned, non-activating Direct2D popup.
// Contract: build/coordination/update-toast-contract.md. UI thread only.
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include "update-types.h"

namespace BrowserUpdate {
struct ToastOptions {
    float edgeInset = 14;              // DIP from the owner client's left and bottom edges (Paper)
    float topReserve = 81;             // DIP of chrome the card must never cover (121 with the bookmarks band)
    bool allowCompositorClock = true;  // false = DWM vblank timing (fixtures only)
    int forceReducedMotion = -1;       // -1 follow Windows "Show animations"; 0/1 override
};
class Toast {
public:
    using ActionHandler = std::function<void(Action)>;
    using Options = ToastOptions;
    Toast();
    ~Toast();
    Toast(const Toast&) = delete;
    Toast& operator=(const Toast&) = delete;

    bool Init(HWND owner, const std::wstring& assetRoot, ActionHandler onAction, Options options = {});
    void SetView(const View& view);
    void Layout();
    void SetOwnerVisible(bool visible);
    // Re-read the reduced-motion preference (Windows' switch or the app's own setting).
    void SyncReducedMotion();
    // DIP on the owner's right the card must avoid (e.g. an open downloads drawer). Default 0.
    void SetReservedRight(float dip);
    // DIP of chrome above the page (the bookmarks band grows it from 81 to 121).
    void SetTopReserve(float dip);
    bool Focus();
    void SetFocusReleaseHandler(std::function<void()> handler);
    void Close();
    HWND Window() const;
    bool Shown() const;
    bool Fits() const;

    struct Impl;  // defined in update-toast.cpp (tests include the .cpp for white-box checks)
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace BrowserUpdate
