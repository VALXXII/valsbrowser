#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <WebView2.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <filesystem>
#include <mutex>
#include "session.h"
#include <cwctype>
#include <cmath>
#include <dwmapi.h>
#include "chrome.h"
#include "native-ui.h"
#include "frame-clock.h"
#include "download-panel.h"
#include "history-panel.h"
#include "settings.h"
#include "address.h"
#include "address-popup.h"
#include "settings-panel.h"
#include "ui-language.h" // browser-wide interface language: read once at startup, saved from Réglages › Langue
#include "context-menu.h"
#include "accessibility.h"
#include "bookmarks.h"
#include "bookmark-popover.h"
#include "bookmark-toast.h"
#include "bookmarks-panel.h"
#include "bookmark-drag.h"
#include "bookmark-promote.h"
#include "bookmark-move.h"
#include "inline-rename.h"
#include "split-view.h"
#include "split-overlay.h"
#include "status-bubble.h"
#include "site-name.h"
#include "debug-sandbox.h"
#include "motion-lab.h"
#include "webview2-runtime.h"
#include "webview2-install.h"
#include <wrl/client.h>
#include "viewport-scrollbar.h"
#include "content-transition.h"
#include <shlobj.h>
#include <atomic>
#include <thread>
// Self-update host integration is compiled into identity-stamped builds only
// (tools/release/build.py defines BROWSER_HAS_BUILD_INFO). Unstamped builds and the
// native fixtures that include this file keep the portable, update-free behaviour.
#ifdef BROWSER_HAS_BUILD_INFO
#define BROWSER_UPDATES 1
#include "build-info.h"
#include "update-core.h"
#include "update-toast.h"
#endif
// Annotation mode (src/annotation-mode.h): identity-stamped builds that ship the MCP bridge. Dev / preview builds have
// it; a --channel release build only when it opts in with BROWSER_RELEASE_ANNOTATIONS, which the release pipeline
// (tools/release/build.py) passes only once the bridge is in the payload (assets\bin) and published to <root>\bin by the
// update helper (public download plan B, items 1-6). Without it a release build keeps its exact previous behaviour:
// no annotation UI ever ships without its bridge. Unstamped fixtures: no annotation mode.
#if defined(BROWSER_HAS_BUILD_INFO) && !defined(BROWSER_NO_ANNOTATIONS) && (!defined(BROWSER_CHANNEL_RELEASE) || defined(BROWSER_RELEASE_ANNOTATIONS))
#define BROWSER_ANNOTATIONS 1
#endif
#include "profile-store.h"
#include "window-sessions.h"
#include "trace-erase.h"
#include "log-privacy.h"
#include "password-vault.h"
#include "form-memory.h"   // #121: typed-info store, the forms world, placement (design/security-review-autofill-under-field.md)
#include "profiles.h"   // Profiles::ValidId for the private window command line (review R-2)
#ifdef BROWSER_E2E_FIXTURE_HOOK
// TEST HOST ONLY (tests/e2e/mtls_host.cpp): the fixture pin. The release and dev browser never define
// BROWSER_E2E_FIXTURE_HOOK, so none of this exists in them (checked by tests/e2e/mtls_e2e.ps1).
#include "e2e-fixture-pin.h"
#endif

// COM callbacks use the official SDK interface IDs, independent of compiler UUID extensions.
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler,"{02fab84b-1428-4fb7-ad45-1b2e64736184}")   /* chat: visual hosting (src/chat-panel-view.h) */
GUID_TEXT(ICoreWebView2CursorChangedEventHandler,"{9da43ccc-26e1-4dad-b56c-d8961c94c571}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2SourceChangedEventHandler,"{3c067f9f-5388-4772-8b48-79f7ef1ab37c}")
GUID_TEXT(ICoreWebView2HistoryChangedEventHandler,"{c79a420c-efd9-4058-9295-3e8b4bcab645}")
GUID_TEXT(ICoreWebView2DocumentTitleChangedEventHandler,"{f5f2b923-953e-4042-9f95-f3a118e1afd4}")
GUID_TEXT(ICoreWebView2NewWindowRequestedEventHandler,"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}")
GUID_TEXT(ICoreWebView2WindowCloseRequestedEventHandler,"{5c19e9e0-092f-486b-affa-ca8231913039}")
GUID_TEXT(ICoreWebView2AcceleratorKeyPressedEventHandler,"{b29c7e28-fa79-41a8-8e44-65811c76dcb2}")
GUID_TEXT(ICoreWebView2ProcessFailedEventHandler,"{79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1}")
GUID_TEXT(ICoreWebView2BrowserProcessExitedEventHandler,"{fa504257-a216-4911-a860-fe8825712861}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2FaviconChangedEventHandler,"{2913da94-833d-4de0-8dca-900fc524a1a4}")
GUID_TEXT(ICoreWebView2FocusChangedEventHandler,"{05ea24bd-6452-4926-9014-4b82b498135d}")
GUID_TEXT(ICoreWebView2CapturePreviewCompletedHandler,"{697e05e9-3d8f-45fa-96f4-8ffe1ededaf5}")
GUID_TEXT(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,"{5c4889f0-5ef6-4c5a-952c-d8f1b92d0574}")
GUID_TEXT(ICoreWebView2DevToolsProtocolEventReceivedEventHandler,"{e2fda4be-5456-406c-a261-3d452138362c}")
GUID_TEXT(ICoreWebView2GetFaviconCompletedHandler,"{a2508329-7da8-49d7-8c05-fa125e4aee8d}")
GUID_TEXT(ICoreWebView2NavigationStartingEventHandler,"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}")
GUID_TEXT(ICoreWebView2FindStartCompletedHandler,"{6a90ecaf-44b0-5bd9-8f07-1967e17be9fb}")
GUID_TEXT(ICoreWebView2DownloadStartingEventHandler,"{efedc989-c396-41ca-83f7-07f845a55724}")
GUID_TEXT(ICoreWebView2BytesReceivedChangedEventHandler,"{828e8ab6-d94c-4264-9cef-5217170d6251}")
GUID_TEXT(ICoreWebView2EstimatedEndTimeChangedEventHandler,"{28f0d425-93fe-4e63-9f8d-2aeec6d3ba1e}")
GUID_TEXT(ICoreWebView2StateChangedEventHandler,"{81336594-7ede-4ba9-bf71-acf0a95b58dd}")
GUID_TEXT(ICoreWebView2ContextMenuRequestedEventHandler,"{04d3fe1d-ab87-42fb-a898-da241d35b63c}")
GUID_TEXT(ICoreWebView2ServerCertificateErrorDetectedEventHandler,"{969b3a26-d85e-4795-8199-fef57344da22}")
GUID_TEXT(ICoreWebView2ClearBrowsingDataCompletedHandler,"{e9710a06-1d1d-49b2-8234-226f35846ae5}")
GUID_TEXT(ICoreWebView2StatusBarTextChangedEventHandler,"{a5e3b0d0-10df-4156-bfad-3b43867acac6}")
GUID_TEXT(ICoreWebView2TrySuspendCompletedHandler,"{00f206a7-9d17-4605-91f6-4e8e4de192e3}")
GUID_TEXT(ICoreWebView2PrintToPdfCompletedHandler,"{ccf1ef04-fd8e-4d5f-b2de-0983e41b8c36}")
GUID_TEXT(ICoreWebView2GetNonDefaultPermissionSettingsCompletedHandler,"{38274481-a15c-4563-94cf-990edc9aeb95}")
GUID_TEXT(ICoreWebView2SetPermissionStateCompletedHandler,"{fc77fb30-9c9e-4076-b8c7-7644a703ca1b}")
GUID_TEXT(ICoreWebView2ProfileGetBrowserExtensionsCompletedHandler,"{fce16a1c-f107-4601-8b75-fc4940ae25d0}")
GUID_TEXT(ICoreWebView2ProfileAddBrowserExtensionCompletedHandler,"{df1aab27-82b9-4ab6-aae8-017a49398c14}")
GUID_TEXT(ICoreWebView2BrowserExtensionEnableCompletedHandler,"{30c186ce-7fad-421f-a3bc-a8eaf071ddb8}")
GUID_TEXT(ICoreWebView2BrowserExtensionRemoveCompletedHandler,"{8e41909a-9b18-4bb1-8cdf-930f467a50be}")
GUID_TEXT(ICoreWebView2WebMessageReceivedEventHandler,"{57213f19-00e6-49fa-8e07-898ea01ecbd2}")
GUID_TEXT(ICoreWebView2ScriptDialogOpeningEventHandler,"{ef381bf9-afa8-4e37-91c4-8ac48524bdfb}")
GUID_TEXT(ICoreWebView2PermissionRequestedEventHandler,"{15e1c6a3-c72a-4df3-91d7-d097fbec6bfd}")
GUID_TEXT(ICoreWebView2BasicAuthenticationRequestedEventHandler,"{58b4d6c2-18d4-497e-b39b-9a96533fa278}")
GUID_TEXT(ICoreWebView2ClientCertificateRequestedEventHandler,"{d7175ba2-bcc3-11eb-8529-0242ac130003}")
GUID_TEXT(ICoreWebView2LaunchingExternalUriSchemeEventHandler,"{74f712e0-8165-43a9-a13f-0cce597e75df}")
GUID_TEXT(ICoreWebView2ContainsFullScreenElementChangedEventHandler,"{e45d98b1-afef-45be-8baf-6c7728867f73}")
GUID_TEXT(ICoreWebView2FindActiveMatchIndexChangedEventHandler,"{0054f514-9a8e-5876-aed5-30b37f8c86a5}")
GUID_TEXT(ICoreWebView2FindMatchCountChangedEventHandler,"{da0d6827-4254-5b10-a6d9-412076afc9f3}")
GUID_TEXT(ICoreWebView2WebResourceRequestedEventHandler,"{ab00b74c-15f1-4646-80e8-e76341d25d71}")
#ifdef BROWSER_E2E_FIXTURE_HOOK
GUID_TEXT(ICoreWebView2ClearServerCertificateErrorActionsCompletedHandler,"{3b40aac6-acfe-4ffd-8211-f607b96e2d5b}")
#endif
// Same-window profiles (src/profile-workspace.h): every COM callback and every deferred job runs in the
// workspace (profile) that was in the globals when it was created — a hidden profile's tab never writes
// into the profile on screen. Defined in profile-workspace.h (end of this file).
#include "taskbar-identity.h" // one taskbar identity for every installed version; pins relaunch the stable launcher
TaskbarIdentity::Identity taskbarIdentity; // installed copies only (ResolveBrowserPaths)
#include "default-browser.h" // Browser in Windows' « Applications par défaut » (registered by the setup / updater; read here)
#include "launch-args.h"     // the link or file this launch was asked to open, apart from the switches
// The link or file given to this launch (browser.exe <url|path>, Windows' --single-argument "%1"), as given; a
// relative file name already made absolute. Opened once, after the session (RestoreSession), or handed to the
// profile's running window (HandOverLaunchTarget). "" when none.
std::wstring launchTarget;
#include "adblock-folder.h"  // the ad blocker installs from a folder that updates never move
#include "popup-target.h"   // script-opened windows: the request lifecycle (design/popup-window-target-report.md)
#include "download-navigation.h"   // a navigation the engine turned into a download: never an error page
#include <map>
PopupHost::Registry popups; extern bool unownedCallbacks;
struct Workspace; Workspace* WorkspaceNow(); bool WorkspaceDead(Workspace* w); bool WorkspaceParked();
bool WorkspaceEngineFailed(const std::wstring& where,HRESULT hr); bool WorkspaceCanFailAlone(); extern bool workspaceTest; extern bool qaControllerFailure; extern bool qaExpectTerminal;
bool windowClosing=false; // WM_CLOSE accepted: late engine failures only clean up
void EngineDropped(const std::wstring& root,const std::wstring& id,const wchar_t* why); void WatchEngineExit(ICoreWebView2Environment* environment,const std::wstring& root,const std::wstring& id);
struct WorkspaceScope { Workspace* previous=nullptr; std::uint64_t switches=0; bool entered=false; explicit WorkspaceScope(Workspace* w); ~WorkspaceScope(); WorkspaceScope(const WorkspaceScope&)=delete; WorkspaceScope& operator=(const WorkspaceScope&)=delete; };
template<class I, class... A> class Handler final : public I {
    LONG refs=1; IID iid{}; std::function<HRESULT(A...)> fn; Workspace* owner=WorkspaceNow();
public:
    explicit Handler(std::function<HRESULT(A...)> f):fn(std::move(f)){CLSIDFromString(GuidText<I>(),&iid);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(id==IID_IUnknown || id==iid){*out=static_cast<I*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override{if(WorkspaceDead(owner))return S_OK; /* its profile left this window: late events are dropped */WorkspaceScope scope(owner);return fn(args...);}
};
template<class I,class... A,class F> auto Callback(F f){return new Handler<I,A...>(std::move(f));}
template<class T> struct Com {
    T* p=nullptr; ~Com(){if(p)p->Release();} T* operator->()const{return p;}
    void set(T* q){if(q)q->AddRef();if(p)p->Release();p=q;}
};
// Stable per-tab identity (never reused): accessibility keys survive closes and moves.
inline unsigned NextTabSerial(){static unsigned serial=0;return ++serial;}
// SETTINGS(https): per-tab HTTPS-only bookkeeping. `upgrade` is the navigation that tries
// https:// for `insecure` (the http:// address asked for); `shown` is the http:// address
// whose "not secure" interstitial the tab displays (NavigateToString), `token` authenticates
// its button messages and `from` is where "Revenir" leads.
struct HttpsOnlyState { std::wstring insecure,expectHost,shown,token,from; UINT64 upgrade=0; bool pending=false; };
// Network error page (src/error-pages.h) shown in place of `shown`, authenticated by `token`.
struct ErrorPageState { std::wstring shown,token; bool pending=false; int kind=0; int forced=-1; bool forcedRetrying=false,forcedRestored=false; /* sandbox: the variant to show */
    std::wstring starting; UINT64 startingId=0; /* last top-level navigation: only its failure gets the page */
    // Server certificate refused by the engine for the request `certUri` (CANCEL, never allowed): the
    // page shows only if the latest top-level navigation then fails for that same URL.
    int certStatus=-1; std::wstring certUri,certIssuer;
    // #121 sheet: the code the page shows (its copy button), and the consecutive failures of `triedUri` in
    // this tab (« Essai : 2e tentative »; the time-out's 8 / 16 / 32 s retries), reset by another address or a success.
    std::wstring code,triedUri; int tries=0;
    int forcedCert=-1; std::wstring forcedIssuer; /* sandbox: a certificate case to show (status, example issuer) */ };
struct Tab { unsigned serial=NextTabSerial(); Com<ICoreWebView2Controller> controller; Com<ICoreWebView2> view; Com<ICoreWebView2_15> faviconView; NativeChrome::FaviconState favicon; NativeChrome::Loading::State loading; std::wstring title=TR(tab_new),uri; bool closed=false,home=true,refreshing=false,certificateError=false;
    ULONGLONG hiddenAt=GetTickCount64(); bool sleeping=false; HttpsOnlyState https; bool holed=false; /* #127: window region cut under the column over the page */ // hiddenAt: when it last stopped being shown
    // Split view: group id (0 = none), relative size, use order (rails collapse the least
    // recent), on screen now, first navigation done (until then the controller stays hidden).
    int group=0; float weight=1; std::uint64_t usedAt=0; bool shown=false,revealed=false; RECT placed{}; int placedVisible=-1; HWND host=nullptr; /* its WebView host window, once known (switch transition capture allowlist) */
    // Per-site audio (audio-integration.h): what this tab plays, and its injected script.
    NativeChrome::Audio::TabAudio audio; std::wstring audioScript; unsigned audioScriptGeneration=0;
    // Resize preview: `navStamp` changes with every navigation (a snapshot never outlives it);
    // `frozen`: bounds held while the preview stands in for the page.
    std::uint64_t navStamp=0; bool frozen=false;
    std::uint64_t navInFlight=0;   // the engine id of the top-level navigation started and not yet completed (review N13)
    // Overlay scrollbar (Paper V3 "Scrollbar minimale"): root scroll metrics posted by the page.
    NativeChrome::Scrollbar::Metrics scroll;
    // Link preview: the engine's status text (hovered link), drawn by our own bubble.
    std::wstring status;
    // Paper p-D-0 surfaces: network error page, debug sandbox (opened by the user in this tab),
    // page element in fullscreen.
    ErrorPageState errorPage; bool sandbox=false,sandboxPending=false,pageFullscreen=false;
    // Script-opened target (window.open): its about:blank is a web document, never the native home, until the user
    // deliberately goes Home (Navigate with an empty address sets blankIsHome again). popupSetup: created for a
    // pending request, not yet assigned: no script removal/reinjection meanwhile (queued in audioReinject).
    bool blankIsHome=true,popupSetup=false,audioReinject=false,scriptCloseQueued=false;
    // A navigation that became a download (src/download-navigation.h). everShown: the tab showed a page (or is the
    // home) at least once; a tab that never did existed only for its download and closes once it starts.
    NativeChrome::DownloadNavigation::Tracker downloadNav; bool everShown=false;
    // Navigate() puts the requested address in `uri` before the engine commits anything: what the tab showed before.
    std::wstring navigateFrom,navigateTo; bool navigateFromHome=false; };
// NEWTAB: the home tab's last render showed most visited sites (Tab is shared; kept beside it).
// A home tab stays hidden (the native canvas shows through) unless it shows tiles.
struct HomeTiles { std::vector<unsigned> serials; bool Has(const Tab& t)const{return std::find(serials.begin(),serials.end(),t.serial)!=serials.end();} void Set(const Tab& t,bool on){serials.erase(std::remove(serials.begin(),serials.end(),t.serial),serials.end());if(on)serials.push_back(t.serial);} } homeTiles;
bool PageShown(const Tab& t){return !t.home||homeTiles.Has(t);}
void TabShown(const std::shared_ptr<Tab>& tab);
HWND window,tabs,address,back,forward,reload,add,closeTab,home;
HFONT font; HBRUSH paper; HINSTANCE instance; HMODULE loader;
// Address selection in the window's profile colour, white text (native-ui.h EditSelection; design/profile-address-selection-report.md).
NativeUI::EditSelection addressSelection;
NativeUI::EditSelection::Colors AddressSelectionColors(){const auto f=NativeChrome::Design::Accent::Selection();auto b=[](float v){return (int)std::lround(std::clamp(v,0.f,1.f)*255);};return {RGB(b(f.r),b(f.g),b(f.b)),RGB(255,255,255)};}
void AddressSelectionChanged(){addressSelection.Refresh();}
// Overlay scrollbar of every shown page (Paper V3 "Scrollbar minimale", see ScrollbarRefresh).
void ScrollbarRefresh(NativeChrome::Motion::Time now);NativeChrome::Motion::Wake ScrollbarWake(NativeChrome::Motion::Time now);void ScrollbarReset();void ScrollbarSettleHover();void ScrollbarDestroy();void SyncScrollbars();
Com<ICoreWebView2Environment> environment;
std::vector<std::shared_ptr<Tab>> pages; int current=-1; bool halted=false;
struct ClosedTab { std::wstring uri,title; int index; };
std::vector<ClosedTab> closedTabs; bool sessionReady=false;
std::wstring base,dataDir,profileDir,sessionFile,logFile,downloadsFile,settingsFile; const COLORREF BG=NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::canvas);
NativeChrome::Chrome chrome; NativeChrome::DownloadsUI::Popover downloadPanel; NativeChrome::DownloadsUI::Drawer downloadDrawer; NativeChrome::SettingsUI::Drawer settingsDrawer; NativeChrome::ContextMenu::Menu contextMenu; BrowserSettings::State settings; NativeUI::AddressKeyState addressKeys; NativeChrome::Motion::State motion; int pressed=0; bool motionMinimized=false; constexpr UINT_PTR MotionTimerId=72; std::optional<NativeChrome::Motion::Time> motionDeadline; HWND tooltip; int hover=0,keyboardFocus=0; bool editing=false,keyboardModality=false;
namespace SV=NativeChrome::Split;
// `origin`: index at the press, restored by Escape. `lifted`: dragged out of the strip into the split overlay.
// #127 column: `at` the last pointer (client px: the drop on the grid, outside the window), `cardW` the lifted card's width.
struct TabDrag { std::shared_ptr<Tab> tab; POINT start{}; float grab=0; bool active=false; int origin=-1; bool lifted=false; float grabY=0; SV::Target target; POINT at{}; float cardW=0; } tabDrag;
std::map<int,SV::Orientation> splitGroups; int nextSplitGroup=1; std::uint64_t useClock=0; int splitResize=-1;
NativeChrome::SplitUI::Overlay splitOverlay;NativeChrome::StatusBubble::Bubble statusBubble;unsigned statusSerial=0;
struct SeparatorDrag { bool active=false; int handle=-1,slot=-1; float grab=0; std::vector<float> extents; float last=0; DWORD lastTime=0; float velocity=0; } sepDrag; int lastSeparator=-1; DWORD lastSeparatorClick=0;
// Annotation mode entry points (src/annotation-mode.h; no-op stubs when the feature is compiled out).
constexpr int AnnotateCommand=3060,AnnotateExitCommand=3061,AnnotateHistoryCommand=3062,ChatCommand=3063; /* chat intégré (src/chat-host.h): Ctrl+Maj+D, the panel's width taken from the page area, the reload hook, shutdown */ float ChatInsetDip(); void ChatNavigationCompleted(const std::shared_ptr<Tab>& t); void ChatShutdown(); void AnnotationStartup(); void AnnotationShutdown(); /* #125: a private window's notes erased at its close, leftovers at the next start */ bool AnnotationHistoryAvailable(); bool AnnotationsCompiled(); bool AnnotationModeActive(); bool AnnotationBandSliding(); bool AnnotationSurfaceFocus(); void AnnotationSyncChrome(); void AnnotationLayout(); void AnnotationWindowState(); void AnnotationNavigation(const std::shared_ptr<Tab>& t); void AnnotationTabChanged(); void AnnotationTabSleeping(const std::shared_ptr<Tab>& t); bool AnnotationPageMessage(const std::shared_ptr<Tab>& t,ICoreWebView2WebMessageReceivedEventArgs* a); bool AnnotationCommand(int cmd);
#include "annotation-entry.h" // settings section « Annotations » and menus: connect/disconnect, status, folder, clear all
namespace CM=NativeChrome::ContextMenu; extern std::vector<CM::Item> lastSplitMenu; void TabContextMenu(int tabIndex,int anchor); void AnnotationMenuItems(const std::shared_ptr<Tab>& tab,ICoreWebView2ContextMenuRequestedEventArgs* args,std::vector<CM::Item>& items); void AnnotationMenuChoice(const std::shared_ptr<Tab>& tab,int choice); bool ResizePlaceholdersUp(); extern std::vector<HWND> splitGrips; void SyncGripLooks(); bool PointerOnGrip(); bool GripsAnimating(); void GripFrame(NativeChrome::Motion::Time now); void Select(int n); void EndSplitDrag(bool restore); void SyncSplitDecor(); void ApplyPages(); bool SplitDragMove(POINT point); bool SplitAnimating(); void SplitFrame(NativeChrome::Motion::Time now);
// Address bar (issue #20): visited-sites history, the suggestion list and the text the
// user typed (the field may also show an inline completion or an arrowed row).
AddressBar::History history; std::vector<AddressBar::PopularSite> popularSites; NativeChrome::AddressUI::Suggestions suggestions; std::wstring addressTyped; bool addressSetting=false,addressDeleting=false;
// Browsing history drawer (src/history-panel.h): the toolbar History button (or Ctrl+H) opens it docked on the right.
NativeChrome::HistoryUI::Drawer historyDrawer; bool historyDismissClick=false;
void InvalidateChrome();
// Mode Focus (src/focus-mode.h; section « Mode Focus » near the window procedure). While it is on, `chrome.floating`:
// the chrome is a panel over the page, chrome-local rectangles reach the client through ChromeClient / ChromePx.
namespace FM=NativeChrome::FocusMode;
constexpr int FocusModeCommand=3070; constexpr UINT_PTR FocusPollTimerId=0x464D,FocusSlideTimerId=0x464E;
FM::Bar focusBar; const wchar_t* focusRevealWhy=L"keyboard"; // why the panel last came at once (log)
bool SyncFocusMode(); void FocusPlace(); void ToggleFocusMode(); void FocusEvaluate(bool pointerMoved=false); void FocusReveal(); void FocusFrame(NativeChrome::Motion::Time now);
NativeChrome::Motion::Wake FocusWake(NativeChrome::Motion::Time now); void InvalidateFocusPanel(); void FocusHoles(bool force);
double FocusMs(NativeChrome::Motion::Time t); void FocusAnchor(const wchar_t* why=L"surface"); bool FocusPanelOver(const RECT& px); bool FocusHoleOver(const RECT& px); extern bool focusLive; void FocusSlideHide(); float PageTopDip(); float ChromeBottomDip(); RECT FocusPanelPixels();
D2D1_POINT_2F ChromeOrigin(); D2D1_RECT_F ChromeClient(D2D1_RECT_F r); RECT ChromePx(D2D1_RECT_F r);
void FocusAbove(); HWND AnnotationBannerWindow(); D2D1_RECT_F FocusChatCard(float widthDip); // Mode Focus with the annotation mode: the banner above the panel, the chat card beside it
// restore=true (Escape, lost capture, Alt+Tab): the order before the press comes back.
void CancelTabDrag(bool restore=false){EndSplitDrag(restore);tabDrag={};chrome.dragIndex=-1;chrome.dragOffset=0;chrome.sidebarDrag=-1;chrome.sidebarInsert=-1;InvalidateChrome();}
int S(float value){return (int)std::lround(value*GetDpiForWindow(window)/96.f);}
void RefreshTools();
struct ICoreWebView2ContextMenuRequestedEventArgs; HRESULT PageContextMenu(const std::shared_ptr<Tab>& tab,ICoreWebView2ContextMenuRequestedEventArgs* args);
void ExtraCommand(int cmd);
// Paper « Éléments & popups » integration (src/elements-integration.h, included below).
void ElementsTabChanged(); void ElementsTabClosed(const std::shared_ptr<Tab>& t); void FormsTabClosed(const std::shared_ptr<Tab>& t); bool IsSandboxUri(const std::wstring& uri);
// B01 address-bar request chip (elements-integration.h): derived from the open permission popover.
// B07: a download the engine blocked (malicious / security check / policy).
void ElementsDangerousDownload(int id,const std::wstring& path,const std::wstring& uri,COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason);
void DownloadNavigationAsked(const std::shared_ptr<Tab>& t,const std::wstring& uri);
void SyncRequestChip(); void ElementsNavigationStarting(const std::shared_ptr<Tab>& t); void VaultNavigationStarting(const std::shared_ptr<Tab>& t,std::uint64_t navigationId,const std::wstring& uri); void VaultNavigationCompleted(const std::shared_ptr<Tab>& t,std::uint64_t navigationId,bool ok); extern int vaultKeyPress; bool VaultKeyArmed(); extern ULONGLONG vaultKeyShownAt; void RequestChipActivated(); bool RequestChipOpen(); void ElementsNavigationCompleted(const std::shared_ptr<Tab>& t,bool ok);
void OpenSandbox(std::shared_ptr<Tab> in=nullptr); void OpenFindBar(); bool InFullscreen(); void ToggleBrowserFullscreen(); void ShowTabLimitToast(const std::wstring& url=L""); void ShowPartialRestoreToast(const std::vector<std::wstring>& urls); void Defer(std::function<void()> job); void ShowImportDialog(bool keyboard);
void AskClearBrowsingData(bool demo=false,std::function<void(bool)> result=nullptr); bool CloseWindowAllowed(bool demo=false,std::function<void(bool)> result=nullptr);
void ShowAboutDialog(); void ShowPrivateDialog(bool future=false); bool ElementSurfaceKey(UINT key,bool ctrl,bool alt,bool shift,int& cmd);
void ElementsFrame(NativeChrome::Motion::Time now); NativeChrome::Motion::Wake ElementsNextWake(NativeChrome::Motion::Time now);
void ElementsReducedMotion(bool reduced); void RepositionElementSurfaces(); void ChromeTipHover(int x,int y); void ChromeTipHide(); void ChromeTipFocus(int id); bool sandboxNextTab=false,debugSandboxAtStart=false;
constexpr int FindNextCommand=3050,FindPreviousCommand=3051,FullscreenCommand=3052,SandboxCommandId=3053,ShortcutsCommand=3054,ExitFullscreenCommand=3055; bool PageFullscreenActive(); void ExitPageFullscreen(); void OpenShortcutsPanel();
// Bookmarks (section "Bookmarks" below).
constexpr int BookmarkBarCommand=3030,BookmarksDrawerCommand=3031,BookmarkUndoCommand=3032;
constexpr long long AccessibleBandKey=0x10003;
constexpr long long AccessibleToolbarKey=0x10001; // = AccessibleToolbar (declared with the UIA tree below)
extern Accessibility::Bridge accessibility;
NativeChrome::BookmarkPopover::Popover bookmarkPopover; NativeChrome::BookmarkToast::Toast bookmarkToast; NativeChrome::BookmarksUI::Drawer bookmarksDrawer;
// Panneau latéral (#127): section « Panneau latéral » below (src/sidebar.h, Chrome::BuildSidebar / PaintSidebar).
constexpr int SidebarToggleCommand=NativeChrome::Chrome::SidebarToggleId;int forcedLayout=-1;BrowserSettings::TabsPosition LayoutInEffect();void LogLayout();std::wstring LayoutLine();int TabLayoutArg(const wchar_t* args);bool SidebarEdgeDown(float x);void SidebarEdgeMove(float x);void SidebarEdgeUp(bool commit);void SidebarEdgeLook(int state);bool SidebarEdgeKey(WPARAM key);void SidebarEdgeReset();bool SidebarEdgeDragging();void ApplyLayout();float DrawerReserveDip();void SyncDrawerReserve();void SetTabsPosition(BrowserSettings::TabsPosition p);void SidebarPopover(bool open);bool AnnotationButtonDimmed();RECT RegionScreen(int id);bool ColumnPopoverAt(int id,float gap,POINT& bottomLeft);void UpdateFont();void SidebarPrepare();void ToggleSidebar();void SidebarHoles();bool SidebarDragMove(POINT point);void SidebarDragEnd(bool commit);
bool SyncBookmarkChrome();void BookmarksChanged();void ToggleBookmarksDrawer(bool keyboard);bool BookmarkedSite(const std::wstring& site);long long AccessibleBandItemKey(int region);
// Self-update host hooks (no-ops in unstamped builds).
std::wstring AboutTitle(){
#ifdef BROWSER_UPDATES
    return std::wstring(L"Browser ")+BROWSER_VERSION_WIDE;   // i18n-exempt: product name and version
#else
    return L"Browser";
#endif
}
void CheckForUpdates(); bool UpdateCheckAvailable(); bool FocusUpdateToast(); bool UpdateToastHasFocus(); void SyncUpdateToastReserve();
void SetUpdateToastTop(float dip); float UpdateToastHeight(); // the bookmarks band and the undo toast keep clear of it
void SyncUpdateToastMotion();
constexpr int CheckUpdatesCommand=3020;
bool SideFocusWanted();void SideFocusReset();void SideFocusEvaluate();void SideRevealChanged();void SideSlideFrame();void SidePictureHide();void SideFocusForAddress();void SideFocusCommand(int cmd);bool SideFocusEscape();
void InvalidateChrome(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */if(chrome.floating){InvalidateFocusPanel();return;}RECT r;GetClientRect(window,&r);if(!chrome.sidebar)r.bottom=std::min(r.bottom,(LONG)S(chrome.ContentTop()));InvalidateRect(window,&r,FALSE);}
// Frame wakes are display-paced by FrameClock (one outstanding notification, worker
// dormant when idle). Deadline-only dwells keep a single one-shot SetTimer. If the
// clock is unavailable the legacy SetTimer path remains (not display synchronised).
constexpr UINT FrameMessage=WM_APP+8; constexpr UINT_PTR DrawerWarmTimerId=73; NativeChrome::FrameClock::Clock frameClock; bool frameClockReady=false,motionFullFrames=false; long long paintedLoading=-2;
std::shared_ptr<Tab> Active();
// Lands every chrome icon animation on its resting state (tab switch, reduced motion,
// timer failure, halt): no half-played Lottie, travel or Stop morph resumes later.
void ClearChromeMotion(){chrome.lottie.Clear();auto t=Active();chrome.nav.Reduce(t&&t->loading.IsLoading());}
// ---- Motion lab (review only: --motion-lab) ----
// Every tunable feeds a real model input; a normal launch never changes the defaults.
bool motionLabMode=false; NativeChrome::MotionLab::Panel motionLab;
constexpr int MotionLabCommand=3040; constexpr UINT_PTR MotionLabTimerId=93;
NativeChrome::NavMotion::SpringParams TravelParams(bool back){namespace T=NativeChrome::Tuning;auto& v=T::Current();
    return back?NativeChrome::NavMotion::SpringParams{v[T::BackPeak],v[T::BackResponse],v[T::BackDamping]}:NativeChrome::NavMotion::SpringParams{v[T::ForwardPeak],v[T::ForwardResponse],v[T::ForwardDamping]};}
void ApplyMotionTuning(){namespace T=NativeChrome::Tuning;auto& v=T::Current();
    chrome.lottie.Tune(v[T::RefreshTurn]/1000.,v[T::CopySpeed],v[T::CopyDwell]/1000.);
    chrome.downloadIcon.Tune(v[T::DownloadSpeed],NativeChrome::Motion::Milliseconds((long long)v[T::DownloadRipple]));
    chrome.nav.stop.Configure(NativeChrome::Motion::Milliseconds((long long)v[T::StopMorph]),v[T::StopTurn]);}
// Feeds the active tab's real loading state to the Reload <-> Stop morph.
void SyncStopMorph(NativeChrome::Motion::Time now){chrome.nav.stop.Sync(chrome.navigationLoading,chrome.lottie.TurnEnd(),now,motion.ReducedMotion());}
// Loading-only frames repaint just the URL surface, and only when the quantised fill
// differs from what was last painted; any other motion (or the frame right after it
// ends, so its endpoint is painted) repaints the whole header.
void CloseAudioDrawer();void InvalidateDownloadIcon();void AudioFrame(NativeChrome::Motion::Time now);NativeChrome::Motion::Wake AudioNextWake(NativeChrome::Motion::Time now);void RefreshAudio();
void InvalidateMotion(NativeChrome::Motion::Time now){
    FocusFrame(now); // Mode Focus: the floating panel's slide (the page windows' hole, then its own repaint)
    ScrollbarRefresh(now); // overlay handles only: never repaints the chrome
    SplitFrame(now);GripFrame(now);ElementsFrame(now);downloadPanel.Frame(now);downloadDrawer.Frame(now);settingsDrawer.Frame(now);historyDrawer.Frame(now);bookmarksDrawer.Frame(now);
    // Downloads icon: its own button only (also the frame that lands its last pose).
    chrome.downloadIcon.Settle(now);chrome.nav.Settle(now);AudioFrame(now);
    // Repainted only when its pose changes (60 poses a second at most at the authored speed), not on every compositor tick.
    {static NativeChrome::DownloadIcon::Pose painted;static bool known=false;const auto pose=chrome.downloadIcon.At(now);if(!known||pose!=painted){InvalidateDownloadIcon();painted=pose;known=true;}}
    const bool full=motionFullFrames||motion.NextWake(now).delay||chrome.TabsNextWake(now).delay||chrome.lottie.NextWake(now,motion.ReducedMotion()).delay||chrome.nav.NextWake(now).delay;
    if(full){InvalidateChrome();return;}
    auto t=Active();if(chrome.LoadingSignature(t?t->loading.Sample(now,motion.ReducedMotion()):NativeChrome::Loading::Visual{})==paintedLoading)return;
    if(chrome.floating&&!chrome.panelLive)return;   // Mode Focus: no panel painted by the window (hidden, or its picture slides)
    RECT r=ChromePx(chrome.LoadingDirty());InvalidateRect(window,&r,FALSE);
}
void ScheduleMotion(NativeChrome::Motion::Time now){
    const bool reduced=motion.ReducedMotion();auto ui=NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(motion.NextWake(now),chrome.TabsNextWake(now)),chrome.lottie.NextWake(now,reduced)),chrome.nav.NextWake(now));motionFullFrames=ui.delay.has_value();
    auto wake=NativeChrome::Motion::Earlier(ui,NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(downloadPanel.NextWake(now),downloadDrawer.NextWake(now)),settingsDrawer.NextWake(now)),NativeChrome::Motion::Earlier(historyDrawer.NextWake(now),bookmarksDrawer.NextWake(now))));wake=NativeChrome::Motion::Earlier(wake,chrome.downloadIcon.NextWake(now));wake=NativeChrome::Motion::Earlier(wake,AudioNextWake(now));wake=NativeChrome::Motion::Earlier(wake,ElementsNextWake(now));if(current>=0&&current<(int)pages.size())wake=NativeChrome::Motion::Earlier(wake,pages[current]->loading.NextWake(now,reduced));if(SplitAnimating()||GripsAnimating())wake=NativeChrome::Motion::Earlier(wake,NativeChrome::Motion::Frame());wake=NativeChrome::Motion::Earlier(wake,ScrollbarWake(now));wake=NativeChrome::Motion::Earlier(wake,FocusWake(now));if(halted||motionMinimized||!wake.delay){KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();return;}
    // A deadline due within one nominal frame is served by the next compositor tick.
    if(frameClockReady&&(wake.frames||*wake.delay<=*NativeChrome::Motion::Frame().delay)){if(motionDeadline){KillTimer(window,MotionTimerId);motionDeadline.reset();}frameClock.Request();return;}
    frameClock.Cancel();
    auto due=now+*wake.delay;if(motionDeadline&&*motionDeadline<=due)return;
    KillTimer(window,MotionTimerId);motionDeadline=due;
    if(!SetTimer(window,MotionTimerId,(UINT)wake.delay->count(),nullptr)){motionDeadline.reset();motion.SetReducedMotion(true);chrome.SettleTabs();motion.copy.Clear();ClearChromeMotion();motion.SetInteraction(hover,0,now);InvalidateChrome();}
}
void UpdateInteraction(int newHover,int newPressed){hover=newHover;pressed=newPressed;auto now=NativeChrome::Motion::Now();int group=NativeChrome::IsClose(hover)?NativeChrome::TabBase+NativeChrome::TabIndex(hover):hover;motion.SetInteraction(group,pressed==hover?pressed:0,now);InvalidateChrome();ScheduleMotion(now);}

constexpr UINT Shortcut=WM_APP+1;
bool smoke=false; int testStage=0; int chromePaints=0;
// A9 private window: this process was started with --private. Its WebView2 controllers are InPrivate
// (ephemeral engine profile, discarded with the last private controller) and the app itself writes
// NOTHING to disk: no session, history, favicon cache, downloads list, settings, bookmarks or log.
bool privateWindow=false;
constexpr const wchar_t* PrivateProfileName=L"browser-private";
// The command line of a new private window: this same executable, --private, and (review R-2) --profile <id> of the
// profile it is opened from when that is not the default one: the private window then READS that profile's vault
// (and still writes nothing anywhere). Only a valid profile id is ever passed.
std::wstring PrivateCommandLine(const std::wstring& exe,const std::wstring& profile){return L"\""+exe+L"\" --private"+(profile!=Profiles::DefaultId&&Profiles::ValidId(profile)?L" --profile "+profile:L"");}
std::wstring PrivateOpenerProfile();   // profiles-integration.h
void Log(const std::wstring& s);
// « Navigation privée » (button, Ctrl+Maj+N): a separate process; its tabs never share the window,
// the profile, the session or any file of this one.
void OpenPrivateWindow(){
    if(smoke)return;
    std::wstring exe(32768,L'\0');const DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);if(exe.empty())return;
    std::wstring cmd=PrivateCommandLine(exe,PrivateOpenerProfile());const std::wstring dir=std::filesystem::path(exe).parent_path().wstring();
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,dir.c_str(),&si,&pi)){Log(L"PRIVATE window launch failed");return;}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Log(L"PRIVATE window launched");
}
// --split-test: native split-view verification on local pages (implies the isolated smoke profile).
bool splitTest=false; constexpr UINT_PTR SplitTestTimer=4100; void SplitTestStep();
bool splitOnScreen=false;  // unused: the screen is shared with the user's own windows, captures use PrintWindow
bool splitSynthetic=false; // --split-test: only the test's own mouse messages reach the window
void SmokeCheck();
void SmokeAdvance();

std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
// Serialized: the WebView2 install worker reports its real outcome from its own thread. A private window logs nothing.
// Release builds write origins only, never full addresses; every build caps the file (src/log-privacy.h, audit SEC-05).
std::mutex logLock;
void Log(const std::wstring& s){if(logFile.empty()||privateWindow)return;std::lock_guard<std::mutex> guard(logLock);LogPrivacy::RotateIfLarge(logFile);
#ifdef BROWSER_CHANNEL_RELEASE
    const std::wstring line=LogPrivacy::Redact(s);
#else
    const std::wstring& line=s;
#endif
    {std::ofstream f(std::filesystem::path(logFile),std::ios::app);f<<Utf8(line)<<"\n";}if(smoke&&!splitTest&&s.rfind(L"NAVIGATION OK" /* i18n-exempt: log marker */,0)==0)PostMessageW(window,WM_APP+2,0,0);}
void Fail(const std::wstring& where,HRESULT hr){if(halted)return;halted=true;popups.CancelAll(); /* pending window.open requests settled before any modal loop */for(auto& tab:pages)tab->loading.Clear();ClearChromeMotion();KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();wchar_t code[32];swprintf(code,32,L"0x%08lX",(unsigned long)hr);auto msg=TRF(wv2_halt_body,where,code);Log(L"WEBVIEW2 ERROR "+msg);if(splitTest){PostMessageW(window,WM_CLOSE,0,0);return;}if(workspaceTest&&qaExpectTerminal){Log(L"WSQA terminal failure captured (normal recovery reached once, dialog suppressed by --workspace-test)");return;}MessageBoxW(window,msg.c_str(),TR(wv2_halt_title),MB_OK|MB_ICONERROR);}
bool Check(HRESULT hr,const wchar_t* op){if(FAILED(hr)){Fail(op,hr);return false;}return !halted;}
// A refused per-tab operation (find, reload, favicon, interstitial): logged, never the loss of the engine.
bool CheckTab(HRESULT hr,const wchar_t* op){if(FAILED(hr)){wchar_t code[32];swprintf(code,32,L"0x%08lX",(unsigned long)hr);Log(std::wstring(L"WEBVIEW2 refused ")+op+L" "+code);return false;}return !halted;}
std::shared_ptr<Tab> Active(){return current>=0&&current<(int)pages.size()?pages[current]:nullptr;}
void FocusAddress(){if(InFullscreen())return; /* no address field over a fullscreen page */editing=true;SideFocusForAddress(); /* #127 Focus: the column at once, with its field */FocusAnchor(L"address field"); /* Mode Focus: the panel at once, at rest (the field sits there); i18n-exempt: log tag */ShowWindow(address,SW_SHOW);SetFocus(address);SendMessageW(address,EM_SETSEL,0,-1);InvalidateRect(window,nullptr,FALSE);}
std::vector<std::wstring> Titles(){std::vector<std::wstring> titles;for(auto& t:pages)titles.push_back(t->title);return titles;}
std::shared_ptr<const NativeChrome::FaviconImage> SiteIcon(const std::wstring& site);
// What the field reads: the active tab's address and security outside editing; while
// editing, the active suggestion's glyph in the indicator slot.
void SyncAddressDisplay(){auto t=Active();chrome.address=t&&!t->home?AddressBar::Describe(t->uri,t->certificateError):AddressBar::Display{};chrome.addressEditing=editing;
    chrome.editIcon=nullptr;chrome.editFavicon=nullptr;if(editing&&suggestions.Visible()){const auto& rows=suggestions.Data().rows;const int a=suggestions.Active();if(a>=0&&a<(int)rows.size()){chrome.editIcon=NativeChrome::AddressUI::Suggestions::IconFor(rows[a]);if(rows[a].kind!=AddressBar::Kind::Search)chrome.editFavicon=SiteIcon(rows[a].site);}}SyncRequestChip();}
// Stable tab serials let the strip animate opens/closes and the selection fade
// without index reuse transferring one tab's visual state to its neighbour.
void SyncTabKeys(){chrome.tabKeys.clear();for(auto& t:pages)chrome.tabKeys.push_back(t->serial);}
void SyncVaultKey(); void FormsSync(); void FormsHideAll();
void BuildChrome(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */FormsSync(); /* #121: the list and the bubble belong to one tab, document and place (P8) */chrome.privateChip=false; /* user polish: the incognito control in the profile slot replaces the « Privée » chip (privacy stays in its name, tooltip and title) */SyncVaultKey(); /* decision 7: the chip is the private indication; P stays a normal command */SyncAddressDisplay();SyncBookmarkChrome();SyncTabKeys();auto titles=Titles();auto now=NativeChrome::Motion::Now();chrome.contentOnly=InFullscreen(); /* fullscreen: the page alone */SidebarPrepare();chrome.Build((int)pages.size(),current,&titles,now);SyncSplitDecor();if(chrome.TakeTabMotionStarted()){InvalidateChrome();ScheduleMotion(now);}}
// After a pointer close the strip keeps its tab width until the pointer leaves
// it, so repeated clicks keep landing on close buttons.
void LockTabWidth(){chrome.lockedTab=chrome.strip.tab;}
void UnlockTabWidth(){if(chrome.lockedTab<=0)return;chrome.lockedTab=0;chrome.animateTabLayout=true;BuildChrome();RefreshTools();InvalidateChrome();}
void FocusPage(){
    keyboardFocus=0;editing=false;ShowWindow(address,SW_HIDE);SetFocus(window);
    auto t=Active();if(t&&t->controller.p&&PageShown(*t)&&!halted)Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus page");
    InvalidateChrome();
}
std::vector<int> ShownPanels();bool FocusContextToast();
void ToggleChromeFocus(bool back=false){
    // F6 cycle: page -> toolbar -> update toast (when one is shown) -> page. In a split
    // view the pages come first, in panel order (Shift+F6 walks back).
    if(UpdateToastHasFocus()||InFullscreen()){FocusPage();return;}   // fullscreen: no chrome to cycle through
    auto panels=ShownPanels();
    if(GetFocus()==window||GetFocus()==address){editing=false;if(!back&&FocusUpdateToast()){keyboardFocus=0;InvalidateChrome();return;}if(!back&&FocusContextToast()){keyboardFocus=0;InvalidateChrome();return;}if(panels.size()>1){Select(back?panels.back():panels.front());}FocusPage();return;}
    if(panels.size()>1){auto it=std::find(panels.begin(),panels.end(),current);if(it!=panels.end()){const int k=(int)(it-panels.begin())+(back?-1:1);if(k>=0&&k<(int)panels.size()){Select(panels[k]);return;}}}
    SetFocus(window);auto order=chrome.FocusOrder();keyboardFocus=order.empty()?0:order.front();FocusAnchor(L"F6");InvalidateChrome();
}
void FindInPage(){
    auto t=Active();if(!t||!t->view.p||t->home)return;
    Com<ICoreWebView2_28> view;Com<ICoreWebView2Environment15> env;
    Com<ICoreWebView2Find> find;Com<ICoreWebView2FindOptions> options;
    if(!Check(t->view->QueryInterface(IID_ICoreWebView2_28,(void**)&view.p),L"QueryInterface Find")||
       !Check(environment->QueryInterface(IID_ICoreWebView2Environment15,(void**)&env.p),L"QueryInterface Find environment")||
       !Check(view->get_Find(&find.p),L"get_Find")||!Check(env->CreateFindOptions(&options.p),L"CreateFindOptions"))return;
    FocusPage();
    auto completed=Callback<ICoreWebView2FindStartCompletedHandler,HRESULT>([](HRESULT hr)->HRESULT{CheckTab(hr,L"Find completed");return S_OK;});
    HRESULT hr=find->Start(options.p,completed);completed->Release();CheckTab(hr,L"Find Start");
}
void CancelContentTransitionIfMoved();
void SyncFolderRename();
void Layout(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */if(!window)return;CancelContentTransitionIfMoved();SyncFocusMode();FocusAbove();chrome.Resize();BuildChrome();FocusPlace();SyncFolderRename();auto r=chrome.edit;
    HDC dc=GetDC(address);auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(address,dc);
    {static float fontSize=0;if(fontSize!=chrome.addressFont){fontSize=chrome.addressFont;UpdateFont();HDC d2=GetDC(address);auto o2=SelectObject(d2,font?font:GetStockObject(DEFAULT_GUI_FONT));GetTextMetricsW(d2,&metrics);SelectObject(d2,o2);ReleaseDC(address,d2);}}
    auto placement=chrome.MeasureText(L"Ag",{r.left,r.top,r.right,r.bottom},chrome.addressFont,false,false,chrome.AddressTracking());
    // Mode Focus: the field sits in the floating panel at rest (editing keeps the panel there).
    const D2D1_POINT_2F to=ChromeOrigin();
    const int baseline=S(to.y+placement.originY+placement.baseline);int editHeight=std::max(S(22),(int)(metrics.tmHeight+metrics.tmExternalLeading+2));
    MoveWindow(address,S(to.x+r.left),baseline-metrics.tmAscent,std::max(1,S(r.right-r.left)),editHeight,TRUE);
    RECT format{};SendMessageW(address,EM_GETRECT,0,(LPARAM)&format);
    SetWindowPos(address,nullptr,S(to.x+r.left),baseline-metrics.tmAscent-format.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    // The page (and a split view's panels, see ContentBox) starts right under the chrome:
    // 81 DIP, 121 with the bookmarks band, no top padding (Paper c-v7q0dg). Mode Focus: 4 DIP, the panel floats over it.
    SetUpdateToastTop(ChromeBottomDip());bookmarkToast.Reposition();
    ApplyPages();
    RefreshTools();InvalidateRect(window,nullptr,FALSE);
}
void Update(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    auto t=Active();if(!t)return;
    BOOL canGoBack=FALSE,canGoForward=FALSE;
    if(t->view.p&&!halted){
        if(!Check(t->view->get_CanGoBack(&canGoBack),L"get_CanGoBack")||
           !Check(t->view->get_CanGoForward(&canGoForward),L"get_CanGoForward"))return;
    }
    chrome.SetEnabled('B',canGoBack!=FALSE);chrome.SetEnabled('F',canGoForward!=FALSE);
    chrome.SetEnabled('C',!t->home&&!t->uri.empty());AnnotationSyncChrome();if(!editing)SetWindowTextW(address,t->home?L"":t->uri.c_str());NativeUI::SetTextIfChanged(window,privateWindow?TRF(window_title_private,t->title):TRF(window_title,t->title)); /* only a new title reaches Windows: Update runs on every address, history and load event */RefreshTools(); /* builds the chrome (PERF-07: no second BuildChrome) */InvalidateRect(window,nullptr,FALSE);
    ScrollbarRefresh(NativeChrome::Motion::Now()); // home <-> page hides or restores the handle
}
// Tab / profile switch transition (src/content-transition.h; functions defined before profile-workspace.h).
ContentTransition::Overlay contentTransition; unsigned contentTransitionGeneration=0; constexpr UINT_PTR ContentTransitionTimerId=0x5051;
constexpr UINT_PTR ContentReadyTimerId=0x5053; void ContentReadyTick();
void BeginContentTransition(ContentTransition::Kind kind);void RevealContentTransition();void ContentTransitionInput(const wchar_t* what);void CancelContentTransition(const wchar_t* why);
// Internal selections that complete a profile switch (its session restored when its engine is ready, the tab chosen
// in another profile's preview) keep the profile transition; only another selection cancels it.
int keepContentTransition=0; unsigned selectedSerial=0; /* the tab Select showed last (indices shift on close / move) */ struct KeepContentTransition{KeepContentTransition(){++keepContentTransition;}~KeepContentTransition(){--keepContentTransition;}};
void Select(int n){if(n<0||n>=(int)pages.size()||halted)return;if(WorkspaceParked()){current=n;pages[n]->usedAt=++useClock;return;}if(pages[n]->serial!=selectedSerial&&!keepContentTransition)CancelContentTransition(L"tab selected");selectedSerial=pages[n]->serial; /* a tab switch is immediate: no blur, no capture; a profile transition still on screen never covers the chosen tab */editing=false;ShowWindow(address,SW_HIDE);current=n;ClearChromeMotion();motion.SelectTab((int)pages[n]->serial,NativeChrome::Motion::Now());ScheduleMotion(NativeChrome::Motion::Now());pages[n]->usedAt=++useClock;ApplyPages();Update();auto t=Active();TabShown(t);if(t->controller.p&&PageShown(*t))Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus");ElementsTabChanged();AnnotationTabChanged();}
// Move shared pointers, preserving the active WebView and its asynchronous callbacks.
void MoveTab(int from,int to){
    if(halted||from<0||to<0||from>=(int)pages.size()||to>=(int)pages.size()||from==to)return;
    auto active=Active();auto tab=pages[from];pages.erase(pages.begin()+from);pages.insert(pages.begin()+to,tab);
    current=(int)(std::find(pages.begin(),pages.end(),active)-pages.begin());
    motion.ClearInteractions();hover=pressed=keyboardFocus=0;Update();
}
// #127 motion spec §1: 6 DIP before a tab (top bar) or a row (column) lifts, for a reorder or a split view: ONE threshold.
constexpr float TabDragThreshold=6;
void DragTab(POINT point){
    if(!tabDrag.tab||halted)return;
    auto it=std::find(pages.begin(),pages.end(),tabDrag.tab);if(it==pages.end()){CancelTabDrag();return;}
    tabDrag.at=point;
    if(!tabDrag.active){
        // #127 motion spec: 6 DIP before the tab lifts (a click otherwise), or the system's drag distance when larger.
        if(std::abs(point.x-tabDrag.start.x)<std::max(S(TabDragThreshold),GetSystemMetrics(SM_CXDRAG)+1)&&std::abs(point.y-tabDrag.start.y)<std::max(S(TabDragThreshold),GetSystemMetrics(SM_CYDRAG)+1))return;
        tabDrag.active=true;
    }
    if(SplitDragMove(point))return;
    if(chrome.sidebar){SidebarDragMove(point);return;}
    float x=point.x*96.f/GetDpiForWindow(window)-ChromeOrigin().x; // chrome-local (Mode Focus: the panel's offset)
    // Drop slot = the visible tab under the pointer on the adaptive, scrolled strip.
    const auto* strip=&chrome.strip;int from=(int)(it-pages.begin()),lo=0,hi=(int)pages.size()-1;
    while(lo<hi&&!strip->Visible(lo))++lo;while(hi>lo&&!strip->Visible(hi))--hi;
    int slot=std::clamp((int)std::floor((x+strip->scroll-NativeChrome::Design::TabStrip::Left())/strip->Pitch()),lo,hi);
    MoveTab(from,slot);
    it=std::find(pages.begin(),pages.end(),tabDrag.tab);chrome.dragIndex=(int)(it-pages.begin());
    auto surface=strip->Surface(chrome.dragIndex);
    float left=std::max(strip->Surface(0).left,strip->ViewLeft());
    float right=std::min(strip->Surface(strip->count-1).right,strip->ViewRight());
    chrome.dragOffset=std::clamp(x-tabDrag.grab,left,std::max(left,right-strip->tab))-surface.left;
    InvalidateChrome();
}
// ---- Split view ---------------------------------------------------------------------
// Paper "Split View — flow" (boards 00-23) and design/split-view.md. A group is a set
// of tabs shown side by side (Row, vertical separators) or stacked (Column). Panel
// order is tab order and the active panel is the selected tab, so the address bar,
// toolbar commands and shortcuts follow it with no extra routing. Geometry, zones,
// capacity and springs live in src/split-view.h (unit-tested); this section only
// binds them to the real WebView2 controllers, the strip and the drag overlay.
float PageZoom(){return std::max(.25f,settings.zoomPercent/100.f);}
std::vector<std::shared_ptr<Tab>> Members(int g){std::vector<std::shared_ptr<Tab>> m;if(g)for(auto& t:pages)if(t->group==g)m.push_back(t);return m;}
int TabIndexOf(const std::shared_ptr<Tab>& t){auto it=std::find(pages.begin(),pages.end(),t);return it==pages.end()?-1:(int)(it-pages.begin());}
std::wstring TabLabel(const std::shared_ptr<Tab>& t){return !t?L"":t->title.empty()?std::wstring(TR(tab_untitled)):t->title;}
// Compact site name for the drag surfaces ("GitHub"); the full document title stays on the
// tab, in history and in the tab's accessible name.
std::wstring SiteLabel(const std::shared_ptr<Tab>& t){if(!t)return L"";if(t->home)return TabLabel(t);auto n=NativeChrome::SiteName::Compact(t->title,t->uri);return n.empty()?TabLabel(t):n;}
void LeaveGroup(const std::shared_ptr<Tab>& t){if(t){t->group=0;t->weight=1;}}
// A group of one is no split: its last tab simply becomes a tab again.
void DissolveSmallGroups(){
    for(auto it=splitGroups.begin();it!=splitGroups.end();){auto m=Members(it->first);if(m.size()<2){for(auto& t:m)LeaveGroup(t);it=splitGroups.erase(it);}else ++it;}
    for(auto& t:pages)if(t->group&&!splitGroups.count(t->group))LeaveGroup(t);
}
int VisibleGroup(){auto t=Active();return t&&t->group&&splitGroups.count(t->group)&&Members(t->group).size()>=2?t->group:0;}
// Same box as a single page: under the chrome, 81 DIP or 121 with the bookmarks band
// (SV::Content assumes the former fixed 84 DIP top).
SV::Box ContentBox(){namespace View=NativeChrome::Design::Viewport;
    // Mode Focus (Paper 1J9P-0): the page 4 DIP inside the window on all four sides; the panel floats over its top.
    // With the annotation mode (Paper 203J-0): under the banner, 4 DIP before the chat card. R25: while the banner slides the
    // page keeps its size and only moves (no relayout per frame), as in the normal chrome.
    if(chrome.floating&&!InFullscreen()){auto f=FM::Content(chrome.ClientWidth(),chrome.height,FM::ChatReserve(ChatInsetDip()),chrome.annotationBand);if(AnnotationBandSliding())f.bottom+=f.top-FM::pageInset;return {f.left,f.top,f.right,f.bottom};}
    auto c=SV::Content(chrome.width,chrome.height);c.top=chrome.ContentTop();const float inset=NativeChrome::Sidebar::Zone::inset; /* #127 (Val): 4 DIP around the page in both layouts */c.left=chrome.sidebar?chrome.SidebarZoneLeft():inset;c.right=std::max(c.left,chrome.width-inset-ChatInsetDip());   /* #127 panel layouts: ChatInsetDip includes the 4 DIP gap to the chat card */
    if(const float drawer=DrawerReserveDip();drawer>0)c.right=std::max(c.left,std::min(c.right,chrome.width-drawer-inset)); /* #127 (Val, B): an open drawer: the page stops 4 DIP before it, its right corners rounded */ /* the chat panel sits at the right of the page area: the page shrinks, it is never covered */c.bottom=chrome.height-inset;if(AnnotationBandSliding())c.bottom+=chrome.annotationBand; /* R25: while the annotation band slides, the page keeps its size and only moves (its bottom goes past the window, unseen): no relayout per frame */if(InFullscreen()){c={0,0,chrome.width,chrome.height};}c.bottom=std::max(c.top,c.bottom);return c;}
struct SplitView{int group=0,active=-1;float min=SV::minWidthCss;std::vector<std::shared_ptr<Tab>> members;std::vector<float> weights;SV::Arrangement a;};
SplitView CurrentSplit(){
    // A page element in fullscreen (video, game…) is presented alone at the whole ContentBox: the other panels,
    // grips, rails, separator hit and F6 panel cycle follow this (they all read CurrentSplit). The group itself (its
    // members, order, orientation, weights) is untouched and comes back as it was when the page leaves fullscreen.
    // Browser fullscreen (F11) alone keeps the split.
    SplitView v;if(PageFullscreenActive())return v;v.group=VisibleGroup();if(!v.group)return v;v.members=Members(v.group);const auto o=splitGroups[v.group];
    // Minimum in CSS px seen by the site, converted with the page zoom (DIP = CSS px x zoom).
    v.min=SV::MinExtent(o,PageZoom());std::vector<std::uint64_t> recency;
    for(int i=0;i<(int)v.members.size();++i){v.weights.push_back(v.members[i]->weight);recency.push_back(v.members[i]->usedAt);if(v.members[i]==Active())v.active=i;}
    v.a=SV::Arrange(ContentBox(),o,v.weights,recency,v.active,v.min);return v;
}
std::shared_ptr<Tab> SplitPartner(const std::shared_ptr<Tab>& dragged){
    auto a=Active();if(a&&a!=dragged&&!VisibleGroup())return a;
    // Dragging the active tab itself: it pairs with the most recently used other tab.
    std::shared_ptr<Tab> best;for(auto& t:pages)if(t!=dragged&&!t->closed&&!t->group&&(!best||t->usedAt>best->usedAt))best=t;return best;
}
int SeparatorAt(float x,float y,const SplitView& v){for(size_t i=0;i<v.a.separators.size();++i)if(v.a.SeparatorHit(v.a.separators[i]).Contains(x,y))return (int)i;return -1;}
void SyncSplitDecor(){
    chrome.tabGroups.assign(pages.size(),0);for(size_t i=0;i<pages.size();++i){const int g=pages[i]->group;if(g&&splitGroups.count(g))chrome.tabGroups[i]=g;}
    chrome.visibleTabGroup=VisibleGroup();
    auto v=CurrentSplit();auto& d=chrome.split;d.rails.clear();d.railTabs.clear();d.handles.clear();d.shown=v.group!=0;
    // One accent at a time: while a drop preview is shown, the active ring steps back.
    d.ring=!tabDrag.lifted&&!ResizePlaceholdersUp();
    if(!d.shown){d.hotHandle=d.focusedHandle=-1;splitResize=-1;SyncGripLooks();return;}
    for(auto& s:v.a.slots){D2D1_RECT_F r{s.box.left,s.box.top,s.box.right,s.box.bottom};if(s.rail){d.rails.push_back(r);d.railTabs.push_back(TabIndexOf(v.members[s.member]));}else if(s.member==v.active)d.active=r;}
    {const auto c=ContentBox();const bool low=std::fabs(d.active.bottom-c.bottom)<.5f;d.activeBottomLeft=low&&std::fabs(d.active.left-c.left)<.5f;d.activeBottomRight=low&&std::fabs(d.active.right-c.right)<.5f;
     // Mode Focus: the pages' top corners are round too (6 DIP); the ring follows them on the content's top edge.
     const bool high=chrome.floating&&std::fabs(d.active.top-c.top)<.5f;d.activeTopLeft=high&&std::fabs(d.active.left-c.left)<.5f;d.activeTopRight=high&&std::fabs(d.active.right-c.right)<.5f;}
    for(int s:v.a.separators){auto b=v.a.SeparatorBox(s);d.handles.push_back({b.left,b.top,b.right,b.bottom});}
    if(splitResize>=(int)d.handles.size())splitResize=-1;
    d.focusedHandle=splitResize;if(sepDrag.active)d.hotHandle=sepDrag.handle;else if(d.hotHandle>=(int)d.handles.size())d.hotHandle=-1;
    d.paintHandles=splitGrips.empty();SyncGripLooks();
}
// Link preview (status bubble): shown only over the page whose link is hovered, while
// that page is on screen; gone when the hover ends, on navigation, tab switch or close.
void SyncStatusBubble(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    std::shared_ptr<Tab> tab;if(!halted&&window&&!IsIconic(window))for(auto& t:pages)if(t&&t->serial==statusSerial){tab=t;break;}
    // A page taken off screen (tab switch, split change) forgets its hover: no stale bubble on return.
    if(tab&&(!tab->shown||tab->placedVisible!=1)){tab->status.clear();statusSerial=0;tab=nullptr;}
    if(!tab||tab->closed||tab->status.empty()||IsRectEmpty(&tab->placed)){statusBubble.Hide();return;}
    statusBubble.Show(tab->status,tab->placed);
}
void SetTabStatus(const std::shared_ptr<Tab>& t,std::wstring text){
    if(!t)return;t->status=std::move(text);
    if(!t->status.empty())statusSerial=t->serial;else if(statusSerial!=t->serial)return;
    SyncStatusBubble();
}
void SyncGrips(const SplitView& v);void SyncCornerMasks();void BlendCorners(HDC dc,POINT client);void SyncGripLooks();void HoverAfterDrag();bool ResizePreviewValid();void ClearResizePreview();
RECT PixelRect(const SV::Box& b){return {S(b.left),S(b.top),S(b.right),S(b.bottom)};}
// The one place that sizes and shows page controllers. A tab alone keeps the existing
// rules (home hidden, a new controller hidden until its first navigation completes);
// a visible group shows every non-collapsed member at its slot. Hidden tabs start
// their sleep clock, shown ones wake: a visible site is never suspended.
void ApplyPages(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    if(!window||halted)return;
    DissolveSmallGroups();if(!ResizePreviewValid())ClearResizePreview();auto v=CurrentSplit();auto active=Active();
    RECT full=PixelRect(ContentBox()); /* Paper V3 Viewbox (2 DIP outline, under the chrome); the whole window in fullscreen */const ULONGLONG now=GetTickCount64();
    // A page that grows shows the parent's surface wherever Chromium has not rendered
    // the new size yet. With WS_CLIPCHILDREN the parent never paints under its pages,
    // so that surface held stale handles/rings or never-painted black (the trails seen
    // while resizing). Before any bounds change, the parent paints the canvas under the
    // old and new rectangles of each moving page (pages are composited above it; a
    // resized page repaints in full anyway).
    HDC under=nullptr;
    for(auto& t:pages){
        bool show=false;RECT bounds=full;
        if(v.group&&t->group==v.group){const int m=(int)(std::find(v.members.begin(),v.members.end(),t)-v.members.begin());const int s=v.a.SlotOf(m);if(s>=0&&!v.a.slots[s].rail){show=true;bounds=PixelRect(v.a.slots[s].box);}}
        else show=t==active&&PageShown(*t); // a home tab shows only when it carries the frequent sites
        show=show&&t->revealed;
        if(!show&&t->shown)t->hiddenAt=now;
        if(show&&t->sleeping){t->sleeping=false;Com<ICoreWebView2_3> v3;if(t->view.p&&SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v3.p))&&v3.p)v3->Resume();}
        t->shown=show;
        if(t->controller.p&&!halted){
            if(!t->frozen&&!EqualRect(&bounds,&t->placed)){
                if(!under)under=GetDCEx(window,nullptr,DCX_CACHE|DCX_CLIPSIBLINGS);
                if(under){RECT both;UnionRect(&both,&bounds,IsRectEmpty(&t->placed)?&bounds:&t->placed);FillRect(under,&both,paper);}
                if(!Check(t->controller->put_Bounds(bounds),L"put_Bounds")){if(under)ReleaseDC(window,under);return;}t->placed=bounds;}
            if(t->placedVisible!=(int)show){if(!Check(t->controller->put_IsVisible(show),L"put_IsVisible"))return;t->placedVisible=show;}
        }
    }
    if(under)ReleaseDC(window,under);
    FocusHoles(true); // Mode Focus: Chromium may reset a page window's region when it resizes; the hole goes back at once
    SyncGrips(v);SyncCornerMasks();SidebarHoles();SyncSplitDecor();SyncStatusBubble();SyncScrollbars();AnnotationLayout();InvalidateRect(window,nullptr,FALSE);
}
// Separator grips. Pages are child windows of another process: over them the main
// window gets no mouse input, so the 4 DIP gap alone would be the target. A layered
// child window sits above the pages, 16 DIP wide (gap + 6 DIP over each page edge):
// alpha 1/255 everywhere (invisible, yet hit-tested), it forwards the pointer to the
// main window and draws the handle, the only thing allowed over the pages.
std::vector<HWND> splitGrips;
LRESULT CALLBACK GripProc(HWND g,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_NCHITTEST:return HTCLIENT;
    case WM_SETCURSOR:{auto v=CurrentSplit();SetCursor(LoadCursor(nullptr,v.a.orientation==SV::Orientation::Column?IDC_SIZENS:IDC_SIZEWE));return TRUE;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(g,&ps);EndPaint(g,&ps);return 0;}
    case WM_MOUSEMOVE:if(!splitTest){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,g,0};TrackMouseEvent(&t);}[[fallthrough]];
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};MapWindowPoints(g,window,&p,1);return SendMessageW(window,msg,w,MAKELPARAM(p.x,p.y));}
    case WM_MOUSELEAVE:if(!sepDrag.active&&chrome.split.hotHandle>=0){chrome.split.hotHandle=-1;SyncGripLooks();InvalidateRect(window,nullptr,FALSE);}return 0;
    }
    return DefWindowProcW(g,msg,w,l);
}
void SyncGrips(const SplitView& v){
    const size_t n=v.group?v.a.separators.size():0;
    while(splitGrips.size()<n){
        static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=GripProc;wc.hInstance=instance?instance:GetModuleHandleW(nullptr);wc.lpszClassName=L"BrowserSplitGrip";wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassW(&wc);registered=true;}
        HWND g=CreateWindowExW(WS_EX_LAYERED,L"BrowserSplitGrip",TR(split_grip_name),WS_CHILD,0,0,1,1,window,nullptr,instance?instance:GetModuleHandleW(nullptr),nullptr);if(!g){Log(L"SPLIT grip unavailable "+std::to_wstring(GetLastError()));return;}
        splitGrips.push_back(g);  // content (and hit-testing) comes with its first UpdateLayeredWindow
    }
    for(size_t i=0;i<splitGrips.size();++i){
        // Mode Focus: a grip under the floating panel would draw over it and take its clicks: it steps away while the panel is on screen.
        if(i<n){auto b=v.a.SeparatorHit(v.a.separators[i]);const RECT px{S(b.left),S(b.top),S(b.right),S(b.bottom)};
            if(FocusPanelOver(px)){if(IsWindowVisible(splitGrips[i]))ShowWindow(splitGrips[i],SW_HIDE);continue;}
            SetWindowPos(splitGrips[i],HWND_TOP,S(b.left),S(b.top),std::max(1,S(b.right)-S(b.left)),std::max(1,S(b.bottom)-S(b.top)),SWP_NOACTIVATE|SWP_SHOWWINDOW);}
        else if(IsWindowVisible(splitGrips[i]))ShowWindow(splitGrips[i],SW_HIDE);
    }
}
// Handle look per grip (Paper 08, "Poignee"): 8 x 36 DIP, #2F2F2F, fully rounded, centred on
// the gap. Hidden at rest; shown while hovered, dragged (kept even if the pointer leaves the
// target, no flicker) or keyboard-focused, where a 2 DIP white 50 % ring 1 DIP off is added.
// Opacity only, 120 ms (none with reduced motion): never a layout change.
struct GripLook{float alpha=0,target=0;bool ring=false;int w=0,h=0;float drawnAlpha=-1;bool drawnRing=false;UINT drawnDpi=0;std::vector<std::uint32_t> pixels;};
std::vector<GripLook> gripLooks;NativeChrome::Motion::Time gripLast{};
void RenderGrip(size_t i){
    HWND g=splitGrips[i];if(!(GetWindowLongW(g,GWL_STYLE)&WS_VISIBLE))return;RECT r;GetWindowRect(g,&r);const int w=r.right-r.left,h=r.bottom-r.top;if(w<=0||h<=0)return;
    auto& L=gripLooks[i];const UINT dpi=GetDpiForWindow(window);
    if(L.w==w&&L.h==h&&L.drawnDpi==dpi&&std::fabs(L.drawnAlpha-L.alpha)<1e-3f&&L.drawnRing==L.ring)return;
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);void* bits=nullptr;HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bmp||!bits){if(bmp)DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);return;}
    HGDIOBJ old=SelectObject(mem,bmp);auto* px=(std::uint32_t*)bits;
    const float k=dpi/96.f,half=4*k,along=18*k-half,a=L.alpha;const bool row=h>=w;const float cx=w/2.f,cy=h/2.f;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        float u=x+.5f-cx,v=y+.5f-cy;if(!row)std::swap(u,v);  // u across the separator, v along it
        const float dv=std::max(0.f,std::fabs(v)-along),s=std::sqrt(u*u+dv*dv);  // distance to the capsule's spine
        float A=1/255.f,C=0;
        if(a>0){const float hc=std::clamp(half-s+.5f,0.f,1.f)*a;C=47/255.f*hc+C*(1-hc);A=hc+A*(1-hc);
            if(L.ring){const float rc=(std::clamp(half+3*k-s+.5f,0.f,1.f)-std::clamp(half+k-s+.5f,0.f,1.f))*.5f*a;C=rc+C*(1-rc);A=rc+A*(1-rc);}}
        const auto B=(std::uint32_t)std::lround(std::clamp(C,0.f,1.f)*255);const auto Al=(std::uint32_t)std::max(1L,std::lround(std::clamp(A,0.f,1.f)*255));
        px[y*w+x]=(Al<<24)|(B<<16)|(B<<8)|B;}
    POINT src{0,0};SIZE size{w,h};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(g,screen,nullptr,&size,mem,&src,0,&blend,ULW_ALPHA)){L.w=w;L.h=h;L.drawnAlpha=L.alpha;L.drawnRing=L.ring;L.drawnDpi=dpi;if(splitTest)L.pixels.assign(px,px+(size_t)w*h);}
    else if(splitTest)Log(L"SPLIT grip render failed "+std::to_wstring(GetLastError()));
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
// --split-test captures: PrintWindow leaves layered children out; blend each grip's last frame.
void BlendLayered(HDC dc,POINT client,HWND h,const std::vector<std::uint32_t>& pixels,int w,int hgt){
    if(pixels.empty()||!(GetWindowLongW(h,GWL_STYLE)&WS_VISIBLE))return;RECT r;GetWindowRect(h,&r);MapWindowPoints(nullptr,window,(POINT*)&r,2);
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-hgt;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp){DeleteDC(mem);return;}
    memcpy(bits,pixels.data(),pixels.size()*4);auto old=SelectObject(mem,bmp);BLENDFUNCTION b{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    GdiAlphaBlend(dc,client.x+r.left,client.y+r.top,w,hgt,mem,0,0,w,hgt,b);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
}
void BlendGrips(HDC dc,POINT client){
    BlendCorners(dc,client);
    for(size_t i=0;i<splitGrips.size()&&i<gripLooks.size();++i){auto& L=gripLooks[i];if(L.pixels.empty()||!(GetWindowLongW(splitGrips[i],GWL_STYLE)&WS_VISIBLE))continue;
        RECT r;GetWindowRect(splitGrips[i],&r);MapWindowPoints(nullptr,window,(POINT*)&r,2);
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=L.w;bi.bmiHeader.biHeight=-L.h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp){DeleteDC(mem);continue;}
        memcpy(bits,L.pixels.data(),L.pixels.size()*4);auto old=SelectObject(mem,bmp);BLENDFUNCTION b{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        GdiAlphaBlend(dc,client.x+r.left,client.y+r.top,L.w,L.h,mem,0,0,L.w,L.h,b);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);}
}
bool GripsAnimating(){for(size_t i=0;i<gripLooks.size()&&i<splitGrips.size();++i)if(gripLooks[i].alpha!=gripLooks[i].target)return true;return false;}
void SyncGripLooks(){
    gripLooks.resize(splitGrips.size());const auto& d=chrome.split;const bool instant=motion.ReducedMotion()||motionMinimized;bool moving=false;
    for(size_t i=0;i<splitGrips.size();++i){auto& L=gripLooks[i];const int n=(int)i;
        L.target=d.shown&&n<(int)d.handles.size()&&(n==d.hotHandle||n==d.focusedHandle||(sepDrag.active&&n==sepDrag.handle))?1.f:0.f;L.ring=d.shown&&n==d.focusedHandle;
        if(instant)L.alpha=L.target;if(L.alpha!=L.target)moving=true;RenderGrip(i);}
    if(moving){gripLast=NativeChrome::Motion::Now();ScheduleMotion(gripLast);}
}
void GripFrame(NativeChrome::Motion::Time now){
    if(!GripsAnimating())return;const float dt=std::clamp(std::chrono::duration<float>(now-gripLast).count(),0.f,.05f);gripLast=now;
    for(size_t i=0;i<gripLooks.size()&&i<splitGrips.size();++i){auto& L=gripLooks[i];const float step=dt/.12f;
        L.alpha=L.target>L.alpha?std::min(L.target,L.alpha+step):std::max(L.target,L.alpha-step);RenderGrip(i);}
}
// ---- Rounded page corners ----------------------------------------------------------------
// The window is round; its pages (child windows of another process) are rectangles, so
// their outer bottom corners used to stay square. A small layered child window sits above
// each outer bottom corner: canvas outside a 6 DIP arc (antialiased), fully transparent
// inside it, where the page shows and keeps the pointer. It really hides the page there,
// for a single page and for split views (side by side: the outer panels; stacked: the
// bottom one). Top corners meet the toolbar and stay square, as in Paper.
struct PageCorner{SV::Box page;bool right=false,top=false;};
// The page card's corner (Designer, 24 nov.): 6 DIP with the top bar, 8 DIP with the side panel (the column's cards).
// Mode Focus (Paper 1J9P-0): 6, its four corners round; they step away while the floating panel covers them.
float PageRadiusDip(){return chrome.floating?FM::pageRadius:chrome.sidebar?NativeChrome::Sidebar::Zone::pageRadius:NativeChrome::Sidebar::Zone::topBarPageRadius;}
std::vector<PageCorner> PageCorners(){
    // Val (24 nov.): the page card keeps its rounded corners and 4 DIP inset when the window is MAXIMIZED too; only F11
    // fullscreen is square, edge to edge. One switch to go back to square-when-maximized: kRoundWhenMaximized = false.
    constexpr bool kRoundWhenMaximized=true;
    std::vector<PageCorner> out;if((!kRoundWhenMaximized&&IsZoomed(window))||InFullscreen())return out;
    const auto c=ContentBox();auto v=CurrentSplit();std::vector<SV::Box> boxes;
    if(v.group){for(auto& s:v.a.slots)if(!s.rail)boxes.push_back(s.box);}else{auto t=Active();if(t&&t->shown)boxes.push_back(c);}
    const bool leftCovered=chrome.sidebar&&(chrome.sidebarOverlay||(chrome.sideHidden&&chrome.sideShown));
    for(auto& b:boxes){for(int top=0;top<2;++top){if(std::fabs((top?b.top:b.bottom)-(top?c.top:c.bottom))>.5f)continue;if(!leftCovered&&std::fabs(b.left-c.left)<.5f)out.push_back({b,false,top!=0});if(std::fabs(b.right-c.right)<.5f)out.push_back({b,true,top!=0});}}
    return out;
}
std::vector<HWND> cornerMasks;struct CornerLook{int x=0,y=0,size=0;bool right=false,top=false;float radius=0;UINT dpi=0;unsigned edge=0;int edgeAlpha=255;std::vector<std::uint32_t> pixels;};std::vector<CornerLook> cornerLooks;
LRESULT CALLBACK CornerProc(HWND h,UINT msg,WPARAM w,LPARAM l){if(msg==WM_NCHITTEST)return HTCLIENT;if(msg==WM_ERASEBKGND)return 1;if(msg==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,msg,w,l);}
// `edge` (#127 « Redimensionner »): 0, or 0x1RRGGBB: the column edge's line, whose arc (radius pageRadius + 1, 2 DIP wide,
// faded over 12 DIP from its end) crosses this left corner's square (Chrome::PaintSideEdge draws the rest).
void RenderCorner(size_t i,bool right,int size,UINT dpi,bool top=false,unsigned edge=0,float edgeAlpha=1){
    const int alpha255=(int)std::lround(std::clamp(edgeAlpha,0.f,1.f)*255);
    auto& L=cornerLooks[i];const float radius=PageRadiusDip();if(L.size==size&&L.radius==radius&&L.right==right&&L.top==top&&L.dpi==dpi&&L.edge==edge&&L.edgeAlpha==alpha255)return;HWND g=cornerMasks[i];
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=size;bi.bmiHeader.biHeight=-size;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);void* bits=nullptr;HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp||!bits){if(bmp)DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);return;}
    HGDIOBJ old=SelectObject(mem,bmp);auto* px=(std::uint32_t*)bits;const float r=radius*dpi/96.f;
    // Mask origin: the corner square's top-left. Arc centre: r inside the page from the corner.
    const float cx=right?(float)size-r:r,cy=top?r:(float)size-r;const std::uint32_t canvas=GetRValue(BG);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){const float px0=x+.5f,py0=y+.5f;const bool beyond=right?px0>cx:px0<cx;float a=0;
        float lr=0,lg=0,lb=0,la=0;
        if(beyond&&(top?py0<cy:py0>cy)){const float d=std::hypot(px0-cx,py0-cy);a=std::clamp(d-r+.5f,0.f,1.f);  // outside the arc: canvas
            if(edge&&!right){const float k=dpi/96.f,ring=r+NativeChrome::Sidebar::Resize::line/2*k,half=NativeChrome::Sidebar::Resize::line/2*k;
                const float cover=std::clamp(half+.5f-std::fabs(d-ring),0.f,1.f),fromEnd=(top?py0:(float)size-py0)/k+1;
                la=cover*std::clamp(fromEnd/NativeChrome::Sidebar::Resize::fade,0.f,1.f)*alpha255/255.f;lr=((edge>>16)&255)*la;lg=((edge>>8)&255)*la;lb=(edge&255)*la;}}
        const float A0=la+a*(1-la);const auto A=(std::uint32_t)std::lround(A0*255);
        const auto R=(std::uint32_t)std::lround(lr+canvas*a*(1-la)),G=(std::uint32_t)std::lround(lg+canvas*a*(1-la)),B=(std::uint32_t)std::lround(lb+canvas*a*(1-la));px[y*size+x]=(A<<24)|(R<<16)|(G<<8)|B;}
    POINT src{0,0};SIZE sz{size,size};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(g,screen,nullptr,&sz,mem,&src,0,&blend,ULW_ALPHA)){L.size=size;L.radius=radius;L.right=right;L.top=top;L.dpi=dpi;L.edge=edge;L.edgeAlpha=alpha255;if(splitTest)L.pixels.assign(px,px+(size_t)size*size);}
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
void BlendLayered(HDC dc,POINT client,HWND h,const std::vector<std::uint32_t>& pixels,int w,int hgt);
void BlendCorners(HDC dc,POINT client){for(size_t i=0;i<cornerMasks.size()&&i<cornerLooks.size();++i)BlendLayered(dc,client,cornerMasks[i],cornerLooks[i].pixels,cornerLooks[i].size,cornerLooks[i].size);}
void SyncCornerMasks(){
    const auto corners=halted?std::vector<PageCorner>{}:PageCorners();const UINT dpi=GetDpiForWindow(window);const int size=(int)std::ceil(PageRadiusDip()*dpi/96.f)+1;
    while(cornerMasks.size()<corners.size()){
        static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=CornerProc;wc.hInstance=instance?instance:GetModuleHandleW(nullptr);wc.lpszClassName=L"BrowserPageCorner";RegisterClassW(&wc);registered=true;}
        HWND g=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,L"BrowserPageCorner",L"",WS_CHILD,0,0,1,1,window,nullptr,instance?instance:GetModuleHandleW(nullptr),nullptr);if(!g)break;
        cornerMasks.push_back(g);cornerLooks.push_back({});
    }
    for(size_t i=0;i<cornerMasks.size();++i){
        if(i>=corners.size()){if(GetWindowLongW(cornerMasks[i],GWL_STYLE)&WS_VISIBLE)ShowWindow(cornerMasks[i],SW_HIDE);continue;}
        const auto& c=corners[i];const int x=c.right?S(c.page.right)-size:S(c.page.left),y=c.top?S(c.page.top):S(c.page.bottom)-size;
        if(c.top&&FocusHoleOver({x,y,x+size,y+size})){if(GetWindowLongW(cornerMasks[i],GWL_STYLE)&WS_VISIBLE)ShowWindow(cornerMasks[i],SW_HIDE);continue;}
        const bool keyed=keyboardFocus==NativeChrome::Chrome::SidebarEdgeId&&keyboardModality;
        const unsigned edge=!c.right&&chrome.sidebar&&std::fabs(c.page.left-chrome.side.page.left)<.5f?chrome.EdgeLineColor(keyed):0;
        SetWindowPos(cornerMasks[i],HWND_TOP,x,y,size,size,SWP_NOACTIVATE|SWP_SHOWWINDOW);RenderCorner(i,c.right,size,dpi,c.top,edge,keyed?1.f:chrome.EdgeAlpha(NativeChrome::Motion::Now()));
    }
}
// ---- Resize readiness probe ----------------------------------------------------------
// After a separator drag the resized pages are shown again only once Chromium has
// rendered them at their new size. ICoreWebView2::CapturePreview (local, no network)
// is the probe: the first capture taken after the resize already has the new size once
// the frame is ready. Only its PNG header is read (the size); nothing is kept.
void CapturePanel(const std::shared_ptr<Tab>& t,std::function<void(bool,UINT,UINT)> done){
    if(!t||!t->view.p||!t->shown||halted){if(done)done(false,0,0);return;}
    IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))){if(done)done(false,0,0);return;}
    std::weak_ptr<Tab> weak=t;const std::uint64_t stamp=t->navStamp;const ULONGLONG started=GetTickCount64();
    auto cb=Callback<ICoreWebView2CapturePreviewCompletedHandler,HRESULT>([weak,stream,stamp,started,done](HRESULT hr)->HRESULT{
        auto tab=weak.lock();UINT w=0,h=0;bool ok=tab&&!tab->closed&&!halted&&SUCCEEDED(hr)&&tab->navStamp==stamp;
        if(ok){IWICImagingFactory* wic=chrome.Imaging();Com<IWICBitmapDecoder> decoder;Com<IWICBitmapFrameDecode> frame;LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);
            ok=wic&&SUCCEEDED(wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnDemand,&decoder.p))&&SUCCEEDED(decoder->GetFrame(0,&frame.p))&&SUCCEEDED(frame->GetSize(&w,&h))&&w&&h;}
        stream->Release();
        if(splitTest&&tab)Log(L"SPLIT capture "+tab->title+L": "+(ok?std::to_wstring(w)+L"x"+std::to_wstring(h):L"failed")+L" in "+std::to_wstring(GetTickCount64()-started)+L" ms");
        if(done)done(ok,w,h);return S_OK;});
    HRESULT hr=t->view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream,cb);cb->Release();
    if(FAILED(hr)){stream->Release();if(done)done(false,0,0);}
}
// A page that takes focus (click, Tab inside it, F6) becomes the active panel.
void ActivatePanel(const std::shared_ptr<Tab>& tab){
    if(!tab||!tab->shown||halted)return;
    // Focus taken with a mouse button down is a click: the discreet ring. Otherwise (Tab inside
    // the page, F6) the strong keyboard ring. Hover never activates a panel.
    const bool keyboard=!(GetAsyncKeyState(VK_LBUTTON)<0||GetAsyncKeyState(VK_RBUTTON)<0||GetAsyncKeyState(VK_MBUTTON)<0);
    if(tab==Active()){if(keyboard!=keyboardModality){keyboardModality=keyboard;SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);}return;}
    const int i=TabIndexOf(tab);if(i<0)return;
    current=i;tab->usedAt=++useClock;editing=false;ShowWindow(address,SW_HIDE);ClearChromeMotion();motion.SelectTab((int)tab->serial,NativeChrome::Motion::Now());
    keyboardModality=keyboard;
    SyncSplitDecor();Update();ScheduleMotion(NativeChrome::Motion::Now());AnnotationTabChanged();
}
void AnnounceSplit(const std::wstring& text);
// Strip placement: `t` right before (after=false) or right after `anchor`.
void PlaceNextTo(const std::shared_ptr<Tab>& t,const std::shared_ptr<Tab>& anchor,bool after){
    int from=TabIndexOf(t),a=TabIndexOf(anchor);if(from<0||a<0||t==anchor)return;int to=after?a+1:a;if(from<to)--to;
    to=std::clamp(to,0,(int)pages.size()-1);if(from!=to)MoveTab(from,to);
}
// Where a drop would put the dragged site, as a whole phrase around the other site's name (« à gauche de X »).
std::wstring SideText(SV::Side s,const std::wstring& other){switch(s){case SV::Side::Left:return TRF(split_left_of,other);case SV::Side::Right:return TRF(split_right_of,other);case SV::Side::Top:return TRF(split_above,other);default:return TRF(split_below,other);}}
std::wstring TargetText(const SV::Target& t,const SplitView& v,const std::shared_ptr<Tab>& partner,const std::shared_ptr<Tab>& dragged){
    switch(t.kind){
    case SV::Kind::Split:return SideText(t.side,TabLabel(partner));
    case SV::Kind::Replace:return TRF(split_replaces,TabLabel(v.members[t.index]));
    case SV::Kind::Swap:return TRF(split_instead_of,TabLabel(v.members[t.index]));
    case SV::Kind::Insert:case SV::Kind::Move:{
        const bool row=v.a.orientation==SV::Orientation::Row;const int n=(int)v.members.size();
        auto skip=[&](int i){return i>=0&&i<n&&v.members[i]==dragged;};
        int before=t.index-1,after=t.index;while(skip(before))--before;while(skip(after))++after;
        if(before>=0&&after<n)return TRF(split_between,TabLabel(v.members[before]),TabLabel(v.members[after]));
        if(after<n)return SideText(row?SV::Side::Left:SV::Side::Top,TabLabel(v.members[after]));
        if(before>=0)return SideText(row?SV::Side::Right:SV::Side::Bottom,TabLabel(v.members[before]));
        return L"";}
    default:return L"";}
}
std::wstring RefusalTitle(int needed){return needed<=2?std::wstring(TR(split_refusal_two)):TRF(split_refusal_nth,std::to_wstring(needed));}
std::wstring RefusalDetail(SV::Orientation o){return o==SV::Orientation::Row?TR(split_refusal_width):TR(split_refusal_height);}
// Applies a drop (or the equivalent menu command): one code path for pointer and keyboard.
// Takes its own references: callers may pass slots of `pages`, which this reorders.
void ApplySplitTarget(std::shared_ptr<Tab> tab,const SV::Target& target,std::shared_ptr<Tab> partner,const SplitView& v){
    if(!tab||halted)return;const std::wstring said=TargetText(target,v,partner,tab);
    switch(target.kind){
    case SV::Kind::Split:{if(!partner||partner==tab)return;LeaveGroup(tab);const int g=nextSplitGroup++;splitGroups[g]=SV::OrientationOf(target.side);
        partner->group=tab->group=g;partner->weight=tab->weight=1;partner->usedAt=++useClock;PlaceNextTo(tab,partner,target.side==SV::Side::Right||target.side==SV::Side::Bottom);break;}
    case SV::Kind::Insert:{const int n=(int)v.members.size();if(!v.group||target.index<0||target.index>n)return;auto w=SV::InsertWeights(v.weights,target.index);LeaveGroup(tab);tab->group=v.group;
        for(int i=0,j=0;i<=n;++i){if(i==target.index)tab->weight=w[i];else v.members[j++]->weight=w[i];}
        if(target.index<n)PlaceNextTo(tab,v.members[target.index],false);else PlaceNextTo(tab,v.members[n-1],true);break;}
    case SV::Kind::Replace:{if(target.index<0||target.index>=(int)v.members.size())return;auto old=v.members[target.index];if(old==tab)return;
        LeaveGroup(tab);tab->group=v.group;tab->weight=old->weight;PlaceNextTo(tab,old,false);LeaveGroup(old);
        // The replaced site is not closed: it becomes a tab again, right after the group.
        auto rest=Members(v.group);if(!rest.empty())PlaceNextTo(old,rest.back(),true);break;}
    case SV::Kind::Swap:{if(target.index<0||target.index>=(int)v.members.size())return;auto other=v.members[target.index];const int a=TabIndexOf(tab),b=TabIndexOf(other);if(a<0||b<0||a==b)return;
        auto active=Active();std::swap(pages[a],pages[b]);std::swap(tab->weight,other->weight);current=TabIndexOf(active);break;}
    case SV::Kind::Move:{const int n=(int)v.members.size();if(target.index<0||target.index>n)return;
        if(target.index<n)PlaceNextTo(tab,v.members[target.index],false);else PlaceNextTo(tab,v.members[n-1],true);break;}
    default:return;
    }
    DissolveSmallGroups();tab->usedAt=++useClock;const int i=TabIndexOf(tab);if(i>=0)Select(i);
    AnnounceSplit(TRF(split_announce_placed,TabLabel(tab),said));
}
// ---- Drag overlay animation (springs from src/split-view.h, display-paced frames)
bool resizePreviewFading();void ClearResizePreviewNow();
// #127 motion spec: the lifted card grows to x1.02 (spring 1.0 / 0.15 s) around the point it was grabbed (`anchor`, a
// fraction of the card). `cardScale` is in percent (100 = rest): the springs settle within 0.25 of their unit.
struct SplitAnim{SV::BoxSpring plate,card;SV::Spring plateOpacity,cardOpacity,tileOpacity,cardScale;float fade=.30f,anchorX=.5f,anchorY=.5f;bool returning=false,committing=false;NativeChrome::Motion::Time last{};NativeChrome::SplitUI::Frame frame;} splitAnim;
bool SplitAnimating(){return splitOverlay.Visible()&&(!splitAnim.plate.Settled()||!splitAnim.card.Settled()||!splitAnim.plateOpacity.Settled()||!splitAnim.cardOpacity.Settled()||!splitAnim.tileOpacity.Settled()||!splitAnim.cardScale.Settled());}
void RenderSplitOverlay(){
    auto& f=splitAnim.frame;f.plate.box=splitAnim.plate.Value();f.plate.opacity=std::clamp(splitAnim.plateOpacity.x,0.f,1.f);f.plate.shown=f.plate.opacity>.01f;
    f.card.box=splitAnim.card.Value();f.card.opacity=std::clamp(splitAnim.cardOpacity.x,0.f,1.f);f.tileOpacity=std::clamp(splitAnim.tileOpacity.x,0.f,1.f);
    if(const float s=splitAnim.cardScale.ready?splitAnim.cardScale.x/100:1;s!=1){auto& b=f.card.box;const float ax=b.left+(b.right-b.left)*splitAnim.anchorX,ay=b.top+(b.bottom-b.top)*splitAnim.anchorY;b={ax-(ax-b.left)*s,ay-(ay-b.top)*s,ax+(b.right-ax)*s,ay+(b.bottom-ay)*s};}
    splitOverlay.Show(f);
}
void FinishSplitAnimation(){splitOverlay.Hide();splitAnim.returning=splitAnim.committing=false;splitAnim.frame={};chrome.liftedIndex=-1;SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);}
void SplitFrame(NativeChrome::Motion::Time now){
    if(!splitOverlay.Visible())return;
    const float dt=std::clamp(std::chrono::duration<float>(now-splitAnim.last).count(),0.f,.05f);splitAnim.last=now;
    splitAnim.plate.Step(dt,.20f);splitAnim.card.Step(dt,.35f);splitAnim.plateOpacity.Step(dt,splitAnim.fade);splitAnim.cardOpacity.Step(dt,.20f);splitAnim.tileOpacity.Step(dt,.12f);splitAnim.cardScale.Step(dt,.15f);   /* #127 motion spec: the zone morphs 1.0 / 0.2 s */
    if(resizePreviewFading()&&splitAnim.tileOpacity.x<=.01f&&splitAnim.tileOpacity.Settled())ClearResizePreviewNow();
    if((splitAnim.returning||splitAnim.committing)&&!SplitAnimating()){FinishSplitAnimation();return;}
    RenderSplitOverlay();
}
// #127 column (section « Panneau latéral »): its right edge is the lift line, the lift empties its row and sizes the card.
float SidebarDropEdge();void SidebarLift();
// Lift / track / drop. Returns true while the tab is lifted (the strip reorder is off).
bool SplitDragMove(POINT point){
    if(!tabDrag.tab||halted)return false;const float d=GetDpiForWindow(window)/96.f,x=point.x/d,y=point.y/d;
    const bool reduced=motion.ReducedMotion();auto& f=splitAnim.frame;
    if(!tabDrag.lifted){
        // Lift below the address row (the column: past its right edge); one tab alone has no partner and stays in the strip.
        // Mode Focus: below the floating panel.
        if((chrome.sidebar?x<SidebarDropEdge():y<ChromeBottomDip())||pages.size()<2)return false;
        if(splitAnim.returning||splitAnim.committing)FinishSplitAnimation();
        tabDrag.lifted=true;tabDrag.target={};chrome.liftedIndex=TabIndexOf(tabDrag.tab);chrome.dragIndex=-1;chrome.dragOffset=0;splitResize=-1;chrome.split.hotHandle=-1;
        if(chrome.sidebar)SidebarLift();
        splitAnim=SplitAnim{};f.card.shown=true;f.card.title=chrome.sidebar?TabLabel(tabDrag.tab):SiteLabel(tabDrag.tab);f.card.favicon=tabDrag.tab->favicon.image;   /* the column's card is its row lifted: the row's title (Paper 24BU-0) */
        splitAnim.cardOpacity.Snap(1);splitAnim.plateOpacity.Snap(0);splitAnim.last=NativeChrome::Motion::Now();
        {const float w=chrome.sidebar?tabDrag.cardW:chrome.strip.tab;splitAnim.anchorX=w>0?std::clamp(tabDrag.grab/w,0.f,1.f):.5f;splitAnim.anchorY=std::clamp(tabDrag.grabY/32,0.f,1.f);
         splitAnim.cardScale.Snap(100);if(!reduced)splitAnim.cardScale.Set(102);}   // lifted: x1.02 around the grab point (none under reduced motion)
        AnnounceSplit(TRF(split_announce_drag,SiteLabel(tabDrag.tab)));
        SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);
    }else if(chrome.sidebar?x<SidebarDropEdge()-20:y<SV::reattachY+ChromeOrigin().y){
        // Back above the address row (into the column): reordering in the strip resumes.
        tabDrag.lifted=false;tabDrag.target={};chrome.liftedIndex=-1;splitOverlay.Hide();splitAnim=SplitAnim{};
        SetCursor(LoadCursor(nullptr,IDC_ARROW));SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);return false;
    }
    auto v=CurrentSplit();SV::Target target;std::shared_ptr<Tab> partner;
    if(chrome.sidebar?x>=SidebarDropEdge()&&y>=ContentBox().top:y>=ChromeBottomDip()){
        if(v.group){int dragged=-1;for(int i=0;i<(int)v.members.size();++i)if(v.members[i]==tabDrag.tab)dragged=i;target=SV::GroupTarget(v.a,v.weights,dragged,x,y,v.min,&tabDrag.target);}
        else if((partner=SplitPartner(tabDrag.tab)))target=SV::SingleTarget(ContentBox(),x,y,SV::MinExtent(SV::Orientation::Row,PageZoom()),SV::MinExtent(SV::Orientation::Column,PageZoom()),&tabDrag.target);
    }
    const bool changed=!(target==tabDrag.target);tabDrag.target=target;
    // The tab follows 1:1 from where it was grabbed: never animated.
    const float w=chrome.sidebar?tabDrag.cardW:chrome.strip.tab;splitAnim.card.Snap({x-tabDrag.grab,y-tabDrag.grabY,x-tabDrag.grab+w,y-tabDrag.grabY+32});
    const bool valid=target.kind!=SV::Kind::None&&target.kind!=SV::Kind::Refuse;
    if(valid){
        // #127 motion spec: the zone morphs from one zone to the next (position and size, never gone and back); reduced
        // motion: it jumps there and fades in (80 ms).
        if(!f.plate.shown||reduced)splitAnim.plate.Snap(target.preview);else splitAnim.plate.Set(target.preview);
        if(reduced&&changed)splitAnim.plateOpacity.Snap(0);
        // The chip names only the dragged tab; where it lands is shown by the preview itself
        // and spoken to screen readers (TargetText below), never written in the chip.
        splitAnim.plateOpacity.Set(1);f.plate.title=SiteLabel(tabDrag.tab);f.plate.detail.clear();f.plate.favicon=tabDrag.tab->favicon.image;
    }else splitAnim.plateOpacity.Set(0);
    splitAnim.fade=reduced?.08f:valid?.20f:.10f;  // the preview answers within ~200 ms; outside any zone it fades out in 100 ms (80 ms reduced)
    f.pill.shown=target.kind==SV::Kind::Refuse;if(f.pill.shown){f.pill.x=x+14;f.pill.y=y+22;f.pill.title=RefusalTitle(target.needed);f.pill.detail=RefusalDetail(target.orientation);}
    SetCursor(LoadCursor(nullptr,target.kind==SV::Kind::Refuse?IDC_NO:IDC_ARROW));
    if(changed&&target.kind!=SV::Kind::None)AnnounceSplit(target.kind==SV::Kind::Refuse?RefusalTitle(target.needed)+L", "+RefusalDetail(target.orientation):TRF(split_announce_target,SiteLabel(tabDrag.tab),TargetText(target,v,partner,tabDrag.tab)));
    splitAnim.last=NativeChrome::Motion::Now();RenderSplitOverlay();ScheduleMotion(splitAnim.last);
    return true;
}
// Release while lifted. A refused or empty target cancels (the tab flies home).
void CommitSplitDrag(){
    auto tab=tabDrag.tab;auto target=tabDrag.target;auto v=CurrentSplit();auto partner=v.group?nullptr:SplitPartner(tab);
    if(!tab||target.kind==SV::Kind::None||target.kind==SV::Kind::Refuse||(target.kind==SV::Kind::Split&&!partner)){
        if(target.kind==SV::Kind::Refuse)AnnounceSplit(TRF(split_announce_drag_canceled_reason,RefusalTitle(target.needed)));
        EndSplitDrag(true);return;
    }
    tabDrag.lifted=false;chrome.liftedIndex=-1;
    ApplySplitTarget(tab,target,partner,v);
    // The preview fades out once the pages sit where it showed them (120 ms).
    splitAnim.frame.card.shown=false;splitAnim.frame.pill.shown=false;splitAnim.fade=.12f;splitAnim.plateOpacity.Set(0);splitAnim.committing=true;
    if(motion.ReducedMotion()||!splitOverlay.Visible())FinishSplitAnimation();else{splitAnim.last=NativeChrome::Motion::Now();RenderSplitOverlay();ScheduleMotion(splitAnim.last);}
}
// Escape, a lost capture or Alt+Tab: the order from before the press comes back and a
// lifted tab returns along its path (or reappears at once under reduced motion).
void EndSplitDrag(bool restore){
    if(!tabDrag.tab)return;
    if(restore&&tabDrag.active&&tabDrag.origin>=0){const int i=TabIndexOf(tabDrag.tab);if(i>=0&&i!=tabDrag.origin&&tabDrag.origin<(int)pages.size())MoveTab(i,tabDrag.origin);}
    if(tabDrag.lifted){
        tabDrag.lifted=false;const int i=TabIndexOf(tabDrag.tab);
        if(restore&&i>=0&&!motion.ReducedMotion()&&splitOverlay.Visible()){
            chrome.liftedIndex=i;auto s=chrome.strip.Surface(i);splitAnim.card.Set({s.left,s.top,s.right,s.bottom});
            if(chrome.sidebar&&i<(int)chrome.SideColumn().tabs.size()){const auto& r=chrome.SideColumn().tabs[i];splitAnim.card.Set({r.left,r.top,r.right,r.bottom});}   /* #127: back to its row in the column */
            splitAnim.fade=.30f;splitAnim.plateOpacity.Set(0);splitAnim.cardScale.Set(100);splitAnim.frame.pill.shown=false;splitAnim.returning=true;
            splitAnim.last=NativeChrome::Motion::Now();ScheduleMotion(splitAnim.last);
            if(restore)AnnounceSplit(TR(split_announce_drag_canceled));
        }else FinishSplitAnimation();
    }
    SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);
}
// A group member dropped in the strip away from every other member leaves its group.
void FinishReorder(){
    auto t=tabDrag.tab;if(!t||!t->group)return;const int i=TabIndexOf(t);if(i<0)return;
    const bool adjacent=(i>0&&pages[i-1]->group==t->group)||(i+1<(int)pages.size()&&pages[i+1]->group==t->group);
    if(!adjacent){LeaveGroup(t);DissolveSmallGroups();AnnounceSplit(TRF(split_announce_back_to_tab,TabLabel(t)));}
    ApplyPages();
}
// ---- Separators: pointer
void SetExtents(const SplitView& v,const std::vector<float>& extents){
    float oldVisible=0,newVisible=0;for(size_t i=0;i<v.a.slots.size();++i)if(!v.a.slots[i].rail){oldVisible+=v.weights[i];newVisible+=extents[i];}
    const float scale=oldVisible>0?newVisible/oldVisible:1;
    for(size_t i=0;i<v.a.slots.size();++i)v.members[i]->weight=v.a.slots[i].rail?std::max(1e-3f,v.weights[i]*scale):std::max(1.f,extents[i]);
}
// ---- Resize placeholders (separator) ----------------------------------------------------
// Paper 09, user direction. Past a 3 DIP threshold, the two pages beside the dragged
// separator are frozen at their bounds (no reflow at all while dragging) and covered by
// opaque #171717 placeholders that follow their new rectangles 1:1, each with the site's
// identity centred: real favicon, short name (SiteName), bare domain. The separator turns
// accent (2 DIP line and 8 x 36 handle); the others stay neutral. On release the real
// bounds are applied and each page is probed until it has rendered at its new size; only
// then do the placeholders fade (120 ms, none with reduced motion). Escape or a lost
// capture restores the sizes: the pages never moved, the placeholders simply go.
struct ResizePreview{bool active=false,settling=false,fading=false;int slot=-1,ready=0;std::vector<std::shared_ptr<Tab>> tabs;ULONGLONG released=0;} resizePreview;
constexpr float ResizeThreshold=3;
bool ResizePlaceholdersUp(){return resizePreview.active||resizePreview.settling||resizePreview.fading;}
void ClearResizePreview(){for(auto& t:resizePreview.tabs)if(t)t->frozen=false;resizePreview=ResizePreview{};splitAnim.frame.tiles.clear();splitAnim.frame.gaps.clear();splitAnim.frame.corners.clear();}
// Still the same pages in the same visible group? Otherwise the placeholders go.
bool ResizePreviewValid(){
    if(!resizePreview.active&&!resizePreview.settling&&!resizePreview.fading)return true;
    const int g=VisibleGroup();for(auto& t:resizePreview.tabs)if(!t||t->closed||t->group!=g||!g||!t->shown)return false;return true;
}
// "github.com" from "https://www.github.com/a/b?c#d": web hosts only, no www, port, path, query or
// trailing slash. Local files, about: and other schemes have no domain line (never invented).
std::wstring BareDomain(const std::wstring& uri){
    auto u=NativeChrome::SiteName::Parse(uri);if((u.scheme!=L"http"&&u.scheme!=L"https")||u.host.empty())return L"";
    std::wstring h=u.host;if(h.rfind(L"www.",0)==0&&h.size()>4)h=h.substr(4);while(!h.empty()&&h.back()==L'.')h.pop_back();return h;
}
void RenderResizePreview(){
    auto v=CurrentSplit();auto& f=splitAnim.frame;f.tiles.clear();f.gaps.clear();f.corners.clear();
    if(v.group&&(resizePreview.active||resizePreview.settling||resizePreview.fading)){
        // Identity read now, from the tab itself: a navigation mid-drag shows the new site, never a stale one.
        for(auto& t:resizePreview.tabs){const int m=(int)(std::find(v.members.begin(),v.members.end(),t)-v.members.begin());const int s=v.a.SlotOf(m);if(s<0||v.a.slots[s].rail)continue;
            NativeChrome::SplitUI::Tile tile;tile.box=v.a.slots[s].box;tile.icon=t->favicon.image;tile.name=SiteLabel(t);tile.domain=BareDomain(t->uri);f.tiles.push_back(tile);}
        if(resizePreview.slot>=0&&resizePreview.slot+1<(int)v.a.slots.size())f.gaps.push_back(v.a.SeparatorBox(resizePreview.slot));
        for(auto& c:PageCorners())if(!c.top)f.corners.push_back({c.right?c.page.right:c.page.left,c.page.bottom,c.right});
    }
    splitAnim.last=NativeChrome::Motion::Now();RenderSplitOverlay();
}
bool StartResizePreview(const SplitView& v,int slot){
    if(resizePreview.active)return true;if(!v.group||slot<0||slot+1>=(int)v.a.slots.size()||halted)return false;
    auto a=v.members[v.a.slots[slot].member],b=v.members[v.a.slots[slot+1].member];if(!a->shown||!b->shown)return false;
    if(resizePreview.settling||resizePreview.fading)ClearResizePreview();
    resizePreview.active=true;resizePreview.slot=slot;resizePreview.tabs={a,b};a->frozen=b->frozen=true;splitAnim.tileOpacity.Snap(1);SyncSplitDecor();
    if(splitTest)Log(L"SPLIT preview: "+a->title+L" + "+b->title+L" frozen, #171717 placeholders stand in");
    return true;
}
void FadeResizePreview(){
    resizePreview.settling=false;resizePreview.fading=true;
    if(splitTest)Log(L"SPLIT preview: pages rendered at their new size after "+std::to_wstring(GetTickCount64()-resizePreview.released)+L" ms, fading");
    if(motion.ReducedMotion()){ClearResizePreview();RenderSplitOverlay();SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);return;}
    splitAnim.tileOpacity.Set(0);splitAnim.last=NativeChrome::Motion::Now();ScheduleMotion(splitAnim.last);
}
// Re-capture a released page until its frame has the new size (the capture itself is
// the "rendered" signal); 20 tries at most, then the fade happens anyway.
void ProbeResized(const std::shared_ptr<Tab>& t,int attempt){
    CapturePanel(t,[t,attempt](bool ok,UINT w,UINT h){
        if(!resizePreview.settling)return;const RECT b=t->placed;
        const bool match=ok&&std::abs((int)w-(int)(b.right-b.left))<=2&&std::abs((int)h-(int)(b.bottom-b.top))<=2;
        if(match||!ok||attempt>=20){if(splitTest&&!match)Log(L"SPLIT preview: "+t->title+L" not confirmed ("+std::to_wstring(attempt)+L" tries)");
            RenderResizePreview();if(++resizePreview.ready>=(int)resizePreview.tabs.size())FadeResizePreview();return;}
        ProbeResized(t,attempt+1);});
}
void SettleResizePreview(){
    if(!resizePreview.active)return;for(auto& t:resizePreview.tabs)t->frozen=false;resizePreview.active=false;resizePreview.settling=true;resizePreview.ready=0;
    resizePreview.released=GetTickCount64();ApplyPages();RenderResizePreview();auto tabs=resizePreview.tabs;for(auto& t:tabs)ProbeResized(t,0);
}
bool resizePreviewFading(){return resizePreview.fading;}
void ClearResizePreviewNow(){ClearResizePreview();RenderSplitOverlay();SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);}
void EqualizeSplit(const SplitView& v){auto e=SV::Extents(v.a);float sum=0;int n=0;for(size_t i=0;i<e.size();++i)if(!v.a.slots[i].rail){sum+=e[i];++n;}for(size_t i=0;i<e.size();++i)if(!v.a.slots[i].rail)e[i]=sum/std::max(1,n);SetExtents(v,e);ApplyPages();}
void AnnounceSeparator(){
    auto v=CurrentSplit();if(!v.group||splitResize<0||splitResize>=(int)v.a.separators.size())return;const int s=v.a.separators[splitResize];
    float sum=0;for(auto& slot:v.a.slots)if(!slot.rail)sum+=SV::Extent(slot.box,v.a.orientation);
    const int pct=(int)std::lround(100*SV::Extent(v.a.slots[s].box,v.a.orientation)/std::max(1.f,sum));
    AnnounceSplit(TRF(split_announce_divider,TabLabel(v.members[v.a.slots[s].member]),I18n::Percent(pct)));
}
bool SplitPointerDown(float x,float y){
    splitResize=-1;auto v=CurrentSplit();if(!v.group)return false;
    for(size_t i=0;i<chrome.split.rails.size();++i){auto r=chrome.split.rails[i];if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom){const int tab=chrome.split.railTabs[i];if(tab>=0){Select(tab);AnnounceSplit(TRF(split_announce_reopened,TabLabel(pages[tab])));}return true;}}
    const int hit=SeparatorAt(x,y,v);if(hit<0)return false;
    // Double-click on a separator: equal shares for the whole group.
    const DWORD now=GetTickCount();if(hit==lastSeparator&&now-lastSeparatorClick<=GetDoubleClickTime()){lastSeparatorClick=0;EqualizeSplit(v);return true;}
    lastSeparator=hit;lastSeparatorClick=now;
    const float pos=SV::Along(x,y,v.a.orientation);sepDrag={true,hit,v.a.separators[hit],pos,SV::Extents(v.a),pos,(DWORD)GetMessageTime(),0};
    if(!splitTest)SetCapture(window);SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);return true;
}
void SeparatorMove(float x,float y){
    auto v=CurrentSplit();if(!v.group||sepDrag.slot+1>=(int)v.a.slots.size()){sepDrag.active=false;return;}
    const float pos=SV::Along(x,y,v.a.orientation);const DWORD now=GetMessageTime();
    if(now!=sepDrag.lastTime){const float inst=(pos-sepDrag.last)*1000.f/std::max<DWORD>(1,now-sepDrag.lastTime);sepDrag.velocity=sepDrag.velocity*.4f+inst*.6f;}
    sepDrag.last=pos;sepDrag.lastTime=now;
    // Live, 1:1 and bounded: a page never goes under its minimum (WebView2 has no
    // overshoot to show, so the edge stops at 360 px instead of rubber-banding).
    // Past the threshold the drag is real: placeholders take over (a click or a double-click never flashes them).
    if(!resizePreview.active&&std::fabs(pos-sepDrag.grab)>=ResizeThreshold)StartResizePreview(v,sepDrag.slot);
    SetExtents(v,SV::Resize(v.a,sepDrag.extents,sepDrag.slot,pos-sepDrag.grab,v.min,false));ApplyPages();
    if(resizePreview.active)RenderResizePreview();
}
// Escape or a lost capture: the sizes from before the press come back.
void CancelSeparator(){
    if(!sepDrag.active)return;sepDrag.active=false;HoverAfterDrag();auto v=CurrentSplit();
    if(v.group&&sepDrag.extents.size()==v.a.slots.size())SetExtents(v,sepDrag.extents);
    // The frozen pages never moved: the preview simply goes, nothing reflows.
    const bool previewed=resizePreview.active;if(previewed)ClearResizePreview();
    if(GetCapture()==window)ReleaseCapture();ApplyPages();if(previewed)RenderSplitOverlay();
}
// The main window gets WM_MOUSELEAVE as soon as the pointer enters a grip (a child): that
// is not leaving the separator. The grip's own leave hides the handle. (Under --split-test
// the real pointer is elsewhere and only synthetic leaves count.)
bool PointerOnGrip(){if(splitTest)return true;POINT p;if(!GetCursorPos(&p))return false;HWND h=WindowFromPoint(p);return std::find(splitGrips.begin(),splitGrips.end(),h)!=splitGrips.end();}
// After a drag the handle stays only if the pointer is still over its target.
void HoverAfterDrag(){POINT p;if(splitTest||!GetCursorPos(&p)){chrome.split.hotHandle=-1;return;}ScreenToClient(window,&p);const float d=GetDpiForWindow(window)/96.f;auto v=CurrentSplit();chrome.split.hotHandle=v.group?SeparatorAt(p.x/d,p.y/d,v):-1;}
void SeparatorUp(){
    auto v=CurrentSplit();sepDrag.active=false;if(GetCapture()==window)ReleaseCapture();HoverAfterDrag();
    if(v.group&&sepDrag.slot+1<(int)v.a.slots.size()){auto e=SV::Extents(v.a);const int s=sepDrag.slot;const float total=e[s]+e[s+1];
        e[s]=SV::SnapFirst(e[s],total,sepDrag.velocity,v.min,v.a.visible);e[s+1]=total-e[s];SetExtents(v,e);}
    if(resizePreview.active)SettleResizePreview();else ApplyPages();
}
// ---- Separators: keyboard ("Redimensionner les panneaux")
void StartKeyboardResize(){
    auto v=CurrentSplit();if(!v.group||v.a.separators.empty())return;const int slot=v.a.SlotOf(v.active);splitResize=0;
    for(size_t i=0;i<v.a.separators.size();++i)if(v.a.separators[i]==slot||v.a.separators[i]==slot-1){splitResize=(int)i;break;}
    SetFocus(window);keyboardFocus=0;keyboardModality=true;SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);AnnounceSeparator();
}
bool SplitResizeKey(WPARAM key){
    auto v=CurrentSplit();if(!v.group||splitResize<0||splitResize>=(int)v.a.separators.size()){splitResize=-1;return false;}
    const bool row=v.a.orientation==SV::Orientation::Row;const float step=GetKeyState(VK_SHIFT)<0?64.f:16.f;const int s=v.a.separators[splitResize];
    float delta=0;
    if(key==(row?VK_LEFT:VK_UP))delta=-step;else if(key==(row?VK_RIGHT:VK_DOWN))delta=step;else if(key==VK_HOME)delta=-1e5f;else if(key==VK_END)delta=1e5f;
    else if(key==VK_RETURN){EqualizeSplit(v);AnnounceSeparator();return true;}
    else if(key==VK_TAB){const int n=(int)v.a.separators.size();splitResize=(splitResize+(GetKeyState(VK_SHIFT)<0?n-1:1))%n;SyncSplitDecor();InvalidateRect(window,nullptr,FALSE);AnnounceSeparator();return true;}
    else if(key==VK_ESCAPE){splitResize=-1;SyncSplitDecor();FocusPage();return true;}
    else return false;
    SetExtents(v,SV::Resize(v.a,SV::Extents(v.a),s,delta,v.min,false));ApplyPages();AnnounceSeparator();return true;
}
// ---- Group commands (tab menu)
void ReverseGroup(int g){
    auto m=Members(g);const int n=(int)m.size();if(n<2)return;std::vector<int> at;std::vector<float> w;for(auto& t:m){at.push_back(TabIndexOf(t));w.push_back(t->weight);}
    // Sizes stay where they are on screen; the sites swap places.
    auto active=Active();for(int i=0;i<n;++i){pages[at[i]]=m[n-1-i];m[n-1-i]->weight=w[i];}current=TabIndexOf(active);ApplyPages();Update();
}
void QuitGroup(int g){for(auto& t:Members(g))LeaveGroup(t);DissolveSmallGroups();ApplyPages();Update();AnnounceSplit(TRF(split_announce_closed,TabLabel(Active())));}
// "Placer à … de X" from the tab menu: same targets as the drop zones.
void PlaceFromMenu(const std::shared_ptr<Tab>& tab,SV::Side side){
    auto v=CurrentSplit();SV::Target t;
    if(v.group){if(tab->group==v.group)return;const int at=v.active+(side==SV::Side::Right||side==SV::Side::Bottom?1:0);t.kind=SV::Kind::Insert;t.index=at;t.orientation=v.a.orientation;ApplySplitTarget(tab,t,nullptr,v);return;}
    t.kind=SV::Kind::Split;t.side=side;t.orientation=SV::OrientationOf(side);ApplySplitTarget(tab,t,SplitPartner(tab),v);
}
// The blank new tab page, in the process language (lang and title only; nothing else is written in it).
std::wstring HomeHtml(){return std::wstring(L"<!doctype html><html lang=\"")+(I18n::Current()==I18n::Locale::En?L"en":L"fr")+L"\"><meta charset=\"utf-8\"><title>"+TR(tab_new)+LR"HTML(</title><style>html,body{margin:0;background:#171717;color:white;min-height:100%;color-scheme:dark}</style><body></body></html>)HTML";}
// What the address field opens (AddressBar::Resolve, unit-tested): http(s) and file: links as typed, a Windows path
// (C:\…, \\serveur\…, quoted or not) as its file:/// URL, a word with a dot as https://, anything else a search.
std::wstring Destination(std::wstring s){const auto r=AddressBar::Resolve(s);
    switch(r.kind){case AddressBar::Typed::Empty:return L"";case AddressBar::Typed::Sandbox:return NativeChrome::DebugSandbox::Url;case AddressBar::Typed::Url:return r.value;default:break;}
    return BrowserSettings::SearchUrl(settings.engine,AddressBar::PercentEncode(r.value),settings.customSearch);
}
// A launch target (link, path, words) as the tab to open: as typed in the address field, except the QA sandbox,
// which is never opened from outside.
std::wstring LaunchUrl(const std::wstring& target){const std::wstring d=Destination(target);return d==NativeChrome::DebugSandbox::Url?L"":d;}
void NewTab(std::wstring uri=L"",int index=-1,std::wstring title=TR(tab_new));
// SETTINGS(tabs): where a user-created tab ("+", Ctrl+T, menu) opens; -1 = at the end.
int UserTabIndex(){return !smoke&&settings.tabPosition==BrowserSettings::TabPosition::NextToActive&&current>=0&&current<(int)pages.size()?current+1:-1;}
void ReopenTab(){if(closedTabs.empty()||pages.size()>=128)return;auto tab=closedTabs.back();NewTab(tab.uri,tab.index,tab.title);if(!halted)closedTabs.pop_back();}
std::wstring HomeDocument(); // NEWTAB: HomeHtml, or the most visited sites (defined with the site icons)
// SETTINGS(newtab): what a new tab (Ctrl+T, "+", a fresh start) opens: "" is the home tab.
std::wstring NewTabTarget(){return settings.newTabPage==BrowserSettings::NewTabPage::Custom?settings.homepage:L"";}
void Navigate(std::shared_ptr<Tab> t,const std::wstring& uri){if(!t||!t->view.p||halted)return;t->navigateFrom=t->uri;t->navigateFromHome=t->home;t->navigateTo=uri; /* what the tab showed, in case this becomes a download */t->home=uri.empty();t->uri=uri;if(uri.empty()){t->blankIsHome=true;const auto html=HomeDocument();homeTiles.Set(*t,html!=HomeHtml());Check(t->view->NavigateToString(html.c_str()),L"NavigateToString");return;}
    // An address the engine refuses (E_INVALIDARG) is not the loss of the engine: the tab keeps what it showed (its home
    // page when it had nothing), so a bad link, bookmark or restored session never stops the browser.
    const HRESULT hr=t->view->Navigate(uri.c_str());if(hr==E_INVALIDARG){Log(L"NAVIGATE refused: invalid address");t->uri=t->navigateFrom;t->home=t->navigateFromHome;t->navigateTo.clear();if(t->uri.empty()||t->home)Navigate(t,L"");else if(t==Active())Update();return;}
    Check(hr,L"Navigate");}
// ---- Address suggestions (issue #20) -----------------------------------------
// The EDIT keeps focus: typing recomputes the list, the arrows move its active row
// (the field shows what Enter will open), Escape restores then closes, Enter opens.
void SyncAccessibility();void AnnounceSuggestion();void SuggestionsStructureChanged();void SaveHistory();void ForgetSiteIcon(const std::wstring& site);
// Profiles (src/profiles-integration.h, included later).
WindowSessions::Adoption AdoptThisWindow();extern bool profilesEnabled;void ProfileMenuOpen(bool keyboard);void PublishProfileWindow(bool cached=false);void SchedulePublishProfileWindow();std::wstring ProfileArgument();bool FocusContextToast();
void HideSuggestions(){if(!suggestions.Visible())return;suggestions.Hide();chrome.editIcon=nullptr;InvalidateChrome();SuggestionsStructureChanged();}
std::wstring AddressText(){int n=GetWindowTextLengthW(address);std::wstring s(n+1,0);GetWindowTextW(address,s.data(),n+1);s.resize(n);return s;}
void SetAddressText(const std::wstring& s,int from,int to){addressSetting=true;SetWindowTextW(address,s.c_str());SendMessageW(address,EM_SETSEL,from,to);addressSetting=false;}
void PlaceSuggestions(AddressBar::Suggestions data,int active){
    // SETTINGS(privacy): "Suggestions de recherche" off keeps the list closed (inline completion stays).
    if(!settings.searchSuggestions){HideSuggestions();return;}
    RECT r=ChromePx(chrome.bar);if(chrome.sidebar&&chrome.SideColumn().addressInColumn)r.right=S(chrome.bar.left+430);MapWindowPoints(window,nullptr,(POINT*)&r,2);
    const bool was=suggestions.Visible();
    suggestions.Show(std::move(data),active,BrowserSettings::Info(settings.engine).name,r.left,r.bottom,r.right,GetDpiForWindow(window));
    InvalidateChrome();if(suggestions.Visible()!=was)SuggestionsStructureChanged();
}
// EN_CHANGE from the user (never from SetAddressText): recompute, then inline-complete
// the top host with the completion selected, so the next keystroke replaces it.
void AddressEdited(){
    if(addressSetting||!editing||halted)return;
    const auto text=AddressText();DWORD from=0,to=0;SendMessageW(address,EM_GETSEL,(WPARAM)&from,(LPARAM)&to);
    const bool atEnd=from==to&&to==text.size();
    addressTyped=text;auto data=AddressBar::Suggest(text,history,atEnd&&!addressDeleting,&popularSites);addressDeleting=false;
    if(!data.completion.empty())SetAddressText(text+data.completion,(int)text.size(),-1);
    if(data.rows.empty()){HideSuggestions();return;}
    PlaceSuggestions(std::move(data),0);AnnounceSuggestion();
}
void MoveSuggestion(int delta){
    const auto& data=suggestions.Data();const int n=(int)data.rows.size();if(!n)return;
    const int next=(suggestions.Active()+delta+n)%n;suggestions.SetActive(next);
    const auto& row=data.rows[next];
    if(next==0&&!data.completion.empty())SetAddressText(addressTyped+data.completion,(int)addressTyped.size(),-1);
    else{const auto fill=row.kind==AddressBar::Kind::Site?row.text:addressTyped;SetAddressText(fill,(int)fill.size(),(int)fill.size());}
    InvalidateChrome();AnnounceSuggestion();
}
bool EscapeSuggestions(){
    if(!suggestions.Visible())return false;
    const auto& data=suggestions.Data();const auto shown=addressTyped+data.completion;
    if(suggestions.Active()!=0||AddressText()!=shown){suggestions.SetActive(0);SetAddressText(shown,(int)addressTyped.size(),-1);InvalidateChrome();AnnounceSuggestion();return true;}
    HideSuggestions();return true;
}
std::wstring SuggestionTarget(const AddressBar::Suggestion& row){
    switch(row.kind){
    case AddressBar::Kind::Site:return row.url;
    case AddressBar::Kind::Address:return Destination(row.text);
    default:return BrowserSettings::SearchUrl(settings.engine,AddressBar::PercentEncode(AddressBar::Trim(row.text)),settings.customSearch);
    }
}
void OpenSuggestion(int index){
    const auto rows=suggestions.Data().rows;if(index<0||index>=(int)rows.size()||halted)return;
    const auto target=SuggestionTarget(rows[index]);
    HideSuggestions();editing=false;ShowWindow(address,SW_HIDE);Navigate(Active(),target);FocusPage();
}
void RemoveSuggestion(int index){
    const auto rows=suggestions.Data().rows;if(index<0||index>=(int)rows.size()||rows[index].kind!=AddressBar::Kind::Site)return;
    const auto& row=rows[index];if(row.popular||!(row.host?history.RemoveHost(row.text):history.Remove(row.url)))return;
    SaveHistory();Log(L"HISTORY removed "+(row.host?row.text:row.url));   // SEC-05 (review): a page row's text has no scheme, so the release log's redaction would keep it whole
    if(!history.HasSite(row.site)&&!BookmarkedSite(row.site))ForgetSiteIcon(row.site);
    // Recompute from what was typed, without completing it again.
    SetAddressText(addressTyped,(int)addressTyped.size(),(int)addressTyped.size());
    auto data=AddressBar::Suggest(addressTyped,history,false,&popularSites);
    if(data.rows.empty()){HideSuggestions();return;}
    const int active=std::min(index,(int)data.rows.size()-1);PlaceSuggestions(std::move(data),active);AnnounceSuggestion();
}
void CreateSuggestions(){
    NativeChrome::AddressUI::Hooks hooks;
    hooks.open=[](int i){OpenSuggestion(i);};
    hooks.remove=[](int i){RemoveSuggestion(i);};
    hooks.icon=[](const std::wstring& site){return SiteIcon(site);};
    if(!suggestions.Create(instance,window,base,std::move(hooks)))Log(L"ADDRESS suggestions unavailable");
}
// Script: window.close() of a script-opened page (WindowCloseRequested): never the window's last-tab close, it closes
// only that tab (a hidden profile's page never closes the shared window). Placeholder: a window.open target that
// never became usable: removed without a recently-closed entry.
enum class CloseReason { User, Script, Placeholder };
bool KeepControllerForDownloads(const std::shared_ptr<Tab>& t);
void CloseAt(int index,CloseReason why=CloseReason::User){if(halted||index<0||index>=(int)pages.size())return;
    popups.CancelTab(pages[index]->serial); /* a pending window.open whose opener or target closes: settled first */
    if(index>=(int)pages.size())return;
    // SETTINGS(tabs): "Fermer la fenêtre avec le dernier onglet": the normal window close (session
    // save, clear on exit) with the tab still alive; one tab never asks "Fermer N onglets ?".
    if(why==CloseReason::User&&pages.size()==1&&settings.closeWithLastTab&&!smoke){Log(L"TABS last tab closed: closing the window");PostMessageW(window,WM_CLOSE,0,0);return;}
    CancelTabDrag();const bool active=index==current;int next=NativeChrome::SelectionAfterClose((int)pages.size(),current,index);
    {auto closing=pages[index];if(active&&closing->group){auto m=Members(closing->group);auto it=std::find(m.begin(),m.end(),closing);std::shared_ptr<Tab> heir=it!=m.end()&&it+1!=m.end()?*(it+1):it!=m.begin()&&it!=m.end()?*(it-1):nullptr;if(heir){const int j=TabIndexOf(heir);next=j>index?j-1:j;}}}
    if(!WorkspaceParked()){motion.ClearInteractions();hover=pressed=keyboardFocus=0;}auto t=pages[index];if(why!=CloseReason::Placeholder&&!t->home&&!t->uri.empty()){if(closedTabs.size()==25)closedTabs.erase(closedTabs.begin());closedTabs.push_back({t->uri,t->title,index});}ElementsTabClosed(t);FormsTabClosed(t);t->closed=true;t->favicon.Close();if(t->controller.p&&!KeepControllerForDownloads(t))t->controller->Close();pages.erase(pages.begin()+index);DissolveSmallGroups();RefreshAudio();
    if(pages.empty()){current=-1;NewTab(NewTabTarget());}else if(active)Select(next);else{current=next;motion.SelectTab((int)pages[current]->serial,NativeChrome::Motion::Now());ApplyPages();Update();}
}
enum ShortcutCommand { FocusChrome=3000, FindPage, PrintActive, FocusChromeBack, MoveTabLeft='{', MoveTabRight='}', SelectTabBase=3010 };
std::vector<int> ShownPanels(){std::vector<int> out;auto v=CurrentSplit();if(v.group)for(auto& s:v.a.slots)if(!s.rail)out.push_back(TabIndexOf(v.members[s.member]));return out;}
void Close(){CloseAt(current);}
void Command(int cmd){if(cmd==MotionLabCommand){if(motionLabMode)motionLab.Toggle();return;}if(halted||!chrome.Enabled(cmd))return;SideFocusCommand(cmd);
    if(tabDrag.tab){CancelTabDrag();if(GetCapture()==window)ReleaseCapture();UpdateInteraction(hover,0);}auto t=Active();if(cmd>=SelectTabBase&&cmd<SelectTabBase+9){Select(cmd==SelectTabBase+8?(int)pages.size()-1:cmd-SelectTabBase);return;}switch(cmd){case FocusChrome:ToggleChromeFocus();break;case FocusChromeBack:ToggleChromeFocus(true);break;case FindPage:OpenFindBar();break;case '{':MoveTab(current,current-1);break;case '}':MoveTab(current,current+1);break;case 'L':FocusAddress();break;case ']':if(!pages.empty())Select((current+1)%(int)pages.size());break;case '[':if(!pages.empty())Select((current+(int)pages.size()-1)%(int)pages.size());break;case 'T':NewTab(NewTabTarget(),UserTabIndex());FocusAddress();break;case 'W':Close();break;case 'U':ReopenTab();break;case 'R':if(t&&t->view.p&&t->loading.IsLoading()){Check(t->view->Stop(),L"Stop");break;}if(t&&t->view.p&&CheckTab(t->view->Reload(),L"Reload")){t->refreshing=true;auto now=NativeChrome::Motion::Now();chrome.lottie.RefreshBegin(now);SyncStopMorph(now);InvalidateChrome();ScheduleMotion(now);}break;case 'H':Navigate(t,L"");break;case 'B':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoBack(&yes);if(yes&&Check(t->view->GoBack(),L"GoBack")){auto now=NativeChrome::Motion::Now();chrome.nav.back.Kick(-1,now,motion.ReducedMotion(),TravelParams(true));InvalidateChrome();ScheduleMotion(now);}}break;case 'F':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoForward(&yes);if(yes&&Check(t->view->GoForward(),L"GoForward")){auto now=NativeChrome::Motion::Now();chrome.nav.forward.Kick(1,now,motion.ReducedMotion(),TravelParams(false));InvalidateChrome();ScheduleMotion(now);}}break;default:ExtraCommand(cmd);break;}}
bool BookmarkPopoverFocus();bool SmokeBookmarks();bool FolderRenameFocus();
bool EscapeOwnedBySurface(HWND focus); // below, after the surfaces: a drawer or a toolbar popover with the keyboard closes on its own Échap
bool EscapeLeavesAnnotation(bool modeActive,HWND focus); // below: Key()'s decision for Échap in the annotation mode (tests/shortcuts_test.cpp)
int Key(UINT key){
    const bool ctrl=GetKeyState(VK_CONTROL)<0,alt=GetKeyState(VK_MENU)<0,shift=GetKeyState(VK_SHIFT)<0;
    // The bookmark popover's fields keep every key (Ctrl+D, Ctrl+Z… belong to the text there).
    if(BookmarkPopoverFocus())return 0;
    if(FolderRenameFocus())return 0; // the in-place folder name keeps every key (text undo, Ctrl+W, F5…)
    if(AnnotationSurfaceFocus())return 0; // the note popover, onboarding, launcher and band keep their keys
    // Modal, prompt and find bar fields keep their keys (Ctrl+W must not close the tab under a dialog).
    {int cmd=0;if(ElementSurfaceKey(key,ctrl,alt,shift,cmd))return cmd;}
    if(motionLabMode&&ctrl&&alt&&shift&&key=='M')return MotionLabCommand;
    if(ctrl&&!alt){
        if(!shift&&key=='D')return 'K';
        if(shift&&key=='B')return BookmarkBarCommand;
        if(shift&&key==VK_OEM_COMMA&&chrome.sidebar)return SidebarToggleCommand; // Ctrl+Maj+, : plier / déplier le panneau (#127), panel layout only
        if(shift&&key=='A')return AnnotateCommand; // Ctrl+Maj+A: mode Annotation (toggle)
        if(shift&&key=='D'&&AnnotationHistoryAvailable())return ChatCommand; // Ctrl+Maj+D: « Demander à l'agent » (chat intégré; starts the mode if needed)
        if(shift&&key=='O')return BookmarksDrawerCommand;
        // Ctrl+Z undoes a bookmark change only from the chrome, the toast or the Favoris drawer, never in a text field or the page.
        if(!shift&&key=='Z'){HWND f=GetFocus();return f&&(f==window||f==bookmarkToast.Window()||f==bookmarksDrawer.Window())?BookmarkUndoCommand:0;}
        if(shift&&key=='T')return 'U';
        if(shift&&key=='N')return 'P'; // Ctrl+Maj+N: nouvelle fenêtre privée
        if(shift&&key=='M'&&(profilesEnabled||privateWindow))return 900; // Ctrl+Maj+M: menu des profils (Paper p-I-0) / fenêtre privée
        if(!shift&&key>='1'&&key<='9')return SelectTabBase+key-'1';
        if(shift&&key==VK_PRIOR)return MoveTabLeft;
        if(shift&&key==VK_NEXT)return MoveTabRight;
        if(key==VK_TAB)return shift?'[':']';
        // In the page WebView2 handles Ctrl+F itself, retaining its native find state.
        // Ctrl+F opens our find bar (src/find-bar.h) everywhere, the page included (the engine's is suppressed).
        if(!shift&&key=='F')return FindPage;
        if(shift&&key=='F')return FocusModeCommand; // Ctrl+Maj+F: Mode Focus (toggle; Réglages › Affichage)
        if(shift&&key==VK_F12)return SandboxCommandId;
        if(!shift&&key==VK_OEM_2)return ShortcutsCommand; // Ctrl+/ : raccourcis clavier (C5)
        // Ctrl+P likewise: the page opens the same browser print preview itself (no double dialog).
        if(!shift&&key=='P')return GetFocus()==window||GetFocus()==address||(Active()&&Active()->home)?PrintActive:0; // a home tab (frequent sites shown) never prints
        if(!shift&&(key=='L'||key=='T'||key=='W'||key=='R'))return key;
        if(shift&&key=='H'&&AnnotationHistoryAvailable())return AnnotateHistoryCommand; // Ctrl+Maj+H: historique des annotations
        if(!shift&&key=='H')return 'Y'; // history, as in every desktop browser
    }
    if(!ctrl&&!alt&&!shift&&key==VK_F6)return FocusChrome;
    if(!ctrl&&!alt&&shift&&key==VK_F6)return FocusChromeBack;
    if(!ctrl&&!alt&&key==VK_F5)return 'R';
    if(!ctrl&&!alt&&!shift&&key==VK_F11)return FullscreenCommand;
    // Échap quitte le mode Annotation, sauf quand une surface du dessus a le clavier (un tiroir, une fenêtre de la barre) :
    // la couche du dessus d'abord, elle se ferme avec son propre Échap ; le suivant, depuis la fenêtre, quitte le mode.
    if(!ctrl&&!alt&&!shift&&key==VK_ESCAPE&&EscapeLeavesAnnotation(AnnotationModeActive(),GetFocus()))return AnnotateExitCommand;
    if(!ctrl&&!alt&&!shift&&key==VK_ESCAPE&&PageFullscreenActive())return ExitFullscreenCommand; // C6: Échap quitte le plein écran de la page
    if(!ctrl&&!alt&&key==VK_F3)return shift?FindPreviousCommand:FindNextCommand;
    if(alt&&!ctrl&&!shift){if(key=='D')return 'L';if(key==VK_LEFT)return 'B';if(key==VK_RIGHT)return 'F';}
    return 0;
}

// Store UTF-8 URLs alongside the profile, preserving blank tabs and selection.
// Portable builds keep <exe dir>\session; installed builds use the stable data
// directory resolved by the update core (outside every version directory).
// Split groups as tab indices in panel order (session format 2).
std::vector<BrowserSession::Group> SessionGroups(){
    std::vector<BrowserSession::Group> out;
    for(auto& [g,o]:splitGroups){auto m=Members(g);if(m.size()<2)continue;BrowserSession::Group group;group.orientation=o==SV::Orientation::Column?1:0;
        for(auto& t:m){group.members.push_back(TabIndexOf(t));group.weights.push_back(t->weight);}out.push_back(std::move(group));}
    return out;
}
void ApplySessionGroups(const std::vector<BrowserSession::Group>& groups){
    for(const auto& group:groups){bool ok=group.members.size()>=2;for(int m:group.members)ok=ok&&m>=0&&m<(int)pages.size()&&!pages[m]->group;if(!ok)continue;
        const int g=nextSplitGroup++;splitGroups[g]=group.orientation?SV::Orientation::Column:SV::Orientation::Row;
        for(size_t i=0;i<group.members.size();++i){auto& t=pages[group.members[i]];t->group=g;t->weight=i<group.weights.size()?group.weights[i]:1;t->usedAt=++useClock;}}
    DissolveSmallGroups();
}
// Decision 2: one session file per normal window (src/window-sessions.h). `windowId` is this
// window's; `windowRequested` the --restore-window id it was started for (handover or relaunch).
std::wstring windowId,windowRequested;HANDLE windowLive=nullptr;bool windowForced=false;std::vector<HANDLE> windowReservations;
std::string SessionText(const std::vector<std::wstring>& urls){BrowserSession::State s;s.active=0;for(auto& u:urls)s.urls.push_back(Utf8(u));std::ostringstream o;BrowserSession::Write(o,s);return o.str();}
bool LaunchWindow(const std::wstring& id){
    if(smoke)return false;
    std::wstring exe(32768,L'\0');const DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);if(exe.empty())return false;
    std::wstring cmd=L"\""+exe+L"\" --restore-window "+id+ProfileArgument();   /* i18n-exempt: command line */const std::wstring dir=std::filesystem::path(exe).parent_path().wstring();
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,dir.c_str(),&si,&pi)){Log(L"WINDOW launch failed "+id);return false;}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Log(L"WINDOW launched "+id);return true;
}
bool claimingInWindow=false; // SwitchWorkspace: the profile starts inside this window
void ClaimWindowSession(){
    if(smoke||privateWindow)return;
    auto a=AdoptThisWindow(); // set-aware (Profiles::AdoptWorkspace): a profile cached in another window set is never adopted alone
    windowId=a.id;windowLive=a.live;windowForced=a.requested;windowReservations=a.reservations; /* held for this process life */sessionFile=WindowSessions::File(dataDir,windowId);
    Log(L"WINDOW session "+windowId+(a.requested?L" (handed over)":a.adopted?L" (adopted)":L" (new)"));
    // A profile started INSIDE this window (same-window switch) adopts one session and launches no window: its
    // other orphans stay orphans (their reservations given back) for the next cold start or « Rouvrir » (L4).
    if(claimingInWindow){for(HANDLE r:a.reservations)if(r)CloseHandle(r);windowReservations.clear();if(!a.relaunch.empty())Log(L"WINDOW in-window start: "+std::to_wstring(a.relaunch.size())+L" other session(s) left for the next start");return;}
    for(const auto& id:a.relaunch)LaunchWindow(id); // a failed relaunch stays an orphan: restored next time
}
void ShowWindowLaunchFailed(const std::wstring& id);
// A7 / partial restore « Nouvelle fenêtre »: the URLs are written as the new window's session BEFORE
// the launch; the new process adopts that file (its durable acknowledgement is the claim of the
// window mutex). A failure keeps the file (orphan → restored at the next start) and offers Réessayer;
// a retry never starts a second window for a session already open.
void OpenWindowWith(const std::vector<std::wstring>& urls){
    if(privateWindow){Log(L"WINDOW handover from a private window not offered");return;}
    const std::wstring id=WindowSessions::NewId();
    if(!WindowSessions::WriteHandover(dataDir,id,SessionText(urls))){ShowWindowLaunchFailed(L"");return;}
    if(!LaunchWindow(id))ShowWindowLaunchFailed(id);
}
void RetryWindowLaunch(const std::wstring& id){if(id.empty()||WindowSessions::Live(dataDir,id))return;if(!LaunchWindow(id))ShowWindowLaunchFailed(id);}
void SaveSession(){
    if(smoke||privateWindow||!sessionReady||pages.empty()||halted)return; // a stopped engine's tabs are not a session to replay
    BrowserSession::State state;state.active=current;
    for(auto& tab:pages)state.urls.push_back(tab->home?"":Utf8(tab->uri));
    state.groups=SessionGroups();
    const auto path=std::filesystem::path(sessionFile),temp=std::filesystem::path(sessionFile+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);BrowserSession::Write(out,state);out.close();if(!out){Log(L"SESSION save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"SESSION replace failed");
    PublishProfileWindow(); // the other windows' profile menus read this window's tabs
}
// Visited sites for the address suggestions, beside `session` (UTF-8, atomic replace).
std::wstring HistoryFile(){return dataDir+L"\\history";}
// Interprocess transaction (src/profile-store.h): visits made here are added to the file's counts,
// removals here are applied there, and another window's removals are never undone.
AddressBar::History historyBase;
ProfileStore::Stamp historySeen; // PERF-01: the file as it was when `history` last equalled it (nothing left to merge)
void SaveHistory(){
    if(smoke||privateWindow)return;
    // Nothing changed here since the file was last read and the file was not replaced since: the
    // transaction would read, merge and find nothing to do (e.g. every window activation).
    if(history==historyBase&&ProfileStore::StampOf(HistoryFile())==historySeen)return;
    ProfileStore::Lock lock(dataDir); /* held through the stamp below (the commit re-enters it: same thread) */
    const auto r=ProfileStore::CommitWith<AddressBar::History>(dataDir,HistoryFile(),historyBase,history,
        [](std::istream& in,bool* damaged){std::string b((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());std::istringstream i2(b);auto h=AddressBar::History::Read(i2);if(damaged)*damaged=!b.empty()&&ProfileStore::RecordLines(b)!=h.Items().size();return h;},[](std::ostream& out,const AddressBar::History& h){h.Write(out);},
        [](const AddressBar::History& b,const AddressBar::History& m,const AddressBar::History& t){return AddressBar::History::Merge(b,m,t);},lock.Held()?5000:0,{"browser-history 1"},false); /* damaged: backed up, never rewritten */
    historySeen=r==ProfileStore::Result::Unchanged?ProfileStore::StampOf(HistoryFile()):ProfileStore::Stamp{};
    if(r==ProfileStore::Result::Failed||r==ProfileStore::Result::Busy)Log(L"HISTORY save failed");
}
// Result pages of the search engines are not suggested back: typing "git" should
// offer the sites visited, not every past search for it.
bool SearchResultPage(const std::wstring& uri){for(int i=0;i<BrowserSettings::EngineCount;++i){const std::wstring prefix=BrowserSettings::Info(static_cast<BrowserSettings::Engine>(i)).search;if(!prefix.empty()&&uri.rfind(prefix,0)==0)return true;}
    // The custom engine: its template up to %s (empty or unusable: nothing matches).
    const std::wstring custom=BrowserSettings::SearchTemplate(settings.customSearch);const size_t at=custom.find(L"%s");return at!=std::wstring::npos&&at>0&&uri.rfind(custom.substr(0,at),0)==0;}
// Remembered site icons (issue #20): the favicon a tab received, kept per site in
// <data>\favicons\<site>.png and shown by the suggestions. Never fetched while typing.
// An import of bookmarks also leaves its sites' icons there (src/import-icons.h).
std::map<std::wstring,std::shared_ptr<const NativeChrome::FaviconImage>> siteIcons;std::set<std::wstring> siteIconsMissing;std::map<std::wstring,size_t> siteIconsWritten;
std::wstring SiteIconFile(const std::wstring& site){return dataDir+L"\\favicons\\"+AddressBar::SiteFileName(site)+L".png";}
std::shared_ptr<const NativeChrome::FaviconImage> DecodeIconFile(const std::wstring& path){
    std::shared_ptr<const NativeChrome::FaviconImage> image;IStream* stream=nullptr;
    if(SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&stream))&&stream){image=chrome.DecodeFavicon(stream);stream->Release();}
    return image;
}
// Icons shipped for the popular sites (assets\sites\<site>.png|.ico): read-only,
// cached with misses, and only a fallback behind the icon remembered from a visit.
std::map<std::wstring,std::shared_ptr<const NativeChrome::FaviconImage>> bundledIcons;
std::shared_ptr<const NativeChrome::FaviconImage> BundledIcon(const std::wstring& site){
    auto found=bundledIcons.find(site);if(found!=bundledIcons.end())return found->second;
    const std::wstring name=AddressBar::SiteFileName(site);
    std::shared_ptr<const NativeChrome::FaviconImage> image;
    for(const wchar_t* ext:{L".png",L".ico"})if(!image)image=DecodeIconFile(base+L"\\assets\\sites\\"+name+ext);
    return bundledIcons[site]=image;
}
// A shipped icon exists for the site (files only: no decoding, usable before the window's graphics exist).
bool BundledIconFile(const std::wstring& site){const std::wstring name=AddressBar::SiteFileName(site);for(const wchar_t* ext:{L".png",L".ico"}){const DWORD a=GetFileAttributesW((base+L"\\assets\\sites\\"+name+ext).c_str());if(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY))return true;}return false;}
std::shared_ptr<const NativeChrome::FaviconImage> SiteIcon(const std::wstring& site){
    if(site.empty())return {};auto found=siteIcons.find(site);if(found!=siteIcons.end())return found->second;
    if(!smoke&&!siteIconsMissing.count(site)){
        if(auto image=DecodeIconFile(SiteIconFile(site))){siteIcons[site]=image;return image;}
        siteIconsMissing.insert(site);
    }
    return BundledIcon(site);
}
void RememberSiteIcon(const std::wstring& uri,const std::shared_ptr<const NativeChrome::FaviconImage>& image,IStream* stream){
    const auto site=AddressBar::SiteOf(uri);if(site.empty()||!image)return;
    siteIcons[site]=image;siteIconsMissing.erase(site);if(smoke||privateWindow||!stream)return;
    STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart==0||stat.cbSize.QuadPart>256*1024)return;
    std::string bytes((size_t)stat.cbSize.QuadPart,'\0');LARGE_INTEGER zero{};ULONG read=0;
    if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr))||FAILED(stream->Read(bytes.data(),(ULONG)bytes.size(),&read))||read!=bytes.size())return;
    const size_t hash=std::hash<std::string>{}(bytes);auto& written=siteIconsWritten[site];if(written==hash)return;written=hash;
    std::error_code ignored;std::filesystem::create_directories(std::filesystem::path(dataDir)/L"favicons",ignored);
    const auto path=std::filesystem::path(SiteIconFile(site)),temp=std::filesystem::path(SiteIconFile(site)+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(bytes.data(),(std::streamsize)bytes.size());out.close();if(!out){Log(L"FAVICONS save failed "+site);return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))Log(L"FAVICONS replace failed "+site);
}
// A site's icon lives as long as the site has a page in the history.
void ForgetSiteIcon(const std::wstring& site){if(site.empty())return;siteIcons.erase(site);siteIconsWritten.erase(site);siteIconsMissing.insert(site);if(smoke||privateWindow)return; /* review P2 */if(!DeleteFileW(SiteIconFile(site).c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND)Log(L"FAVICONS delete failed "+site);}
void ClearSiteIcons(){siteIcons.clear();siteIconsMissing.clear();siteIconsWritten.clear();if(smoke||privateWindow)return; /* review P2 */std::error_code ignored;std::filesystem::remove_all(std::filesystem::path(dataDir)/L"favicons",ignored);}
void LoadPopularSites(){std::ifstream in(std::filesystem::path(base+L"\\assets\\popular-sites.txt"),std::ios::binary);popularSites=AddressBar::ReadPopular(in);if(popularSites.empty())Log(L"ADDRESS popular sites unavailable");}
void LoadHistory(){LoadPopularSites();if(smoke||privateWindow)return; /* point 8: a private window shows no history, normal or private */std::string bytes;{ProfileStore::Lock lock(dataDir);ProfileStore::ReadAll(HistoryFile(),bytes);}std::istringstream in(bytes);history=AddressBar::History::Read(in);historyBase=history;historySeen={};}
// ---- New tab page (SETTINGS newtab) ----------------------------------------------
// "Sites fréquents": the home tab shows the most visited hosts (AddressBar::TopSites, the
// ranking of the address bar's host rows) as plain http(s) links. Self-contained document
// (NavigateToString, so it stays about:blank = a home tab): icons and font are data: URIs,
// the CSP allows nothing else (no script, no network). Nothing visited yet: HomeHtml.
std::wstring HtmlEscape(const std::wstring& s){std::wstring r;r.reserve(s.size());for(wchar_t c:s){switch(c){case L'&':r+=L"&amp;";break;case L'<':r+=L"&lt;";break;case L'>':r+=L"&gt;";break;case L'"':r+=L"&quot;";break;case L'\'':r+=L"&#39;";break;default:r+=c<0x20?L' ':c;}}return r;}
// data:<mime>;base64,… of a local file (empty when missing, empty or above `cap` bytes).
std::wstring DataUri(const std::wstring& path,const wchar_t* mime,size_t cap){
    std::ifstream in(std::filesystem::path(path),std::ios::binary|std::ios::ate);if(!in)return L"";const auto size=(std::streamoff)in.tellg();if(size<=0||(size_t)size>cap)return L"";
    std::string bytes((size_t)size,'\0');in.seekg(0);if(!in.read(bytes.data(),size))return L"";
    static const char* digits="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::wstring r=std::wstring(L"data:")+mime+L";base64,";r.reserve(r.size()+(bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){const unsigned n=((unsigned char)bytes[i]<<16)|(i+1<bytes.size()?(unsigned char)bytes[i+1]<<8:0)|(i+2<bytes.size()?(unsigned char)bytes[i+2]:0);
        r+=(wchar_t)digits[(n>>18)&63];r+=(wchar_t)digits[(n>>12)&63];r+=i+1<bytes.size()?(wchar_t)digits[(n>>6)&63]:L'=';r+=i+2<bytes.size()?(wchar_t)digits[n&63]:L'=';}
    return r;
}
// The site's icon as the suggestions show it: remembered from a visit, else the bundled one.
std::wstring SiteIconUri(const std::wstring& site){
    if(auto uri=DataUri(SiteIconFile(site),L"image/png",48*1024);!uri.empty())return uri;
    const auto name=std::filesystem::path(SiteIconFile(site)).stem().wstring(); // same file-name rule as BundledIcon
    if(auto uri=DataUri(base+L"\\assets\\sites\\"+name+L".png",L"image/png",48*1024);!uri.empty())return uri;
    return DataUri(base+L"\\assets\\sites\\"+name+L".ico",L"image/x-icon",48*1024);
}
std::wstring HomeDocument(){
    homeTiles.serials.erase(std::remove_if(homeTiles.serials.begin(),homeTiles.serials.end(),[](unsigned s){return std::none_of(pages.begin(),pages.end(),[s](const std::shared_ptr<Tab>& t){return t->serial==s;});}),homeTiles.serials.end());
    if(settings.newTabPage!=BrowserSettings::NewTabPage::FrequentSites)return HomeHtml();
    std::wstring tiles;
    for(const auto& h:AddressBar::TopSites(history,8)){
        const auto site=AddressBar::SiteOf(h.url);if(site.empty()||h.url.size()>2048)continue; // http(s) only
        std::wstring name=h.host;for(const auto& p:popularSites)if(p.host==h.host&&!p.name.empty()){name=p.name;break;}
        const auto icon=SiteIconUri(site);std::wstring glyph;
        if(icon.empty()){wchar_t first=name.empty()?L'?':(wchar_t)towupper(name[0]);glyph=L"<span class=\"l\" aria-hidden=\"true\">"+HtmlEscape(std::wstring(1,first>=0xD800&&first<=0xDFFF?L'?':first))+L"</span>";}
        else glyph=L"<img src=\""+icon+L"\" alt=\"\" width=\"32\" height=\"32\">";
        tiles+=L"<li><a href=\""+HtmlEscape(h.url)+L"\" title=\""+HtmlEscape(AddressBar::MatchText(h.url))+L"\"><span class=\"i\">"+glyph+L"</span><span class=\"n\">"+HtmlEscape(name)+L"</span></a></li>";
    }
    if(tiles.empty())return HomeHtml();
    static const std::wstring font=DataUri(base+L"\\assets\\fonts\\InterDisplay-Medium.ttf",L"font/ttf",1024*1024);
    const bool reduced=motion.ReducedMotion();
    return std::wstring(L"<!doctype html><html lang=\"")+(I18n::Current()==I18n::Locale::En?L"en":L"fr")+LR"HTML("><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; font-src data:; style-src 'unsafe-inline'"><title>)HTML"+TR(tab_new)+LR"HTML(</title><style>)HTML"
        +(font.empty()?L"":L"@font-face{font-family:\"Inter Display\";src:url("+font+L") format(\"truetype\");font-weight:500;font-display:block}")
        +LR"CSS(html,body{margin:0;background:#171717;color:white;min-height:100%;color-scheme:dark}
body{display:flex;align-items:center;justify-content:center;min-height:100vh;font-family:"Inter Display",Inter,system-ui,sans-serif;font-feature-settings:"cv01","cv02","cv03","cv04","cv13";font-weight:500}
ul{list-style:none;margin:0;padding:24px 16px;display:grid;grid-template-columns:repeat(4,120px);gap:16px 8px}
@media (max-width:519px){ul{grid-template-columns:repeat(2,120px)}}
a{display:flex;flex-direction:column;align-items:center;gap:8px;width:120px;padding:8px 0;box-sizing:border-box;border-radius:12px;color:rgba(255,255,255,.7);text-decoration:none;outline:none}
a:hover{color:rgba(255,255,255,.9)}
a:focus-visible{outline:2px solid rgba(255,255,255,.5);outline-offset:2px}
.i{display:flex;align-items:center;justify-content:center;width:56px;height:56px;border-radius:12px;background:rgba(255,255,255,.04)}
a:hover .i{background:rgba(255,255,255,.08)}
a:active .i{background:rgba(255,255,255,.06)}
.i img{width:32px;height:32px}
.l{width:32px;height:32px;border-radius:50%;background:rgba(255,255,255,.12);color:rgba(255,255,255,.9);font-size:16px;line-height:32px;text-align:center}
.n{max-width:112px;font-size:14px;line-height:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
)CSS"+(reduced?L"":L".i{transition:background-color 120ms ease-out}@media (prefers-reduced-motion:reduce){.i{transition:none}}")
        +L"</style><body><nav aria-label=\""+TR(newtab_frequent)+L"\"><ul>"+tiles+L"</ul></nav></body></html>";
}
void RestoreSession(){KeepContentTransition keep; // a cold profile's tabs come back under its switch transition
    // The link or file this launch was given opens once, in a new tab after the restored ones (consumed here: a later
    // cold profile switch never reopens it).
    const std::wstring opening=launchTarget.empty()?L"":LaunchUrl(launchTarget);launchTarget.clear();if(!opening.empty())Log(L"OPEN launch target in a new tab");
    if(privateWindow){NewTab(opening.empty()?NewTabTarget():opening);return;} // a private window starts empty and restores nothing
    BrowserSession::State state;
    // "Restaurer les onglets" off: start on a fresh tab (the session is still saved on exit).
    // A handed-over or relaunched window always opens its session (it is why it was started).
    if(!smoke&&(settings.restoreTabs||windowForced)){std::string bytes;ProfileStore::ReadAll(sessionFile,bytes);std::istringstream in(bytes);state=BrowserSession::Read(in);}
    for(const auto& url:state.urls){
        int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),nullptr,0);
        if(!url.empty()&&!size){state=BrowserSession::State{};break;}
    }
    std::vector<std::wstring> overflow; // past 128 tabs: offered in a new window, never dropped silently
    for(const auto& url:state.urls){int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),nullptr,0);std::wstring uri(size,0);if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),uri.data(),size);if(!AddressBar::Navigable(uri))uri.clear(); /* an address the engine refuses comes back as a new tab */if(pages.size()>=128){overflow.push_back(uri);continue;}NewTab(uri);}
    if(!overflow.empty())Defer([overflow]{ShowPartialRestoreToast(overflow);});
    if(pages.empty())NewTab(opening.empty()?NewTabTarget():opening);else{if(pages.size()==state.urls.size())ApplySessionGroups(state.groups);Select(state.active);if(!opening.empty()){NewTab(opening);Select((int)pages.size()-1);}}
    // Restored (or first) tabs are the initial state: no entrance cascade, no selection fade.
    BuildChrome();chrome.SettleTabs();motion.selection.Settle();BuildChrome();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());
    sessionReady=true;if(splitTest)SetTimer(window,SplitTestTimer,1500,nullptr);
}

// ---- Downloads -------------------------------------------------------------
// WebView2's own flyout is suppressed (Handled); operations feed the native
// "Download history" popover. Finished entries persist in `downloads` next to
// `session`; in-progress operations belong to the engine and are not restored.
namespace DL=NativeChrome::Downloads;
std::vector<DL::Item> downloads; int nextDownloadId=1;
// `owner`: the workspace (profile in this window) whose `downloads` hold the row. The map is process-wide (ids are
// unique across workspaces); every prune / disposal touches only the operations of ONE owner, never another
// profile's live downloads (a parked profile's download stays cancellable after another profile clears its data).
DL::Item* FindDownload(int id);
// tab: the serial of the tab that started it. view: that tab's WebView once the tab closed while the download runs
// (a tab opened only for its download closes at once): the engine stops raising the operation's events when its
// WebView closes, so the closed tab's hidden controller lives until its last download ends (released with the entry).
// fullAt: when every byte was in (GetTickCount64, 0 = not yet): past DL::Engine::HoldGraceMs still in progress, the
// engine holds the file for the user's decision. keeping/keepLink: « Conserver » in progress (KeepHeldDownload).
struct DownloadOperation { Com<ICoreWebView2DownloadOperation> op; bool userPaused=false; Workspace* owner=nullptr; unsigned tab=0; std::shared_ptr<Com<ICoreWebView2Controller>> view; ULONGLONG fullAt=0; bool keeping=false; std::wstring keepLink; };
static_assert(DL::Engine::InProgress==COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS&&DL::Engine::Interrupted==COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED&&DL::Engine::Completed==COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED,"engine download states");
static_assert(DL::Engine::ReasonUserCanceled==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED&&DL::Engine::ReasonUserPaused==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_PAUSED,"engine interrupt reasons");
std::map<int,std::shared_ptr<DownloadOperation>> downloadOps;
// Drops the operations of `owner` (the workspace in the globals by default) whose row is gone from its downloads.
void PruneDownloadOps(Workspace* owner=WorkspaceNow()){for(auto it=downloadOps.begin();it!=downloadOps.end();)it=it->second->owner==owner&&!FindDownload(it->first)?downloadOps.erase(it):std::next(it);}
// Drops every operation of a workspace leaving this window (its engine is closed with it).
void DropDownloadOps(Workspace* owner){for(auto it=downloadOps.begin();it!=downloadOps.end();)it=it->second->owner==owner?downloadOps.erase(it):std::next(it);}
bool downloadDismissClick=false;
DL::Item* FindDownload(int id){for(auto& item:downloads)if(item.id==id)return &item;return nullptr;}
// Interprocess transaction (src/profile-store.h + DL::MergeHistory): another window's finished
// downloads and removals are kept; this window's live rows are untouched.
std::vector<DL::Item> downloadsBase;
void SaveDownloads(){
    if(smoke||privateWindow)return;
    ProfileStore::Lock lock(dataDir);if(!lock.Held()){Log(L"DOWNLOADS profile busy");return;}
    std::string bytes;if(!ProfileStore::ReadAll(downloadsFile,bytes)){Log(L"DOWNLOADS save failed (read)");return;}
    {const auto head=ProfileStore::FirstLine(bytes);if(!bytes.empty()&&head!="browser-downloads 2"&&head!="browser-downloads 1"){Log(L"DOWNLOADS unknown file format: not overwritten");return;}} // decision S3
    int ignored=0;std::istringstream in(bytes);const auto theirs=DL::ReadHistory(in,ignored);
    if(!bytes.empty()&&ProfileStore::RecordLines(bytes)!=theirs.size()){Log(L"DOWNLOADS file damaged: not rewritten");return;} // S3: never rewrite from a partial read
    auto merged=DL::MergeHistory(downloadsBase,downloads,theirs,nextDownloadId);
    std::ostringstream out;DL::WriteHistory(out,merged);
    if(out.str()!=bytes&&!ProfileStore::WriteAtomic(downloadsFile,out.str())){Log(L"DOWNLOADS save failed");return;}
    const bool others=merged.size()!=downloads.size();
    downloads=std::move(merged);downloadsBase.clear();for(const auto& d:downloads)if(DL::Persistent(d))downloadsBase.push_back(d);
    if(others)Log(L"DOWNLOADS merged with another window's list");
}
// History written before start times existed: date each row from its file's
// creation time (when the download began writing it), else from the history
// file itself, then save so the date sticks.
std::int64_t FileTimeUnixMs(const FILETIME& t){return NativeChrome::DownloadsUI::FileTimeMs(t)-11644473600000LL;}
void LoadDownloads(){
    if(smoke||privateWindow)return; // review P3: a private window lists only its own downloads
    const auto path=std::filesystem::path(downloadsFile);
    {std::string bytes;{ProfileStore::Lock lock(dataDir);ProfileStore::ReadAll(downloadsFile,bytes);}std::istringstream in(bytes);downloads=DL::ReadHistory(in,nextDownloadId);downloadsBase=downloads;}
    WIN32_FILE_ATTRIBUTE_DATA history{};const bool known=GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&history)!=0;
    bool migrated=false;
    for(auto& item:downloads){
        if(item.started>0)continue;
        WIN32_FILE_ATTRIBUTE_DATA file{};
        if(GetFileAttributesExW(item.path.c_str(),GetFileExInfoStandard,&file))item.started=FileTimeUnixMs(file.ftCreationTime);
        else if(known)item.started=FileTimeUnixMs(history.ftLastWriteTime);
        migrated=migrated||item.started>0;
    }
    if(migrated){Log(L"DOWNLOADS history dated from file times");SaveDownloads();}
}
// Stored as a stable code (« @download_error_offline »), rendered in the interface language by DL::FailureText:
// a row saved in one language reads in the other after a switch.
std::wstring InterruptText(COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON r){
    switch(r){
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:return DL::ReasonCode(I18n::Id::download_error_disk_full);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:return DL::ReasonCode(I18n::Id::download_error_access_denied);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:return DL::ReasonCode(I18n::Id::download_error_name_too_long);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:return DL::ReasonCode(I18n::Id::download_error_too_large);
    // E6: three distinct French reasons (the row shows them alone, see DL::FailureText).
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS:return DL::ReasonCode(I18n::Id::download_blocked_dangerous);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:return DL::ReasonCode(I18n::Id::download_blocked_check_failed);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED_BY_POLICY:return DL::ReasonCode(I18n::Id::download_blocked_policy);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:return DL::ReasonCode(I18n::Id::download_error_offline);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_INVALID_REQUEST:return DL::ReasonCode(I18n::Id::download_error_network);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_DOWNLOAD_PROCESS_CRASHED:return DL::ReasonCode(I18n::Id::download_error_interrupted);
    default:break;
    }
    if(r>=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED&&r<=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_CROSS_ORIGIN_REDIRECT)return DL::ReasonCode(I18n::Id::download_error_server);
    return DL::ReasonCode(I18n::Id::download_error_file);
}
void ShowDownloadDrawer();void SyncLatch();
void DeferAfter(UINT ms,std::function<void()> job);void ShowDownloads();void FinishKeep(int id,int attempt);
// Toolbar icon: a run lasts while any download is active; its progress counts
// every download of the run (completed ones included) so it never goes back.
std::set<int> downloadIconRun;
void InvalidateDownloadIcon(){if(chrome.floating&&!chrome.PanelShown())return;RECT r=ChromePx(chrome.DownloadIconDirty());InvalidateRect(window,&r,FALSE);}
bool labDownloadRunning=false; // motion lab download replay (a real download cancels it)
void UpdateDownloadIcon(bool completedNow){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    if(labDownloadRunning){labDownloadRunning=false;KillTimer(window,MotionLabTimerId);}
    bool active=false;
    // A held download (every byte in, waiting for « Conserver » / « Supprimer ») ends the run: never a 100 % bar that spins on.
    for(auto& item:downloads)if((item.state==DL::State::InProgress&&!item.held)||item.state==DL::State::Paused){active=true;downloadIconRun.insert(item.id);}
    long double received=0,total=0;
    for(auto& item:downloads)if(downloadIconRun.count(item.id)&&item.total>0&&(item.state==DL::State::InProgress||item.state==DL::State::Paused||item.state==DL::State::Completed)){total+=item.total;received+=std::min(item.received,item.total);}
    const float fraction=total>0?static_cast<float>(received/total):0.f;
    auto now=NativeChrome::Motion::Now();
    chrome.downloadIcon.Update(active,fraction,completedNow,now,motion.ReducedMotion());
    if(!active)downloadIconRun.clear();
    InvalidateDownloadIcon();ScheduleMotion(now);
}
// Motion lab replays: the real models and renderer, never a navigation, reload, clipboard
// write or download. Reduced motion shows each end state at once, like the real triggers.
NativeChrome::Motion::Time labDownloadStart{};
constexpr double LabDownloadSeconds=1.4;
void MotionLabDownloadTick(){
    auto now=NativeChrome::Motion::Now();const double t=std::chrono::duration<double>(now-labDownloadStart).count();
    if(!labDownloadRunning){KillTimer(window,MotionLabTimerId);return;}
    if(t<LabDownloadSeconds)chrome.downloadIcon.Update(true,(float)(t/LabDownloadSeconds),false,now,motion.ReducedMotion());
    else{chrome.downloadIcon.Update(false,1,true,now,motion.ReducedMotion());labDownloadRunning=false;KillTimer(window,MotionLabTimerId);}
    InvalidateDownloadIcon();ScheduleMotion(now);
}
// i18n-exempt-begin: motion lab (--motion-lab, review tool only): French review notes
std::wstring MotionLabReplay(NativeChrome::Tuning::Group group){
    namespace T=NativeChrome::Tuning;auto now=NativeChrome::Motion::Now();const bool reduced=motion.ReducedMotion();
    std::wstring done=reduced?L" (mouvement réduit actif : état final sans animation)":L"";
    switch(group){
    case T::Back:chrome.nav.back.Kick(-1,now,reduced,TravelParams(true));break;
    case T::Forward:chrome.nav.forward.Kick(1,now,reduced,TravelParams(false));break;
    case T::Refresh:{
        if(chrome.navigationLoading)return L"Un vrai chargement est en cours : rejouer une fois la page chargée.";
        chrome.lottie.RefreshBegin(now);const auto turnEnd=chrome.lottie.TurnEnd();
        chrome.nav.stop.Preview(std::max(turnEnd,now)+std::chrono::milliseconds((long long)T::Current()[T::RefreshPreview]),turnEnd,now,reduced);break;}
    case T::Copy:chrome.lottie.CopySucceeded(now);motion.CopySucceeded(now);break;
    case T::Download:{
        for(auto& item:downloads)if(item.state==DL::State::InProgress||item.state==DL::State::Paused)return L"Un vrai téléchargement est en cours : rejouer après.";
        labDownloadStart=now;labDownloadRunning=true;chrome.downloadIcon.Update(true,0,false,now,reduced);
        SetTimer(window,MotionLabTimerId,50,nullptr);InvalidateDownloadIcon();break;}
    default:break;
    }
    InvalidateChrome();ScheduleMotion(now);
    return std::wstring(L"Rejoué : ")+T::GroupName(group)+done;
}
// i18n-exempt-end
void DownloadsChanged(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */downloadPanel.Changed();downloadDrawer.Changed();}
// Pull the operation's current state into its history row (UI thread, engine events).
void SyncDownload(int id){
    auto found=downloadOps.find(id);auto* item=FindDownload(id);if(found==downloadOps.end()||!item)return;
    auto entry=found->second;auto* op=entry->op.p;if(!op)return;
    INT64 total=0,received=0;op->get_TotalBytesToReceive(&total);op->get_BytesReceived(&received);item->total=total;item->received=received;
    LPWSTR text=nullptr;if(SUCCEEDED(op->get_ResultFilePath(&text))&&text){if(*text)item->path=text;CoTaskMemFree(text);}
    text=nullptr;item->endTime=0;if(SUCCEEDED(op->get_EstimatedEndTime(&text))&&text){item->endTime=DL::ParseIsoTime(text);CoTaskMemFree(text);}
    BOOL resume=FALSE;op->get_CanResume(&resume);item->canResume=resume!=FALSE;
    COREWEBVIEW2_DOWNLOAD_STATE state=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;op->get_State(&state);
    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;if(state==COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED)op->get_InterruptReason(&reason);
    // « Conserver »: the engine let go of the file (our cancel); the kept copy takes its place.
    if(entry->keeping){if(state!=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS)FinishKeep(id,0);return;}
    const auto before=item->state;const bool heldBefore=item->held;
    item->state=DL::MapEngineState(state,reason,entry->userPaused);
    if(item->state==DL::State::Completed)item->missing=false;
    // Every byte in but still in progress: past the grace, the engine holds it for the user's decision (no event says
    // so). One re-check is scheduled for the end of the grace.
    const ULONGLONG tick=GetTickCount64();
    if(state==COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS&&total>0&&received>=total){if(!entry->fullAt){entry->fullAt=tick;DeferAfter((UINT)DL::Engine::HoldGraceMs+50,[id]{if(!halted)SyncDownload(id);});}}
    else entry->fullAt=0;
    item->held=DL::HeldForDecision(state,received,total,entry->userPaused,entry->fullAt?(std::int64_t)(tick-entry->fullAt):-1);
    if(item->held&&!heldBefore)Log(L"DOWNLOAD held by the engine for the user's decision (all bytes in, still in progress): "+DL::FileName(item->path));
    if(!item->held&&heldBefore)Log(L"DOWNLOAD no longer held: state="+std::to_wstring((int)state)+L" reason="+std::to_wstring((int)reason));
    if(item->state==DL::State::Failed){item->reason=InterruptText(reason);
        if(before!=DL::State::Failed&&(reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS||reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED||reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED_BY_POLICY))
            ElementsDangerousDownload(id,item->path,item->uri,reason);}
    if(item->state!=before)Log(L"DOWNLOAD state="+std::to_wstring((int)item->state)+L" "+DL::FileName(item->path));
    // Terminal rows keep no engine reference; resumable failures keep it for Retry.
    if(item->state==DL::State::Completed||item->state==DL::State::Cancelled||(item->state==DL::State::Failed&&!item->canResume))downloadOps.erase(found);
    if(item->state!=before&&DL::Persistent(*item))SaveDownloads();
    // Newly held: the decision lives in the downloads popover, so it is shown (as when a download starts).
    if(item->held&&!heldBefore&&!smoke&&!downloadPanel.Visible()&&!(downloadDrawer.Visible()&&!downloadDrawer.Closing()))ShowDownloads();
    else DownloadsChanged();
    UpdateDownloadIcon(item->state!=before&&item->state==DL::State::Completed);
    if(smoke&&item->state!=before&&testStage==7){
        if(item->state==DL::State::Completed){
            std::ifstream file(std::filesystem::path(item->path),std::ios::binary);std::string body((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());file.close();
            if(body!="browser smoke download"||!downloadPanel.Visible()||downloads.empty()||downloads.front().id!=id||DL::FileName(item->path)!=L"smoke-download.txt"){Fail(L"Smoke download assertion",E_FAIL);return;}
            Log(L"SMOKE download started, shown in history and completed: "+DL::Bytes(item->received,item->total));
            ShowDownloadDrawer();
            if(!downloadDrawer.Visible()||!IsWindowVisible(downloadDrawer.Window())||downloadPanel.Visible()||chrome.latched!='D'){Fail(L"Smoke download drawer did not open",E_FAIL);return;}
            downloadDrawer.Hide(false,false);
            if(downloadDrawer.Visible()||IsWindowVisible(downloadDrawer.Window())||chrome.latched){Fail(L"Smoke download drawer did not close",E_FAIL);return;}
            Log(L"SMOKE download history drawer opened from View all and closed");
            DeleteFileW(item->path.c_str());PostMessageW(window,WM_APP+3,0,0);
        }else if(item->state!=DL::State::InProgress){Fail(L"Smoke download did not complete",E_FAIL);return;}
    }
}
// ---- A download held for the user's decision (B07b « À confirmer ») --------------------------------------
// WebView2 has no API to accept a file its SmartScreen check holds (no danger type, no « keep »): its own flyout is
// the only way, and this browser replaces it. The held file is complete at its final path, so « Conserver » keeps a
// second link to it, lets the engine go (Cancel: it deletes its file), puts the kept link back at the same path, and
// then hands the file to Windows' attachment services like any finished download (Mark of the Web, antivirus
// scan): SmartScreen still checks it when it is opened. « Supprimer » cancels: the engine deletes the file.
std::wstring KeepLinkPath(const std::wstring& path,int id){return path+L".keep-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(id);}
void KeepHeldDownload(int id){
    auto* item=FindDownload(id);auto found=downloadOps.find(id);if(!item||found==downloadOps.end()||!item->held||found->second->keeping)return;
    auto entry=found->second;auto* op=entry->op.p;if(!op)return;
    const auto link=KeepLinkPath(item->path,id);DeleteFileW(link.c_str());
    if(!CreateHardLinkW(link.c_str(),item->path.c_str(),nullptr)&&!CopyFileW(item->path.c_str(),link.c_str(),TRUE)){Log(L"DOWNLOAD keep: the held file could not be kept ("+std::to_wstring(GetLastError())+L")");return;}
    SetFileAttributesW(link.c_str(),FILE_ATTRIBUTE_HIDDEN);
    entry->keeping=true;entry->keepLink=link;
    Log(L"DOWNLOAD keep requested: "+DL::FileName(item->path));
    if(FAILED(op->Cancel())){entry->keeping=false;entry->keepLink.clear();DeleteFileW(link.c_str());Log(L"DOWNLOAD keep: the engine refused to let go");return;}
    SyncDownload(id);
}
// Windows attachment services on a kept file (what the engine does for every finished download): zone mark and
// antivirus scan, off the UI thread. done: 1 saved, 2 refused (the file may be gone).
void QuarantineKeptFile(const std::wstring& path,const std::wstring& uri,std::shared_ptr<std::atomic<int>> done,std::shared_ptr<std::atomic<long>> result){
    std::thread([path,uri,done,result]{
        const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);HRESULT hr=E_FAIL;IAttachmentExecute* ae=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_AttachmentServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&ae)))&&ae){
            static const GUID client={0x6d3f1c52,0x0b2e,0x4d0c,{0x9a,0x41,0x52,0x7e,0x2b,0x11,0x84,0x3a}};   // this browser's downloads
            ae->SetClientGuid(client);ae->SetLocalPath(path.c_str());if(!uri.empty())ae->SetSource(uri.c_str());hr=ae->Save();ae->Release();}
        if(SUCCEEDED(init))CoUninitialize();
        result->store((long)hr);done->store(SUCCEEDED(hr)?1:2);
    }).detach();
}
void KeptFileChecked(int id,std::shared_ptr<std::atomic<int>> done,std::shared_ptr<std::atomic<long>> result,int attempt){
    if(halted)return;const int r=done->load();
    if(!r&&attempt<600){DeferAfter(100,[id,done,result,attempt]{KeptFileChecked(id,done,result,attempt+1);});return;}
    auto* item=FindDownload(id);if(!item)return;
    const bool there=GetFileAttributesW(item->path.c_str())!=INVALID_FILE_ATTRIBUTES;
    if(r!=1)Log(L"DOWNLOAD kept file: attachment services refused it hr="+std::to_wstring((unsigned long)result->load())+std::wstring(there?L" (file kept)":L" (file removed)"));
    else Log(L"DOWNLOAD kept file handed to attachment services");
    if(!there){item->state=DL::State::Failed;item->canResume=false;item->reason=DL::ReasonCode(I18n::Id::download_blocked_dangerous);SaveDownloads();DownloadsChanged();UpdateDownloadIcon(false);}
}
// The engine let go of the held file: the kept link goes back to its path once the engine's file is gone.
void FinishKeep(int id,int attempt){
    auto found=downloadOps.find(id);auto* item=FindDownload(id);if(found==downloadOps.end()||!item||!found->second->keeping)return;
    auto entry=found->second;const auto path=item->path,link=entry->keepLink;
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES&&attempt<30){DeferAfter(100,[id,attempt]{if(!halted)FinishKeep(id,attempt+1);});return;}
    bool kept=false;
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)kept=MoveFileExW(link.c_str(),path.c_str(),MOVEFILE_COPY_ALLOWED)!=FALSE;
    else{kept=true;DeleteFileW(link.c_str());}   // the engine left its file: it is the same file
    if(kept)SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    downloadOps.erase(found);
    item->held=false;item->endTime=0;
    if(kept){item->state=DL::State::Completed;item->missing=false;if(item->total>0)item->received=item->total;Log(L"DOWNLOAD kept by the user: "+DL::FileName(path));}
    else{item->state=DL::State::Failed;item->canResume=false;item->reason=DL::ReasonCode(I18n::Id::download_error_file);DeleteFileW(link.c_str());Log(L"DOWNLOAD keep failed: the kept file could not be put back ("+std::to_wstring(GetLastError())+L")");}
    SaveDownloads();DownloadsChanged();UpdateDownloadIcon(kept);
    if(kept){auto done=std::make_shared<std::atomic<int>>(0);auto result=std::make_shared<std::atomic<long>>(0);QuarantineKeptFile(path,item->uri,done,result);KeptFileChecked(id,done,result,0);}
}
void RefreshMissing(){for(auto& item:downloads)if(item.state==DL::State::Completed)item.missing=GetFileAttributesW(item.path.c_str())==INVALID_FILE_ATTRIBUTES;}
void ShowDownloads(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    if(!downloadPanel.Window())return;
    FocusAnchor(L"downloads"); // Mode Focus: the panel at rest, the popover hangs from its button
    CloseAudioDrawer();RefreshMissing();settingsDrawer.Hide(false,true);historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);
    // Right edge on the toolbar's 6 DIP margin, just under the toolbar row.
    RECT anchor=ChromePx({0,48,chrome.width-NativeChrome::Design::Space::sm,76});MapWindowPoints(window,nullptr,(POINT*)&anchor,2);
    chrome.latched='D';InvalidateChrome();
    POINT from{};if(ColumnPopoverAt('D',NativeChrome::Downloads::Panel::anchorGap,from))downloadPanel.ShowFrom((float)from.x,(float)from.y);
    else downloadPanel.Show(anchor);
    // Once the popover has settled, prepare the drawer so "View all" never waits.
    SetTimer(window,DrawerWarmTimerId,260,nullptr);
}
// "View all": the popover gives way to the drawer docked on the right.
void ShowDownloadDrawer(){
    if(!downloadDrawer.Window())return;
    CloseAudioDrawer();RefreshMissing();settingsDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);chrome.latched='D';InvalidateChrome();
    // From the popover: its rectangle morphs into the drawer.
    RECT from{};const bool morph=downloadPanel.Visible()&&GetWindowRect(downloadPanel.Window(),&from);
    downloadDrawer.Show(morph?&from:nullptr,morph?downloadPanel.Window():nullptr);downloadPanel.Hide(false);
    SyncUpdateToastReserve();
}
void CloseDownloadSurfaces(bool animate=true){downloadPanel.Hide(false);downloadDrawer.Hide(false,animate);}
void ToggleDownloads(){
    if(downloadDrawer.Visible()&&!downloadDrawer.Closing()){downloadDrawer.Hide(true,true);return;}
    if(downloadPanel.Visible()){downloadPanel.Hide(true);return;}
    ShowDownloads();
}
// Download settings (folder, "Demander où enregistrer") applied as a download starts.
std::wstring EffectiveDownloadFolder();
bool downloadFolderOnProfile=false; // the engine profile already saves into EffectiveDownloadFolder()
// Without the profile setting: the engine's suggested name, deduplicated in our folder.
std::wstring UniqueDownloadPath(const std::wstring& folder,const std::wstring& suggested){
    namespace fs=std::filesystem;const fs::path file=fs::path(suggested).filename();fs::path target=fs::path(folder)/file;
    for(int n=1;n<1000&&GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES;n++)target=fs::path(folder)/(file.stem().wstring()+L" ("+std::to_wstring(n)+L")"+file.extension().wstring());
    return target.wstring();
}
// Save dialog for one download: false = the user cancelled. A dialog that cannot
// open keeps the engine's path rather than losing the download.
bool AskDownloadPath(std::wstring& path){
    namespace fs=std::filesystem;const auto file=fs::path(path).filename().wstring(),ext=fs::path(path).extension().wstring();
    IFileSaveDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))||!dialog){Log(L"DOWNLOADS save dialog unavailable");return true;}
    FILEOPENDIALOGOPTIONS options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT|FOS_PATHMUSTEXIST|FOS_NOREADONLYRETURN);
    // Same type first (keeps the extension when the name is retyped), then anything.
    std::wstring kind=ext.size()>1?ext.substr(1):L"";for(auto& c:kind)c=(wchar_t)std::towupper(c);const std::wstring label=TRF(save_dialog_file_kind,kind),spec=L"*"+ext;
    const COMDLG_FILTERSPEC types[2]={{label.c_str(),spec.c_str()},{TR(save_dialog_all_files),L"*.*"}};
    if(kind.empty())dialog->SetFileTypes(1,types+1);else{dialog->SetFileTypes(2,types);dialog->SetDefaultExtension(ext.c_str()+1);}
    dialog->SetTitle(TR(save_dialog_title));dialog->SetFileName(file.c_str());
    IShellItem* folder=nullptr;if(SUCCEEDED(SHCreateItemFromParsingName(EffectiveDownloadFolder().c_str(),nullptr,IID_PPV_ARGS(&folder)))&&folder){dialog->SetFolder(folder);folder->Release();}
    bool keep=true;const HRESULT hr=dialog->Show(window);
    if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))keep=false;
    else if(FAILED(hr))Log(L"DOWNLOADS save dialog failed hr="+std::to_wstring((unsigned long)hr));
    else{IShellItem* item=nullptr;PWSTR chosen=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&item&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&chosen))&&chosen)path=chosen;else Log(L"DOWNLOADS save dialog result unavailable");CoTaskMemFree(chosen);if(item)item->Release();}
    dialog->Release();return keep;
}
void TrackDownload(ICoreWebView2DownloadStartingEventArgs* args,unsigned tab=0,std::shared_ptr<Com<ICoreWebView2Controller>> view=nullptr);
// "Demander où enregistrer": the engine waits on a deferral while the save dialog
// runs from the message loop, never re-entrantly inside its event handler.
constexpr UINT DownloadAskMessage=WM_APP+21;
struct PendingDownload { Com<ICoreWebView2DownloadStartingEventArgs> args; Com<ICoreWebView2Deferral> deferral; unsigned tab=0; std::shared_ptr<Com<ICoreWebView2Controller>> view; };
std::vector<std::shared_ptr<PendingDownload>> pendingDownloads; bool downloadAsking=false;
void AskPendingDownloads(){
    // The modal dialog pumps messages: later downloads queue up behind this loop.
    if(downloadAsking)return;downloadAsking=true;
    while(!pendingDownloads.empty()){
        auto pending=pendingDownloads.front();pendingDownloads.erase(pendingDownloads.begin());auto* args=pending->args.p;
        LPWSTR text=nullptr;std::wstring path;if(SUCCEEDED(args->get_ResultFilePath(&text))&&text){path=text;CoTaskMemFree(text);}
        if(!halted&&AskDownloadPath(path)){if(FAILED(args->put_ResultFilePath(path.c_str())))Log(L"DOWNLOADS put_ResultFilePath failed "+path);TrackDownload(args,pending->tab,pending->view);}
        else{args->put_Cancel(TRUE);Log(L"DOWNLOAD cancelled from the save dialog");}
        if(FAILED(pending->deferral->Complete()))Log(L"DOWNLOADS deferral completion failed");
    }
    downloadAsking=false;
}
HRESULT DownloadStarting(ICoreWebView2DownloadStartingEventArgs* args,unsigned tab){
    if(halted||!args)return S_OK;
    args->put_Handled(TRUE); // the native popover replaces WebView2's flyout
    if(smoke){const auto target=base+L"\\smoke-download.txt";DeleteFileW(target.c_str());args->put_ResultFilePath(target.c_str());TrackDownload(args,tab);return S_OK;}
    if(settings.askWhereToSave){
        auto pending=std::make_shared<PendingDownload>();
        if(SUCCEEDED(args->GetDeferral(&pending->deferral.p))&&pending->deferral.p){pending->args.set(args);pending->tab=tab;pendingDownloads.push_back(pending);PostMessageW(window,DownloadAskMessage,0,0);return S_OK;}
        Log(L"DOWNLOADS deferral unavailable: saving without asking");
    }
    if(!downloadFolderOnProfile&&!settings.downloadFolder.empty()){
        LPWSTR text=nullptr;if(SUCCEEDED(args->get_ResultFilePath(&text))&&text){const auto target=UniqueDownloadPath(settings.downloadFolder,text);CoTaskMemFree(text);if(FAILED(args->put_ResultFilePath(target.c_str())))Log(L"DOWNLOADS put_ResultFilePath failed "+target);}
    }
    TrackDownload(args,tab);return S_OK;
}
// A closing tab with a download in progress (or waiting for « Enregistrer sous »): its WebView stays, hidden, until
// those downloads end (DownloadOperation::view), or their events would stop. True when the controller was kept.
bool KeepControllerForDownloads(const std::shared_ptr<Tab>& t){
    if(!t||!t->controller.p)return false;
    std::shared_ptr<Com<ICoreWebView2Controller>> view;
    auto keep=[&]{if(!view){view=std::make_shared<Com<ICoreWebView2Controller>>();view->set(t->controller.p);}return view;};
    for(auto& [id,entry]:downloadOps){if(!entry||entry->tab!=t->serial||entry->view)continue;const auto* item=FindDownload(id);if(item&&(item->state==DL::State::InProgress||item->state==DL::State::Paused))entry->view=keep();}
    for(auto& pending:pendingDownloads)if(pending&&pending->tab==t->serial&&!pending->view)pending->view=keep();
    if(!view)return false;
    t->controller->put_IsVisible(FALSE);Log(L"DOWNLOAD tab closed during its download: its page stays hidden until the download ends");return true;
}
// History row + popover for a download whose path is settled.
void TrackDownload(ICoreWebView2DownloadStartingEventArgs* args,unsigned tab,std::shared_ptr<Com<ICoreWebView2Controller>> view){
    ICoreWebView2DownloadOperation* raw=nullptr;if(FAILED(args->get_DownloadOperation(&raw))||!raw){Log(L"DOWNLOAD operation unavailable");return;}
    auto entry=std::make_shared<DownloadOperation>();entry->op.p=raw;entry->owner=WorkspaceNow();entry->tab=tab;entry->view=std::move(view);   // the profile that started it (scope of DownloadStarting)
    DL::Item item;item.id=nextDownloadId++;item.started=NativeChrome::DownloadsUI::UnixNowMs();const int id=item.id;
    LPWSTR text=nullptr;if(SUCCEEDED(args->get_ResultFilePath(&text))&&text){item.path=text;CoTaskMemFree(text);}
    text=nullptr;if(SUCCEEDED(raw->get_Uri(&text))&&text){item.uri=text;CoTaskMemFree(text);}
    if(item.path.empty())item.path=L"download";
    downloads.insert(downloads.begin(),item);downloadOps[id]=entry;
    EventRegistrationToken token{};
    auto bytes=Callback<ICoreWebView2BytesReceivedChangedEventHandler,ICoreWebView2DownloadOperation*,IUnknown*>([id](ICoreWebView2DownloadOperation*,IUnknown*)->HRESULT{if(!halted)SyncDownload(id);return S_OK;});
    raw->add_BytesReceivedChanged(bytes,&token);bytes->Release();
    auto end=Callback<ICoreWebView2EstimatedEndTimeChangedEventHandler,ICoreWebView2DownloadOperation*,IUnknown*>([id](ICoreWebView2DownloadOperation*,IUnknown*)->HRESULT{if(!halted)SyncDownload(id);return S_OK;});
    raw->add_EstimatedEndTimeChanged(end,&token);end->Release();
    auto state=Callback<ICoreWebView2StateChangedEventHandler,ICoreWebView2DownloadOperation*,IUnknown*>([id](ICoreWebView2DownloadOperation*,IUnknown*)->HRESULT{if(!halted)SyncDownload(id);return S_OK;});
    raw->add_StateChanged(state,&token);state->Release();
    Log(L"DOWNLOAD started "+DL::FileName(downloads.front().path));
    // Open (or refresh) the history first so even an instant completion is shown.
    if(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))DownloadsChanged();else ShowDownloads();
    SyncDownload(id);
}
void DownloadAction(int id,DL::Action action){
    auto* item=FindDownload(id);if(!item)return;
    auto found=downloadOps.find(id);auto entry=found==downloadOps.end()?nullptr:found->second;auto* op=entry?entry->op.p:nullptr;
    switch(action){
    // Pause / Resume / Cancel need the row's live operation. Without it (never expected), the row is not left offering
    // a dead action: it becomes a non-resumable failure (« Remove »), logged.
    case DL::Action::Pause:case DL::Action::Resume:case DL::Action::Retry:case DL::Action::Cancel:
        if(!op){Log(L"DOWNLOAD action without its operation: "+DL::FileName(item->path));if(item->state==DL::State::InProgress||item->state==DL::State::Paused||item->state==DL::State::Failed){item->state=DL::State::Failed;item->canResume=false;item->reason=DL::ReasonCode(I18n::Id::downloads_unavailable);SaveDownloads();DownloadsChanged();UpdateDownloadIcon(false);}break;}
        if(action==DL::Action::Pause){if(SUCCEEDED(op->Pause()))entry->userPaused=true;}
        else if(action==DL::Action::Cancel){const HRESULT hr=op->Cancel();Log(L"DOWNLOAD cancel "+DL::FileName(item->path)+(SUCCEEDED(hr)?L"":L" failed"));}
        else if(SUCCEEDED(op->Resume()))entry->userPaused=false;
        SyncDownload(id);break;
    case DL::Action::Keep:KeepHeldDownload(id);break;
    // « Supprimer » on a held file: the engine cancels and deletes it; the row leaves the list (like B07's Delete).
    case DL::Action::Discard:{if(!item->held||(entry&&entry->keeping))break;const auto path=item->path;Log(L"DOWNLOAD held file discarded by the user: "+DL::FileName(path));DownloadAction(id,DL::Action::Remove);
        DeferAfter(1500,[path]{if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES&&!DeleteFileW(path.c_str()))Log(L"DOWNLOAD discarded file could not be removed");});break;}
    case DL::Action::Remove:{if(op&&(item->state==DL::State::InProgress||item->state==DL::State::Paused))op->Cancel();downloadOps.erase(id);downloads.erase(std::find_if(downloads.begin(),downloads.end(),[id](const DL::Item& d){return d.id==id;}));SaveDownloads();DownloadsChanged();UpdateDownloadIcon(false);break;}
    case DL::Action::ShowInFolder:{const auto path=item->path;CloseDownloadSurfaces();
        PIDLIST_ABSOLUTE pidl=ILCreateFromPathW(path.c_str());HRESULT hr=pidl?SHOpenFolderAndSelectItems(pidl,0,nullptr,0):E_FAIL;if(pidl)ILFree(pidl);
        if(FAILED(hr))Log(L"DOWNLOAD show in folder failed "+path);break;}
    case DL::Action::Open:{const auto path=item->path;CloseDownloadSurfaces();
        if((INT_PTR)ShellExecuteW(window,nullptr,path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)Log(L"DOWNLOAD open failed "+path);break;}
    default:break;
    }
}
void CreateDownloadPanel(){
    NativeChrome::DownloadsUI::Popover::Hooks hooks;
    hooks.schedule=[](NativeChrome::Motion::Time now){ScheduleMotion(now);};
    hooks.action=[](int id,DL::Action action){DownloadAction(id,action);};
    // The toolbar button stays latched while either surface is (still) open.
    hooks.closed=[](bool keyboard){SyncLatch();SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    hooks.viewAll=[](){ShowDownloadDrawer();};
    auto drawerHooks=hooks;
    if(!downloadPanel.Create(instance,window,base,&downloads,std::move(hooks)))Log(L"DOWNLOADS panel unavailable");
    if(!downloadDrawer.Create(instance,window,base,&downloads,std::move(drawerHooks)))Log(L"DOWNLOADS drawer unavailable");
}

// ---- Settings ----------------------------------------------------------------
// "Réglages" drawer (src/settings-panel.h): the gear slides it in from the right
// edge, where the download history docks too, so opening one closes the other.
// The state persists in `settings` next to `session`; smoke runs keep the
// defaults and never write it. Each setting's behaviour lives in one function
// below, called by SettingChanged() after the drawer changed the state.
bool settingsDismissClick=false;
// Interprocess transaction, field by field: a setting changed in this window takes this window's
// value; every other setting keeps the value another window may have saved meanwhile.
BrowserSettings::State settingsBase;
// Called only for a VOLUNTARY change (Réglages, « Ne plus demander »): D3, a private window keeps it too, through
// the same interprocess transaction (it is a choice, not a trace of browsing; nothing else is written in private).
void SaveSettings(){
    if(smoke)return;
    auto text=[](const BrowserSettings::State& s){std::ostringstream o;BrowserSettings::Write(o,s);return o.str();};
    const auto r=ProfileStore::CommitWith<BrowserSettings::State>(dataDir,settingsFile,settingsBase,settings,
        [](std::istream& in,bool*){return BrowserSettings::Read(in);},[](std::ostream& out,const BrowserSettings::State& s){BrowserSettings::Write(out,s);},
        [&text](const BrowserSettings::State& b,const BrowserSettings::State& m,const BrowserSettings::State& t){std::istringstream in(ProfileStore::MergeKeyValues(text(b),text(m),text(t)));return BrowserSettings::Read(in);},5000,{"browser-settings 1"});
    if(r==ProfileStore::Result::Failed||r==ProfileStore::Result::Busy)Log(L"SETTINGS save failed");
}
void LoadSettings(){if(smoke)return;std::string bytes;{ProfileStore::Lock lock(dataDir);ProfileStore::ReadAll(settingsFile,bytes);}std::istringstream in(bytes);settings=BrowserSettings::Read(in);settingsBase=settings;ApplyLayout();}
// Download folder: the chosen one, else the system Downloads folder.
std::wstring SystemDownloadFolder(){PWSTR known=nullptr;std::wstring path;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,0,nullptr,&known))&&known)path=known;CoTaskMemFree(known);return path;}
std::wstring EffectiveDownloadFolder(){return settings.downloadFolder.empty()?SystemDownloadFolder():settings.downloadFolder;}
void SettingChanged(BrowserSettings::Change change);
// SETTINGS(downloads): folder picker for "Enregistrer dans" (modal; the drawer stays open).
void PickDownloadFolder(){
    IFileOpenDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))||!dialog){Log(L"DOWNLOADS folder picker unavailable");return;}
    FILEOPENDIALOGOPTIONS options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(TR(folder_dialog_title));
    IShellItem* start=nullptr;if(SUCCEEDED(SHCreateItemFromParsingName(EffectiveDownloadFolder().c_str(),nullptr,IID_PPV_ARGS(&start)))&&start){dialog->SetFolder(start);start->Release();}
    std::wstring chosen;const HRESULT hr=dialog->Show(window);
    if(SUCCEEDED(hr)){IShellItem* item=nullptr;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&item&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))&&path)chosen=path;else Log(L"DOWNLOADS picked folder has no file system path");CoTaskMemFree(path);if(item)item->Release();}
    else if(hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED))Log(L"DOWNLOADS folder picker failed hr="+std::to_wstring((unsigned long)hr));
    dialog->Release();if(chosen.empty())return;
    // Picking Windows' own Downloads stores "default", so it keeps following Windows.
    const auto same=[](std::wstring a,std::wstring b){while(a.size()>3&&(a.back()==L'\\'||a.back()==L'/'))a.pop_back();while(b.size()>3&&(b.back()==L'\\'||b.back()==L'/'))b.pop_back();return CompareStringOrdinal(a.c_str(),(int)a.size(),b.c_str(),(int)b.size(),TRUE)==CSTR_EQUAL;};
    const std::wstring folder=same(chosen,SystemDownloadFolder())?L"":chosen;
    if(folder==settings.downloadFolder)return;
    settings.downloadFolder=folder;Log(L"SETTINGS download folder "+EffectiveDownloadFolder());
    SettingChanged(BrowserSettings::Change::DownloadFolder);
}
// The profile is shared by every tab: setting it once covers existing and future
// downloads (those in progress keep their path).
bool ApplyDownloadFolder(ICoreWebView2* view){
    const auto folder=EffectiveDownloadFolder();if(folder.empty())return true;
    Com<ICoreWebView2_13> view13;Com<ICoreWebView2Profile> profile;
    if(!view||FAILED(view->QueryInterface(IID_ICoreWebView2_13,(void**)&view13.p))||!view13.p||FAILED(view13->get_Profile(&profile.p))||!profile.p)return false;
    if(FAILED(profile->put_DefaultDownloadFolderPath(folder.c_str()))){Log(L"DOWNLOADS put_DefaultDownloadFolderPath failed "+folder);return false;}
    return true;
}
// SETTINGS(downloads): push the folder to the engine (new and existing tabs).
// Without a profile, DownloadStarting() sets each download's path instead.
void ApplyDownloadSettings(){
    if(smoke)return;
    bool any=false,applied=false;for(auto& t:pages)if(t&&!t->closed&&t->view.p){any=true;if(ApplyDownloadFolder(t->view.p)){applied=true;break;}}
    if(!any)return;
    static bool logged=false;if(!applied&&!logged){Log(L"DOWNLOADS profile download folder unavailable: path set per download");logged=true;}
    downloadFolderOnProfile=applied;
}
// SETTINGS(motion): Windows' animation switch or "Réduire les animations".
bool ReducedMotionPreferred(){NativeChrome::Motion::SetAppReducedMotion(settings.reduceMotion);return NativeChrome::Motion::PreferredReducedMotion();}
// SETTINGS(motion): every animated surface follows ReducedMotionPreferred(). Also on
// startup and WM_SETTINGCHANGE. Popups re-read the process-wide preference on Show();
// Lottie and the loading bar take motion.ReducedMotion() at each frame.
void ApplyReducedMotion(){
    const bool reduced=ReducedMotionPreferred();
    // Settles running interactions; a half-played copy Lottie must not resume later.
    motion.SetReducedMotion(reduced);chrome.SetTabsReducedMotion(reduced);if(reduced)ClearChromeMotion();
    downloadPanel.SetReducedMotion(reduced);downloadDrawer.SetReducedMotion(reduced);historyDrawer.SetReducedMotion(reduced);bookmarksDrawer.SetReducedMotion(reduced);
    SyncUpdateToastMotion();
    settingsDrawer.SetReducedMotion();ElementsReducedMotion(reduced);
    if(reduced)ScrollbarSettleHover();
    InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());
}
// SETTINGS(data): "Effacer les données…" (modal confirmation first).
void AnnounceDataCleared();
// The exe carries no comctl32 v6 manifest, so TaskDialogIndirect is resolved at
// run time (absent from v5) and the prompt falls back to a plain MessageBox.
// `ok` labels IDOK (Annuler is IDCANCEL); `body` may be null; `okDefault` picks the default button.
bool ConfirmDialog(const wchar_t* caption,const wchar_t* title,const wchar_t* body,const wchar_t* ok,bool okDefault){
    using TaskDialogFn=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
    HMODULE comctl=GetModuleHandleW(L"comctl32.dll");
    auto taskDialog=comctl?reinterpret_cast<TaskDialogFn>(GetProcAddress(comctl,"TaskDialogIndirect")):nullptr;
    if(taskDialog){
        TASKDIALOG_BUTTON buttons[]={{IDOK,ok},{IDCANCEL,TR(common_cancel)}};
        TASKDIALOGCONFIG config{};config.cbSize=sizeof config;config.hwndParent=window;config.hInstance=instance;
        config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;config.pszWindowTitle=caption;
        config.pszMainIcon=TD_WARNING_ICON;config.pszMainInstruction=title;config.pszContent=body;
        config.pButtons=buttons;config.cButtons=2;config.nDefaultButton=okDefault?IDOK:IDCANCEL;
        int button=IDCANCEL;if(SUCCEEDED(taskDialog(&config,&button,nullptr,nullptr)))return button==IDOK;
    }
    return MessageBoxW(window,(std::wstring(title)+(body?std::wstring(L"\n\n")+body:L"")).c_str(),caption,MB_OKCANCEL|MB_ICONWARNING|(okDefault?MB_DEFBUTTON1:MB_DEFBUTTON2))==IDOK;
}
bool ConfirmClearData(){
    return ConfirmDialog(TR(clear_data_window),TR(clear_data_title),TR(clear_data_body),TR(clear_data_action),false);
}
std::wstring HrText(HRESULT hr){wchar_t code[16];swprintf(code,16,L"0x%08lX",(unsigned long)hr);return code;}
// Local traces: reopen stack, history, site icons and finished download rows; live downloads (and their ops) stay.
// « Effacer les données » is an explicit wipe, not a merge: under the profile lock the history and the
// finished-downloads files are replaced outright (whatever they held, even another window's newer
// entries or an unreadable file), so nothing cleared survives on disk. The other windows pick the
// wipe up at their next activation or save (review S5): their old entries never come back, since a
// merge only re-adds visits made after their base.
// Returns true only when both files really are wiped (a busy lock or a failed write is reported).
bool WipeLocalTraceFiles(){
    if(smoke||privateWindow)return true;
    ProfileStore::Lock lock(dataDir);if(!lock.Held()){Log(L"CLEAR profile busy: files not wiped");return false;}
    bool ok=true;
    {std::ostringstream o;AddressBar::History{}.Write(o);if(ProfileStore::WriteAtomic(HistoryFile(),o.str())){historyBase=AddressBar::History{};TraceErase::DropDamagedBackups(HistoryFile());}else{ok=false;Log(L"CLEAR history wipe failed");}}
    {std::vector<DL::Item> keep;for(const auto& d:downloads)if(!DL::Persistent(d))keep.push_back(d);std::ostringstream o;DL::WriteHistory(o,keep);if(ProfileStore::WriteAtomic(downloadsFile,o.str())){downloadsBase.clear();TraceErase::DropDamagedBackups(downloadsFile);}else{ok=false;Log(L"CLEAR downloads wipe failed");}}
    // The browsing log goes too (audit SEC-05); a window appending at that instant keeps it a moment: said in the log, not a failure.
    {bool erased=true;{std::lock_guard<std::mutex> guard(logLock);if(!logFile.empty())erased=LogPrivacy::Erase(logFile);}if(!erased)Log(L"CLEAR log file busy: not deleted");}
    return ok;
}
void FormsWipe();   // src/forms-integration.h: the e-mails typed and sent (#121 P13), erased with the engine's form data
void ClearLocalTraces(){
    FormsWipe();closedTabs.clear();history.Clear();ClearSiteIcons();if(!WorkspaceParked())historyDrawer.Changed();
    downloads.erase(std::remove_if(downloads.begin(),downloads.end(),[](const DL::Item& d){return d.state!=DL::State::InProgress&&d.state!=DL::State::Paused;}),downloads.end());
    PruneDownloadOps();   // this profile's operations only
    if(!WipeLocalTraceFiles())Log(L"CLEAR local traces: NOT fully erased on disk");DownloadsChanged();
}
// Engine data lives in the shared profile: any live tab's webview reaches it. `out`
// receives the profile as `iid` (false: no live tab, or a runtime without it).
bool LiveProfile(REFIID iid,void** out){
    *out=nullptr;
    for(auto& t:pages){if(!t||t->closed||!t->view.p)continue;Com<ICoreWebView2_13> view13;Com<ICoreWebView2Profile> base;
        if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_13,(void**)&view13.p))&&view13.p&&SUCCEEDED(view13->get_Profile(&base.p))&&base.p&&
           SUCCEEDED(base->QueryInterface(iid,out))&&*out)return true;}
    return false;
}
// ClearBrowsingDataAll; `done` runs on the UI thread with the engine's result. False when
// the call itself failed (logged), so `done` will never run.
bool StartClearEngineData(ICoreWebView2Profile2* profile,std::function<void(HRESULT)> done){
    auto handler=Callback<ICoreWebView2ClearBrowsingDataCompletedHandler,HRESULT>([done](HRESULT hr)->HRESULT{
        if(SUCCEEDED(hr))Log(L"SETTINGS browsing data cleared");else Log(L"SETTINGS clear data failed "+HrText(hr));done(hr);return S_OK;});
    HRESULT hr=profile->ClearBrowsingDataAll(handler);handler->Release();
    if(FAILED(hr)){Log(L"SETTINGS ClearBrowsingDataAll failed "+HrText(hr));return false;}
    return true;
}
void ClearBrowsingData(){if(smoke)return;AskClearBrowsingData();}
// After the A5 confirmation (src/elements-integration.h).
void StartClearBrowsingData(std::function<void(HRESULT)>){
    ClearLocalTraces();
    Com<ICoreWebView2Profile2> profile;
    if(!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){Log(L"SETTINGS clear data: no ICoreWebView2Profile2 (no live tab or old runtime); local traces only");AnnounceDataCleared();return;}
    StartClearEngineData(profile.p,[](HRESULT hr){if(SUCCEEDED(hr))AnnounceDataCleared();});
}
// SETTINGS(privacy): tracking prevention, password saving and form autofill on every
// tab's profile/settings (also called for each new tab); see ClearOnExitAndClose for quitting.
// Tracking prevention is profile-wide (the environment keeps EnableTrackingPrevention at
// its default TRUE: no options are passed); passwords/autofill are per-webview settings.
void VaultCaptureSync();
void ApplyPrivacySettings(){
    if(smoke)return;
    VaultCaptureSync();   // review V-5: the vault capture follows « Mots de passe » on every open tab
    static bool trackingLogged=false,formsLogged=false;bool any=false;
    for(auto& t:pages){if(!t||t->closed||!t->view.p)continue;any=true;
        Com<ICoreWebView2Settings> base;Com<ICoreWebView2Settings4> s4;
        if(FAILED(t->view->get_Settings(&base.p))||!base.p||FAILED(base->QueryInterface(IID_ICoreWebView2Settings4,(void**)&s4.p))||!s4.p){
            if(!formsLogged){Log(L"SETTINGS ICoreWebView2Settings4 unavailable: password and autofill switches ignored");formsLogged=true;}continue;}
        // Review P1: a private window's engine never saves passwords or form data (Chromium would keep
        // them in the on-disk private profile).
        HRESULT hr=s4->put_IsPasswordAutosaveEnabled(settings.passwords&&!privateWindow&&!Vault::Active()?TRUE:FALSE); /* new saves go to our vault only once it is active (decision 1); older engine passwords keep filling */if(SUCCEEDED(hr))hr=s4->put_IsGeneralAutofillEnabled(Forms::EngineAutofill&&settings.autofill&&!privateWindow?TRUE:FALSE); /* #121 P13: our list replaces the engine's (never two lists); its saved form data is kept, never deleted */
        if(FAILED(hr)&&!formsLogged){Log(L"SETTINGS password/autofill settings failed "+HrText(hr));formsLogged=true;}
        {static int lastAutosave=-1;BOOL on=FALSE;   // review R-5: what the ENGINE reports (read back), logged on change only
         if(SUCCEEDED(s4->get_IsPasswordAutosaveEnabled(&on))&&(on?1:0)!=lastAutosave){lastAutosave=on?1:0;Log(std::wstring(L"SETTINGS engine password autosave ")+(on?L"on":L"off")+L" (read back from the engine)");}}
    }
    if(!any)return;
    Com<ICoreWebView2Profile3> profile;HRESULT hr=E_NOINTERFACE;
    if(LiveProfile(IID_ICoreWebView2Profile3,(void**)&profile.p))hr=profile->put_PreferredTrackingPreventionLevel(static_cast<COREWEBVIEW2_TRACKING_PREVENTION_LEVEL>(settings.tracking));
    if(FAILED(hr)&&!trackingLogged){Log(L"SETTINGS tracking prevention unavailable "+HrText(hr));trackingLogged=true;}
}
// SETTINGS(privacy): "Effacer en quittant" at WM_CLOSE. Returns true when it took over the
// close (it destroys the window itself once the engine finished, or after a timeout).
// The engine clears through the live webviews, so the window (and its tabs) stays up
// meanwhile; a slow or stuck engine must not keep the browser from quitting.
constexpr UINT_PTR ExitClearTimerId=75;constexpr UINT ExitClearTimeoutMs=3000;
enum class ExitClear{Idle,Waiting,Done} exitClear=ExitClear::Idle;
void FinishExitClear(){
    if(exitClear!=ExitClear::Waiting)return; // completion and timeout race: first one wins
    exitClear=ExitClear::Done;KillTimer(window,ExitClearTimerId);DestroyWindow(window);
}
bool ClearOnExitAndClose(){
    if(exitClear!=ExitClear::Idle)return true; // repeated WM_CLOSE while waiting
    if(!settings.clearOnExit||smoke)return false;
    ClearLocalTraces();
    Com<ICoreWebView2Profile2> profile;
    if(!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){Log(L"SETTINGS clear on exit: no ICoreWebView2Profile2; local traces only");return false;}
    if(!SetTimer(window,ExitClearTimerId,ExitClearTimeoutMs,nullptr))return false; // no timeout guard: do not wait
    exitClear=ExitClear::Waiting;
    if(!StartClearEngineData(profile.p,[](HRESULT){FinishExitClear();})){exitClear=ExitClear::Idle;KillTimer(window,ExitClearTimerId);return false;}
    Log(L"SETTINGS clear on exit: waiting for the engine");
    return true;
}
// SETTINGS(tabs): "Confirmer avant de fermer plusieurs onglets", asked at WM_CLOSE (caption
// button, Alt+F4, taskbar). False = keep the window. Never for smoke runs, the updater's
// install handoff (closeWithoutPrompt), a close already under way (clear on exit), or a
// WM_CLOSE arriving while the question is open.
bool closeWithoutPrompt=false;
bool ConfirmWindowClose(){
    static bool asking=false;if(asking)return false;
    if(smoke||halted||closeWithoutPrompt||exitClear!=ExitClear::Idle||!settings.confirmCloseTabs)return true;
    const int n=(int)std::count_if(pages.begin(),pages.end(),[](const std::shared_ptr<Tab>& t){return t&&!t->closed;});if(n<=1)return true;
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);
    const std::wstring title=TRP(close_window_title,n);
    asking=true;const bool ok=ConfirmDialog(TR(chrome_tip_close_window),title.c_str(),settings.restoreTabs?TR(close_window_restore_note):nullptr,TR(common_close),true);asking=false;
    if(!ok)Log(L"TABS window close cancelled");
    return ok;
}
// SETTINGS(permissions): the panel's Autorisations views. Load is asynchronous (engine call);
// `done` runs on the UI thread with the sites that have at least one non-default permission.
struct EnginePermission { std::wstring origin; COREWEBVIEW2_PERMISSION_KIND kind; COREWEBVIEW2_PERMISSION_STATE state; };
void LogPermissionHr(const wchar_t* what,HRESULT hr){wchar_t code[16];swprintf(code,16,L"0x%08lX",(unsigned long)hr);Log(std::wstring(L"SETTINGS permissions ")+what+L" failed "+code);}
// Permissions live in the shared profile: any live tab's webview reaches it.
bool PermissionProfile(Com<ICoreWebView2Profile4>& profile){
    for(auto& t:pages){if(t->closed||!t->view.p)continue;Com<ICoreWebView2_13> view13;Com<ICoreWebView2Profile> base;
        if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_13,(void**)&view13.p))&&view13.p&&SUCCEEDED(view13->get_Profile(&base.p))&&base.p&&
           SUCCEEDED(base->QueryInterface(IID_ICoreWebView2Profile4,(void**)&profile.p))&&profile.p)return true;}
    Log(L"SETTINGS permissions: no ICoreWebView2Profile4 (no live tab or old runtime)");return false;
}
BrowserSettings::Permission PermissionFromEngine(COREWEBVIEW2_PERMISSION_KIND k){
    using P=BrowserSettings::Permission;
    switch(k){case COREWEBVIEW2_PERMISSION_KIND_CAMERA:return P::Camera;case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:return P::Microphone;
    case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:return P::Location;case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:return P::Notifications;
    case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:return P::Clipboard;default:return P::Other;}
}
// In-flight Set/Reset calls: the drawer reloads right after a change, so a Load issued
// meanwhile waits for them instead of reading the pre-change state.
int permissionWrites=0; std::vector<std::function<void()>> permissionLoadsWaiting;
void PermissionWriteDone(){if(--permissionWrites>0)return;permissionWrites=0;auto waiting=std::move(permissionLoadsWaiting);permissionLoadsWaiting.clear();for(auto& f:waiting)f();}
void WritePermission(ICoreWebView2Profile4* profile,const std::wstring& origin,COREWEBVIEW2_PERMISSION_KIND kind,COREWEBVIEW2_PERMISSION_STATE state){
    ++permissionWrites;
    auto cb=Callback<ICoreWebView2SetPermissionStateCompletedHandler,HRESULT>([](HRESULT hr)->HRESULT{if(FAILED(hr))LogPermissionHr(L"SetPermissionState",hr);PermissionWriteDone();return S_OK;});
    HRESULT hr=profile->SetPermissionState(kind,origin.c_str(),state,cb);cb->Release();
    if(FAILED(hr)){LogPermissionHr(L"SetPermissionState call",hr);PermissionWriteDone();}
}
// Raw engine list; `done` runs on the UI thread (WebView2 completes there), never once halted or the window is gone.
void QueryPermissions(std::function<void(std::vector<EnginePermission>)> done){
    Com<ICoreWebView2Profile4> profile;if(halted||!PermissionProfile(profile)){if(!halted)done({});return;}
    auto cb=Callback<ICoreWebView2GetNonDefaultPermissionSettingsCompletedHandler,HRESULT,ICoreWebView2PermissionSettingCollectionView*>(
        [done](HRESULT hr,ICoreWebView2PermissionSettingCollectionView* view)->HRESULT{
        if(halted||!IsWindow(window))return S_OK;
        std::vector<EnginePermission> out;UINT32 count=0;
        if(FAILED(hr)||!view||FAILED(view->get_Count(&count))){LogPermissionHr(L"GetNonDefaultPermissionSettings",FAILED(hr)?hr:E_POINTER);done({});return S_OK;}
        for(UINT32 i=0;i<count;i++){Com<ICoreWebView2PermissionSetting> s;if(FAILED(view->GetValueAtIndex(i,&s.p))||!s.p)continue;
            EnginePermission e{};LPWSTR origin=nullptr;
            if(FAILED(s->get_PermissionKind(&e.kind))||FAILED(s->get_PermissionState(&e.state))||FAILED(s->get_PermissionOrigin(&origin))||!origin){if(origin)CoTaskMemFree(origin);continue;}
            e.origin=origin;CoTaskMemFree(origin);if(e.state!=COREWEBVIEW2_PERMISSION_STATE_DEFAULT&&!e.origin.empty())out.push_back(std::move(e));}
        done(std::move(out));return S_OK;});
    HRESULT hr=profile->GetNonDefaultPermissionSettings(cb);cb->Release();
    if(FAILED(hr)){LogPermissionHr(L"GetNonDefaultPermissionSettings call",hr);done({});}
}
// SETTINGS(permissions): the panel's Autorisations views. Load is asynchronous (engine call);
// `done` runs on the UI thread with the sites that have at least one non-default permission.
void LoadSitePermissions(std::function<void(std::vector<BrowserSettings::Site>)> done){
    if(permissionWrites>0){permissionLoadsWaiting.push_back([done](){LoadSitePermissions(done);});return;}
    QueryPermissions([done](std::vector<EnginePermission> raw){
        using namespace BrowserSettings;
        std::vector<std::pair<std::wstring,SitePermission>> rows;
        for(auto& e:raw)rows.push_back({e.origin,{PermissionFromEngine(e.kind),e.state==COREWEBVIEW2_PERMISSION_STATE_ALLOW?PermissionState::Allow:PermissionState::Block}});
        std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first<b.first:(int)a.second.kind<(int)b.second.kind;});
        std::vector<Site> sites;
        for(auto& r:rows){
            if(sites.empty()||sites.back().origin!=r.first)sites.push_back({r.first,{}});
            auto& perms=sites.back().permissions;
            // Several engine kinds fold into Other: one row, a block anywhere wins.
            if(!perms.empty()&&perms.back().kind==r.second.kind){if(r.second.state==PermissionState::Block)perms.back().state=PermissionState::Block;continue;}
            perms.push_back(r.second);}
        done(std::move(sites));});
}
void SetSitePermission(const std::wstring& origin,BrowserSettings::Permission kind,BrowserSettings::PermissionState state){
    using namespace BrowserSettings;
    if(origin.empty()||halted)return;
    const auto engineState=state==PermissionState::Allow?COREWEBVIEW2_PERMISSION_STATE_ALLOW:state==PermissionState::Block?COREWEBVIEW2_PERMISSION_STATE_DENY:COREWEBVIEW2_PERMISSION_STATE_DEFAULT;
    if(kind!=Permission::Other){
        const auto k=kind==Permission::Camera?COREWEBVIEW2_PERMISSION_KIND_CAMERA:kind==Permission::Microphone?COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:kind==Permission::Location?COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:kind==Permission::Notifications?COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ;
        Com<ICoreWebView2Profile4> profile;if(PermissionProfile(profile))WritePermission(profile.p,origin,k,engineState);
        return;}
    // Other covers several engine kinds: apply to those this origin already has. The query
    // counts as a write so a reload waits for the whole change.
    ++permissionWrites;
    QueryPermissions([origin,engineState](std::vector<EnginePermission> raw){
        Com<ICoreWebView2Profile4> profile;
        if(PermissionProfile(profile))for(auto& e:raw)if(e.origin==origin&&PermissionFromEngine(e.kind)==Permission::Other)WritePermission(profile.p,e.origin,e.kind,engineState);
        PermissionWriteDone();});
}
// Empty origin: every site.
void ResetSitePermissions(const std::wstring& origin){
    if(halted)return;
    ++permissionWrites;
    QueryPermissions([origin](std::vector<EnginePermission> raw){
        Com<ICoreWebView2Profile4> profile;
        if(PermissionProfile(profile))for(auto& e:raw)if(origin.empty()||e.origin==origin)WritePermission(profile.p,e.origin,e.kind,COREWEBVIEW2_PERMISSION_STATE_DEFAULT);
        Log(origin.empty()?L"SETTINGS permissions reset for every site":L"SETTINGS permissions reset for "+origin);
        PermissionWriteDone();});
}
// SETTINGS(tabs): background tabs sleep after settings.sleepMinutes out of sight.
// One 60 s timer, armed only while the setting is on and an awake background tab exists.
constexpr UINT_PTR SleepTimerId=76; bool sleepTimerArmed=false; std::weak_ptr<Tab> shownTab;
void ApplySleepSettings(){
    if(!window)return;auto active=Active();bool want=false;
    if(settings.sleepMinutes>0&&!halted)for(auto& t:pages)if(t!=active&&!t->shown&&!t->closed&&!t->sleeping){want=true;break;}
    if(want==sleepTimerArmed)return;
    if(want)sleepTimerArmed=SetTimer(window,SleepTimerId,60000,nullptr)!=0;else{KillTimer(window,SleepTimerId);sleepTimerArmed=false;}
}
// Called by Select: the previous tab starts its out-of-sight clock; a sleeping tab wakes
// (put_IsVisible(TRUE) resumes it too; Resume also covers a hidden home tab).
void ApplyTabSettings();void ApplyMemorySaver();
void TabShown(const std::shared_ptr<Tab>& tab){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    auto previous=shownTab.lock();if(previous==tab)return;
    if(previous)previous->hiddenAt=GetTickCount64();shownTab=tab;ApplyTabSettings();ApplyMemorySaver();
    if(tab&&tab->sleeping){tab->sleeping=false;Com<ICoreWebView2_3> v3;if(tab->view.p&&SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v3.p))&&v3.p)v3->Resume();InvalidateChrome();}
    ApplySleepSettings();
}
void SleepTabs(){
    if(halted||settings.sleepMinutes<=0){ApplySleepSettings();return;}
    const ULONGLONG now=GetTickCount64(),after=settings.sleepMinutes*60000ull;auto active=Active();
    // Downloads are not tied to a tab: any live transfer defers sleeping.
    const bool downloading=std::any_of(downloads.begin(),downloads.end(),[](const DL::Item& d){return d.state==DL::State::InProgress;});
    if(!downloading)for(auto& t:pages){
        if(t==active||t->shown||t->closed||t->sleeping||t->home||!t->view.p||now-t->hiddenAt<after)continue;
        Com<ICoreWebView2_8> v8;BOOL audio=FALSE;
        if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_8,(void**)&v8.p))&&v8.p&&SUCCEEDED(v8->get_IsDocumentPlayingAudio(&audio))&&audio)continue;
        Com<ICoreWebView2_3> v3;if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v3.p))||!v3.p)continue;
        std::weak_ptr<Tab> weak=t;
        auto done=Callback<ICoreWebView2TrySuspendCompletedHandler,HRESULT,BOOL>([weak](HRESULT hr,BOOL ok)->HRESULT{
            auto tab=weak.lock();if(!tab||tab->closed||halted||FAILED(hr)||!ok)return S_OK;
            // Selected while the suspend was in flight: wake it straight back.
            if(tab==Active()||tab->shown){Com<ICoreWebView2_3> v;if(SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v.p))&&v.p)v->Resume();return S_OK;}
            AnnotationTabSleeping(tab);tab->sleeping=true;Log(L"TABS sleeping "+tab->uri);InvalidateChrome();ApplySleepSettings();return S_OK;});
        HRESULT hr=v3->TrySuspend(done);done->Release();if(FAILED(hr))Log(L"TABS TrySuspend failed "+t->uri);
    }
    ApplySleepSettings();
}
// SETTINGS(tabs): settings.zoomPercent as every tab's zoom (and new tabs').
void ApplyZoom(ICoreWebView2Controller* c){if(c&&FAILED(c->put_ZoomFactor(settings.zoomPercent/100.0)))Log(L"SETTINGS put_ZoomFactor failed");}
void ApplyZoomSettings(){for(auto& t:pages)if(!t->closed)ApplyZoom(t->controller.p);ApplyPages();}
// SETTINGS(v3) stubs, filled per area: tabs/media, https/cookies, adblock/memory, new tab page.
// SETTINGS(tabs): tab position, close confirmation and close-with-last-tab are read where tabs
// open/close (UserTabIndex, ConfirmWindowClose, CloseAt). Here: "Couper le son des onglets en
// arrière-plan", only the active tab plays sound. Re-applied by TabShown (selection) and for each
// new webview. There is no per-tab user mute, so turning it off unmutes every tab; while it has
// never been on, no tab is touched.
bool backgroundMuted=false;
void ApplyTabSettings(){
    if(halted||(!settings.muteBackground&&!backgroundMuted))return;
    static bool logged=false;auto active=Active();
    for(auto& t:pages){if(!t||t->closed||!t->view.p)continue;
        Com<ICoreWebView2_8> v8;HRESULT hr=t->view->QueryInterface(IID_ICoreWebView2_8,(void**)&v8.p);
        if(SUCCEEDED(hr)&&v8.p)hr=v8->put_IsMuted(settings.muteBackground&&t!=active?TRUE:FALSE);else if(SUCCEEDED(hr))hr=E_NOINTERFACE;
        if(FAILED(hr)&&!logged){Log(L"SETTINGS mute background tabs unavailable "+HrText(hr));logged=true;}
    }
    backgroundMuted=settings.muteBackground;
}
// SETTINGS(media): the autoplay policy is an engine argument set in wWinMain before the
// environment exists, so a change applies at the next launch.
void ApplyMediaSettings(){Log(L"SETTINGS autoplay "+std::wstring(BrowserSettings::Info(settings.autoplay).label)+L": applies at next launch");}
// SETTINGS(https): "HTTPS uniquement". Top-level http:// navigations are cancelled in
// NavigationStarting and retried as https:// (local and private hosts and the hosts allowed
// this session are left alone). When the https:// attempt fails to connect, or its server
// redirects back to http://, the tab shows the "Ce site n'est pas sécurisé" page (NavigateToString,
// the address bar keeps the http:// address); its buttons post `token:back` / `token:continue`.
std::vector<std::wstring> httpAllowedHosts; // « Continuer quand même », for this session only
std::vector<std::function<void()>> deferredJobs;
// Navigations started from inside a WebView2 event handler run on the next message instead.
void CALLBACK RunDeferredJobs(HWND,UINT,UINT_PTR id,DWORD){KillTimer(nullptr,id);auto jobs=std::move(deferredJobs);deferredJobs.clear();for(auto& job:jobs)if(!halted)job();}
void Defer(std::function<void()> job){job=[owner=WorkspaceNow(),job=std::move(job)]{if(WorkspaceDead(owner))return; /* its workspace was disposed: dropped, like a late COM event */WorkspaceScope scope(owner);job();};deferredJobs.push_back(std::move(job));if(deferredJobs.size()==1&&!SetTimer(nullptr,0,0,RunDeferredJobs)){auto jobs=std::move(deferredJobs);deferredJobs.clear();for(auto& j:jobs)if(!halted)j();}}
// Lower-case host (IPv6 kept in brackets), explicit port and authority end of an http:// URL.
bool HttpParts(const std::wstring& uri,std::wstring& host,std::wstring& port,size_t& end){
    if(uri.size()<8||_wcsnicmp(uri.c_str(),L"http://",7)!=0)return false;
    end=uri.find_first_of(L"/?#",7);if(end==std::wstring::npos)end=uri.size();
    std::wstring authority=uri.substr(7,end-7);const auto at=authority.rfind(L'@');if(at!=std::wstring::npos)authority.erase(0,at+1);
    size_t colon=std::wstring::npos;
    if(!authority.empty()&&authority[0]==L'['){const auto close=authority.find(L']');if(close==std::wstring::npos)return false;host=authority.substr(0,close+1);if(close+1<authority.size()&&authority[close+1]==L':')colon=close+1;}
    else{colon=authority.find(L':');host=authority.substr(0,colon);}
    port=colon==std::wstring::npos?L"":authority.substr(colon+1);
    std::transform(host.begin(),host.end(),host.begin(),towlower);while(!host.empty()&&host.back()==L'.')host.pop_back();
    return !host.empty();
}
bool EndsWith(const std::wstring& s,const wchar_t* suffix){const size_t n=wcslen(suffix);return s.size()>=n&&s.compare(s.size()-n,n,suffix)==0;}
// Hosts that never get an https:// upgrade: loopback, link-local and private addresses,
// single-label intranet names, *.local / *.localhost, and the session allowlist.
bool HttpExempt(const std::wstring& host){
    if(std::find(httpAllowedHosts.begin(),httpAllowedHosts.end(),host)!=httpAllowedHosts.end())return true;
    if(host[0]==L'[')return host==L"[::1]"||host.rfind(L"[fe80:",0)==0||host.rfind(L"[fc",0)==0||host.rfind(L"[fd",0)==0||host.rfind(L"[::ffff:127.",0)==0;
    if(host==L"localhost"||EndsWith(host,L".localhost")||EndsWith(host,L".local")||host.find(L'.')==std::wstring::npos)return true;
    unsigned a=0,b=0,c=0,d=0;wchar_t extra=0;
    if(swscanf(host.c_str(),L"%u.%u.%u.%u%lc",&a,&b,&c,&d,&extra)==4&&a<256&&b<256&&c<256&&d<256)
        return a==127||a==10||a==0||(a==172&&b>=16&&b<=31)||(a==192&&b==168)||(a==169&&b==254);
    return false;
}
void PageFonts(NativeChrome::ErrorPages::Extras& x,bool mono); // elements-integration.h
// The "not secure" page for `insecure` (an http:// address), in place of the failed attempt
// (#121 layout and lock-open icon: NativeChrome::ErrorPages::InsecureHtml).
void ShowInsecureInterstitial(const std::shared_ptr<Tab>& t,const std::wstring& insecure){
    if(halted||!t||t->closed||!t->view.p)return;
    std::wstring host,port;size_t end=0;if(!HttpParts(insecure,host,port,end))return;
    GUID guid{};wchar_t token[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,token,40))return;
    auto& h=t->https;h.shown=insecure;h.token=token;h.pending=true;h.upgrade=0;
    t->uri=insecure;t->home=false;t->title=TR(insecure_title);
    Log(L"HTTPS interstitial "+insecure);
    NativeChrome::ErrorPages::Extras extras;PageFonts(extras,false);extras.reducedMotion=motion.ReducedMotion();
    const std::wstring html=NativeChrome::ErrorPages::InsecureHtml(host,token,extras);
    CheckTab(t->view->NavigateToString(html.c_str()),L"NavigateToString interstitial");
    if(t==Active())Update();
}
// NavigationStarting hook (top-level). True when the navigation was cancelled here.
bool HttpsNavigationStarting(const std::shared_ptr<Tab>& t,ICoreWebView2NavigationStartingEventArgs* a,const std::wstring& uri,UINT64 id){
    auto& h=t->https;const bool blank=uri==L"about:blank"||uri.rfind(L"data:",0)==0;
    if(blank&&h.pending){h.pending=false;return false;} // the interstitial itself
    COREWEBVIEW2_NAVIGATION_KIND kind=COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT;
    {Com<ICoreWebView2NavigationStartingEventArgs3> a3;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2NavigationStartingEventArgs3,(void**)&a3.p))&&a3.p)a3->get_NavigationKind(&kind);}
    if(blank&&!h.shown.empty()&&t->uri==h.shown&&kind==COREWEBVIEW2_NAVIGATION_KIND_RELOAD){ // Reload on the interstitial: try https:// again
        a->put_Cancel(TRUE);std::weak_ptr<Tab> weak=t;auto target=h.shown;h.shown.clear();
        Defer([weak,target]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,target);});return true;}
    h.shown.clear();h.pending=false;
    if(!h.expectHost.empty()){std::wstring host,port;size_t end=0;if(uri.rfind(L"https://",0)==0&&HttpParts(L"http://"+uri.substr(8),host,port,end)&&host==h.expectHost)h.upgrade=id;h.expectHost.clear();}
    std::wstring host,port;size_t end=0;
    if(!settings.httpsOnly||!HttpParts(uri,host,port,end)||HttpExempt(host))return false;
    if(FAILED(a->put_Cancel(TRUE)))return false;
    BOOL redirected=FALSE;a->get_IsRedirected(&redirected);
    const bool downgrade=redirected&&h.upgrade==id; // the https:// attempt redirected back to http://
    if(!downgrade){LPWSTR source=nullptr;h.from=SUCCEEDED(t->view->get_Source(&source))&&source?source:L"";CoTaskMemFree(source);}
    std::weak_ptr<Tab> weak=t;
    if(downgrade||(!port.empty()&&port!=L"80")){ // https:// cannot be tried on another port
        Log(L"HTTPS no secure version "+uri);
        Defer([weak,uri]{ShowInsecureInterstitial(weak.lock(),uri);});return true;}
    std::wstring secure=L"https://"+uri.substr(7,end-7-(port.empty()?0:port.size()+1))+uri.substr(end);
    h.insecure=uri;h.expectHost=host;h.upgrade=0;
    Log(L"HTTPS upgrade "+uri);
    Defer([weak,secure]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,secure);});
    return true;
}
// NavigationCompleted hook for a failed navigation: an https:// upgrade that could not connect
// (DNS, TCP, TLS or certificate) gives the interstitial. True when it was shown.
bool HttpsUpgradeFailed(const std::shared_ptr<Tab>& t,UINT64 id,COREWEBVIEW2_WEB_ERROR_STATUS error){
    auto& h=t->https;if(!h.upgrade||h.upgrade!=id||h.insecure.empty())return false;h.upgrade=0;
    switch(error){
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE:
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:case COREWEBVIEW2_WEB_ERROR_STATUS_ERROR_HTTP_INVALID_SERVER_RESPONSE:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED:case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_REDIRECT_FAILED:case COREWEBVIEW2_WEB_ERROR_STATUS_UNEXPECTED_ERROR:
        Log(L"HTTPS upgrade failed WebErrorStatus="+std::to_wstring(error)+L" "+h.insecure);
        ShowInsecureInterstitial(t,h.insecure);return true;
    default:return false; // offline, cancelled, authentication: the engine's own page
    }
}
// The interstitial's buttons. Only the document the tab shows as interstitial, with its token.
HRESULT InterstitialMessage(const std::shared_ptr<Tab>& t,ICoreWebView2WebMessageReceivedEventArgs* a){
    if(halted||!t||t->closed||t->https.shown.empty()||t->https.pending||t->uri!=t->https.shown)return S_OK;
    LPWSTR source=nullptr,raw=nullptr;
    const bool blank=SUCCEEDED(a->get_Source(&source))&&source&&(wcscmp(source,L"about:blank")==0||wcsncmp(source,L"data:",5)==0);CoTaskMemFree(source);
    if(!blank||FAILED(a->TryGetWebMessageAsString(&raw))||!raw){CoTaskMemFree(raw);return S_OK;}
    std::wstring message=raw;CoTaskMemFree(raw);const auto& h=t->https;
    if(message.size()<=h.token.size()+1||message.compare(0,h.token.size(),h.token)!=0||message[h.token.size()]!=L':')return S_OK;
    const std::wstring action=message.substr(h.token.size()+1),insecure=h.shown;std::weak_ptr<Tab> weak=t;
    if(action==L"continue"){
        std::wstring host,port;size_t end=0;if(!HttpParts(insecure,host,port,end))return S_OK;
        if(std::find(httpAllowedHosts.begin(),httpAllowedHosts.end(),host)==httpAllowedHosts.end())httpAllowedHosts.push_back(host);
        Log(L"HTTPS allowed for this session "+host);
        Defer([weak,insecure]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,insecure);});
    }else if(action==L"back"){
        std::wstring from=h.from;const bool web=_wcsnicmp(from.c_str(),L"https://",8)==0||_wcsnicmp(from.c_str(),L"http://",7)==0;
        Defer([weak,from,web]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,web?from:L"");});
    }
    return S_OK;
}
bool AudioPageMessage(const std::shared_ptr<Tab>& t,const std::wstring& message,const std::wstring& source);bool ScrollbarMessageFrom(const std::shared_ptr<Tab>& t,const std::wstring& text);
ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* PopupScriptBarrier(const PopupHost::RequestPtr& popup,const wchar_t* what); // audio-integration.h
const std::wstring& PopupQa();
void InjectAudioScript(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup); // audio-integration.h
#include "elements-integration.h"
#include "tabs-position-icon.h"
#include "forms-integration.h"   // #121: our list under a page field, our validation bubble (design/security-review-autofill-under-field.md)
void SyncLatch();
#include "audio-integration.h"
#include "profiles-integration.h"
#include "viewport-scrollbar-host.h"
// SETTINGS(cookies): third-party cookie blocking is Chromium's ForceThirdPartyCookieBlockingEnabled
// feature (net::cookie_util::IsForceThirdPartyCookieBlockingEnabled also needs
// ThirdPartyStoragePartitioning), an engine argument set in wWinMain: applies at the next launch.
// Adds features to the --enable-features list of an argument string without duplicating either.
std::wstring WithEnabledFeatures(std::wstring args,const std::vector<std::wstring>& features){
    const std::wstring key=L"--enable-features=";auto at=args.rfind(key); // the last one wins
    if(at==std::wstring::npos){std::wstring list;for(auto& f:features)list+=(list.empty()?L"":L",")+f;return args+(args.empty()?L"":L" ")+key+list;}
    size_t start=at+key.size();if(start<args.size()&&args[start]==L'"')++start;
    const size_t stop=args.find_first_of(L" \t\"",start);const std::wstring present=L","+args.substr(start,stop==std::wstring::npos?std::wstring::npos:stop-start)+L",";
    std::wstring add;for(auto& f:features)if(present.find(L","+f+L",")==std::wstring::npos)add+=f+L",";
    if(!add.empty()){if(start==args.size()||present==L",,")add.pop_back();args.insert(start,add);}
    return args;
}
void ApplySecuritySettings(){
    Log(settings.httpsOnly?L"SETTINGS https-only on":L"SETTINGS https-only off");
    Log(std::wstring(settings.blockThirdPartyCookies?L"SETTINGS block third-party cookies on":L"SETTINGS block third-party cookies off")+L": applies at next launch");
}
// SETTINGS(adblock): settings.adBlock runs uBlock Origin Lite (MV3, unmodified, GPLv3; fetched by
// fetch-ublock-lite.py into assets\extensions\ublock-lite). Blocking is declarativeNetRequest inside
// the engine: no per-request callback here. Engine extensions are on outside smoke runs (EngineOptions)
// and the profile keeps the install, so the toggle is Enable(TRUE/FALSE), live for every tab.
// The unpacked extension's id follows its folder, and each installed version has its own folder:
// dataDir\adblock remembers "<folder>\n<id>" of the current install; any other "uBlock Origin Lite"
// in the profile is an older version's leftover and is removed. One sync at a time.
bool adBlockSynced=false,adBlockBusy=false,adBlockAgain=false,adBlockNoProfileLogged=false;void ApplyAdBlock();
const wchar_t* const AdBlockName=L"uBlock Origin Lite";
// Installed from a folder that survives updates (src/adblock-folder.h): <data root>\extensions\ublock-lite-<version>.
// Resolved once per launch; the bundled versions\<v>\assets\... folder only when that copy is not possible.
std::wstring AdBlockSource(){static const std::wstring folder=[]{std::wstring note;const std::wstring f=AdBlockFolder::Stable(base+L"\\assets\\extensions\\ublock-lite",profileRoot.empty()?dataDir:profileRoot,&note);Log(L"SETTINGS adblock source: "+note);return f;}();return folder;}
std::string AdBlockMarker(const std::wstring& id){return Utf8(AdBlockSource())+"\n"+Utf8(id);}
void AdBlockDone(){adBlockBusy=false;if(adBlockAgain&&!halted){adBlockAgain=false;ApplyAdBlock();}}
void AdBlockEnable(ICoreWebView2BrowserExtension* ext,bool on){
    auto done=Callback<ICoreWebView2BrowserExtensionEnableCompletedHandler,HRESULT>([on](HRESULT hr)->HRESULT{
        Log(FAILED(hr)?L"SETTINGS adblock Enable failed":on?L"SETTINGS adblock on (uBlock Origin Lite enabled)":L"SETTINGS adblock off (uBlock Origin Lite disabled)");AdBlockDone();return S_OK;});
    HRESULT hr=ext->Enable(on?TRUE:FALSE,done);done->Release();if(FAILED(hr)){Log(L"SETTINGS adblock Enable call failed");AdBlockDone();}
}
void AdBlockInstall(ICoreWebView2Profile7* profile){
    const auto folder=AdBlockSource();
    if(GetFileAttributesW((folder+L"\\manifest.json").c_str())==INVALID_FILE_ATTRIBUTES){Log(L"SETTINGS adblock: extension missing ("+folder+L"); browsing continues without ad blocking");AdBlockDone();return;}
    auto done=Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler,HRESULT,ICoreWebView2BrowserExtension*>([](HRESULT hr,ICoreWebView2BrowserExtension* ext)->HRESULT{
        LPWSTR id=nullptr;
        if(FAILED(hr)||!ext||FAILED(ext->get_Id(&id))||!id){wchar_t code[16];swprintf(code,16,L"0x%08lX",(unsigned long)hr);Log(std::wstring(L"SETTINGS adblock install failed ")+code);AdBlockDone();return S_OK;}
        const std::wstring extId=id;CoTaskMemFree(id);
        {std::ofstream f(std::filesystem::path(dataDir+L"\\adblock"),std::ios::binary|std::ios::trunc);f<<AdBlockMarker(extId);}
        Log(L"SETTINGS adblock installed uBlock Origin Lite "+extId);
        if(!settings.adBlock){AdBlockEnable(ext,false);return S_OK;} // turned off while installing
        AdBlockDone();return S_OK;});
    HRESULT hr=profile->AddBrowserExtension(folder.c_str(),done);done->Release();if(FAILED(hr)){Log(L"SETTINGS adblock AddBrowserExtension call failed");AdBlockDone();}
}
void ApplyAdBlock(){
    if(privateWindow)return; // no extension install from a private window (it would write the profile)
    if(smoke||halted)return;if(adBlockBusy){adBlockAgain=true;return;}
    Com<ICoreWebView2Profile7> profile;
    if(!LiveProfile(IID_ICoreWebView2Profile7,(void**)&profile.p)){if(!adBlockNoProfileLogged){adBlockNoProfileLogged=true;Log(L"SETTINGS adblock: no ICoreWebView2Profile7 yet (no live tab or old runtime)");}return;}
    adBlockSynced=adBlockBusy=true;
    auto listed=Callback<ICoreWebView2ProfileGetBrowserExtensionsCompletedHandler,HRESULT,ICoreWebView2BrowserExtensionList*>([](HRESULT hr,ICoreWebView2BrowserExtensionList* list)->HRESULT{
        Com<ICoreWebView2Profile7> profile;
        if(halted||FAILED(hr)||!list||!LiveProfile(IID_ICoreWebView2Profile7,(void**)&profile.p)){if(!halted)Log(L"SETTINGS adblock: extension list unavailable");AdBlockDone();return S_OK;}
        std::string marker;{std::ifstream in(std::filesystem::path(dataDir+L"\\adblock"),std::ios::binary);marker.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());}
        Com<ICoreWebView2BrowserExtension> ours;UINT count=0;list->get_Count(&count);
        for(UINT i=0;i<count;++i){
            Com<ICoreWebView2BrowserExtension> ext;LPWSTR id=nullptr,name=nullptr;
            if(FAILED(list->GetValueAtIndex(i,&ext.p))||!ext.p)continue;
            if(SUCCEEDED(ext->get_Id(&id))&&id&&SUCCEEDED(ext->get_Name(&name))&&name&&std::wstring(name)==AdBlockName){
                if(!ours.p&&marker==AdBlockMarker(id))ours.set(ext.p);
                else{Log(L"SETTINGS adblock removing a previous install "+std::wstring(id));auto removed=Callback<ICoreWebView2BrowserExtensionRemoveCompletedHandler,HRESULT>([](HRESULT r)->HRESULT{if(FAILED(r))Log(L"SETTINGS adblock remove failed");return S_OK;});if(FAILED(ext->Remove(removed)))Log(L"SETTINGS adblock Remove call failed");removed->Release();}}
            if(id)CoTaskMemFree(id);if(name)CoTaskMemFree(name);
        }
        if(ours.p){BOOL on=FALSE;ours->get_IsEnabled(&on);if(!!on!=settings.adBlock){AdBlockEnable(ours.p,settings.adBlock);return S_OK;}Log(settings.adBlock?L"SETTINGS adblock on":L"SETTINGS adblock off");AdBlockDone();return S_OK;}
        if(settings.adBlock){AdBlockInstall(profile.p);return S_OK;}
        Log(L"SETTINGS adblock off (not installed)");AdBlockDone();return S_OK;});
    HRESULT hr=profile->GetBrowserExtensions(listed);listed->Release();if(FAILED(hr)){Log(L"SETTINGS adblock GetBrowserExtensions call failed");adBlockBusy=false;}
}
// SETTINGS(memory): settings.memorySaver asks the engine to trim background tabs
// (MemoryUsageTargetLevel LOW, scripts keep running); the active tab, and every tab once it is
// off, is NORMAL. Re-applied by TabShown (selection) and for each new webview. Sleeping tabs are
// left alone: the engine ignores the level while suspended, TrySuspend sets LOW and Resume NORMAL.
void ApplyMemorySaver(){
    static bool unsupportedLogged=false;auto active=Active();
    for(auto& t:pages){
        if(!t||t->closed||t->sleeping||!t->view.p)continue;
        Com<ICoreWebView2_19> v19;if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_19,(void**)&v19.p))||!v19.p){if(!unsupportedLogged){unsupportedLogged=true;Log(L"SETTINGS memory saver: ICoreWebView2_19 unavailable");}return;}
        const auto want=settings.memorySaver&&t!=active?COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW:COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL;
        COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL now=COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL;
        if(SUCCEEDED(v19->get_MemoryUsageTargetLevel(&now))&&now==want)continue;
        if(FAILED(v19->put_MemoryUsageTargetLevel(want)))Log(L"SETTINGS memory saver: put_MemoryUsageTargetLevel failed "+t->uri);
    }
}
// NEWTAB: open home tabs re-render (frequent sites <-> blank); other tabs are untouched.
// "Page personnalisée" applies to the next new tab: an open home tab then shows the blank page.
void ApplyNewTabPage(){
    Log(std::wstring(L"SETTINGS new tab page ")+(settings.newTabPage==BrowserSettings::NewTabPage::FrequentSites?L"frequent sites":settings.newTabPage==BrowserSettings::NewTabPage::Custom?L"custom":L"blank"));
    if(halted)return;const auto html=HomeDocument();const bool blank=html==HomeHtml();
    for(auto& t:std::vector<std::shared_ptr<Tab>>(pages))if(!t->closed&&t->home&&t->view.p&&!(blank&&!homeTiles.Has(*t)))Navigate(t,L"");
}
// SETTINGS(updates): footer line and the check-for-updates button.
NativeChrome::SettingsUI::Drawer::Version SettingsVersion(); // defined with the self-update host (needs the engine)
void SettingChanged(BrowserSettings::Change change){
    using BrowserSettings::Change;SaveSettings();
    switch(change){
    case Change::Engine:Log(L"SETTINGS search engine "+std::wstring(BrowserSettings::Info(settings.engine).name));break;
    case Change::RestoreTabs:Log(settings.restoreTabs?L"SETTINGS restore tabs on":L"SETTINGS restore tabs off");break;
    case Change::DownloadFolder:case Change::AskWhereToSave:ApplyDownloadSettings();break;
    case Change::ReduceMotion:Log(settings.reduceMotion?L"SETTINGS reduced motion on":L"SETTINGS reduced motion off");ApplyReducedMotion();break;
    case Change::SearchSuggestions:if(!settings.searchSuggestions)HideSuggestions();break;
    case Change::Tracking:case Change::Passwords:case Change::Autofill:ApplyPrivacySettings();break;
    case Change::ClearOnExit:break;
    case Change::Sleep:ApplySleepSettings();break;
    case Change::Zoom:ApplyZoomSettings();break;
    case Change::CustomSearch:break;
    case Change::TabPosition:case Change::ConfirmCloseTabs:case Change::CloseWithLastTab:case Change::MuteBackground:ApplyTabSettings();break;
    case Change::Autoplay:ApplyMediaSettings();break;
    case Change::TabsPosition:forcedLayout=-1;ApplyLayout();if(window&&chrome.sidebar)Layout();settingsDrawer.Changed();Log(std::wstring(L"SETTINGS tabs position ")+BrowserSettings::Info(settings.tabsPosition).label);break;
    case Change::GrayscaleText:Log(settings.grayscaleText?L"SETTINGS grayscale text on: applies at next launch":L"SETTINGS grayscale text off: applies at next launch");break;
    // Réglages › « Annotations »: the launcher shows / hides at once; the launch prompt and the
    // retention are read by the mode when it next needs them (src/annotation-mode.h, Prefs).
    case Change::AnnotationConnectPrompt:Log(BrowserSettings::ConnectPromptActive(settings)?L"SETTINGS annotation connect prompt on (« Plus tard » count reset)":L"SETTINGS annotation connect prompt off");AnnotationSettingsChanged();break;
    case Change::AnnotationRetention:Log(L"SETTINGS annotation retention "+std::to_wstring(settings.annotationRetentionDays)+L" days");AnnotationSettingsChanged();break;
    case Change::DefaultBrowserLater:Log(L"SETTINGS default browser card: later (30 days)");break;
    case Change::FocusMode:Log(settings.focusMode?L"SETTINGS focus mode on":L"SETTINGS focus mode off");Layout();break; // instant relayout, like the bookmarks bar
    case Change::HttpsOnly:case Change::ThirdPartyCookies:ApplySecuritySettings();break;
    case Change::AdBlock:ApplyAdBlock();break;
    case Change::MemorySaver:ApplyMemorySaver();break;
    case Change::NewTabPage:case Change::Homepage:ApplyNewTabPage();break;
    case Change::BookmarksBar:{const bool shown=chrome.bandShown;BookmarksChanged();if(chrome.bandShown!=shown)accessibility.Notify('K',chrome.bandShown?TR(bookmarks_bar_shown):TR(bookmarks_bar_hidden),L"Bookmarks" /* i18n-exempt: UIA activity id */);Log(L"SETTINGS bookmarks bar "+std::wstring(BrowserSettings::ShortLabel(settings.bookmarksBar)));break;}
    }
}
// The toolbar button of the surface that is (still) open stays latched.
void SyncLatch(){chrome.latched=audioPanel.Visible()?'V':settingsDrawer.Open()?'S':(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))?'D':historyDrawer.Open()?'Y':0;InvalidateChrome();}
void ShowSettings(){
    if(!settingsDrawer.Window())return;
    FocusAnchor(L"settings"); // Mode Focus: the panel at rest above the drawer
    CloseAudioDrawer();CloseDownloadSurfaces();historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);chrome.latched='S';InvalidateChrome();
    settingsDrawer.Show();SyncUpdateToastReserve();
}
void ToggleSettings(){if(settingsDrawer.Open()){settingsDrawer.Hide(true,true);return;}ShowSettings();}
// #121: « Gérer la saisie automatique » opens Réglages on its section.
void ShowSettingsAt(NativeChrome::SettingsUI::View v){
    if(!settingsDrawer.Window())return;
    CloseAudioDrawer();CloseDownloadSurfaces();historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);chrome.latched='S';InvalidateChrome();
    settingsDrawer.Show(v);SyncUpdateToastReserve();
}
void CreateSettingsPanel(){
    NativeChrome::SettingsUI::Drawer::Hooks hooks;
    hooks.schedule=[](NativeChrome::Motion::Time now){ScheduleMotion(now);};
    hooks.closed=[](bool keyboard){SyncLatch();SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    hooks.changed=SettingChanged;
    hooks.pickFolder=PickDownloadFolder;
    // The drawer knows how « Effacer les données… » was activated (click vs Entrée / Espace / UIA):
    // that is the host's last input for the confirmation's focus ring, whatever the chrome saw before.
    hooks.clearData=[](bool keyboard){keyboardModality=keyboard;ClearBrowsingData();};
    // Réglages › Profils (src/profiles-integration.h): list, detail editor, delete with undo.
    hooks.profiles=[]{return SettingsProfiles();};hooks.profile=[](const std::wstring& id){const auto* p=profileRegistry.Find(id);return p?*p:DefaultProfileLook();};
    hooks.saveProfile=[](const Profiles::Profile& p,Profiles::NameError* ne){return SaveProfileEdit(p,ne);};
    hooks.validateProfileName=[](const std::wstring& n,const std::wstring& id){return Profiles::ValidateName(profileRegistry,n,id);};
    hooks.deleteProfile=[](const std::wstring& id,bool keyboard){keyboardModality=keyboard;AskDeleteProfile(id,keyboard);};
    hooks.profilesReadOnly=[]{return profileRegistry.readOnly;};
    hooks.managePasswords=[](bool keyboard){ShowVaultManager(keyboard);};
    if(!privateWindow)hooks.importData=[](bool keyboard){ShowImportDialog(keyboard);};   // a private window imports nothing into its profile
    // The Réglages card « Voulez-vous avoir Browser comme navigateur par défaut ? » (src/default-browser.h): offered while
    // Windows knows Browser (the installed copy registered it) and http / https do not open with it. « Browser par défaut »
    // only opens Windows' Default apps page on Browser: the default is never changed silently (Windows forbids it anyway).
    hooks.defaultBrowser=[]{extern bool qaDefaultCard;if(qaDefaultCard)return 0;   /* dev builds, --qa-default-card: the card shown for review */
        if(!DefaultBrowser::Registered(HKEY_CURRENT_USER))return -1;return DefaultBrowser::StatusFrom(DefaultBrowser::SystemDefault)==DefaultBrowser::Status::Default?1:0;};
    hooks.setDefaultBrowser=[]{const std::wstring uri=DefaultBrowser::SettingsUri();Log(L"DEFAULT BROWSER Windows' Default apps page asked for");ShellExecuteW(window,L"open",uri.c_str(),nullptr,nullptr,SW_SHOWNORMAL);};
    hooks.vaultInfo=[]{return VaultInfo();};
    hooks.clearOnExitNote=[]{return ClearOnExitNote();};   // owner decision B
    hooks.checkUpdates=[](){CheckForUpdates();};
    hooks.version=SettingsVersion;
    // The version line opens « À propos de Browser » (Paper « À propos : point d'entrée » A; the A8 window): the drawer
    // closes first and the window gets the keyboard back when the A8 window closes (as from the old menu).
    hooks.about=[](bool keyboard){settingsDrawer.Hide(false,true);if(window)SetFocus(window);keyboardModality=keyboard;Log(std::wstring(L"ABOUT opened from Réglages")+(keyboard?L" (keyboard)":L""));ShowAboutDialog();};
    hooks.downloadFolder=EffectiveDownloadFolder;
    hooks.reducedMotion=ReducedMotionPreferred;
    hooks.loadSites=LoadSitePermissions;
    hooks.setPermission=SetSitePermission;
    hooks.resetSites=ResetSitePermissions;
    // Réglages › Langue: one preference for the whole browser (the global data root, every profile and window).
    // Saving never switches this process: the next start of a window reads it (src/ui-language.h).
    hooks.savedLanguage=[]{return UiLanguage::Load(profileRoot.empty()?dataDir:profileRoot).locale;};
    hooks.saveLanguage=[](I18n::Locale l){const bool ok=UiLanguage::Save(profileRoot.empty()?dataDir:profileRoot,l);Log(std::wstring(L"UI LANGUAGE save ")+(l==I18n::Locale::En?L"en":L"fr")+(ok?L" ok":L" FAILED"));return ok;};
    // Value rows open the native menu as their list of choices.
    hooks.choose=[](const std::vector<NativeChrome::ContextMenu::Item>& items,RECT anchor,bool keyboard){return contextMenu.Track(items,{anchor.left,anchor.bottom},&anchor,keyboard,motion.ReducedMotion(),true);};
    // « Annotations » (design/annotations-settings.md): dev / preview builds with the mode, never in a
    // private window. The drawer only asks; every disk / other-app access runs in src/annotation-mode.h, off the UI thread.
    namespace AS=AnnotationSettings;
    hooks.annotations=[]{return AnnotationsCompiled()&&AnnotationHistoryAvailable();};
    hooks.loadAgents=[](std::function<void(AS::Agents)> done){AnnotationAgentsStatus([done](const AnnotationAgentState& claude,const AnnotationAgentState& codex){
        auto to=[](const AnnotationAgentState& s){AS::Agent a;a.detected=s.detected;a.configured=s.configured;a.connected=s.connected;a.stalePath=s.stalePath;a.lastReadAt=s.lastReadAt;a.restart=s.restart;return a;};
        AS::Agents agents;agents.claude=to(claude);agents.codex=to(codex);agents.codex.app=AS::App::Codex;if(done)done(agents);});};
    hooks.connectAgent=[](AS::App app,bool on,std::function<void(bool,std::wstring)> done){   // explicit click only: same path, backups and test guard as the modal
        AnnotationConnectAgent(app==AS::App::Claude?AnnotationApp::Claude:AnnotationApp::Codex,on,[done](bool ok,const std::wstring& message){if(done)done(ok,message);});};
    hooks.openConnect=[]{settingsDrawer.Hide(false,true);AnnotationOpenConnect(true);};   // « Autre app ou réglage manuel »: the modal on its manual fallback
    hooks.annotationFolder=[]{return AnnotationStoreRoot();};
    hooks.openAnnotationFolder=[]{AnnotationOpenFolder();};
    hooks.countAnnotations=[](std::function<void(std::size_t)> done){AnnotationCount(std::move(done));};
    hooks.clearAnnotations=[](std::size_t n,bool keyboard){
        keyboardModality=keyboard;   // the confirmation's focus ring follows how the row was activated (as « Effacer les données… »)
        auto spec=NativeChrome::SettingsUI::ClearAnnotationsSpec(n);spec.buttons[1].closes=false;
        spec.onButton=[](int id){
            if(id!=MD::AcceptButton)return;
            modal.SetBusy(true,TR(annotations_clearing));
            AnnotationClearAll([](bool ok,std::size_t removed){
                if(!ok){modal.SetBusy(false);modal.SetError(TR(annotations_clear_failed));}
                else{modal.Finish(MD::AcceptButton);accessibility.Notify('S',TRP(annotations_cleared,removed).c_str(),L"Settings");}   // i18n-exempt: UIA activity id
                settingsDrawer.AnnotationsChanged();});
        };
        AskModal(std::move(spec),nullptr,[](const MD::Result&){});};
    if(!settingsDrawer.Create(instance,window,base,&settings,std::move(hooks)))Log(L"SETTINGS drawer unavailable");
}

// ---- Browsing history ----------------------------------------------------------
// The toolbar History button (beside Downloads) opens the "History" drawer on the
// right, where the download and settings drawers dock (src/history-panel.h): the pages
// of `history` (the address suggestions' own store, saved in `history` next to
// `session`), grouped by day like the download drawer, searchable as you type.
void AnnounceHistory(const std::wstring& text);
void ShowHistory(bool keyboard){
    if(!historyDrawer.Window())return;
    FocusAnchor(L"history"); // Mode Focus: the panel at rest above the drawer
    CloseAudioDrawer();HideSuggestions();CloseDownloadSurfaces();settingsDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);
    chrome.latched='Y';InvalidateChrome();historyDrawer.Show(keyboard);SyncUpdateToastReserve();
}
void ToggleHistory(bool keyboard){if(historyDrawer.Open()){historyDrawer.Hide(keyboard,true);return;}ShowHistory(keyboard);}
// Row click / Enter: the page replaces the active tab; the "+" action, Ctrl+click,
// Ctrl/Shift+Enter or a middle click open it in a new tab.
void OpenFromHistory(const std::wstring& url,bool newTab){
    if(halted||url.empty())return;
    historyDrawer.Hide(false,true);Log(L"HISTORY open "+url+(newTab?L" (new tab)":L""));
    auto t=Active();
    if(newTab||!t||!t->view.p){if(pages.size()<128)NewTab(url);return;}
    Navigate(t,url);FocusPage();
}
void RemoveFromHistory(const std::wstring& url){
    if(!history.Remove(url))return;
    SaveHistory();Log(L"HISTORY removed "+url);
    const auto site=AddressBar::SiteOf(url);if(!site.empty()&&!history.HasSite(site)&&!BookmarkedSite(site))ForgetSiteIcon(site);
    if(!WorkspaceParked())historyDrawer.Changed();
}
void CreateHistoryPanel(){
    NativeChrome::HistoryUI::Drawer::Hooks hooks;
    hooks.schedule=[](NativeChrome::Motion::Time now){ScheduleMotion(now);};
    hooks.open=OpenFromHistory;
    hooks.remove=RemoveFromHistory;
    hooks.closed=[](bool keyboard){SyncLatch();SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    hooks.icon=[](const std::wstring& site){return SiteIcon(site);};
    hooks.announce=AnnounceHistory;
    if(!historyDrawer.Create(instance,window,base,&history,std::move(hooks)))Log(L"HISTORY drawer unavailable");
}

void RequestFavicon(const std::shared_ptr<Tab>& tab){
    if(halted||!tab||tab->closed||!tab->faviconView.p)return;
    LPWSTR raw=nullptr;if(!Check(tab->faviconView->get_FaviconUri(&raw),L"get_FaviconUri"))return;
    std::wstring uri=raw?raw:L"";CoTaskMemFree(raw);const auto ticket=tab->favicon.Request(uri);InvalidateChrome();
    if(uri.empty())return; // Official absence signal: do not fetch or report a fault.
    std::weak_ptr<Tab> weak=tab;
    auto callback=Callback<ICoreWebView2GetFaviconCompletedHandler,HRESULT,IStream*>([weak,ticket](HRESULT hr,IStream* stream)->HRESULT{
        auto tab=weak.lock();if(halted||!tab||tab->closed||!tab->favicon.Current(ticket))return S_OK;
        if(!CheckTab(hr,L"GetFavicon callback"))return S_OK;
        // Null/empty/undecodable image streams are site image absence. WIC
        // pixels are copied while the callback stream is alive; no disk/network.
        auto image=chrome.DecodeFavicon(stream);RememberSiteIcon(tab->uri,image,stream);
        if(tab->favicon.Complete(ticket,image))InvalidateChrome();
        return S_OK;
    });
    HRESULT hr=tab->faviconView->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,callback);callback->Release();CheckTab(hr,L"GetFavicon");
}

// Network failures stay inside the WebView: its built-in error page provides
// details and retry, while the address bar, other tabs and commands remain usable.
#ifdef BROWSER_E2E_FIXTURE_HOOK
// Review P-L2 (coordinator msg_0114e68f7b35): the TEST HOST talks only to the manifest's exact origin.
// Every top-level/frame navigation (redirects included) and every network request elsewhere is refused
// BEFORE it is sent; only the initial local documents (about:, data:) are allowed. A guard that cannot be
// installed halts the host (fail closed).
// Fail closed (coordinator msg_9e6cd0db778d): the host stops at once (every controller closed, process
// exits with code 41) rather than ever announcing a refusal it could not enforce. The fixture origin is
// reachable only after ClearServerCertificateErrorActions COMPLETED successfully.
bool e2eCleared=false;
bool E2EInjected(const wchar_t* name){wchar_t v[8]{};return GetEnvironmentVariableW(name,v,8)&&v[0]==L'1';} // test host only: failure injection
[[noreturn]] void E2EHostAbort(const std::wstring& why){
    Log(L"E2E BOUND ABORT "+why);
    // Terminate at once from inside the engine callback: the callback never returns, so the paused
    // request is never released to the network; the engine closes the orphaned webview. (Closing the
    // controller re-entrantly from the callback is not safe.)
    TerminateProcess(GetCurrentProcess(),41);
    for(;;)Sleep(INFINITE);
}
bool E2EAllowedUri(const std::wstring& u){
    if(u.rfind(L"about:",0)==0||u.rfind(L"data:",0)==0)return true;
    if(!e2eCleared)return false;   // nothing reaches the fixture before the cache clear completed
    wchar_t path[1024]{};if(!GetEnvironmentVariableW(L"BROWSER_E2E_PIN_MANIFEST",path,1024))return false;
    const auto m=E2EFixturePin::Load(path);if(!m.loaded)return false;
    auto at=[&](int port){if(!port)return false;const std::wstring o=L"https://"+m.host+L":"+std::to_wstring(port);return u==o||u.rfind(o+L"/",0)==0||u.rfind(o+L"?",0)==0||u.rfind(o+L"#",0)==0;};
    return at(m.port)||at(m.port2);   // port2: the optional second fixture origin (vault coverage, cross-origin frame)
}
bool E2EFixtureAllows(ICoreWebView2ServerCertificateErrorDetectedEventArgs* args){
    wchar_t path[1024]{};if(!GetEnvironmentVariableW(L"BROWSER_E2E_PIN_MANIFEST",path,1024))return false;
    const auto m=E2EFixturePin::Load(path);
    COREWEBVIEW2_WEB_ERROR_STATUS st{};LPWSTR uri=nullptr;std::wstring u;
    if(FAILED(args->get_ErrorStatus(&st))||FAILED(args->get_RequestUri(&uri))||!uri)return false;u=uri;CoTaskMemFree(uri);
    Com<ICoreWebView2Certificate> c;if(FAILED(args->get_ServerCertificate(&c.p))||!c.p)return false;
    LPWSTR pem=nullptr;double from=0,to=0;if(FAILED(c->ToPemEncoding(&pem))||!pem)return false;std::wstring p(pem);CoTaskMemFree(pem);
    if(FAILED(c->get_ValidFrom(&from))||FAILED(c->get_ValidTo(&to)))return false;
    std::string der;if(!E2EFixturePin::DerFromPem(p,der))return false;
    FILETIME ft;GetSystemTimeAsFileTime(&ft);const double now=((((ULONGLONG)ft.dwHighDateTime)<<32|ft.dwLowDateTime)/10000000.0)-11644473600.0;
    std::wstring why;const bool ok=E2EFixturePin::Decide(m,u,(int)st,der,from,to,now,&why)==E2EFixturePin::Verdict::Allow;
    if(!ok)Log(L"E2E FIXTURE PIN refused: "+why);
    return ok;
}
#endif
// ---- A navigation the engine turned into a download (src/download-navigation.h) -------------------
// Its NavigationCompleted failure (CONNECTION_ABORTED / OPERATION_CANCELED) and its DownloadStarting come in
// either order: a possible download's failure is held DownloadNavigation::WaitMs, then shows its error page
// only if no download of that navigation came.
std::map<UINT_PTR,std::function<void()>> delayedJobs;
void CALLBACK RunDelayedJob(HWND,UINT,UINT_PTR id,DWORD){KillTimer(nullptr,id);auto it=delayedJobs.find(id);if(it==delayedJobs.end())return;auto job=std::move(it->second);delayedJobs.erase(it);if(!halted)job();}
// Defer() after `ms` (a thread timer, no window needed), in the caller's workspace.
void DeferAfter(UINT ms,std::function<void()> job){job=[owner=WorkspaceNow(),job=std::move(job)]{if(WorkspaceDead(owner))return;WorkspaceScope scope(owner);job();};const UINT_PTR id=SetTimer(nullptr,0,ms,RunDelayedJob);if(id)delayedJobs[id]=std::move(job);else Defer(std::move(job));}
// The tab's navigation became a download: no error page; the tab keeps the document it shows and its address
// (Navigate() had already put the typed address in t->uri: the shown document's own address comes back). A tab
// that never showed anything (a link opened in a new tab, target=_blank, window.open) existed only for that
// download: it closes, like Chrome (no « recently closed » entry, never the window; the last tab leaves a home tab).
void NavigationBecameDownload(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed)return;
    if(t->https.upgrade&&t->https.upgrade==t->downloadNav.id)t->https.upgrade=0;
    Log(t->downloadNav.asked?L"NAVIGATION waits for the 'download multiple files' question: no error page, the tab keeps its page":L"NAVIGATION became a download: no error page, the tab keeps its page");
    if(!t->everShown&&!t->downloadNav.asked){ /* (the question's chip belongs to that tab: it stays while it asks) */std::weak_ptr<Tab> weak=t;Defer([weak]{auto tab=weak.lock();if(!tab||tab->closed)return;const int i=TabIndexOf(tab);if(i<0)return;Log(L"DOWNLOAD tab opened only for the download: closed");CloseAt(i,CloseReason::Placeholder);});return;}
    LPWSTR s=nullptr;if(t->view.p&&SUCCEEDED(t->view->get_Source(&s))&&s){const std::wstring shown=s;CoTaskMemFree(s);const bool hostPage=shown==L"about:blank"||shown.rfind(L"data:",0)==0;
        if(!hostPage){t->uri=shown;t->home=false;}
        else if(t->uri==t->navigateTo){t->uri=t->navigateFrom;t->home=t->navigateFromHome;}}   // our own document (home, error page, interstitial): the address it had
    if(t==Active())Update();
}
// DownloadStarting in this tab: settles the tab's held navigation failure when it is that navigation's download.
void DownloadNavigationStarted(const std::shared_ptr<Tab>& t,ICoreWebView2DownloadStartingEventArgs* args){
    if(halted||!t||t->closed||!args)return;
    Com<ICoreWebView2DownloadOperation> op;LPWSTR text=nullptr;
    if(FAILED(args->get_DownloadOperation(&op.p))||!op.p||FAILED(op->get_Uri(&text))||!text)return;
    const std::wstring uri=text;CoTaskMemFree(text);
    if(t->downloadNav.DownloadStarted(uri,GetTickCount64()))NavigationBecameDownload(t);
}
// PermissionRequested MULTIPLE_AUTOMATIC_DOWNLOADS: the tab's navigation waits for the answer, it is a download.
void DownloadNavigationAsked(const std::shared_ptr<Tab>& t,const std::wstring& uri){
    if(halted||!t||t->closed)return;
    if(t->downloadNav.DownloadAsked(uri,GetTickCount64()))NavigationBecameDownload(t);
}
// A failed top-level navigation that is not a download: the error page, the HTTPS-only interstitial, the site's own
// HTTP error page or the engine's (unchanged behaviour).
HRESULT NavigationFailed(const std::shared_ptr<Tab>& t,UINT64 navigationId,COREWEBVIEW2_WEB_ERROR_STATUS error,int httpStatus){
    if(halted||!t||t->closed)return S_OK;
    {
        if(error==COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)return S_OK;
        if(HttpsUpgradeFailed(t,navigationId,error))return S_OK;
        Log(L"NAVIGATION FAILED engine WebErrorStatus="+std::to_wstring(error)+L" HttpStatusCode="+std::to_wstring(httpStatus)+(navigationId==t->errorPage.startingId?L" latest":L" older"));   // engine facts only (no URL, no body)
        // An HTTP 4xx/5xx answer (e.g. a 403 challenge page) IS the site's page: shown as received, never replaced.
        // Only for the tab's latest top-level navigation, never when WE refused this page's own certificate
        // (a refused sub-resource certificate does not count), no forced error page.
        const bool ownCertificateRefused=t->errorPage.certStatus>=0&&NativeChrome::ErrorPages::CertificateMatches(t->errorPage.certStatus,t->errorPage.certUri,t->errorPage.starting);
        if(navigationId==t->errorPage.startingId&&!ownCertificateRefused&&t->errorPage.forced<0&&NativeChrome::ErrorPages::KeepsServerPage(false,(int)error,httpStatus)){
            Log(L"NAVIGATION HTTP "+std::to_wstring(httpStatus)+L" (the site's own page) "+t->errorPage.starting);
            // A 4xx/5xx with NO body shows the engine's own error document (chrome-error:, which no site page can
            // be); a 4xx/5xx CUT after its headers reports the very same engine values (UNKNOWN + 403) but leaves
            // no document at all (no documentElement, still « loading »; measured, observe_cut_facts_*.txt). Both
            // are replaced by our error page as before, still only for this very navigation. A complete answer
            // always has its document when NavigationCompleted fires.
            {std::weak_ptr<Tab> weak=t;const auto failed=error;
             auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([weak,navigationId,failed](HRESULT hr,LPCWSTR json)->HRESULT{
                auto tab=weak.lock();if(halted||!tab||tab->closed)return S_OK;
                if(FAILED(hr)||!json){Log(L"NAVIGATION HTTP error: page check failed hr="+std::to_wstring((long)hr)+L" (the received page stays)");return S_OK;}   // review: a late failure is said
                const std::wstring verdict=json;if(verdict!=L"\"engine-error\""&&verdict!=L"\"no-document\"")return S_OK;
                if(tab->errorPage.startingId!=navigationId||!tab->errorPage.shown.empty())return S_OK;
                Log(L"NAVIGATION HTTP error without a page of its own ("+verdict+L"): our error page");
                if(ShowErrorPage(tab,failed,navigationId)){tab->revealed=true;ApplyPages();if(tab==Active())Update();}
                return S_OK;});
             if(FAILED(t->view->ExecuteScript(L"location.protocol==='chrome-error:'?'engine-error':(document.documentElement?'page':'no-document')",cb)))Log(L"NAVIGATION HTTP error: page check unavailable");cb->Release();}
            RequestFavicon(t);if(halted)return S_OK;
            t->revealed=true;ApplyPages();if(t==Active())Update();
            return S_OK;
        }
        if(ShowErrorPage(t,error,navigationId)){t->revealed=true;ApplyPages();if(t==Active())Update();return S_OK;}
        Log(L"NAVIGATION FAILED WebErrorStatus="+std::to_wstring(error)+L" "+t->uri);
        // A new controller starts hidden until its first navigation completes.
        // Reveal the error document too, including a failed first address.
        t->revealed=true;ApplyPages();if(t==Active())Update();
        return S_OK;
    }
}
// The failure of a possible download (either order with DownloadStarting): settled now, or held.
HRESULT DownloadNavigationFailed(const std::shared_ptr<Tab>& t,UINT64 navigationId,COREWEBVIEW2_WEB_ERROR_STATUS error,int httpStatus){
    namespace DN=NativeChrome::DownloadNavigation;
    switch(t->downloadNav.Failed(navigationId,(int)error,httpStatus,GetTickCount64())){
    case DN::Verdict::Download:NavigationBecameDownload(t);return S_OK;
    case DN::Verdict::Wait:{
        Log(L"NAVIGATION FAILED engine WebErrorStatus="+std::to_wstring(error)+L" HttpStatusCode="+std::to_wstring(httpStatus)+L": held for a possible download");
        std::weak_ptr<Tab> weak=t;
        DeferAfter((UINT)DN::WaitMs,[weak,navigationId,error,httpStatus]{auto tab=weak.lock();if(halted||!tab||tab->closed||!tab->downloadNav.Expire(navigationId))return;Log(L"NAVIGATION no download came: the failure stands");NavigationFailed(tab,navigationId,error,httpStatus);});
        return S_OK;}
    default:return NavigationFailed(t,navigationId,error,httpStatus);
    }
}
HRESULT NavigationCompleted(const std::shared_ptr<Tab>& t,ICoreWebView2NavigationCompletedEventArgs* a){
    if(halted||!t||t->closed)return S_OK;
    BOOL ok=FALSE;a->get_IsSuccess(&ok);
    ElementsNavigationCompleted(t,ok!=FALSE);ChatNavigationCompleted(t);
    UINT64 navigationId=0;
    if(!Check(a->get_NavigationId(&navigationId),L"NavigationCompleted id"))return S_OK;
    t->loading.Finish(navigationId,NativeChrome::Motion::Now());t->refreshing=false;
    if(t->navInFlight==navigationId)t->navInFlight=0;VaultNavigationCompleted(t,navigationId,ok!=FALSE);
    if(t==Active()){chrome.lottie.RefreshEnd();RefreshTools();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}
    if(!ok){
        COREWEBVIEW2_WEB_ERROR_STATUS error;a->get_WebErrorStatus(&error);

        // Our CANCEL of a refused server certificate may surface as « cancelled »: the recorded status wins.
        // (WebView2 reports the cancelled request as « connection aborted/reset » or « cancelled »), only for
        // this navigation and only when the refused request is the navigation's own URL.
        if(t->errorPage.certStatus>=0&&navigationId==t->errorPage.startingId&&NativeChrome::ErrorPages::CertificateMatches(t->errorPage.certStatus,t->errorPage.certUri,t->errorPage.starting))error=(COREWEBVIEW2_WEB_ERROR_STATUS)t->errorPage.certStatus;
        int httpStatus=0;{Com<ICoreWebView2NavigationCompletedEventArgs2> a2;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2NavigationCompletedEventArgs2,(void**)&a2.p))&&a2.p&&FAILED(a2->get_HttpStatusCode(&httpStatus)))httpStatus=0;}   // 0: older runtime / no response
        return DownloadNavigationFailed(t,navigationId,error,httpStatus);   // a navigation that became a download never reaches NavigationFailed
    }
    if(t->home||t->uri!=L"about:blank")t->everShown=true;
    t->downloadNav.Shown();
    RequestFavicon(t);if(halted)return S_OK;
    Log(L"NAVIGATION OK "+t->uri);
    if(!t->home&&t->https.shown.empty()&&t->errorPage.shown.empty()&&!IsSandboxUri(t->uri)&&!SearchResultPage(t->uri)&&history.Record(t->uri,t->title==TR(tab_new)?L"":t->title,NativeChrome::DownloadsUI::UnixNowMs())){SaveHistory();if(!WorkspaceParked())historyDrawer.Changed();}
    t->revealed=true;ApplyPages();if(t==Active())Update();
    return S_OK;
}

HRESULT ProcessFailed(const std::shared_ptr<Tab>& t,ICoreWebView2ProcessFailedEventArgs* a){
    if(halted||!t||t->closed)return S_OK;
    COREWEBVIEW2_PROCESS_FAILED_KIND kind;
    if(!Check(a->get_ProcessFailedKind(&kind),L"ProcessFailed kind"))return S_OK;
    if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED){
        Fail(L"ProcessFailed kind="+std::to_wstring(kind),E_FAIL);return S_OK;
    }
    Log(L"TAB PROCESS FAILED kind="+std::to_wstring(kind)+L" "+t->uri);
    if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED){
        // WebView2 replaces the crashed document with its error page. Keep the
        // existing URL/history so Reload (or F5) can recover just this tab.
        t->loading.Clear();t->refreshing=false;ElementsTabClosed(t);
        if(t==Active()){chrome.lottie.RefreshEnd();RefreshTools();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}
    }
    // Subframe and auxiliary process failures recover within WebView2.
    return S_OK;
}

#ifdef BROWSER_E2E_FIXTURE_HOOK
void RegisterE2EDestinationGuards(const std::shared_ptr<Tab>& t){
        {   // test host only: bound every destination to the manifest's exact origin, fail closed
            bool guarded=true;EventRegistrationToken tk{};
            auto nav=Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* a)->HRESULT{LPWSTR u=nullptr;if(SUCCEEDED(a->get_Uri(&u))&&u){std::wstring s(u);CoTaskMemFree(u);if(!E2EAllowedUri(s)){a->put_Cancel(TRUE);Log(L"E2E BOUND refused navigation "+s);}}else a->put_Cancel(TRUE);return S_OK;});
            guarded=guarded&&SUCCEEDED(t->view->add_NavigationStarting(nav,&tk))&&SUCCEEDED(t->view->add_FrameNavigationStarting(nav,&tk));nav->Release();
            guarded=guarded&&SUCCEEDED(t->view->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL));
            {Com<ICoreWebView2_22> v22;guarded=guarded&&SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_22,(void**)&v22.p))&&v22.p   // review P-L2: service-worker and shared-worker requests too
                &&SUCCEEDED(v22->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL));}
            auto res=Callback<ICoreWebView2WebResourceRequestedEventHandler,ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*>([](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* a)->HRESULT{
                Com<ICoreWebView2WebResourceRequest> rq;LPWSTR u=nullptr;std::wstring s;if(SUCCEEDED(a->get_Request(&rq.p))&&rq.p&&SUCCEEDED(rq->get_Uri(&u))&&u){s=u;CoTaskMemFree(u);}
                if(!E2EAllowedUri(s)){
                    Com<ICoreWebView2WebResourceResponse> r;HRESULT hr=E2EInjected(L"BROWSER_E2E_INJECT_RESPONSE_FAILURE")?E_FAIL:environment->CreateWebResourceResponse(nullptr,403,L"E2E bound",L"",&r.p);
                    if(SUCCEEDED(hr)&&r.p)hr=a->put_Response(r.p);else if(SUCCEEDED(hr))hr=E_POINTER;
                    if(FAILED(hr))E2EHostAbort(L"refusal response not installed for "+s);   // never claims a refusal that is not in place
                    Log(L"E2E BOUND refused request "+s);}
                return S_OK;});
            guarded=guarded&&SUCCEEDED(t->view->add_WebResourceRequested(res,&tk));res->Release();
            Com<ICoreWebView2_14> v14;guarded=guarded&&SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_14,(void**)&v14.p))&&v14.p;
            if(guarded){auto cleared=Callback<ICoreWebView2ClearServerCertificateErrorActionsCompletedHandler,HRESULT>([](HRESULT hr)->HRESULT{
                    if(E2EInjected(L"BROWSER_E2E_INJECT_CLEAR_FAILURE"))hr=E_FAIL;
                    if(FAILED(hr))E2EHostAbort(L"certificate decisions not cleared");
                    e2eCleared=true;Log(L"E2E BOUND certificate decisions cleared");return S_OK;});
                guarded=SUCCEEDED(v14->ClearServerCertificateErrorActions(cleared));cleared->Release();}
            if(!guarded)E2EHostAbort(L"destination guards unavailable");
        }
}
#endif
// ---- window.open targets (design/popup-window-target-report.md) --------------------------------------------
// --workspace-test only (tests/popup/popup_target_e2e.py): BROWSER_POPUP_QA=script-fail|assign-fail|posthook-fail|delay
// injects a failure at that stage, or holds every request ~4 s before its assignment. Never read otherwise.
const std::wstring& PopupQa(){static const std::wstring qa=[]{if(!workspaceTest)return std::wstring();wchar_t b[64]{};const DWORD n=GetEnvironmentVariableW(L"BROWSER_POPUP_QA",b,64);return n&&n<64?std::wstring(b,n):std::wstring();}();return qa;}
std::vector<std::weak_ptr<PopupHost::Request>> popupQaHeld;void PopupAssign(const PopupHost::RequestPtr& r);
void CALLBACK PopupQaRelease(HWND,UINT,UINT_PTR id,DWORD){KillTimer(nullptr,id);auto held=std::move(popupQaHeld);popupQaHeld.clear();for(auto& w:held)if(auto r=w.lock())PopupAssign(r);}
// The engine's own request, retained until Finish: its args (put_NewWindow), its deferral, and the exact environment
// the opener ran in (the target must be created in it — same profile, same private partition).
struct PopupCom { ComRef<ICoreWebView2NewWindowRequestedEventArgs> args; DeferralGuard deferral; Com<ICoreWebView2Environment> env; };
PopupCom* PopupComOf(const PopupHost::RequestPtr& r){return r?static_cast<PopupCom*>(r->com.get()):nullptr;}
ICoreWebView2Environment* PopupEnvironment(const PopupHost::RequestPtr& r){auto* c=PopupComOf(r);return c?c->env.p:nullptr;}
// A failed/cancelled target: removed by identity (indexes move), without history and without the last-tab close.
void DropPopupTarget(unsigned serial){auto t=TabBySerial(serial);if(!t||t->closed)return;const int i=TabIndexOf(t);if(i>=0){Log(L"POPUP target dropped");CloseAt(i,CloseReason::Placeholder);}}
// Barrier reached: revalidate everything, assign the target exactly once, then the post-assignment hooks (resource
// handlers: Microsoft's contract), then complete the deferral. Any failure finishes the request as failed.
void PopupAssign(const PopupHost::RequestPtr& r){
    Workspace* owner=static_cast<Workspace*>(const_cast<void*>(r->owner));
    if(r->terminal)return;if(halted||WorkspaceDead(owner)){popups.Finish(r,false);return;}
    if(PopupQa()==L"delay"&&!r->qaHeld){r->qaHeld=true;popupQaHeld.push_back(r);SetTimer(nullptr,0,4000,PopupQaRelease);Log(L"POPUP QA held before assignment");return;}
    WorkspaceScope scope(owner);
    auto* com=PopupComOf(r);auto t=TabBySerial(r->target);auto opener=TabBySerial(r->opener);
    if(!com||!com->args.p||!t||t->closed||!t->view.p||!opener||opener->closed||environment.p!=com->env.p){Log(L"POPUP target no longer valid: refused");popups.Finish(r,false);return;}
    if(PopupQa()==L"assign-fail"||FAILED(com->args->put_NewWindow(t->view.p))){Log(L"POPUP put_NewWindow failed");popups.Finish(r,false);return;}
    r->assigned=true;
    bool hooks=RegisterSandboxResources(t)&&PopupQa()!=L"posthook-fail";
#ifdef BROWSER_E2E_FIXTURE_HOOK
    RegisterE2EDestinationGuards(t);
#endif
    if(!hooks){Log(L"POPUP post-assignment handlers failed: target closed");popups.Finish(r,false);return;}
    t->popupSetup=false;
    Log(std::wstring(L"POPUP target assigned ")+(r->blank?L"blank":L"web")+L" user="+(r->userInitiated?L"1":L"0"));
    popups.Finish(r,true);   // completes the deferral: the engine now loads the request in this target
    if(t->audioReinject){t->audioReinject=false;InjectAudioScript(t,nullptr);}
    if(r->blank&&!t->closed){t->revealed=t->everShown=true;ApplyPages();if(t==Active())Update();}   // a blank document (and what the opener writes in it) shows without an HTTP NavigationCompleted
}
void CreateTab(std::wstring uri,int index,std::wstring title,const PopupHost::RequestPtr& popup);
// NewWindowRequested: always handled by the host (never the engine's own unmanaged window). Admitted: HTTP(S) as
// before, and the exact blank document; refused otherwise (the page gets null). IsUserInitiated is recorded only.
HRESULT NewWindowRequested(const std::shared_ptr<Tab>& opener,ICoreWebView2NewWindowRequestedEventArgs* a){
    if(halted||!a)return S_OK;a->put_Handled(TRUE);
    LPWSTR raw=nullptr;if(!Check(a->get_Uri(&raw),L"NewWindow get_Uri")||!raw)return S_OK;std::wstring uri=raw;CoTaskMemFree(raw);
    BOOL user=FALSE;if(FAILED(a->get_IsUserInitiated(&user)))user=FALSE;
    if(!PopupHost::AdmitInitialUri(uri,IsSandboxUri)){Log(L"POPUP refused: address not admitted");return S_OK;}
    if(!opener||opener->closed||!environment.p)return S_OK;
    // The debug sandbox (an internal page) never gets a linked web window: its previous behaviour, an unlinked tab.
    if(opener->sandbox||IsSandboxUri(opener->uri)){if(uri!=L"about:blank")NewTab(uri);Log(L"POPUP from the debug sandbox: unlinked tab");return S_OK;}
    if(pages.size()>=128){if(WorkspaceParked())Log(L"TABS limit reached in a hidden profile");else ShowTabLimitToast(uri);return S_OK;}
    ICoreWebView2Deferral* deferral=nullptr;if(FAILED(a->GetDeferral(&deferral))||!deferral){Log(L"POPUP refused: no deferral");return S_OK;}
    auto com=std::make_shared<PopupCom>();com->deferral.deferral=deferral;com->args=ComRef<ICoreWebView2NewWindowRequestedEventArgs>(a);com->env.set(environment.p);
    auto r=std::make_shared<PopupHost::Request>();r->owner=WorkspaceNow();r->opener=opener->serial;r->userInitiated=user!=FALSE;r->blank=uri==L"about:blank";r->com=com;
    r->complete=[weakCom=std::weak_ptr<PopupCom>(com)]{if(auto c=weakCom.lock())c->deferral.Complete();};
    r->ready=[weakReq=std::weak_ptr<PopupHost::Request>(r)]{if(auto q=weakReq.lock())PopupAssign(q);};
    popups.Add(r);
    Log(std::wstring(L"POPUP request ")+(r->blank?L"blank":L"web")+L" user="+(user?L"1":L"0"));
    CreateTab(uri,-1,TR(tab_new),r);
    return S_OK;
}
void CreateTab(std::wstring uri,int index,std::wstring title,const PopupHost::RequestPtr& popup);
void NewTab(std::wstring uri,int index,std::wstring title){CreateTab(std::move(uri),index,std::move(title),nullptr);}
void CreateTab(std::wstring uri,int index,std::wstring title,const PopupHost::RequestPtr& popup){if(halted||!environment.p){if(popup)popups.Finish(popup,false);return;}const bool parked=WorkspaceParked(); /* a hidden profile's page opened a window: its tab, never the visible window's hover / toast */if(pages.size()>=128){if(popup)popups.Finish(popup,false);if(parked)Log(L"TABS limit reached in a hidden profile");else ShowTabLimitToast(uri);return;}if(!parked){motion.ClearInteractions();hover=pressed=keyboardFocus=0;}auto t=std::make_shared<Tab>();t->sandboxPending=sandboxNextTab&&IsSandboxUri(uri);if(IsSandboxUri(uri)&&!t->sandboxPending)uri.clear(); /* restored session or a page: never the sandbox */sandboxNextTab=false;t->uri=uri;t->home=uri.empty();t->everShown=t->home; /* a home tab shows something: never closed for a download */t->title=std::move(title);
    if(popup){t->home=false;t->blankIsHome=false;t->popupSetup=true;t->sandboxPending=false;popup->target=t->serial;Workspace* owner=static_cast<Workspace*>(const_cast<void*>(popup->owner));const unsigned serial=t->serial;
        popup->dropTarget=[owner,serial]{if(halted||WorkspaceDead(owner))return;WorkspaceScope scope(owner);Defer([serial]{DropPopupTarget(serial);});};}
   if(index<0)index=(int)pages.size();index=std::min(index,(int)pages.size());/* Opening a tab (« + », Ctrl+T, a link or window.open to a new tab) blurs the page on screen out and the new tab in (tab timing); a switch between existing tabs stays immediate. Not inside a profile switch or a session restore (their own transition, keepContentTransition). */const bool openTransition=!parked&&!keepContentTransition&&current>=0&&current<(int)pages.size()&&pages[current]&&!pages[current]->closed&&pages[current]->controller.p;if(openTransition)BeginContentTransition(ContentTransition::Kind::Tab); /* the outgoing page, captured before it is hidden */CancelTabDrag();pages.insert(pages.begin()+index,t);{KeepContentTransition keep;Select(index);}if(openTransition)RevealContentTransition();std::weak_ptr<Tab> weak=t;
    if(popup)unownedCallbacks=true;   // a window.open target: its completion must run (and clean up) even after its profile left
    auto done=Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>([weak,uri,popup](HRESULT hr,ICoreWebView2Controller* c)->HRESULT{
        // window.open target: validated by hand (unowned callback), then run in the opener's workspace.
        Workspace* popupOwner=popup?static_cast<Workspace*>(const_cast<void*>(popup->owner)):nullptr;
        if(popup&&(popup->terminal||WorkspaceDead(popupOwner)||halted)){if(c)c->Close();popups.Finish(popup,false);return S_OK;}
        std::optional<WorkspaceScope> popupScope;if(popup)popupScope.emplace(popupOwner);
        struct Settle{PopupHost::RequestPtr r;bool armed=true;~Settle(){if(r&&armed&&!r->terminal)popups.Finish(r,false);}} settle{popup};   // every early return settles the request
        // A tab closed before its controller arrived (e.g. reopened then closed, or the
        // window closing) may complete with E_ABORT: that is not an engine failure.
        auto t=weak.lock();if(!t||t->closed){if(c)c->Close();return S_OK;}if(workspaceTest&&qaControllerFailure&&SUCCEEDED(hr)&&WorkspaceCanFailAlone()){qaControllerFailure=false;if(c)c->Close();c=nullptr;hr=HRESULT_FROM_WIN32(ERROR_INVALID_STATE);Log(L"WSQA controller failure injected");} /* --workspace-test only */if(FAILED(hr)&&WorkspaceEngineFailed(L"CreateController callback",hr))return S_OK; /* a profile started in this window: that profile fails, not the window */if(!Check(hr,L"CreateController callback"))return S_OK;if(!c){Fail(L"CreateController null",E_POINTER);return S_OK;}t->controller.set(c);if(!Check(c->get_CoreWebView2(&t->view.p),L"get_CoreWebView2"))return S_OK;
        Com<ICoreWebView2Settings> settings;Com<ICoreWebView2Settings3> settings3;
        if(!Check(t->view->get_Settings(&settings.p),L"get_Settings")||
           !Check(settings->QueryInterface(IID_ICoreWebView2Settings3,(void**)&settings3.p),L"QueryInterface Settings3")||
           !Check(settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE),L"Enable browser accelerators"))return S_OK;
        // Link preview: our own bubble (status-bubble.h) replaces the engine's status bar.
        {Com<ICoreWebView2_12> view12;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_12,(void**)&view12.p))&&view12.p){
            auto status=Callback<ICoreWebView2StatusBarTextChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;Com<ICoreWebView2_12> v12;LPWSTR text=nullptr;
                if(v&&SUCCEEDED(v->QueryInterface(IID_ICoreWebView2_12,(void**)&v12.p))&&v12.p&&SUCCEEDED(v12->get_StatusBarText(&text))){SetTabStatus(tab,text?text:L"");CoTaskMemFree(text);}return S_OK;});
            EventRegistrationToken statusToken{};
            if(SUCCEEDED(view12->add_StatusBarTextChanged(status,&statusToken))){if(FAILED(settings->put_IsStatusBarEnabled(FALSE)))Log(L"STATUS put_IsStatusBarEnabled failed: engine bubble kept");}
            else Log(L"STATUS add_StatusBarTextChanged failed: engine bubble kept");status->Release();}
         else Log(L"STATUS ICoreWebView2_12 unavailable: engine bubble kept");}
        EventRegistrationToken token{};
        // Overlay scrollbar (Paper V3): the page script hides the root scrollbar and posts its
        // metrics; PageMessage hands them to ScrollbarMessageFrom.
        {auto* barrier=PopupScriptBarrier(popup,L"scrollbar");HRESULT added=t->view->AddScriptToExecuteOnDocumentCreated(ScrollbarScript,barrier);if(barrier){if(FAILED(added))popups.Finish(popup,false);barrier->Release();}Check(added,L"AddScriptToExecuteOnDocumentCreated scrollbar");}
        IID faviconInterface;CLSIDFromString(L"{517B2D1D-7DAE-4A66-A4F4-10352FFB9518}",&faviconInterface);
        if(!Check(t->view->QueryInterface(faviconInterface,(void**)&t->faviconView.p),L"QueryInterface favicon"))return S_OK;
        auto faviconChanged=Callback<ICoreWebView2FaviconChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted)RequestFavicon(tab);return S_OK;});
        HRESULT faviconResult=t->faviconView->add_FaviconChanged(faviconChanged,&token);faviconChanged->Release();if(!Check(faviconResult,L"add_FaviconChanged"))return S_OK;
        auto starting=Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;tab->certificateError=false;AnnotationNavigation(tab);++tab->navStamp;SetTabStatus(tab,L"");LPWSTR uri=nullptr;if(!Check(args->get_Uri(&uri),L"NavigationStarting get_Uri"))return S_OK;UINT64 navigationId=0;if(!Check(args->get_NavigationId(&navigationId),L"NavigationStarting id")){CoTaskMemFree(uri);return S_OK;}if(SandboxNavigationStarting(tab,args,uri?uri:L"")){CoTaskMemFree(uri);return S_OK;}ElementsNavigationStarting(tab);VaultNavigationStarting(tab,navigationId,uri?uri:L"");tab->navInFlight=navigationId;ErrorPageNavigationStarting(tab,uri?uri:L"",navigationId);tab->downloadNav.Starting(navigationId,uri?uri:L"",GetTickCount64());if(HttpsNavigationStarting(tab,args,uri?uri:L"",navigationId)){CoTaskMemFree(uri);return S_OK;}if(uri&&wcscmp(uri,L"about:blank")!=0)tab->loading.Start(navigationId,NativeChrome::Motion::Now());else tab->loading.Clear();if(tab==Active()){RefreshTools();ScheduleMotion(NativeChrome::Motion::Now());}tab->favicon.Navigation(uri?uri:L"");CoTaskMemFree(uri);InvalidateChrome();return S_OK;});
        HRESULT startingResult=t->view->add_NavigationStarting(starting,&token);starting->Release();if(!Check(startingResult,L"add_NavigationStarting favicon"))return S_OK;
        auto source=Callback<ICoreWebView2SourceChangedEventHandler,ICoreWebView2*,ICoreWebView2SourceChangedEventArgs*>([weak](ICoreWebView2* v,ICoreWebView2SourceChangedEventArgs*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;AnnotationNavigation(t);++t->navStamp;if(Check(v->get_Source(&s),L"get_Source")&&s){t->uri=s;CoTaskMemFree(s);/* our own pages (error, HTTPS-only) report about:blank or data: in EVERY tab, a script-opened one included: the real address comes back regardless of blankIsHome, which only decides « home » */const bool hostPage=t->uri==L"about:blank"||t->uri.rfind(L"data:",0)==0;t->home=t->uri==L"about:blank"&&t->blankIsHome;if(!t->https.shown.empty()&&hostPage){t->uri=t->https.shown;t->home=false;}if(!t->errorPage.shown.empty()&&hostPage){t->uri=t->errorPage.shown;t->home=false;}}t->loading.Committed(NativeChrome::Motion::Now());AudioSiteChanged(t);if(t==Active()){Update();ScheduleMotion(NativeChrome::Motion::Now());}return S_OK;});
        HRESULT result=t->view->add_SourceChanged(source,&token);source->Release();if(!Check(result,L"add_SourceChanged"))return S_OK;
        auto history=Callback<ICoreWebView2HistoryChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted&&tab==Active())Update();return S_OK;});
        result=t->view->add_HistoryChanged(history,&token);history->Release();if(!Check(result,L"add_HistoryChanged"))return S_OK;
        auto title=Callback<ICoreWebView2DocumentTitleChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_DocumentTitle(&s),L"get_DocumentTitle")&&s){t->title=s;CoTaskMemFree(s);if(!t->home&&t->https.shown.empty()&&t->errorPage.shown.empty()&&!IsSandboxUri(t->uri)&&::history.SetTitle(t->uri,t->title)){SaveHistory();if(!WorkspaceParked())historyDrawer.Changed();}}
            /* PERF-07: only the active tab's title reaches the window title and the toolbar; another tab's only changes the strip */if(t==Active())Update();else if(!WorkspaceParked()){RefreshTools();InvalidateChrome();}SchedulePublishProfileWindow();return S_OK;});
        result=t->view->add_DocumentTitleChanged(title,&token);title->Release();if(!Check(result,L"add_DocumentTitleChanged"))return S_OK;
        auto nav=Callback<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* a)->HRESULT{return NavigationCompleted(weak.lock(),a);});
        result=t->view->add_NavigationCompleted(nav,&token);nav->Release();if(!Check(result,L"add_NavigationCompleted"))return S_OK;
        auto opened=Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{return NewWindowRequested(weak.lock(),a);});
        result=t->view->add_NewWindowRequested(opened,&token);opened->Release();if(!Check(result,L"add_NewWindowRequested"))return S_OK;
        if(popup){   // window.close() of a script-opened page: only that tab, deferred (never inside the engine's event)
            auto closing=Callback<ICoreWebView2WindowCloseRequestedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted||tab->scriptCloseQueued)return S_OK;tab->scriptCloseQueued=true;Defer([weak]{auto t2=weak.lock();if(!t2||t2->closed)return;const int i=TabIndexOf(t2);if(i>=0){Log(L"POPUP closed by its page");CloseAt(i,CloseReason::Script);}});return S_OK;});
            result=t->view->add_WindowCloseRequested(closing,&token);closing->Release();if(FAILED(result)){Log(L"POPUP add_WindowCloseRequested failed: target refused");return S_OK;}}
        auto key=Callback<ICoreWebView2AcceleratorKeyPressedEventHandler,ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs*>([](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* a)->HRESULT{if(halted)return S_OK;COREWEBVIEW2_KEY_EVENT_KIND kind;a->get_KeyEventKind(&kind);UINT vk;a->get_VirtualKey(&vk);if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)ContentTransitionInput(L"key");if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN){int cmd=Key(vk);if(cmd){a->put_Handled(TRUE);PostMessageW(window,Shortcut,cmd,0);}}return S_OK;});
        result=c->add_AcceleratorKeyPressed(key,&token);key->Release();if(!Check(result,L"add_AcceleratorKeyPressed"))return S_OK;
        auto gotFocus=Callback<ICoreWebView2FocusChangedEventHandler,ICoreWebView2Controller*,IUnknown*>([weak](ICoreWebView2Controller*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted)ActivatePanel(tab);return S_OK;});
        result=c->add_GotFocus(gotFocus,&token);gotFocus->Release();if(!Check(result,L"add_GotFocus"))return S_OK;
        auto failed=Callback<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* a)->HRESULT{return ProcessFailed(weak.lock(),a);});
        result=t->view->add_ProcessFailed(failed,&token);failed->Release();if(!Check(result,L"add_ProcessFailed"))return S_OK;
        // SETTINGS(https): buttons of the "Ce site n'est pas sécurisé" page.
        {auto message=Callback<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* a)->HRESULT{return PageMessage(weak.lock(),a);});if(FAILED(t->view->add_WebMessageReceived(message,&token)))Log(L"HTTPS add_WebMessageReceived failed");message->Release();}
        {bool required=true;RegisterElementEvents(t,settings.p,popup,&required);RegisterAudio(t,popup);if(popup&&!required){Log(L"POPUP security handlers incomplete: target refused");return S_OK;}}
        // Invalid server certificate: the engine keeps its own interstitial; the bar says "Non sécurisé".
        {Com<ICoreWebView2_14> view14;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_14,(void**)&view14.p))&&view14.p){auto cert=Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler,ICoreWebView2*,ICoreWebView2ServerCertificateErrorDetectedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ServerCertificateErrorDetectedEventArgs* args)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;tab->certificateError=true;Log(L"CERTIFICATE error "+tab->uri);
            // Never ALWAYS_ALLOW (no bypass): CANCEL refuses the connection; our page replaces the engine's
            // interstitial only for the top-level navigation (ShowErrorPage checks the navigation id and URL).
#ifdef BROWSER_E2E_FIXTURE_HOOK
            if(args&&E2EFixtureAllows(args)){args->put_Action(COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW);Log(L"E2E FIXTURE PIN allowed (test host only)");return S_OK;}
#endif
            if(args){args->put_Action(COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);COREWEBVIEW2_WEB_ERROR_STATUS st{};LPWSTR u=nullptr;
                if(SUCCEEDED(args->get_ErrorStatus(&st)))tab->errorPage.certStatus=(int)st;if(SUCCEEDED(args->get_RequestUri(&u))&&u){tab->errorPage.certUri=u;CoTaskMemFree(u);}
                Com<ICoreWebView2Certificate> c;LPWSTR issuer=nullptr;tab->errorPage.certIssuer.clear();if(SUCCEEDED(args->get_ServerCertificate(&c.p))&&c.p&&SUCCEEDED(c->get_Issuer(&issuer))&&issuer){tab->errorPage.certIssuer=issuer;CoTaskMemFree(issuer);}}if(tab==Active()){BuildChrome();RefreshTools();InvalidateChrome();}return S_OK;});if(FAILED(view14->add_ServerCertificateErrorDetected(cert,&token)))Log(L"CERTIFICATE add_ServerCertificateErrorDetected failed");cert->Release();}else Log(L"CERTIFICATE ICoreWebView2_14 unavailable");}
        {Com<ICoreWebView2_4> view4;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_4,(void**)&view4.p))&&view4.p){auto download=Callback<ICoreWebView2DownloadStartingEventHandler,ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* a)->HRESULT{auto tab=weak.lock();if(!halted)DownloadNavigationStarted(tab,a);return DownloadStarting(a,tab?tab->serial:0);});if(FAILED(view4->add_DownloadStarting(download,&token)))Log(L"DOWNLOADS add_DownloadStarting failed");download->Release();}else Log(L"DOWNLOADS ICoreWebView2_4 unavailable: engine flyout kept");ApplyDownloadSettings();ApplyPrivacySettings();ApplyZoom(c);ApplySleepSettings();ApplyTabSettings();ApplyMemorySaver();if(!adBlockSynced)ApplyAdBlock();}
        {Com<ICoreWebView2_11> view11;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_11,(void**)&view11.p))&&view11.p){auto menu=Callback<ICoreWebView2ContextMenuRequestedEventHandler,ICoreWebView2*,ICoreWebView2ContextMenuRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ContextMenuRequestedEventArgs* a)->HRESULT{return PageContextMenu(weak.lock(),a);});if(FAILED(view11->add_ContextMenuRequested(menu,&token)))Log(L"MENU add_ContextMenuRequested failed");menu->Release();}else Log(L"MENU ICoreWebView2_11 unavailable: engine menu kept");}
#ifdef BROWSER_E2E_FIXTURE_HOOK
        if(!popup)RegisterE2EDestinationGuards(t);   // a window.open target: after its assignment (PopupAssign)
#endif
        Layout();if(!Check(c->put_IsVisible(FALSE),L"put_IsVisible initial"))return S_OK;t->placedVisible=0;
        if(popup){settle.armed=false;popups.Step(popup,true);return S_OK;}   // the engine navigates the target itself: never Navigate/NavigateToString here
        Navigate(t,uri);return S_OK;
    });
    if(popup){unownedCallbacks=false;PopupHost::Registry::Arm(popup,1);}   // the controller setup itself
    HRESULT hr=E_FAIL;ICoreWebView2Environment* creating=popup?PopupEnvironment(popup):environment.p;
    if(!creating){done->Release();if(popup)popups.Finish(popup,false);return;}
    if(privateWindow){
        // InPrivate controller on a dedicated profile name: never the live profile under a private label.
        Com<ICoreWebView2Environment10> env10;Com<ICoreWebView2ControllerOptions> options;
        if(SUCCEEDED(creating->QueryInterface(IID_ICoreWebView2Environment10,(void**)&env10.p))&&env10.p&&SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&options.p))&&options.p
           &&SUCCEEDED(options->put_IsInPrivateModeEnabled(TRUE))&&SUCCEEDED(options->put_ProfileName(PrivateProfileName)))
            hr=env10->CreateCoreWebView2ControllerWithOptions(window,options.p,done);
        else{if(popup)popups.Finish(popup,false);Fail(L"InPrivate controller unavailable: no private window without it",E_NOINTERFACE);done->Release();return;}
    }else hr=creating->CreateCoreWebView2Controller(window,done);
    done->Release();if(FAILED(hr)&&popup)popups.Finish(popup,false);Check(hr,L"CreateCoreWebView2Controller");
}

// i18n-exempt-begin: smoke / split / menu QA harness (--smoke-test, --split-test): assertions, fixtures, log text
void SmokeCheck(){if(!smoke||halted)return;auto t=Active();if(!t||!t->view.p){Fail(L"Smoke active view missing",E_FAIL);return;}auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([](HRESULT hr,LPCWSTR json)->HRESULT{if(!Check(hr,L"Smoke ExecuteScript callback"))return S_OK;std::wstring expected=testStage<2?L"Nouvel onglet|rgb(23, 23, 23)|":L"Example Domain";if(!json||std::wstring(json).find(expected)==std::wstring::npos){Fail(L"Smoke unexpected document title",E_FAIL);return S_OK;}Log(L"SMOKE document verified stage="+std::to_wstring(testStage)+L" title="+json);PostMessageW(window,WM_APP+3,0,0);return S_OK;});HRESULT hr=t->view->ExecuteScript(testStage<2?L"document.title + '|' + getComputedStyle(document.body).backgroundColor + '|' + document.body.textContent.trim()":L"document.title",cb);cb->Release();Check(hr,L"Smoke ExecuteScript");}
void SmokeAdvance(){if(!smoke||halted)return; /* SEC-26: another process posting WM_APP+3 never steps the test script */switch(testStage++){
case 0:Command('T');break;
case 1:Navigate(Active(),L"https://example.com/");break;
case 2:Navigate(Active(),L"https://example.org/");break;
case 3:Command('B');break;
case 4:Command('F');break;
case 5:Command('R');break;
case 6:{Command('Y');if(halted)return;if(!historyDrawer.Visible()||!IsWindowVisible(historyDrawer.Window())||chrome.latched!='Y'){Fail(L"Smoke history drawer did not open",E_FAIL);return;}
    {const auto& shown=historyDrawer.Shown();const bool org=std::any_of(shown.begin(),shown.end(),[](const NativeChrome::HistoryView::Entry& e){return e.url.find(L"example.org")!=std::wstring::npos;});
     historyDrawer.TypeForTest(L"example.org");if(!org||historyDrawer.Query()!=L"example.org"||historyDrawer.Shown().size()!=1){Fail(L"Smoke history search assertion",E_FAIL);return;}}
    historyDrawer.KeyForTest(VK_ESCAPE);if(!historyDrawer.Query().empty()||!historyDrawer.Visible()){Fail(L"Smoke history Escape must clear the search first",E_FAIL);return;}
    historyDrawer.Hide(false,false);if(historyDrawer.Visible()||IsWindowVisible(historyDrawer.Window())||chrome.latched){Fail(L"Smoke history drawer did not close",E_FAIL);return;}
    Log(L"SMOKE history drawer opened from the toolbar History button, filtered by search, Escape cleared it, closed");
    Command('D');if(halted)return;if(!downloadPanel.Visible()||!IsWindowVisible(downloadPanel.Window())){Fail(L"Smoke download history did not open",E_FAIL);return;}downloadPanel.Hide(false);if(downloadPanel.Visible()||chrome.latched){Fail(L"Smoke download history did not close",E_FAIL);return;}Log(L"SMOKE native download history opened and closed");
    Command('S');if(!settingsDrawer.Visible()||!IsWindowVisible(settingsDrawer.Window())||chrome.latched!='S'){Fail(L"Smoke settings drawer did not open",E_FAIL);return;}settingsDrawer.Hide(false,false);if(settingsDrawer.Visible()||chrome.latched){Fail(L"Smoke settings drawer did not close",E_FAIL);return;}Log(L"SMOKE settings drawer opened and closed");
    auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([](HRESULT hr,LPCWSTR)->HRESULT{Check(hr,L"Smoke download script");return S_OK;});
    HRESULT hr=Active()->view->ExecuteScript(L"(()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob(['browser smoke download'],{type:'text/plain'}));a.download='browser-smoke.txt';document.body.appendChild(a);a.click();})()",cb);cb->Release();Check(hr,L"Smoke download ExecuteScript");break;}
case 7:Command('M');if(!IsZoomed(window)){Fail(L"Smoke maximize assertion",E_FAIL);return;}Command('M');if(IsZoomed(window)){Fail(L"Smoke restore assertion",E_FAIL);return;}Log(L"SMOKE native maximize/restore verified DPI="+std::to_wstring(GetDpiForWindow(window)));auto closed=Active();int closedIndex=current;Command('W');Command('U');if(!Active()||Active()->uri!=closed->uri||current!=closedIndex||pages.size()!=2||!closedTabs.empty()){Fail(L"Smoke reopen tab assertion",E_FAIL);return;}Command('W');Log(L"SMOKE reopen closed tab at original index verified");Command('L');
    // Chrome icons and fonts load lazily on the first real paint. A smoke launched hidden (Start-Process -WindowStyle Hidden)
    // gets no WM_PAINT until maximize/restore above shows the window, and that WM_PAINT is still queued here: paint now,
    // synchronously and through the normal WM_PAINT path, so the asset check does not depend on how the window was launched.
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    {const HWND focus=GetFocus();const auto destination=Destination(L"deux mots");const bool assets=chrome.AssetsReady(),tabs=pages.size()==1,focused=focus==address,search=destination==L"https://duckduckgo.com/?q=deux%20mots";
     if(!assets||!tabs||!focused||!search){Log(L"SMOKE native controls: visible="+std::to_wstring(IsWindowVisible(window)!=0)+L" paints="+std::to_wstring(chromePaints)+L" "+chrome.AssetsReport()+L" tabs="+std::to_wstring(pages.size())+L" focusIsAddress="+std::to_wstring(focused)+L" foreground="+std::to_wstring(GetForegroundWindow()==window)+L" destination="+destination);
        Fail(!assets?L"Smoke native controls assertion: chrome assets not ready":!tabs?L"Smoke native controls assertion: tab count":!focused?L"Smoke native controls assertion: address bar not focused":L"Smoke native controls assertion: search destination",E_FAIL);return;}}{auto s=AddressBar::Suggest(L"example.c",history);if(s.rows.empty()||s.rows[0].url.rfind(L"https://example.com",0)!=0||s.completion!=L"om"){Fail(L"Smoke address history assertion",E_FAIL);return;}
    for(wchar_t c:std::wstring(L"example.c"))SendMessageW(address,WM_CHAR,c,0);
    DWORD from=0,to=0;SendMessageW(address,EM_GETSEL,(WPARAM)&from,(LPARAM)&to);
    if(!suggestions.Visible()||AddressText()!=L"example.com"||from!=9||to!=11||suggestions.Active()!=0){Fail(L"Smoke address suggestions UI assertion",E_FAIL);return;}
    SendMessageW(address,WM_KEYDOWN,VK_ESCAPE,0);if(suggestions.Visible()){Fail(L"Smoke address suggestions escape assertion",E_FAIL);return;}
    Log(L"SMOKE address suggestions: typing example.c proposes example.com with inline completion; Escape closes");}
    {auto you=AddressBar::Suggest(L"yout",AddressBar::History{},true,&popularSites);
     if(popularSites.size()<100||you.rows.empty()||you.rows[0].text!=L"youtube.com"||you.completion!=L"ube.com"||!SiteIcon(L"youtube.com")||!SiteIcon(L"arte.tv")){Fail(L"Smoke popular sites assertion",E_FAIL);return;}
     Log(L"SMOKE popular sites: "+std::to_wstring(popularSites.size())+L" bundled, yout -> youtube.com with its bundled icon (PNG and ICO decode)");}
    if(!SmokeBookmarks())return;
    Log(L"SMOKE PASS: blank #171717 home, bundled font families, 11 exported SVG icons, 2 tabs, HTTPS, document titles, back, forward, reload, close tab, address focus, search encoding");KillTimer(window,1);PostMessageW(window,WM_CLOSE,0,0);break;
}}

// ---- Native split-view verification (--split-test) ----------------------------------
// Real WebView2 pages loaded from local files only (no network), driven through the
// same window messages as a user. Checks the controllers' real bounds and visibility,
// measures innerWidth/innerHeight/devicePixelRatio inside each page (CSS px = DIP /
// zoom, devicePixelRatio = DPI / 96 x zoom) and saves screen captures as evidence in
// <exe dir>\split-qa. Writes "SPLIT PASS" to prototype.log, exits non-zero on failure.
int splitStage=0,splitWait=0; std::wstring splitQa; std::shared_ptr<Tab> splitFocus; std::pair<std::wstring,std::wstring> splitSaved; std::shared_ptr<Tab> splitNamed; std::shared_ptr<std::function<void(int)>> splitProbe;
void SplitFail(const std::wstring& what){Log(L"SPLIT FAIL "+what);Fail(L"Split test: "+what,E_FAIL);}
std::wstring SplitPage(const std::wstring& name,const wchar_t* color){
    const std::wstring path=splitQa+name+L".html";std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);
    out<<"<!doctype html><html lang=\"fr\"><meta charset=\"utf-8\"><title>"<<Utf8(name)<<"</title><style>html,body{margin:0;height:100%;font:16px system-ui;color:#111;background:"<<Utf8(color)<<"}"
         "main{padding:24px}h1{font-size:28px;margin:0 0 8px}#w{font-variant-numeric:tabular-nums}input,button{font:inherit;padding:4px 8px;color:#333}</style>"
         "<main><h1>"<<Utf8(name)<<"</h1><p id=\"w\"></p><p><input aria-label=\"Champ de test\" placeholder=\"Saisir ici\"> <button onclick=\"this.textContent=++n\">0</button></p>"
         "<p>Page locale de test du split view : aucune requête réseau.</p></main><script>let n=0;function u(){document.getElementById('w').textContent=innerWidth+' x '+innerHeight+' px CSS, devicePixelRatio '+devicePixelRatio.toFixed(2)}addEventListener('resize',u);u()</script>";
    std::wstring url=L"file:///"+path;std::replace(url.begin(),url.end(),L'\\',L'/');return url;
}
void SplitMouse(UINT msg,float x,float y){splitSynthetic=true;SendMessageW(window,msg,msg==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(S(x),S(y)));splitSynthetic=false;}
void SplitDrag(int tab,float x,float y,bool release){BuildChrome();auto s=chrome.strip.Surface(tab);SplitMouse(WM_LBUTTONDOWN,s.left+20,20);SplitMouse(WM_MOUSEMOVE,s.left+40,30);SplitMouse(WM_MOUSEMOVE,x,70);SplitMouse(WM_MOUSEMOVE,x,y);if(release)SplitMouse(WM_LBUTTONUP,x,y);}
void SplitShot(const std::wstring& name){
    // The window's own rendering (chrome + WebView2 children); during stress sweeps, the
    // real screen instead, so stale DWM surfaces would show exactly as a user sees them.
    if(!splitOnScreen)RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);DwmFlush();
    RECT r;GetWindowRect(window,&r);const int w=r.right-r.left,h=r.bottom-r.top;HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(mem,bmp);
    if(splitOnScreen)BitBlt(mem,0,0,w,h,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);
    else{PrintWindow(window,mem,PW_RENDERFULLCONTENT);POINT client{0,0};ClientToScreen(window,&client);BlendGrips(mem,{client.x-r.left,client.y-r.top});splitOverlay.BlendInto(mem,{client.x-r.left,client.y-r.top});}
    SelectObject(mem,old);DeleteDC(mem);ReleaseDC(nullptr,screen);
    IWICImagingFactory* f=nullptr;IWICBitmap* b=nullptr;IWICStream* s=nullptr;IWICBitmapEncoder* e=nullptr;IWICBitmapFrameEncode* fr=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f));
    if(SUCCEEDED(hr))hr=f->CreateBitmapFromHBITMAP(bmp,nullptr,WICBitmapIgnoreAlpha,&b);if(SUCCEEDED(hr))hr=f->CreateStream(&s);
    if(SUCCEEDED(hr))hr=s->InitializeFromFilename((splitQa+name+L".png").c_str(),GENERIC_WRITE);if(SUCCEEDED(hr))hr=f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e);
    if(SUCCEEDED(hr))hr=e->Initialize(s,WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=e->CreateNewFrame(&fr,nullptr);if(SUCCEEDED(hr))hr=fr->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=fr->WriteSource(b,nullptr);if(SUCCEEDED(hr))hr=fr->Commit();if(SUCCEEDED(hr))hr=e->Commit();
    for(IUnknown* u:std::initializer_list<IUnknown*>{fr,e,s,b,f})if(u)u->Release();DeleteObject(bmp);
    Log(L"SPLIT shot "+name+(SUCCEEDED(hr)?L".png":L" FAILED"));
}
// ---- Native QA of the tab menu's split block (V2) -------------------------------------
// The menu is modal: a thread timer drives it from inside its own loop, one step every
// 300 ms, and each capture composes the window (PrintWindow) with the menu levels, which
// are topmost popups read from the screen.
std::vector<std::function<void()>> menuScript;size_t menuStep=0;bool menuScriptFailed=false;std::wstring menuScriptError;
void CALLBACK MenuScriptTick(HWND,UINT,UINT_PTR id,DWORD){if(menuStep>=menuScript.size()){KillTimer(nullptr,id);return;}auto f=menuScript[menuStep++];f();}
void MenuFail(const std::wstring& why){if(!menuScriptFailed){menuScriptFailed=true;menuScriptError=why;}for(int d=contextMenu.Depth();d>0;--d)PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_ESCAPE,0);}
void MenuShot(const std::wstring& name){
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);for(int i=0;i<contextMenu.Depth();++i)RedrawWindow(contextMenu.LevelWindow(i),nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);DwmFlush();
    RECT r;GetWindowRect(window,&r);RECT u=r;for(int i=0;i<contextMenu.Depth();++i){RECT m;GetWindowRect(contextMenu.LevelWindow(i),&m);UnionRect(&u,&u,&m);}
    const int w=u.right-u.left,h=u.bottom-u.top;HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen),win=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h),wb=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);
    auto old=SelectObject(mem,bmp);RECT all{0,0,w,h};FillRect(mem,&all,(HBRUSH)GetStockObject(WHITE_BRUSH));auto oldw=SelectObject(win,wb);PrintWindow(window,win,PW_RENDERFULLCONTENT);
    BitBlt(mem,r.left-u.left,r.top-u.top,r.right-r.left,r.bottom-r.top,win,0,0,SRCCOPY);SelectObject(win,oldw);DeleteDC(win);DeleteObject(wb);
    for(int i=0;i<contextMenu.Depth();++i){RECT m;GetWindowRect(contextMenu.LevelWindow(i),&m);BitBlt(mem,m.left-u.left,m.top-u.top,m.right-m.left,m.bottom-m.top,screen,m.left,m.top,SRCCOPY|CAPTUREBLT);}
    SelectObject(mem,old);DeleteDC(mem);ReleaseDC(nullptr,screen);
    IWICImagingFactory* f=nullptr;IWICBitmap* b=nullptr;IWICStream* s=nullptr;IWICBitmapEncoder* e=nullptr;IWICBitmapFrameEncode* fr=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f));
    if(SUCCEEDED(hr))hr=f->CreateBitmapFromHBITMAP(bmp,nullptr,WICBitmapIgnoreAlpha,&b);if(SUCCEEDED(hr))hr=f->CreateStream(&s);
    if(SUCCEEDED(hr))hr=s->InitializeFromFilename((splitQa+name+L".png").c_str(),GENERIC_WRITE);if(SUCCEEDED(hr))hr=f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e);
    if(SUCCEEDED(hr))hr=e->Initialize(s,WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=e->CreateNewFrame(&fr,nullptr);if(SUCCEEDED(hr))hr=fr->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=fr->WriteSource(b,nullptr);if(SUCCEEDED(hr))hr=fr->Commit();if(SUCCEEDED(hr))hr=e->Commit();
    for(IUnknown* u2:std::initializer_list<IUnknown*>{fr,e,s,b,f})if(u2)u2->Release();DeleteObject(bmp);
    Log(L"SPLIT shot "+name+(SUCCEEDED(hr)?L".png":L" FAILED"));
}
// By local page file (titles can be injected by earlier stages).
int TabTitled(const wchar_t* name){const std::wstring file=std::wstring(L"/")+name+L".html";for(size_t i=0;i<pages.size();++i){const auto& u=pages[i]->uri;if(u.size()>=file.size()&&u.compare(u.size()-file.size(),file.size(),file)==0)return (int)i;}return -1;}
// Opens the tab menu on `source` and plays `steps`; returns false (with a log) on failure.
bool RunMenuScript(int source,std::vector<std::function<void()>> steps){
    menuScript=std::move(steps);menuStep=0;menuScriptFailed=false;menuScriptError.clear();
    menuScript.push_back([]{for(int d=contextMenu.Depth();d>0;--d)PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_ESCAPE,0);});  // one Escape per open level
    KillTimer(window,SplitTestTimer);const UINT_PTR t=SetTimer(nullptr,0,300,MenuScriptTick);
    contextMenu.ignorePointer=true;TabContextMenu(source,0);contextMenu.ignorePointer=false;KillTimer(nullptr,t);SetTimer(window,SplitTestTimer,1500,nullptr);
    if(menuScriptFailed){SplitFail(L"menu V2: "+menuScriptError);return false;}return true;
}
// Real controller state against the arrangement: visible members at their slot, rails hidden.
bool SplitCheck(const std::wstring& label,int members,SV::Orientation orientation){
    auto v=CurrentSplit();if(!v.group||(int)v.members.size()!=members||v.a.orientation!=orientation){SplitFail(label+L": group shape");return false;}
    for(auto& slot:v.a.slots){auto t=v.members[slot.member];if(!t->controller.p){SplitFail(label+L": controller missing");return false;}
        BOOL visible=FALSE;RECT b{};t->controller->get_IsVisible(&visible);t->controller->get_Bounds(&b);const RECT want=PixelRect(slot.box);
        if((visible!=FALSE)==slot.rail||(!slot.rail&&!EqualRect(&b,&want))){SplitFail(label+L": "+t->title+L" visible="+std::to_wstring(visible)+L" bounds "+std::to_wstring(b.left)+L","+std::to_wstring(b.right));return false;}}
    Log(L"SPLIT "+label+L": "+std::to_wstring(members)+L" sites, "+std::to_wstring(v.a.visible)+L" visible, controllers placed at their slots");return true;
}
// innerWidth/innerHeight are the CSS px each site lays out with.
void SplitMeasure(const std::wstring& label){
    auto v=CurrentSplit();const UINT dpi=GetDpiForWindow(window);const float zoom=PageZoom();
    for(auto& slot:v.a.slots){if(slot.rail)continue;auto t=v.members[slot.member];RECT b{};t->controller->get_Bounds(&b);
        const float dipW=(b.right-b.left)*96.f/dpi,dipH=(b.bottom-b.top)*96.f/dpi;std::wstring title=t->title;
        auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([=](HRESULT hr,LPCWSTR json)->HRESULT{
            if(FAILED(hr)||!json){SplitFail(label+L": measure "+title);return S_OK;}
            std::wstring s=json;for(auto& c:s)if(!(iswdigit(c)||c==L'.'))c=L' ';float css[3]{};swscanf(s.c_str(),L"%f %f %f",&css[0],&css[1],&css[2]);
            const float wantW=dipW/zoom,wantH=dipH/zoom,wantRatio=dpi/96.f*zoom;
            wchar_t line[400];swprintf(line,400,L"SPLIT CSS %ls %ls: innerWidth %.0f (DIP %.1f / zoom %.2f = %.1f), innerHeight %.0f (%.1f), devicePixelRatio %.3f (DPI %u/96 x zoom = %.3f)",
                label.c_str(),title.c_str(),css[0],dipW,zoom,wantW,css[1],wantH,css[2],dpi,wantRatio);Log(line);
            if(std::fabs(css[0]-wantW)>2||std::fabs(css[1]-wantH)>2||std::fabs(css[2]-wantRatio)>.02f)SplitFail(label+L": CSS px do not match DIP / zoom for "+title);
            if(css[0]+.5f<SV::minWidthCss&&v.a.orientation==SV::Orientation::Row)SplitFail(label+L": "+title+L" under 360 CSS px");
            return S_OK;});
        HRESULT hr=t->view->ExecuteScript(L"JSON.stringify([innerWidth,innerHeight,devicePixelRatio])",cb);cb->Release();if(FAILED(hr)){SplitFail(label+L": ExecuteScript");return;}}
}
// ---- Repaint stress (--split-test): separator swept fast, slow, back and forth, in
// both orientations, with Escape and a lost capture. Each capture is read back and
// classified against the arrangement: black inside a page (never-painted surface),
// accent outside the active ring (stale ring or handle), anything but canvas in a
// gap. Pages may legitimately show the canvas while they re-render after a resize.
struct SplitCapture{int w=0,h=0;POINT client{};std::vector<std::uint32_t> px;std::uint32_t At(int x,int y)const{return x>=0&&y>=0&&x<w&&y<h?px[(size_t)y*w+x]:0;}};
SplitCapture SplitGrab(){
    SplitCapture c;RECT r;GetWindowRect(window,&r);c.w=r.right-r.left;c.h=r.bottom-r.top;POINT o{0,0};ClientToScreen(window,&o);c.client={o.x-r.left,o.y-r.top};
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,c.w,c.h);auto old=SelectObject(mem,bmp);
    if(splitOnScreen){DwmFlush();BitBlt(mem,0,0,c.w,c.h,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);}
    else{PrintWindow(window,mem,PW_RENDERFULLCONTENT);BlendGrips(mem,c.client);splitOverlay.BlendInto(mem,c.client);}SelectObject(mem,old);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=c.w;info.bmiHeader.biHeight=-c.h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    c.px.resize((size_t)c.w*c.h);GetDIBits(mem,bmp,0,c.h,c.px.data(),&info,DIB_RGB_COLORS);DeleteDC(mem);ReleaseDC(nullptr,screen);DeleteObject(bmp);return c;
}
int SplitArtifacts(const SplitCapture& c,const std::wstring& label,bool settled=false){
    auto v=CurrentSplit();if(!v.group)return 0;const float d=GetDpiForWindow(window)/96.f;int black=0,trail=0,gap=0,hole=0;
    auto inside=[&](const SV::Box& b,float x,float y,float m){return x>=b.left+m&&x<b.right-m&&y>=b.top+m&&y<b.bottom-m;};
    const auto content=ContentBox();const SV::Box active=v.active>=0?v.a.slots[v.a.SlotOf(v.active)].box:SV::Box{};
    int run=0,blackRun=0;
    for(int py=(int)(content.top*d)+6;py<(int)(content.bottom*d)-6;py+=7)for(int px=(int)(content.left*d)+2;px<(int)(content.right*d)-2;px+=1){if(px==(int)(content.left*d)+2)run=blackRun=0;
        const float x=px/d,y=py/d;const std::uint32_t p=c.At(px+c.client.x,py+c.client.y);const int R=(p>>16)&255,G=(p>>8)&255,B=p&255;
        const bool isBlack=R<8&&G<8&&B<8,isAccent=std::abs(R-62)<16&&std::abs(G-103)<16&&B>230,isCanvas=std::abs(R-23)<10&&std::abs(G-23)<10&&std::abs(B-23)<10;
        bool inPage=false;for(auto& s:v.a.slots)if(!s.rail&&inside(s.box,x,y,2))inPage=true;
        bool nearRing=!inPage&&x>=active.left-4&&x<active.right+4&&y>=active.top-4&&y<active.bottom+4;
        bool onHandle=false;for(int s:v.a.separators)if(inside(v.a.SeparatorHit(s),x,y,-2)&&std::fabs(SV::Along(x,y,v.a.orientation==SV::Orientation::Row?SV::Orientation::Column:SV::Orientation::Row)-SV::Along((v.a.SeparatorBox(s).left+v.a.SeparatorBox(s).right)/2,(v.a.SeparatorBox(s).top+v.a.SeparatorBox(s).bottom)/2,v.a.orientation==SV::Orientation::Row?SV::Orientation::Column:SV::Orientation::Row))<30)onHandle=true;
        // While the placeholders are up, the dragged separator is an accent line along its whole length.
        if(ResizePlaceholdersUp()&&resizePreview.slot>=0&&resizePreview.slot+1<(int)v.a.slots.size()&&inside(v.a.SeparatorBox(resizePreview.slot),x,y,-2))onHandle=true;
        bool onRail=false;for(auto& s:v.a.slots)if(s.rail&&inside(s.box,x,y,-1))onRail=true;
        bool edge=false;for(auto& s:v.a.slots)if(!s.rail&&inside(s.box,x,y,-1)&&!inside(s.box,x,y,2))edge=true;  // antialiased page edge
        auto note=[&](const wchar_t* kind){if(black+trail+gap+hole<6){wchar_t m[160];swprintf(m,160,L"SPLIT repaint sample %ls at %.1f,%.1f rgb(%d,%d,%d)",kind,x,y,R,G,B);Log(m);}};
        const bool exactCanvas=std::abs(R-23)<3&&std::abs(G-23)<3&&std::abs(B-23)<3;
        if(inPage&&settled&&exactCanvas){if(++run==24){note(L"hole");++hole;}}else run=0;  // a page still showing the canvas under it
        if(inPage&&isBlack){if(++blackRun==12){note(L"black");++black;}}else blackRun=0;
        if(inPage){if(isAccent){note(L"trail");++trail;}}
        else if(!onRail&&!onHandle&&!edge){if(isAccent&&!nearRing){note(L"trail");++trail;}else if(!isCanvas&&!isAccent&&!nearRing){note(L"gap");++gap;}}
    }
    Log(L"SPLIT repaint "+label+L": black="+std::to_wstring(black)+L" trail="+std::to_wstring(trail)+L" gap="+std::to_wstring(gap)+(settled?L" hole="+std::to_wstring(hole):L""));
    return black+trail+gap+hole;
}
void SplitPump(int ms){const ULONGLONG end=GetTickCount64()+ms;MSG m;while(GetTickCount64()<end){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjects(0,nullptr,FALSE,4,QS_ALLINPUT);}}
// Sweep separator 0 by `steps` x `step` DIPs and back; `interval` ms between moves.
// Captures mid-sweep (while pages re-render) and after settling; returns artifacts.
int SplitSweep(const std::wstring& label,int steps,float step,int interval,bool shots){
    auto v=CurrentSplit();if(!v.group||v.a.separators.empty())return -1;const bool row=v.a.orientation==SV::Orientation::Row;
    auto sep=v.a.SeparatorBox(v.a.separators[0]);float x=(sep.left+sep.right)/2,y=(sep.top+sep.bottom)/2;if(row)y=400;else x=600;
    SplitMouse(WM_MOUSEMOVE,x,y);SplitPump(250);  // hover first, as a pointer does: the neighbours get captured
    SplitMouse(WM_LBUTTONDOWN,x,y);int artifacts=0;
    if(resizePreview.active){Log(L"SPLIT "+label+L": placeholders shown on a mere press");++artifacts;}  // only past the 3 DIP threshold
    for(int pass=0;pass<2;++pass)for(int i=0;i<steps;++i){
        if(row)x+=(pass?-step:step);else y+=(pass?-step:step);SplitMouse(WM_MOUSEMOVE,x,y);SplitPump(interval);
        if(pass==0&&i==steps/2){auto c=SplitGrab();artifacts+=SplitArtifacts(c,label+L" pendant",false);if(shots)SplitShot(label+L"-pendant");
            if(!resizePreview.active||!splitOverlay.Visible()||splitAnim.frame.tiles.size()!=2||splitAnim.frame.gaps.size()!=1){Log(L"SPLIT placeholders missing mid-drag");++artifacts;}
            else Log(L"SPLIT "+label+L": #171717 placeholders ("+splitAnim.frame.tiles[0].name+L" "+splitAnim.frame.tiles[0].domain+L" / "+splitAnim.frame.tiles[1].name+L" "+splitAnim.frame.tiles[1].domain+L"), accent separator");}
    }
    SplitMouse(WM_LBUTTONUP,x,y);
    // After release the preview must hold until the pages render, then go: wait for it, bounded.
    const ULONGLONG released=GetTickCount64();while((resizePreview.settling||resizePreview.fading)&&GetTickCount64()-released<2500)SplitPump(16);
    if(resizePreview.settling||resizePreview.fading){Log(L"SPLIT preview did not finish");++artifacts;}
    SplitPump(200);auto c=SplitGrab();artifacts+=SplitArtifacts(c,label+L" apres",true);if(shots)SplitShot(label+L"-apres");
    return artifacts;
}
bool splitBusy=false;
void SplitTestStep(){
    if(splitBusy)return;struct Busy{Busy(){splitBusy=true;}~Busy(){splitBusy=false;}} busy;
    if(halted)return;auto loaded=[]{for(auto& t:pages)if(!t->revealed)return false;return true;};
    switch(splitStage){
    case 0:{splitQa=base+L"\\split-qa\\";std::filesystem::create_directories(std::filesystem::path(splitQa));
        SetWindowPos(window,nullptr,0,0,S(1180),S(815),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);
        Navigate(Active(),SplitPage(L"Youtube",L"#EAF0FF"));NewTab(SplitPage(L"Paper",L"#FFF4E5"));NewTab(SplitPage(L"Luma",L"#E9F7EF"));NewTab(SplitPage(L"GitHub",L"#F3E8FF"));
        Log(L"SPLIT pages: 4 local files, DPI="+std::to_wstring(GetDpiForWindow(window)));break;}
    case 1:if(!loaded()){if(++splitWait<20)return;SplitFail(L"local pages did not load");return;}Select(0);break;
    case 2:SplitDrag(1,1000,450,true);if(!SplitCheck(L"pair",2,SV::Orientation::Row))return;SplitMeasure(L"pair");SplitShot(L"01-deux-sites-cote-a-cote");splitStage=17;return;
    // Feasibility probe: latency of CapturePreview and what it returns right after a resize.
    case 17:{auto v=CurrentSplit();auto tab=v.members[1];CapturePanel(tab,[tab](bool ok,UINT w,UINT h){RECT b=tab->placed;Log(L"SPLIT probe idle: ok="+std::to_wstring(ok)+L" "+std::to_wstring(w)+L"x"+std::to_wstring(h)+L" bounds "+std::to_wstring(b.right-b.left)+L"x"+std::to_wstring(b.bottom-b.top));});splitStage=18;return;}
    case 18:{auto v=CurrentSplit();auto tab=v.members[1];auto e=SV::Extents(v.a);e[0]+=150;e[1]-=150;SetExtents(v,e);ApplyPages();const RECT want=tab->placed;const ULONGLONG t0=GetTickCount64();
        auto probe=std::make_shared<std::function<void(int)>>();std::weak_ptr<std::function<void(int)>> self=probe;
        *probe=[tab,want,t0,self](int n){CapturePanel(tab,[tab,want,t0,self,n](bool ok,UINT w,UINT h){const bool match=ok&&std::abs((int)w-(want.right-want.left))<=1&&std::abs((int)h-(want.bottom-want.top))<=1;
            Log(L"SPLIT probe after resize #"+std::to_wstring(n)+L" +"+std::to_wstring(GetTickCount64()-t0)+L" ms: "+std::to_wstring(w)+L"x"+std::to_wstring(h)+L" want "+std::to_wstring(want.right-want.left)+L"x"+std::to_wstring(want.bottom-want.top)+(match?L" MATCH":L""));
            if(!match&&n<30)if(auto f=self.lock())(*f)(n+1);});};
        splitProbe=probe;(*probe)(0);splitStage=19;return;}
    // The grip: topmost child over the gap and 4 DIP into each real page, and a drag on it moves the separator.
    case 19:{auto v=CurrentSplit();auto g=v.a.SeparatorBox(v.a.separators[0]);const float gx=(g.left+g.right)/2;if(splitGrips.empty()){SplitFail(L"no separator grip (layered child windows unavailable)");return;}
        for(float dx:{-7.f,0.f,7.f}){POINT p{S(gx+dx),S(400)};if(ChildWindowFromPointEx(window,p,CWP_SKIPINVISIBLE)!=splitGrips[0]){SplitFail(L"grip is not on top at "+std::to_wstring(dx)+L" DIP");return;}}
        {POINT p{S(gx+12),S(400)};HWND page=ChildWindowFromPointEx(window,p,CWP_SKIPINVISIBLE);if(page==splitGrips[0]||!page){SplitFail(L"grip wider than 16 DIP");return;}}
        const float before=SV::Extents(v.a)[0];auto grip=[&](UINT msg,float x){POINT p{S(x),S(400)};MapWindowPoints(window,splitGrips[0],&p,1);splitSynthetic=true;SendMessageW(splitGrips[0],msg,msg==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(p.x,p.y));splitSynthetic=false;};
        // Handle (Paper 08): hidden at rest, fades in on hover within 150 ms, out on leave.
        auto look=[&]{return gripLooks.empty()?GripLook{}:gripLooks[0];};auto settle=[&](float want)->int{const ULONGLONG t0=GetTickCount64();while(look().alpha!=want&&GetTickCount64()-t0<1000)SplitPump(4);return (int)(GetTickCount64()-t0);};
        if(look().alpha!=0||look().target!=0){SplitFail(L"handle shown at rest");return;}SplitShot(L"13-poignee-repos");
        {POINT p{S(gx),S(400)};MapWindowPoints(window,splitGrips[0],&p,1);splitSynthetic=true;SendMessageW(splitGrips[0],WM_MOUSEMOVE,0,MAKELPARAM(p.x,p.y));splitSynthetic=false;}
        if(chrome.split.hotHandle!=0||look().target!=1){SplitFail(L"hovering the grip did not show the handle");return;}
        const int fadeIn=settle(1);if(look().alpha!=1||fadeIn>200){SplitFail(L"handle fade-in too slow ("+std::to_wstring(fadeIn)+L" ms)");return;}SplitShot(L"14-poignee-survol");
        SendMessageW(splitGrips[0],WM_MOUSELEAVE,0,0);const int fadeOut=settle(0);if(look().alpha!=0){SplitFail(L"leaving the grip did not hide the handle");return;}
        Log(L"SPLIT handle: hidden at rest, hover shows it in "+std::to_wstring(fadeIn)+L" ms, leave hides it in "+std::to_wstring(fadeOut)+L" ms (opacity only)");
        grip(WM_LBUTTONDOWN,gx+6);bool kept=true;for(int i=1;i<=6;++i){grip(WM_MOUSEMOVE,gx+6+5.f*i);if(i==3)SendMessageW(splitGrips[0],WM_MOUSELEAVE,0,0);SplitPump(16);if(look().target!=1)kept=false;}
        if(!kept){SplitFail(L"the handle flickered off during a drag that left its target");return;}
        grip(WM_LBUTTONUP,gx+36);SplitPump(300);if(look().alpha!=0){SplitFail(L"the handle stayed after a drag released off its target");return;}
        const float after=SV::Extents(CurrentSplit().a)[0];if(after<=before+1){SplitFail(L"a drag on the grip did not move the separator");return;}
        StartKeyboardResize();SplitPump(200);if(look().alpha!=1||!look().ring){SplitFail(L"keyboard focus on the separator did not show the handle and its ring");return;}SplitShot(L"15-poignee-clavier");
        SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);SplitPump(200);if(look().alpha!=0||look().ring){SplitFail(L"leaving keyboard resize kept the handle");return;}
        Log(L"SPLIT handle: kept through a drag that left the target, gone on release off it; keyboard focus shows it with its 50 % ring");
        Log(L"SPLIT grip: topmost child over the 4 DIP gap and 6 DIP into each page (16 DIP), drag on it moved the separator "+std::to_wstring((int)before)+L" -> "+std::to_wstring((int)after)+L" DIP");splitStage=20;return;}
    case 20:{  // placeholders name the sites: short name and bare domain, injected locally (no navigation)
        auto v=CurrentSplit();auto a=v.members[0],b=v.members[1];const auto sa=std::make_pair(a->title,a->uri),sb=std::make_pair(b->title,b->uri);
        a->title=L"GitHub · Le changement est constant. GitHub vous permet de garder une longueur d’avance. · GitHub · GitHub";a->uri=L"https://www.github.com/ValXXII/Browser/pulls?q=is%3Aopen#top";
        b->title=L"Tableau de bord";b->uri=L"https://un-sous-domaine-vraiment-tres-long.exemple-de-domaine-public-pour-tester.fr:8443/a/b/";
        const bool names=SiteLabel(a)==L"GitHub"&&BareDomain(a->uri)==L"github.com"&&BareDomain(b->uri)==L"un-sous-domaine-vraiment-tres-long.exemple-de-domaine-public-pour-tester.fr"&&BareDomain(sa.second).empty();
        const int bad=SplitSweep(L"08-separateur-rapide",20,10,8,true);a->title=sa.first;a->uri=sa.second;b->title=sb.first;b->uri=sb.second;
        if(!names){SplitFail(L"placeholder identity: short name / bare domain");return;}
        if(bad){SplitFail(L"repaint artifacts after a fast separator drag");return;}break;}
    case 21:if(SplitSweep(L"09-separateur-lent",40,3,33,true)){SplitFail(L"repaint artifacts after a slow separator drag");return;}break;
    case 22:{auto before=SV::Extents(CurrentSplit().a);auto v=CurrentSplit();auto sep=v.a.SeparatorBox(v.a.separators[0]);float x=(sep.left+sep.right)/2;
        SplitMouse(WM_LBUTTONDOWN,x,400);for(int i=0;i<12;++i){x+=9;SplitMouse(WM_MOUSEMOVE,x,400);SplitPump(8);}SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);SplitPump(300);
        auto after=SV::Extents(CurrentSplit().a);if(sepDrag.active||std::fabs(after[0]-before[0])>1){SplitFail(L"Escape did not restore the separator");return;}
        SplitPump((int)GetDoubleClickTime()+80);  // a second press within the double-click time would equalise instead
        SplitMouse(WM_LBUTTONDOWN,x-108,400);for(int i=0;i<12;++i){SplitMouse(WM_MOUSEMOVE,x-108-9.f*(i+1),400);SplitPump(8);}SendMessageW(window,WM_CANCELMODE,0,0);SplitPump(300);
        after=SV::Extents(CurrentSplit().a);if(sepDrag.active||std::fabs(after[0]-before[0])>1){SplitFail(L"a lost capture did not restore the separator");return;}
        if(!gripLooks.empty()&&gripLooks[0].target!=0){SplitFail(L"the handle stayed after a lost capture");return;}
        if(SplitArtifacts(SplitGrab(),L"10-echap-capture-perdue")){SplitFail(L"repaint artifacts after Escape / lost capture");return;}Log(L"SPLIT separator: Escape and a lost capture restore the sizes");splitStage=3;return;}
    case 3:{{auto v=CurrentSplit();SplitDrag(2,v.a.slots[0].box.right-20,450,true);}if(!SplitCheck(L"insert",3,SV::Orientation::Row))return;SplitMeasure(L"three");SplitShot(L"02-trois-sites");
        if(SplitSweep(L"11-trois-sites-separateur",6,4,16,false)){SplitFail(L"repaint artifacts with three sites");return;}break;}
    case 4:{auto v=CurrentSplit();const float x=v.a.slots[1].box.right-6;SplitDrag(3,x,450,false);
        if(tabDrag.target.kind!=SV::Kind::Refuse||!splitOverlay.Visible()){SplitFail(L"refusal preview");return;}SplitShot(L"03-refus-pas-de-place");
        SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);if(TabIndexOf(pages.back())!=3||pages[3]->group){SplitFail(L"escape restore");return;}break;}
    case 5:{splitStage=50;auto v=CurrentSplit();const float x=(v.a.slots[0].box.left+v.a.slots[0].box.right)/2;
        // Long localised GitHub title, injected locally (no navigation): the chip and the card must read "GitHub".
        splitNamed=pages[3];splitSaved={splitNamed->title,splitNamed->uri};splitNamed->title=L"GitHub · Le changement est constant. GitHub vous permet de garder une longueur d’avance. · GitHub · GitHub";splitNamed->uri=L"https://github.com/?locale=fr-fr";
        SplitDrag(3,x,450,false);
        if(tabDrag.target.kind!=SV::Kind::Replace){SplitFail(L"replace preview");return;}break;}
    // A tick later: the preview has faded in on display-paced frames (nothing is forced).
    case 51:if(!splitOverlay.Visible()||splitAnim.frame.plate.opacity<.95f){SplitFail(L"preview did not fade in");return;}
        if(splitAnim.frame.plate.title!=L"GitHub"||splitAnim.frame.card.title!=L"GitHub"||!splitAnim.frame.plate.detail.empty()){SplitFail(L"drag surfaces must read GitHub");return;}
        Log(L"SPLIT names: chip and card read \""+splitAnim.frame.plate.title+L"\" for the tab titled \""+splitNamed->title+L"\"");
        SplitShot(L"04-apercu-remplacer");SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);splitNamed->title=splitSaved.first;splitNamed->uri=splitSaved.second;splitNamed.reset();splitStage=6;return;
    case 6:{SetWindowPos(window,nullptr,0,0,S(900),S(815),SWP_NOMOVE|SWP_NOZORDER);if(!SplitCheck(L"narrow window",3,SV::Orientation::Row))return;
        if(CurrentSplit().a.visible!=2||pages.size()!=4){SplitFail(L"rail on shrink");return;}SplitShot(L"05-rail-fenetre-etroite");break;}
    case 7:{SetWindowPos(window,nullptr,0,0,S(1180),S(815),SWP_NOMOVE|SWP_NOZORDER);StartKeyboardResize();for(int i=0;i<3;++i)SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);
        if(!SplitCheck(L"keyboard resize",3,SV::Orientation::Row))return;SplitShot(L"06-redimensionner-clavier");SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);break;}
    case 8:{QuitGroup(Active()->group);Select(0);PlaceFromMenu(pages[1],SV::Side::Bottom);if(!SplitCheck(L"stacked",2,SV::Orientation::Column))return;SplitMeasure(L"stacked");SplitShot(L"07-empiles");
        SplitPump(500);if(SplitSweep(L"12-empiles-separateur-rapide",14,6,8,true)){SplitFail(L"repaint artifacts after a stacked separator drag");return;}break;}
    case 9:settings.zoomPercent=125;ApplyZoomSettings();break;
    case 10:{if(!SplitCheck(L"zoom 125",2,SV::Orientation::Column))return;SplitMeasure(L"zoom125");break;}
    case 11:{settings.zoomPercent=100;ApplyZoomSettings();auto v=CurrentSplit();auto other=v.members[v.active==0?1:0];splitFocus=other;if(other->controller.p)other->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);break;}
    case 12:{if(Active()!=splitFocus){SplitFail(L"focus in a page did not make it the active panel");return;}Log(L"SPLIT focus: clicking/focusing "+splitFocus->title+L" made it the active panel (address "+splitFocus->uri+L")");splitFocus.reset();
        splitStage=60;return;}
    // Tab menu, split block V2 (Paper p-7-0): free tabs + hover, full group, other site by keyboard.
    case 60:{std::vector<int> groups;for(auto& g:splitGroups)groups.push_back(g.first);for(int g:groups)QuitGroup(g);
        const int yt=TabTitled(L"Youtube"),paper=TabTitled(L"Paper");if(yt<0||paper<0){SplitFail(L"menu V2: local tabs missing");return;}Select(yt);
        if(!RunMenuScript(paper,{
            []{if(lastSplitMenu.empty()||lastSplitMenu[0].kind!=CM::Kind::Header||lastSplitMenu[0].label!=L"Avec Youtube"){MenuFail(L"header does not name the page on screen");return;}
               if(!contextMenu.TestHover(0,L"À droite",false))MenuFail(L"no À droite row");},
            [yt]{if(chrome.menuTargets!=std::vector<int>({yt}))MenuFail(L"hovering À droite did not designate Youtube in the strip");MenuShot(L"16-menu-v2-onglets-libres-survol");},
        }))return;
        Log(L"SPLIT menu V2: header names the page on screen (Avec Youtube · affiché), À droite designates it in the strip");
        // The clicked tab is the one on screen: the block is the lone "Avec un autre site", flush left.
        if(!RunMenuScript(yt,{
            []{if(lastSplitMenu.size()!=1||lastSplitMenu[0].label!=L"Avec un autre site"||lastSplitMenu[0].glyph!=CM::Glyph::None){MenuFail(L"lone Avec un autre site row is not flush left");return;}
               if(!contextMenu.TestHover(0,L"Avec un autre site",true))MenuFail(L"lone row not focusable");},
            []{PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_RIGHT,0);},
            []{if(contextMenu.Depth()<2){MenuFail(L"Right did not open the site list from the lone row");return;}MenuShot(L"19-menu-v2-autre-site-seul");},
        }))return;
        Log(L"SPLIT menu V2: clicked tab on screen, lone Avec un autre site row flush with the other commands, site list opens by keyboard");break;}
    case 61:{const int yt=TabTitled(L"Youtube"),gh=TabTitled(L"GitHub"),luma=TabTitled(L"Luma"),paper=TabTitled(L"Paper");if(yt<0||gh<0||luma<0||paper<0){SplitFail(L"menu V2: local tabs missing");return;}Select(yt);
        {SV::Target t;t.kind=SV::Kind::Split;t.side=SV::Side::Right;t.orientation=SV::Orientation::Row;ApplySplitTarget(pages[gh],t,pages[yt],CurrentSplit());}
        {auto v=CurrentSplit();SV::Target t;t.kind=SV::Kind::Insert;t.index=(int)v.members.size();t.orientation=v.a.orientation;ApplySplitTarget(pages[TabTitled(L"Luma")],t,nullptr,v);}
        (void)luma;if(CurrentSplit().members.size()!=3){SplitFail(L"menu V2: could not build the 3-site group");return;}
        if(!RunMenuScript(TabTitled(L"Paper"),{
            []{bool note=false,off=true;for(auto& i:lastSplitMenu){if(i.kind==CM::Kind::Note&&i.label==L"Pas de place pour un 4ᵉ site · 360 px minimum")note=true;if((i.label==L"Ajouter au début"||i.label==L"Ajouter à la fin")&&i.enabled)off=false;}
               if(!note||!off){MenuFail(L"full group: rows not unavailable or no reason row");return;}
               if(contextMenu.TestHover(0,L"Ajouter au début",false)){MenuFail(L"an unavailable row took the highlight");return;}
               if(!contextMenu.TestHover(0,L"Remplacer un site",false))MenuFail(L"no Remplacer un site row");},
            []{},  // the submenu opens after the 200 ms hover delay
            []{if(contextMenu.Depth()<2){MenuFail(L"Remplacer un site did not open its member list");return;}contextMenu.TestHover(1,L"GitHub",false);
               if(chrome.menuTargets.size()!=1)MenuFail(L"a member row did not designate that member");MenuShot(L"17-menu-v2-groupe-plein");},
        }))return;
        (void)paper;Log(L"SPLIT menu V2: full group shows start / end / between unavailable with the reason row, Remplacer un site lists the members");break;}
    case 62:{std::vector<int> groups;for(auto& g:splitGroups)groups.push_back(g.first);for(int g:groups)QuitGroup(g);Select(TabTitled(L"Youtube"));
        if(!RunMenuScript(TabTitled(L"Paper"),{
            []{if(!contextMenu.TestHover(0,L"Avec un autre site",true)){MenuFail(L"no Avec un autre site row");return;}PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_RIGHT,0);},
            []{if(contextMenu.Depth()<2){MenuFail(L"Right did not open the site list");return;}if(!contextMenu.TestHover(1,L"GitHub",true)){MenuFail(L"GitHub missing from the site list");return;}PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_RIGHT,0);},
            []{if(contextMenu.Depth()<3){MenuFail(L"Right did not open the positions");return;}PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_DOWN,0);PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_DOWN,0);},
            []{const int gh=TabTitled(L"GitHub");if(chrome.menuTargets!=std::vector<int>({gh}))MenuFail(L"keyboard focus did not designate GitHub");MenuShot(L"18-menu-v2-autre-site-clavier");
               PostMessageW(contextMenu.LevelWindow(0),WM_KEYDOWN,VK_RETURN,0);},
        }))return;
        {auto paper=pages[TabTitled(L"Paper")],gh=pages[TabTitled(L"GitHub")];auto v=CurrentSplit();
         if(!paper->group||paper->group!=gh->group||v.a.orientation!=SV::Orientation::Column||v.members[0]!=paper||Active()!=paper){SplitFail(L"menu V2: Au-dessus on GitHub (keyboard) did not stack Paper above GitHub");return;}}
        if(!chrome.menuTargets.empty()){SplitFail(L"menu V2: designation left in the strip after the menu closed");return;}
        Log(L"SPLIT menu V2: keyboard Avec un autre site › GitHub › Au-dessus stacked Paper above GitHub (off-screen target), designation cleared");break;}
    case 63:{  // rounded page corners: a light single page, then a light / dark pair
        std::vector<int> groups;for(auto& g:splitGroups)groups.push_back(g.first);for(int g:groups)QuitGroup(g);Select(TabTitled(L"Paper"));SplitPump(400);
        auto corner=[&](const std::wstring& label)->bool{auto cap=SplitGrab();auto at=[&](int x,int y)->std::uint32_t{x+=cap.client.x;y+=cap.client.y;return x>=0&&y>=0&&x<cap.w&&y<cap.h?cap.px[(size_t)y*cap.w+x]&0xFFFFFF:0;};
            const int canvas=GetRValue(BG);auto grey=[&](std::uint32_t p,int v){return std::abs((int)(p&255)-v)<=3&&std::abs((int)((p>>8)&255)-v)<=3&&std::abs((int)((p>>16)&255)-v)<=3;};
            const auto cs=PageCorners();if(cs.empty()){SplitFail(label+L": no rounded corner");return false;}
            for(auto& c:cs){const int x=c.right?S(c.page.right)-1:S(c.page.left),y=S(c.page.bottom)-1,in=S(SV::pageRadius)+2;const int ix=c.right?x-in:x+in,iy=y-in;
                if(!grey(at(x,y),canvas)){SplitFail(label+L": the page still shows in its "+(c.right?L"right":L"left")+L" corner");return false;}
                if(grey(at(ix,iy),canvas)){SplitFail(label+L": the corner mask covers the page inside its arc");return false;}}
            Log(L"SPLIT corners "+label+L": "+std::to_wstring(cs.size())+L" outer bottom corners clipped to "+std::to_wstring((int)SV::pageRadius)+L" DIP, page intact inside the arc");return true;};
        if(!corner(L"single page"))return;SplitShot(L"20-coins-page-seule");
        Navigate(pages[TabTitled(L"Luma")],SplitPage(L"Sombre",L"#0D1117"));SplitPump(1500);
        {const int dark=TabTitled(L"Sombre");if(dark<0){SplitFail(L"corners: dark page did not load");return;}
         SV::Target t;t.kind=SV::Kind::Split;t.side=SV::Side::Right;t.orientation=SV::Orientation::Row;auto a=pages[dark],b=pages[TabTitled(L"Paper")];ApplySplitTarget(a,t,b,CurrentSplit());SplitPump(600);}
        if(!corner(L"light / dark pair"))return;SplitShot(L"21-coins-split-clair-sombre");
        {auto v=CurrentSplit();auto e=SV::Extents(v.a);e[0]+=120;e[1]-=120;SetExtents(v,e);ApplyPages();SplitPump(400);}
        if(!corner(L"pair after a resize"))return;
        const auto before=Active();Command('W');if(Active()==before||VisibleGroup()){SplitFail(L"close active panel");return;}
        if(pages.size()!=3){SplitFail(L"tabs lost on close");return;}
        Log(L"SPLIT PASS: real WebView2 pages side by side, inserted, refused, replaced (preview), rails, keyboard resize, stacked, zoom 125, close; CSS px = DIP / zoom verified in each page");
        KillTimer(window,SplitTestTimer);PostMessageW(window,WM_CLOSE,0,0);return;}
    }
    ++splitStage;
}
// i18n-exempt-end
void RefreshTools(){auto tab=Active();chrome.navigationLoading=tab&&tab->loading.IsLoading();SyncStopMorph(NativeChrome::Motion::Now());BuildChrome();if(!tooltip)return;static std::vector<int> previous;for(int id:previous){TOOLINFOW t{sizeof(t)};t.hwnd=window;t.uId=id;SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&t);}previous.clear();for(auto&r:chrome.regions){TOOLINFOW t{sizeof(t)};t.uFlags=TTF_SUBCLASS;t.hwnd=window;t.uId=r.id;t.rect=ChromePx(r.rect);t.lpszText=const_cast<wchar_t*>(r.tip);SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);previous.push_back(r.id);}}
namespace CM=NativeChrome::ContextMenu;
void AnnounceCopied();
// A keyboard-opened menu drops just below its control (DIP region -> device pixels), aligned to the
// control's outer edge: the flower's menu opens rightward, the gear's leftward.
struct MenuPlacement{RECT control;bool alignRight;};
MenuPlacement PlaceMenu(const D2D1_RECT_F& control,float chromeWidth,UINT dpi){
    auto px=[dpi](float v){return (LONG)std::lround(v*dpi/96.f);};
    return {{px(control.left),px(control.top),px(control.right),px(control.bottom)},control.left+control.right>chromeWidth};
}
// Every right-click menu is the native one (src/context-menu.h). `anchor` is the chrome
// region a keyboard-opened menu belongs to; 0 opens it at the pointer.
int ShowMenu(const std::vector<CM::Item>& items,int anchor){
    POINT p{};GetCursorPos(&p);
    auto region=anchor?std::find_if(chrome.regions.begin(),chrome.regions.end(),[anchor](const NativeChrome::Region& r){return r.id==anchor;}):chrome.regions.end();
    if(region==chrome.regions.end())return contextMenu.Track(items,p,nullptr,false,motion.ReducedMotion());
    FocusAnchor(L"menu"); // Mode Focus: a keyboard menu under its control: the panel at rest first
    auto place=PlaceMenu(ChromeClient(region->rect),chrome.ClientWidth(),GetDpiForWindow(window));
    MapWindowPoints(window,nullptr,(POINT*)&place.control,2);
    return contextMenu.Track(items,p,&place.control,true,motion.ReducedMotion(),place.alignRight);
}
// Commands reached by Enter/Space on a focused chrome control anchor their menu to it; a click clears keyboardFocus first.
int KeyboardAnchor(){return GetFocus()==window?keyboardFocus:0;}
std::vector<CM::Item> TabMenuItems(){
    std::vector<CM::Item> items;
    // Titles are verbatim: '&' would read as an access key.
    for(int i=0;i<(int)pages.size();++i)items.push_back(CM::Command(1000+i,NativeUI::EscapeMenuText(pages[i]->title.empty()?std::wstring(TR(tab_untitled)):pages[i]->title),L"",true,i==current));
    return items;
}
void TabMenu(int anchor){
    const int cmd=ShowMenu(TabMenuItems(),anchor);if(cmd>=1000&&cmd-1000<(int)pages.size())Select(cmd-1000);
}
bool CopyToClipboard(const std::wstring& text){
    if(text.empty()||!OpenClipboard(window))return false;bool done=false;const auto bytes=(text.size()+1)*sizeof(wchar_t);
    if(HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes)){if(void* data=GlobalLock(mem)){memcpy(data,text.c_str(),bytes);GlobalUnlock(mem);EmptyClipboard();done=SetClipboardData(CF_UNICODETEXT,mem)!=nullptr;}if(!done)GlobalFree(mem);}
    CloseClipboard();return done;
}
// ---- Print / PDF -------------------------------------------------------------
// Print: the engine's own print preview (ICoreWebView2_16); older runtimes get window.print().
void PrintPage(std::shared_ptr<Tab> tab){
    if(halted||!tab||tab->closed||!tab->view.p)return;
    Com<ICoreWebView2_16> view16;
    if(SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_16,(void**)&view16.p))&&view16.p&&SUCCEEDED(view16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER)))return;
    Log(L"PRINT ShowPrintUI unavailable: window.print()");
    if(FAILED(tab->view->ExecuteScript(L"window.print()",nullptr)))Log(L"PRINT window.print failed");
}
// PDF name: page title, else host; characters Windows refuses become spaces.
std::wstring PdfFileName(const Tab& tab){
    std::wstring name=tab.home?L"":tab.title;if(name.empty())name=AddressBar::SiteOf(tab.uri);
    for(auto& c:name)if(c<32||wcschr(L"<>:\"/\\|?*",c))c=L' ';
    if(name.size()>120)name.resize(120);
    while(!name.empty()&&(name.back()==L' '||name.back()==L'.'))name.pop_back();
    while(!name.empty()&&name.front()==L' ')name.erase(name.begin());
    std::wstring upper=name;for(auto& c:upper)c=(wchar_t)std::towupper(c);
    static const wchar_t* reserved[]={L"CON",L"PRN",L"AUX",L"NUL",L"COM1",L"COM2",L"COM3",L"COM4",L"COM5",L"COM6",L"COM7",L"COM8",L"COM9",L"LPT1",L"LPT2",L"LPT3",L"LPT4",L"LPT5",L"LPT6",L"LPT7",L"LPT8",L"LPT9"};
    for(auto r:reserved)if(upper==r){name+=L" (page)";break;}
    return (name.empty()?L"page":name)+L".pdf";
}
// Save as PDF straight into the download folder; the file then appears in the
// download history like a finished download (no engine operation behind it).
void SavePageAsPdf(std::shared_ptr<Tab> tab){
    if(halted||!tab||tab->closed||!tab->view.p)return;
    Com<ICoreWebView2_7> view7;if(FAILED(tab->view->QueryInterface(IID_ICoreWebView2_7,(void**)&view7.p))||!view7.p){Log(L"PDF ICoreWebView2_7 unavailable");return;}
    const auto folder=EffectiveDownloadFolder();if(folder.empty()){Log(L"PDF no download folder");return;}
    std::error_code ignored;std::filesystem::create_directories(folder,ignored);
    const auto path=UniqueDownloadPath(folder,PdfFileName(*tab));const auto uri=tab->home?std::wstring():tab->uri;const auto started=NativeChrome::DownloadsUI::UnixNowMs();
    auto done=Callback<ICoreWebView2PrintToPdfCompletedHandler,HRESULT,BOOL>([path,uri,started](HRESULT hr,BOOL ok)->HRESULT{
        if(FAILED(hr)||!ok){Log(L"PDF failed hr="+std::to_wstring((unsigned long)hr)+L" "+path);return S_OK;}
        if(halted)return S_OK;
        DL::Item item;item.id=nextDownloadId++;item.path=path;item.uri=uri;item.started=started;item.state=DL::State::Completed;
        WIN32_FILE_ATTRIBUTE_DATA file{};if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&file))item.total=item.received=((std::int64_t)file.nFileSizeHigh<<32)|file.nFileSizeLow;
        downloads.insert(downloads.begin(),item);SaveDownloads();Log(L"PDF saved "+DL::FileName(path));
        if(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))DownloadsChanged();else ShowDownloads();
        return S_OK;});
    const HRESULT hr=view7->PrintToPdf(path.c_str(),nullptr,done);done->Release();
    if(FAILED(hr))Log(L"PDF PrintToPdf failed hr="+std::to_wstring((unsigned long)hr));
}
// ---- Split block of the tab menu (Paper p-7-0, "V2 · Bloc à plat, site nommé en en-tête") ----
// The block names its target once, in a read-only header: the page on screen (the active
// tab, or its group when split). No "last used" guess. "Avec un autre site ›" reaches every
// other tab or group, in strip order. Every row runs an existing split operation
// (ApplySplitTarget): split with a tab, insert into a group, replace a member.
struct SplitMenuAction{SV::Kind kind=SV::Kind::Split;std::shared_ptr<Tab> partner;int group=0;SV::Side side=SV::Side::Right;int index=0;};
struct SplitMenu{std::vector<CM::Item> items;std::vector<SplitMenuAction> actions;};
constexpr int SplitMenuBase=5000;
std::vector<CM::Item> lastSplitMenu;  // the last block shown (tests, diagnostics)
std::shared_ptr<const NativeChrome::FaviconImage> TabIcon(const std::shared_ptr<Tab>& t){return t?t->favicon.image:nullptr;}
// Short name plus, for homonyms, a second word: the URL's last path segment when that tells
// them apart ("GitHub  Issues"), otherwise their order in the strip ("GitHub  2").
std::pair<std::wstring,std::wstring> MenuName(const std::shared_ptr<Tab>& t){
    const std::wstring name=SiteLabel(t);std::vector<std::shared_ptr<Tab>> same;for(auto& p:pages)if(!p->closed&&SiteLabel(p)==name)same.push_back(p);
    if(same.size()<2)return {name,L""};
    auto word=[](const std::shared_ptr<Tab>& p){auto path=NativeChrome::SiteName::Parse(p->uri).path;const auto q=path.find_first_of(L"?#");if(q!=std::wstring::npos)path=path.substr(0,q);
        while(!path.empty()&&path.back()==L'/')path.pop_back();const auto slash=path.find_last_of(L'/');std::wstring w=slash==std::wstring::npos?path:path.substr(slash+1);
        if(w.size()>24)w=w.substr(0,23)+L"…";return NativeChrome::SiteName::Capitalize(w);};
    bool distinct=true;std::vector<std::wstring> words;for(auto& p:same){auto w=word(p);if(w.empty()||std::find(words.begin(),words.end(),w)!=words.end())distinct=false;words.push_back(w);}
    const int k=(int)(std::find(same.begin(),same.end(),t)-same.begin());return {name,distinct?words[k]:std::to_wstring(k+1)};
}
std::wstring MenuLabel(const std::shared_ptr<Tab>& t){auto n=MenuName(t);return n.second.empty()?n.first:n.first+L" "+n.second;}
// « 3ᵉ » / « 3rd »: an ordinal as each language writes it (in whole messages below).
std::wstring Ordinal(int n){
    if(I18n::Current()==I18n::Locale::En){const int t=n%100,u=n%10;const wchar_t* s=(t>=11&&t<=13)?L"th":u==1?L"st":u==2?L"nd":u==3?L"rd":L"th";return std::to_wstring(n)+s;}   // i18n-exempt: English ordinal suffixes
    return n==1?std::wstring(TR(ordinal_first_fr)):TRF(ordinal_fr,std::to_wstring(n));}
SplitView GroupView(int group){
    SplitView v;if(!group||!splitGroups.count(group))return v;v.group=group;v.members=Members(group);const auto o=splitGroups[group];v.min=SV::MinExtent(o,PageZoom());
    std::vector<std::uint64_t> recency;int active=0;for(size_t i=0;i<v.members.size();++i){v.weights.push_back(v.members[i]->weight);recency.push_back(v.members[i]->usedAt);if(v.members[i]->usedAt>v.members[active]->usedAt)active=(int)i;}
    v.active=active;v.a=SV::Arrange(ContentBox(),o,v.weights,recency,v.active,v.min);return v;
}
// Positions for one target, as menu rows. `with` names the target for assistive tech.
void SplitPositions(SplitMenu& m,std::vector<CM::Item>& out,const std::shared_ptr<Tab>& source,const std::shared_ptr<Tab>& partner,int group,const std::wstring& with){
    auto add=[&](std::vector<CM::Item>& into,const std::wstring& label,CM::Glyph glyph,bool enabled,const std::wstring& reason,SplitMenuAction a){
        CM::Item item=CM::Command(SplitMenuBase+(int)m.actions.size(),label,L"",enabled);item.glyph=glyph;
        item.accessible=enabled?TRF(split_menu_item_spoken,label,with):TRF(split_menu_item_spoken_unavailable,label,with,reason);item.tag=a.partner?TabIndexOf(a.partner)+1:-a.group;m.actions.push_back(a);into.push_back(item);};
    const auto c=ContentBox();
    if(partner){  // a tab on its own: the four sides
        const bool wide=SV::Fits(c.W(),SV::MinExtent(SV::Orientation::Row,PageZoom()),2),tall=SV::Fits(c.H(),SV::MinExtent(SV::Orientation::Column,PageZoom()),2);
        const std::wstring side=TR(split_menu_no_room_side),stack=TR(split_menu_no_room_stack);
        auto act=[&](SV::Side s){SplitMenuAction a;a.kind=SV::Kind::Split;a.partner=partner;a.side=s;return a;};
        add(out,TR(split_menu_right),CM::Glyph::Right,wide,side,act(SV::Side::Right));add(out,TR(split_menu_left),CM::Glyph::Left,wide,side,act(SV::Side::Left));if(!wide)out.push_back(CM::Note(side,false));
        add(out,TR(split_menu_top),CM::Glyph::Top,tall,stack,act(SV::Side::Top));add(out,TR(split_menu_bottom),CM::Glyph::Bottom,tall,stack,act(SV::Side::Bottom));if(!tall)out.push_back(CM::Note(stack,false));
        return;
    }
    auto v=GroupView(group);if(!v.group)return;const int n=(int)v.members.size();const bool row=v.a.orientation==SV::Orientation::Row;
    const bool fits=SV::Fits(SV::Extent(c,v.a.orientation),v.min,n+1);
    const std::wstring reason=TRF(split_menu_no_room_nth,Ordinal(n+1),row?L"360":L"256");
    auto ins=[&](int i){SplitMenuAction a;a.kind=SV::Kind::Insert;a.group=group;a.index=i;return a;};
    add(out,TR(split_menu_add_start),row?CM::Glyph::StartRow:CM::Glyph::StartColumn,fits,reason,ins(0));
    add(out,TR(split_menu_add_end),row?CM::Glyph::EndRow:CM::Glyph::EndColumn,fits,reason,ins(n));
    if(n>=2){CM::Item between=CM::Command(0,TR(split_menu_insert_between),L"",fits);between.accessible=fits?TRF(split_menu_item_spoken,TR(split_menu_insert_between),with):TRF(split_menu_item_spoken_unavailable,TR(split_menu_insert_between),with,reason);
        for(int i=1;i<n;++i)add(between.children,TRF(split_menu_between,MenuLabel(v.members[i-1]),MenuLabel(v.members[i])),CM::Glyph::None,fits,reason,ins(i));
        out.push_back(between);}
    if(!fits)out.push_back(CM::Note(reason,false));
    // Replacing is never implicit: its own row, a member list.
    CM::Item replace=CM::Command(0,TR(split_menu_replace));replace.accessible=TRF(split_menu_item_spoken,TR(split_menu_replace),with);
    for(int i=0;i<n;++i){if(v.members[i]==source)continue;SplitMenuAction a;a.kind=SV::Kind::Replace;a.group=group;a.index=i;
        CM::Item item=CM::Command(SplitMenuBase+(int)m.actions.size(),MenuName(v.members[i]).first);item.second=MenuName(v.members[i]).second;item.icons={TabIcon(v.members[i])};item.siteIcon=true;
        item.accessible=TRF(split_menu_replace_spoken,MenuLabel(v.members[i]),MenuLabel(source));item.tag=TabIndexOf(v.members[i])+1;m.actions.push_back(a);replace.children.push_back(item);}
    if(!replace.children.empty())out.push_back(replace);
}
std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> GroupIcons(int group){std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto& t:Members(group)){if(icons.size()==3)break;icons.push_back(TabIcon(t));}return icons;}
SplitMenu BuildSplitMenu(const std::shared_ptr<Tab>& source){
    SplitMenu m;int open=0;for(auto& t:pages)if(!t->closed)++open;if(!source||open<2)return m;  // one tab: no block
    const auto shown=Active();const int shownGroup=VisibleGroup();
    // Default target: the page on screen, unless it is the clicked tab (or its own group).
    std::shared_ptr<Tab> partner;int group=0;
    if(shownGroup){if(source->group!=shownGroup)group=shownGroup;}else if(shown&&shown!=source)partner=shown;
    if(partner||group){
        const std::wstring with=partner?TRF(split_menu_with,MenuLabel(partner)):TRP(split_menu_with_group,Members(group).size());
        m.items.push_back(partner?CM::Header(TRF(split_menu_with,MenuLabel(partner)),TR(split_menu_shown),{TabIcon(partner)},TabIndexOf(partner)+1)
                                 :CM::Header(TR(split_menu_with_split),TRP(split_menu_sites_second,Members(group).size()),GroupIcons(group),-group));
        SplitPositions(m,m.items,source,partner,group,with);
    }
    // Every other tab or group, in strip order (a group once, at its first member).
    CM::Item other=CM::Command(0,TR(split_menu_other_site));other.accessible=TR(split_menu_other_site);std::set<int> seen;
    for(auto& t:pages){
        if(t->closed||t==source||t==partner)continue;const int g=t->group&&splitGroups.count(t->group)&&Members(t->group).size()>=2?t->group:0;
        if(g){if(g==group||g==source->group||seen.count(g))continue;seen.insert(g);const int n=(int)Members(g).size();
            CM::Item entry=CM::Command(0,TR(split_menu_split_view));entry.second=TRP(split_menu_sites_second,n);entry.icons=GroupIcons(g);entry.accessible=TRP(split_menu_split_view_spoken,n);entry.tag=-g;
            SplitPositions(m,entry.children,source,nullptr,g,TRP(split_menu_with_group,n));if(!entry.children.empty())other.children.push_back(entry);continue;}
        auto name=MenuName(t);CM::Item entry=CM::Command(0,name.first);entry.second=name.second;entry.icons={TabIcon(t)};entry.siteIcon=true;entry.accessible=MenuLabel(t);entry.tag=TabIndexOf(t)+1;
        SplitPositions(m,entry.children,source,t,0,TRF(split_menu_with,MenuLabel(t)));other.children.push_back(entry);
    }
    // Generic commands and reason rows are flush with the menu's text, never behind an empty
    // glyph slot (Paper p-7-0 V2 realigned, p-3-0 flow); only position rows carry a glyph and its indent.
    other.glyph=CM::Glyph::None;
    if(!other.children.empty())m.items.push_back(other);
    return m;
}
void RunSplitMenuAction(const std::shared_ptr<Tab>& source,const SplitMenuAction& a){
    if(!source||source->closed)return;SV::Target t;t.kind=a.kind;t.index=a.index;
    if(a.kind==SV::Kind::Split){if(!a.partner||a.partner->closed)return;t.side=a.side;t.orientation=SV::OrientationOf(a.side);ApplySplitTarget(source,t,a.partner,CurrentSplit());return;}
    auto v=GroupView(a.group);if(!v.group)return;t.orientation=v.a.orientation;ApplySplitTarget(source,t,nullptr,v);
}
// Highlighting a row designates its target in the strip (1 DIP white 50 %) and reads it out.
// #127: the side column outlines the right-clicked tab too (Paper 2473-0 « 2 · Survol du menu »): `splitMenuSource`.
std::shared_ptr<Tab> splitMenuSource;
void SplitMenuHighlight(const CM::Item* item){
    std::vector<int> targets;
    if(item&&item->tag>0&&item->tag-1<(int)pages.size())targets.push_back(item->tag-1);
    else if(item&&item->tag<0)for(auto& t:Members(-item->tag))targets.push_back(TabIndexOf(t));
    const int source=targets.empty()?-1:TabIndexOf(splitMenuSource);
    if(targets!=chrome.menuTargets||source!=chrome.menuSource){chrome.menuTargets=targets;chrome.menuSource=source;InvalidateRect(window,nullptr,FALSE);}
    if(item&&!item->accessible.empty())AnnounceSplit(item->accessible);
}
// Tab right click (Paper "Clic droit menu — poli"): acts on the tab that was clicked.
// « Position des onglets » at the bottom of a tab's menu (Paper « Proposition — Clic droit · 3 icônes segmentées
// (validée) » 1XO9-0): a full-width segmented control, icons only (src/tabs-position-icon.h), the current choice
// highlighted, each named in a tooltip; a click changes the layout at once and closes the menu. Ids base .. base+2.
CM::Item TabsPositionMenu(int base){std::vector<CM::Segment> seg;
    for(int i=0;i<BrowserSettings::TabsPositionCount;++i){const auto p=static_cast<BrowserSettings::TabsPosition>(i);seg.push_back({base+i,BrowserSettings::Info(p).label,settings.tabsPosition==p});}
    return CM::Segmented(TR(settings_tabs_position),std::move(seg),[](ID2D1RenderTarget* t,ID2D1Factory* f,ID2D1SolidColorBrush* b,D2D1_RECT_F box,int i,int state){
        NativeChrome::TabsPositionIcon::Paint(t,f,b,box,NativeChrome::ContextMenu::Layout::segIcon,i,static_cast<NativeChrome::TabsPositionIcon::State>(state));});}
// #127 (Paper « Clic droit zone vide · barre du haut / panneau latéral / compact », maquette prête): a right-click on
// the tab bar, the side panel or the compact column anywhere but a tab (and the bookmarks, the bar's / grid's blank
// and the address, which keep their own menus) opens the short menu: « Nouvel onglet », « Rouvrir l'onglet fermé »,
// then the 3-view picker of the tab menu (same component, items, strings and commands).
enum EmptyAreaItem{EmptyNewTab=1,EmptyReopen=2,EmptyPosition=40};
std::vector<CM::Item> EmptyAreaMenuItems(){return {CM::Command(EmptyNewTab,TR(tab_new),L"Ctrl+T"),CM::Command(EmptyReopen,TR(cmd_reopen_tab),TR(key_ctrl_shift_t),!closedTabs.empty()&&pages.size()<128),CM::Separator(),TabsPositionMenu(EmptyPosition)};}
// Where the short menu answers (client DIP; `id` the region under the pointer): the top bar's tab row, the column
// (docked or over the page) — never a tab or its close, a bookmark tile, « » », the address or the grid's blank.
bool EmptyAreaMenuAt(int id,float x,float y){
    if(NativeChrome::TabIndex(id)>=0||NativeChrome::IsBookmark(id)||id=='O'||id=='L'||id==NativeChrome::Chrome::SidebarGridToggleId||id==NativeChrome::Chrome::SidebarManageId)return false;
    if(!chrome.sidebar)return y<44;   /* the tab row, above the toolbar (the former blank-strip threshold) */
    const auto& L=chrome.SideColumn();const float column=chrome.sidebarOverlay?L.column:chrome.side.column;
    if(x>=column)return false;if(!id&&chrome.bandShown&&L.gridBlock.Contains(x,y))return false;
    return true;}
void EmptyAreaMenu(int anchor){
    contextMenu.onSegment=[](RECT r,const std::wstring& tip,bool keyboard){if(tip.empty()){ChromeTipHide();return;}RECT bounds=NativeChrome::TooltipPlacement::VisibleRect(window);if(keyboard)chromeTip.Show(TipContent(tip.c_str()),r,&bounds);else chromeTip.Hover(TipContent(tip.c_str()),r,&bounds);};
    const int chosen=ShowMenu(EmptyAreaMenuItems(),anchor);contextMenu.onSegment=nullptr;ChromeTipHide();
    if(chosen==EmptyNewTab)Command('T');else if(chosen==EmptyReopen)Command('U');
    else if(chosen>=EmptyPosition&&chosen<EmptyPosition+BrowserSettings::TabsPositionCount)SetTabsPosition(static_cast<BrowserSettings::TabsPosition>(chosen-EmptyPosition));
}
void TabContextMenu(int tabIndex,int anchor){
    if(tabIndex<0||tabIndex>=(int)pages.size())return;
    enum{NewTabItem=1,ReloadItem,CopyItem,ReopenItem,CloseItem,CloseOthersItem,PrintItem,PdfItem,PlaceRight=20,PlaceLeft,PlaceTop,PlaceBottom,ReverseItem,OrientItem,ResizeItem,QuitItem,TabsPositionItem=40};
    auto tab=pages[tabIndex];const bool link=!tab->home&&!tab->uri.empty(),page=link&&tab->view.p;
    auto v=CurrentSplit();std::vector<CM::Item> split;
    if(v.group&&tab->group==v.group){
        const bool row=v.a.orientation==SV::Orientation::Row;const auto other=row?SV::Orientation::Column:SV::Orientation::Row;
        const bool fits=SV::Fits(SV::Extent(ContentBox(),other),SV::MinExtent(other,PageZoom()),(int)v.members.size());
        split={CM::Command(ReverseItem,TR(split_cmd_reverse)),CM::Command(OrientItem,(fits?std::wstring(row?TR(split_cmd_stack):TR(split_cmd_side_by_side)):(row?TRF(split_cmd_no_room,TR(split_cmd_stack)):TRF(split_cmd_no_room,TR(split_cmd_side_by_side)))),L"",fits),
               CM::Command(ResizeItem,TR(split_cmd_resize),L"",!v.a.separators.empty()),CM::Command(QuitItem,TR(split_cmd_quit))};
    }
    // V2 block: replaces the former four "Placer … de X" rows, same place, no shortcut.
    auto block=BuildSplitMenu(tab);
    std::vector<CM::Item> items{CM::Command(NewTabItem,TR(tab_new),L"Ctrl+T"),CM::Command(ReloadItem,TR(cmd_reload),L"Ctrl+R",tab->view.p!=nullptr),CM::Command(CopyItem,TR(chrome_tip_copy_link),L"",link),CM::Separator()};
    // A member of the shown group: its commands and "Avec un autre site ›" form one block (Paper 12b).
    if(!split.empty()){items.insert(items.end(),split.begin(),split.end());if(block.items.empty())items.push_back(CM::Separator());}
    if(!block.items.empty()){items.insert(items.end(),block.items.begin(),block.items.end());items.push_back(CM::Separator());}
    lastSplitMenu=block.items;
    const std::vector<CM::Item> rest{
        CM::Command(PrintItem,TR(cmd_print),L"Ctrl+P",page),CM::Command(PdfItem,TR(cmd_save_pdf),L"",page),CM::Separator(),
        CM::Command(ReopenItem,TR(cmd_reopen_tab),TR(key_ctrl_shift_t),!closedTabs.empty()&&pages.size()<128),CM::Separator(),
        CM::Command(CloseItem,TR(cmd_close_tab),L"Ctrl+W"),CM::Command(CloseOthersItem,TR(cmd_close_other_tabs),L"",pages.size()>1)};
    items.insert(items.end(),rest.begin(),rest.end());
    items.push_back(CM::Separator());items.push_back(TabsPositionMenu(TabsPositionItem));
    contextMenu.onSegment=[](RECT r,const std::wstring& tip,bool keyboard){if(tip.empty()){ChromeTipHide();return;}RECT bounds=NativeChrome::TooltipPlacement::VisibleRect(window);if(keyboard)chromeTip.Show(TipContent(tip.c_str()),r,&bounds);else chromeTip.Hover(TipContent(tip.c_str()),r,&bounds);};
    splitMenuSource=tab;contextMenu.onHighlight=SplitMenuHighlight;const int chosen=ShowMenu(items,anchor);contextMenu.onHighlight=nullptr;contextMenu.onSegment=nullptr;ChromeTipHide();SplitMenuHighlight(nullptr);splitMenuSource=nullptr;
    if(chosen>=SplitMenuBase&&chosen-SplitMenuBase<(int)block.actions.size()){RunSplitMenuAction(tab,block.actions[chosen-SplitMenuBase]);return;}
    switch(chosen){
    case PlaceRight:PlaceFromMenu(tab,SV::Side::Right);break;
    case PlaceLeft:PlaceFromMenu(tab,SV::Side::Left);break;
    case PlaceTop:PlaceFromMenu(tab,SV::Side::Top);break;
    case PlaceBottom:PlaceFromMenu(tab,SV::Side::Bottom);break;
    case ReverseItem:ReverseGroup(tab->group);break;
    case TabsPositionItem:case TabsPositionItem+1:case TabsPositionItem+2:SetTabsPosition(static_cast<BrowserSettings::TabsPosition>(chosen-TabsPositionItem));break;
    case OrientItem:if(splitGroups.count(tab->group)){auto& o=splitGroups[tab->group];o=o==SV::Orientation::Row?SV::Orientation::Column:SV::Orientation::Row;ApplyPages();AnnounceSplit(o==SV::Orientation::Row?TR(split_announce_side_by_side):TR(split_announce_stacked));}break;
    case ResizeItem:StartKeyboardResize();break;
    case QuitItem:QuitGroup(tab->group);break;
    case NewTabItem:Command('T');break;
    case ReloadItem:if(tabIndex==current)Command('R');else if(tab->view.p)CheckTab(tab->view->Reload(),L"Reload");break;
    case CopyItem:if(tabIndex==current)Command('C');else if(CopyToClipboard(tab->uri))AnnounceCopied();break;
    case ReopenItem:Command('U');break;
    // The preview draws inside the page: a background tab is brought forward first.
    case PrintItem:{auto it=std::find(pages.begin(),pages.end(),tab);if(it==pages.end())break;if(tab!=Active())Select((int)(it-pages.begin()));PrintPage(tab);break;}
    case PdfItem:SavePageAsPdf(tab);break;
    case CloseItem:{auto it=std::find(pages.begin(),pages.end(),tab);if(it!=pages.end())CloseAt((int)(it-pages.begin()));break;}
    case CloseOthersItem:for(int i=(int)pages.size()-1;i>=0;--i)if(pages[i]!=tab)CloseAt(i);break;
    default:break;
    }
}
// Address field right click: the EDIT's own commands in the native menu.
void AddressContextMenu(LPARAM at){
    enum{UndoItem=1,CutItem,CopyItem,PasteItem,DeleteItem,SelectAllItem};
    DWORD from=0,to=0;SendMessageW(address,EM_GETSEL,(WPARAM)&from,(LPARAM)&to);const bool selection=to>from;const int length=GetWindowTextLengthW(address);
    const std::vector<CM::Item> items{CM::Command(UndoItem,TR(common_undo),L"Ctrl+Z",SendMessageW(address,EM_CANUNDO,0,0)!=0),CM::Separator(),
        CM::Command(CutItem,TR(cmd_cut),L"Ctrl+X",selection),CM::Command(CopyItem,TR(cmd_copy),L"Ctrl+C",selection),CM::Command(PasteItem,TR(cmd_paste),L"Ctrl+V",IsClipboardFormatAvailable(CF_UNICODETEXT)!=0),CM::Command(DeleteItem,TR(common_delete),TR(key_delete),selection),CM::Separator(),
        CM::Command(SelectAllItem,TR(cmd_select_all),L"Ctrl+A",length>0&&!(from==0&&(int)to==length))};
    POINT p{GET_X_LPARAM(at),GET_Y_LPARAM(at)};RECT edit{};const bool keyboard=p.x==-1&&p.y==-1;
    if(keyboard){GetWindowRect(address,&edit);p={edit.left,edit.bottom};}
    const int cmd=contextMenu.Track(items,p,keyboard?&edit:nullptr,keyboard,motion.ReducedMotion());
    if(IsWindowVisible(address))SetFocus(address);
    switch(cmd){case UndoItem:SendMessageW(address,WM_UNDO,0,0);break;case CutItem:SendMessageW(address,WM_CUT,0,0);break;case CopyItem:SendMessageW(address,WM_COPY,0,0);break;case PasteItem:SendMessageW(address,WM_PASTE,0,0);break;case DeleteItem:SendMessageW(address,WM_CLEAR,0,0);break;case SelectAllItem:SendMessageW(address,EM_SETSEL,0,-1);break;default:break;}
}
// Web page right click: WebView2 hands over its menu (ContextMenuRequested); it is
// shown natively once the event returns (deferral), then the engine runs the choice.
constexpr UINT PageMenuMessage=WM_APP+22;
struct PendingPageMenu{Com<ICoreWebView2ContextMenuRequestedEventArgs> args;Com<ICoreWebView2Deferral> deferral;std::weak_ptr<Tab> tab;std::vector<CM::Item> items;std::vector<INT32> commands;POINT at{};bool keyboard=false;};
std::shared_ptr<PendingPageMenu> pendingPageMenu;
// Our own page-menu entries: ids far above the engine command indices (1..commands.size()).
constexpr int PagePrintItem=0x10000,PagePdfItem=0x10001;
void ReadPageMenu(ICoreWebView2ContextMenuItemCollection* list,std::vector<CM::Item>& out,std::vector<INT32>& commands){
    UINT32 count=0;if(!list||FAILED(list->get_Count(&count)))return;
    for(UINT32 i=0;i<count;++i){
        Com<ICoreWebView2ContextMenuItem> item;if(FAILED(list->GetValueAtIndex(i,&item.p))||!item.p)continue;
        COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND kind=COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND;item->get_Kind(&kind);
        if(kind==COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_SEPARATOR){if(!out.empty()&&!out.back().separator)out.push_back(CM::Separator());continue;}
        CM::Item m;LPWSTR text=nullptr;
        if(SUCCEEDED(item->get_Label(&text))&&text){m.label=text;CoTaskMemFree(text);}
        text=nullptr;if(SUCCEEDED(item->get_ShortcutKeyDescription(&text))&&text){m.shortcut=text;CoTaskMemFree(text);}
        BOOL flag=TRUE;item->get_IsEnabled(&flag);m.enabled=flag!=FALSE;
        flag=FALSE;item->get_IsChecked(&flag);m.checked=flag&&(kind==COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_CHECK_BOX||kind==COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_RADIO);
        if(kind==COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_SUBMENU){Com<ICoreWebView2ContextMenuItemCollection> children;if(SUCCEEDED(item->get_Children(&children.p))&&children.p)ReadPageMenu(children.p,m.children,commands);if(m.children.empty())continue;}
        else{INT32 command=0;item->get_CommandId(&command);commands.push_back(command);m.id=(int)commands.size();}
        text=nullptr;std::wstring name;if(SUCCEEDED(item->get_Name(&text))&&text){name=text;CoTaskMemFree(text);}
        out.push_back(std::move(m));
        if(name==L"print")out.push_back(CM::Command(PagePdfItem,TR(cmd_save_pdf))); // beside the engine's own print
    }
    while(!out.empty()&&out.back().separator)out.pop_back();
}
HRESULT PageContextMenu(const std::shared_ptr<Tab>& tab,ICoreWebView2ContextMenuRequestedEventArgs* args){
    if(halted||!tab||!args||pendingPageMenu||contextMenu.Open())return S_OK;
    if(tab->home){args->put_Handled(TRUE);return S_OK;} // NEWTAB: the frequent sites page has no menu (Ctrl/middle click still opens a tile in a new tab)
    auto pending=std::make_shared<PendingPageMenu>();
    Com<ICoreWebView2ContextMenuItemCollection> list;if(FAILED(args->get_MenuItems(&list.p))||!list.p)return S_OK;
    ReadPageMenu(list.p,pending->items,pending->commands);if(pending->items.empty())return S_OK; // nothing to show: the engine keeps its own menu
    // No engine print item (e.g. on a link or selection): our print group goes before the last group.
    auto& items=pending->items;std::function<bool(const std::vector<CM::Item>&)> has=[&](const std::vector<CM::Item>& v){for(auto& m:v)if(m.id==PagePdfItem||has(m.children))return true;return false;};
    if(!tab->home&&!has(items)){
        auto last=std::find_if(items.rbegin(),items.rend(),[](const CM::Item& m){return m.separator;});auto at=last==items.rend()?items.end():std::prev(last.base());
        items.insert(at,{CM::Separator(),CM::Command(PagePrintItem,TR(cmd_print)),CM::Command(PagePdfItem,TR(cmd_save_pdf))});
    }
    AnnotationMenuItems(tab,args,items); // « Annoter l'élément » / « Annoter la sélection »
    if(FAILED(args->GetDeferral(&pending->deferral.p))||!pending->deferral.p)return S_OK;
    args->put_Handled(TRUE);pending->args.set(args);pending->tab=tab;
    // Location is relative to the WebView, in the same pixels as the controller bounds.
    POINT location{};args->get_Location(&location);RECT bounds{};if(tab->controller.p)tab->controller->get_Bounds(&bounds);
    // A mouse right click opens at the pointer; the Menu key or Shift+F10 at the engine's location.
    pending->keyboard=GetAsyncKeyState(VK_APPS)<0||(GetAsyncKeyState(VK_F10)<0&&GetAsyncKeyState(VK_SHIFT)<0);
    if(pending->keyboard){pending->at={bounds.left+location.x,bounds.top+location.y};ClientToScreen(window,&pending->at);}else GetCursorPos(&pending->at);
    pendingPageMenu=pending;PostMessageW(window,PageMenuMessage,0,0);return S_OK;
}
void ShowPageMenu(){
    auto pending=std::move(pendingPageMenu);if(!pending)return;
    const int choice=halted?0:contextMenu.Track(pending->items,pending->at,nullptr,pending->keyboard,motion.ReducedMotion());
    if(pending->args.p)pending->args->put_SelectedCommandId(choice>0&&choice<=(int)pending->commands.size()?pending->commands[choice-1]:-1);
    if(pending->deferral.p)pending->deferral->Complete();
    // Keyboard focus goes back to the page the menu came from.
    if(auto tab=pending->tab.lock();tab&&!tab->closed&&tab->controller.p&&tab==Active())tab->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    // Our entries run once the engine has closed its menu (it got -1 for them).
    if(auto tab=pending->tab.lock();tab&&!tab->closed){if(choice==PagePrintItem)PrintPage(tab);else if(choice==PagePdfItem)SavePageAsPdf(tab);else AnnotationMenuChoice(tab,choice);}
}
#include "annotation-mode.h"

// ---- Bookmarks ----------------------------------------------------------------
// Real bookmarks (src/bookmarks.h): the star and Ctrl+D save the page at once, the
// popover only corrects; an optional band under the toolbar (settings
// "bookmarks-bar": auto = once it holds a bookmark); folders open as native menus;
// the "Favoris" drawer (Ctrl+Maj+O) finds and manages everything. Removing never
// asks: it is undone from the toast or Ctrl+Z. Stored in <data>\bookmarks (atomic
// replace); a damaged file is copied aside before any rewrite, and the last file
// read cleanly is kept as bookmarks.bak. Smoke runs neither read nor write it.
namespace BM=Bookmarks;
BM::Store bookmarks; BM::Id lastFolder=BM::BarRoot; bool bookmarksSaveFailed=false;
// What this process last read from or wrote to the shared file: the base of its next three-way
// merge (src/profile-store.h). Another window's changes arrive with every commit and on activation.
BM::Store bookmarksBase;
ProfileStore::Stamp bookmarksSeen; // PERF-08: the file as it was when `bookmarks` last equalled it
int popoverReturn=0; bool bookmarksDismissClick=false;
struct BandDrag { int index=-1; BM::Id id=0; POINT start{}; float grab=0; bool active=false; BM::Id parent=0; size_t from=0; BookmarkDrag::Drop drop; Workspace* owner=nullptr; } bandDrag;   // drop: where a release would land (bar or drawer)
// A bookmark dragged out of a folder/overflow menu or the drawer (src/bookmark-drag.h): stable id + the profile it
// belongs to; nothing moves until the drop, which is checked again (same profile shown, item still there, place valid).
bool qaDrawerStays=false;   // dev builds only, --qa-drawer-stays (see wWinMain)
bool qaDefaultCard=false;   // dev builds only, --qa-default-card: the Réglages default-browser card shown whatever Windows says
struct OuterDrag { BM::Id id=0; Workspace* owner=nullptr; bool active=false; BookmarkDrag::Drop drop; int source=0; } outerDrag;   // source: 1 menu, 2 drawer
void StartOuterDrag(BM::Id id,int source);
// Undo: each entry replays its steps backwards (16 entries, this session only).
struct BookmarkStep { enum Kind { Created, Removed, Moved, Renamed, Readdressed } kind; BM::Id id=0,parent=0; size_t index=0; std::wstring text; BM::Removed removed; };
struct BookmarkUndo { std::vector<BookmarkStep> steps; std::wstring done; };
std::vector<BookmarkUndo> bookmarkUndo;
std::wstring BookmarksFile(){return dataDir+L"\\bookmarks";}
void AnnounceBookmark(const std::wstring& text){Log(L"BOOKMARKS "+text);accessibility.Notify('K',text.c_str(),L"Bookmarks" /* i18n-exempt: UIA activity id */);}
void SaveBookmarks(){
    if(smoke)return;
    // Transaction under the profile lock: this window's changes (base → mine) replayed onto the file as
    // it is now, written atomically; the result (with the other windows' changes) becomes the state.
    // PERF-08: nothing changed here and the file was not replaced since it last equalled this state: nothing to merge.
    if(!bookmarksSaveFailed&&bookmarks==bookmarksBase&&ProfileStore::StampOf(BookmarksFile())==bookmarksSeen)return;
    ProfileStore::Lock lock(dataDir); /* held through the stamp below (the commit re-enters it: same thread) */
    std::map<BM::Id,BM::Id> remap;const BM::Store before=bookmarks;
    const auto r=ProfileStore::Commit<BM::Store>(dataDir,BookmarksFile(),bookmarksBase,bookmarks,[&remap](const BM::Store& b,const BM::Store& m,const BM::Store& t){return BM::Store::Merge(b,m,t,&remap);},lock.Held()?5000:0,{"browser-bookmarks 1"});
    bookmarksSeen=r==ProfileStore::Result::Unchanged?ProfileStore::StampOf(BookmarksFile()):ProfileStore::Stamp{};
    const bool ok=r==ProfileStore::Result::Written||r==ProfileStore::Result::Unchanged;
    if(!remap.empty()){bookmarkUndo.clear();Log(L"BOOKMARKS new ids remapped (another window used them)");}
    if(ok&&bookmarks!=before){if(!bookmarks.Find(lastFolder))lastFolder=BM::BarRoot;Defer([]{BookmarksChanged();});Log(L"BOOKMARKS merged with another window's changes");}
    if(ok){if(bookmarksSaveFailed){bookmarksSaveFailed=false;Log(L"BOOKMARKS saved again");}return;}
    if(r==ProfileStore::Result::Busy)Log(L"BOOKMARKS profile busy (lock timeout)");
    if(r==ProfileStore::Result::UnknownFormat)Log(L"BOOKMARKS file of an unknown format: not overwritten");
    // The data stays in memory; a sticky toast offers to try again.
    Log(L"BOOKMARKS save failed");bookmarksSaveFailed=true;
    if(bookmarkToast.Window())bookmarkToast.Show(TR(bookmark_save_failed),[]{SaveBookmarks();},motion.ReducedMotion(),TR(downloads_action_retry),true);
}
std::wstring TimeStamp(){SYSTEMTIME t;GetLocalTime(&t);wchar_t s[32];swprintf(s,32,L"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return s;}
void LoadBookmarks(){
    if(smoke)return;
    namespace fs=std::filesystem;const fs::path path(BookmarksFile()),backup(BookmarksFile()+L".bak");std::error_code ec;
    if(!fs::exists(path,ec)){
        bookmarksBase=BM::Store{};bookmarksSeen={}; // nothing on disk: everything in memory is new to the file
        // No file: first run, or a lost one; a clean backup wins over an empty start.
        if(fs::exists(backup,ec)){std::ifstream in(backup,std::ios::binary);bool damaged=false;auto restored=BM::Store::Read(in,&damaged);if(!damaged&&restored.Size()){bookmarks=std::move(restored);Log(L"BOOKMARKS restored from bookmarks.bak");SaveBookmarks();}}
        return;
    }
    bool damaged=false;{ProfileStore::Lock lock(dataDir);std::string bytes;ProfileStore::ReadAll(path.wstring(),bytes);std::istringstream in(bytes);bookmarks=BM::Store::Read(in,&damaged);} // read under the profile lock
    bookmarksBase=bookmarks;bookmarksSeen={}; // what is on disk now
    if(damaged){
        // Personal data, not a cache: keep the damaged file aside before anything rewrites it.
        const auto aside=fs::path(BookmarksFile()+L".damaged-"+TimeStamp());fs::copy_file(path,aside,fs::copy_options::overwrite_existing,ec);
        Log(L"BOOKMARKS file damaged: kept as "+aside.filename().wstring()+L", "+std::to_wstring(bookmarks.Size())+L" entries loaded");
        if(!bookmarks.Size()&&fs::exists(backup,ec)){std::ifstream in(backup,std::ios::binary);bool bad=false;auto restored=BM::Store::Read(in,&bad);if(!bad&&restored.Size()){bookmarks=std::move(restored);Log(L"BOOKMARKS restored from bookmarks.bak");}}
        SaveBookmarks();return;
    }
    fs::copy_file(path,backup,fs::copy_options::overwrite_existing,ec);
    Log(L"BOOKMARKS loaded "+std::to_wstring(bookmarks.Size()));
}
bool BandShown(){
    if(settings.bookmarksBar==BrowserSettings::BookmarksBar::Never)return false;
    return settings.bookmarksBar==BrowserSettings::BookmarksBar::Always||!bookmarks.Root(BM::BarRoot).children.empty();
}
bool BookmarkedSite(const std::wstring& site){
    if(site.empty())return false;
    for(BM::Id root:{BM::BarRoot,BM::OtherRoot})for(BM::Id top:bookmarks.Root(root).children)for(BM::Id id:bookmarks.Subtree(top)){auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Link&&AddressBar::SiteOf(n->url)==site)return true;}
    return false;
}
std::wstring ReadableUrl(const std::wstring& url){AddressBar::Parts p;if(AddressBar::Split(url,p)&&(p.scheme==L"http"||p.scheme==L"https"))return AddressBar::PercentDecode(AddressBar::MatchText(url));return AddressBar::PercentDecode(url);}
std::wstring FolderSummary(BM::Id id){const auto* n=bookmarks.Find(id);const size_t items=n?n->children.size():0;return TRP(bookmarks_folder_summary,items);}
// The star, the band and the chrome height follow the store and the active page.
// Returns true when the band appeared or disappeared (the page moves: Layout()).
bool SyncBookmarkChrome(){
    const bool shown=BandShown(),changed=shown!=chrome.bandShown;chrome.bandShown=shown;
    chrome.bandItems.clear();
    for(BM::Id id:bookmarks.Root(BM::BarRoot).children){const auto* n=bookmarks.Find(id);if(!n)continue;
        NativeChrome::Chrome::BandItem item;item.id=id;item.folder=n->kind==BM::Kind::Folder;item.title=BM::DisplayTitle(*n);
        if(item.folder)item.tip=item.title+L"\n"+FolderSummary(id);
        else{AddressBar::Parts p;item.local=AddressBar::Split(n->url,p)&&p.scheme==L"file";item.image=SiteIcon(AddressBar::SiteOf(n->url));item.tip=(item.title.empty()?ReadableUrl(n->url):item.title+L"\n"+ReadableUrl(n->url));}
        chrome.bandItems.push_back(std::move(item));}
    chrome.bandButtonForced=!bookmarks.Root(BM::OtherRoot).children.empty();
    auto t=Active();const bool page=t&&!t->home&&BM::Admissible(t->uri);
    chrome.SetEnabled('K',page);chrome.starred=page&&!bookmarks.Matching(t->uri).empty();
    if(chrome.bandRover>=(int)chrome.bandItems.size())chrome.bandRover=-1;
    return changed;
}
void BookmarksChanged(){
    const bool was=chrome.bandShown;BuildChrome();
    if(chrome.bandShown!=was)Layout();else{RefreshTools();InvalidateChrome();}
    bookmarksDrawer.Changed();
    if(accessibility.Listening())accessibility.ChildrenChanged(AccessibleBandKey);
    SyncFolderRename();
}
void PushUndo(std::vector<BookmarkStep> steps,std::wstring done){if(steps.empty())return;if(bookmarkUndo.size()==16)bookmarkUndo.erase(bookmarkUndo.begin());bookmarkUndo.push_back({std::move(steps),std::move(done)});}
std::wstring UniqueFolderName(BM::Id parent){
    std::wstring name=TR(bookmark_new_folder_name);int n=1;const auto* p=bookmarks.Find(parent);if(!p)return name;
    for(bool clash=true;clash&&n<1000;){clash=false;for(BM::Id c:p->children){auto* x=bookmarks.Find(c);if(x&&x->kind==BM::Kind::Folder&&x->title==name){clash=true;name=TRF(bookmark_new_folder_numbered,TR(bookmark_new_folder_name),std::to_wstring(++n));break;}}}
    return name;
}
// D3: a bookmark added or edited from a private window is a voluntary write: it goes through the same
// interprocess transaction as a normal window (no history, session or usage trace comes with it).
BM::Id NewFolder(BM::Id parent,size_t index=BM::End){
    const BM::Id id=bookmarks.AddFolder(parent,index,UniqueFolderName(parent),NativeChrome::DownloadsUI::UnixNowMs());
    if(!id){AnnounceBookmark(TR(bookmark_too_deep));return 0;}
    PushUndo({{BookmarkStep::Created,id}},TR(bookmark_undo_folder_created));SaveBookmarks();BookmarksChanged();AnnounceBookmark(TRF(bookmark_folder_created,bookmarks.Find(id)->title));return id;
}
void ShowBookmarkToast(const std::wstring& text){
    bookmarkToast.SetBottomReserve(UpdateToastHeight());
    bookmarkToast.Show(text,[]{PostMessageW(window,Shortcut,BookmarkUndoCommand,0);},motion.ReducedMotion());
}
void RemoveBookmark(BM::Id id){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return;
    const bool folder=n->kind==BM::Kind::Folder;const std::wstring title=BM::DisplayTitle(*n);const size_t links=folder?bookmarks.CountLinks(id):0;
    auto removed=bookmarks.Remove(id);if(!removed)return;
    BookmarkStep step;step.kind=BookmarkStep::Removed;step.id=id;step.removed=std::move(*removed);
    PushUndo({std::move(step)},folder?TR(bookmark_undo_folder_restored):TR(bookmark_undo_restored));SaveBookmarks();BookmarksChanged();
    const std::wstring text=folder?(links?TRP(bookmark_folder_deleted_links,links,title):TRF(bookmark_folder_deleted,title)):std::wstring(TR(bookmark_deleted));
    ShowBookmarkToast(text);AnnounceBookmark(TRF(bookmark_announce_undo_hint,text));
}
// Replays the newest entry backwards. Restoring a subtree whose folder is gone lands in "Autres favoris".
void UndoBookmark(){
    if(bookmarkUndo.empty()){AnnounceBookmark(TR(bookmark_nothing_to_undo));return;}
    auto entry=std::move(bookmarkUndo.back());bookmarkUndo.pop_back();std::wstring note=entry.done;
    for(auto it=entry.steps.rbegin();it!=entry.steps.rend();++it){auto& s=*it;
        switch(s.kind){
        case BookmarkStep::Created:bookmarks.Remove(s.id);break;
        case BookmarkStep::Removed:if(bookmarks.Restore(s.removed)){const auto* n=bookmarks.Find(s.id);if(n&&n->parent!=s.removed.parent)note=TRF(bookmark_undo_moved_to_other_folder,note);}else note=TR(bookmark_undo_failed);break;
        case BookmarkStep::Moved:bookmarks.Move(s.id,s.parent,s.index);break;
        case BookmarkStep::Renamed:bookmarks.Rename(s.id,s.text);break;
        case BookmarkStep::Readdressed:bookmarks.SetUrl(s.id,s.text);break;
        }}
    SaveBookmarks();BookmarksChanged();bookmarkToast.Dismiss();AnnounceBookmark(note);
}
std::vector<std::tuple<unsigned,int,std::wstring>> FolderChoices(BM::Id exclude){
    std::vector<std::tuple<unsigned,int,std::wstring>> out;
    for(BM::Id root:{BM::BarRoot,BM::OtherRoot}){std::vector<std::pair<BM::Id,int>> stack{{root,0}};
        while(!stack.empty()){auto [id,depth]=stack.back();stack.pop_back();const auto* n=bookmarks.Find(id);if(!n||n->kind!=BM::Kind::Folder)continue;
            if(exclude&&(id==exclude||bookmarks.IsAncestor(exclude,id)))continue;
            out.emplace_back(id,depth,n->title.empty()?std::wstring(TR(bookmarks_unnamed_folder)):n->title);for(auto c=n->children.rbegin();c!=n->children.rend();++c)stack.push_back({*c,depth+1});}}
    return out;
}
RECT ClientScreen(){RECT r{};GetClientRect(window,&r);MapWindowPoints(window,nullptr,(POINT*)&r,2);return r;}
// A popover from a column tool (#127): its bottom-left corner in screen pixels. Complete column: above the tools row,
// on the footer line's left; compact: at the column's right, level with the button's bottom. False in the top bar.
bool ColumnPopoverAt(int id,float gap,POINT& at){if(!chrome.sidebar||!window)return false;const RECT b=RegionScreen(id);if(IsRectEmpty(&b))return false;
    const auto& L=chrome.SideColumn();POINT o{0,0};ClientToScreen(window,&o);
    if(L.mode==NativeChrome::Sidebar::Mode::Complete)at={o.x+S(L.footerLine.left),b.top-S(gap)};else at={o.x+S(L.column+gap),b.bottom};return true;}
RECT RegionScreen(int id){FocusAnchor(L"region"); /* Mode Focus: whatever opens at a chrome region opens under the panel at rest */for(auto& r:chrome.regions)if(r.id==id){RECT p=ChromePx(r.rect);MapWindowPoints(window,nullptr,(POINT*)&p,2);return p;}return {};}
// The popover under `anchor` (screen); `returnTo`: chrome region keeping the keyboard after Escape (0 = page).
void EditBookmark(BM::Id id,RECT anchor,bool alignRight,bool keyboard,bool added=false,int returnTo=0){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return;
    NativeChrome::BookmarkPopover::Request r;r.id=id;r.folder=n->kind==BM::Kind::Folder;r.added=added;r.title=n->title;r.url=n->url;r.parent=n->parent;
    popoverReturn=returnTo;bookmarksDrawer.holding_=bookmarksDrawer.Visible();
    bookmarkPopover.Show(r,anchor,ClientScreen(),alignRight,keyboard,motion.ReducedMotion());
}
// ---- Folder rename in place (design/bookmark-inline-rename-plan.md) -------------------------------
// The folder's own label becomes a borderless EDIT (NativeUI::InlineRename) on the surface that showed it:
// the band item or the Favoris drawer row. Keyed by the bookmark id; cancelled when the folder, its label
// or the profile goes away; anything else (sub-folder in a native menu, behind », links) keeps the popover.
enum RenameSurface { RenameNone=0, RenameBand, RenameDrawer };
struct FolderRenameState { BM::Id id=0; int surface=RenameNone; bool keyboard=false; int returnTo=0; std::wstring profile; } folderRenameState;
NativeUI::InlineRename folderRename; HFONT folderRenameFont=nullptr;
bool FolderRenameFocus(){HWND f=GetFocus();return f&&folderRename.Owns(f);}
// The folder label's text box on `surface`, in that surface window's client pixels; false when not shown.
bool FolderLabelBox(BM::Id id,int surface,RECT& box){
    if(surface==RenameDrawer)return bookmarksDrawer.TitleBox(id,box); // only while the drawer is really shown
    // Fullscreen hides the chrome while the band's metadata stays filled: no band label is on screen then.
    // The content-only chrome (page fullscreen, F11) has no regions or band either.
    if(surface!=RenameBand||!chrome.bandShown||InFullscreen()||chrome.contentOnly)return false;
    for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i){if(chrome.bandItems[i].id!=id)continue;if(!chrome.bandItems[i].folder)return false;
        const auto b=chrome.BandTitleBox(i);if(b.right-b.left<24)return false;box=ChromePx(b);return true;}
    return false;
}
void MarkFolderRename(){
    const int was=chrome.bandRenaming;chrome.bandRenaming=-1;
    if(folderRename.Active()&&folderRenameState.surface==RenameBand)for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==folderRenameState.id)chrome.bandRenaming=i;
    bookmarksDrawer.SetRenaming(folderRename.Active()&&folderRenameState.surface==RenameDrawer?folderRenameState.id:0);
    if(was!=chrome.bandRenaming)InvalidateChrome();
}
void CommitFolderRename(BM::Id id,const std::wstring& typed,const std::wstring& profile){
    if(profile!=dataDir){Log(L"BOOKMARKS rename in place dropped: the window changed profile");return;}
    const auto* n=bookmarks.Find(id);
    if(!n||n->kind!=BM::Kind::Folder||BM::Store::IsRoot(id)){Log(L"BOOKMARKS rename in place dropped: the folder is gone");return;}
    const std::wstring title=AddressBar::Trim(typed),old=n->title;
    if(title.empty()){Log(L"BOOKMARKS rename in place: empty name, the folder keeps its name");return;}
    if(title==old||!bookmarks.Rename(id,title))return;
    PushUndo({{BookmarkStep::Renamed,id,0,0,old}},TR(bookmark_undo_edit));SaveBookmarks();BookmarksChanged();
    // Never « saved » when the file was not written: the sticky retry toast is already showing.
    AnnounceBookmark(bookmarksSaveFailed?TR(bookmark_save_failed):TR(bookmark_saved));
}
void FolderRenameDone(BM::Id id,bool committed,const std::wstring& text){
    const auto state=folderRenameState;folderRenameState={};MarkFolderRename();
    if(committed)CommitFolderRename(id,text,state.profile);else Log(L"BOOKMARKS rename in place cancelled");
    if(!state.keyboard)return;
    if(state.surface==RenameBand&&GetFocus()==window){
        int region=0;for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==id)region=NativeChrome::BookmarkBase+i;
        if(region){keyboardModality=true;keyboardFocus=region;chrome.bandRover=NativeChrome::BookmarkIndex(region);InvalidateChrome();SyncAccessibility();}
    }
    if(state.surface==RenameDrawer&&bookmarksDrawer.Visible()&&GetFocus()==bookmarksDrawer.Window())bookmarksDrawer.FocusBookmark(id);
}
// The label's font at the host's current DPI: Inter Display Medium, 14 DIP on the band, 16 DIP in the drawer.
int FolderRenameFontPx(int surface,HWND host){const float size=surface==RenameBand?NativeChrome::Design::Type::label.size:NativeChrome::Downloads::Panel::line;return (int)std::lround(size*GetDpiForWindow(host)/96.f);}
HFONT FolderRenameFont(int surface,HWND host){return CreateFontW(-FolderRenameFontPx(surface,host),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display" /* i18n-exempt: font family */);}
// Starts the in-place editor on the folder's label on `surface`; false (caller keeps the popover) when the
// item is not a renamable folder or its label is not on that surface now.
bool StartFolderRename(BM::Id id,int surface,bool keyboard,int returnTo){
    const auto* n=bookmarks.Find(id);if(!n||n->kind!=BM::Kind::Folder||BM::Store::IsRoot(id))return false;
    RECT box{};if(!FolderLabelBox(id,surface,box))return false;
    HWND host=surface==RenameBand?window:bookmarksDrawer.Window();if(!host)return false;
    namespace D=NativeChrome::Design;
    HFONT font=FolderRenameFont(surface,host);
    NativeUI::InlineRename::Style style;style.font=font;style.text=D::NativeGray(D::Text::primary);
    style.back=D::NativeGray(surface==RenameBand?chrome.BandSurface(1,0):D::Surface::canvas);
    folderRename.Cancel(); // one editor at a time (an earlier one reports its cancel first)
    folderRenameState={id,surface,keyboard,returnTo,dataDir};
    const bool ok=folderRename.Begin(host,id,box,n->title,style,TR(bookmark_field_name),[](unsigned long long done,bool committed,const std::wstring& text){FolderRenameDone((BM::Id)done,committed,text);});
    if(folderRenameFont)DeleteObject(folderRenameFont);folderRenameFont=font;
    if(!ok){folderRenameState={};return false;}
    MarkFolderRename();Log(L"BOOKMARKS rename in place");return true;
}
// After any change of the store, the layout or the drawer's scroll: follow the label or cancel.
void SyncFolderRename(){
    if(!folderRename.Active())return;
    const auto& s=folderRenameState;const auto* n=bookmarks.Find(s.id);RECT box{};
    if(s.profile!=dataDir||!n||n->kind!=BM::Kind::Folder||!FolderLabelBox(s.id,s.surface,box)){folderRename.Cancel();return;}
    MarkFolderRename();
    // The host's DPI changed (another monitor): same editor, new font size; text, selection and caret kept.
    HWND host=folderRename.Host();LOGFONTW current{};
    if(host&&folderRenameFont&&GetObjectW(folderRenameFont,sizeof(current),&current)&&-current.lfHeight!=FolderRenameFontPx(s.surface,host)){
        if(HFONT next=FolderRenameFont(s.surface,host)){HFONT old=folderRenameFont;folderRename.SetFont(next);folderRenameFont=next;DeleteObject(old);Log(L"BOOKMARKS rename in place: font follows the new DPI");}
    }
    const RECT was=folderRename.Box();if(!EqualRect(&was,&box))folderRename.Move(box);
}
// Rename / F2 / new folder: in place on `surface` when the folder's label is there, else the popover.
void RenameOrEdit(BM::Id id,int surface,RECT anchor,bool keyboard,int returnTo){
    if(StartFolderRename(id,surface,keyboard,returnTo))return;
    EditBookmark(id,anchor,false,keyboard,false,returnTo);
}
int bookmarkMenuSurface=RenameNone; // the surface whose row opened BookmarkContextMenu (drawer), else band/none
// Ctrl+D / the star: save at once in the last folder used, then the popover; an
// already saved page opens its most recent bookmark instead of a second one.
void BookmarkStar(bool keyboard){
    auto t=Active();if(!t||t->home||!BM::Admissible(t->uri))return;
    HideSuggestions();
    const auto matches=bookmarks.Matching(t->uri);const RECT star=RegionScreen('K');
    if(!matches.empty()){EditBookmark(matches.front(),star,true,keyboard,false,keyboard?'K':0);return;}
    if(!bookmarks.Find(lastFolder)||bookmarks.Find(lastFolder)->kind!=BM::Kind::Folder)lastFolder=BM::BarRoot;
    std::wstring title=t->title==TR(tab_new)?L"":t->title;
    const BM::Id id=bookmarks.Add(lastFolder,BM::End,title,t->uri,NativeChrome::DownloadsUI::UnixNowMs());
    if(!id){AnnounceBookmark(TR(bookmark_add_failed));return;}
    PushUndo({{BookmarkStep::Created,id}},TR(bookmark_undo_add));SaveBookmarks();BookmarksChanged();
    AnnounceBookmark(TRF(bookmark_added_to,bookmarks.Find(lastFolder)->title));
    EditBookmark(id,RegionScreen('K'),true,keyboard,true,keyboard?'K':0);
}
void CommitBookmark(unsigned id,const std::wstring& title,const std::wstring& url,unsigned parent){
    const auto* n=bookmarks.Find(id);if(!n)return;std::vector<BookmarkStep> steps;
    const std::wstring oldTitle=n->title,oldUrl=n->url;const BM::Id oldParent=n->parent;const size_t oldIndex=bookmarks.IndexOf(id);
    if(title!=oldTitle&&bookmarks.Rename(id,title))steps.push_back({BookmarkStep::Renamed,id,0,0,oldTitle});
    if(!url.empty()&&url!=oldUrl&&bookmarks.SetUrl(id,url))steps.push_back({BookmarkStep::Readdressed,id,0,0,oldUrl});
    if(parent&&parent!=oldParent){if(bookmarks.Move(id,parent,BM::End)){steps.push_back({BookmarkStep::Moved,id,oldParent,oldIndex});lastFolder=parent;}else AnnounceBookmark(TR(bookmark_too_deep));}
    else if(parent)lastFolder=parent;
    if(steps.empty())return;
    PushUndo(std::move(steps),TR(bookmark_undo_edit));SaveBookmarks();BookmarksChanged();AnnounceBookmark(TR(bookmark_saved));
}
void PopoverClosed(bool keyboard){
    bookmarksDrawer.holding_=false;
    if(bookmarksDrawer.Visible()){SetForegroundWindow(bookmarksDrawer.Window());SetFocus(bookmarksDrawer.Window());return;}
    if(!keyboard)return;
    if(popoverReturn){SetForegroundWindow(window);SetFocus(window);keyboardModality=true;keyboardFocus=popoverReturn;InvalidateChrome();SyncAccessibility();}else FocusPage();
}
void OpenBookmarkUrl(const std::wstring& url,int mode){ // 0 here, 1 background tab, 2 foreground tab
    if(halted||url.empty()||!BM::Admissible(url))return;
    Log(L"BOOKMARKS open "+url+(mode==1?L" (background tab)":mode==2?L" (new tab)":L""));
    auto t=Active();
    if(mode==0&&t&&t->view.p){Navigate(t,url);FocusPage();return;}
    if(pages.size()>=128)return;
    const int previous=current;NewTab(url,current+1);if(mode==1&&previous>=0&&previous<(int)pages.size())Select(previous);
}
void OpenUrls(const std::vector<std::wstring>& urls);
void OpenAllIn(BM::Id folder){
    std::vector<std::wstring> urls;for(BM::Id id:bookmarks.Subtree(folder)){auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Link)urls.push_back(n->url);}
    if(urls.empty())return;
    if(urls.size()>20){AskModal(MD::OpenManyTabsSpec((int)urls.size()),nullptr,[urls](const MD::Result& r){if(r.button==MD::AcceptButton)OpenUrls(urls);});return;}
    OpenUrls(urls);
}
void OpenUrls(const std::vector<std::wstring>& urls){
    const int previous=current;int at=current;for(auto& u:urls){if(pages.size()>=128)break;NewTab(u,++at);}
    if(previous>=0&&previous<(int)pages.size())Select(previous);
    AnnounceBookmark(TRP(bookmark_opened_in_tabs,urls.size()));
}
// Menu ids: kind << 28 | bookmark id.
enum MenuKind { MenuOpen=1, MenuOpenAll, MenuAddHere, MenuManage, MenuMoveTo, MenuNewFolder };
int MenuId(MenuKind k,BM::Id id){return (int)(((unsigned)k<<28)|(id&0x0FFFFFFF));}
MenuKind MenuKindOf(int v){return (MenuKind)((unsigned)v>>28);}
BM::Id MenuBookmark(int v){return (BM::Id)((unsigned)v&0x0FFFFFFF);}
// A folder's contents as menu rows: "Ouvrir les N favoris" first, subfolders as submenus.
std::vector<CM::Item> BookmarkFolderItems(BM::Id folder,int depth=0){
    std::vector<CM::Item> items;const auto* f=bookmarks.Find(folder);if(!f)return items;
    const size_t links=bookmarks.CountLinks(folder);
    if(links){auto all=CM::Command(MenuId(MenuOpenAll,folder),TRP(bookmark_open_all,links));all.icon=L"open-out";items.push_back(all);items.push_back(CM::Separator());}
    for(BM::Id id:f->children){const auto* n=bookmarks.Find(id);if(!n)continue;
        if(n->kind==BM::Kind::Folder){CM::Item sub;sub.id=MenuId(MenuOpen,id);sub.label=NativeUI::EscapeMenuText(n->title.empty()?std::wstring(TR(bookmarks_unnamed_folder)):n->title);sub.icon=L"folder-closed";sub.help=FolderSummary(id);
            if(depth<(int)BM::Store::MaxDepth)sub.children=BookmarkFolderItems(id,depth+1);if(sub.children.empty())sub.children.push_back(CM::Command(0,TR(bookmarks_menu_empty),L"",false));items.push_back(sub);}
        else{auto item=CM::Command(MenuId(MenuOpen,id),NativeUI::EscapeMenuText(BM::DisplayTitle(*n)));item.image=SiteIcon(AddressBar::SiteOf(n->url));item.icon=L"favicon";item.help=ReadableUrl(n->url);items.push_back(item);}}
    if(f->children.empty()){items.push_back(CM::Command(0,TR(bookmarks_menu_empty),L"",false));}
    auto t=Active();if(f->children.empty()&&t&&!t->home&&BM::Admissible(t->uri)){auto add=CM::Command(MenuId(MenuAddHere,folder),TR(bookmark_add_here));add.icon=L"star";items.push_back(add);}
    return items;
}
void BookmarkContextMenu(BM::Id id,POINT at,const RECT* anchor,bool keyboard);
// Runs a bookmark menu (folder, overflow) under `region`; handles the gesture it returns.
void RunBookmarkMenu(const std::vector<CM::Item>& items,int region,bool keyboard){
    RECT anchor=RegionScreen(region);chrome.bandOpen=region;InvalidateChrome();UpdateWindow(window);
    if(accessibility.Listening())accessibility.Raise(AccessibleBandItemKey(region),UIA_ExpandCollapseExpandCollapseStatePropertyId);
    POINT p{anchor.left,anchor.bottom};
    const auto choice=contextMenu.TrackChoice(items,p,&anchor,keyboard,motion.ReducedMotion(),false,[](int v){if(MenuKindOf(v)==MenuOpen){auto* n=bookmarks.Find(MenuBookmark(v));if(n&&n->kind==BM::Kind::Link)OpenBookmarkUrl(n->url,1);}},
        [](int v){return MenuKindOf(v)==MenuOpen&&!BM::Store::IsRoot(MenuBookmark(v))&&bookmarks.Find(MenuBookmark(v))!=nullptr;});   // opt-in drag: bookmark rows only
    chrome.bandOpen=0;InvalidateChrome();
    if(choice.drag){StartOuterDrag(MenuBookmark(choice.drag),1);return;}
    if(keyboard&&region){SetFocus(window);keyboardFocus=region;keyboardModality=true;InvalidateChrome();}
    if(!choice.id)return;
    const BM::Id id=MenuBookmark(choice.id);
    if(choice.context){POINT c{};GetCursorPos(&c);BookmarkContextMenu(id,c,nullptr,false);return;}
    switch(MenuKindOf(choice.id)){
    case MenuOpen:{auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Link)OpenBookmarkUrl(n->url,choice.shift?2:choice.ctrl?1:0);break;}
    case MenuOpenAll:keyboardModality=choice.keyboard;OpenAllIn(id);break; // the > 20 tabs confirmation's ring follows how « Ouvrir tout » was picked
    case MenuAddHere:{lastFolder=id;BookmarkStar(keyboard);break;}
    case MenuManage:ToggleBookmarksDrawer(keyboard);break;
    default:break;
    }
}
void OverflowMenu(bool keyboard){
    std::vector<CM::Item> items;const auto& bar=bookmarks.Root(BM::BarRoot).children;
    for(size_t i=(size_t)chrome.band.visible;i<bar.size();++i){const auto* n=bookmarks.Find(bar[i]);if(!n)continue;
        if(n->kind==BM::Kind::Folder){CM::Item sub;sub.id=MenuId(MenuOpen,n->id);sub.label=NativeUI::EscapeMenuText(n->title.empty()?std::wstring(TR(bookmarks_unnamed_folder)):n->title);sub.icon=L"folder-closed";sub.help=FolderSummary(n->id);sub.children=BookmarkFolderItems(n->id,1);items.push_back(sub);}
        else{auto item=CM::Command(MenuId(MenuOpen,n->id),NativeUI::EscapeMenuText(BM::DisplayTitle(*n)));item.image=SiteIcon(AddressBar::SiteOf(n->url));item.icon=L"favicon";item.help=ReadableUrl(n->url);items.push_back(item);}}
    if(!bookmarks.Root(BM::OtherRoot).children.empty()){if(!items.empty())items.push_back(CM::Separator());CM::Item other;other.id=MenuId(MenuOpen,BM::OtherRoot);other.label=TR(bookmarks_other_root);other.icon=L"folder-closed";other.children=BookmarkFolderItems(BM::OtherRoot,1);items.push_back(other);}
    if(!items.empty())items.push_back(CM::Separator());
    auto manage=CM::Command(MenuId(MenuManage,0),TR(bookmarks_manage),TR(key_ctrl_shift_o));manage.icon=L"star";items.push_back(manage);
    RunBookmarkMenu(items,'O',keyboard);
}
void FolderMenu(int index,bool keyboard){
    if(index<0||index>=(int)chrome.bandItems.size())return;
    RunBookmarkMenu(BookmarkFolderItems(chrome.bandItems[index].id),NativeChrome::BookmarkBase+index,keyboard);
}
std::wstring BandToggleLabel(){return BandShown()?TR(bookmarks_bar_hide):TR(bookmarks_bar_show);}
void ToggleBookmarkBar(){
    settings.bookmarksBar=BandShown()?BrowserSettings::BookmarksBar::Never:BrowserSettings::BookmarksBar::Always;
    SettingChanged(BrowserSettings::Change::BookmarksBar);
}
BM::Id DuplicateBookmark(BM::Id id){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return 0;
    const auto now=NativeChrome::DownloadsUI::UnixNowMs();const size_t at=bookmarks.IndexOf(id)+1;
    BM::Id copy=n->kind==BM::Kind::Folder?bookmarks.AddFolder(n->parent,at,n->title,now):bookmarks.Add(n->parent,at,n->title,n->url,now);
    if(!copy)return 0;
    if(n->kind==BM::Kind::Folder){
        // Children copied in display order with an explicit stack (bounded by the store's depth).
        std::vector<std::pair<BM::Id,BM::Id>> stack{{id,copy}};
        while(!stack.empty()){auto [from,to]=stack.back();stack.pop_back();const auto* f=bookmarks.Find(from);if(!f)continue;const auto children=f->children;
            for(BM::Id c:children){const auto* x=bookmarks.Find(c);if(!x)continue;BM::Id made=x->kind==BM::Kind::Folder?bookmarks.AddFolder(to,BM::End,x->title,now):bookmarks.Add(to,BM::End,x->title,x->url,now);if(made&&x->kind==BM::Kind::Folder)stack.push_back({c,made});}}
    }
    PushUndo({{BookmarkStep::Created,copy}},TR(bookmark_undo_copy));SaveBookmarks();BookmarksChanged();AnnounceBookmark(TR(bookmark_copy_inserted));return copy;
}
void MoveBookmarkTo(BM::Id id,BM::Id parent){
    const auto* n=bookmarks.Find(id);if(!n)return;const BM::Id oldParent=n->parent;const size_t oldIndex=bookmarks.IndexOf(id);
    if(!bookmarks.Move(id,parent,BM::End)){AnnounceBookmark(TR(bookmark_move_failed));return;}
    PushUndo({{BookmarkStep::Moved,id,oldParent,oldIndex}},TR(bookmark_undo_move));SaveBookmarks();BookmarksChanged();AnnounceBookmark(TRF(bookmark_moved_into,bookmarks.Find(parent)->title));
}
// One step within its folder (Ctrl+Maj+←/→ on the band, Alt+↑/↓ in the drawer).
void ShiftBookmark(BM::Id id,int delta){
    const auto* n=bookmarks.Find(id);if(!n)return;const BM::Id parent=n->parent;const size_t index=bookmarks.IndexOf(id);
    const size_t count=bookmarks.Find(parent)->children.size();const long long to=(long long)index+delta;if(to<0||to>=(long long)count)return;
    if(!bookmarks.Move(id,parent,(size_t)to))return;
    PushUndo({{BookmarkStep::Moved,id,parent,index}},TR(bookmark_undo_move));SaveBookmarks();BookmarksChanged();
    AnnounceBookmark(TRF(bookmark_moved_position,std::to_wstring(to+1),std::to_wstring(count)));
}
std::vector<CM::Item> MoveToItems(BM::Id id){
    std::vector<CM::Item> items;const auto* n=bookmarks.Find(id);
    for(auto& [folder,depth,name]:FolderChoices(n&&n->kind==BM::Kind::Folder?id:0)){auto item=CM::Command(MenuId(MenuMoveTo,folder),std::wstring((size_t)depth*2,L' ')+NativeUI::EscapeMenuText(name),L"",n&&folder!=n->parent,n&&folder==n->parent);item.icon=L"folder-closed";items.push_back(item);}
    return items;
}
// A band item's (or menu row's, or drawer row's) own menu, in the order fixed in Paper (c-s207a4).
// « Placer le contenu dans la barre de favoris » (folder menu): the folder's entries go, same ids and order, to the end
// of the bar in ONE profile-store transaction, applied on the merged tree under the lock (src/bookmark-promote.h; an
// outside move + remove lost the moved children in the merge, review 9ef33c5 P1). The emptied folder is removed only
// when the import folder map names that very folder (ImportOwnedFolder); otherwise it stays, empty, as the person's.
// One undo step from what was committed; a failed save writes nothing and says so.
bool ImportOwnedFolder(const BM::Store& s,BM::Id id);
void PromoteToBar(BM::Id id){
    const auto* f=bookmarks.Find(id);if(smoke||!f||f->kind!=BM::Kind::Folder||BM::Store::IsRoot(id)||f->children.empty())return;
    const auto r=BookmarkPromote::Commit(dataDir,BookmarksFile(),bookmarksBase,bookmarks,id,[](const BM::Store& s,BM::Id folder){return ImportOwnedFolder(s,folder);});
    if(!r.remap.empty())bookmarkUndo.clear();
    if(!r.Saved()){Log(L"BOOKMARKS promote to bar: save failed, nothing changed");if(bookmarkToast.Window())bookmarkToast.Show(TR(bookmark_save_failed),[id]{PromoteToBar(id);},motion.ReducedMotion(),TR(downloads_action_retry),true);return;}
    BookmarksChanged();
    if(r.refused){Log(L"BOOKMARKS promote to bar: the folder changed in another window, nothing moved");const std::wstring t=TR(bookmark_promote_folder_changed);if(bookmarkToast.Window()){bookmarkToast.SetBottomReserve(UpdateToastHeight());bookmarkToast.Show(t,[]{},motion.ReducedMotion(),TR(common_ok));}AnnounceBookmark(t);return;}
    if(r.moved.empty())return;
    std::vector<BookmarkStep> steps;for(const auto& m:r.moved)steps.push_back({BookmarkStep::Moved,m.id,m.parent,m.index});   // committed ids (after remap)
    if(r.removed){BookmarkStep st;st.kind=BookmarkStep::Removed;st.id=r.folder;st.removed=*r.removed;steps.push_back(std::move(st));}   // undone first: the folder comes back before its children
    PushUndo(std::move(steps),TR(bookmark_undo_move));
    const std::wstring text=TRP(bookmark_promoted_to_bar,r.moved.size());ShowBookmarkToast(text);AnnounceBookmark(TRF(bookmark_announce_undo_hint,text));
}
void BookmarkContextMenu(BM::Id id,POINT at,const RECT* anchor,bool keyboard){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return;const bool folder=n->kind==BM::Kind::Folder;
    enum{OpenTab=1,OpenAll,Edit,Duplicate,Copy,Delete,NewFolderItem,AddHere,ToggleBar,Promote};
    std::vector<CM::Item> items;
    if(folder){const size_t links=bookmarks.CountLinks(id);auto all=CM::Command(OpenAll,TRP(bookmark_open_all,links),L"",links>0);items.push_back(all);}
    else items.push_back(CM::Command(OpenTab,TR(history_open_new_tab)));
    items.push_back(CM::Separator());
    items.push_back(CM::Command(Edit,folder?TR(cmd_rename):TR(cmd_edit),L"F2"));
    items.push_back(CM::Command(Duplicate,TR(cmd_duplicate)));
    if(!folder)items.push_back(CM::Command(Copy,TR(chrome_tip_copy_link)));
    items.push_back(CM::Separator());
    auto del=CM::Command(Delete,TR(common_delete),TR(key_delete));del.danger=true;items.push_back(del);
    items.push_back(CM::Separator());
    items.push_back(CM::Command(NewFolderItem,TR(bookmark_new_folder_menu)));
    if(folder){auto t=Active();items.push_back(CM::Command(AddHere,TR(bookmark_add_here),L"",t&&!t->home&&BM::Admissible(t->uri)));}
    CM::Item move;move.label=TR(cmd_move_to);move.children=MoveToItems(id);if(move.children.empty())move.enabled=false;items.push_back(move);
    if(folder&&n->parent!=BM::BarRoot)items.push_back(CM::Command(Promote,TR(bookmark_promote_to_bar),L"",!n->children.empty()));
    items.push_back(CM::Command(ToggleBar,BandToggleLabel(),TR(key_ctrl_shift_b)));
    const int region=[&]{for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==id)return NativeChrome::BookmarkBase+i;return 0;}();
    if(region){chrome.bandOpen=region;InvalidateChrome();UpdateWindow(window);}
    bookmarksDrawer.holding_=bookmarksDrawer.Visible();
    const int cmd=contextMenu.Track(items,at,anchor,keyboard,motion.ReducedMotion());
    bookmarksDrawer.holding_=false;chrome.bandOpen=0;InvalidateChrome();
    if(!bookmarks.Find(id))return;
    const RECT edit=region?RegionScreen(region):anchor?*anchor:RECT{at.x,at.y,at.x,at.y};
    const int surface=region?RenameBand:bookmarkMenuSurface; // a row of a native folder menu: none (popover)
    if(MenuKindOf(cmd)==MenuMoveTo){MoveBookmarkTo(id,MenuBookmark(cmd));return;}
    switch(cmd){
    case OpenTab:OpenBookmarkUrl(n->url,2);break;
    case OpenAll:keyboardModality=contextMenu.LastChoiceKeyboard();OpenAllIn(id);break;
    case Edit:if(folder)RenameOrEdit(id,surface,edit,keyboard,keyboard?region:0);else EditBookmark(id,edit,false,keyboard,false,keyboard?region:0);break;
    case Duplicate:DuplicateBookmark(id);break;
    case Copy:if(CopyToClipboard(n->url))AnnounceBookmark(TR(link_copied));break;
    case Delete:RemoveBookmark(id);break;
    case NewFolderItem:{const BM::Id parent=folder?id:n->parent;const size_t at2=folder?BM::End:bookmarks.IndexOf(id)+1;if(BM::Id made=NewFolder(parent,at2))RenameOrEdit(made,surface,edit,keyboard,keyboard?region:0);break;}
    case AddHere:lastFolder=id;BookmarkStar(keyboard);break;
    case ToggleBar:ToggleBookmarkBar();break;
    case Promote:PromoteToBar(id);break;
    default:break;
    }
}
// Right click on the band outside an item.
void BandContextMenu(POINT at){
    enum{AddPage=1,NewFolderItem,ToggleBar};auto t=Active();
    const std::vector<CM::Item> items{CM::Command(AddPage,TR(bookmark_add_page_cmd),L"Ctrl+D",t&&!t->home&&BM::Admissible(t->uri)),CM::Command(NewFolderItem,TR(bookmark_new_folder_menu)),CM::Separator(),CM::Command(ToggleBar,BandToggleLabel(),TR(key_ctrl_shift_b))};
    switch(contextMenu.Track(items,at,nullptr,false,motion.ReducedMotion())){
    case AddPage:lastFolder=BM::BarRoot;BookmarkStar(false);break;
    case NewFolderItem:if(BM::Id made=NewFolder(BM::BarRoot)){RECT r=RegionScreen(NativeChrome::BookmarkBase+(int)bookmarks.IndexOf(made));if(r.right<=r.left)r={at.x,at.y,at.x,at.y};RenameOrEdit(made,RenameBand,r,false,0);}break;
    case ToggleBar:ToggleBookmarkBar();break;
    default:break;
    }
}
// Band item activation: a link opens (Ctrl / middle: background tab, Shift: new tab), a folder opens its menu.
void BandActivate(int index,bool ctrl,bool shift,bool keyboard){
    if(index<0||index>=(int)chrome.bandItems.size())return;chrome.bandRover=index;
    const auto& item=chrome.bandItems[index];
    if(item.folder){FolderMenu(index,keyboard);return;}
    if(const auto* n=bookmarks.Find(item.id))OpenBookmarkUrl(n->url,shift?2:ctrl?1:0);
}
void ShowBookmarksDrawer(bool keyboard){
    if(!bookmarksDrawer.Window())return;
    FocusAnchor(L"bookmarks"); // Mode Focus: the panel at rest above the drawer
    CloseAudioDrawer();HideSuggestions();CloseDownloadSurfaces();settingsDrawer.Hide(false,true);historyDrawer.Hide(false,true);
    bookmarksDrawer.Show(keyboard);SyncUpdateToastReserve();
}
void ToggleBookmarksDrawer(bool keyboard){if(bookmarksDrawer.Open()){bookmarksDrawer.Hide(keyboard,true);return;}ShowBookmarksDrawer(keyboard);}
void CreateBookmarkSurfaces(){
    NativeChrome::BookmarkPopover::Hooks p;
    p.commit=CommitBookmark;p.remove=[](unsigned id){RemoveBookmark(id);};
    p.newFolder=[](unsigned parent)->unsigned{return NewFolder(parent);};
    p.folders=[](unsigned id){const auto* n=bookmarks.Find(id);return FolderChoices(n&&n->kind==BM::Kind::Folder?id:0);};
    p.choose=[](const std::vector<CM::Item>& items,RECT anchor,bool keyboard){return contextMenu.Track(items,{anchor.left,anchor.bottom},&anchor,keyboard,motion.ReducedMotion());};
    p.closed=PopoverClosed;p.announce=AnnounceBookmark;
    if(!bookmarkPopover.Create(instance,window,base,std::move(p)))Log(L"BOOKMARKS popover unavailable");
    if(!bookmarkToast.Create(instance,window,base))Log(L"BOOKMARKS toast unavailable");
    bookmarkToast.returnFocus_=[]{FocusPage();};
    NativeChrome::BookmarksUI::Drawer::Hooks d;
    d.schedule=[](NativeChrome::Motion::Time now){ScheduleMotion(now);};
    d.open=[](const std::wstring& url,bool newTab){bookmarksDrawer.Hide(false,true);OpenBookmarkUrl(url,newTab?2:0);};
    d.remove=[](BM::Id id){RemoveBookmark(id);};
    d.edit=[](BM::Id id,RECT r){const auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Folder)RenameOrEdit(id,RenameDrawer,r,true,0);else EditBookmark(id,r,false,true);};
    d.editColor=[](HDC dc,HWND edit)->LRESULT{return folderRename.Owns(edit)?folderRename.Color(dc):0;};
    d.relayout=[]{SyncFolderRename();};
    d.move=[](BM::Id id,int delta){ShiftBookmark(id,delta);bookmarksDrawer.FocusBookmark(id);};
    d.context=[](BM::Id id,POINT at,bool keyboard){bookmarkMenuSurface=RenameDrawer;BookmarkContextMenu(id,at,nullptr,keyboard);bookmarkMenuSurface=RenameNone;if(bookmarksDrawer.Visible()&&!bookmarkPopover.Visible()&&!folderRename.Active()){SetForegroundWindow(bookmarksDrawer.Window());SetFocus(bookmarksDrawer.Window());}};
    d.closed=[](bool keyboard){SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    d.icon=[](const std::wstring& site){return SiteIcon(site);};
    d.announce=AnnounceBookmark;
    d.dragStart=[](BM::Id id,POINT){StartOuterDrag(id,2);};
    if(!bookmarksDrawer.Create(instance,window,base,&bookmarks,std::move(d)))Log(L"BOOKMARKS drawer unavailable");
    bookmarksDrawer.stayForTest=qaDrawerStays;
}
// ---- One drop, any origin (review eb542bc): src/bookmark-move.h -------------------------------------------------------
// The place shown on screen is captured as identities (item, destination folder, the sibling it goes before) and the
// move is applied INSIDE the save, on the tree merged with the other windows' changes. Only a move that was actually
// written gets its undo step and « déplacé » announcement, from the committed ids; a failed write keeps the sticky
// retry (which re-checks the same identities and profile); an item or place that changed elsewhere moves nothing and
// says so.
void ShowBookmarkNotice(const std::wstring& text){
    if(bookmarkToast.Window()){bookmarkToast.SetBottomReserve(UpdateToastHeight());bookmarkToast.Show(text,[]{},motion.ReducedMotion(),TR(common_ok));}
    AnnounceBookmark(text);
}
void CommitBookmarkDrop(const BookmarkMove::Request& q,Workspace* owner){
    if(WorkspaceNow()!=owner||WorkspaceParked()){Log(L"BOOKMARKS drag cancelled: the profile changed");return;}
    BookmarkMove::Result r;
    if(smoke)BookmarkMove::Apply(bookmarks,q,{},r);   // --smoke-test / UI tests: no profile on disk, the same checked move in memory (as SaveBookmarks skips the write)
    else r=BookmarkMove::Commit(dataDir,BookmarksFile(),bookmarksBase,bookmarks,q);
    if(!r.remap.empty()){bookmarkUndo.clear();Log(L"BOOKMARKS new ids remapped (another window used them)");}
    if(r.outcome==BookmarkMove::Outcome::SaveFailed){
        Log(L"BOOKMARKS drag drop not saved");bookmarksSaveFailed=true;
        if(bookmarkToast.Window()){bookmarkToast.SetBottomReserve(UpdateToastHeight());bookmarkToast.Show(TR(bookmark_save_failed),[q,owner]{CommitBookmarkDrop(q,owner);},motion.ReducedMotion(),TR(downloads_action_retry),true);}
        AnnounceBookmark(TR(bookmark_save_failed));return;
    }
    if(bookmarksSaveFailed){bookmarksSaveFailed=false;Log(L"BOOKMARKS saved again");}
    if(!bookmarks.Find(lastFolder))lastFolder=BM::BarRoot;
    BookmarksChanged();
    switch(r.outcome){
    case BookmarkMove::Outcome::Gone:Log(L"BOOKMARKS drag cancelled: the item changed in another window");ShowBookmarkNotice(TR(bookmark_drop_gone));return;
    case BookmarkMove::Outcome::TargetChanged:Log(L"BOOKMARKS drag cancelled: the place changed in another window");ShowBookmarkNotice(TR(bookmark_drop_target_changed));return;
    case BookmarkMove::Outcome::Refused:AnnounceBookmark(TR(bookmark_too_deep));return;
    case BookmarkMove::Outcome::Unchanged:Log(L"BOOKMARKS drag dropped where it was");return;
    default:break;
    }
    PushUndo({{BookmarkStep::Moved,r.id,r.from,r.fromIndex}},TR(bookmark_undo_move));
    const auto* p=bookmarks.Find(r.to);
    if(p&&p->id==BM::BarRoot)AnnounceBookmark(TRF(bookmark_moved_position,std::to_wstring(bookmarks.IndexOf(r.id)+1),std::to_wstring(p->children.size())));
    else AnnounceBookmark(TRF(bookmark_moved_into,p&&!p->title.empty()?p->title:std::wstring(TR(bookmarks_unnamed_folder))));
    Log(L"BOOKMARKS drag dropped");
}
void ClearOuterMarkers(){chrome.bandInsertX=-1;chrome.bandDropInto=-1;bookmarksDrawer.SetDropMarker(-1,BookmarkDrag::Mark::None);InvalidateChrome();}
// Where a drag at `client` would land (bar: within its band only; drawer: over its window); nothing else is a target.
// Sets the markers. The store is never touched here.
BookmarkDrag::Drop DropTargetAt(BM::Id id,POINT client){
    const float k=96.f/GetDpiForWindow(window);float x=client.x*k,y=client.y*k;const bool onChrome=chrome.Local(x,y); // chrome-local (Mode Focus: in the panel only)
    POINT screen=client;ClientToScreen(window,&screen);
    BookmarkDrag::Drop drop;chrome.bandInsertX=-1;chrome.bandDropInto=-1;
    RECT dr{};const bool overDrawer=bookmarksDrawer.Visible()&&GetWindowRect(bookmarksDrawer.Window(),&dr)&&PtInRect(&dr,screen);
    if(overDrawer){
        const auto t=bookmarksDrawer.DropAt(screen,id);
        if(t.drop.valid)drop=t.drop;
        bookmarksDrawer.SetDropMarker(t.drop.valid?t.row:-1,t.drop.valid?t.mark:BookmarkDrag::Mark::None);
        return drop;
    }
    bookmarksDrawer.SetDropMarker(-1,BookmarkDrag::Mark::None);
    // #127 column (Paper « Favoris · clic droit sur un favori »: « Glisser une case sur un dossier la range aussi »): a tile
    // of the grid, the compact column or the « » » popover dropped on a folder tile goes inside it (at its end).
    if(chrome.sidebar){const int i=chrome.SideFolderAt(x,y,(unsigned)id);
        if(i>=0&&BookmarkDrag::Accepts(bookmarks,id,chrome.bandItems[i].id)){drop.parent=chrome.bandItems[i].id;drop.index=BM::End;drop.valid=true;chrome.bandDropInto=i;}
        return drop;}
    if(onChrome&&chrome.bandShown&&y>=BookmarksBar::Layout::top-6&&y<BookmarksBar::Layout::bottom+6&&x>=0){
        std::vector<BookmarkDrag::Box> boxes;for(int i=0;i<chrome.band.visible;++i){const auto r=chrome.BandRect(i);boxes.push_back({r.left,r.right});}
        RECT client{};GetClientRect(window,&client);const float end=chrome.band.overflow?chrome.band.overflowLeft:chrome.floating?chrome.width:client.right*k;
        if(x<end){   // the bar's own width (trailing blank included), never the overflow button or past the window
            const auto t=BookmarkDrag::BarTarget(bookmarks,id,boxes,x);
            if(t.drop.valid){drop=t.drop;chrome.bandDropInto=t.into;chrome.bandInsertX=t.into>=0?-1:t.marker;}
        }
    }
    return drop;
}
// Bar pointer: press records a drag candidate; past the system threshold the item follows the pointer (drawn only)
// and a marker shows where it would land — on the bar (line, or the folder it would enter) or in the drawer. The
// store changes only at a valid release (CommitBookmarkDrop); anywhere else the drag ends with nothing changed.
void BandDragMove(POINT point){
    if(bandDrag.index<0||halted)return;
    if(!bandDrag.active){if(std::abs(point.x-bandDrag.start.x)<=GetSystemMetrics(SM_CXDRAG)&&std::abs(point.y-bandDrag.start.y)<=GetSystemMetrics(SM_CYDRAG))return;bandDrag.active=true;bandDrag.owner=WorkspaceNow();bookmarksDrawer.holding_=bookmarksDrawer.Visible();Log(L"BOOKMARKS drag start");}
    bandDrag.drop={};
    if(!bookmarks.Find(bandDrag.id)||WorkspaceNow()!=bandDrag.owner){chrome.bandDrag=-1;ClearOuterMarkers();return;}
    bandDrag.drop=DropTargetAt(bandDrag.id,point);
    const auto& bar=bookmarks.Root(BM::BarRoot).children;const int index=(int)(std::find(bar.begin(),bar.end(),bandDrag.id)-bar.begin());
    chrome.bandDrag=index<chrome.band.visible?index:-1;
    if(chrome.bandDrag>=0)chrome.bandDragOffset=point.x*96.f/GetDpiForWindow(window)-bandDrag.grab-chrome.BandRect(index).left;
    SetCursor(LoadCursor(nullptr,bandDrag.drop.valid?IDC_ARROW:IDC_NO));
    InvalidateChrome();
}
// `at`: the release point (the drop is computed there again); none or `cancel`: nothing changes.
void BandDragEnd(bool cancel,const POINT* at=nullptr){
    if(bandDrag.index<0)return;
    if(!cancel&&at&&bandDrag.active)BandDragMove(*at);
    const auto drag=bandDrag;
    bandDrag={};chrome.bandDrag=-1;chrome.bandDragOffset=0;ClearOuterMarkers();bookmarksDrawer.holding_=false;
    if(!drag.active)return;
    if(cancel||!at||!drag.drop.valid){Log(L"BOOKMARKS drag cancelled");BookmarksChanged();return;}
    if(!bookmarks.Find(drag.id)){Log(L"BOOKMARKS drag cancelled: item gone");BookmarksChanged();return;}
    CommitBookmarkDrop(BookmarkMove::Capture(bookmarks,drag.id,drag.drop),drag.owner);
}
// ---- Drag from a menu or the drawer (src/bookmark-drag.h) --------------------------------------------------------------
// The menu or the drawer hands over a bookmark while the button is still down; the main window takes the capture and
// shows where it would land: on the bar (insertion line or the folder it would enter) or in the drawer (line / folder).
// Only a valid release commits (CommitBookmarkDrop). Escape, a lost capture, a profile switch, the item deleted
// meanwhile, a release anywhere else or the window closing cancel it with nothing changed.
void StartOuterDrag(BM::Id id,int source){
    if(halted||outerDrag.active||bandDrag.index>=0||!bookmarks.Find(id)||BM::Store::IsRoot(id))return;   // the source saw the button down (its message's MK_LBUTTON)
    outerDrag={id,WorkspaceNow(),true,{},source};
    bookmarksDrawer.holding_=bookmarksDrawer.Visible();   // keep the drawer open while the main window takes the pointer
    if(GetFocus()!=window)SetFocus(window);   // Escape reaches the controller (the drawer stays: holding_)
    SetCapture(window);Log(source==1?L"BOOKMARKS drag start (menu)":L"BOOKMARKS drag start (drawer)");
}
void OuterDragMove(POINT client){
    if(!outerDrag.active)return;
    if(!bookmarks.Find(outerDrag.id)||WorkspaceNow()!=outerDrag.owner){outerDrag.drop={};ClearOuterMarkers();return;}
    outerDrag.drop=DropTargetAt(outerDrag.id,client);
    SetCursor(LoadCursor(nullptr,outerDrag.drop.valid?IDC_ARROW:IDC_NO));
    InvalidateChrome();
}
void OuterDragEnd(bool cancel,const POINT* at=nullptr){
    if(!outerDrag.active)return;
    if(!cancel&&at)OuterDragMove(*at);
    const auto drag=outerDrag;outerDrag={};ClearOuterMarkers();bookmarksDrawer.holding_=false;
    if(GetCapture()==window)ReleaseCapture();
    if(cancel||!at||!drag.drop.valid||WorkspaceNow()!=drag.owner||WorkspaceParked()){Log(L"BOOKMARKS drag cancelled");return;}
    if(!bookmarks.Find(drag.id)){Log(L"BOOKMARKS drag cancelled: item gone");return;}
    CommitBookmarkDrop(BookmarkMove::Capture(bookmarks,drag.id,drag.drop),drag.owner);
}
// Band keyboard (focus on an item or the » button): arrows rove, Ctrl+Maj+arrows move.
bool BandKey(WPARAM w){
    const int focus=keyboardFocus;const int index=NativeChrome::BookmarkIndex(focus);
    if(index<0&&focus!='O')return false;
    const bool ctrl=GetKeyState(VK_CONTROL)<0,shift=GetKeyState(VK_SHIFT)<0;
    const auto order=chrome.BandOrder();auto it=std::find(order.begin(),order.end(),focus);if(it==order.end())return false;
    auto rove=[&](int to){keyboardFocus=order[(size_t)std::clamp(to,0,(int)order.size()-1)];chrome.bandRover=NativeChrome::IsBookmark(keyboardFocus)?NativeChrome::BookmarkIndex(keyboardFocus):-2;keyboardModality=true;InvalidateChrome();return true;};
    const int at=(int)(it-order.begin());
    if(ctrl&&shift&&(w==VK_LEFT||w==VK_RIGHT)&&index>=0){const BM::Id id=chrome.bandItems[index].id;ShiftBookmark(id,w==VK_LEFT?-1:1);
        const size_t now=bookmarks.IndexOf(id);if((int)now<chrome.band.visible){keyboardFocus=NativeChrome::BookmarkBase+(int)now;chrome.bandRover=(int)now;}InvalidateChrome();return true;}
    if(ctrl||GetKeyState(VK_MENU)<0)return false;
    switch(w){
    case VK_LEFT:return rove(at-1);
    case VK_RIGHT:return rove(at+1);
    case VK_HOME:return rove(0);
    case VK_END:return rove((int)order.size()-1);
    case VK_DOWN:if(focus=='O'){OverflowMenu(true);return true;}if(chrome.bandItems[index].folder){FolderMenu(index,true);return true;}return false;
    case VK_F2:if(index>=0){const BM::Id id=chrome.bandItems[index].id;if(chrome.bandItems[index].folder)RenameOrEdit(id,RenameBand,RegionScreen(focus),true,focus);else EditBookmark(id,RegionScreen(focus),false,true,false,focus);return true;}return false;
    case VK_DELETE:if(index>=0){RemoveBookmark(chrome.bandItems[index].id);if(chrome.band.visible>0)rove(std::min(at,chrome.band.visible-1));else ToggleChromeFocus();return true;}return false;
    case VK_ESCAPE:FocusPage();return true;
    case VK_RETURN:if(index>=0){keyboardModality=true;BandActivate(index,ctrl,shift,true);return true;}return false;
    default:return false;
    }
}
// Smoke run (no file read or written): the whole bookmark flow in memory, through the
// same functions the pointer and the keyboard use.
// i18n-exempt-begin: bookmarks smoke fixture (sample titles, assertion text)
bool SmokeBookmarks(){
    auto fail=[](const wchar_t* what){Fail(std::wstring(L"Smoke bookmarks: ")+what,E_FAIL);return false;};
    auto t=Active();if(!t||!t->controller.p)return fail(L"no tab");
    auto top=[&]{RECT b{};t->controller->get_Bounds(&b);return b.top;};
    if(bookmarks.Size()||chrome.bandShown||chrome.ContentTop()!=81||top()!=S(81))return fail(L"empty profile must keep the 81 DIP chrome, page right under it");
    const std::wstring savedUri=t->uri;const bool savedHome=t->home;t->uri=L"https://example.org/";t->home=false;BuildChrome();
    BookmarkStar(true);
    const auto first=bookmarks.Matching(L"https://example.org/");
    if(bookmarks.Size()!=1||first.empty()||!chrome.starred||!chrome.bandShown||chrome.ContentTop()!=121||top()!=S(121))return fail(L"Ctrl+D must save at once, fill the star and show the band (page at 121)");
    if(!bookmarkPopover.Visible()||GetFocus()!=bookmarkPopover.NameEdit())return fail(L"popover must open focused on Nom");
    bookmarkPopover.KeyForTest(VK_ESCAPE);if(bookmarkPopover.Visible()||bookmarks.Size()!=1)return fail(L"Escape must close the popover and keep the bookmark");
    const BM::Id id=first.front();EditBookmark(id,RegionScreen('K'),true,true);
    bookmarkPopover.SetUrlForTest(L"lemonde..fr/international");bookmarkPopover.CommitForTest();
    if(!bookmarkPopover.Visible()||!bookmarkPopover.ShowsError())return fail(L"an invalid address must keep the popover open with its error");
    bookmarkPopover.SetUrlForTest(L"lemonde.fr/international");if(bookmarkPopover.ShowsError())return fail(L"a valid address must clear the error as it is typed");
    bookmarkPopover.SetNameForTest(L"Le Monde — International");bookmarkPopover.CommitForTest();
    if(bookmarkPopover.Visible()||bookmarks.Find(id)->url!=L"https://lemonde.fr/international"||bookmarks.Find(id)->title!=L"Le Monde — International")return fail(L"Terminé must apply name and normalized address");
    if(chrome.starred)return fail(L"the star must empty once the page is no longer bookmarked");
    UndoBookmark();if(bookmarks.Find(id)->url!=L"https://example.org/")return fail(L"Ctrl+Z must undo the edit");
    const BM::Id folder=NewFolder(BM::BarRoot);if(!folder)return fail(L"new folder");
    MoveBookmarkTo(id,folder);if(bookmarks.CountLinks(folder)!=1||chrome.bandItems.size()!=1||!chrome.bandItems[0].folder)return fail(L"move into folder");
    const size_t index=bookmarks.IndexOf(folder);RemoveBookmark(folder);
    if(bookmarks.Find(folder)||bookmarks.Find(id)||!bookmarkToast.Visible())return fail(L"removing a folder must be immediate, with the undo toast");
    UndoBookmark();if(!bookmarks.Find(folder)||bookmarks.IndexOf(folder)!=index||bookmarks.Find(id)->parent!=folder||bookmarkToast.Visible())return fail(L"undo must restore the folder, same ids and place, and close the toast");
    const BM::Id copy=DuplicateBookmark(folder);if(!copy||bookmarks.IndexOf(copy)!=index+1||bookmarks.CountLinks(copy)!=1)return fail(L"duplicate goes right after the original, with its contents");
    for(int i=0;i<40;++i)bookmarks.Add(BM::BarRoot,BM::End,L"Exemple "+std::to_wstring(i),L"https://example.com/"+std::to_wstring(i),0);
    BookmarksChanged();if(!chrome.band.overflow||chrome.band.visible>=(int)chrome.bandItems.size()||chrome.band.visible<1)return fail(L"overflow » expected with 42 items");
    for(int i=0;i<chrome.band.visible;++i)if(chrome.band.width[i]<BookmarksBar::Layout::minItem-.5f&&chrome.band.width[i]<BookmarksBar::Layout::Natural(chrome.BandTitleWidth(chrome.bandItems[i]),chrome.bandItems[i].folder)-.5f)return fail(L"no title below the 96 DIP floor");
    ShowBookmarksDrawer(true);if(!bookmarksDrawer.Visible())return fail(L"drawer");
    bookmarksDrawer.TypeForTest(L"exemple 39");if(bookmarksDrawer.Rows().size()!=1)return fail(L"drawer search");
    bookmarksDrawer.KeyForTest(VK_ESCAPE);if(!bookmarksDrawer.Query().empty()||!bookmarksDrawer.Visible())return fail(L"Escape clears the search first");
    bookmarksDrawer.Hide(false,false);if(bookmarksDrawer.Visible())return fail(L"drawer close");
    while(!bookmarks.Root(BM::BarRoot).children.empty())bookmarks.Remove(bookmarks.Root(BM::BarRoot).children.front());
    bookmarkUndo.clear();bookmarkToast.HideNow();t->uri=savedUri;t->home=savedHome;BookmarksChanged();
    if(chrome.bandShown||chrome.ContentTop()!=81||top()!=S(81))return fail(L"an empty bar must disappear (auto)");
    Log(L"SMOKE bookmarks: Ctrl+D saves at once (band shown, page 81 -> 121 DIP), popover Escape keeps it, invalid address stays red until valid, edit + undo, folder move, delete + undo toast, duplicate, overflow at 42 items, drawer search");
    return true;
}
// i18n-exempt-end
bool BookmarkPopoverFocus(){HWND f=GetFocus();return bookmarkPopover.Visible()&&f&&(f==bookmarkPopover.Window()||IsChild(bookmarkPopover.Window(),f));}
void ExtraCommand(int cmd){auto t=Active();if(AnnotationCommand(cmd))return;if(NativeChrome::IsBookmark(cmd)){BandActivate(NativeChrome::BookmarkIndex(cmd),false,false,KeyboardAnchor()!=0||keyboardModality);return;}switch(cmd){
case 'K':BookmarkStar(KeyboardAnchor()!=0||keyboardModality);break;
case 'E':lastFolder=BM::BarRoot;BookmarkStar(KeyboardAnchor()!=0||keyboardModality);break;
case 'O':if(chrome.sidebar){SidebarPopover(!chrome.sidebarPopover);break;}OverflowMenu(KeyboardAnchor()!=0);break;
case NativeChrome::Chrome::SidebarManageId:SidebarPopover(false);ShowBookmarksDrawer(keyboardModality);break;   // « Gérer les favoris »: the whole drawer (Ctrl+Maj+O)
case NativeChrome::Chrome::SidebarEdgeId:SidebarEdgeReset();break;   // UI Automation Invoke / Entrée: 248
case NativeChrome::Chrome::SidebarGridToggleId:chrome.sidebarGridOpen=!chrome.sidebarGridOpen;if(!chrome.sidebarGridOpen)chrome.sidebarGridScroll=0;BuildChrome();InvalidateChrome();{const std::wstring said=chrome.sidebarGridOpen?std::wstring(TR(sidebar_collapse_grid)):TRF(sidebar_all_bookmarks,std::to_wstring(chrome.bandItems.size()));accessibility.Notify('K',said.c_str(),L"Bookmarks" /* i18n-exempt: UIA activity id */);}break;
case BookmarkBarCommand:ToggleBookmarkBar();break;
case SidebarToggleCommand:ToggleSidebar();break;
case FocusModeCommand:ToggleFocusMode();break;
case BookmarksDrawerCommand:if(bookmarksDismissClick){bookmarksDismissClick=false;break;}ToggleBookmarksDrawer(keyboardModality);break;
case BookmarkUndoCommand:UndoBookmark();break;
case 'I':FocusAddress();break; // the security indicator opens the address
case 'Q':RequestChipActivated();break;
case NativeChrome::Chrome::VaultKeyId:VaultKeyActivated();break; // B03 native fill control (armed, guarded; review §5) // B01 chip: back to its open popover, never a new request
case 900:ProfileMenuOpen(keyboardModality);break;
case 'Y':if(historyDismissClick){historyDismissClick=false;break;}ToggleHistory(KeyboardAnchor()!=0||keyboardModality);break;
case PrintActive:if(t&&!t->home)PrintPage(t);break; // like the tab menu: nothing to print on a home tab
case CheckUpdatesCommand:CheckForUpdates();break;
case 'm':ShowWindow(window,SW_MINIMIZE);break;
case 'M':ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);break;
case 'X':PostMessageW(window,WM_CLOSE,0,0);break;
case 'C':if(t&&!t->home&&!t->uri.empty()&&OpenClipboard(window)){auto bytes=(t->uri.size()+1)*sizeof(wchar_t);HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(mem){void* data=GlobalLock(mem);if(data){memcpy(data,t->uri.c_str(),bytes);GlobalUnlock(mem);EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,mem)){auto now=NativeChrome::Motion::Now();motion.CopySucceeded(now);chrome.lottie.CopySucceeded(now);InvalidateChrome();ScheduleMotion(now);AnnounceCopied();}else GlobalFree(mem);}else GlobalFree(mem);}CloseClipboard();}break;
case 'D':if(downloadDismissClick){downloadDismissClick=false;break;}ToggleDownloads();break;
case 'V':if(audioDismissClick){audioDismissClick=false;break;}ToggleAudio(KeyboardAnchor()!=0||keyboardModality);break;
case 'P':OpenPrivateWindow();break;
case 'S':if(settingsDismissClick){settingsDismissClick=false;break;}ToggleSettings();break;
case 'A':ShowAboutDialog();break;
case FindNextCommand:case FindPreviousCommand:FindStep(cmd==FindNextCommand);break;
case FullscreenCommand:if(PageFullscreenActive())ExitPageFullscreen();else ToggleBrowserFullscreen();break;
case ExitFullscreenCommand:ExitPageFullscreen();break;
case SandboxCommandId:OpenSandbox();break;
case ShortcutsCommand:OpenShortcutsPanel();break;
}}
#ifndef BROWSER_UPDATES
void SyncUpdateToastReserve(){SyncDrawerReserve();} void CheckForUpdates(){} void SetUpdateToastTop(float){} float UpdateToastHeight(){return 0;} bool UpdateCheckAvailable(){return false;} bool FocusUpdateToast(){return false;} bool UpdateToastHasFocus(){return false;}
NativeChrome::SettingsUI::Drawer::Version SettingsVersion(){return {AboutTitle(),false,false};}
NativeChrome::Modal::Status AboutStatusNow(){namespace MD=NativeChrome::Modal;auto st=MD::AboutStatus(MD::AboutUpToDate);st.icon=MD::StatusIcon::None;st.title=TR(updates_unavailable);st.detail=TR(updates_unavailable_build);st.hasButton=false;return st;}
void AboutUpdateAction(int){}
void SyncUpdateToastMotion(){}
#else
// ---- Self-update host -------------------------------------------------------
// The engine works on its own threads and only posts UpdateMessage (coalesced and
// generation-tagged, stale ones are refused by OnNotify); everything here runs on
// the UI thread. Update failures stay inside the toast: they never set `halted`
// and never touch tabs, WebView2, page downloads or the session.
constexpr UINT UpdateMessage=WM_APP+9; constexpr UINT_PTR UpdateTimerId=74;
UpdateCore::Engine updates; BrowserUpdate::Toast updateToast; bool updatesStarted=false,closingForInstall=false;
DWORD updateTimerDelayMs=0; // armed one-shot delay (0 = none); read by fixtures
void RefreshAboutStatus();
// « Nouveautés » replaces the « Mise à jour installée » toast when it opens after this update (user request 2026-11-16).
bool installedToastReplaced=false;
void ShowUpdateView(){if(updatesStarted){auto v=updates.View();if(installedToastReplaced&&v.phase==BrowserUpdate::Phase::Installed)v=BrowserUpdate::View{};updateToast.SetView(v);}settingsDrawer.Changed();RefreshAboutStatus();}
// Periodic checks for a long-running browser: ONE one-shot timer, due-aware and re-armed after every
// engine notification and every firing. Delay = time to the persisted next-check (6 h TTL / back-off),
// never below 15 min nor above 6 h, so an idle browser wakes at most a few times per hour and usually
// once per 6 h; the engine itself decides (quietly) whether a check is due and allowed.
void ArmUpdateTimer(){
    const DWORD delay=updatesStarted&&!closingForInstall?updates.AutoCheckDelayMs():0;
    if(!delay){KillTimer(window,UpdateTimerId);updateTimerDelayMs=0;return;}
    if(SetTimer(window,UpdateTimerId,delay,nullptr))updateTimerDelayMs=delay;else updateTimerDelayMs=0;
}
void UpdateTimerFired(){KillTimer(window,UpdateTimerId);updateTimerDelayMs=0;if(updatesStarted&&!closingForInstall)updates.AutoCheck();ArmUpdateTimer();}
void UpdateAction(BrowserUpdate::Action action){
    if(!updatesStarted||closingForInstall)return;
    // The session is saved before the helper exists and again by WM_CLOSE.
    if(action==BrowserUpdate::Action::RestartInstall)SaveSession();
    const auto result=updates.Perform(action);
    ShowUpdateView();
    if(result==UpdateCore::ActionResult::CloseForInstall){
        // The helper waits for this exact process to exit on its own; close normally.
        closingForInstall=true;closeWithoutPrompt=true;Log(L"UPDATE install handoff: saving session and closing");PostMessageW(window,WM_CLOSE,0,0);
    }
}
std::wstring ExePath(){std::wstring exe(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);return exe;}
UpdateCore::Config UpdateConfig(){
    UpdateCore::Config config;config.currentVersion=BROWSER_UPDATE_VERSION;config.repository=BROWSER_UPDATE_REPO_WIDE;config.exePath=ExePath();
    config.notifyWindow=window;config.notifyMessage=UpdateMessage;config.enabled=!smoke&&!privateWindow&&!config.exePath.empty();
#ifdef BROWSER_PREVIEW
    config.enabled=false; // design preview: no update feed, no install
#endif
    return config;
}
// Fixtures pass an isolated config (temp install root, mocked transport).
void StartUpdates(UpdateCore::Config config=UpdateConfig()){
    if(!updateToast.Init(window,base+L"\\assets",UpdateAction))Log(L"UPDATE toast unavailable");
    updateToast.SetFocusReleaseHandler([]{FocusPage();});
    updatesStarted=updates.Start(config);
    Log(updatesStarted?L"UPDATE engine started "+config.currentVersion:L"UPDATE engine disabled");
    // « Nouveautés » Mini once after a successful update (src/release-notes.h): Installed is set by Start itself.
    // When it opens, it takes the place of the « Mise à jour installée » toast.
    installedToastReplaced=ReleaseNotesAtStartup(updatesStarted&&updates.View().phase==BrowserUpdate::Phase::Installed,config.installRoot.empty()?UpdateCore::DefaultInstallRoot():config.installRoot,config.currentVersion,profileRoot.empty()?dataDir:profileRoot);
    if(installedToastReplaced)Log(L"UPDATE installed toast replaced by Nouveautés");
    ShowUpdateView();ArmUpdateTimer();
}
bool UpdateCheckAvailable(){return updatesStarted&&!closingForInstall;}
// A8 « À propos »: the engine's real state (same source as the toast and the Réglages footer).
NativeChrome::Modal::Status AboutStatusNow(){
    namespace MD=NativeChrome::Modal;using P=BrowserUpdate::Phase;using L=UpdateCore::Engine::LastCheck;
    if(!UpdateCheckAvailable()){auto st=MD::AboutStatus(MD::AboutUpToDate);st.icon=MD::StatusIcon::None;st.title=TR(updates_unavailable);st.detail=closingForInstall?TR(updates_installing_now):TR(updates_disabled_build);st.hasButton=false;return st;}
    const auto v=updates.View();
    switch(v.phase){
    case P::Checking:return MD::AboutStatus(MD::AboutChecking);
    case P::Available:{auto st=MD::AboutStatus(MD::AboutUpToDate);st.icon=MD::StatusIcon::Download;st.title=TRF(updates_version_available,v.version);st.detail=TR(updates_download_on_request);
        st.button=MD::Make(MD::AboutDownloadButton,TR(updates_download),MD::Role::Primary,false,false,false,L"AboutDownload");st.button.closes=false;return st;}
    case P::Downloading:{auto st=MD::AboutStatus(MD::AboutChecking);st.title=TRF(updates_downloading_version,v.version);
        st.detail=v.total?std::to_wstring(std::min<std::uint64_t>(100,v.received*100/v.total))+L" %":L"";
        // Annuler = the engine's own Action::Cancel (same as the toast); no inert « Rechercher ».
        st.button=MD::Make(MD::AboutCancelButton,TR(common_cancel),MD::Role::Quiet,false,false,false,L"AboutCancel");st.button.closes=false;st.hasButton=true;return st;}
    case P::Ready:{auto st=MD::AboutStatus(MD::AboutReady,L"",v.version);st.button.closes=false;return st;}
    case P::Installing:case P::Installed:{auto st=MD::AboutStatus(MD::AboutChecking);st.title=TR(updates_installing);st.hasButton=false;return st;} // no Annuler after the #60 handoff
    case P::Error:{ // E4: the failed step is named; the action is the engine's real retry
        using F=UpdateCore::Engine::Failure;const F f=updates.LastFailure();
        auto st=MD::AboutStatus(MD::AboutOffline,TR(updates_check_failed)); // check: cause unknown, never « Aucune connexion » by guess
        // Download / install failures (contract KS4-0 / KT4-0): « Rechercher à nouveau » = a real Check.
        // F1: after a download failure the engine's Retry downloads again (manifest kept): « Réessayer ».
        if(f==F::Download){st.title=TR(updates_download_failed_title);st.detail=!v.error.empty()&&v.error!=TR(update_error_download_failed)?v.error:v.version.empty()?std::wstring(TR(updates_download_failed)):TRF(updates_download_failed_version,v.version);st.button.label=TR(downloads_action_retry);}
        // F6: install failure: the engine's own message (it says when the previous version was kept), else
        // an install wording; Retry is then a check: « Rechercher à nouveau ».
        else if(f==F::Install){st.title=TR(updates_install_failed_title);st.detail=v.error.empty()?std::wstring(TR(updates_install_failed)):v.error;st.button.label=TR(updates_check_again);}
        return st;}
    default:{const auto last=updates.LastCheckResult();
        if(last==L::Failed)return MD::AboutStatus(MD::AboutOffline,TR(updates_check_failed));
        if(last==L::UpToDate)return MD::AboutStatus(MD::AboutUpToDate,TR(updates_last_check_ok)); // a real UpToDate result for this version
        // No positive result yet: never « browser est à jour ».
        auto st=MD::AboutStatus(MD::AboutUpToDate,TR(updates_check_prompt));st.icon=MD::StatusIcon::None;st.title=TR(updates_title);return st;}
    }
}
void AboutUpdateAction(int button){
    namespace MD=NativeChrome::Modal;
    if(button==MD::AboutCheckButton){
        // In Error the engine's own Retry: download again after a download failure (« Réessayer »),
        // a check after a check or install failure (« Réessayer » / « Rechercher à nouveau »).
        if(updates.View().phase==BrowserUpdate::Phase::Error)UpdateAction(BrowserUpdate::Action::Retry);else CheckForUpdates();return;}
    if(button==MD::AboutCancelButton){UpdateAction(BrowserUpdate::Action::Cancel);return;}
    if(button==MD::AboutDownloadButton){UpdateAction(BrowserUpdate::Action::Download);return;}
    if(button==MD::AboutRestartButton)UpdateAction(BrowserUpdate::Action::RestartInstall); // #60 handoff: closeWithoutPrompt, normal close
}
void CheckForUpdates(){if(UpdateCheckAvailable()){updates.CheckNow();ShowUpdateView();}}
// Settings footer: "browser <v> · <statut>", kept within ~200 DIP at 14 px (hence the terse wording).
// A manual check from the drawer goes through the engine like the menu command, so the toast
// surfaces Available/Ready/Error by itself; "À jour" only lives here (the toast stays hidden).
NativeChrome::SettingsUI::Drawer::Version SettingsVersion(){
    using P=BrowserUpdate::Phase;using L=UpdateCore::Engine::LastCheck;
    if(!updatesStarted||!updates.Enabled())return {AboutTitle(),false,false};
    const auto v=updates.View();std::wstring status;
    switch(v.phase){
    case P::Hidden:{const auto last=updates.LastCheckResult();status=last==L::UpToDate?TR(updates_status_up_to_date):last==L::Available?TR(updates_status_available):last==L::Failed?TR(updates_status_check_failed):L"";break;} // F5: the cause is not known: never « Hors ligne »
    case P::Checking:status=TR(updates_status_checking);break;
    case P::Available:status=TR(updates_status_available);break;
    case P::Downloading:status=v.total?TRF(updates_status_downloading_percent,I18n::Percent((long long)std::min<std::uint64_t>(100,v.received*100/v.total))):std::wstring(TR(updates_status_downloading));break;
    case P::Ready:status=TR(updates_status_ready);break;
    case P::Installing:status=TR(updates_installing);break;
    case P::Installed:status=TR(updates_status_installed);break;
    case P::Error:status=v.version.empty()?TR(updates_status_check_failed):TR(downloads_failed);break; // empty version = the check itself failed (cause unknown)
    }
    const bool busy=v.phase==P::Checking||v.phase==P::Downloading||v.phase==P::Installing;
    return {status.empty()?AboutTitle():AboutTitle()+L" · "+status,v.phase==P::Checking,UpdateCheckAvailable()&&!busy};
}
void StopUpdates(){if(window)KillTimer(window,UpdateTimerId);updateTimerDelayMs=0;if(updatesStarted){updatesStarted=false;updates.Shutdown();}updateToast.Close();}
// Legacy portable exe (old Start Menu shortcut target) after a successful first install: hand over to the
// installed current version (stable profile/session) instead of reopening the obsolete portable copy.
// Validation lives in UpdateCore::PortableForwardTarget; any failure keeps the portable browser running.
extern std::wstring launchArgs;
bool ForwardToInstalled(const std::wstring& installRoot=L""){
    const auto target=UpdateCore::PortableForwardTarget(ExePath(),installRoot);if(target.empty())return false;
    const auto dir=target.substr(0,target.find_last_of(L'\\'));std::wstring command=L"\""+target+L"\"";
    // The installed copy opens the same profile (review M5): --profile <id> is passed through, and the link or file
    // this launch was given (after --single-argument: never read as switches), nothing else.
    if(const auto at=launchArgs.find(L"--profile ");at!=std::wstring::npos){size_t b=at+10;while(b<launchArgs.size()&&launchArgs[b]==L' ')++b;size_t e=b;while(e<launchArgs.size()&&launchArgs[e]!=L' ')++e;const std::wstring id=launchArgs.substr(b,e-b);if(Profiles::ValidId(id))command+=L" --profile "+id;}
    if(!launchTarget.empty())command+=std::wstring(L" ")+LaunchArgs::SingleArgument+L" "+LaunchArgs::Quoted(launchTarget);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(target.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,dir.c_str(),&si,&pi)){Log(L"UPDATE forward to installed failed, staying portable");return false;}
    AllowSetForegroundWindow(pi.dwProcessId);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    Log(L"UPDATE portable launch forwarded to "+target);return true;
}
void LayoutUpdateToast(){if(updateToast.Window())updateToast.Layout();}
// The bottom-left card keeps clear of the download drawer docked on the right.
void SyncUpdateToastReserve(){SyncDrawerReserve();const bool open=(downloadDrawer.Visible()&&!downloadDrawer.Closing())||settingsDrawer.Open()||historyDrawer.Open()||bookmarksDrawer.Open();updateToast.SetReservedRight(open?(settingsDrawer.Open()?NativeChrome::SettingsUI::Layout::width:NativeChrome::Downloads::DrawerLayout::width)+NativeChrome::Downloads::DrawerLayout::margin:0.f);}
bool FocusUpdateToast(){return updateToast.Shown()&&updateToast.Focus();}
bool UpdateToastHasFocus(){HWND toast=updateToast.Window();HWND focus=GetFocus();return toast&&focus&&(focus==toast||IsChild(toast,focus));}
void SyncUpdateToastMotion(){updateToast.SyncReducedMotion();} // no-op before Init/after Close
void SetUpdateToastTop(float dip){updateToast.SetTopReserve(dip);}
float UpdateToastHeight(){if(!updateToast.Shown()||!updateToast.Window())return 0;RECT r{};GetWindowRect(updateToast.Window(),&r);return (r.bottom-r.top)*96.f/GetDpiForWindow(window)+8;}
#endif
// Portable layout (unchanged): profile, session and log beside the executable.
// Installed layout <root>\versions\<v>\browser.exe keeps profile/session/log in the
// stable <root>\data, outside every version directory (ResolveDataPaths is pure;
// the one-time copy of a portable profile is done by the helper, never here).
void ResolveBrowserPaths(const std::wstring& installRoot=L""){
    dataDir=base;profileDir=base+L"\\profile";sessionFile=base+L"\\session";downloadsFile=base+L"\\downloads";settingsFile=base+L"\\settings";logFile=base+L"\\prototype.log";
#ifdef BROWSER_UPDATES
    auto paths=UpdateCore::ResolveDataPaths(ExePath(),installRoot);
    if(paths.installed&&!paths.dataDir.empty()&&!paths.profileDir.empty()&&!paths.sessionFile.empty()){
        std::error_code ignored;std::filesystem::create_directories(std::filesystem::path(paths.dataDir),ignored);
        dataDir=paths.dataDir;profileDir=paths.profileDir;sessionFile=paths.sessionFile;downloadsFile=paths.downloadsFile.empty()?paths.dataDir+L"\\downloads":paths.downloadsFile;settingsFile=paths.dataDir+L"\\settings";if(!paths.logFile.empty())logFile=paths.logFile;
        taskbarIdentity=TaskbarIdentity::ForInstalled(ExePath(),true);TaskbarIdentity::ApplyToProcess(taskbarIdentity);} // before any window
#else
    (void)installRoot;
#endif
}
// ---- Accessibility ----------------------------------------------------------
// The chrome is painted with Direct2D, so Windows sees no controls in it.
// src/accessibility.h exposes them to screen readers through UI Automation; this
// section describes them. Keys: chrome region ids for fixed controls, a container
// constant for the strip and the toolbar, and the tab's serial for a tab (kind 1)
// or its close button (kind 2), so an element never silently becomes another tab
// after a close or a move.
Accessibility::Bridge accessibility;
constexpr long long AccessibleStrip=0x10000,AccessibleToolbar=0x10001,AccessibleSuggestions=0x10002,AccessiblePrivateChip=0x10009; // 0x10009: the private window chip (Text)
static_assert(AccessibleToolbar==AccessibleToolbarKey,"early toolbar key matches the UIA tree");
// Suggestion rows: kind 3, keyed by row index (the list is rebuilt per keystroke).
long long AccessibleSuggestionKey(int i){return (3LL<<32)|(unsigned)i;}
constexpr UINT AccessibleMessage=WM_APP+11;
enum AccessibleActionKind { AccessibleInvoke=1, AccessibleSelect, AccessibleSetValue, AccessibleExpand };
std::wstring accessibleAddress;
long long AccessibleTabKey(int index,bool close=false){return index>=0&&index<(int)pages.size()?((close?2LL:1LL)<<32)|pages[index]->serial:0;}
// Band items: kind 4, keyed by bookmark id (stable across moves and overflow).
long long AccessibleBandItemKey(int region){const int i=NativeChrome::BookmarkIndex(region);if(i>=0&&i<(int)chrome.bandItems.size())return (4LL<<32)|chrome.bandItems[i].id;return region;}
long long AccessibleKey(int id){if(NativeChrome::IsClose(id))return AccessibleTabKey(NativeChrome::TabIndex(id),true);if(NativeChrome::IsTab(id))return AccessibleTabKey(NativeChrome::TabIndex(id));if(NativeChrome::IsBookmark(id))return AccessibleBandItemKey(id);return id;}
// Chrome region id currently realising a key (0 once its tab is gone).
int AccessibleRegion(long long key){
    const long long kind=key>>32;
    if(kind==4){for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==(BM::Id)(key&0xffffffff))return NativeChrome::BookmarkBase+i;return 0;}
    if(kind!=1&&kind!=2)return (int)key;
    for(int i=0;i<(int)pages.size();++i)if(pages[i]->serial==(unsigned)(key&0xffffffff))return (kind==2?NativeChrome::CloseBase:NativeChrome::TabBase)+i;
    return 0;
}
// Spoken names: the tooltip text without its shortcut, which goes to AcceleratorKey.
struct AccessibleLabel { std::wstring name,accelerator,help; const wchar_t* id; };
AccessibleLabel AccessibleButton(int id){
    switch(id){
    // i18n-exempt-begin: the 4th field of each label is the UIA automation id (stable identifier)
    case 900:if(chrome.profileButton.shown&&chrome.profileButton.privateWindow)return {TR(a11y_private_window),TR(key_ctrl_shift_m),TRF(a11y_private_window_help,TRP(a11y_open_tabs,pages.size())),L"ProfileButton"};
        if(chrome.profileButton.shown)return {TRF(a11y_profile,chrome.profileButton.name),TR(key_ctrl_shift_m),TRF(a11y_profile_menu_help,TRP(a11y_open_tabs,pages.size())),L"ProfileButton"};
        return {TR(a11y_tab_list),L"",TRP(a11y_open_tabs,pages.size()),L"TabList"};
    case 'Y':return {TR(history_title),L"Ctrl+H",L"",L"History"};
    case 'T':return {TR(tab_new),L"Ctrl+T",L"",L"NewTab"};
    case 'm':return {TR(chrome_tip_minimize),L"",L"",L"Minimize"};
    case 'M':return {window&&IsZoomed(window)?TR(a11y_restore):TR(a11y_maximize),L"",L"",L"Maximize"};
    case 'X':return {TR(chrome_tip_close_window),L"Alt+F4",L"",L"Close"};
    case 'B':return {TR(a11y_back),TR(key_alt_left),L"",L"Back"};
    case 'F':return {TR(a11y_forward),TR(key_alt_right),L"",L"Forward"};
    case 'R':return {chrome.navigationLoading?TR(a11y_stop):TR(cmd_reload),L"Ctrl+R",L"",L"Reload"};
    case 'C':return {TR(chrome_tip_copy_link),L"",L"",L"CopyLink"};
    case 'K':return {chrome.starred?TR(bookmark_title_edit):TR(a11y_add_bookmark),L"Ctrl+D",L"",L"Bookmark.Star"};
    case 'O':{const size_t hidden=chrome.bandItems.size()-std::min(chrome.bandItems.size(),(size_t)chrome.band.visible),other=bookmarks.Root(BM::OtherRoot).children.size();return {TRF(a11y_other_bookmarks_count,std::to_wstring(hidden+other)),L"",TR(a11y_other_bookmarks_help),L"Bookmarks.Overflow"};}
    case 'E':return {TR(chrome_bookmarks_add_page_full),L"Ctrl+D",L"",L"Bookmarks.AddPage"};
    case 'D':return {TR(chrome_tip_downloads),L"",L"",L"Downloads"};
    case 'V':return {AU::ButtonName(AudioRows(NativeChrome::Motion::Now()),audioToolbar.Music()),L"",TR(a11y_audio_help),L"SiteAudio"};
    case 'P':return {TR(a11y_new_private_window),TR(key_ctrl_shift_n),privateWindow?TR(a11y_private_this_window_help):TR(a11y_private_new_window_help),L"Private"};
    case 'S':return {TR(chrome_tip_settings),L"",L"",L"Settings"};
    case 'N':return {TR(a11y_annotate),TR(key_ctrl_shift_a),chrome.AnnotateHelp(),L"AnnotateButton"};
    // i18n-exempt-end
    default:return {L"",L"",L"",L""};
    }
}
bool AddressOpen(){return editing&&address&&IsWindowVisible(address);}
// Snapshot of the chrome as UI Automation nodes, in reading order:
// tab list button, tabs (each with its close button), new tab, window buttons, toolbar.
std::vector<Accessibility::Node> AccessibleNodes(){
    using Accessibility::Node;using Accessibility::Role;namespace TS=NativeChrome::Design::TabStrip;
    std::vector<Node> nodes;
    if(InFullscreen())return nodes;   // the page alone: no tab strip, toolbar or band to read or focus
    auto screen=[](D2D1_RECT_F r){RECT p=ChromePx(r);MapWindowPoints(window,nullptr,(POINT*)&p,2);return p;};   // Mode Focus: the panel at rest
    auto region=[](int id)->const NativeChrome::Region*{for(auto& r:chrome.regions)if(r.id==id)return &r;return nullptr;};
    auto button=[&](int id,long long parent){
        auto* r=region(id);if(!r)return;auto label=AccessibleButton(id);
        Node n;n.key=id;n.parent=parent;n.name=label.name;n.accelerator=label.accelerator;n.help=label.help;n.automationId=label.id;
        n.bounds=screen(r->rect);n.enabled=chrome.Enabled(id);n.focusable=r->focusable&&n.enabled;nodes.push_back(std::move(n));
    };
    button(900,0);
    Node strip;strip.key=AccessibleStrip;strip.role=Role::Tab;strip.name=TR(settings_tabs);strip.automationId=L"Tabs";strip.focusable=false;
    strip.bounds=screen({chrome.strip.ViewLeft(),TS::regionTop,chrome.strip.ViewRight(),TS::regionBottom});nodes.push_back(strip);
    for(int i=0;i<(int)pages.size();++i){
        const auto& page=*pages[i];const std::wstring title=page.title.empty()?std::wstring(TR(tab_untitled)):page.title;
        Node tab;tab.key=AccessibleTabKey(i);tab.parent=AccessibleStrip;tab.role=Role::TabItem;tab.name=title;tab.automationId=L"Tab";
        tab.selected=i==current;tab.position=i+1;tab.count=(int)pages.size();
        if(i<8)tab.accelerator=L"Ctrl+"+std::to_wstring(i+1);else if(i==(int)pages.size()-1)tab.accelerator=L"Ctrl+9";
        if(page.loading.IsLoading())tab.status=TR(a11y_tab_loading);
        if(page.sleeping)tab.status+=tab.status.empty()?std::wstring(TR(a11y_tab_sleeping)):TRF(a11y_status_and,TR(a11y_tab_sleeping_lower));
        if(page.group&&splitGroups.count(page.group)){auto m=Members(page.group);if(m.size()>=2){const int k=(int)(std::find(m.begin(),m.end(),pages[i])-m.begin());
            std::wstring g=page.group==VisibleGroup()&&!page.shown?TRF(a11y_tab_split_folded,std::to_wstring(k+1),std::to_wstring(m.size())):TRF(a11y_tab_split,std::to_wstring(k+1),std::to_wstring(m.size()));tab.status+=tab.status.empty()?g:L", "+g;}}
        // Tabs scrolled out of the strip stay listed (and selectable) but off screen.
        if(auto* r=region(NativeChrome::TabBase+i)){tab.bounds=screen(r->rect);tab.focusable=r->focusable;}else{tab.offscreen=true;tab.focusable=false;}
        nodes.push_back(tab);
        if(auto* r=region(NativeChrome::CloseBase+i)){
            Node close;close.key=AccessibleTabKey(i,true);close.parent=tab.key;close.name=TRF(a11y_close_tab,title);
            if(i==current)close.accelerator=L"Ctrl+W";
            close.automationId=L"CloseTab";close.bounds=screen(r->rect);close.enabled=chrome.Enabled(r->id);nodes.push_back(std::move(close));
        }
    }
    button('T',0);button('m',0);button('M',0);button('X',0);
    Node bar;bar.key=AccessibleToolbar;bar.role=Role::ToolBar;bar.name=TR(a11y_toolbar);bar.automationId=L"Toolbar";bar.focusable=false;
    bar.bounds=screen({6,chrome.bar.top,chrome.width-6,chrome.bar.bottom});nodes.push_back(bar);
    button('B',AccessibleToolbar);button('F',AccessibleToolbar);button('R',AccessibleToolbar);
    // Security indicator (left of the address, so read first): icon + label; invoking it
    // opens the address.
    if(auto* r=region('I')){Node n;n.key='I';n.parent=AccessibleToolbar;n.name=chrome.address.tip();n.automationId=L"SiteSecurity";n.bounds=screen(r->rect);n.focusable=false;nodes.push_back(std::move(n));}
    // B01 pending-request chip, right after the security indicator: invoking it focuses the open popover.
    if(auto* r=region(NativeChrome::Chrome::VaultKeyId)){Node n;n.key=NativeChrome::Chrome::VaultKeyId;n.parent=AccessibleToolbar;n.name=TR(chrome_tip_fill_password);n.automationId=L"VaultFill";n.bounds=screen(r->rect);nodes.push_back(std::move(n));}
    if(auto* r=region('Q')){Node n;n.key='Q';n.parent=AccessibleToolbar;n.name=chrome.requestTip;n.automationId=L"SitePermissionRequest";n.bounds=screen(r->rect);n.focusable=false;nodes.push_back(std::move(n));}
    // While the real EDIT is open it stands for the address itself (named through
    // AccessibleHost().label), with its own value, text and caret.
    if(auto* r=region('L');r&&!AddressOpen()){
        Node edit;edit.key='L';edit.parent=AccessibleToolbar;edit.role=Role::Edit;edit.name=TR(a11y_address);edit.accelerator=L"Ctrl+L";edit.automationId=L"Address";
        edit.bounds=screen(r->rect);
        if(auto t=Active();t&&!t->home)edit.value=t->uri;
        nodes.push_back(std::move(edit));
    }
    // Suggestion list while the EDIT is open: focus stays in the EDIT, the active row
    // is the selected list item (combobox / aria-activedescendant model).
    if(AddressOpen()&&suggestions.Visible()){
        Node list;list.key=AccessibleSuggestions;list.parent=AccessibleToolbar;list.role=Role::List;list.name=TR(suggest_window_name);list.automationId=L"AddressSuggestions";list.focusable=false;list.bounds=suggestions.Bounds();nodes.push_back(list);
        const int n=(int)suggestions.Data().rows.size();
        for(int i=0;i<n;++i){Node item;item.key=AccessibleSuggestionKey(i);item.parent=AccessibleSuggestions;item.role=Role::ListItem;item.name=suggestions.Spoken(i);item.selected=i==suggestions.Active();item.position=i+1;item.count=n;item.focusable=false;item.bounds=suggestions.RowScreen(i);nodes.push_back(std::move(item));}
    }
    button('C',AccessibleToolbar);
    // The star: a toggle (On = this page is bookmarked) that also invokes.
    if(auto* r=region('K')){auto label=AccessibleButton('K');Node n;n.key='K';n.parent=AccessibleToolbar;n.name=label.name;n.accelerator=label.accelerator;n.automationId=label.id;n.bounds=screen(r->rect);n.enabled=chrome.Enabled('K');n.focusable=n.enabled;n.toggle=chrome.starred?2:1;nodes.push_back(std::move(n));}
    if(auto* r=region('N')){auto label=AccessibleButton('N');Node n;n.key='N';n.parent=AccessibleToolbar;n.name=label.name;n.accelerator=label.accelerator;n.help=label.help;n.automationId=label.id;n.bounds=screen(r->rect);n.enabled=chrome.Enabled('N');n.focusable=n.enabled;n.toggle=chrome.annotateActive?2:1;nodes.push_back(std::move(n));}
    for(int id:{'V','Y','D','S'})button(id,AccessibleToolbar);
    // #127 side panel: the fold button, « Tous les favoris (N) » / « Réduire », the edge (« Largeur du panneau »: ← → resize it).
    if(auto* r=region(NativeChrome::Chrome::SidebarToggleId)){Node n;n.key=NativeChrome::Chrome::SidebarToggleId;n.parent=AccessibleToolbar;n.name=r->tip?r->tip:L"";n.automationId=L"SidePanelFold";n.bounds=screen(r->rect);n.focusable=true;nodes.push_back(std::move(n));}
    if(auto* r=region(NativeChrome::Chrome::SidebarGridToggleId)){Node n;n.key=NativeChrome::Chrome::SidebarGridToggleId;n.parent=AccessibleToolbar;n.name=chrome.gridToggleTip;n.automationId=L"BookmarksGridToggle";n.bounds=screen(r->rect);n.expand=chrome.sidebarGridOpen?2:1;n.focusable=true;nodes.push_back(std::move(n));}
    if(auto* r=region(NativeChrome::Chrome::SidebarManageId)){Node n;n.key=NativeChrome::Chrome::SidebarManageId;n.parent=AccessibleToolbar;n.name=TR(bookmarks_manage);n.accelerator=TR(key_ctrl_shift_o);n.automationId=L"ManageBookmarks";n.bounds=screen(r->rect);n.focusable=true;nodes.push_back(std::move(n));}
    if(auto* r=region(NativeChrome::Chrome::SidebarEdgeId)){Node n;n.key=NativeChrome::Chrome::SidebarEdgeId;n.parent=AccessibleToolbar;n.name=TR(sidebar_tip_resize);n.help=TR(sidebar_resize_help);n.automationId=L"SidePanelEdge";n.bounds=screen(r->rect);n.focusable=true;nodes.push_back(std::move(n));}
    // Decision 7: the private chip is plain text for UIA (not a button: no focus, no Invoke).
    if(auto* r=region(NativeChrome::Chrome::PrivateChipId)){Node n;n.key=AccessiblePrivateChip;n.role=Role::Text;n.name=TR(a11y_private_chip);n.automationId=L"PrivateWindowChip";n.focusable=false;n.bounds=screen(r->rect);nodes.push_back(std::move(n));}
    // "Barre de favoris": every item of the bar root, those behind » listed off screen (still invocable).
    if(chrome.bandShown){
        Node band;band.key=AccessibleBandKey;band.role=Role::ToolBar;band.name=TR(bookmarks_bar_root);band.automationId=L"BookmarksBar";band.focusable=false;
        band.bounds=screen({6,BookmarksBar::Layout::top,chrome.width-6,BookmarksBar::Layout::bottom});nodes.push_back(band);
        const int n=(int)chrome.bandItems.size();
        for(int i=0;i<n;++i){const auto& item=chrome.bandItems[i];Node b;b.key=(4LL<<32)|item.id;b.parent=AccessibleBandKey;
            b.name=item.title.empty()?std::wstring(TR(a11y_untitled_bookmark)):item.title;b.automationId=L"Bookmark."+std::to_wstring(item.id);b.position=i+1;b.count=n;
            if(item.folder){b.help=FolderSummary(item.id);b.expand=chrome.bandOpen==NativeChrome::BookmarkBase+i?2:1;}
            else if(const auto* node=bookmarks.Find(item.id))b.help=ReadableUrl(node->url);
            if(auto* r=region(NativeChrome::BookmarkBase+i)){b.bounds=screen(r->rect);b.focusable=true;}else{b.offscreen=true;b.focusable=false;}
            nodes.push_back(std::move(b));}
        if(auto* r=region('O')){auto label=AccessibleButton('O');Node o;o.key='O';o.parent=AccessibleBandKey;o.name=label.name;o.help=label.help;o.automationId=label.id;o.bounds=screen(r->rect);o.expand=chrome.bandOpen=='O'?2:1;nodes.push_back(std::move(o));}
        if(region('E'))button('E',AccessibleBandKey);
    }
    return nodes;
}
// Keyboard focus on a drawn control (the window itself has focus). The open address
// EDIT and the page (WebView2) are real windows and report their own focus.
long long AccessibleFocus(){return window&&GetFocus()==window?AccessibleKey(keyboardFocus):0;}
void SyncAccessibility();
void AccessibleSetFocus(long long key){
    if((key>>32)==3||key==AccessibleSuggestions||key==AccessibleBandKey)return;
    const int id=AccessibleRegion(key);if(!id||halted)return;
    if(id=='L'){FocusAddress();return;}
    editing=false;ShowWindow(address,SW_HIDE);SetFocus(window);keyboardModality=true;keyboardFocus=id;InvalidateChrome();SyncAccessibility();
}
// UIA calls must not block (menus and message boxes are modal): actions are posted.
void AccessibleAction(int action,long long key){
    if((key>>32)==3){if(action==AccessibleInvoke||action==AccessibleSelect)OpenSuggestion((int)(key&0xffffffff));return;}
    if((key>>32)==4){
        // Band item, on the band or behind »: Invoke opens a link, Expand / Invoke a folder opens its menu.
        const BM::Id bid=(BM::Id)(key&0xffffffff);const auto* n=bookmarks.Find(bid);if(!n||halted)return;keyboardModality=true;
        const int region=AccessibleRegion(key);
        if(n->kind==BM::Kind::Link){OpenBookmarkUrl(n->url,0);return;}
        if(region){SetFocus(window);keyboardFocus=region;InvalidateChrome();FolderMenu(NativeChrome::BookmarkIndex(region),true);}
        else{SetFocus(window);keyboardFocus='O';RunBookmarkMenu(BookmarkFolderItems(bid),'O',true);}
        return;
    }
    const int id=AccessibleRegion(key);if(!id||halted)return;keyboardModality=true;
    if(action==AccessibleExpand&&id=='O'){SetFocus(window);keyboardFocus='O';InvalidateChrome();OverflowMenu(true);return;}
    switch(action){
    case AccessibleInvoke:
        if(NativeChrome::IsClose(id)){CloseAt(NativeChrome::TabIndex(id));break;}
        if(NativeChrome::IsTab(id))break;
        // A menu opens under the control it belongs to, as with Enter on that control.
        if(id==900||id=='S'||id=='K'||id=='O'){editing=false;ShowWindow(address,SW_HIDE);SetFocus(window);keyboardFocus=id;InvalidateChrome();}
        Command(id);break;
    case AccessibleSelect:if(NativeChrome::IsTab(id))Select(NativeChrome::TabIndex(id));break;
    case AccessibleSetValue:if(id=='L'){const auto value=std::move(accessibleAddress);accessibleAddress.clear();FocusAddress();SetWindowTextW(address,value.c_str());SendMessageW(address,EM_SETSEL,value.size(),value.size());}break;
    default:break;
    }
}
Accessibility::Host AccessibleHost(){
    Accessibility::Host host;host.window=window;host.nodes=AccessibleNodes;host.focused=AccessibleFocus;
    host.label=[](HWND child,Accessibility::Node& node){if(child&&folderRename.Owns(child)){node.name=TR(bookmark_field_name);node.automationId=L"BookmarkRename" /* i18n-exempt: UIA id */;return true;}if(!child||child!=address)return false;node.name=TR(a11y_address);node.accelerator=L"Ctrl+L";node.automationId=L"Address";return true;};
    host.invoke=[](long long key){PostMessageW(window,AccessibleMessage,AccessibleInvoke,(LPARAM)key);};
    host.select=[](long long key){PostMessageW(window,AccessibleMessage,AccessibleSelect,(LPARAM)key);};
    host.focus=AccessibleSetFocus;
    host.setValue=[](long long key,const std::wstring& value){accessibleAddress=value;PostMessageW(window,AccessibleMessage,AccessibleSetValue,(LPARAM)key);};
    host.expand=[](long long key,bool open){if(open)PostMessageW(window,AccessibleMessage,AccessibleExpand,(LPARAM)key);};
    host.toggle=[](long long key){PostMessageW(window,AccessibleMessage,AccessibleInvoke,(LPARAM)key);};
    return host;
}
// Raises what changed since the last call: tab set, selected tab, chrome focus.
struct { long long focus=0,selection=0; unsigned long long tabs=0; } accessibleState;
void SuggestionsStructureChanged(){if(accessibility.Listening())accessibility.ChildrenChanged(AccessibleToolbar);}
// The active row is announced while focus stays in the EDIT.
void AnnounceSuggestion(){
    if(!suggestions.Visible()||!accessibility.Listening())return;
    const int i=suggestions.Active(),n=(int)suggestions.Data().rows.size();const long long key=AccessibleSuggestionKey(i);
    accessibility.Raise(key,UIA_SelectionItem_ElementSelectedEventId);
    const auto text=TRF(suggestion_position,suggestions.Spoken(i),std::to_wstring(i+1),std::to_wstring(n));
    accessibility.Notify(key,text.c_str(),L"AddressSuggestion");
}
void SyncAccessibility(){
    if(!accessibility.Listening())return;
    unsigned long long tabs=pages.size();for(auto& t:pages)tabs=tabs*1000003ull+t->serial;
    if(tabs!=accessibleState.tabs){accessibleState.tabs=tabs;accessibility.ChildrenChanged(AccessibleStrip);}
    const long long selection=AccessibleTabKey(current);
    if(selection!=accessibleState.selection){accessibleState.selection=selection;accessibility.Raise(selection,UIA_SelectionItem_ElementSelectedEventId);}
    const long long focus=AccessibleFocus();
    if(focus!=accessibleState.focus){accessibleState.focus=focus;accessibility.Raise(focus,UIA_AutomationFocusChangedEventId);}
}
void AnnounceSplit(const std::wstring& text){accessibility.Notify(AccessibleTabKey(current),text.c_str(),L"SplitView");}
void AnnounceCopied(){accessibility.Notify('C',TR(link_copied),L"CopyLink");}
// Raised on the Réglages button: it always exists, unlike drawer rows.
void AnnounceDataCleared(){accessibility.Notify('S',TR(a11y_data_cleared),L"ClearData");}
// Raised on the History button that opened the drawer (the drawer is painted, it has no UIA tree of its own).
void AnnounceHistory(const std::wstring& text){accessibility.Notify('Y',text.c_str(),L"History");}   // i18n-exempt: UIA activity id
LRESULT CALLBACK AddressProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_IME_STARTCOMPOSITION)addressKeys.SetComposing(true);
    if(msg==WM_IME_ENDCOMPOSITION)addressKeys.SetComposing(false);
    if(msg==WM_CHAR&&addressKeys.ConsumeChar(w))return 0;
    if(msg==WM_GETDLGCODE){LRESULT code=DefSubclassProc(h,msg,w,l)|DLGC_WANTCHARS|DLGC_WANTARROWS;auto message=(MSG*)l;if(message&&!addressKeys.composing&&(message->wParam==VK_RETURN||message->wParam==VK_ESCAPE))code|=DLGC_WANTMESSAGE;return code;}
    if(msg==WM_LBUTTONDOWN&&keyboardModality){keyboardModality=false;InvalidateRect(window,nullptr,FALSE);}
    if(msg==WM_CONTEXTMENU){AddressContextMenu(l);return 0;}
    if(msg==WM_SETFOCUS){const LRESULT r=DefSubclassProc(h,msg,w,l);static NativeUI::AccentCaret caret;const auto a=NativeChrome::Design::Accent::Get();
        const COLORREF field=NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::raised);caret.Apply(h,field,NativeUI::AccentCaret::Mix(field,RGB(std::lround(a.r*255),std::lround(a.g*255),std::lround(a.b*255)),NativeChrome::Design::Accent::caretOpacity),std::max(1,S(1)),S(18));return r;}
    if(msg==WM_KILLFOCUS&&!contextMenu.Open()){HideSuggestions();editing=false;ShowWindow(h,SW_HIDE);InvalidateRect(window,nullptr,FALSE);}
    if(msg==WM_KEYDOWN&&(w==VK_BACK||w==VK_DELETE))addressDeleting=true;
    if(msg==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN)&&suggestions.Visible()&&!addressKeys.composing){MoveSuggestion(w==VK_UP?-1:1);return 0;}
    if(msg==WM_KEYDOWN&&w==VK_DELETE&&GetKeyState(VK_SHIFT)<0&&suggestions.Visible()){const int a=suggestions.Active();if(a>=0&&a<(int)suggestions.Data().rows.size()&&suggestions.Data().rows[a].kind==AddressBar::Kind::Site&&!suggestions.Data().rows[a].popular){RemoveSuggestion(a);return 0;}}
    if(msg==WM_KEYDOWN&&w==VK_ESCAPE&&addressKeys.HandleKey(VK_ESCAPE)){if(EscapeSuggestions())return 0;editing=false;ShowWindow(h,SW_HIDE);FocusPage();Update();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_RETURN&&addressKeys.HandleKey(VK_RETURN)&&suggestions.Visible()){OpenSuggestion(suggestions.Active());return 0;}
    if(msg==WM_KEYDOWN&&w==VK_RETURN&&addressKeys.returnHandled){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);editing=false;ShowWindow(h,SW_HIDE);{const auto d=Destination(s);if(IsSandboxUri(d))OpenSandbox(Active());else Navigate(Active(),d);}FocusPage();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_TAB&&addressKeys.HandleKey(VK_TAB)){editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);keyboardFocus=chrome.Enabled('C')?'C':chrome.Enabled('K')?'K':'Y';keyboardModality=true;InvalidateRect(window,nullptr,FALSE);SyncAccessibility();return 0;}
    return DefSubclassProc(h,msg,w,l);
}
// ---- Panneau latéral (#127, variante A finale) ----------------------------------------------------------------
// src/sidebar.h holds the Paper values and the geometry; Chrome::BuildSidebar / PaintSidebar draw the column. Here: the
// mode (« Position des onglets » `settings.tabsPosition`, compact by itself under 800 DIP without touching it),
// the fold animation (the page follows the column), the complete column opened over the page after 400 ms on the
// compact one (the page windows get a hole there: the page does not move), the vertical tab drag, the list's wheel.
// Focus mode (branch VALXXII/Mode-Focus, not in main) plugs in here: design/127-sidebar-plan.md § 4.
HWND PageHostWindow(Tab& t);
namespace SBar=NativeChrome::Sidebar;
SBar::Fold sidebarFold;SBar::Hover sidebarHover;SBar::FocusReveal sideReveal;NativeChrome::Motion::Spring sideSlide;NativeChrome::Motion::Tween sideFade;bool sideLive=false,sideDrawerHold=false,sidePolling=false;bool sidebarReady=false;int sidebarShownTab=-1;
// The panel's presented width (motion spec §5): a spring towards the profile's width; a rubber-banded release springs
// back to its bound (.3 s), a double-click goes to 248 (.35 s); at rest it sits on the saved width.
NativeChrome::Motion::Spring sideWidthSpring{.05f};bool sideWidthKnown=false;
constexpr UINT_PTR SidebarFoldTimerId=0x5161,SidebarHoverTimerId=0x5162;
double SidebarNowMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// --tab-layout=top|side|compact (Fix's tests; --side-panel = side): this run's layout, never saved. Réglages and the
// tab menu (SetTabsPosition) take over again; the fold button, Ctrl+Maj+, and the edge switch it for the run only.
BrowserSettings::TabsPosition LayoutInEffect(){return forcedLayout>=0?static_cast<BrowserSettings::TabsPosition>(forcedLayout):settings.tabsPosition;}
SBar::Mode SidebarEffective(){if(SidebarEdgeDragging())return chrome.sidebarMode;   // the edge drag sets it live (SidebarEdgeMove)
    const bool compact=LayoutInEffect()==BrowserSettings::TabsPosition::Compact;
    return SBar::Effective(compact?SBar::Mode::Compact:SBar::Mode::Complete,chrome.width);}
// One log line with the active layout at start and at every change (Fix's tests read it): « layout: top|side|compact »,
// « (narrow window) » when a window under 800 DIP shows the side panel compact by itself, « (this run) » when forced.
std::wstring LayoutLine(){std::wstring line=L"layout: ";
    if(!chrome.sidebar)line+=L"top";else{const bool chosenCompact=LayoutInEffect()==BrowserSettings::TabsPosition::Compact;const bool compact=SidebarEffective()==SBar::Mode::Compact;
        line+=compact?L"compact":L"side";if(compact&&!chosenCompact)line+=L" (narrow window)" /* i18n-exempt: log line read by tests */;if(!compact)line+=L" "+std::to_wstring((int)std::lround(chrome.sidebarWidth));}
    if(forcedLayout>=0)line+=L" (this run)" /* i18n-exempt: log line read by tests */;return line;}
bool layoutApplied=false;   // LogLayout waits for the first ApplyLayout (the settings are read): never a passing « top » at start
void LogLayout(){static std::wstring last;if(!window||!layoutApplied||SidebarEdgeDragging())return;   /* an edge drag logs its result at the release, not every pixel */const auto line=LayoutLine();if(line==last)return;last=line;Log(line);}
// The start switches: --tab-layout=top|side|compact (a whole word), else --side-panel (side); -1 when neither.
int TabLayoutArg(const wchar_t* args){if(!args)return -1;if(const wchar_t* tl=wcsstr(args,L"--tab-layout=")){tl+=13;
        for(int i=0;i<BrowserSettings::TabsPositionCount;++i){const char* k=BrowserSettings::Info(static_cast<BrowserSettings::TabsPosition>(i)).key;size_t n=0;while(k[n]&&tl[n]==(wchar_t)k[n])++n;if(!k[n]&&(!tl[n]||tl[n]==L' '||tl[n]==L'"'))return i;}return -1;}
    return wcsstr(args,L"--side-panel")?static_cast<int>(BrowserSettings::TabsPosition::Side):-1;}
// The window's layout (Val, 24 nov.): the tab bar at the top by default; the side panel when the setting « layout=
// side-panel » is on (Réglages row to come: Change::SidePanel) or for a run started with --side-panel (not saved).
// Called when the settings are loaded (start, profile switch) and when the setting changes: live, no restart. The
// panel's own complete / compact choice is kept either way.
bool SidePanelWanted(){return !smoke&&BrowserSettings::SidePanel(LayoutInEffect());}
// The one source of truth (Val, 24 nov.): Réglages, the tab menu, the fold button and Ctrl+Maj+, all set this value.
void SetTabsPosition(BrowserSettings::TabsPosition p){if(settings.tabsPosition==p&&forcedLayout<0)return;settings.tabsPosition=p;SettingChanged(BrowserSettings::Change::TabsPosition);}
// #127 (Val, B): the drawer docked open on the right (its column's width), 0 when none: the page stops 4 DIP before it.
float DrawerReserveDip(){if(InFullscreen()||halted)return 0;if(settingsDrawer.Open())return NativeChrome::SettingsUI::Layout::width;
    if((downloadDrawer.Visible()&&!downloadDrawer.Closing())||historyDrawer.Open()||bookmarksDrawer.Open())return NativeChrome::Downloads::DrawerLayout::width;
    if(audioPanel.Docked())return NativeChrome::AudioPanel::Layout::width;return 0;}
void SyncDrawerReserve(){static float applied=0;const float r=DrawerReserveDip();if(r==applied)return;applied=r;if(window&&!halted)Layout();}
void ApplyLayout(){
    layoutApplied=true;
    const bool want=SidePanelWanted();NativeChrome::ShortcutsPanel::SidePanelRows()=want;if(want==chrome.sidebar)return;
    if(tabDrag.tab)CancelTabDrag(true);sidebarHover.Close();chrome.sidebarOverlay=false;chrome.sidebarDrag=-1;chrome.sidebarInsert=-1;
    if(window){KillTimer(window,SidebarFoldTimerId);KillTimer(window,SidebarHoverTimerId);}
    for(auto& t:pages)if(t&&t->holed){if(t->host&&IsWindow(t->host))SetWindowRgn(t->host,nullptr,TRUE);t->holed=false;}
    chrome.sidebar=want;sidebarReady=false;sidebarShownTab=-1;chrome.ResetTabMotion();   // the other layout starts settled
    if(window){HideSuggestions();chrome.latched=0;CloseDownloadSurfaces(false);audioPanel.Close(false);settingsDrawer.Hide(false,false);historyDrawer.Hide(false,false);bookmarksDrawer.Hide(false,false);
        Layout();bookmarkToast.Reposition();RepositionElementSurfaces();}
    Log(want?L"LAYOUT side panel":L"LAYOUT top bar");
}
// The compact « » » popover (grid B « compact, 40 »): every bookmark in a card at the column's right, over the page
// (a hole in the page windows like the column over the page). Closed by « » », Échap, a click elsewhere or on the page,
// opening a bookmark, or the column leaving compact.
void SidebarPopover(bool open){if(!chrome.sidebar)open=false;if(open==chrome.sidebarPopover)return;chrome.sidebarPopover=open;chrome.sidebarPopoverScroll=0;BuildChrome();if(window){ApplyPages();InvalidateChrome();}
    accessibility.Notify('K',open?TR(sidebar_all_bookmarks_tip):L"",L"Bookmarks" /* i18n-exempt: UIA activity id */);}
// Before every Build: the mode, the drawn width and titles (fold), the column over the page, per-tab details.
void SidebarPrepare(){
    namespace V=NativeChrome::Design::Vertical;chrome.sidebarReserve=DrawerReserveDip();
    {const bool hidden=SideFocusWanted();if(hidden!=chrome.sideHidden){chrome.sideHidden=hidden;SideFocusReset();}}   // Mode Focus: the column hides
    // Drawers start under the window bar: 40 in the complete layout, 25 in the compact one (Paper 07 / 11); in Focus at the
    // page's top (4, under the annotation banner when it is there).
    V::DrawerTopValue()=!chrome.sidebar?V::top:chrome.sideHidden?std::max(chrome.annotationBand,SBar::Zone::inset):(SidebarEffective()==SBar::Mode::Compact?SBar::Zone::compactDrawerTop:SBar::Zone::drawerTop);
    if(window){bookmarkToast.pageLeftPx=chrome.sidebar?S(chrome.SidebarZoneLeft()):0;bookmarkToast.pageBottomPx=chrome.sidebar?S(SBar::Zone::inset):0;}
    if(!chrome.sidebar){chrome.sidebarResizable=false;chrome.sidebarEdge=0;LogLayout();return;}const double now=SidebarNowMs();const auto mode=SidebarEffective();
    // The complete width: the profile's « side-width », or the edge being dragged.
    if(!SidebarEdgeDragging()){const auto mnow=NativeChrome::Motion::Now();const float saved=SBar::Resize::Clamp((float)settings.sideWidth);
        if(!sideWidthKnown){sideWidthSpring.Snap(saved);sideWidthKnown=true;}
        else if(!sideWidthSpring.Moving(mnow)&&!sidebarFold.Moving(now)&&sideWidthSpring.Target()!=saved)sideWidthSpring.Snap(saved);
        else if(sideWidthSpring.Moving(mnow)&&sideWidthSpring.Target()!=saved)sideWidthSpring.Set(saved,mnow,SBar::Resize::settleResponse,motion.ReducedMotion());
        chrome.sidebarWidth=sideWidthSpring.Value(mnow);}
    sidebarFold.panel=chrome.sidebarWidth;
    if(!sidebarReady){sidebarFold.Set(mode,now,false);sidebarReady=true;}
    else if(mode!=sidebarFold.target){sidebarFold.Set(mode,now,!SidebarEdgeDragging()&&!motion.ReducedMotion()&&window&&IsWindowVisible(window)&&!IsIconic(window));if(window&&sidebarFold.Moving(now))SetTimer(window,SidebarFoldTimerId,15,nullptr);}
    sidebarFold.Update(now);sidebarFold.Settle(now);const bool moving=sidebarFold.Moving(now);   /* motion spec §2: the titles' fade-in starts past 70 % */
    if(sidebarFold.target!=SBar::Mode::Compact||moving){sidebarHover.Close();chrome.sidebarPopover=false;}
    chrome.sidebarMode=SidebarEdgeDragging()?chrome.sidebarMode:sidebarFold.CompleteLayout(now)?SBar::Mode::Complete:SBar::Mode::Compact;   /* folding: complete for the titles' 80 ms, then compact (its pieces glide); unfolding: complete at once */
    chrome.sidebarColumn=sidebarFold.ColumnWidth(now);chrome.sidebarTitles=sidebarFold.Titles(now);chrome.sidebarOverlay=sidebarHover.open;
    if(chrome.sideHidden){sidebarHover.Close();chrome.sidebarOverlay=false;if(sideReveal.wide){chrome.sidebarMode=SBar::Mode::Complete;chrome.sidebarTitles=1;}}   // Focus: its own way back (SideFocusEvaluate); the address in compact: the complete column
    chrome.sideMorphing=moving||chrome.MorphMoving(NativeChrome::Motion::Now());   // §2: the pieces glide while the column folds
    chrome.annotateDimmed=chrome.annotateShown&&AnnotationButtonDimmed();
    // The edge handle: docked, settled, and the window wide enough for the chosen layout (not compact by itself).
    chrome.sidebarResizable=!sidebarHover.open&&!moving&&chrome.width>=SBar::autoCompactBelow&&!InFullscreen()&&!chrome.sideHidden;if(!chrome.sidebarResizable&&!SidebarEdgeDragging())chrome.sidebarEdge=0;
    LogLayout();
    chrome.sideHome.clear();chrome.sideDetails.clear();for(auto& t:pages){chrome.sideHome.push_back(t->home);chrome.sideDetails.push_back(t->home?std::wstring():BareDomain(t->uri));}
    // The selected tab's row is brought into the list's view (a new tab, Ctrl+Tab, Ctrl+1…9).
    if(current!=sidebarShownTab&&current>=0&&current<(int)pages.size()){sidebarShownTab=current;const auto L=SBar::Compute(chrome.SideInput(chrome.sidebarOverlay?SBar::Mode::Complete:chrome.sidebarMode,(int)pages.size()));chrome.sidebarScroll=SBar::ScrollToShow(L,current);}
}
// The fold button and Ctrl+Maj+,: the user's choice flips (saved). In a window too narrow for the complete column,
// the choice is kept and the complete column opens over the page instead (Designer, answer 11).
void ToggleSidebar(){
    if(!chrome.sidebar)return;
    if(chrome.sideHidden){sideReveal.Toggle();SideRevealChanged();if(sideReveal.open){SetFocus(window);keyboardModality=true;keyboardFocus=NativeChrome::TabBase+std::max(0,current);InvalidateChrome();}return;}   // Mode Focus: in, then out
    if(chrome.width<SBar::autoCompactBelow){if(sidebarHover.open)sidebarHover.Close();else sidebarHover.Open();Layout();return;}
    sidebarHover.Close();
    // The fold button and Ctrl+Maj+, switch Panneau latéral <-> Panneau compact in the setting (a trial run forced with
    // --side-panel while the setting says « En haut » folds for this run only).
    using BrowserSettings::TabsPosition;
    if(forcedLayout>=0){forcedLayout=static_cast<int>(LayoutInEffect()==TabsPosition::Side?TabsPosition::Compact:TabsPosition::Side);Layout();return;}
    if(BrowserSettings::SidePanel(settings.tabsPosition)){settings.tabsPosition=settings.tabsPosition==TabsPosition::Side?TabsPosition::Compact:TabsPosition::Side;SettingChanged(BrowserSettings::Change::TabsPosition);}
}
// ---- « Redimensionner » the column (Paper « Redimensionner · 1 à 4 (validées) »; model in src/sidebar.h Resize) ----
// The edge (a 6 DIP handle inside the column along the page) is dragged live: the page follows, the grid re-flows; under
// 160 the column snaps to compact and back past 160 it opens at 220. The release saves: a complete column its width in
// the profile (« side-width ») and « Panneau latéral »; a compact one « Panneau compact » (the width is kept for the next
// time it opens). Double-click (or Entrée on the focused edge): 248, from compact too. Échap / a lost capture: undone.
// A run forced with --tab-layout keeps the layout change in the run (the width is saved either way).
struct EdgeDrag{bool active=false;float offset=0;SBar::Resize::State state{SBar::Mode::Complete,SBar::Complete::width};bool startedCompact=false;float raw=0;
    struct Sample{float x;double ms;};std::vector<Sample> trail;   // the pointer's last positions (velocity at the release)
    float Velocity()const{if(trail.size()<2)return 0;const auto& a=trail.front();const auto& b=trail.back();const double dt=(b.ms-a.ms)/1000;return dt>.001?(float)((b.x-a.x)/dt):0.f;}
} edgeDrag;DWORD edgeClickAt=0;
void SidebarWidthTo(float w,float response){const auto mnow=NativeChrome::Motion::Now();if(!sideWidthKnown||!window){sideWidthSpring.Snap(w);sideWidthKnown=true;return;}sideWidthSpring.Set(w,mnow,response,motion.ReducedMotion());if(window)SetTimer(window,0x5161 /* SidebarFoldTimerId */,15,nullptr);}
bool SidebarEdgeDragging(){return edgeDrag.active;}
constexpr UINT_PTR SidebarEdgeFadeTimerId=0x5163,SideFocusPollTimerId=0x5164,SideSlideTimerId=0x5165,SideSlideLiveTimerId=0x5166;
void SidebarEdgeLook(int state){if(edgeDrag.active)state=2;if(!chrome.sidebarResizable&&!edgeDrag.active)state=0;if(chrome.sidebarEdge==state)return;
    if(state==1&&chrome.sidebarEdge==0){chrome.edgeHoverAt=NativeChrome::Motion::Now();if(window)SetTimer(window,SidebarEdgeFadeTimerId,15,nullptr);}   // §5: the line fades in (120 ms) after 150 ms
    chrome.sidebarEdge=state;if(window){SyncCornerMasks();InvalidateChrome();}}
// The layout the edge leaves: « Panneau latéral » or « Panneau compact » (saved, or for this run when forced).
void SidebarEdgeMode(SBar::Mode m){using BrowserSettings::TabsPosition;const auto p=m==SBar::Mode::Compact?TabsPosition::Compact:TabsPosition::Side;
    if(forcedLayout>=0){forcedLayout=static_cast<int>(p);if(window)Layout();return;}
    if(settings.tabsPosition!=p)SetTabsPosition(p);else if(window)Layout();}
void SidebarEdgeAnnounce(){const std::wstring text=SidebarEffective()==SBar::Mode::Compact?std::wstring(TR(sidebar_resize_compact)):TRF(sidebar_resize_announce,std::to_wstring((int)std::lround(chrome.sidebarWidth)));
    accessibility.Notify(NativeChrome::Chrome::SidebarEdgeId,text.c_str(),L"PanelWidth" /* i18n-exempt: UIA activity id */);}
void SidebarEdgeReset(){settings.sideWidth=BrowserSettings::SideWidthDefault;SaveSettings();SidebarWidthTo(SBar::Complete::width,(float)SBar::Timing::foldResponse);   // §5: the same spring as the fold
    if(!window)chrome.sidebarWidth=SBar::Complete::width;SidebarEdgeMode(SBar::Mode::Complete);if(window)Layout();SidebarEdgeAnnounce();Log(L"SIDEBAR width reset to 248");}
bool SidebarEdgeDown(float x){if(!chrome.sidebar||!chrome.sidebarResizable)return false;ChromeTipHide();
    const DWORD now=GetMessageTime();if(edgeClickAt&&now-edgeClickAt<=GetDoubleClickTime()){edgeClickAt=0;SidebarEdgeReset();return true;}
    edgeClickAt=now;edgeDrag.active=true;edgeDrag.offset=chrome.side.column-x;edgeDrag.state={SidebarEffective(),chrome.sidebarWidth};keyboardFocus=0;keyboardModality=false;
    edgeDrag.startedCompact=edgeDrag.state.mode==SBar::Mode::Compact;edgeDrag.raw=chrome.side.column;edgeDrag.trail.clear();edgeDrag.trail.push_back({edgeDrag.raw,SidebarNowMs()});
    if(window){SetFocus(window);if(!splitTest)SetCapture(window);}SidebarEdgeLook(2);return true;}
void SidebarEdgeMove(float x){if(!edgeDrag.active)return;edgeDrag.raw=x+edgeDrag.offset;const double t=SidebarNowMs();
    edgeDrag.trail.push_back({edgeDrag.raw,t});while(edgeDrag.trail.size()>2&&t-edgeDrag.trail.front().ms>80)edgeDrag.trail.erase(edgeDrag.trail.begin());   // the last 80 ms
    const auto s=SBar::Resize::Dragging(edgeDrag.raw,edgeDrag.startedCompact);
    if(s.mode==edgeDrag.state.mode&&std::fabs(s.width-edgeDrag.state.width)<.01f)return;edgeDrag.state=s;
    chrome.sidebarMode=s.mode;if(s.mode==SBar::Mode::Complete){chrome.sidebarWidth=s.width;sideWidthSpring.Snap(s.width);sideWidthKnown=true;}
    if(window)Layout();}
void SidebarEdgeUp(bool commit){if(!edgeDrag.active)return;const auto s=edgeDrag.state;const float v=edgeDrag.Velocity(),raw=edgeDrag.raw;edgeDrag.active=false;if(window&&GetCapture()==window)ReleaseCapture();
    if(commit){
        const auto r=edgeDrag.startedCompact&&s.mode==SBar::Mode::Compact?SBar::Resize::Release{SBar::Mode::Compact,SBar::Resize::Clamp(raw),false}:SBar::Resize::Released(raw,v);
        if(r.mode==SBar::Mode::Complete){settings.sideWidth=BrowserSettings::SideWidth((int)std::lround(r.width));SaveSettings();
            if(r.springBack)SidebarWidthTo((float)settings.sideWidth,SBar::Resize::settleResponse);}   // §5: back to the bound
        else if(s.mode==SBar::Mode::Complete){   // §5: folded with the gesture's speed (.85 / .3 s) from the width it shows
            const double t=SidebarNowMs();const float w=chrome.sidebarWidth;sidebarFold.panel=w;sideWidthSpring.Snap(w);
            sidebarFold.Set(SBar::Mode::Compact,t,!motion.ReducedMotion()&&window&&IsWindowVisible(window),std::min(0.f,v)/std::max(1.f,w-SBar::Compact::width),SBar::Resize::throwDamping);
            if(window)SetTimer(window,0x5161 /* SidebarFoldTimerId */,15,nullptr);}
        SidebarEdgeMode(r.mode);
        Log(r.mode==SBar::Mode::Compact?std::wstring(L"SIDEBAR edge released (projected under 160): compact"):L"SIDEBAR width "+std::to_wstring(settings.sideWidth));}
    else{if(window)Layout();}
    chrome.sidebarEdge=0;POINT p{};if(window&&GetCursorPos(&p)){ScreenToClient(window,&p);const float d=GetDpiForWindow(window)/96.f;SidebarEdgeLook(chrome.Hit(p.x/d,p.y/d)==NativeChrome::Chrome::SidebarEdgeId?1:0);}
    if(window){SyncCornerMasks();InvalidateChrome();}if(commit)SidebarEdgeAnnounce();}
// Keyboard on the focused edge (a separator, like the split view's): ← → 16 DIP (Maj 64), Début 220, Fin 360, Entrée / Espace
// 248. Past the minimum to the left it goes compact; to the right from compact it opens at 220.
bool SidebarEdgeKey(WPARAM key){if(!chrome.sidebar)return false;const bool shift=GetKeyState(VK_SHIFT)<0;const float step=shift?64.f:16.f;
    const bool compact=SidebarEffective()==SBar::Mode::Compact;float w=chrome.sidebarWidth;SBar::Mode m=compact?SBar::Mode::Compact:SBar::Mode::Complete;
    if(key==VK_RETURN||key==VK_SPACE){SidebarEdgeReset();return true;}
    if(key==VK_LEFT){if(compact)return true;if(w<=SBar::Resize::minimum+.5f)m=SBar::Mode::Compact;else w=SBar::Resize::Clamp(w-step);}
    else if(key==VK_RIGHT){if(compact){m=SBar::Mode::Complete;w=SBar::Resize::minimum;}else w=SBar::Resize::Clamp(w+step);}
    else if(key==VK_HOME){m=SBar::Mode::Complete;w=SBar::Resize::minimum;}
    else if(key==VK_END){m=SBar::Mode::Complete;w=SBar::Resize::maximum;}
    else return false;
    keyboardModality=true;if(m==SBar::Mode::Complete){settings.sideWidth=BrowserSettings::SideWidth((int)std::lround(w));chrome.sidebarWidth=(float)settings.sideWidth;SaveSettings();}
    SidebarEdgeMode(m);if(window){Layout();SyncCornerMasks();InvalidateChrome();}SidebarEdgeAnnounce();return true;}
// While the complete column is open over the page, every page window under it gets a window region without the
// column: the main window draws the column there and gets the pointer; the page keeps its size and place.
void SidebarHoles(){
    const bool focusCol=chrome.sidebar&&chrome.sideHidden&&chrome.sideShown&&!InFullscreen();   // Mode Focus: the column back at rest
    const bool over=chrome.sidebar&&(chrome.sidebarOverlay||focusCol)&&!InFullscreen(),pop=chrome.sidebar&&chrome.sidebarPopover&&!InFullscreen()&&!chrome.sidePop.box.Empty();const bool open=over||pop;
    const RECT hole=focusCol?RECT{0,S(chrome.side.top),S(chrome.side.column+SBar::FocusSide::border),S(chrome.height)}:over?RECT{0,0,S(chrome.sideOver.column+SBar::Overlay::border),S(chrome.height)}:RECT{0,S(chrome.sidePop.box.top),S(chrome.sidePop.box.right),S(chrome.sidePop.box.bottom)};
    for(auto& t:pages){if(!t||!t->controller.p)continue;HWND h=t->host&&IsWindow(t->host)?t->host:(t->placedVisible==1?PageHostWindow(*t):nullptr);if(!h)continue;
        RECT w{};GetWindowRect(h,&w);MapWindowPoints(nullptr,window,(POINT*)&w,2);RECT cut{};
        if(open&&t->placedVisible==1&&IntersectRect(&cut,&hole,&w)){HRGN all=CreateRectRgn(0,0,w.right-w.left,w.bottom-w.top),sub=CreateRectRgn(cut.left-w.left,cut.top-w.top,cut.right-w.left,cut.bottom-w.top);
            CombineRgn(all,all,sub,RGN_DIFF);DeleteObject(sub);SetWindowRgn(h,all,TRUE);t->holed=true;}
        else if(t->holed){SetWindowRgn(h,nullptr,TRUE);t->holed=false;}}
}
// Pointer on the compact column: 400 ms of rest opens the complete column over the page (03); leaving it closes it.
// Val (24 nov.): the compact column ALWAYS stays icons: resting the pointer on it no longer opens the complete column over
// the page (the titles are the icons' tooltips, on hover and on keyboard focus). The column over the page remains only
// for the fold button in a window too narrow for the complete one; it closes when the pointer leaves it. The 400 ms
// hover-to-open (SBar::Hover, SidebarHoverDue) stays as the hook for Focus mode's hidden panel (plan § 4).
void SidebarPointer(float x,float y){(void)y;
    if(!chrome.sidebar||!sidebarHover.open||tabDrag.tab)return;
    if(x>=chrome.sideOver.column+SBar::Overlay::border){sidebarHover.Leave();Layout();}
}
void SidebarPointerLeft(){if(!chrome.sidebar||tabDrag.tab)return;KillTimer(window,SidebarHoverTimerId);const bool was=sidebarHover.open;sidebarHover.Leave();if(was&&!sidebarHover.open)Layout();}
void SidebarHoverDue(){KillTimer(window,SidebarHoverTimerId);if(!sidebarHover.Due(SidebarNowMs()))return;POINT p;GetCursorPos(&p);ScreenToClient(window,&p);const float d=GetDpiForWindow(window)/96.f;
    if(p.x<0||p.x/d>=chrome.side.column||p.y<0||p.y/d>=chrome.height){sidebarHover.Leave();return;}sidebarHover.Open();Layout();}
// ---- #127 × Mode Focus: the side layouts' hidden column (src/sidebar.h FocusSide; Designer 24 nov., Paper 2091-0 / 20EJ-0)
// Focus in a side layout hides the column: the page takes the window minus 4 DIP. The pointer resting 400 ms in the
// window's left 8 DIP brings it back over the page (under the annotation banner); it leaves when the pointer leaves it,
// unless something holds it (a drawer opened from it, a menu, a drag, the field being typed in). Ctrl+Maj+, (and the
// fold button) bring it / send it away and Échap sends it away. Over the pages it is a hole in their windows, like the
// column over the page in a narrow window; while it slides a picture of it moves instead (a moving hole would lag the
// pages by a frame, the same reason as Focus's top panel): in on a critically damped 0.3 s spring, out 0.2 s, from the
// displayed position (caught mid-flight, it turns back); reduced motion: in place, a 120 ms fade.
bool SideFocusWanted(){return settings.focusMode&&chrome.sidebar&&!halted&&!InFullscreen();}
struct SidePicture{HWND hwnd=nullptr;HDC mem=nullptr;HBITMAP bmp=nullptr;HGDIOBJ old=nullptr;int w=0,h=0,alpha=-1;bool shown=false,broken=false;} sidePic;
void SideRevealLive(bool live);
LRESULT CALLBACK SidePictureProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_NCHITTEST:return HTCLIENT;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_SETCURSOR:SetCursor(LoadCursor(nullptr,IDC_ARROW));return TRUE;
    // A press on the sliding column: at rest at once (live), then the same press on the column under the pointer.
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:case WM_LBUTTONDBLCLK:{if(sideReveal.open){sideSlide.Snap(1);sideFade.Snap(1);SideRevealLive(true);}POINT p{};GetCursorPos(&p);ScreenToClient(window,&p);return SendMessageW(window,msg,w,MAKELPARAM(p.x,p.y));}
    case WM_MOUSEMOVE:case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};MapWindowPoints(h,window,&p,1);return SendMessageW(window,msg,w,MAKELPARAM(p.x,p.y));}
    }
    return DefWindowProcW(h,msg,w,l);
}
void SidePictureRelease(){if(sidePic.mem){if(sidePic.old)SelectObject(sidePic.mem,sidePic.old);DeleteDC(sidePic.mem);}if(sidePic.bmp)DeleteObject(sidePic.bmp);sidePic.mem=nullptr;sidePic.bmp=nullptr;sidePic.old=nullptr;sidePic.alpha=-1;}
void SidePictureHide(){if(sidePic.hwnd&&sidePic.shown)ShowWindow(sidePic.hwnd,SW_HIDE);sidePic.shown=false;SidePictureRelease();}
// The picture at slide position `pos` (0 off to the left .. 1 at rest), `alpha` 0 .. 1 (reduced motion's fade).
void SidePictureMove(float pos,float alpha){
    if(!sidePic.shown||!sidePic.hwnd||!sidePic.mem)return;const float s=GetDpiForWindow(window)/96.f;
    const int a=(int)std::lround(std::clamp(alpha,0.f,1.f)*255);
    if(a!=sidePic.alpha){HDC screen=GetDC(nullptr);POINT src{0,0};SIZE size{sidePic.w,sidePic.h};BLENDFUNCTION blend{AC_SRC_OVER,0,(BYTE)a,AC_SRC_ALPHA};
        UpdateLayeredWindow(sidePic.hwnd,screen,nullptr,&size,sidePic.mem,&src,0,&blend,ULW_ALPHA);ReleaseDC(nullptr,screen);sidePic.alpha=a;}
    SetWindowPos(sidePic.hwnd,HWND_TOP,(int)std::lround((std::clamp(pos,0.f,1.f)-1)*sidePic.w),(int)std::lround(chrome.side.top*s),0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
// A fresh picture of the column at rest. False when it cannot be made: the column then comes and goes in place (live).
bool SidePictureShow(){
    if(sidePic.broken||!window)return false;
    SyncAddressDisplay();std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;
    for(auto& t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));
    {std::vector<bool> asleep;for(auto& t:pages)asleep.push_back(t->sleeping);chrome.sleeping=std::move(asleep);}
    std::vector<std::uint32_t> px;UINT w=0,h=0;
    if(!chrome.RenderSideColumn(titles,current,editing,0,0,NativeChrome::Motion::Now(),px,w,h)){sidePic.broken=true;Log(L"FOCUS column picture unavailable: the column comes in place");return false;}
    HINSTANCE inst=instance?instance:GetModuleHandleW(nullptr);
    if(!sidePic.hwnd){static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=SidePictureProc;wc.hInstance=inst;wc.lpszClassName=L"BrowserSideSlide";wc.style=CS_DBLCLKS;RegisterClassW(&wc);registered=true;}
        sidePic.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,L"BrowserSideSlide",L"",WS_CHILD,0,0,1,1,window,nullptr,inst,nullptr);
        if(!sidePic.hwnd){sidePic.broken=true;Log(L"FOCUS column picture window unavailable: the column comes in place");return false;}}
    SidePictureRelease();
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=(LONG)w;bi.bmiHeader.biHeight=-(LONG)h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);sidePic.mem=CreateCompatibleDC(screen);void* bits=nullptr;sidePic.bmp=CreateDIBSection(sidePic.mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);ReleaseDC(nullptr,screen);
    if(!sidePic.mem||!sidePic.bmp||!bits){SidePictureRelease();sidePic.broken=true;Log(L"FOCUS column picture refused: the column comes in place");return false;}
    memcpy(bits,px.data(),px.size()*4);sidePic.old=SelectObject(sidePic.mem,sidePic.bmp);sidePic.w=(int)w;sidePic.h=(int)h;sidePic.alpha=-1;sidePic.shown=true;
    return true;
}
// Live: the column painted by the window in a hole of the pages (at rest); not live: the pages whole again.
void SideRevealLive(bool live){
    if(live==sideLive)return;sideLive=live;chrome.sideShown=live&&chrome.sideHidden;
    BuildChrome();SidebarHoles();SyncCornerMasks();InvalidateRect(window,nullptr,FALSE);if(!IsIconic(window))UpdateWindow(window);
    if(live&&sidePic.shown)SetTimer(window,SideSlideLiveTimerId,60,nullptr);   // the picture bridges the frame the pages' new region takes
    Log(live?L"FOCUS column shown":L"FOCUS column away");
}
void SideSlideFrame(){
    if(!window)return;const auto now=NativeChrome::Motion::Now();
    const float pos=sideSlide.Value(now),alpha=sideFade.Value(now);SidePictureMove(pos,alpha);
    if(sideSlide.Moving(now)||sideFade.Moving(now))return;
    KillTimer(window,SideSlideTimerId);
    if(sideReveal.open)SideRevealLive(true);else SidePictureHide();
}
// The reveal changed (pointer, shortcut, Échap, the address): the slide (or the fade) from where it is shown.
void SideRevealChanged(){
    if(!window||!chrome.sideHidden)return;const auto now=NativeChrome::Motion::Now();const bool reduced=motion.ReducedMotion(),open=sideReveal.open;
    namespace FS=SBar::FocusSide;
    if(!open&&(GetFocus()==window||editing)){keyboardFocus=0;FocusPage();}   // the column goes: the keyboard back to the page
    if(!open&&sideLive){if(!SidePictureShow()){SideRevealLive(false);return;}sideSlide.Snap(1);sideFade.Snap(1);SidePictureMove(1,1);SideRevealLive(false);}
    else if(open&&!sideLive&&!sidePic.shown){if(!SidePictureShow()){SideRevealLive(true);return;}if(reduced){sideSlide.Snap(1);sideFade.Snap(0);}else{sideFade.Snap(1);if(sideSlide.Value(now)<=0)sideSlide.Snap(0);}SidePictureMove(sideSlide.Value(now),sideFade.Value(now));}
    if(reduced){sideSlide.Snap(1);sideFade.Set(open?1.f:0.f,now,std::chrono::milliseconds((int)FS::reducedFadeMs),false,NativeChrome::Motion::Curve::SmoothStep);}
    else{sideFade.Snap(1);sideSlide.Set(open?1.f:0.f,now,open?FS::enterResponse:FS::exitResponse);}
    SetTimer(window,SideSlideTimerId,15,nullptr);SideSlideFrame();
    Log(open?(sideReveal.pinned?L"FOCUS column in (shortcut)":L"FOCUS column in (left edge)"):L"FOCUS column out");
}
// Focus on / off or the layout changed: nothing shown, nothing moving.
void SideFocusReset(){
    sideReveal.Close();sideDrawerHold=false;sideSlide.Snap(0);sideFade.Snap(1);
    if(window){KillTimer(window,SideSlideTimerId);KillTimer(window,SideSlideLiveTimerId);}
    SidePictureHide();if(sideLive){sideLive=false;chrome.sideShown=false;}
    if(!chrome.sideHidden&&sidePolling&&window){KillTimer(window,SideFocusPollTimerId);sidePolling=false;}
    if(chrome.sideHidden&&window)SideFocusEvaluate();
}
// What keeps the column while the pointer is elsewhere: Focus's own rules (a menu, the field, a drag, a popover…), a
// drawer opened from the column (Designer: it stays while that drawer is open), the « » » popover.
bool FocusHold();bool FocusKeep();
bool SideFocusHolds(){
    if(sideDrawerHold&&DrawerReserveDip()<=0&&!downloadDrawer.Visible())sideDrawerHold=false;
    return sideDrawerHold||chrome.sidebarPopover||FocusHold()||FocusKeep();
}
// Every ~30 ms while the window is in front or the column is there (over a page the window gets no mouse message).
void SideFocusEvaluate(){
    if(!window)return;
    if(!chrome.sideHidden){if(sidePolling){KillTimer(window,SideFocusPollTimerId);sidePolling=false;}return;}
    POINT sp{};const bool pointer=GetCursorPos(&sp)!=FALSE;POINT cp=sp;ScreenToClient(window,&cp);
    const float k=96.f/GetDpiForWindow(window),x=cp.x*k,y=cp.y*k;
    HWND fg=GetForegroundWindow();const bool front=fg&&(fg==window||GetAncestor(fg,GA_ROOTOWNER)==window)&&IsWindowVisible(window)&&!IsIconic(window);
    const bool buttons=GetAsyncKeyState(VK_LBUTTON)<0||GetAsyncKeyState(VK_RBUTTON)<0||GetAsyncKeyState(VK_MBUTTON)<0;
    RECT wr{};GetWindowRect(window,&wr);const bool ours=pointer&&front&&PtInRect(&wr,sp);
    const float top=chrome.side.top;namespace FS=SBar::FocusSide;
    const bool inBand=ours&&!buttons&&x>=0&&x<FS::edge&&y>=top&&y<chrome.height;
    const bool overColumn=ours&&(sideLive||sidePic.shown)&&x>=0&&x<chrome.side.column+FS::border&&y>=top&&y<chrome.height;
    const double ms=SidebarNowMs();
    if(sideReveal.Pointer(ms,inBand,overColumn,sideReveal.open&&SideFocusHolds()))SideRevealChanged();
    const bool poll=front||sideReveal.open||sidePic.shown;
    if(poll&&!sidePolling)sidePolling=SetTimer(window,SideFocusPollTimerId,30,nullptr)!=0;
    else if(!poll&&sidePolling){KillTimer(window,SideFocusPollTimerId);sidePolling=false;}
}
// The field needs the column (Ctrl+L, F6, typing): at rest at once (the keyboard does not wait for a slide).
void SideFocusForAddress(){
    if(!chrome.sideHidden||!window)return;
    sideReveal.ForAddress(SidebarEffective()==SBar::Mode::Compact);KillTimer(window,SideSlideTimerId);SidePictureHide();sideSlide.Snap(1);sideFade.Snap(1);
    if(!sideLive)SideRevealLive(true);else BuildChrome();
    Layout();
}
// A drawer opened from the shown column keeps it (Designer, board 15's rule).
void SideFocusCommand(int cmd){
    if(!chrome.sideHidden||!sideReveal.open)return;
    if(cmd=='Y'||cmd=='S'||cmd=='D'||cmd=='V'||cmd==BookmarksDrawerCommand||cmd==NativeChrome::Chrome::SidebarManageId)sideDrawerHold=true;
}
bool SideFocusEscape(){if(!chrome.sideHidden||!sideReveal.open)return false;sideReveal.Close();sideDrawerHold=false;SideRevealChanged();return true;}
// Window dragging: the bar right of the column, and the column's top above the field (complete) / the fold button (compact).
bool SidebarCaption(float x,float y){if(!chrome.sidebar)return false;const auto& L=chrome.SideColumn();const float col=chrome.sidebarOverlay?L.column:chrome.side.column;
    if(x>=col)return y<chrome.side.bar;return y<(L.mode==SBar::Mode::Complete?L.address.top:L.toggle.bottom);}
// Wheel over the tab list scrolls it (answer 4).
void SidebarWheel(POINT client,int delta){const float d=GetDpiForWindow(window)/96.f,x=client.x/d,y=client.y/d;const auto& L=chrome.SideColumn();
    if(chrome.sidebarPopover&&chrome.sidePop.box.Contains(x,y)){chrome.sidebarPopoverScroll=std::clamp(chrome.sidePop.grid.scroll-delta/120.f*48.f,0.f,chrome.sidePop.grid.maxScroll);BuildChrome();InvalidateChrome();return;}
    if(L.gridOpen&&L.grid.view.Contains(x,y)){chrome.sidebarGridScroll=std::clamp(L.grid.scroll-delta/120.f*48.f,0.f,L.grid.maxScroll);BuildChrome();InvalidateChrome();return;}
    if(!L.tabsView.Contains(x,y))return;
    chrome.sidebarScroll=std::clamp(L.scroll-delta/120.f*48.f,0.f,L.maxScroll);BuildChrome();InvalidateChrome();}
// A right click on the list's empty space opens the tab list menu, as on the empty tab row before.
// Vertical drag in the list (05): the row follows the pointer, its place stays hollow, an accent line marks the drop.
struct RowSample{float y;double ms;};std::vector<RowSample> rowTrail;   // the lifted row's last positions (its speed at the drop)
bool SidebarDragMove(POINT point){
    if(!tabDrag.tab)return false;const float d=GetDpiForWindow(window)/96.f,y=point.y/d;
    const int from=TabIndexOf(tabDrag.tab);if(from<0){CancelTabDrag();return true;}
    const auto& L=chrome.SideColumn();if(from>=(int)L.tabs.size())return true;
    const float h=L.tabs[from].Height();
    // §1: 1:1 from the grab point; past the list's ends a rubber band instead of a hard stop.
    {const float raw=y-tabDrag.grabY,lo=L.tabsView.top-h/2,hi=L.tabsView.bottom-h/2;chrome.sidebarDragY=raw<lo?lo-SBar::Resize::Rubber(lo-raw)*.5f:raw>hi?hi+SBar::Resize::Rubber(raw-hi)*.5f:raw;}
    {const double t=SidebarNowMs();rowTrail.push_back({chrome.sidebarDragY,t});while(rowTrail.size()>2&&t-rowTrail.front().ms>80)rowTrail.erase(rowTrail.begin());}
    chrome.sidebarDrag=from;
    chrome.sidebarInsert=SBar::BlockMove(L.split,from,SBar::DropSlot(L,y)).insert;   // a split view block moves whole, never lands inside another
    if(y<L.tabsView.top+16&&L.scroll>0)chrome.sidebarScroll=std::max(0.f,L.scroll-8);else if(y>L.tabsView.bottom-16&&L.scroll<L.maxScroll)chrome.sidebarScroll=std::min(L.maxScroll,L.scroll+8);
    InvalidateChrome();return true;
}
// Split view from the column (answer 9, Paper « Vue partagée dans la colonne » 3): a row dragged past the column's right
// edge into the page lifts like a top bar tab (SplitDragMove: the same card, drop preview, refusal and split creation);
// back 20 DIP inside the column, reordering resumes. The column open over the page (narrow window) is column too: its
// own edge, and it closes at the lift so the whole page takes the drop.
float SidebarDropEdge(){if(chrome.sideHidden)return chrome.sideShown?chrome.side.column+SBar::FocusSide::border:ContentBox().left;return chrome.sidebarOverlay?chrome.sideOver.column+SBar::Overlay::border:ContentBox().left;}
void SidebarLift(){
    chrome.sidebarDrag=-1;chrome.sidebarInsert=-1;   // no row follows in the list: the row's place stays empty (liftedIndex)
    // The card fits its title (Paper 24BU-0: 8 · favicon 16 · 8 · title 14 medium · 10), at most 240 like the drop chip;
    // it keeps the grab point when it can, never so far right that the card is left behind the pointer.
    tabDrag.cardW=std::min(240.f,8+16+8+splitOverlay.painter.TextWidth(TabLabel(tabDrag.tab),true)+10);tabDrag.grab=std::min(tabDrag.grab,tabDrag.cardW-16);
    if(sidebarHover.open){sidebarHover.Leave();Layout();}
}
// Released outside the window (the release point `up`, client px): the tab opens in a new window (answer 9). True when handled.
bool SidebarTearOff(POINT up){
    if(!chrome.sidebar||!tabDrag.tab||!tabDrag.active)return false;ClientToScreen(window,&up);RECT w;GetWindowRect(window,&w);if(PtInRect(&w,up))return false;
    auto t=tabDrag.tab;CancelTabDrag(true);
    if(!t->home&&!t->uri.empty()&&pages.size()>1&&!privateWindow){if(splitAnim.returning)FinishSplitAnimation(); /* lifted onto the page first: the card leaves with the tab, no flight home */
        OpenWindowWith({t->uri});CloseAt(TabIndexOf(t));Log(L"SIDEBAR tab moved to a new window");}
    return true;
}
// A split view block dropped in the list (Paper 2473-0 « la paire se déplace d'un bloc »): its tabs move together, as MoveTab moves one.
void SidebarMoveBlock(int first,int count,int dest){
    if(count==1){MoveTab(first,dest);return;}
    if(halted||first<0||count<=0||first+count>(int)pages.size()||dest<0||dest+count>(int)pages.size()||dest==first)return;
    auto active=Active();SBar::MoveRange(pages,first,count,dest);
    current=(int)(std::find(pages.begin(),pages.end(),active)-pages.begin());
    motion.ClearInteractions();hover=pressed=keyboardFocus=0;Update();
}
void SidebarDragEnd(bool commit){
    const int from=chrome.sidebarDrag,to=chrome.sidebarInsert;chrome.sidebarDragTopAtDrop=chrome.sidebarDragY;chrome.sidebarDrag=-1;chrome.sidebarInsert=-1;
    if(!commit||from<0||!tabDrag.tab)return;
    const float d=GetDpiForWindow(window)/96.f,x=tabDrag.at.x/d,y=tabDrag.at.y/d;   // the last pointer of the drag (what the list showed)
    const auto& L=chrome.SideColumn();
    if(!L.gridBlock.Empty()&&L.gridBlock.Contains(x,y)){   // onto the grid: a new bookmark (answer 9)
        auto t=tabDrag.tab;if(t->home||!BM::Admissible(t->uri))return;
        const BM::Id id=bookmarks.Add(BM::BarRoot,BM::End,t->title==TR(tab_new)?L"":t->title,t->uri,NativeChrome::DownloadsUI::UnixNowMs());
        if(id){PushUndo({{BookmarkStep::Created,id}},TR(bookmark_undo_add));SaveBookmarks();BookmarksChanged();AnnounceBookmark(TRF(bookmark_added_to,bookmarks.Find(BM::BarRoot)->title));}
        return;}
    {const float v=rowTrail.size()>=2&&rowTrail.back().ms>rowTrail.front().ms?(float)((rowTrail.back().y-rowTrail.front().y)/((rowTrail.back().ms-rowTrail.front().ms)/1000)):0.f;rowTrail.clear();
        chrome.RowDropped(chrome.TabKey(from),chrome.sidebarDragTopAtDrop,v,NativeChrome::Motion::Now());}   // §1: to its place with the pointer's speed
    if(to>=0&&to!=from){const auto m=SBar::BlockMove(L.split,from,to);if(m.Moves())SidebarMoveBlock(m.first,m.count,m.dest);}
    if(window){InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}
}
void UpdateFont(){if(font)DeleteObject(font);font=CreateFontW(-S(chrome.addressFont),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");SendMessageW(address,WM_SETFONT,(WPARAM)font,TRUE);}
// WebView2 startup recovery (defined before wWinMain): installer worker notifications, shutdown.
constexpr UINT EngineInstallMessage=WM_APP+41;void OnEngineInstallMessage();void StopEngineStart();
// « Importer depuis un navigateur… » (src/browser-import-integration.h): the worker's result reaches the UI thread.
constexpr UINT ImportMessage=WM_APP+43;void ImportFinished();
// Icons of imported bookmarks fetched in the background (src/browser-import-integration.h): the bar redraws with them.
constexpr UINT ImportIconsMessage=WM_APP+44;void ImportIconsArrived();
// The monitor a maximized window fills. Restored from minimized, the window still sits at Windows' off-screen minimized
// position (-32000, -32000) during the first size calculation: the nearest monitor is then whichever lies closest to
// that point (a second screen on the left or above), the window was laid out once at that screen's size and back, and
// every tab's page resized twice in the middle of the restore animation (a visible hitch). Off every screen, the
// window's restore placement names the screen it maximizes on.
HMONITOR MaximizedMonitor(HWND h){if(HMONITOR m=MonitorFromWindow(h,MONITOR_DEFAULTTONULL))return m;WINDOWPLACEMENT p{sizeof(p)};return GetWindowPlacement(h,&p)?MonitorFromRect(&p.rcNormalPosition,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);}
LRESULT CALLBACK ChromeProc(HWND h,UINT msg,WPARAM w,LPARAM l){LRESULT frameResult;
    // Maximized with WS_CAPTION, Windows pushes the window past the monitor by its frame thickness: the client
    // is the monitor's work area exactly (nothing cut at the edges, the taskbar left visible).
    if(msg==WM_NCCALCSIZE&&w&&IsZoomed(h)){MONITORINFO mi{sizeof(mi)};if(GetMonitorInfoW(MaximizedMonitor(h),&mi)){reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0]=mi.rcWork;return 0;}}
    // WS_CAPTION makes DefWindowProc paint a classic caption straight onto the window when the title or icon changes
    // (and through the undocumented uxtheme caption / frame messages): the title and icon are stored with the caption
    // style briefly cleared, so nothing is drawn (NativeUI::DefWindowProcNoCaption); the two theme messages draw nothing.
    if(msg==0x00AE/*WM_NCUAHDRAWCAPTION*/||msg==0x00AF/*WM_NCUAHDRAWFRAME*/)return 0;
    // Activation changes (a drawer or popup taking the focus, Alt+Tab): the frame is ours, so DefWindowProc is not
    // called at all (it painted the classic frame over the chrome, as Chromium's custom frame avoids it too). Marking
    // the window invisible around it instead, as above, hid the engine's page windows for that instant: one composed
    // frame showed the canvas (#171717) in place of the page (measured: about one activation change in four).
    if(msg==WM_NCACTIVATE){InvalidateRect(h,nullptr,FALSE);return TRUE;}
    // Not by marking the window invisible (the earlier way, as for WM_NCACTIVATE above): that hid the pages for that instant
    // too, and as the page's title, address and history changes all set the window title, clicks in a site could flash
    // the canvas in place of the page.
    if(msg==WM_SETTEXT||msg==WM_SETICON)return NativeUI::DefWindowProcNoCaption(h,msg,w,l);if(NativeUI::HandleFrame(h,msg,w,l,frameResult))return frameResult;if(msg==ProfilesChangedMessage()){ReloadProfiles();return 0;}switch(msg){
case WM_CREATE:window=h;address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,h,nullptr,instance,nullptr); NativeUI::SelectAllOnCtrlA(address);SendMessageW(address,EM_SETLIMITTEXT,8192,0);SendMessageW(address,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowSubclass(address,AddressProc,1,0);addressSelection.Attach(address,AddressSelectionColors);accessibility.Attach(AccessibleHost());tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,h,nullptr,instance,nullptr);NativeUI::StripTooltipShadow(tooltip);return 0;
// Alt alone (or F10) asks for the menu bar; this window has none, and the system-menu state it starts instead freezes
// the WebView that had the keyboard (page or chat panel) until the next key. Alt+Espace (lParam = the key) still opens the system menu.
case WM_SYSCOMMAND:if((w&0xFFF0)==SC_KEYMENU&&l==0)return 0;break;
case WM_NCHITTEST:{if(InFullscreen())return HTCLIENT;POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);int edge=S(4);if(!IsZoomed(h)){bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<S(3),bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}float x=p.x*96.f/GetDpiForWindow(h),y=p.y*96.f/GetDpiForWindow(h);int id=chrome.Hit(x,y);if(id=='M')return HTMAXBUTTON;if(chrome.sidebar){if(!id&&SidebarCaption(x,y))return HTCAPTION;return HTCLIENT;}
    if(chrome.Local(x,y)&&y<44&&!id)return HTCAPTION; /* the tab row's empty space (Mode Focus: only while the floating panel shows it) */return HTCLIENT;}
case WM_NCLBUTTONDOWN:if(w==HTMAXBUTTON){Command('M');return 0;}break;
// A click on the B01 request chip must not take activation from its popover (light dismiss
// would answer « fermé »): the chip's click then hands focus back to that same popover.
case WM_MOUSEACTIVATE:{if(!RequestChipOpen())break;POINT p;GetCursorPos(&p);ScreenToClient(h,&p);const float d=GetDpiForWindow(h)/96.f;if(chrome.Hit(p.x/d,p.y/d)=='Q')return MA_NOACTIVATE;break;}
case WM_NCMOUSEMOVE:if(w==HTMAXBUTTON){UpdateInteraction('M',0);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,h,0};TrackMouseEvent(&track);return 0;}break;
case WM_NCMOUSELEAVE:UpdateInteraction(0,0);return 0;
case WM_NCLBUTTONDBLCLK:if(w==HTCAPTION){Command('M');return 0;}break;
case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)l;info->ptMinTrackSize={S(560),S(300)};MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MaximizedMonitor(h),&monitor)){info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};}return 0;}
case WM_DPICHANGED:{contentTransition.Cancel();auto r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);UpdateFont();Layout();RepositionElementSurfaces();
#ifdef BROWSER_UPDATES
    LayoutUpdateToast();
#endif
    return 0;}
case WM_SIZE:HideSuggestions();FormsHideAll();motionMinimized=w==SIZE_MINIMIZED;contentTransition.Cancel(); /* resize / minimise / maximise */AnnotationWindowState();if(!motionMinimized){Layout();audioPanel.OwnerChanged();downloadDrawer.Reposition();settingsDrawer.Reposition();historyDrawer.Reposition();bookmarksDrawer.Reposition();bookmarkToast.Reposition();RepositionElementSurfaces();}else{CloseDownloadSurfaces(false);audioPanel.Close(false);settingsDrawer.Hide(false,false);historyDrawer.Hide(false,false);bookmarksDrawer.Hide(false,false);bookmarkPopover.Hide();bookmarkToast.HideNow();}ScheduleMotion(NativeChrome::Motion::Now());
#ifdef BROWSER_UPDATES
    // Minimised: the toast stops its clock/timer (zero wakes); otherwise SetOwnerVisible(true)
    // re-lays the card out (bottom-left anchor, drawer reserve kept).
    updateToast.SetOwnerVisible(!motionMinimized);
#endif
    return 0;
case WM_PARENTNOTIFY:{const UINT e=LOWORD(w);if(e==WM_LBUTTONDOWN||e==WM_RBUTTONDOWN||e==WM_MBUTTONDOWN||e==WM_XBUTTONDOWN||e==0x0246 /* WM_POINTERDOWN */){ContentTransitionInput(L"click");if(chrome.sidebarPopover)SidebarPopover(false);}break;}
case WM_MOVE:contentTransition.Cancel();RepositionElementSurfaces();HideSuggestions();FormsHideAll();SyncStatusBubble();audioPanel.OwnerChanged();downloadDrawer.Reposition();settingsDrawer.Reposition();historyDrawer.Reposition();bookmarksDrawer.Reposition();bookmarkToast.Reposition();if(bookmarkPopover.Visible())bookmarkPopover.Hide();
#ifdef BROWSER_UPDATES
    LayoutUpdateToast();
#endif
    return 0;
#ifdef BROWSER_UPDATES
case WM_EXITSIZEMOVE:LayoutUpdateToast();return 0;
case UpdateMessage:if(updatesStarted&&updates.OnNotify(w)){ShowUpdateView();ArmUpdateTimer();}return 0;
#endif
case WM_ERASEBKGND:return 1;
case WM_SETCURSOR:if(LOWORD(l)==HTCLIENT&&(HWND)w==h){POINT p;GetCursorPos(&p);ScreenToClient(h,&p);const float d=GetDpiForWindow(h)/96.f;auto v=CurrentSplit();
    if(SidebarEdgeDragging()||chrome.Hit(p.x/d,p.y/d)==NativeChrome::Chrome::SidebarEdgeId){SetCursor(LoadCursor(nullptr,IDC_SIZEWE));return TRUE;}   // #127: the column's edge
    float px=p.x/d,py=p.y/d;const bool overPanel=chrome.floating&&chrome.Local(px,py); // Mode Focus: no separator under the floating panel
    if(v.group&&(sepDrag.active||(!overPanel&&SeparatorAt(p.x/d,p.y/d,v)>=0))){SetCursor(LoadCursor(nullptr,v.a.orientation==SV::Orientation::Row?IDC_SIZEWE:IDC_SIZENS));return TRUE;}}break;
case WM_PAINT:{PAINTSTRUCT paint;BeginPaint(h,&paint);++chromePaints;SyncAddressDisplay();std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto&t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));{std::vector<bool> asleep;for(auto&t:pages)asleep.push_back(t->sleeping);chrome.sleeping=std::move(asleep);}auto t=Active();D2D1_RECT_F dirty{paint.rcPaint.left*96.f/GetDpiForWindow(h),paint.rcPaint.top*96.f/GetDpiForWindow(h),paint.rcPaint.right*96.f/GetDpiForWindow(h),paint.rcPaint.bottom*96.f/GetDpiForWindow(h)};chrome.loading=t?t->loading.Sample(NativeChrome::Motion::Now(),motion.ReducedMotion()):NativeChrome::Loading::Visual{};{auto b=ChromeClient(chrome.LoadingDirty());if(dirty.left<=b.left&&dirty.top<=b.top&&dirty.right>=b.right&&dirty.bottom>=b.bottom)paintedLoading=chrome.LoadingSignature(chrome.loading);}chrome.keyboardModality=keyboardModality;SyncTabKeys();chrome.Paint(titles,current,t&&!t->home?t->uri:L"",editing,hover,GetFocus()==address?'L':GetFocus()==h?keyboardFocus:0,&dirty);EndPaint(h,&paint);if(chrome.TakeTabMotionStarted()){InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}if(editing)RedrawWindow(address,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);return 0;}
case WM_MOUSEMOVE:{if(chrome.floating)FocusEvaluate(true); /* Mode Focus: the pointer on the window's own surface (hit box strip, panel) */if(chrome.sideHidden)SideFocusEvaluate(); /* #127: its left band, its column */
    if(outerDrag.active){OuterDragMove({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;}ChromeTipHover(GET_X_LPARAM(l),GET_Y_LPARAM(l));{const float d=GetDpiForWindow(h)/96.f,x=GET_X_LPARAM(l)/d,y=GET_Y_LPARAM(l)/d;SidebarPointer(x,y);
    if(sepDrag.active){SeparatorMove(x,y);return 0;}
    if(SidebarEdgeDragging()){SidebarEdgeMove(x);return 0;}
    if(!tabDrag.tab){auto v=CurrentSplit();float px=x,py=y;const int hot=v.group&&!(chrome.floating&&chrome.Local(px,py))?SeparatorAt(x,y,v):-1;if(hot!=chrome.split.hotHandle&&!sepDrag.active){chrome.split.hotHandle=hot;SyncGripLooks();InvalidateRect(h,nullptr,FALSE);}}}
    if(tabDrag.tab){DragTab({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(tabDrag.active)return 0;}if(bandDrag.index>=0){BandDragMove({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(bandDrag.active)return 0;}int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));SidebarEdgeLook(id==NativeChrome::Chrome::SidebarEdgeId?1:0);if(NativeChrome::TabIndex(id)<0)UnlockTabWidth();if(id!=hover)UpdateInteraction(id,pressed);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
case WM_COMMAND:if((HWND)l==address&&HIWORD(w)==EN_CHANGE){AddressEdited();return 0;}break;
case WM_MOUSELEAVE:ChromeTipHide();UnlockTabWidth();SidebarPointerLeft();if(!SidebarEdgeDragging())SidebarEdgeLook(0);if(chrome.split.hotHandle>=0&&!sepDrag.active&&!PointerOnGrip()){chrome.split.hotHandle=-1;SyncGripLooks();InvalidateRect(h,nullptr,FALSE);}UpdateInteraction(0,pressed);return 0;
case WM_LBUTTONDOWN:{if(folderRename.Active()&&folderRename.Host()==h)SetFocus(h);ChromeTipHide();
    // Mode Focus: a press on the panel while it moves first lands it at rest (live): every hit test of this press and of
    // its release then reads the same panel under the pointer (code review: a lost click / stray drag otherwise).
    if(chrome.floating&&(focusBar.Moving(FocusMs(NativeChrome::Motion::Now()))||!focusLive)&&chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h)))FocusAnchor(L"press");
    {const float d=GetDpiForWindow(h)/96.f,x=GET_X_LPARAM(l)/d,y=GET_Y_LPARAM(l)/d;if(splitResize>=0){splitResize=-1;SyncSplitDecor();InvalidateRect(h,nullptr,FALSE);}
    if(y>=(chrome.floating?std::max(PageTopDip(),chrome.PanelVisible().bottom):SV::liftY)&&!chrome.Hit(x,y)&&SplitPointerDown(x,y))return 0;if(chrome.Hit(x,y)==NativeChrome::Chrome::SidebarEdgeId&&SidebarEdgeDown(x))return 0;}
    int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));vaultKeyPress=id==NativeChrome::Chrome::VaultKeyId?(VaultKeyArmed()?1:0):-1;downloadDismissClick=id=='D'&&GetTickCount64()-std::max(downloadPanel.hiddenAt,downloadDrawer.hiddenAt)<150;settingsDismissClick=id=='S'&&GetTickCount64()-settingsDrawer.hiddenAt<150;historyDismissClick=id=='Y'&&GetTickCount64()-historyDrawer.hiddenAt<150;audioDismissClick=id=='V'&&GetTickCount64()-audioPanel.hiddenAt<150;if(id){keyboardFocus=0;keyboardModality=false;if(!NativeChrome::IsClose(id)&&id!='Q')SetFocus(h); /* B01 chip: focus stays with its popover (no light dismiss) */if(!splitTest)SetCapture(h);UpdateInteraction(id,id);if(NativeChrome::IsTab(id)){tabDrag.tab=pages[NativeChrome::TabIndex(id)];tabDrag.start={GET_X_LPARAM(l),GET_Y_LPARAM(l)};tabDrag.grab=GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-ChromeOrigin().x-chrome.ShownTabLeft(NativeChrome::TabIndex(id));tabDrag.grabY=GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h)-ChromeOrigin().y-chrome.strip.Surface(NativeChrome::TabIndex(id)).top;tabDrag.origin=NativeChrome::TabIndex(id);
        if(chrome.sidebar&&NativeChrome::TabIndex(id)<(int)chrome.SideColumn().tabs.size()){tabDrag.grabY=GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.SideColumn().tabs[NativeChrome::TabIndex(id)].top;tabDrag.grab=GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.SideColumn().tabs[NativeChrome::TabIndex(id)].left;}}
    if(NativeChrome::IsBookmark(id)){const int i=NativeChrome::BookmarkIndex(id);if(i<(int)chrome.bandItems.size()){const BM::Id bid=chrome.bandItems[i].id;bandDrag={i,bid,{GET_X_LPARAM(l),GET_Y_LPARAM(l)},GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.BandRect(i).left,false,BM::BarRoot,bookmarks.IndexOf(bid)};}}}
    if(chrome.floating&&id)FocusAnchor(L"press"); /* Mode Focus: a press on the floating panel: it is in use, at rest (no slide left under the pointer) */return 0;}
case WM_LBUTTONUP:{const POINT up{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(outerDrag.active){OuterDragEnd(false,&up);return 0;}if(sepDrag.active){SeparatorUp();return 0;}if(SidebarEdgeDragging()){SidebarEdgeUp(true);return 0;}bool dragged=tabDrag.active||bandDrag.active;if(SidebarTearOff(up)){}else if(tabDrag.lifted)CommitSplitDrag();else if(tabDrag.active){if(chrome.sidebar)SidebarDragEnd(true);FinishReorder();}CancelTabDrag();BandDragEnd(false,&up);int original=pressed;int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(GetCapture()==h)ReleaseCapture();UpdateInteraction(id,0);
    if(!dragged&&original&&original==id&&NativeChrome::IsBookmark(id)){BandActivate(NativeChrome::BookmarkIndex(id),(w&MK_CONTROL)!=0,(w&MK_SHIFT)!=0,false);return 0;}
    if(!dragged&&original&&original==id){if(NativeChrome::IsClose(id)){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}else if(NativeChrome::IsTab(id))Select(NativeChrome::TabIndex(id));else Command(id);}vaultKeyPress=-1;return 0;}   /* review R3: a press never outlives its release */
case WM_CAPTURECHANGED:case WM_CANCELMODE:if(outerDrag.active&&(msg==WM_CANCELMODE||(HWND)l!=h))OuterDragEnd(true);CancelSeparator();if(SidebarEdgeDragging()&&(msg==WM_CANCELMODE||(HWND)l!=h))SidebarEdgeUp(false);CancelTabDrag(true);if(bandDrag.index>=0&&(HWND)l!=h)BandDragEnd(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;
case WM_COPYDATA:if(const int answer=ProfileCopyDataMessage(l))return answer;break;
// Files dropped from Explorer on the tab strip or the toolbar (the page area is the engine's own drop): each opens
// in a new tab, the last one shown.
case WM_DROPFILES:{HDROP drop=(HDROP)w;const UINT n=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);int opened=0;
    for(UINT i=0;i<n&&i<32;++i){const UINT len=DragQueryFileW(drop,i,nullptr,0);if(!len)continue;std::wstring path(len+1,L'\0');DragQueryFileW(drop,i,path.data(),len+1);path.resize(len);NewTab(AddressBar::FileUrlFromPath(path));++opened;}
    DragFinish(drop);if(opened){Select((int)pages.size()-1);Log(L"OPEN dropped files: "+std::to_wstring(opened));}return 0;}
case WM_ACTIVATE:if(LOWORD(w)!=WA_INACTIVE){vaultKeyShownAt=GetTickCount64();PublishProfileWindow();settingsDrawer.RefreshDefaultBrowser();}   /* review N6: the key re-arms when the window is activated */
    if(chrome.floating)FocusEvaluate(); /* Mode Focus: the pointer poll runs while the window is in front (or the panel still shows) */if(chrome.sideHidden)SideFocusEvaluate(); /* #127: the same for the column's left band */
    if(LOWORD(w)==WA_INACTIVE){FormsHideAll();CancelTabDrag(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(0,0);}else if(!smoke&&!halted)Defer([]{if(!bandDrag.active)SaveBookmarks();if(sessionReady){SaveHistory();SaveDownloads();if(!WorkspaceParked())historyDrawer.Changed();DownloadsChanged();}}); /* pick up the other windows' changes, a wipe included (S5) */return 0;
case WM_SHOWWINDOW:AnnotationWindowState();break; // V3-B: the annotation band's wave stops in the background (WM_ACTIVATEAPP: below)
case WM_DISPLAYCHANGE:Layout();break;
case WM_SETTINGCHANGE:ApplyReducedMotion();return 0; // Windows' switch, OR'd with the app's own
case WM_MBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));
    if(NativeChrome::IsBookmark(id)){const int i=NativeChrome::BookmarkIndex(id);if(i<(int)chrome.bandItems.size()&&!chrome.bandItems[i].folder)BandActivate(i,true,false,false);return 0;}
    if(NativeChrome::TabIndex(id)>=0){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}return 0;}
case WM_RBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));float x=GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),y=GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h);
    if(!chrome.Local(x,y))return 0;   // Mode Focus: outside the floating panel there is no chrome to open a menu on (y is chrome-local below)
    if(NativeChrome::IsBookmark(id)&&NativeChrome::BookmarkIndex(id)<(int)chrome.bandItems.size()){POINT p{};GetCursorPos(&p);BookmarkContextMenu(chrome.bandItems[NativeChrome::BookmarkIndex(id)].id,p,nullptr,false);return 0;}
    if(!chrome.sidebar&&chrome.bandShown&&y>=NativeChrome::Design::Vertical::toolbarBottom&&y<chrome.ContentTop()){POINT p{};GetCursorPos(&p);BandContextMenu(p);return 0;}
    if(chrome.sidebar&&!id&&chrome.bandShown&&chrome.SideColumn().gridBlock.Contains(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),y)){POINT p{};GetCursorPos(&p);BandContextMenu(p);return 0;}
    if(NativeChrome::TabIndex(id)>=0)TabContextMenu(NativeChrome::TabIndex(id),0);else if(EmptyAreaMenuAt(id,GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),y))EmptyAreaMenu(0);return 0;}   /* #127: the short menu with the 3 views */
// Menu key / Shift+F10 arrive with lParam (-1,-1): on a focused tab or its close button, open the right-click menu
// below it; on the flower or the gear, open their own menu.
case WM_CONTEXTMENU:if(GET_X_LPARAM(l)==-1&&GET_Y_LPARAM(l)==-1&&GetFocus()==h){int tabIndex=NativeChrome::TabIndex(keyboardFocus);if(tabIndex>=0&&tabIndex<(int)pages.size()){keyboardModality=true;TabContextMenu(tabIndex,keyboardFocus);return 0;}if(keyboardFocus==900||keyboardFocus=='S'){keyboardModality=true;Command(keyboardFocus);return 0;}
    if(NativeChrome::IsBookmark(keyboardFocus)&&NativeChrome::BookmarkIndex(keyboardFocus)<(int)chrome.bandItems.size()){keyboardModality=true;const RECT r=RegionScreen(keyboardFocus);BookmarkContextMenu(chrome.bandItems[NativeChrome::BookmarkIndex(keyboardFocus)].id,{r.left,r.bottom},&r,true);if(GetForegroundWindow()==window&&!bookmarkPopover.Visible())SetFocus(window);InvalidateChrome();return 0;}}break;
case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(chrome.sidebar){SidebarWheel(p,GET_WHEEL_DELTA_WPARAM(w));return 0;}float wx=p.x*96.f/GetDpiForWindow(h),wy=p.y*96.f/GetDpiForWindow(h);if(chrome.Local(wx,wy)&&S(wy)<S(44)&&!pages.empty())Command(GET_WHEEL_DELTA_WPARAM(w)>0?'[':']');return 0;}
case WM_KEYDOWN:if(w==VK_ESCAPE&&chrome.sidebarPopover){SidebarPopover(false);return 0;}if(w==VK_ESCAPE&&SideFocusEscape())return 0;if(w==VK_ESCAPE&&chrome.sidebar&&sidebarHover.open&&!tabDrag.tab){sidebarHover.Close();Layout();return 0;} /* #127: Échap closes the column opened over the page */if(w==VK_ESCAPE&&sepDrag.active){CancelSeparator();return 0;}if(splitResize>=0&&SplitResizeKey(w))return 0;if(w==VK_ESCAPE&&tabDrag.tab){CancelTabDrag(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;}
    if(w==VK_ESCAPE&&outerDrag.active){OuterDragEnd(true);return 0;}
    if(w==VK_ESCAPE&&bandDrag.index>=0){BandDragEnd(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;}
    if(GetFocus()==h&&BandKey(w)){SyncAccessibility();return 0;}if(w==VK_TAB||((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus)||((w==VK_LEFT||w==VK_RIGHT||(chrome.sidebar&&(w==VK_UP||w==VK_DOWN)))&&NativeChrome::IsTab(keyboardFocus)))keyboardModality=true;if(w==VK_TAB){auto order=chrome.FocusOrder();if(order.empty())return 0;auto it=std::find(order.begin(),order.end(),keyboardFocus);int index=it==order.end()?-1:(int)(it-order.begin());index=it==order.end()?(GetKeyState(VK_SHIFT)<0?(int)order.size()-1:0):(index+(GetKeyState(VK_SHIFT)<0?-1:1)+(int)order.size())%(int)order.size();keyboardFocus=order[index];InvalidateRect(h,nullptr,FALSE);ChromeTipFocus(keyboardFocus);return 0;}if((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus){if(NativeChrome::IsClose(keyboardFocus))CloseAt(NativeChrome::TabIndex(keyboardFocus));else if(NativeChrome::IsTab(keyboardFocus)){Select(NativeChrome::TabIndex(keyboardFocus));SetFocus(h);}else Command(keyboardFocus);return 0;}
    if(keyboardFocus==NativeChrome::Chrome::SidebarEdgeId&&SidebarEdgeKey(w))return 0;
    if(chrome.sidebar&&(w==VK_UP||w==VK_DOWN)&&NativeChrome::IsTab(keyboardFocus))w=w==VK_UP?VK_LEFT:VK_RIGHT; /* #127: the column's list is vertical */
    if((w==VK_LEFT||w==VK_RIGHT)&&NativeChrome::IsTab(keyboardFocus)&&!pages.empty()){int next=(NativeChrome::TabIndex(keyboardFocus)+(w==VK_LEFT?-1:1)+(int)pages.size())%(int)pages.size();keyboardFocus=NativeChrome::TabBase+next;Select(next);SetFocus(h);ChromeTipFocus(keyboardFocus);return 0;}break;
case WM_CTLCOLOREDIT:{
    if(folderRename.Owns((HWND)l))return folderRename.Color((HDC)w);
    using namespace NativeChrome::Design;
    static NativeUI::AddressBackground background;
    HDC dc=(HDC)w;RECT rect;GetClientRect(address,&rect);
    POINT origin{};MapWindowPoints(address,window,&origin,1);
    const float surface=chrome.EditSurface();   /* #127: the field's own colour in every layout (no lighter box in the column / compact bar) */
    const float alpha=AddressLoading::opacity*chrome.loading.opacity;
    const auto field=ChromeClient(chrome.bar);   // client DIP (Mode Focus: the floating panel's offset), like the EDIT's own place
    const int edge=S(field.left+(field.right-field.left)*chrome.loading.fraction)-origin.x;
    SetBkMode(dc,TRANSPARENT);SetBkColor(dc,NativeGray(surface));
    SetDCBrushColor(dc,NativeGray(surface));SetBrushOrgEx(dc,0,0,nullptr);
    SetTextColor(dc,NativeGray(surface+(1-surface)*Text::address));
    addressSelection.Clip((HWND)l,dc,NativeGray(surface+(1-surface)*alpha));   // after the text/background colours: the field's ordinary palette
    return (LRESULT)background.Brush(rect.right,edge,NativeGray(surface),NativeGray(surface+(1-surface)*alpha));
}
case Shortcut:keyboardModality=true;Command((int)w);return 0;
case AccessibleMessage:AccessibleAction((int)w,(long long)l);return 0;
case DownloadAskMessage:AskPendingDownloads();return 0;
case PageMenuMessage:ShowPageMenu();return 0;
case AudioArtMessage:AudioArtArrived(l);return 0;
case WM_APP+2:SmokeCheck();return 0;
case WM_APP+3:SmokeAdvance();return 0;
case FrameMessage:{if(l==NativeChrome::FrameClock::ClockLost){frameClock.Stop();frameClockReady=false;ScheduleMotion(NativeChrome::Motion::Now());return 0;}
    frameClock.Acknowledge();auto now=NativeChrome::Motion::Now();if(!halted&&!motionMinimized)InvalidateMotion(now);ScheduleMotion(now);return 0;}
case WM_TIMER:
    if(w==ContentTransitionTimerId){KillTimer(h,ContentTransitionTimerId);if(contentTransition.Holding()||contentTransition.Waiting())contentTransition.Cancel();else contentTransition.Finish(contentTransitionGeneration);return 0;}if(w==ContentReadyTimerId){KillTimer(h,ContentReadyTimerId);ContentReadyTick();return 0;}if(w==ProfileUndoTimerId){ProfileUndoTick();return 0;}if(w==ProfileAnimTimerId){ProfileAnimTick();return 0;}if(w==ProfilePublishTimerId){KillTimer(h,ProfilePublishTimerId);PublishDirtyWorkspaces();return 0;}if(w==ProfileCloseWaitTimerId){ProfileCloseWaitTick();return 0;}
#ifdef BROWSER_UPDATES
    if(w==UpdateTimerId){UpdateTimerFired();return 0;}
#endif
    if(w==SleepTimerId){SleepTabs();return 0;}
    if(w==SidebarFoldTimerId){Layout();if(!sidebarFold.Moving(SidebarNowMs())&&!chrome.MorphMoving(NativeChrome::Motion::Now())&&!sideWidthSpring.Moving(NativeChrome::Motion::Now())){KillTimer(h,SidebarFoldTimerId);Layout();}return 0;}
    if(w==SidebarEdgeFadeTimerId){SyncCornerMasks();InvalidateChrome();if(chrome.sidebarEdge!=1||chrome.EdgeAlpha(NativeChrome::Motion::Now())>=1)KillTimer(h,SidebarEdgeFadeTimerId);return 0;}
    if(w==SidebarHoverTimerId){SidebarHoverDue();return 0;}
    if(w==SideFocusPollTimerId){SideFocusEvaluate();return 0;}
    if(w==SideSlideTimerId){SideSlideFrame();return 0;}
    if(w==SideSlideLiveTimerId){KillTimer(h,SideSlideLiveTimerId);if(chrome.sideShown)SidePictureHide();return 0;}
    if(w==FocusPollTimerId){FocusEvaluate();return 0;}
    if(w==FocusSlideTimerId){KillTimer(h,FocusSlideTimerId);if(focusLive)FocusSlideHide();return 0;}   // Mode Focus: the live panel is on screen, its picture goes
    if(w==AudioTimerId){KillTimer(h,AudioTimerId);RefreshAudio();return 0;}
    if(w==AudioSaveTimerId){AudioSaveDue();return 0;}
    if(w==MotionLabTimerId){MotionLabDownloadTick();return 0;}
    if(w==SplitTestTimer){SplitTestStep();return 0;}
    if(w==ExitClearTimerId){Log(L"SETTINGS clear on exit: timed out, closing");FinishExitClear();return 0;}
    if(w==DrawerWarmTimerId){KillTimer(h,DrawerWarmTimerId);if(downloadPanel.Visible())downloadDrawer.Warm();return 0;}
    if(w==MotionTimerId){KillTimer(h,MotionTimerId);motionDeadline.reset();auto now=NativeChrome::Motion::Now();InvalidateMotion(now);ScheduleMotion(now);return 0;}if(w==1&&smoke){KillTimer(h,1);Fail(L"Smoke timeout after 60 seconds",HRESULT_FROM_WIN32(WAIT_TIMEOUT));}return 0;
case EngineInstallMessage:OnEngineInstallMessage();return 0;
case ImportMessage:ImportFinished();return 0;
case ImportIconsMessage:ImportIconsArrived();return 0;
// A window that never became ready (WebView2 recovery) never removes its session: it keeps it for the
// relaunch or the next start. Release also gives up this window's open marker inside the profile lock.
case WM_ACTIVATEAPP:if(!w)contentTransition.Cancel();AnnotationWindowState();break; /* another application came to the foreground; V3-B: the annotation band's wave stops in the background */
case WM_CLOSE:OuterDragEnd(true);BandDragEnd(true);if(!CloseWindowAllowed())return 0;windowClosing=true;popups.CancelAll(); /* pending window.open requests: settled before the profiles close */contentTransition.Cancel();SaveSession();ProfilesClosing();if(!smoke&&!privateWindow&&!windowId.empty()&&WindowSessions::Release(dataDir,windowId,closeWithoutPrompt||!sessionReady,windowLive))Log(L"WINDOW closed among others: session removed");SaveDownloads();if(!bookmarksSaveFailed)SaveBookmarks();if(!ClearOnExitAndClose())DestroyWindow(h);return 0;
case WM_DESTROY:ChatShutdown(); /* the agent's process tree and the panel's WebView go first */AnnotationShutdown(); /* #125: a private window's annotations go with it */contentTransition.Destroy();StopEngineStart();accessibility.Detach();DisposeElementSurfaces();
#ifdef BROWSER_UPDATES
    StopUpdates(); // bounded: cancels transfers; an already launched install helper is a separate process
#endif
    KillTimer(h,MotionTimerId);motionDeadline.reset();frameClock.Stop();frameClockReady=false;popups.CancelAll();for(auto&t:pages){t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();}pages.clear();ScrollbarDestroy();PostQuitMessage(0);return 0;
}return DefWindowProcW(h,msg,w,l);}
// Every message may move keyboard focus, the selection or the tab set: report the
// change to UI Automation clients once the message is handled.
LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    // --split-test drives the window with synthetic messages; the user's own mouse (busy in other
    // applications meanwhile) must neither steer the test nor be captured by it.
    if(splitTest&&!splitSynthetic&&msg>=WM_MOUSEFIRST&&msg<=WM_MOUSELAST)return 0;
    if(msg==WM_GETOBJECT){LRESULT result;if(accessibility.OnGetObject(h,w,l,result))return result;}
    const LRESULT result=ChromeProc(h,msg,w,l);
    if(h==window&&msg!=WM_GETOBJECT&&msg!=WM_DESTROY&&msg!=WM_NCDESTROY)SyncAccessibility();
    return result;
}
// Options for CreateCoreWebView2EnvironmentWithOptions (the SDK's WebView2EnvironmentOptions.h needs
// WRL). `arguments` is AdditionalBrowserArguments; `extensions` (AreBrowserExtensionsEnabled) is on
// outside smoke runs for the ad blocker. The options of a running engine are fixed: a change takes
// effect at the next launch. `target` mirrors the SDK default (CORE_WEBVIEW_TARGET_PRODUCT_VERSION
// of deps/webview2 1.0.4314.35).
class EngineOptions final:public ICoreWebView2EnvironmentOptions,public ICoreWebView2EnvironmentOptions6{
    LONG refs=1;
    static HRESULT Get(const std::wstring& s,LPWSTR* out){if(!out)return E_POINTER;*out=nullptr;if(s.empty())return S_OK;const size_t n=(s.size()+1)*sizeof(wchar_t);*out=static_cast<LPWSTR>(CoTaskMemAlloc(n));if(!*out)return E_OUTOFMEMORY;memcpy(*out,s.c_str(),n);return S_OK;}
    static HRESULT Put(std::wstring& s,LPCWSTR v){s=v?v:L"";return S_OK;}
public:
    std::wstring arguments,language,target=L"156.0.4314.35";BOOL sso=FALSE,extensions=FALSE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==IID_ICoreWebView2EnvironmentOptions)*out=static_cast<ICoreWebView2EnvironmentOptions*>(this);
        else if(id==IID_ICoreWebView2EnvironmentOptions6)*out=static_cast<ICoreWebView2EnvironmentOptions6*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* v) override{return Get(arguments,v);}
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR v) override{return Put(arguments,v);}
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* v) override{return Get(language,v);}
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR v) override{return Put(language,v);}
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* v) override{return Get(target,v);}
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR v) override{return Put(target,v);}
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* v) override{if(!v)return E_POINTER;*v=sso;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL v) override{sso=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_AreBrowserExtensionsEnabled(BOOL* v) override{if(!v)return E_POINTER;*v=extensions;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AreBrowserExtensionsEnabled(BOOL v) override{extensions=v;return S_OK;}
};
// ---- WebView2 startup and recovery (src/webview2-runtime.h, design/webview2-startup-recovery.md) ----
// Loader → probe (same options as the environment) → CreateCoreWebView2EnvironmentWithOptions. A
// startup failure opens the native recovery dialog instead of Fail(): Installer WebView2 (runtime
// missing only), Réessayer, Quitter. The session is restored once, after an environment exists.
namespace WR=WebView2Runtime; namespace WI=WebView2Install;
constexpr int EngineInstallButton=10,EngineRetryButton=11,EngineRelaunchButton=12,EngineQuitButton=13,EngineManualButton=14,EngineManualLink=201;
std::unique_ptr<WR::Controller> engineStart; WI::Installer engineInstaller;
bool engineRelaunched=false,engineDialogOpen=false,engineRelaunchPending=false; std::wstring launchArgs;
#ifdef BROWSER_WEBVIEW2_TEST_HOOKS
// Compiled into tests/webview2-runtime/host_test only: fake download/trust/launch and an isolated
// per-user claim. The browser has no such entry point; it always runs WI::ProductionEffects().
std::function<WI::Effects()> engineInstallEffects; std::function<WI::Coordination()> engineCoordination;
WI::Effects EngineEffects(){return engineInstallEffects?engineInstallEffects():WI::ProductionEffects();}
WI::Coordination EngineCoordination(){return engineCoordination?engineCoordination():WI::DefaultCoordination();}
#else
WI::Effects EngineEffects(){return WI::ProductionEffects();}
WI::Coordination EngineCoordination(){return WI::DefaultCoordination();}
#endif
using CreateEnvironmentFn=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
using ProbeRuntimeFn=HRESULT(STDAPICALLTYPE*)(PCWSTR,ICoreWebView2EnvironmentOptions*,LPWSTR*);
CreateEnvironmentFn engineCreate=nullptr; ProbeRuntimeFn engineProbe=nullptr;
std::wstring engineGivenArgs;
// The engine arguments of one environment, from the settings in the globals (its profile's): interface language, the
// autoplay policy, grayscale text, third-party cookies; any user-set arguments are kept.
std::wstring EngineArgumentsFor(const BrowserSettings::State& s){
    std::wstring value=I18n::WithEngineLang(engineGivenArgs);   // the interface language (fr / en-US), as EngineOptions::put_Language
    // SETTINGS(media): autoplay policy, fixed for the engine's lifetime. Muted autoplay stays allowed under user-gesture-required.
    if(value.find(L"--autoplay-policy=")==std::wstring::npos)value+=(value.empty()?L"":L" ")+std::wstring(s.autoplay==BrowserSettings::Autoplay::Allowed?L"--autoplay-policy=no-user-gesture-required":L"--autoplay-policy=user-gesture-required");
    // SETTINGS(display): grayscale text antialiasing for page content (D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE).
    if(s.grayscaleText&&value.find(L"--disable-lcd-text")==std::wstring::npos)value+=(value.empty()?L"":L" ")+std::wstring(L"--disable-lcd-text");
    // SETTINGS(cookies): third-party cookies blocked for the engine's lifetime (see WithEnabledFeatures).
    if(s.blockThirdPartyCookies)value=WithEnabledFeatures(value,{L"ForceThirdPartyCookieBlockingEnabled",L"ThirdPartyStoragePartitioning"});
    return value;
}
// One user data folder = one set of options (WebView2 refuses a second environment on it with others): while another
// process holds that profile's engine, its recorded arguments are used (Profiles::AcquireEngineArgs lease).
std::wstring EngineArgumentsLeased(){const auto wanted=EngineArgumentsFor(settings);if(!profilesEnabled||smoke||privateWindow)return wanted;const auto lease=Profiles::AcquireEngineArgs(profileRoot,profileId,wanted);EngineHeld(profileId);if(!lease.error.empty())Log(L"PROFILES engine args "+lease.error);if(lease.shared&&lease.arguments.find(I18n::WithEngineLang(L""))==std::wstring::npos)Log(L"UI LANGUAGE engine shared with an open window of this profile: "+lease.arguments+L" (applies when every window of the profile is closed)");return lease.arguments;}
EngineOptions* NewEngineOptions(){auto options=new EngineOptions();options->language=I18n::Tag();options->extensions=!smoke;options->arguments=EngineArgumentsLeased();return options;}
HRESULT PrepareLoader(WR::Stage& stage){
    if(!loader){loader=LoadLibraryExW((base+L"\\WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader){stage=WR::Stage::Loader;return HRESULT_FROM_WIN32(GetLastError());}}
    if(!WI::Resolve(loader,"CreateCoreWebView2EnvironmentWithOptions",engineCreate)){stage=WR::Stage::Export;return HRESULT_FROM_WIN32(GetLastError());}
    WI::Resolve(loader,"GetAvailableCoreWebView2BrowserVersionStringWithOptions",engineProbe); // absent from very old loaders: no probe
    return S_OK;
}
WR::Probe ProbeRuntime(){
    WR::Probe probe;if(!engineProbe)return probe;probe.attempted=true;LPWSTR version=nullptr;auto options=new EngineOptions();options->language=I18n::Tag();options->extensions=!smoke;options->arguments=EngineArgumentsFor(settings); /* a version probe creates no environment: no engine lease (it would never be released) */
    probe.hr=engineProbe(nullptr,options,&version);options->Release();if(version){probe.version=version;CoTaskMemFree(version);}return probe;
}
HRESULT CreateEngine(std::uint64_t generation){
    // Each attempt takes an engine lease hold (NewEngineOptions); a failed attempt gives it back, a superseded one
    // (an environment nobody uses) gives it back once that environment's browser process has exited.
    const std::wstring root=profileRoot,id=profileId;
    auto done=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([generation,root,id](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{
        if(SUCCEEDED(hr)&&!env)hr=E_POINTER;
        const bool used=engineStart&&SUCCEEDED(hr)&&engineStart->Accepts(generation);
        if(FAILED(hr))EngineDropped(root,id,L"start attempt failed");else if(!used)WatchEngineExit(env,root,id);
        if(!engineStart)return S_OK; // window gone: nothing to restore into
        if(used){if(environment.p&&environment.p!=env)WatchEngineExit(environment.p,root,id); /* a retry replaces an earlier environment: its hold follows its exit */environment.set(env);}
        engineStart->Created(generation,hr);return S_OK;});
    auto options=NewEngineOptions();HRESULT hr=engineCreate(nullptr,profileDir.c_str(),options,done);options->Release();done->Release();
    if(FAILED(hr))EngineDropped(root,id,L"start attempt not started");
    return hr;
}
void EngineReady(){
    LPWSTR version=nullptr;if(Check(environment->get_BrowserVersionString(&version),L"get_BrowserVersionString")&&version){Log(L"RUNTIME "+std::wstring(version));CoTaskMemFree(version);}
    RestoreSession();if(debugSandboxAtStart&&!smoke)Defer([]{OpenSandbox();});
}
// The new instance starts only after this one has closed: WM_CLOSE (saves, clear on exit — in
// recovery there is no engine profile, so ClearLocalTraces runs synchronously) then WM_DESTROY,
// then RunPendingRelaunch() after the message loop. It never reads history/downloads before the
// old instance has erased them.
// Arguments of the recovery relaunch: the original ones (so --private stays), any previous
// --restore-window replaced by this normal window's own session id (adopted again by the new instance).
std::wstring RelaunchArgs(std::wstring args,bool isPrivate,const std::wstring& id){
    if(const auto at=args.find(L"--restore-window");at!=std::wstring::npos){size_t e=at+16;while(e<args.size()&&args[e]==L' ')++e;while(e<args.size()&&args[e]!=L' ')++e;args.erase(at,e-at);}
    while(!args.empty()&&args.back()==L' ')args.pop_back();
    if(!isPrivate&&!id.empty())args+=(args.empty()?L"":L" ")+std::wstring(L"--restore-window ")+id;
    return args;
}
bool StartRelaunchedInstance(){
    std::wstring exe(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());if(!n||n>=exe.size())return false;exe.resize(n);
    // Same mode (--private is in launchArgs) and, for a normal window, the SAME session: its open marker
    // was given up at close (WindowSessions::Release), so the new instance adopts exactly this id.
    std::wstring command=L"\""+exe+L"\" "+RelaunchArgs(ProfileArgs::ForRelaunch(launchArgs),privateWindow,windowId)+(launchArgs.find(L"--profile ")==std::wstring::npos?ProfileArgument():L"")+L" --webview2-recovered";STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,base.c_str(),&si,&pi)){Log(L"WEBVIEW2 RECOVERY relaunch failed "+std::to_wstring(GetLastError()));return false;}
    AllowSetForegroundWindow(pi.dwProcessId);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Log(L"WEBVIEW2 RECOVERY relaunched after close");return true;
}
bool RelaunchBrowser(){engineRelaunchPending=true;return true;}
void RunPendingRelaunch(){
    if(!engineRelaunchPending)return;engineRelaunchPending=false;
    if(!StartRelaunchedInstance())MessageBoxW(nullptr,TR(relaunch_failed),L"Browser",MB_OK|MB_ICONINFORMATION);
}
void ShowEngineView(const WR::View& v);
void OnEngineButton(int id){
    if(!engineStart)return;
    if(id==EngineInstallButton)engineStart->Install();
    else if(id==EngineRetryButton)engineStart->Attempt();
    else if(id==EngineManualButton){if((INT_PTR)ShellExecuteW(window,L"open",WI::ManualPageUrl(),nullptr,nullptr,SW_SHOWNORMAL)<=32)Log(L"WEBVIEW2 RECOVERY manual page open failed");}
    else if(id==EngineRelaunchButton){engineStart->Relaunch();if(engineStart->phase()==WR::Phase::Closed){closeWithoutPrompt=true;PostMessageW(window,WM_CLOSE,0,0);}}
}
void EngineDialogDone(const MD::Result& r){
    if(!engineDialogOpen)return;engineDialogOpen=false;
    if(!engineStart||engineStart->phase()==WR::Phase::Closed||engineStart->phase()==WR::Phase::Recovered)return;
    if(r.button==EngineQuitButton){Log(L"WEBVIEW2 RECOVERY quit");PostMessageW(window,WM_CLOSE,0,0);return;}
    if(r.button==EngineManualLink&&(INT_PTR)ShellExecuteW(window,L"open",WI::ManualPageUrl(),nullptr,nullptr,SW_SHOWNORMAL)<=32)Log(L"WEBVIEW2 RECOVERY manual page open failed");
    // Échap or the Microsoft page link: the browser cannot work without its engine, the dialog comes back.
    Defer([]{if(engineStart&&!engineDialogOpen)ShowEngineView(engineStart->Current());});
}
MD::Spec EngineSpec(const WR::View& v){
    MD::Spec s;s.kind=MD::Kind::Generic;s.scope=MD::Scope::Window;s.automationId=L"webview2.startup";
    s.title=v.diagnosis.title;s.body=v.diagnosis.body;if(!v.note.empty())s.body+=L"\n\n"+v.note;s.footnote=v.diagnosis.technical;
    const bool busy=WR::Busy(v.phase);
    if(busy){s.status.icon=MD::StatusIcon::Busy;s.status.title=v.status;s.status.detail=v.statusDetail;s.hasStatus=true;}
    const auto cause=v.diagnosis.cause;
    if(!v.manualPrimary&&(cause==WR::Cause::RuntimeMissing||cause==WR::Cause::RuntimeInconsistent||cause==WR::Cause::OverrideInvalid))s.links.push_back({EngineManualLink,TR(engine_link_microsoft_page)});
    auto button=[&](int id,const wchar_t* label,MD::Role role,bool enabled,bool closes){MD::Button b;b.id=id;b.label=label;b.role=role;b.disabled=!enabled;b.closes=closes;b.automationId=L"webview2.button."+std::to_wstring(id);s.buttons.push_back(b);};
    button(EngineQuitButton,TR(engine_quit),MD::Role::Secondary,true,true);
    button(EngineRetryButton,TR(downloads_action_retry),MD::Role::Secondary,!busy&&v.canRetry,false);
    if(v.manualPrimary)button(EngineManualButton,TR(engine_open_microsoft_page),MD::Role::Primary,true,false);
    else if(v.canRelaunch)button(EngineRelaunchButton,TR(engine_relaunch),MD::Role::Primary,!busy,false);
    else if(v.diagnosis.offerInstall||busy)button(EngineInstallButton,busy?TR(updates_installing):TR(engine_install_webview2),MD::Role::Primary,!busy&&v.canInstall,false);
    for(auto& b:s.buttons)if(!b.disabled&&b.role==MD::Role::Primary){b.isDefault=true;b.initialFocus=true;}
    s.onButton=[](int id){OnEngineButton(id);};
    return s;
}
void ShowEngineView(const WR::View& v){
    if(v.phase==WR::Phase::Recovered||v.phase==WR::Phase::Closed){if(engineDialogOpen){engineDialogOpen=false;modal.Finish(-1);}return;}
    if(v.phase==WR::Phase::Creating||v.phase==WR::Phase::Idle)return;
    if(smoke){Fail(L"WebView2 startup ("+std::wstring(WR::CauseName(v.diagnosis.cause))+L")",v.diagnosis.hr);return;}
    auto spec=EngineSpec(v);
    if(engineDialogOpen&&modal.Visible()){modal.Update(std::move(spec));return;}
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);
    engineDialogOpen=modal.Show(std::move(spec),PageScreenRect(),[](const MD::Result& r){EngineDialogDone(r);});
    if(!engineDialogOpen){ // native dialog unavailable (graphics init failed): plain message, then the official page
        Log(L"WEBVIEW2 RECOVERY dialog unavailable");
        const auto text=v.diagnosis.body+L"\n\n"+v.diagnosis.technical+(v.diagnosis.offerInstall?TRF(engine_open_page_question_block,TR(engine_open_page_question)):std::wstring());
        if(MessageBoxW(window,text.c_str(),v.diagnosis.title.c_str(),(v.diagnosis.offerInstall?MB_YESNO:MB_OK)|MB_ICONERROR)==IDYES)ShellExecuteW(window,L"open",WI::ManualPageUrl(),nullptr,nullptr,SW_SHOWNORMAL);
        PostMessageW(window,WM_CLOSE,0,0);
    }
}
void OnEngineInstallMessage(){
    for(auto& e:engineInstaller.Take()){if(!engineStart)return;if(e.done)engineStart->InstallFinished(e.report);else engineStart->Progress(e.phase,e.bytes,e.total);}
}
void StartEngine(){
    WR::Hooks hooks;hooks.prepare=PrepareLoader;hooks.probe=ProbeRuntime;hooks.create=CreateEngine;
    hooks.overrides=[]{std::wstring exe(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);return WR::ReadOverridesFor(exe.substr(exe.find_last_of(L'\\')+1),taskbarIdentity.aumid);};
    hooks.startInstall=[]{return engineInstaller.Start(EngineEffects(),[]{PostMessageW(window,EngineInstallMessage,0,0);},[](const WR::InstallReport& r){
        // Worker thread: the real outcome, logged even when the window is already gone.
        Log(std::wstring(L"WEBVIEW2 INSTALL WORKER outcome=")+WR::OutcomeName(r.outcome)+L" code="+WR::Hex(static_cast<HRESULT>(r.code))+(r.pid?L" pid="+std::to_wstring(r.pid):L"")+
            (r.installerRunning?L" microsoft-installer-still-running":L"")+(r.residue.empty()?L"":L" residue=%TEMP%\\"+r.residue));});};
    hooks.installRunning=[]{WI::Settled settled;const DWORD pid=WI::ActiveInstall(EngineCoordination(),&settled);
        // A browser that died while launching left its setup: it was resumed here (1 = still suspended since its creation, so not started under our invariant; 0 = it was already running).
        if(settled.adopted)Log(L"WEBVIEW2 INSTALL adopted the setup of a closed browser pid="+std::to_wstring(pid)+L" previous-suspend="+std::to_wstring(settled.previousSuspend));
        return pid;};
    hooks.cancelInstall=[]{engineInstaller.Cancel();};hooks.show=ShowEngineView;hooks.ready=EngineReady;hooks.relaunch=RelaunchBrowser;
    hooks.log=[](const std::wstring& line){Log(line);};
    engineStart=std::make_unique<WR::Controller>(std::move(hooks),engineRelaunched);
    engineStart->Attempt();
}
void StopEngineStart(){
    if(engineStart)engineStart->Shutdown();
    const auto state=engineInstaller.Shutdown(1500);
    if(state.stage==WI::Stage::Idle||(state.stage==WI::Stage::Done&&!state.detached))return; // nothing was in flight
    // What really happened to the request at close (the gate decides atomically with the resume).
    // i18n-exempt-begin: log line assembled below (English diagnostics)
    std::wstring line=std::wstring(L"WEBVIEW2 INSTALL close during ")+WI::StageName(state.stage)+L": ";
    if(state.alreadyResumed)line+=L"the Microsoft installer had already started and keeps running (never killed)";
    else{
        line+=state.terminatedSuspended?L"the created setup was terminated before it ever ran":L"no installer was started";
        line+=state.withdrawalRecorded?L"; withdrawal recorded (no later check will resume it)":L"; withdrawal NOT recorded (no record of ours or lock/write failure)";
    }
    if(state.detached)line+=L"; worker still busy"+(state.dir.empty()?std::wstring():L", its folder %TEMP%\\"+state.dir+L" may remain if the process ends first");   // i18n-exempt-end
    Log(line);
}
// ---- tab / profile switch transition (src/content-transition.h) ----------------------------------------------
void QaTransitionFrames();
// The WebView host window of a shown tab: resolved once, only when unambiguous (exactly one visible Chrome_WidgetWin
// child of the browser window at the tab's bounds and not claimed by another tab), then kept for the tab's life.
BOOL CALLBACK CollectWebViewHost(HWND h,LPARAM l){wchar_t c[64]{};if(GetParent(h)==window&&IsWindowVisible(h)&&GetClassNameW(h,c,64)&&std::wcsncmp(c,L"Chrome_WidgetWin",16)==0)reinterpret_cast<std::vector<HWND>*>(l)->push_back(h);return TRUE;}
HWND PageHostWindow(Tab& t){
    if(t.host&&IsWindow(t.host)&&GetParent(t.host)==window)return t.host;t.host=nullptr;
    std::vector<HWND> all;EnumChildWindows(window,CollectWebViewHost,reinterpret_cast<LPARAM>(&all));HWND found=nullptr;int n=0;
    for(HWND h:all){bool claimed=false;for(auto& o:pages)if(o&&o.get()!=&t&&o->host==h){claimed=true;break;}if(claimed)continue;
        RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);if(EqualRect(&r,&t.placed)){found=h;++n;}}
    if(n==1)t.host=found;return t.host;}
// The host windows of the pages shown NOW (the capture allowlist; split panes included).
std::vector<HWND> ShownPageWindows(){std::vector<HWND> v;for(auto& t:pages)if(t&&!t->closed&&t->controller.p&&t->placedVisible==1&&PageShown(*t))if(HWND h=PageHostWindow(*t))v.push_back(h);return v;}
// ---- Mode Focus (src/focus-mode.h, design/focus-mode-plan.md, Paper « Mode Focus » p-U-0) -------------------------
// Off: the normal chrome. On (Réglages › Affichage, Ctrl+Maj+F; per profile, default off): the page fills the window
// minus 4 DIP with 6 DIP corners, and the chrome is a floating panel that slides down over the page's top when the
// pointer reaches the window's top edge (a 14 DIP hit box), stays while anything holds it and slides back up 300 ms
// after. Fullscreen (F11, a page's element) suspends it. The annotation mode does not (owner's rule 2026-11-24): its
// banner stays at the top and the chat (mode Panneau) on the right as a card, both visible and usable; the panel floats
// under the banner, over the page's width only (src/focus-mode.h « With the annotation mode », Paper 203J-0 / 20MM-0).
// The panel is the normal chrome, painted translated (Chrome::DrawPanel). The main window cannot draw over a page (each
// page is a windowed WebView2 of another process, and the window clips its children), so while the panel is on screen
// every page window under it gets a window region without the panel's pixels: the main window shows there and receives
// the pointer there (WindowFromPoint honours regions). Updated on every frame of the slide.
double FocusMs(NativeChrome::Motion::Time t){return std::chrono::duration<double,std::milli>(t.time_since_epoch()).count();}
float focusPlaced=-1;bool focusPolling=false,focusCovering=false,focusCoveringLive=false;RECT focusPainted{};
struct FocusHole{HWND host=nullptr;std::vector<FM::Pixels> spans;};std::map<unsigned,FocusHole> focusHoles;   // by tab serial
bool FocusWanted(){return settings.focusMode&&!halted&&!InFullscreen()&&!chrome.sidebar;}   /* the side layouts hide their column instead (#127, SideFocusWanted) */
// The panel's surroundings (Layout, before the chrome is measured): the annotation banner's height above it and the chat
// card's strip on its right; nothing outside focus mode.
void FocusAbove(){chrome.SetPanelAbove(chrome.floating?FM::Above{std::max(0.f,chrome.annotationBand),FM::ChatReserve(ChatInsetDip())}:FM::Above{});}
// The chat card at rest (client DIP, src/chat-host-ui.h PanelRect): 4 DIP from the window's right and bottom edges, under
// the banner; R25: while the banner slides it keeps its height and only moves, like the page.
D2D1_RECT_F FocusChatCard(float widthDip){auto c=FM::ChatCard(chrome.ClientWidth(),chrome.height,widthDip,chrome.annotationBand);if(AnnotationBandSliding())c.bottom+=c.top-FM::pageInset;return {c.left,c.top,c.right,c.bottom};}
// The sliding picture passes under the annotation banner (it comes out from behind it): the banner stays on top.
void FocusBannerOnTop(){if(chrome.panelAbove.banner<=0)return;if(HWND b=AnnotationBannerWindow())if(IsWindowVisible(b))SetWindowPos(b,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
// Chrome-local -> client: what is anchored to the chrome (popovers, menus, tooltips, the address field, UI Automation)
// is anchored to the floating panel at rest; a surface held open keeps the panel there (FocusHold).
D2D1_POINT_2F ChromeOrigin(){return chrome.floating?chrome.PanelAt(1).origin:D2D1_POINT_2F{0,0};}
D2D1_RECT_F ChromeClient(D2D1_RECT_F r){const auto o=ChromeOrigin();return {r.left+o.x,r.top+o.y,r.right+o.x,r.bottom+o.y};}
RECT ChromePx(D2D1_RECT_F r){const auto c=ChromeClient(r);return {S(c.left),S(c.top),S(c.right),S(c.bottom)};}
// Where the page starts (81 / 121 DIP under the normal chrome, 4 in focus mode) and what lies under the toolbar (the
// normal chrome's lower edge, or the floating panel's bottom at rest: 88 / 128).
float PageTopDip(){return chrome.floating?FM::PageTop(chrome.annotationBand):chrome.ContentTop();}
float ChromeBottomDip(){return chrome.floating?FM::RestBottom(chrome.bandShown,chrome.panelAbove.banner):chrome.ContentTop();}
// The panel's part inside the window, in client pixels (empty while it is off screen or focus mode is off): the live
// panel at rest, or its sliding picture.
RECT FocusPanelPixels(){if(!chrome.floating||!chrome.PanelShown())return {};const auto v=chrome.PanelVisible();return {S(v.left),S(v.top),S(v.right),S(v.bottom)};}
bool FocusPanelOver(const RECT& px){const RECT p=FocusPanelPixels();RECT i{};return !IsRectEmpty(&p)&&IntersectRect(&i,&p,&px);}
// Only the live panel (the page windows' hole): the page's top corner masks step away for it; under the sliding picture
// they stay (the picture is above them and its transparent corners show their canvas, as the live panel does).
bool focusLive=false;
bool FocusHoleOver(const RECT& px){return focusLive&&FocusPanelOver(px);}
// The panel's current rectangle and the one painted before it (that one goes back to canvas or page).
void InvalidateFocusPanel(){if(!window)return;const RECT r=FocusPanelPixels();if(!IsRectEmpty(&r))InvalidateRect(window,&r,FALSE);if(!IsRectEmpty(&focusPainted))InvalidateRect(window,&focusPainted,FALSE);focusPainted=r;}
// Every shown page window under the panel loses the panel's pixels (bottom corners on their 4 DIP arc); every other
// page window has its whole surface. `force`: set again even when unchanged (after a resize Chromium may reset it).
void FocusHoles(bool force){
    if(!window)return;std::vector<FM::Pixels> spans;
    if(chrome.floating&&!halted&&focusLive&&chrome.PanelShown()){const float s=GetDpiForWindow(window)/96.f;const auto p=chrome.panel;
        spans=FM::HoleSpans({(int)std::lround(p.left*s),(int)std::lround(p.top*s),(int)std::lround(p.right*s),(int)std::lround(p.bottom*s)},FM::panelRadius*s);}
    for(auto& t:pages){if(!t)continue;auto it=focusHoles.find(t->serial);std::vector<FM::Pixels> mine;HWND host=nullptr;
        if(!spans.empty()&&!t->closed&&t->controller.p&&t->placedVisible==1&&t->shown&&!IsRectEmpty(&t->placed)){
            mine=FM::ClipTo(spans,{t->placed.left,t->placed.top,t->placed.right,t->placed.bottom});if(!mine.empty())host=PageHostWindow(*t);}
        if(it!=focusHoles.end()&&(it->second.host!=host||mine.empty())){if(IsWindow(it->second.host))SetWindowRgn(it->second.host,nullptr,TRUE);focusHoles.erase(it);it=focusHoles.end();}
        if(mine.empty()||!host)continue;
        if(it!=focusHoles.end()&&it->second.spans==mine&&!force){
            // The engine may drop a page window's region on its own (resize, focus, activation): put it back at once.
            HRGN probe=CreateRectRgn(0,0,0,0);const int kind=GetWindowRgn(host,probe);DeleteObject(probe);
            if(kind!=ERROR&&kind!=NULLREGION)continue;
            static int restored=0;if(++restored<=5)Log(L"FOCUS page window region reset by the engine: set again");}
        HRGN rgn=CreateRectRgn(0,0,t->placed.right-t->placed.left,t->placed.bottom-t->placed.top);
        for(auto& s:mine){HRGN cut=CreateRectRgn(s.left,s.top,s.right,s.bottom);CombineRgn(rgn,rgn,cut,RGN_DIFF);DeleteObject(cut);}
        if(SetWindowRgn(host,rgn,TRUE))focusHoles[t->serial]={host,std::move(mine)};   // the system owns the region now
        else{DeleteObject(rgn);static bool logged=false;if(!logged){logged=true;Log(L"FOCUS page window region refused "+std::to_wstring(GetLastError()));}}
    }
    // Tabs no longer in this window's list (closed, or another profile's now on screen): their window gets its surface back.
    for(auto it=focusHoles.begin();it!=focusHoles.end();){if(!TabBySerial(it->first)){if(IsWindow(it->second.host))SetWindowRgn(it->second.host,nullptr,FALSE);it=focusHoles.erase(it);continue;}++it;}
}
// ---- The slide: a picture of the panel above the page (fidelity audit D1) ----
// A page window's new region reaches the screen a frame after the window's own paint: a hole moved on every frame
// lagged the painted panel (slide-in: the page cut the toolbar's bottom; slide-out: a stale toolbar strip under it).
// While the panel slides, it is a picture of the panel at rest (Chrome::RenderPanel, premultiplied, its rounded
// corners transparent) in a per-pixel-alpha layered child window above the page windows, moved with SetWindowPos:
// position and content reach the screen together. The page windows keep their whole surface meanwhile. At rest the
// panel is live again (the hole, the window's own paint) and the picture stays 3 frames over it, until the hole is on
// screen. A slide-out starts with the picture at rest and the hole taken away, and moves after a short lead
// (FocusMode::Bar::hideLeadMs).
struct FocusSlideWindow{HWND hwnd=nullptr;bool shown=false,broken=false;int w=0,h=0;} focusSlide;
constexpr double FocusHideLeadMs=50;
bool FocusRenderPanel(std::vector<std::uint32_t>& px,UINT& w,UINT& h){
    SyncAddressDisplay();std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;
    for(auto& t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));
    {std::vector<bool> asleep;for(auto& t:pages)asleep.push_back(t->sleeping);chrome.sleeping=std::move(asleep);}
    auto t=Active();chrome.loading=t?t->loading.Sample(NativeChrome::Motion::Now(),motion.ReducedMotion()):NativeChrome::Loading::Visual{};
    chrome.keyboardModality=keyboardModality;SyncTabKeys();
    return chrome.RenderPanel(titles,current,editing,hover,GetFocus()==address?'L':GetFocus()==window?keyboardFocus:0,NativeChrome::Motion::Now(),px,w,h);
}
void FocusReveal();
LRESULT CALLBACK FocusSlideProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_NCHITTEST:return HTCLIENT;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_SETCURSOR:SetCursor(LoadCursor(nullptr,IDC_ARROW));return TRUE;
    // A press on the sliding panel: at rest at once (live), then the same press on the live panel under the pointer.
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:case WM_LBUTTONDBLCLK:{FocusAnchor(L"press on the sliding panel"); /* i18n-exempt: log tag */POINT p{};GetCursorPos(&p);ScreenToClient(window,&p);return SendMessageW(window,msg,w,MAKELPARAM(p.x,p.y));}
    case WM_MOUSEMOVE:case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};MapWindowPoints(h,window,&p,1);return SendMessageW(window,msg,w,MAKELPARAM(p.x,p.y));}
    }
    return DefWindowProcW(h,msg,w,l);
}
// The picture's place: the panel's current rectangle in client pixels (above the window's top while it slides).
void FocusSlideMove(){
    if(!focusSlide.shown||!focusSlide.hwnd)return;const float s=GetDpiForWindow(window)/96.f;
    SetWindowPos(focusSlide.hwnd,HWND_TOP,(int)std::lround(chrome.panel.left*s),(int)std::lround(chrome.panel.top*s),0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    FocusBannerOnTop();
}
// A fresh picture of the panel at rest, shown at the panel's current place. False when it cannot be made (no
// Direct2D 1.1, no layered child): the panel then slides as before, live in a moving hole.
bool FocusSlideShow(){
    if(focusSlide.broken||!window)return false;
    std::vector<std::uint32_t> px;UINT w=0,h=0;if(!FocusRenderPanel(px,w,h)){focusSlide.broken=true;Log(L"FOCUS panel picture unavailable: the panel slides live");return false;}
    HINSTANCE inst=instance?instance:GetModuleHandleW(nullptr);
    if(!focusSlide.hwnd){static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=FocusSlideProc;wc.hInstance=inst;wc.lpszClassName=L"BrowserFocusSlide";wc.style=CS_DBLCLKS;RegisterClassW(&wc);registered=true;}
        focusSlide.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,L"BrowserFocusSlide",L"",WS_CHILD,0,0,1,1,window,nullptr,inst,nullptr);
        if(!focusSlide.hwnd){focusSlide.broken=true;Log(L"FOCUS panel picture window unavailable: the panel slides live");return false;}}
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=(LONG)w;bi.bmiHeader.biHeight=-(LONG)h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);void* bits=nullptr;HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);bool ok=false;
    if(bmp&&bits){memcpy(bits,px.data(),px.size()*4);HGDIOBJ old=SelectObject(mem,bmp);POINT src{0,0};SIZE size{(LONG)w,(LONG)h};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        // Child layered window: content and size here, its place by SetWindowPos (client pixels).
        ok=UpdateLayeredWindow(focusSlide.hwnd,screen,nullptr,&size,mem,&src,0,&blend,ULW_ALPHA)!=FALSE;SelectObject(mem,old);}
    if(bmp)DeleteObject(bmp);
    DeleteDC(mem);ReleaseDC(nullptr,screen);
    if(!ok){focusSlide.broken=true;Log(L"FOCUS panel picture refused: the panel slides live");return false;}
    focusSlide.w=(int)w;focusSlide.h=(int)h;focusSlide.shown=true;const float s=GetDpiForWindow(window)/96.f;
    SetWindowPos(focusSlide.hwnd,HWND_TOP,(int)std::lround(chrome.panel.left*s),(int)std::lround(chrome.panel.top*s),0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    FocusBannerOnTop();
    return true;
}
void FocusSlideHide(){if(window)KillTimer(window,FocusSlideTimerId);if(focusSlide.hwnd&&focusSlide.shown)ShowWindow(focusSlide.hwnd,SW_HIDE);focusSlide.shown=false;}
// ---- The panel's bottom corners over the page (fidelity audit D2) ----
// The hole takes only the pixels the panel covers completely; the partly covered ones along the two bottom arcs stay
// the page's, and a per-pixel-alpha layered child window per corner lays the panel's colour (its border along the arc,
// its fill inside) back over them at its coverage: antialiased over any site. Shown only while the panel is live.
struct FocusCornerWindows{HWND hwnd[2]{};int n=0;UINT dpi=0;bool shown=false;} focusCorners;
LRESULT CALLBACK FocusCornerProc(HWND h,UINT msg,WPARAM w,LPARAM l){if(msg==WM_NCHITTEST)return HTTRANSPARENT;if(msg==WM_ERASEBKGND)return 1;if(msg==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,msg,w,l);}
void FocusCornersHide(){if(!focusCorners.shown)return;for(HWND c:focusCorners.hwnd)if(c)ShowWindow(c,SW_HIDE);focusCorners.shown=false;}
void FocusCornersSync(){
    if(!window||!chrome.floating||!focusLive||!chrome.PanelShown()||halted){FocusCornersHide();return;}
    const UINT dpi=GetDpiForWindow(window);const float s=dpi/96.f,r=FM::panelRadius*s;const int n=FM::CornerSize(r);if(n<=0){FocusCornersHide();return;}
    HINSTANCE inst=instance?instance:GetModuleHandleW(nullptr);
    for(int i=0;i<2;++i)if(!focusCorners.hwnd[i]){static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=FocusCornerProc;wc.hInstance=inst;wc.lpszClassName=L"BrowserFocusCorner";RegisterClassW(&wc);registered=true;}
        focusCorners.hwnd[i]=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"BrowserFocusCorner",L"",WS_CHILD,0,0,1,1,window,nullptr,inst,nullptr);if(!focusCorners.hwnd[i])return;}
    if(focusCorners.n!=n||focusCorners.dpi!=dpi){
        const float c=NativeChrome::Design::Surface::canvas;const auto px=FM::BottomCorner(r,FM::border*s,c,c+(1-c)*FM::borderAlpha);
        for(int i=0;i<2;++i){   // 0: bottom-left, 1: bottom-right (mirrored); bitmap rows top-down (row 0 of the mask is the bottom one)
            BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=n;bi.bmiHeader.biHeight=-n;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
            HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);void* bits=nullptr;HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
            if(bmp&&bits){auto* out=static_cast<std::uint32_t*>(bits);
                for(int y=0;y<n;++y)for(int x=0;x<n;++x){const int j=n-1-y,ii=i==0?x:n-1-x;const auto& p=px[(size_t)j*n+ii];
                    const bool full=FM::FullyInside(ii,j,r);const float a=full?0.f:p.alpha;   // the hole's own pixels are the window's paint
                    const auto A=(std::uint32_t)std::lround(a*255),C=(std::uint32_t)std::lround(p.grey*a*255);out[y*n+x]=(A<<24)|(C<<16)|(C<<8)|C;}
                HGDIOBJ old=SelectObject(mem,bmp);POINT src{0,0};SIZE size{n,n};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
                UpdateLayeredWindow(focusCorners.hwnd[i],screen,nullptr,&size,mem,&src,0,&blend,ULW_ALPHA);SelectObject(mem,old);}
            if(bmp)DeleteObject(bmp);
            DeleteDC(mem);ReleaseDC(nullptr,screen);}
        focusCorners.n=n;focusCorners.dpi=dpi;}
    const auto& p=chrome.panel;const int left=(int)std::lround(p.left*s),right=(int)std::lround(p.right*s),bottom=(int)std::lround(p.bottom*s);
    SetWindowPos(focusCorners.hwnd[0],HWND_TOP,left,bottom-n,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    SetWindowPos(focusCorners.hwnd[1],HWND_TOP,right-n,bottom-n,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    focusCorners.shown=true;
}
// The panel at slide position `pos`. At rest (shown, not moving): live — the hole, the window's own paint, the corner
// windows. Anywhere else: the picture at that place, the page windows whole.
void FocusApply(float pos){
    if(!window||!chrome.floating)return;
    const double ms=FocusMs(NativeChrome::Motion::Now());
    chrome.PlacePanel(pos);focusPlaced=pos;
    const bool rest=focusBar.Shown()&&pos>=1&&!focusBar.Moving(ms);
    if(rest||focusSlide.broken){
        if(!focusLive){   // the picture (at rest) bridges the frame the page's new region takes to reach the screen
            if(rest&&!focusSlide.shown)FocusSlideShow();else FocusSlideMove();
            focusLive=true;chrome.panelLive=true;FocusHoles(true);FocusCornersSync();FocusSlideMove();InvalidateFocusPanel();if(!IsIconic(window))UpdateWindow(window);
            {static int traced=0;if(++traced<=40)Log(L"FOCUS live (holes "+std::to_wstring(focusHoles.size())+L", picture "+std::to_wstring(focusSlide.shown)+L", panel "+std::to_wstring((int)std::lround(chrome.panel.left))+L","+std::to_wstring((int)std::lround(chrome.panel.top))+L","+std::to_wstring((int)std::lround(chrome.panel.right))+L","+std::to_wstring((int)std::lround(chrome.panel.bottom))+L")");}   // DIP (tests/focus/annotation_focus_journey.py)
            if(focusSlide.shown)SetTimer(window,FocusSlideTimerId,60,nullptr);
        }else{FocusHoles(false);FocusCornersSync();InvalidateFocusPanel();if(!IsIconic(window)&&focusSlide.broken)UpdateWindow(window);}
    }else{
        if(focusLive){   // leaving rest: the picture first (fresh, at rest), then the page windows get their whole surface back
            KillTimer(window,FocusSlideTimerId);
            const bool picture=pos>0&&FocusSlideShow();if(picture&&!IsIconic(window))DwmFlush();
            focusLive=false;chrome.panelLive=false;FocusHoles(false);FocusCornersHide();InvalidateFocusPanel();
            {static int traced=0;if(++traced<=40)Log(L"FOCUS sliding (picture "+std::to_wstring(picture)+L")");}
        }else if((!focusSlide.shown&&pos>0)||(focusBar.Shown()&&pos<=0))FocusSlideShow();   // a reveal starts: a fresh picture of the panel as it is now
        else FocusSlideMove();
        if(pos<=0&&!focusBar.Shown()&&!focusBar.Moving(ms))FocusSlideHide();   // off screen: nothing left
    }
    const RECT p=FocusPanelPixels();const bool covering=!IsRectEmpty(&p)&&p.bottom>S(PageTopDip());
    if(covering!=focusCovering||focusLive!=focusCoveringLive){focusCovering=covering;focusCoveringLive=focusLive;
        SyncGrips(CurrentSplit());SyncCornerMasks();SyncStatusBubble();ScrollbarRefresh(NativeChrome::Motion::Now());   // overlays step away / come back at once
        if(focusLive)FocusCornersSync();FocusSlideMove();}   // the corners and the picture (topmost) stay above the re-raised overlays
    if(findBar.Visible())findBar.Reposition(FindBarArea());   // the find bar keeps clear of the panel
}
// The frame clock's tick while the panel slides (InvalidateMotion).
void FocusFrame(NativeChrome::Motion::Time now){
    if(!chrome.floating)return;const double ms=FocusMs(now);const float pos=(float)focusBar.Position(ms);
    // The ease-out may land on 1 (or 0) a frame before its end: the frame after the end still settles the panel (live at
    // rest; the picture gone once off screen) although the position no longer changes.
    const bool settle=focusBar.Shown()&&!focusBar.Moving(ms)&&pos>=1&&!focusLive,gone=!focusBar.Shown()&&!focusBar.Moving(ms)&&pos<=0&&focusSlide.shown;
    if(pos!=focusPlaced||settle||gone)FocusApply(pos);
}
NativeChrome::Motion::Wake FocusWake(NativeChrome::Motion::Time now){return chrome.floating&&focusBar.Moving(FocusMs(now))?NativeChrome::Motion::Frame():NativeChrome::Motion::Wake{};}
// Shows the panel at once and keeps it: the chrome is in use from the keyboard, or one of its surfaces is open (the
// panel is where it is anchored). Never animated: keyboard-initiated (emil-design-eng), and the address field, a
// popover or a drawer is placed where the panel stays.
bool FocusHold(){
    if(editing||(address&&GetFocus()==address))return true;     // Ctrl+L, Alt+D, Ctrl+T, a click in the field
    if(GetFocus()==window&&keyboardFocus)return true;             // F6 / Tab in the chrome
    if(chrome.latched||chrome.bandOpen)return true;               // a toolbar button's surface or a band menu is open
    if(bookmarkPopover.Visible()||bookmarksDrawer.Visible()||audioPanel.Visible())return true;
    if(folderRename.Active()&&folderRenameState.surface==RenameBand)return true;   // a band folder renamed in place
    if(RequestChipOpen()||originPrompt.Visible())return true;     // anchored to the address field
    return false;
}
// Keeps a shown panel without bringing a hidden one: a menu, the suggestions, a drag, the mouse captured.
bool FocusKeep(){
    if(GetCapture()==window||tabDrag.tab||bandDrag.index>=0||outerDrag.active||sepDrag.active)return true;
    if(contextMenu.Open()||profileMenu.Open()||suggestions.Visible())return true;
    return false;
}
// One evaluation: the pointer (polled every ~30 ms while the window is in front or the panel shows: over a page the
// window gets no mouse message), the window and what holds the chrome. `pointerMoved`: a WM_MOUSEMOVE of the window.
void FocusEvaluate(bool pointerMoved){
    (void)pointerMoved;if(!window)return;
    if(!chrome.floating){if(focusPolling){KillTimer(window,FocusPollTimerId);focusPolling=false;}return;}
    const auto now=NativeChrome::Motion::Now();const double ms=FocusMs(now);
    POINT sp{};const bool pointer=GetCursorPos(&sp)!=FALSE;POINT cp=sp;ScreenToClient(window,&cp);
    const float k=96.f/GetDpiForWindow(window),x=cp.x*k,y=cp.y*k;
    // In front: the window, or one of its own popups / drawers, is the foreground window (no WindowFromPoint every 30 ms:
    // it would hit-test the pages' windows across processes). The pointer counts only over the window then.
    HWND fg=GetForegroundWindow();const bool front=fg&&(fg==window||GetAncestor(fg,GA_ROOTOWNER)==window)&&IsWindowVisible(window)&&!IsIconic(window);
    const bool buttons=GetAsyncKeyState(VK_LBUTTON)<0||GetAsyncKeyState(VK_RBUTTON)<0||GetAsyncKeyState(VK_MBUTTON)<0;
    RECT wr{};GetWindowRect(window,&wr);const bool ours=pointer&&front&&PtInRect(&wr,sp);
    const bool inHit=pointer&&FM::InHitBox(x,y,chrome.ClientWidth(),chrome.panelAbove);   // under the annotation banner when it is there
    const auto v=chrome.PanelVisible();const bool overPanel=pointer&&chrome.PanelShown()&&x>=v.left&&x<v.right&&y>=v.top&&y<v.bottom;
    FM::Input in;in.reveal=inHit&&front&&ours&&!buttons;in.pointer=(inHit||overPanel)&&ours;in.hold=FocusHold();in.keep=FocusKeep();
    const bool was=focusBar.Shown();
    if(focusBar.Update(ms,in,motion.ReducedMotion())){FocusApply((float)focusBar.Position(ms));ScheduleMotion(now);
        if(focusBar.Shown()!=was)Log(std::wstring(L"FOCUS bar ")+(focusBar.Shown()?L"shown":L"hidden")+L" (front "+std::to_wstring(front)+L", pointer "+std::to_wstring(in.pointer)+L", hold "+std::to_wstring(in.hold)+L", keep "+std::to_wstring(in.keep)+L")");}
    else if(focusBar.Visible(ms))FocusHoles(false);   // still on screen: the hole is checked on every tick
    const bool poll=front||focusBar.Shown()||focusBar.Visible(ms);
    if(poll&&!focusPolling){focusPolling=SetTimer(window,FocusPollTimerId,(UINT)FM::pollMs,nullptr)!=0;}
    else if(!poll&&focusPolling){KillTimer(window,FocusPollTimerId);focusPolling=false;}
}
// The keyboard (or a press on the panel) needs the chrome now: at rest at once.
void FocusReveal(){
    if(!chrome.floating||!window)return;const double ms=FocusMs(NativeChrome::Motion::Now());
    const bool was=focusBar.Shown();FM::Input in;in.hold=true;focusBar.Update(ms,in,motion.ReducedMotion());if(focusPlaced!=1||!focusLive)FocusApply(1);
    if(!was){static int traced=0;if(++traced<=40)Log(std::wstring(L"FOCUS bar shown at once (")+focusRevealWhy+L")");}
}
// A surface anchored to the chrome opens (a menu at a region, the profile menu, a popover, a drawer, the permission
// prompt): the panel at rest at once, so its anchor is on screen (code review, keyboard-opened surfaces).
void FocusAnchor(const wchar_t* why){if(!chrome.floating)return;focusRevealWhy=why;FocusReveal();focusRevealWhy=L"keyboard";}
// On / off follows the setting, fullscreen and the annotation mode (Layout, before the chrome is measured). Returns
// true when it changed. The bar starts hidden (the next evaluation reveals it if the pointer already asks for it).
bool SyncFocusMode(){
    const bool want=FocusWanted();if(want==chrome.floating)return false;
    chrome.SetFloating(want);focusBar.Reset(false,FocusMs(NativeChrome::Motion::Now()));focusBar.hideLeadMs=FocusHideLeadMs;focusPlaced=want?0.f:-1.f;focusCovering=focusCoveringLive=false;
    focusLive=false;chrome.panelLive=!want;FocusSlideHide();FocusCornersHide();
    if(!want){FocusHoles(false);InvalidateFocusPanel();focusPainted={};if(focusPolling&&window){KillTimer(window,FocusPollTimerId);focusPolling=false;}}
    Log(want?L"FOCUS mode on":L"FOCUS mode off");return true;
}
// After the chrome is measured (Layout): the panel at its slide position for the new size, the drawers' top under it.
void FocusPlace(){
    // Drawers (user request 2026-11-23): 4 DIP under the panel, their right edge on the panel's (W - 4), their two top
    // corners round (6 DIP, antialiased: src/drawer-corners.h). Normal mode: 81 DIP, flush right, square top.
    // With the annotation mode (Paper « Annotation + Focus · barre du haut · tiroir ouvert »): under the panel under the
    // banner (124), at W - 4 over the chat card (FocusMode::drawerOverChat; the chat is back when the drawer closes).
    namespace DL=NativeChrome::Downloads::DrawerLayout;
    const float top=chrome.floating?FM::DrawerTop(chrome.bandShown,chrome.panelAbove.banner):NativeChrome::Design::Vertical::DrawerTop() /* #127: 40 / 32 in the panel layouts */,right=chrome.floating?FM::pageInset+(FM::drawerOverChat?0.f:chrome.panelAbove.reserve):0.f,radius=chrome.floating?FM::pageRadius:0.f,bottom=chrome.floating?FM::drawerBottom:0.f;
    if(top!=DL::liveTop||right!=DL::liveRight||radius!=DL::liveRadius||bottom!=DL::liveBottom){DL::liveTop=top;DL::liveRight=right;DL::liveRadius=radius;DL::liveBottom=bottom;
        downloadDrawer.Reposition();settingsDrawer.Reposition();historyDrawer.Reposition();bookmarksDrawer.Reposition();audioPanel.OwnerChanged();}
    if(!chrome.floating)return;
    chrome.PlacePanel(focusPlaced<0?0.f:focusPlaced);InvalidateFocusPanel();FocusCornersSync();FocusSlideMove();
    if(!focusPolling)FocusEvaluate();
}
// The surfaces over the window that close on their own Échap (the top-most layer first): the docked drawers (Réglages,
// Historique, Favoris, Téléchargements) and the toolbar popovers (Téléchargements, Son). While one of them has the
// keyboard, the message loop's Key() leaves Échap to it (before: the annotation mode left first, src/main.cpp Key).
bool EscapeOwnedBySurface(HWND focus){
    if(!focus)return false;const HWND root=GetAncestor(focus,GA_ROOT);
    for(HWND s:{settingsDrawer.Window(),historyDrawer.Window(),bookmarksDrawer.Window(),downloadDrawer.Window(),downloadPanel.Window(),audioPanel.Window()})
        if(s&&(focus==s||root==s))return true;
    return false;
}
bool EscapeLeavesAnnotation(bool modeActive,HWND focus){if(focus==window&&chrome.sidebar&&((chrome.sideHidden&&sideReveal.open)||chrome.sidebarPopover||sidebarHover.open))return false;   /* #127: the column / popover over the page first (top-most layer first) */return modeActive&&focus!=address&&!EscapeOwnedBySurface(focus);}
void ToggleFocusMode(){
    settings.focusMode=!settings.focusMode;SettingChanged(BrowserSettings::Change::FocusMode);
    if(settingsDrawer.Visible())settingsDrawer.Changed();   // the switch follows the shortcut
}
// The first input to a page (a click: WM_PARENTNOTIFY, sent before the page handles it; a key in the page) removes
// the overlay at once: what the user acts on is what they see. Nothing is consumed or delayed.
// Removes the overlay now, releases both snapshots and the pending readiness / end timers (no idle wake left).
void CancelContentTransition(const wchar_t* why){if(!contentTransition.Active())return;contentTransition.Cancel();if(window){KillTimer(window,ContentReadyTimerId);KillTimer(window,ContentTransitionTimerId);}if(workspaceTest)Log(std::wstring(L"TRANSITION cancelled by ")+why);}
void ContentTransitionInput(const wchar_t* what){CancelContentTransition((std::wstring(L"input: ")+what).c_str());}
// Mode Focus: the overlay (composed above every child window) stays under the floating panel while it is on screen.
RECT ContentTransitionRect(){const auto c=ContentBox();RECT r{S(c.left),S(c.top),S(c.right),S(c.bottom)};if(chrome.floating){const RECT p=FocusPanelPixels();if(!IsRectEmpty(&p))r.top=std::min(r.bottom,std::max(r.top,p.bottom));}return r;}
// Called just BEFORE the outgoing page is hidden: the snapshot is taken now, the switch then happens at once.
void BeginContentTransition(ContentTransition::Kind kind){
    if(!window||halted||smoke||splitTest||windowClosing||WorkspaceParked()||IsIconic(window)||!IsWindowVisible(window))return;
    const bool reduced=ReducedMotionPreferred();const float scale=GetDpiForWindow(window)/96.f;
    const bool was=contentTransition.Active();
    if(!contentTransition.Begin(window,ContentTransitionRect(),kind,reduced,scale,ShownPageWindows())){if(!contentTransition.Available())return;Log(L"TRANSITION unavailable: instant switch");return;}
    contentTransitionGeneration=contentTransition.Generation();
    SetTimer(window,ContentTransitionTimerId,2600,nullptr);   // safety: cleared even if the switch never reveals
    if(workspaceTest){Log(std::wstring(L"TRANSITION ")+(kind==ContentTransition::Kind::Profile?L"profile":L"tab")+(was?L" retarget":L" capture "+std::to_wstring(static_cast<int>(contentTransition.CaptureMs()))+L" ms")+(reduced?L" reduced (opacity only)":contentTransition.Blurring()?L" gpu blur":L" blur unavailable (fade only)"));QaTransitionFrames();}
}
// --workspace-test only: the DWM-composed viewport (screen pixels of this window's viewport) at 0 / 50 / 100 / 150 /
// 220 ms of the next transition, written as BMP to the folder armed by « qa transition-frames <dir> ».
std::wstring qaFramesDir; int qaFrameIndex=0; constexpr UINT_PTR QaFrameTimerId=0x5052; constexpr int QaFrames=22;
// --workspace-test only: our own window rendered by PrintWindow (PW_RENDERFULLCONTENT: its DirectComposition content
// included), cropped to the viewport, every 30 ms of a transition; written as BMP to the armed QA folder. Never the
// desktop or another application.
void QaSaveFrame(){
    if(qaFramesDir.empty()||!window)return;RECT r=ContentTransitionRect();const int w=r.right-r.left,h=r.bottom-r.top;RECT c{};GetClientRect(window,&c);if(w<=0||h<=0||c.right<=0||c.bottom<=0)return;
    BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),c.right,-c.bottom,1,32,BI_RGB};void* bits=nullptr;HDC dc=GetDC(window);HDC mem=CreateCompatibleDC(dc);HBITMAP dib=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);ReleaseDC(window,dc);
    if(!dib||!bits){if(dib)DeleteObject(dib);DeleteDC(mem);return;}HGDIOBJ old=SelectObject(mem,dib);PrintWindow(window,mem,PW_CLIENTONLY|0x2);SelectObject(mem,old);
    std::vector<BYTE> px((size_t)w*h*4);for(int y=0;y<h;++y)memcpy(&px[(size_t)y*w*4],(BYTE*)bits+((size_t)(r.top+y)*c.right+r.left)*4,(size_t)w*4);DeleteObject(dib);DeleteDC(mem);
    BITMAPINFOHEADER hi{sizeof(hi),w,-h,1,32,BI_RGB};BITMAPFILEHEADER fh{0x4D42,(DWORD)(sizeof(fh)+sizeof(hi)+px.size()),0,0,sizeof(fh)+sizeof(hi)};
    const std::wstring path=qaFramesDir+L"\\frame-"+std::to_wstring(qaFrameIndex)+L".bmp";std::ofstream f(std::filesystem::path(path),std::ios::binary);f.write((const char*)&fh,sizeof(fh));f.write((const char*)&hi,sizeof(hi));f.write((const char*)px.data(),px.size());
    const auto v=contentTransition.Now();const int ph=static_cast<int>(contentTransition.CurrentPhase());
    Log(L"WSQA frame "+std::to_wstring(qaFrameIndex)+L" phase="+std::to_wstring(ph)+L" oldBlur="+std::to_wstring(v.oldBlur)+L" oldOpacity="+std::to_wstring(v.oldOpacity)+L" newBlur="+std::to_wstring(v.newBlur)+L" overlay="+std::to_wstring(v.outer));
}
void CALLBACK QaFrameTick(HWND,UINT,UINT_PTR,DWORD){QaSaveFrame();++qaFrameIndex;if(qaFrameIndex>=QaFrames){KillTimer(window,QaFrameTimerId);qaFramesDir.clear();}}
void QaTransitionFrames(){if(qaFramesDir.empty()||qaFrameIndex!=0)return;QaSaveFrame();qaFrameIndex=1;SetTimer(window,QaFrameTimerId,30,QaFrameTick);}
// The switch that began the transition returned: the fade starts (the timer then releases the snapshot).
// The switch that began the transition returned: the incoming page is being placed. Its readiness is polled
// asynchronously (generation-checked): a cached page is ready after one frame; a cold profile waits for its first
// navigation, bounded (never the old profile's capture held over the new one). Ready -> blur-in; else a fade.
unsigned contentReadyGeneration=0; ULONGLONG contentReadyDeadline=0;
void RevealContentTransition(){if(!contentTransition.Holding()||!window)return;contentReadyGeneration=contentTransition.Placed();
    contentReadyDeadline=GetTickCount64()+ContentTransition::WaitCapMs;SetTimer(window,ContentReadyTimerId,16,nullptr);}   // opaque hold capped: then a fade, cold profiles included
void ContentReadyTick(){
    if(!contentTransition.Waiting()||contentTransition.Generation()!=contentReadyGeneration||WorkspaceParked())return;   // stale: a newer switch owns the overlay
    auto t=Active();const bool timeout=GetTickCount64()>=contentReadyDeadline;
    const bool ready=t&&(!PageShown(*t)||(t->controller.p&&t->revealed&&t->placedVisible==1));
    if(!ready&&!timeout){const ULONGLONG left=contentReadyDeadline-GetTickCount64();SetTimer(window,ContentReadyTimerId,static_cast<UINT>(std::min<ULONGLONG>(33,std::max<ULONGLONG>(1,left))),nullptr);return;}   // the last poll lands on the cap
    const bool incoming=ready&&PageShown(*t)&&contentTransition.Incoming(window,contentReadyGeneration,ShownPageWindows());
    if(!incoming)contentTransition.FadeOut(contentReadyGeneration);
    if(workspaceTest)Log(std::wstring(L"TRANSITION ")+(incoming?L"incoming capture "+std::to_wstring(static_cast<int>(contentTransition.IncomingCaptureMs()))+L" ms"+(contentTransition.IncomingBlurring()?L" gpu blur-in":L""):timeout?L"incoming timeout: fade":L"incoming none: fade")+L" after "+std::to_wstring(static_cast<long long>(GetTickCount64()-(contentReadyDeadline-ContentTransition::WaitCapMs)))+L" ms surfaces="+std::to_wstring(contentTransition.Surfaces()));
    contentTransitionGeneration=contentTransition.Generation();
    SetTimer(window,ContentTransitionTimerId,static_cast<UINT>(contentTransition.RemainingMs())+40,nullptr);
}
void CancelContentTransitionIfMoved(){if(contentTransition.Active())contentTransition.Reanchor(ContentTransitionRect());}   // bookmarks bar, chrome layout: re-anchored (offset + clip)
#include "browser-import-integration.h"
#include "workspace-failover.h"
#include "profile-workspace.h"
#include "onboarding-integration.h"
// The window carries the identity and relaunches through the stable launcher; our Start Menu and desktop
// shortcuts to that launcher get the same identity once (setups before 0.3.379 wrote them without it).
// Setups before the « Browser » name wrote « browser.lnk »: ours is renamed in place first (same file, so whatever
// was pinned from it stays; TaskbarIdentity::RenameShortcut). The taskbar's own pinned copy is Windows' and is left.
void StampTaskbarIdentity(){
    if(!taskbarIdentity.active()||!window)return;
    if(!TaskbarIdentity::ApplyToWindow(window,taskbarIdentity))Log(L"TASKBAR identity not applied to the window");
    for(const KNOWNFOLDERID* folder:{&FOLDERID_Programs,&FOLDERID_Desktop}){PWSTR dir=nullptr;if(FAILED(SHGetKnownFolderPath(*folder,0,nullptr,&dir))||!dir){CoTaskMemFree(dir);continue;}
        const std::wstring where(dir),lnk=TaskbarIdentity::LauncherShortcut(dir);CoTaskMemFree(dir);
        if(TaskbarIdentity::RenameShortcut(where,taskbarIdentity.launcher)==TaskbarIdentity::Rename::Renamed)Log(L"TASKBAR shortcut renamed « Browser »: "+lnk);
        const auto r=TaskbarIdentity::StampShortcut(lnk,taskbarIdentity);
        if(r==TaskbarIdentity::Stamp::Stamped)Log(L"TASKBAR shortcut given the identity: "+lnk);else if(r==TaskbarIdentity::Stamp::Failed)Log(L"TASKBAR shortcut identity could not be written: "+lnk);}
}
// A link or a file for a profile that already has a window on screen: that window opens it in a new tab and comes
// forward, and this process ends before any window of its own. None (or it refuses): this launch opens its own
// window with it (RestoreSession). Never for a private window, which always opens its own.
bool HandOverLaunchTarget(){
    if(launchTarget.empty()||smoke||privateWindow||!profilesEnabled)return false;
    const auto live=Profiles::ActiveIn(profileRoot,profileId);if(live.empty())return false;
    HWND target=nullptr;   // the one nearest the top of the z-order: the window the person used last
    for(HWND h=GetTopWindow(nullptr);h&&!target;h=GetWindow(h,GW_HWNDNEXT))for(const auto& w:live)if(w.hwnd==h){target=h;break;}
    if(!target)target=live.front().hwnd;
    DWORD pid=0;GetWindowThreadProcessId(target,&pid);AllowSetForegroundWindow(pid);
    const std::wstring text=L"open " /* i18n-exempt: window protocol */+launchTarget;COPYDATASTRUCT cd{ProfileCopyData,(DWORD)((text.size()+1)*sizeof(wchar_t)),(PVOID)text.c_str()};DWORD_PTR answer=0;
    if(!SendMessageTimeoutW(target,WM_COPYDATA,0,(LPARAM)&cd,SMTO_ABORTIFHUNG,5000,&answer)||answer!=1){Log(L"OPEN hand-over refused: own window");return false;}
    Log(L"OPEN launch target handed to the running window");return true;
}
// Receiver side (ProfileCopyDataMessage « open <target> »): a new tab, this window brought forward.
void OpenHandedTarget(const std::wstring& target){
    const std::wstring url=LaunchUrl(target);if(url.empty())return;
    NewTab(url);Select((int)pages.size()-1);Log(L"OPEN handed target in a new tab");
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);
}
// browser.exe [switches] [<url>|<path>] or [switches] --single-argument <url|path>: the target apart from the switches.
std::wstring AbsoluteIfFile(const std::wstring& target){
    const std::wstring t=AddressBar::Unquoted(target);
    if(t.empty()||t.find(L':')!=std::wstring::npos||AddressBar::LooksLikeWindowsPath(t))return target;   // a link, a drive or share path: as given
    wchar_t full[32768];const DWORD n=GetFullPathNameW(t.c_str(),32768,full,nullptr);if(!n||n>=32768)return target;
    const DWORD a=GetFileAttributesW(full);return a!=INVALID_FILE_ATTRIBUTES?std::wstring(full):target;   // « page.html » in the current folder
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR args,int show){
    // The switches are read in `args` only: a link such as https://site/?--private never turns into a switch.
    static std::wstring switchArgs;{int n=0;LPWSTR* v=CommandLineToArgvW(GetCommandLineW(),&n);if(v){const auto parsed=LaunchArgs::Parse(std::vector<std::wstring>(v+1,v+n));LocalFree(v);
        if(!parsed.target.empty()||parsed.single){switchArgs=parsed.switches;args=switchArgs.data();launchTarget=AbsoluteIfFile(parsed.target);}}}
    {int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
#ifndef BROWSER_CHANNEL_RELEASE
    // SEC-12: test switches exist in dev builds only and are read before --single-argument only (LaunchArgs::Has).
    const std::vector<std::wstring> tokens=argv&&argc>1?std::vector<std::wstring>(argv+1,argv+argc):std::vector<std::wstring>{};
    importRootsOverride=LaunchArgs::ValueOf(tokens,L"--import-sources");importHold=LaunchArgs::ValueOf(tokens,L"--import-hold"); /* tests: import from synthetic roots only */
    qaDrawerStays=LaunchArgs::Has(tokens,L"--qa-drawer-stays");   // QA: drawer kept while the app is not in the foreground
    qaDefaultCard=LaunchArgs::Has(tokens,L"--qa-default-card");   // QA: the default-browser card, for review
#endif
    if(argv)LocalFree(argv);}smoke=wcsstr(args,L"--smoke-test")!=nullptr;splitTest=wcsstr(args,L"--split-test")!=nullptr;workspaceTest=wcsstr(args,L"--workspace-test")!=nullptr;debugSandboxAtStart=wcsstr(args,L"--debug-sandbox")!=nullptr;privateWindow=wcsstr(args,L"--private")!=nullptr;Vault::WritesForbidden()=privateWindow;
#ifdef BROWSER_CHANNEL_RELEASE
    smoke=splitTest=workspaceTest=debugSandboxAtStart=false;   // SEC-12: the test harnesses are never honoured by a release build
#endif
#if defined(BROWSER_E2E_VAULT_HOST)||defined(BROWSER_E2E_VAULT_ACTIVE)
    Vault::ActiveFlag()=true;   /* TEST HOSTS (tests/e2e/vault_host.cpp, tests/e2e/vault_product_host.cpp): already the default since activation; kept explicit */
#endif
    if(const wchar_t* r=wcsstr(args,L"--restore-window ")){r+=17;while(*r==L' ')++r;const wchar_t* e=r;while(*e&&*e!=L' ')++e;windowRequested.assign(r,e);}smoke=smoke||splitTest;engineRelaunched=wcsstr(args,L"--webview2-recovered")!=nullptr;launchArgs=args?args:L"";motionLabMode=!smoke&&wcsstr(args,L"--motion-lab")!=nullptr;forcedLayout=smoke?-1:TabLayoutArg(args);   /* --tab-layout=top|side|compact, or the older --side-panel (no line comment here: the rest of this line is code) */ /* #127: the top bar is the default (Val, 24 nov.); --side-panel shows the panel for this run without saving (trial launcher); the smoke suites assert the toolbar */instance=hi;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 1;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);PathRemoveFileSpecW(path);base=path;ResolveBrowserPaths();
    // Interface language, pinned for this process BEFORE any dialog, window or WebView2 (src/ui-language.h): the
    // browser-wide preference at the data root (portable: next to the exe; installed: <root>\data). French by default.
    const UiLanguage::Loaded uiLanguage=UiLanguage::Load(dataDir);I18n::SetForProcess(uiLanguage.locale);
#ifdef BROWSER_UPDATES
#ifndef BROWSER_PREVIEW
    if(!smoke&&ForwardToInstalled()){CoUninitialize();return 0;} // before any window, profile or WebView2
#endif
#endif
    const Onboarding::Reason onboardingReason=OnboardingReason(args); // before the registry exists: first run or --onboarding (src/onboarding-integration.h)
    SelectProfile(args); // after the forward: a forwarded launch never touches this folder's profiles (review M5)
    // A link or a file while this profile already has a window: that window opens it (onboarding, if any, belongs
    // to the first window: a launch with a link shows it first, then opens the link in the first tab).
    if(HandOverLaunchTarget()){CoUninitialize();return 0;}
    Log(L"START browser");{const char* st=UiLanguage::StatusText(uiLanguage.status);Log(std::wstring(L"UI LANGUAGE ")+(uiLanguage.locale==I18n::Locale::En?L"en":L"fr")+L" ("+std::wstring(st,st+std::strlen(st))+L")");}Log(std::wstring(L"VAULT state: ")+(Vault::Active()?L"active":L"inactive"));   /* P-I1: what a product run reports */
#ifdef BROWSER_E2E_VAULT_HOST
    Log(L"E2E VAULT HOST: vault active, foreground condition lifted (test host only)");   /* binary marker, absent from dev/release (tests/e2e/product_markers.ps1) */
#endif
#ifdef BROWSER_VAULT_FORCE_INACTIVE
    Log(L"E2E VAULT FORCED INACTIVE: engine-control test host, the engine keeps its own password autosave (test build only)");   /* binary marker, absent from dev/release (tests/e2e/product_markers.ps1) */
#endif
#ifdef BROWSER_E2E_VAULT_ACTIVE
    Log(L"E2E VAULT ACTIVE: product path, vault forced active, every foreground condition in force (test build only)");   /* review R-5 host marker, absent from dev/release (tests/e2e/product_markers.ps1) */
#endif
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);RunOnboarding(onboardingReason);/* first-run window: returns when the person is done, then the normal start */paper=CreateSolidBrush(BG);WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"LisiereBrowser";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);{const UINT dpi=GetDpiForSystem();/* channel app icon (src/app-icon.rc), picked from its multi-size ICO at the system DPI for the title bar/Alt+Tab and the taskbar */wc.hIcon=(HICON)LoadImageW(hi,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXICON,dpi),GetSystemMetricsForDpi(SM_CYICON,dpi),LR_DEFAULTCOLOR);wc.hIconSm=(HICON)LoadImageW(hi,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetricsForDpi(SM_CXSMICON,dpi),GetSystemMetricsForDpi(SM_CYSMICON,dpi),LR_DEFAULTCOLOR);}wc.hbrBackground=paper;RegisterClassExW(&wc);window=CreateWindowW(wc.lpszClassName,privateWindow?TR(window_title_private_bare):L"Browser",WS_POPUP|WS_CAPTION/* Windows' minimize / restore / maximize animations (no caption is drawn: WM_NCCALCSIZE gives the whole window to the client) */|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,hi,nullptr);StampTaskbarIdentity();DragAcceptFiles(window,TRUE);NativeUI::ConfigureFullClientFrame(window);NativeUI::RoundNatively(window);if(const HRESULT gfx=chrome.Init(window,base);FAILED(gfx)){wchar_t code[16];swprintf(code,16,L"0x%08lX",(unsigned long)gfx);Log(std::wstring(L"STARTUP graphics failed at ")+chrome.initStep+L": hr="+code+L" (assets "+base+L"\\assets)");MessageBoxW(window,TR(startup_graphics_failed),L"Browser",MB_OK|MB_ICONERROR);return 3;}   /* the step and the HRESULT in the log (startup_graphics_failed) */splitOverlay.Init(hi,window,chrome.Factory(),chrome.WriteFactory(),chrome.FontCollection());statusBubble.Init(hi,window,chrome.Factory(),chrome.WriteFactory(),chrome.FontCollection());LoadSettings();ClaimWindowSession();LoadAudioVolumes();motion.SetReducedMotion(ReducedMotionPreferred());NativeChrome::Design::Accent::Set(settings.accent);ProfilesStarted(args);AnnotationStartup(); /* #125: notes of a private window that did not close normally */LoadDownloads();LoadHistory();LoadBookmarks();CreateDownloadPanel();CreateAudioPanel();CreateSuggestions();CreateSettingsPanel();CreateHistoryPanel();CreateBookmarkSurfaces();CreateElementSurfaces();contextMenu.Init(instance,window,base);profileMenu.Init(instance,window,base);profileDialog.Init(instance,window,base);profileDeleteDialog.Init(instance,window,base);frameClockReady=frameClock.Start(window,FrameMessage);motion.EnablePolish();chrome.animateSelection=true;chrome.SetMotion(&motion);UpdateFont();Layout();OnboardingShowBrowser(show);/* plain ShowWindow + UpdateWindow, or the scale-in after a completed onboarding */OnboardingAfterStart();if(motionLabMode){motionLab.changed=[]{ApplyMotionTuning();InvalidateChrome();};motionLab.replay=MotionLabReplay;if(motionLab.Create(hi,window,base+L"\\motion-tuning.ini")){ApplyMotionTuning();motionLab.Show();}}if(smoke)SetTimer(window,1,splitTest?180000:60000,nullptr);
#ifdef BROWSER_UPDATES
    StartUpdates(); // never blocks: the delayed startup check runs on the engine's thread
#endif
    // TEST LAUNCHER ONLY (« Voir la modale Nouveautés.cmd »): opens the Nouveautés Mini at start for review. It never
    // writes the read state and is reachable only through this command-line argument (no menu, no shortcut).
    if(!smoke&&!privateWindow&&wcsstr(args,L"--release-notes-preview"))Defer([]{Log(L"RELEASE NOTES preview (test launcher)");ShowReleaseNotes(true,nullptr,false);});
    // Engine arguments are per ENVIRONMENT (EngineArgumentsFor, passed in the environment options): one window
    // hosts several profiles, each with its own flags. The process-wide variable would force the first profile's
    // flags onto every environment, so a user-set value is kept aside (BROWSER_GIVEN_WEBVIEW2_ARGUMENTS, inherited
    // by the windows this process starts) and merged into each environment's arguments instead.
    {auto read=[](const wchar_t* name){const DWORD size=GetEnvironmentVariableW(name,nullptr,0);std::wstring v(size,L'\0');if(size)v.resize(GetEnvironmentVariableW(name,v.data(),size));return v;};
     engineGivenArgs=read(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");if(engineGivenArgs.empty())engineGivenArgs=read(L"BROWSER_GIVEN_WEBVIEW2_ARGUMENTS");
     if(!engineGivenArgs.empty())SetEnvironmentVariableW(L"BROWSER_GIVEN_WEBVIEW2_ARGUMENTS",engineGivenArgs.c_str());
     SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",nullptr);}
    StartWorkspaces();
    StartEngine(); // loader, probe, environment; a failure opens the recovery dialog
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(motionLab.Window()&&IsDialogMessageW(motionLab.Window(),&msg))continue;if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){int cmd=Key((UINT)msg.wParam);if(cmd){keyboardModality=true;Command(cmd);SyncAccessibility();continue;}}TranslateMessage(&msg);DispatchMessageW(&msg);}StopEngineStart();engineStart.reset();environment.set(nullptr);if(loader)FreeLibrary(loader);RunPendingRelaunch();suggestions.Dispose();downloadPanel.Dispose();downloadDrawer.Dispose();settingsDrawer.Dispose();historyDrawer.Dispose();bookmarkPopover.Dispose();bookmarkToast.Dispose();bookmarksDrawer.Dispose();contextMenu.Dispose();splitOverlay.Dispose();statusBubble.Dispose();motionLab.Dispose();chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return halted?2:0;
}
