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
#include <fstream>
#include <filesystem>
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
#include "context-menu.h"
#include "accessibility.h"
#include "bookmarks.h"
#include "bookmark-popover.h"
#include "bookmark-toast.h"
#include "bookmarks-panel.h"
#include <shlobj.h>
// Self-update host integration is compiled into identity-stamped builds only
// (tools/release/build.py defines BROWSER_HAS_BUILD_INFO). Unstamped builds and the
// native fixtures that include this file keep the portable, update-free behaviour.
#ifdef BROWSER_HAS_BUILD_INFO
#define BROWSER_UPDATES 1
#include "build-info.h"
#include "update-core.h"
#include "update-toast.h"
#endif

// COM callbacks use the official SDK interface IDs, independent of compiler UUID extensions.
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2SourceChangedEventHandler,"{3c067f9f-5388-4772-8b48-79f7ef1ab37c}")
GUID_TEXT(ICoreWebView2HistoryChangedEventHandler,"{c79a420c-efd9-4058-9295-3e8b4bcab645}")
GUID_TEXT(ICoreWebView2DocumentTitleChangedEventHandler,"{f5f2b923-953e-4042-9f95-f3a118e1afd4}")
GUID_TEXT(ICoreWebView2NewWindowRequestedEventHandler,"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}")
GUID_TEXT(ICoreWebView2AcceleratorKeyPressedEventHandler,"{b29c7e28-fa79-41a8-8e44-65811c76dcb2}")
GUID_TEXT(ICoreWebView2ProcessFailedEventHandler,"{79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2FaviconChangedEventHandler,"{2913da94-833d-4de0-8dca-900fc524a1a4}")
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
GUID_TEXT(ICoreWebView2TrySuspendCompletedHandler,"{00f206a7-9d17-4605-91f6-4e8e4de192e3}")
GUID_TEXT(ICoreWebView2PrintToPdfCompletedHandler,"{ccf1ef04-fd8e-4d5f-b2de-0983e41b8c36}")
GUID_TEXT(ICoreWebView2GetNonDefaultPermissionSettingsCompletedHandler,"{38274481-a15c-4563-94cf-990edc9aeb95}")
GUID_TEXT(ICoreWebView2SetPermissionStateCompletedHandler,"{fc77fb30-9c9e-4076-b8c7-7644a703ca1b}")
template<class I, class... A> class Handler final : public I {
    LONG refs=1; IID iid{}; std::function<HRESULT(A...)> fn;
public:
    explicit Handler(std::function<HRESULT(A...)> f):fn(std::move(f)){CLSIDFromString(GuidText<I>(),&iid);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER; *out=nullptr;
        if(id==IID_IUnknown || id==iid){*out=static_cast<I*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override{return fn(args...);}
};
template<class I,class... A,class F> auto Callback(F f){return new Handler<I,A...>(std::move(f));}
template<class T> struct Com {
    T* p=nullptr; ~Com(){if(p)p->Release();} T* operator->()const{return p;}
    void set(T* q){if(q)q->AddRef();if(p)p->Release();p=q;}
};
// Stable per-tab identity (never reused): accessibility keys survive closes and moves.
inline unsigned NextTabSerial(){static unsigned serial=0;return ++serial;}
struct Tab { unsigned serial=NextTabSerial(); Com<ICoreWebView2Controller> controller; Com<ICoreWebView2> view; Com<ICoreWebView2_15> faviconView; NativeChrome::FaviconState favicon; NativeChrome::Loading::State loading; std::wstring title=L"Nouvel onglet",uri; bool closed=false,home=true,refreshing=false,certificateError=false;
    ULONGLONG hiddenAt=GetTickCount64(); bool sleeping=false; }; // hiddenAt: when it last stopped being the active tab
void TabShown(const std::shared_ptr<Tab>& tab);
HWND window,tabs,address,back,forward,reload,add,closeTab,home;
HFONT font; HBRUSH paper; HINSTANCE instance; HMODULE loader;
Com<ICoreWebView2Environment> environment;
std::vector<std::shared_ptr<Tab>> pages; int current=-1; bool halted=false;
struct ClosedTab { std::wstring uri,title; int index; };
std::vector<ClosedTab> closedTabs; bool sessionReady=false;
std::wstring base,dataDir,profileDir,sessionFile,logFile,downloadsFile,settingsFile; const COLORREF BG=NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::canvas);
NativeChrome::Chrome chrome; NativeChrome::DownloadsUI::Popover downloadPanel; NativeChrome::DownloadsUI::Drawer downloadDrawer; NativeChrome::SettingsUI::Drawer settingsDrawer; NativeChrome::ContextMenu::Menu contextMenu; BrowserSettings::State settings; NativeUI::AddressKeyState addressKeys; NativeChrome::Motion::State motion; int pressed=0; bool motionMinimized=false; constexpr UINT_PTR MotionTimerId=72; std::optional<NativeChrome::Motion::Time> motionDeadline; HWND tooltip; int hover=0,keyboardFocus=0; bool editing=false,keyboardModality=false;
struct TabDrag { std::shared_ptr<Tab> tab; POINT start{}; float grab=0; bool active=false; } tabDrag;
// Address bar (issue #20): visited-sites history, the suggestion list and the text the
// user typed (the field may also show an inline completion or an arrowed row).
AddressBar::History history; std::vector<AddressBar::PopularSite> popularSites; NativeChrome::AddressUI::Suggestions suggestions; std::wstring addressTyped; bool addressSetting=false,addressDeleting=false;
// Browsing history drawer (src/history-panel.h): the toolbar History button (or Ctrl+H) opens it docked on the right.
NativeChrome::HistoryUI::Drawer historyDrawer; bool historyDismissClick=false;
void InvalidateChrome();
void CancelTabDrag(){tabDrag={};chrome.dragIndex=-1;chrome.dragOffset=0;InvalidateChrome();}
int S(float value){return (int)std::lround(value*GetDpiForWindow(window)/96.f);}
void RefreshTools();
struct ICoreWebView2ContextMenuRequestedEventArgs; HRESULT PageContextMenu(const std::shared_ptr<Tab>& tab,ICoreWebView2ContextMenuRequestedEventArgs* args);
void ExtraCommand(int cmd);
// Bookmarks (section "Bookmarks" below).
constexpr int BookmarkBarCommand=3030,BookmarksDrawerCommand=3031,BookmarkUndoCommand=3032;
constexpr long long AccessibleBandKey=0x10003;
extern Accessibility::Bridge accessibility;
NativeChrome::BookmarkPopover::Popover bookmarkPopover; NativeChrome::BookmarkToast::Toast bookmarkToast; NativeChrome::BookmarksUI::Drawer bookmarksDrawer;
bool SyncBookmarkChrome();void BookmarksChanged();void ToggleBookmarksDrawer(bool keyboard);bool BookmarkedSite(const std::wstring& site);long long AccessibleBandItemKey(int region);
// Self-update host hooks (no-ops in unstamped builds).
std::wstring AboutTitle(){
#ifdef BROWSER_UPDATES
    return std::wstring(L"browser ")+BROWSER_VERSION_WIDE;
#else
    return L"browser";
#endif
}
void CheckForUpdates(); bool UpdateCheckAvailable(); bool FocusUpdateToast(); bool UpdateToastHasFocus(); void SyncUpdateToastReserve();
void SetUpdateToastTop(float dip); float UpdateToastHeight(); // the bookmarks band and the undo toast keep clear of it
void SyncUpdateToastMotion();
constexpr int CheckUpdatesCommand=3020;
void InvalidateChrome(){RECT r;GetClientRect(window,&r);r.bottom=std::min(r.bottom,(LONG)S(chrome.ContentTop()));InvalidateRect(window,&r,FALSE);}
// Frame wakes are display-paced by FrameClock (one outstanding notification, worker
// dormant when idle). Deadline-only dwells keep a single one-shot SetTimer. If the
// clock is unavailable the legacy SetTimer path remains (not display synchronised).
constexpr UINT FrameMessage=WM_APP+8; constexpr UINT_PTR DrawerWarmTimerId=73; NativeChrome::FrameClock::Clock frameClock; bool frameClockReady=false,motionFullFrames=false; long long paintedLoading=-2;
std::shared_ptr<Tab> Active();
// Loading-only frames repaint just the URL surface, and only when the quantised fill
// differs from what was last painted; any other motion (or the frame right after it
// ends, so its endpoint is painted) repaints the whole header.
void InvalidateMotion(NativeChrome::Motion::Time now){
    downloadPanel.Frame(now);downloadDrawer.Frame(now);settingsDrawer.Frame(now);historyDrawer.Frame(now);bookmarksDrawer.Frame(now);
    const bool full=motionFullFrames||motion.NextWake(now).delay||chrome.lottie.NextWake(now,motion.ReducedMotion()).delay;
    if(full){InvalidateChrome();return;}
    auto t=Active();if(chrome.LoadingSignature(t?t->loading.Sample(now,motion.ReducedMotion()):NativeChrome::Loading::Visual{})==paintedLoading)return;
    auto d=chrome.LoadingDirty();RECT r{S(d.left),S(d.top),S(d.right),S(d.bottom)};InvalidateRect(window,&r,FALSE);
}
void ScheduleMotion(NativeChrome::Motion::Time now){
    const bool reduced=motion.ReducedMotion();auto ui=NativeChrome::Motion::Earlier(motion.NextWake(now),chrome.lottie.NextWake(now,reduced));motionFullFrames=ui.delay.has_value();
    auto wake=NativeChrome::Motion::Earlier(ui,NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(NativeChrome::Motion::Earlier(downloadPanel.NextWake(now),downloadDrawer.NextWake(now)),settingsDrawer.NextWake(now)),NativeChrome::Motion::Earlier(historyDrawer.NextWake(now),bookmarksDrawer.NextWake(now))));if(current>=0&&current<(int)pages.size())wake=NativeChrome::Motion::Earlier(wake,pages[current]->loading.NextWake(now,reduced));if(halted||motionMinimized||!wake.delay){KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();return;}
    // A deadline due within one nominal frame is served by the next compositor tick.
    if(frameClockReady&&(wake.frames||*wake.delay<=*NativeChrome::Motion::Frame().delay)){if(motionDeadline){KillTimer(window,MotionTimerId);motionDeadline.reset();}frameClock.Request();return;}
    frameClock.Cancel();
    auto due=now+*wake.delay;if(motionDeadline&&*motionDeadline<=due)return;
    KillTimer(window,MotionTimerId);motionDeadline=due;
    if(!SetTimer(window,MotionTimerId,(UINT)wake.delay->count(),nullptr)){motionDeadline.reset();motion.SetReducedMotion(true);motion.copy.Clear();chrome.lottie.Clear();motion.SetInteraction(hover,0,now);InvalidateChrome();}
}
void UpdateInteraction(int newHover,int newPressed){hover=newHover;pressed=newPressed;auto now=NativeChrome::Motion::Now();int group=NativeChrome::IsClose(hover)?NativeChrome::TabBase+NativeChrome::TabIndex(hover):hover;motion.SetInteraction(group,pressed==hover?pressed:0,now);InvalidateChrome();ScheduleMotion(now);}

constexpr UINT Shortcut=WM_APP+1;
bool smoke=false; int testStage=0; int chromePaints=0;
void SmokeCheck();
void SmokeAdvance();

std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
void Log(const std::wstring& s){if(logFile.empty())return;std::ofstream f(std::filesystem::path(logFile),std::ios::app);f<<Utf8(s)<<"\n";if(smoke&&s.rfind(L"NAVIGATION OK",0)==0)PostMessageW(window,WM_APP+2,0,0);}
void Fail(const std::wstring& where,HRESULT hr){if(halted)return;halted=true;for(auto& tab:pages)tab->loading.Clear();chrome.lottie.Clear();KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();wchar_t code[32];swprintf(code,32,L"0x%08lX",(unsigned long)hr);auto msg=where+L" : "+code+L"\nOpérations arrêtées. Aucun correctif automatique.";Log(L"WEBVIEW2 ERROR "+msg);MessageBoxW(window,msg.c_str(),L"WebView2 — arrêt demandé",MB_OK|MB_ICONERROR);}
bool Check(HRESULT hr,const wchar_t* op){if(FAILED(hr)){Fail(op,hr);return false;}return !halted;}
std::shared_ptr<Tab> Active(){return current>=0&&current<(int)pages.size()?pages[current]:nullptr;}
void FocusAddress(){editing=true;ShowWindow(address,SW_SHOW);SetFocus(address);SendMessageW(address,EM_SETSEL,0,-1);InvalidateRect(window,nullptr,FALSE);}
std::vector<std::wstring> Titles(){std::vector<std::wstring> titles;for(auto& t:pages)titles.push_back(t->title);return titles;}
std::shared_ptr<const NativeChrome::FaviconImage> SiteIcon(const std::wstring& site);
// What the field reads: the active tab's address and security outside editing; while
// editing, the active suggestion's glyph in the indicator slot.
void SyncAddressDisplay(){auto t=Active();chrome.address=t&&!t->home?AddressBar::Describe(t->uri,t->certificateError):AddressBar::Display{};chrome.addressEditing=editing;
    chrome.editIcon=nullptr;chrome.editFavicon=nullptr;if(editing&&suggestions.Visible()){const auto& rows=suggestions.Data().rows;const int a=suggestions.Active();if(a>=0&&a<(int)rows.size()){chrome.editIcon=NativeChrome::AddressUI::Suggestions::IconFor(rows[a]);if(rows[a].kind!=AddressBar::Kind::Search)chrome.editFavicon=SiteIcon(rows[a].site);}}}
void BuildChrome(){SyncAddressDisplay();SyncBookmarkChrome();auto titles=Titles();chrome.Build((int)pages.size(),current,&titles);}
// After a pointer close the strip keeps its tab width until the pointer leaves
// it, so repeated clicks keep landing on close buttons.
void LockTabWidth(){chrome.lockedTab=chrome.strip.tab;}
void UnlockTabWidth(){if(chrome.lockedTab<=0)return;chrome.lockedTab=0;BuildChrome();RefreshTools();InvalidateChrome();}
void FocusPage(){
    keyboardFocus=0;editing=false;ShowWindow(address,SW_HIDE);SetFocus(window);
    auto t=Active();if(t&&t->controller.p&&!t->home&&!halted)Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus page");
    InvalidateChrome();
}
void ToggleChromeFocus(){
    // F6 cycle: page -> toolbar -> update toast (when one is shown) -> page.
    if(UpdateToastHasFocus()){FocusPage();return;}
    if(GetFocus()==window||GetFocus()==address){editing=false;if(FocusUpdateToast()){keyboardFocus=0;InvalidateChrome();return;}FocusPage();return;}
    SetFocus(window);auto order=chrome.FocusOrder();keyboardFocus=order.empty()?0:order.front();InvalidateChrome();
}
void FindInPage(){
    auto t=Active();if(!t||!t->view.p||t->home)return;
    Com<ICoreWebView2_28> view;Com<ICoreWebView2Environment15> env;
    Com<ICoreWebView2Find> find;Com<ICoreWebView2FindOptions> options;
    if(!Check(t->view->QueryInterface(IID_ICoreWebView2_28,(void**)&view.p),L"QueryInterface Find")||
       !Check(environment->QueryInterface(IID_ICoreWebView2Environment15,(void**)&env.p),L"QueryInterface Find environment")||
       !Check(view->get_Find(&find.p),L"get_Find")||!Check(env->CreateFindOptions(&options.p),L"CreateFindOptions"))return;
    FocusPage();
    auto completed=Callback<ICoreWebView2FindStartCompletedHandler,HRESULT>([](HRESULT hr)->HRESULT{Check(hr,L"Find completed");return S_OK;});
    HRESULT hr=find->Start(options.p,completed);completed->Release();Check(hr,L"Find Start");
}
void Layout(){if(!window)return;chrome.Resize();BuildChrome();auto r=chrome.edit;
    HDC dc=GetDC(address);auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(address,dc);
    auto placement=chrome.MeasureText(L"Ag",{r.left,44,r.right,76},16,false,false,-.16f);
    const int baseline=S(placement.originY+placement.baseline);int editHeight=std::max(S(22),(int)(metrics.tmHeight+metrics.tmExternalLeading+2));
    MoveWindow(address,S(r.left),baseline-metrics.tmAscent,std::max(1,S(r.right-r.left)),editHeight,TRUE);
    RECT format{};SendMessageW(address,EM_GETRECT,0,(LPARAM)&format);
    SetWindowPos(address,nullptr,S(r.left),baseline-metrics.tmAscent-format.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(!IsZoomed(window)){RECT bounds;GetClientRect(window,&bounds);SetWindowRgn(window,CreateRoundRectRgn(0,0,bounds.right+1,bounds.bottom+1,S(20),S(20)),TRUE);}else SetWindowRgn(window,nullptr,TRUE);
    // The page starts right under the chrome (81 DIP, 121 with the bookmarks band): no top padding (Paper c-v7q0dg).
    const float top=chrome.ContentTop();RECT bounds{S(4),S(top),S(chrome.width-4),std::max(S(top),S(chrome.height-4))};
    SetUpdateToastTop(top);
    bookmarkToast.Reposition();for(auto& t:pages)if(t->controller.p&&!halted)Check(t->controller->put_Bounds(bounds),L"put_Bounds");
    RefreshTools();InvalidateRect(window,nullptr,FALSE);
}
void Update(){
    auto t=Active();if(!t)return;
    BOOL canGoBack=FALSE,canGoForward=FALSE;
    if(t->view.p&&!halted){
        if(!Check(t->view->get_CanGoBack(&canGoBack),L"get_CanGoBack")||
           !Check(t->view->get_CanGoForward(&canGoForward),L"get_CanGoForward"))return;
    }
    chrome.SetEnabled('B',canGoBack!=FALSE);chrome.SetEnabled('F',canGoForward!=FALSE);
    chrome.SetEnabled('C',!t->home&&!t->uri.empty());if(!editing)SetWindowTextW(address,t->home?L"":t->uri.c_str());SetWindowTextW(window,(t->title+L" — browser").c_str());BuildChrome();RefreshTools();InvalidateRect(window,nullptr,FALSE);
}
void Select(int n){if(n<0||n>=(int)pages.size()||halted)return;editing=false;ShowWindow(address,SW_HIDE);current=n;chrome.lottie.Clear();motion.SelectTab(n+1,NativeChrome::Motion::Now());ScheduleMotion(NativeChrome::Motion::Now());for(int i=0;i<(int)pages.size();++i)if(pages[i]->controller.p)Check(pages[i]->controller->put_IsVisible(i==n&&!pages[i]->home),L"put_IsVisible");Update();auto t=Active();TabShown(t);if(t->controller.p&&!t->home)Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus");}
// Move shared pointers, preserving the active WebView and its asynchronous callbacks.
void MoveTab(int from,int to){
    if(halted||from<0||to<0||from>=(int)pages.size()||to>=(int)pages.size()||from==to)return;
    auto active=Active();auto tab=pages[from];pages.erase(pages.begin()+from);pages.insert(pages.begin()+to,tab);
    current=(int)(std::find(pages.begin(),pages.end(),active)-pages.begin());
    motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;Update();
}
void DragTab(POINT point){
    if(!tabDrag.tab||halted)return;
    auto it=std::find(pages.begin(),pages.end(),tabDrag.tab);if(it==pages.end()){CancelTabDrag();return;}
    if(!tabDrag.active){
        if(std::abs(point.x-tabDrag.start.x)<=GetSystemMetrics(SM_CXDRAG)&&std::abs(point.y-tabDrag.start.y)<=GetSystemMetrics(SM_CYDRAG))return;
        tabDrag.active=true;
    }
    float x=point.x*96.f/GetDpiForWindow(window);
    // Drop slot = the visible tab under the pointer on the adaptive, scrolled strip.
    const auto* strip=&chrome.strip;int from=(int)(it-pages.begin()),lo=0,hi=(int)pages.size()-1;
    while(lo<hi&&!strip->Visible(lo))++lo;while(hi>lo&&!strip->Visible(hi))--hi;
    int slot=std::clamp((int)std::floor((x+strip->scroll-NativeChrome::Design::TabStrip::left)/strip->Pitch()),lo,hi);
    MoveTab(from,slot);
    it=std::find(pages.begin(),pages.end(),tabDrag.tab);chrome.dragIndex=(int)(it-pages.begin());
    auto surface=strip->Surface(chrome.dragIndex);
    float left=std::max(strip->Surface(0).left,strip->ViewLeft());
    float right=std::min(strip->Surface(strip->count-1).right,strip->ViewRight());
    chrome.dragOffset=std::clamp(x-tabDrag.grab,left,std::max(left,right-strip->tab))-surface.left;
    InvalidateChrome();
}
const wchar_t* HomeHtml=LR"HTML(<!doctype html><html lang="fr"><meta charset="utf-8"><title>Nouvel onglet</title><style>html,body{margin:0;background:#171717;color:white;min-height:100%;color-scheme:dark}</style><body></body></html>)HTML";
std::wstring Destination(std::wstring s){auto first=s.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)return L"";s=s.substr(first,s.find_last_not_of(L" \t\r\n")-first+1);if(s==L"about:blank")return s;
    auto low=s;std::transform(low.begin(),low.end(),low.begin(),towlower);if(low.rfind(L"https://",0)==0||low.rfind(L"http://",0)==0)return s;
    if(s.find_first_of(L" \t")==std::wstring::npos && s.find(L"://")==std::wstring::npos && (s.find(L'.')!=std::wstring::npos||s.rfind(L"localhost",0)==0))return L"https://"+s;
    return BrowserSettings::SearchUrl(settings.engine,AddressBar::PercentEncode(s));
}
void NewTab(std::wstring uri=L"",int index=-1,std::wstring title=L"Nouvel onglet");
void ReopenTab(){if(closedTabs.empty()||pages.size()>=128)return;auto tab=closedTabs.back();NewTab(tab.uri,tab.index,tab.title);if(!halted)closedTabs.pop_back();}
void Navigate(std::shared_ptr<Tab> t,const std::wstring& uri){if(!t||!t->view.p||halted)return;t->home=uri.empty();t->uri=uri;Check(uri.empty()?t->view->NavigateToString(HomeHtml):t->view->Navigate(uri.c_str()),uri.empty()?L"NavigateToString":L"Navigate");}
// ---- Address suggestions (issue #20) -----------------------------------------
// The EDIT keeps focus: typing recomputes the list, the arrows move its active row
// (the field shows what Enter will open), Escape restores then closes, Enter opens.
void SyncAccessibility();void AnnounceSuggestion();void SuggestionsStructureChanged();void SaveHistory();void ForgetSiteIcon(const std::wstring& site);
void HideSuggestions(){if(!suggestions.Visible())return;suggestions.Hide();chrome.editIcon=nullptr;InvalidateChrome();SuggestionsStructureChanged();}
std::wstring AddressText(){int n=GetWindowTextLengthW(address);std::wstring s(n+1,0);GetWindowTextW(address,s.data(),n+1);s.resize(n);return s;}
void SetAddressText(const std::wstring& s,int from,int to){addressSetting=true;SetWindowTextW(address,s.c_str());SendMessageW(address,EM_SETSEL,from,to);addressSetting=false;}
void PlaceSuggestions(AddressBar::Suggestions data,int active){
    // SETTINGS(privacy): "Suggestions de recherche" off keeps the list closed (inline completion stays).
    if(!settings.searchSuggestions){HideSuggestions();return;}
    RECT r{S(chrome.bar.left),S(chrome.bar.top),S(chrome.bar.right),S(chrome.bar.bottom)};MapWindowPoints(window,nullptr,(POINT*)&r,2);
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
    default:return BrowserSettings::SearchUrl(settings.engine,AddressBar::PercentEncode(AddressBar::Trim(row.text)));
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
    SaveHistory();Log(L"HISTORY removed "+row.text);
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
void CloseAt(int index){if(halted||index<0||index>=(int)pages.size())return;
    CancelTabDrag();const bool active=index==current;const int next=NativeChrome::SelectionAfterClose((int)pages.size(),current,index);
    motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;auto t=pages[index];if(!t->home&&!t->uri.empty()){if(closedTabs.size()==25)closedTabs.erase(closedTabs.begin());closedTabs.push_back({t->uri,t->title,index});}t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();pages.erase(pages.begin()+index);
    if(pages.empty()){current=-1;NewTab();}else if(active)Select(next);else{current=next;motion.SelectTab(current+1,NativeChrome::Motion::Now());Update();}
}
enum ShortcutCommand { FocusChrome=3000, FindPage, PrintActive, MoveTabLeft='{', MoveTabRight='}', SelectTabBase=3010 };
void Close(){CloseAt(current);}
void Command(int cmd){if(halted||!chrome.Enabled(cmd))return;if(tabDrag.tab){CancelTabDrag();if(GetCapture()==window)ReleaseCapture();UpdateInteraction(hover,0);}auto t=Active();if(cmd>=SelectTabBase&&cmd<SelectTabBase+9){Select(cmd==SelectTabBase+8?(int)pages.size()-1:cmd-SelectTabBase);return;}switch(cmd){case FocusChrome:ToggleChromeFocus();break;case FindPage:FindInPage();break;case '{':MoveTab(current,current-1);break;case '}':MoveTab(current,current+1);break;case 'L':FocusAddress();break;case ']':if(!pages.empty())Select((current+1)%(int)pages.size());break;case '[':if(!pages.empty())Select((current+(int)pages.size()-1)%(int)pages.size());break;case 'T':NewTab();FocusAddress();break;case 'W':Close();break;case 'U':ReopenTab();break;case 'R':if(t&&t->view.p&&t->loading.IsLoading()){Check(t->view->Stop(),L"Stop");break;}if(t&&t->view.p&&Check(t->view->Reload(),L"Reload")){t->refreshing=true;auto now=NativeChrome::Motion::Now();chrome.lottie.RefreshBegin(now);InvalidateChrome();ScheduleMotion(now);}break;case 'H':Navigate(t,L"");break;case 'B':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoBack(&yes);if(yes)Check(t->view->GoBack(),L"GoBack");}break;case 'F':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoForward(&yes);if(yes)Check(t->view->GoForward(),L"GoForward");}break;default:ExtraCommand(cmd);break;}}
bool BookmarkPopoverFocus();bool SmokeBookmarks();
int Key(UINT key){
    const bool ctrl=GetKeyState(VK_CONTROL)<0,alt=GetKeyState(VK_MENU)<0,shift=GetKeyState(VK_SHIFT)<0;
    // The bookmark popover's fields keep every key (Ctrl+D, Ctrl+Z… belong to the text there).
    if(BookmarkPopoverFocus())return 0;
    if(ctrl&&!alt){
        if(!shift&&key=='D')return 'K';
        if(shift&&key=='B')return BookmarkBarCommand;
        if(shift&&key=='O')return BookmarksDrawerCommand;
        // Ctrl+Z undoes a bookmark change only from the chrome, the toast or the Favoris drawer, never in a text field or the page.
        if(!shift&&key=='Z'){HWND f=GetFocus();return f&&(f==window||f==bookmarkToast.Window()||f==bookmarksDrawer.Window())?BookmarkUndoCommand:0;}
        if(shift&&key=='T')return 'U';
        if(!shift&&key>='1'&&key<='9')return SelectTabBase+key-'1';
        if(shift&&key==VK_PRIOR)return MoveTabLeft;
        if(shift&&key==VK_NEXT)return MoveTabRight;
        if(key==VK_TAB)return shift?'[':']';
        // In the page WebView2 handles Ctrl+F itself, retaining its native find state.
        if(!shift&&key=='F')return GetFocus()==window||GetFocus()==address?FindPage:0;
        // Ctrl+P likewise: the page opens the same browser print preview itself (no double dialog).
        if(!shift&&key=='P')return GetFocus()==window||GetFocus()==address?PrintActive:0;
        if(!shift&&(key=='L'||key=='T'||key=='W'||key=='R'))return key;
        if(!shift&&key=='H')return 'Y'; // history, as in every desktop browser
    }
    if(!ctrl&&!alt&&!shift&&key==VK_F6)return FocusChrome;
    if(!ctrl&&!alt&&key==VK_F5)return 'R';
    if(alt&&!ctrl&&!shift){if(key=='D')return 'L';if(key==VK_LEFT)return 'B';if(key==VK_RIGHT)return 'F';}
    return 0;
}

// Store UTF-8 URLs alongside the profile, preserving blank tabs and selection.
// Portable builds keep <exe dir>\session; installed builds use the stable data
// directory resolved by the update core (outside every version directory).
void SaveSession(){
    if(smoke||!sessionReady||pages.empty())return;
    BrowserSession::State state;state.active=current;
    for(auto& tab:pages)state.urls.push_back(tab->home?"":Utf8(tab->uri));
    const auto path=std::filesystem::path(sessionFile),temp=std::filesystem::path(sessionFile+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);BrowserSession::Write(out,state);out.close();if(!out){Log(L"SESSION save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"SESSION replace failed");
}
// Visited sites for the address suggestions, beside `session` (UTF-8, atomic replace).
std::wstring HistoryFile(){return dataDir+L"\\history";}
void SaveHistory(){
    if(smoke)return;
    const auto path=std::filesystem::path(HistoryFile()),temp=std::filesystem::path(HistoryFile()+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);history.Write(out);out.close();if(!out){Log(L"HISTORY save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"HISTORY replace failed");
}
// Result pages of the search engines are not suggested back: typing "git" should
// offer the sites visited, not every past search for it.
bool SearchResultPage(const std::wstring& uri){for(int i=0;i<BrowserSettings::EngineCount;++i)if(uri.rfind(BrowserSettings::Info(static_cast<BrowserSettings::Engine>(i)).search,0)==0)return true;return false;}
// Remembered site icons (issue #20): the favicon a tab received, kept per site in
// <data>\favicons\<site>.png and shown by the suggestions. Never fetched while typing.
std::map<std::wstring,std::shared_ptr<const NativeChrome::FaviconImage>> siteIcons;std::set<std::wstring> siteIconsMissing;std::map<std::wstring,size_t> siteIconsWritten;
std::wstring SiteIconFile(const std::wstring& site){std::wstring name=site;for(auto& c:name)if(!(iswalnum(c)||c==L'.'||c==L'-'))c=L'_';return dataDir+L"\\favicons\\"+name+L".png";}
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
    std::wstring name=site;for(auto& c:name)if(!(iswalnum(c)||c==L'.'||c==L'-'))c=L'_';
    std::shared_ptr<const NativeChrome::FaviconImage> image;
    for(const wchar_t* ext:{L".png",L".ico"})if(!image)image=DecodeIconFile(base+L"\\assets\\sites\\"+name+ext);
    return bundledIcons[site]=image;
}
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
    siteIcons[site]=image;siteIconsMissing.erase(site);if(smoke||!stream)return;
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
void ForgetSiteIcon(const std::wstring& site){if(site.empty())return;siteIcons.erase(site);siteIconsWritten.erase(site);siteIconsMissing.insert(site);if(smoke)return;if(!DeleteFileW(SiteIconFile(site).c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND)Log(L"FAVICONS delete failed "+site);}
void ClearSiteIcons(){siteIcons.clear();siteIconsMissing.clear();siteIconsWritten.clear();if(smoke)return;std::error_code ignored;std::filesystem::remove_all(std::filesystem::path(dataDir)/L"favicons",ignored);}
void LoadPopularSites(){std::ifstream in(std::filesystem::path(base+L"\\assets\\popular-sites.txt"),std::ios::binary);popularSites=AddressBar::ReadPopular(in);if(popularSites.empty())Log(L"ADDRESS popular sites unavailable");}
void LoadHistory(){LoadPopularSites();if(smoke)return;std::ifstream in(std::filesystem::path(HistoryFile()),std::ios::binary);history=AddressBar::History::Read(in);}
void RestoreSession(){
    BrowserSession::State state;
    // "Restaurer les onglets" off: start on a fresh tab (the session is still saved on exit).
    if(!smoke&&settings.restoreTabs){std::ifstream in(std::filesystem::path(sessionFile),std::ios::binary);state=BrowserSession::Read(in);}
    for(const auto& url:state.urls){
        int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),nullptr,0);
        if(!url.empty()&&!size){state=BrowserSession::State{};break;}
    }
    for(const auto& url:state.urls){int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),nullptr,0);std::wstring uri(size,0);if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data(),(int)url.size(),uri.data(),size);NewTab(uri);}
    if(pages.empty())NewTab();else Select(state.active);
    sessionReady=true;
}

// ---- Downloads -------------------------------------------------------------
// WebView2's own flyout is suppressed (Handled); operations feed the native
// "Download history" popover. Finished entries persist in `downloads` next to
// `session`; in-progress operations belong to the engine and are not restored.
namespace DL=NativeChrome::Downloads;
std::vector<DL::Item> downloads; int nextDownloadId=1;
struct DownloadOperation { Com<ICoreWebView2DownloadOperation> op; bool userPaused=false; };
std::map<int,std::shared_ptr<DownloadOperation>> downloadOps;
bool downloadDismissClick=false;
DL::Item* FindDownload(int id){for(auto& item:downloads)if(item.id==id)return &item;return nullptr;}
void SaveDownloads(){
    if(smoke)return;
    const auto path=std::filesystem::path(downloadsFile),temp=std::filesystem::path(downloadsFile+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);DL::WriteHistory(out,downloads);out.close();if(!out){Log(L"DOWNLOADS save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"DOWNLOADS replace failed");
}
// History written before start times existed: date each row from its file's
// creation time (when the download began writing it), else from the history
// file itself, then save so the date sticks.
std::int64_t FileTimeUnixMs(const FILETIME& t){return NativeChrome::DownloadsUI::FileTimeMs(t)-11644473600000LL;}
void LoadDownloads(){
    if(smoke)return;
    const auto path=std::filesystem::path(downloadsFile);
    {std::ifstream in(path,std::ios::binary);downloads=DL::ReadHistory(in,nextDownloadId);}
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
std::wstring InterruptText(COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON r){
    switch(r){
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:return L"Disk full";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:return L"Access denied";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG:return L"Name too long";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE:return L"File too large";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED_BY_POLICY:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:return L"Blocked";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:return L"Offline";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_INVALID_REQUEST:return L"Network error";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN:case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_DOWNLOAD_PROCESS_CRASHED:return L"Interrupted";
    default:break;
    }
    if(r>=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED&&r<=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_CROSS_ORIGIN_REDIRECT)return L"Server error";
    return L"File error";
}
void ShowDownloadDrawer();void SyncLatch();
void DownloadsChanged(){downloadPanel.Changed();downloadDrawer.Changed();}
// Pull the operation's current state into its history row (UI thread, engine events).
void SyncDownload(int id){
    auto found=downloadOps.find(id);auto* item=FindDownload(id);if(found==downloadOps.end()||!item)return;
    auto entry=found->second;auto* op=entry->op.p;if(!op)return;
    INT64 total=0,received=0;op->get_TotalBytesToReceive(&total);op->get_BytesReceived(&received);item->total=total;item->received=received;
    LPWSTR text=nullptr;if(SUCCEEDED(op->get_ResultFilePath(&text))&&text){if(*text)item->path=text;CoTaskMemFree(text);}
    text=nullptr;item->endTime=0;if(SUCCEEDED(op->get_EstimatedEndTime(&text))&&text){item->endTime=DL::ParseIsoTime(text);CoTaskMemFree(text);}
    BOOL resume=FALSE;op->get_CanResume(&resume);item->canResume=resume!=FALSE;
    COREWEBVIEW2_DOWNLOAD_STATE state=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;op->get_State(&state);
    const auto before=item->state;
    if(state==COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED){item->state=DL::State::Completed;item->missing=false;}
    else if(state==COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS)item->state=entry->userPaused?DL::State::Paused:DL::State::InProgress;
    else{
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason=COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;op->get_InterruptReason(&reason);
        if(reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_PAUSED)item->state=DL::State::Paused;
        else if(reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED)item->state=DL::State::Cancelled;
        else{item->state=DL::State::Failed;item->reason=InterruptText(reason);}
    }
    if(item->state!=before)Log(L"DOWNLOAD state="+std::to_wstring((int)item->state)+L" "+DL::FileName(item->path));
    // Terminal rows keep no engine reference; resumable failures keep it for Retry.
    if(item->state==DL::State::Completed||item->state==DL::State::Cancelled||(item->state==DL::State::Failed&&!item->canResume))downloadOps.erase(found);
    if(item->state!=before&&DL::Persistent(*item))SaveDownloads();
    DownloadsChanged();
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
void RefreshMissing(){for(auto& item:downloads)if(item.state==DL::State::Completed)item.missing=GetFileAttributesW(item.path.c_str())==INVALID_FILE_ATTRIBUTES;}
void ShowDownloads(){
    if(!downloadPanel.Window())return;
    RefreshMissing();settingsDrawer.Hide(false,true);historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);
    // Right edge on the toolbar's 6 DIP margin, just under the toolbar row.
    RECT anchor{0,S(48),S(chrome.width-NativeChrome::Design::Space::sm),S(76)};MapWindowPoints(window,nullptr,(POINT*)&anchor,2);
    chrome.latched='D';InvalidateChrome();downloadPanel.Show(anchor);
    // Once the popover has settled, prepare the drawer so "View all" never waits.
    SetTimer(window,DrawerWarmTimerId,260,nullptr);
}
// "View all": the popover gives way to the drawer docked on the right.
void ShowDownloadDrawer(){
    if(!downloadDrawer.Window())return;
    RefreshMissing();settingsDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);chrome.latched='D';InvalidateChrome();
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
    std::wstring kind=ext.size()>1?ext.substr(1):L"";for(auto& c:kind)c=(wchar_t)std::towupper(c);const std::wstring label=L"Fichier "+kind,spec=L"*"+ext;
    const COMDLG_FILTERSPEC types[2]={{label.c_str(),spec.c_str()},{L"Tous les fichiers",L"*.*"}};
    if(kind.empty())dialog->SetFileTypes(1,types+1);else{dialog->SetFileTypes(2,types);dialog->SetDefaultExtension(ext.c_str()+1);}
    dialog->SetTitle(L"Enregistrer le téléchargement");dialog->SetFileName(file.c_str());
    IShellItem* folder=nullptr;if(SUCCEEDED(SHCreateItemFromParsingName(EffectiveDownloadFolder().c_str(),nullptr,IID_PPV_ARGS(&folder)))&&folder){dialog->SetFolder(folder);folder->Release();}
    bool keep=true;const HRESULT hr=dialog->Show(window);
    if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))keep=false;
    else if(FAILED(hr))Log(L"DOWNLOADS save dialog failed hr="+std::to_wstring((unsigned long)hr));
    else{IShellItem* item=nullptr;PWSTR chosen=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&item&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&chosen))&&chosen)path=chosen;else Log(L"DOWNLOADS save dialog result unavailable");CoTaskMemFree(chosen);if(item)item->Release();}
    dialog->Release();return keep;
}
void TrackDownload(ICoreWebView2DownloadStartingEventArgs* args);
// "Demander où enregistrer": the engine waits on a deferral while the save dialog
// runs from the message loop, never re-entrantly inside its event handler.
constexpr UINT DownloadAskMessage=WM_APP+21;
struct PendingDownload { Com<ICoreWebView2DownloadStartingEventArgs> args; Com<ICoreWebView2Deferral> deferral; };
std::vector<std::shared_ptr<PendingDownload>> pendingDownloads; bool downloadAsking=false;
void AskPendingDownloads(){
    // The modal dialog pumps messages: later downloads queue up behind this loop.
    if(downloadAsking)return;downloadAsking=true;
    while(!pendingDownloads.empty()){
        auto pending=pendingDownloads.front();pendingDownloads.erase(pendingDownloads.begin());auto* args=pending->args.p;
        LPWSTR text=nullptr;std::wstring path;if(SUCCEEDED(args->get_ResultFilePath(&text))&&text){path=text;CoTaskMemFree(text);}
        if(!halted&&AskDownloadPath(path)){if(FAILED(args->put_ResultFilePath(path.c_str())))Log(L"DOWNLOADS put_ResultFilePath failed "+path);TrackDownload(args);}
        else{args->put_Cancel(TRUE);Log(L"DOWNLOAD cancelled from the save dialog");}
        if(FAILED(pending->deferral->Complete()))Log(L"DOWNLOADS deferral completion failed");
    }
    downloadAsking=false;
}
HRESULT DownloadStarting(ICoreWebView2DownloadStartingEventArgs* args){
    if(halted||!args)return S_OK;
    args->put_Handled(TRUE); // the native popover replaces WebView2's flyout
    if(smoke){const auto target=base+L"\\smoke-download.txt";DeleteFileW(target.c_str());args->put_ResultFilePath(target.c_str());TrackDownload(args);return S_OK;}
    if(settings.askWhereToSave){
        auto pending=std::make_shared<PendingDownload>();
        if(SUCCEEDED(args->GetDeferral(&pending->deferral.p))&&pending->deferral.p){pending->args.set(args);pendingDownloads.push_back(pending);PostMessageW(window,DownloadAskMessage,0,0);return S_OK;}
        Log(L"DOWNLOADS deferral unavailable: saving without asking");
    }
    if(!downloadFolderOnProfile&&!settings.downloadFolder.empty()){
        LPWSTR text=nullptr;if(SUCCEEDED(args->get_ResultFilePath(&text))&&text){const auto target=UniqueDownloadPath(settings.downloadFolder,text);CoTaskMemFree(text);if(FAILED(args->put_ResultFilePath(target.c_str())))Log(L"DOWNLOADS put_ResultFilePath failed "+target);}
    }
    TrackDownload(args);return S_OK;
}
// History row + popover for a download whose path is settled.
void TrackDownload(ICoreWebView2DownloadStartingEventArgs* args){
    ICoreWebView2DownloadOperation* raw=nullptr;if(FAILED(args->get_DownloadOperation(&raw))||!raw){Log(L"DOWNLOAD operation unavailable");return;}
    auto entry=std::make_shared<DownloadOperation>();entry->op.p=raw;
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
    case DL::Action::Pause:if(op&&SUCCEEDED(op->Pause())){entry->userPaused=true;SyncDownload(id);}break;
    case DL::Action::Resume:case DL::Action::Retry:if(op&&SUCCEEDED(op->Resume())){entry->userPaused=false;SyncDownload(id);}break;
    case DL::Action::Cancel:if(op){op->Cancel();SyncDownload(id);}break;
    case DL::Action::Remove:{if(op&&(item->state==DL::State::InProgress||item->state==DL::State::Paused))op->Cancel();downloadOps.erase(id);downloads.erase(std::find_if(downloads.begin(),downloads.end(),[id](const DL::Item& d){return d.id==id;}));SaveDownloads();DownloadsChanged();break;}
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
void SaveSettings(){
    if(smoke)return;
    const auto path=std::filesystem::path(settingsFile),temp=std::filesystem::path(settingsFile+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);BrowserSettings::Write(out,settings);out.close();if(!out){Log(L"SETTINGS save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"SETTINGS replace failed");
}
void LoadSettings(){if(smoke)return;std::ifstream in(std::filesystem::path(settingsFile),std::ios::binary);settings=BrowserSettings::Read(in);}
// Download folder: the chosen one, else the system Downloads folder.
std::wstring SystemDownloadFolder(){PWSTR known=nullptr;std::wstring path;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,0,nullptr,&known))&&known)path=known;CoTaskMemFree(known);return path;}
std::wstring EffectiveDownloadFolder(){return settings.downloadFolder.empty()?SystemDownloadFolder():settings.downloadFolder;}
void SettingChanged(BrowserSettings::Change change);
// SETTINGS(downloads): folder picker for "Enregistrer dans" (modal; the drawer stays open).
void PickDownloadFolder(){
    IFileOpenDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))||!dialog){Log(L"DOWNLOADS folder picker unavailable");return;}
    FILEOPENDIALOGOPTIONS options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Enregistrer les téléchargements dans");
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
    motion.SetReducedMotion(reduced);if(reduced)chrome.lottie.Clear();
    downloadPanel.SetReducedMotion(reduced);downloadDrawer.SetReducedMotion(reduced);historyDrawer.SetReducedMotion(reduced);bookmarksDrawer.SetReducedMotion(reduced);
    SyncUpdateToastMotion();
    settingsDrawer.SetReducedMotion();
    InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());
}
// SETTINGS(data): "Effacer les données…" (modal confirmation first).
void AnnounceDataCleared();
// The exe carries no comctl32 v6 manifest, so TaskDialogIndirect is resolved at
// run time (absent from v5) and the prompt falls back to a plain MessageBox.
bool ConfirmClearData(){
    const wchar_t* title=L"Effacer les données de navigation ?";
    const wchar_t* body=L"L'historique, les cookies et données de site, le cache, les formulaires et mots de passe enregistrés de ce profil ainsi que la liste des téléchargements seront effacés. Les fichiers téléchargés restent sur le disque et les onglets ouverts restent ouverts.\n\nCette action est irréversible.";
    using TaskDialogFn=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
    HMODULE comctl=GetModuleHandleW(L"comctl32.dll");
    auto taskDialog=comctl?reinterpret_cast<TaskDialogFn>(GetProcAddress(comctl,"TaskDialogIndirect")):nullptr;
    if(taskDialog){
        TASKDIALOG_BUTTON buttons[]={{IDOK,L"Effacer"},{IDCANCEL,L"Annuler"}};
        TASKDIALOGCONFIG config{};config.cbSize=sizeof config;config.hwndParent=window;config.hInstance=instance;
        config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;config.pszWindowTitle=L"Effacer les données";
        config.pszMainIcon=TD_WARNING_ICON;config.pszMainInstruction=title;config.pszContent=body;
        config.pButtons=buttons;config.cButtons=2;config.nDefaultButton=IDCANCEL;
        int button=IDCANCEL;if(SUCCEEDED(taskDialog(&config,&button,nullptr,nullptr)))return button==IDOK;
    }
    return MessageBoxW(window,(std::wstring(title)+L"\n\n"+body).c_str(),L"Effacer les données",MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)==IDOK;
}
std::wstring HrText(HRESULT hr){wchar_t code[16];swprintf(code,16,L"0x%08lX",(unsigned long)hr);return code;}
// Local traces: reopen stack, history, site icons and finished download rows; live downloads (and their ops) stay.
void ClearLocalTraces(){
    closedTabs.clear();history.Clear();SaveHistory();ClearSiteIcons();historyDrawer.Changed();
    downloads.erase(std::remove_if(downloads.begin(),downloads.end(),[](const DL::Item& d){return d.state!=DL::State::InProgress&&d.state!=DL::State::Paused;}),downloads.end());
    for(auto it=downloadOps.begin();it!=downloadOps.end();)it=FindDownload(it->first)?std::next(it):downloadOps.erase(it);
    SaveDownloads();DownloadsChanged();
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
void ClearBrowsingData(){
    if(smoke||!ConfirmClearData())return;
    ClearLocalTraces();
    Com<ICoreWebView2Profile2> profile;
    if(!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){Log(L"SETTINGS clear data: no ICoreWebView2Profile2 (no live tab or old runtime); local traces only");AnnounceDataCleared();return;}
    StartClearEngineData(profile.p,[](HRESULT hr){if(SUCCEEDED(hr))AnnounceDataCleared();});
}
// SETTINGS(privacy): tracking prevention, password saving and form autofill on every
// tab's profile/settings (also called for each new tab); see ClearOnExitAndClose for quitting.
// Tracking prevention is profile-wide (the environment keeps EnableTrackingPrevention at
// its default TRUE: no options are passed); passwords/autofill are per-webview settings.
void ApplyPrivacySettings(){
    if(smoke)return;
    static bool trackingLogged=false,formsLogged=false;bool any=false;
    for(auto& t:pages){if(!t||t->closed||!t->view.p)continue;any=true;
        Com<ICoreWebView2Settings> base;Com<ICoreWebView2Settings4> s4;
        if(FAILED(t->view->get_Settings(&base.p))||!base.p||FAILED(base->QueryInterface(IID_ICoreWebView2Settings4,(void**)&s4.p))||!s4.p){
            if(!formsLogged){Log(L"SETTINGS ICoreWebView2Settings4 unavailable: password and autofill switches ignored");formsLogged=true;}continue;}
        HRESULT hr=s4->put_IsPasswordAutosaveEnabled(settings.passwords?TRUE:FALSE);if(SUCCEEDED(hr))hr=s4->put_IsGeneralAutofillEnabled(settings.autofill?TRUE:FALSE);
        if(FAILED(hr)&&!formsLogged){Log(L"SETTINGS password/autofill settings failed "+HrText(hr));formsLogged=true;}
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
    if(settings.sleepMinutes>0&&!halted)for(auto& t:pages)if(t!=active&&!t->closed&&!t->sleeping){want=true;break;}
    if(want==sleepTimerArmed)return;
    if(want)sleepTimerArmed=SetTimer(window,SleepTimerId,60000,nullptr)!=0;else{KillTimer(window,SleepTimerId);sleepTimerArmed=false;}
}
// Called by Select: the previous tab starts its out-of-sight clock; a sleeping tab wakes
// (put_IsVisible(TRUE) resumes it too; Resume also covers a hidden home tab).
void TabShown(const std::shared_ptr<Tab>& tab){
    auto previous=shownTab.lock();if(previous==tab)return;
    if(previous)previous->hiddenAt=GetTickCount64();shownTab=tab;
    if(tab&&tab->sleeping){tab->sleeping=false;Com<ICoreWebView2_3> v3;if(tab->view.p&&SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v3.p))&&v3.p)v3->Resume();InvalidateChrome();}
    ApplySleepSettings();
}
void SleepTabs(){
    if(halted||settings.sleepMinutes<=0){ApplySleepSettings();return;}
    const ULONGLONG now=GetTickCount64(),after=settings.sleepMinutes*60000ull;auto active=Active();
    // Downloads are not tied to a tab: any live transfer defers sleeping.
    const bool downloading=std::any_of(downloads.begin(),downloads.end(),[](const DL::Item& d){return d.state==DL::State::InProgress;});
    if(!downloading)for(auto& t:pages){
        if(t==active||t->closed||t->sleeping||t->home||!t->view.p||now-t->hiddenAt<after)continue;
        Com<ICoreWebView2_8> v8;BOOL audio=FALSE;
        if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_8,(void**)&v8.p))&&v8.p&&SUCCEEDED(v8->get_IsDocumentPlayingAudio(&audio))&&audio)continue;
        Com<ICoreWebView2_3> v3;if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v3.p))||!v3.p)continue;
        std::weak_ptr<Tab> weak=t;
        auto done=Callback<ICoreWebView2TrySuspendCompletedHandler,HRESULT,BOOL>([weak](HRESULT hr,BOOL ok)->HRESULT{
            auto tab=weak.lock();if(!tab||tab->closed||halted||FAILED(hr)||!ok)return S_OK;
            // Selected while the suspend was in flight: wake it straight back.
            if(tab==Active()){Com<ICoreWebView2_3> v;if(SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_3,(void**)&v.p))&&v.p)v->Resume();return S_OK;}
            tab->sleeping=true;Log(L"TABS sleeping "+tab->uri);InvalidateChrome();ApplySleepSettings();return S_OK;});
        HRESULT hr=v3->TrySuspend(done);done->Release();if(FAILED(hr))Log(L"TABS TrySuspend failed "+t->uri);
    }
    ApplySleepSettings();
}
// SETTINGS(tabs): settings.zoomPercent as every tab's zoom (and new tabs').
void ApplyZoom(ICoreWebView2Controller* c){if(c&&FAILED(c->put_ZoomFactor(settings.zoomPercent/100.0)))Log(L"SETTINGS put_ZoomFactor failed");}
void ApplyZoomSettings(){for(auto& t:pages)if(!t->closed)ApplyZoom(t->controller.p);}
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
    case Change::BookmarksBar:{const bool shown=chrome.bandShown;BookmarksChanged();if(chrome.bandShown!=shown)accessibility.Notify('K',chrome.bandShown?L"Barre de favoris affichée":L"Barre de favoris masquée",L"Bookmarks");Log(L"SETTINGS bookmarks bar "+std::wstring(BrowserSettings::ShortLabel(settings.bookmarksBar)));break;}
    }
}
// The toolbar button of the surface that is (still) open stays latched.
void SyncLatch(){chrome.latched=settingsDrawer.Open()?'S':(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))?'D':historyDrawer.Open()?'Y':0;InvalidateChrome();}
void ShowSettings(){
    if(!settingsDrawer.Window())return;
    CloseDownloadSurfaces();historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);chrome.latched='S';InvalidateChrome();
    settingsDrawer.Show();SyncUpdateToastReserve();
}
void ToggleSettings(){if(settingsDrawer.Open()){settingsDrawer.Hide(true,true);return;}ShowSettings();}
void CreateSettingsPanel(){
    NativeChrome::SettingsUI::Drawer::Hooks hooks;
    hooks.schedule=[](NativeChrome::Motion::Time now){ScheduleMotion(now);};
    hooks.closed=[](bool keyboard){SyncLatch();SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    hooks.changed=SettingChanged;
    hooks.pickFolder=PickDownloadFolder;
    hooks.clearData=ClearBrowsingData;
    hooks.checkUpdates=[](){CheckForUpdates();};
    hooks.version=SettingsVersion;
    hooks.downloadFolder=EffectiveDownloadFolder;
    hooks.reducedMotion=ReducedMotionPreferred;
    hooks.loadSites=LoadSitePermissions;
    hooks.setPermission=SetSitePermission;
    hooks.resetSites=ResetSitePermissions;
    // Value rows open the native menu as their list of choices.
    hooks.choose=[](const std::vector<NativeChrome::ContextMenu::Item>& items,RECT anchor,bool keyboard){return contextMenu.Track(items,{anchor.left,anchor.bottom},&anchor,keyboard,motion.ReducedMotion(),true);};
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
    HideSuggestions();CloseDownloadSurfaces();settingsDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);
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
    historyDrawer.Changed();
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
        if(!Check(hr,L"GetFavicon callback"))return S_OK;
        // Null/empty/undecodable image streams are site image absence. WIC
        // pixels are copied while the callback stream is alive; no disk/network.
        auto image=chrome.DecodeFavicon(stream);RememberSiteIcon(tab->uri,image,stream);
        if(tab->favicon.Complete(ticket,image))InvalidateChrome();
        return S_OK;
    });
    HRESULT hr=tab->faviconView->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,callback);callback->Release();Check(hr,L"GetFavicon");
}

// Network failures stay inside the WebView: its built-in error page provides
// details and retry, while the address bar, other tabs and commands remain usable.
HRESULT NavigationCompleted(const std::shared_ptr<Tab>& t,ICoreWebView2NavigationCompletedEventArgs* a){
    if(halted||!t||t->closed)return S_OK;
    BOOL ok=FALSE;a->get_IsSuccess(&ok);
    UINT64 navigationId=0;
    if(!Check(a->get_NavigationId(&navigationId),L"NavigationCompleted id"))return S_OK;
    t->loading.Finish(navigationId,NativeChrome::Motion::Now());t->refreshing=false;
    if(t==Active()){chrome.lottie.RefreshEnd();RefreshTools();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}
    if(!ok){
        COREWEBVIEW2_WEB_ERROR_STATUS error;a->get_WebErrorStatus(&error);
        if(error==COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)return S_OK;
        Log(L"NAVIGATION FAILED WebErrorStatus="+std::to_wstring(error)+L" "+t->uri);
        // A new controller starts hidden until its first navigation completes.
        // Reveal the error document too, including a failed first address.
        if(t==Active()){Check(t->controller->put_IsVisible(!t->home),L"put_IsVisible navigation error");Update();}
        return S_OK;
    }
    RequestFavicon(t);if(halted)return S_OK;
    Log(L"NAVIGATION OK "+t->uri);
    if(!t->home&&!SearchResultPage(t->uri)&&history.Record(t->uri,t->title==L"Nouvel onglet"?L"":t->title,NativeChrome::DownloadsUI::UnixNowMs())){SaveHistory();historyDrawer.Changed();}
    if(t==Active()){Check(t->controller->put_IsVisible(!t->home),L"put_IsVisible navigation");Update();}
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
        t->loading.Clear();t->refreshing=false;
        if(t==Active()){chrome.lottie.RefreshEnd();RefreshTools();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());}
    }
    // Subframe and auxiliary process failures recover within WebView2.
    return S_OK;
}

void NewTab(std::wstring uri,int index,std::wstring title){if(halted||!environment.p)return;if(pages.size()>=128){MessageBoxW(window,L"Limite de 128 onglets atteinte.",L"browser",MB_OK);return;}motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;auto t=std::make_shared<Tab>();t->uri=uri;t->home=uri.empty();t->title=std::move(title);if(index<0)index=(int)pages.size();index=std::min(index,(int)pages.size());CancelTabDrag();pages.insert(pages.begin()+index,t);Select(index);std::weak_ptr<Tab> weak=t;
    auto done=Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>([weak,uri](HRESULT hr,ICoreWebView2Controller* c)->HRESULT{
        // A tab closed before its controller arrived (e.g. reopened then closed, or the
        // window closing) may complete with E_ABORT: that is not an engine failure.
        auto t=weak.lock();if(!t||t->closed){if(c)c->Close();return S_OK;}if(!Check(hr,L"CreateController callback"))return S_OK;if(!c){Fail(L"CreateController null",E_POINTER);return S_OK;}t->controller.set(c);if(!Check(c->get_CoreWebView2(&t->view.p),L"get_CoreWebView2"))return S_OK;
        Com<ICoreWebView2Settings> settings;Com<ICoreWebView2Settings3> settings3;
        if(!Check(t->view->get_Settings(&settings.p),L"get_Settings")||
           !Check(settings->QueryInterface(IID_ICoreWebView2Settings3,(void**)&settings3.p),L"QueryInterface Settings3")||
           !Check(settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE),L"Enable browser accelerators"))return S_OK;
        EventRegistrationToken token{};
        IID faviconInterface;CLSIDFromString(L"{517B2D1D-7DAE-4A66-A4F4-10352FFB9518}",&faviconInterface);
        if(!Check(t->view->QueryInterface(faviconInterface,(void**)&t->faviconView.p),L"QueryInterface favicon"))return S_OK;
        auto faviconChanged=Callback<ICoreWebView2FaviconChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted)RequestFavicon(tab);return S_OK;});
        HRESULT faviconResult=t->faviconView->add_FaviconChanged(faviconChanged,&token);faviconChanged->Release();if(!Check(faviconResult,L"add_FaviconChanged"))return S_OK;
        auto starting=Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;tab->certificateError=false;LPWSTR uri=nullptr;if(!Check(args->get_Uri(&uri),L"NavigationStarting get_Uri"))return S_OK;UINT64 navigationId=0;if(!Check(args->get_NavigationId(&navigationId),L"NavigationStarting id")){CoTaskMemFree(uri);return S_OK;}if(uri&&wcscmp(uri,L"about:blank")!=0)tab->loading.Start(navigationId,NativeChrome::Motion::Now());else tab->loading.Clear();if(tab==Active()){RefreshTools();ScheduleMotion(NativeChrome::Motion::Now());}tab->favicon.Navigation(uri?uri:L"");CoTaskMemFree(uri);InvalidateChrome();return S_OK;});
        HRESULT startingResult=t->view->add_NavigationStarting(starting,&token);starting->Release();if(!Check(startingResult,L"add_NavigationStarting favicon"))return S_OK;
        auto source=Callback<ICoreWebView2SourceChangedEventHandler,ICoreWebView2*,ICoreWebView2SourceChangedEventArgs*>([weak](ICoreWebView2* v,ICoreWebView2SourceChangedEventArgs*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_Source(&s),L"get_Source")&&s){t->uri=s;CoTaskMemFree(s);t->home=t->uri==L"about:blank";}t->loading.Committed(NativeChrome::Motion::Now());if(t==Active()){Update();ScheduleMotion(NativeChrome::Motion::Now());}return S_OK;});
        HRESULT result=t->view->add_SourceChanged(source,&token);source->Release();if(!Check(result,L"add_SourceChanged"))return S_OK;
        auto history=Callback<ICoreWebView2HistoryChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted&&tab==Active())Update();return S_OK;});
        result=t->view->add_HistoryChanged(history,&token);history->Release();if(!Check(result,L"add_HistoryChanged"))return S_OK;
        auto title=Callback<ICoreWebView2DocumentTitleChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_DocumentTitle(&s),L"get_DocumentTitle")&&s){t->title=s;CoTaskMemFree(s);if(!t->home&&::history.SetTitle(t->uri,t->title)){SaveHistory();historyDrawer.Changed();}}Update();return S_OK;});
        result=t->view->add_DocumentTitleChanged(title,&token);title->Release();if(!Check(result,L"add_DocumentTitleChanged"))return S_OK;
        auto nav=Callback<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* a)->HRESULT{return NavigationCompleted(weak.lock(),a);});
        result=t->view->add_NavigationCompleted(nav,&token);nav->Release();if(!Check(result,L"add_NavigationCompleted"))return S_OK;
        auto popup=Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{if(halted)return S_OK;a->put_Handled(TRUE);LPWSTR uri=nullptr;if(Check(a->get_Uri(&uri),L"NewWindow get_Uri")&&uri){std::wstring s=uri;CoTaskMemFree(uri);if(s.rfind(L"https://",0)==0||s.rfind(L"http://",0)==0)NewTab(s);}return S_OK;});
        result=t->view->add_NewWindowRequested(popup,&token);popup->Release();if(!Check(result,L"add_NewWindowRequested"))return S_OK;
        auto key=Callback<ICoreWebView2AcceleratorKeyPressedEventHandler,ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs*>([](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* a)->HRESULT{if(halted)return S_OK;COREWEBVIEW2_KEY_EVENT_KIND kind;a->get_KeyEventKind(&kind);UINT vk;a->get_VirtualKey(&vk);if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN){int cmd=Key(vk);if(cmd){a->put_Handled(TRUE);PostMessageW(window,Shortcut,cmd,0);}}return S_OK;});
        result=c->add_AcceleratorKeyPressed(key,&token);key->Release();if(!Check(result,L"add_AcceleratorKeyPressed"))return S_OK;
        auto failed=Callback<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* a)->HRESULT{return ProcessFailed(weak.lock(),a);});
        result=t->view->add_ProcessFailed(failed,&token);failed->Release();if(!Check(result,L"add_ProcessFailed"))return S_OK;
        // Invalid server certificate: the engine keeps its own interstitial; the bar says "Non sécurisé".
        {Com<ICoreWebView2_14> view14;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_14,(void**)&view14.p))&&view14.p){auto cert=Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler,ICoreWebView2*,ICoreWebView2ServerCertificateErrorDetectedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ServerCertificateErrorDetectedEventArgs*)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;tab->certificateError=true;Log(L"CERTIFICATE error "+tab->uri);if(tab==Active()){BuildChrome();RefreshTools();InvalidateChrome();}return S_OK;});if(FAILED(view14->add_ServerCertificateErrorDetected(cert,&token)))Log(L"CERTIFICATE add_ServerCertificateErrorDetected failed");cert->Release();}else Log(L"CERTIFICATE ICoreWebView2_14 unavailable");}
        {Com<ICoreWebView2_4> view4;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_4,(void**)&view4.p))&&view4.p){auto download=Callback<ICoreWebView2DownloadStartingEventHandler,ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* a)->HRESULT{return DownloadStarting(a);});if(FAILED(view4->add_DownloadStarting(download,&token)))Log(L"DOWNLOADS add_DownloadStarting failed");download->Release();}else Log(L"DOWNLOADS ICoreWebView2_4 unavailable: engine flyout kept");ApplyDownloadSettings();ApplyPrivacySettings();ApplyZoom(c);ApplySleepSettings();}
        {Com<ICoreWebView2_11> view11;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_11,(void**)&view11.p))&&view11.p){auto menu=Callback<ICoreWebView2ContextMenuRequestedEventHandler,ICoreWebView2*,ICoreWebView2ContextMenuRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ContextMenuRequestedEventArgs* a)->HRESULT{return PageContextMenu(weak.lock(),a);});if(FAILED(view11->add_ContextMenuRequested(menu,&token)))Log(L"MENU add_ContextMenuRequested failed");menu->Release();}else Log(L"MENU ICoreWebView2_11 unavailable: engine menu kept");}
        Layout();if(!Check(c->put_IsVisible(FALSE),L"put_IsVisible initial"))return S_OK;Navigate(t,uri);return S_OK;
    });
    HRESULT hr=environment->CreateCoreWebView2Controller(window,done);done->Release();Check(hr,L"CreateCoreWebView2Controller");
}

void SmokeCheck(){if(!smoke||halted)return;auto t=Active();if(!t||!t->view.p){Fail(L"Smoke active view missing",E_FAIL);return;}auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([](HRESULT hr,LPCWSTR json)->HRESULT{if(!Check(hr,L"Smoke ExecuteScript callback"))return S_OK;std::wstring expected=testStage<2?L"Nouvel onglet|rgb(23, 23, 23)|":L"Example Domain";if(!json||std::wstring(json).find(expected)==std::wstring::npos){Fail(L"Smoke unexpected document title",E_FAIL);return S_OK;}Log(L"SMOKE document verified stage="+std::to_wstring(testStage)+L" title="+json);PostMessageW(window,WM_APP+3,0,0);return S_OK;});HRESULT hr=t->view->ExecuteScript(testStage<2?L"document.title + '|' + getComputedStyle(document.body).backgroundColor + '|' + document.body.textContent.trim()":L"document.title",cb);cb->Release();Check(hr,L"Smoke ExecuteScript");}
void SmokeAdvance(){if(halted)return;switch(testStage++){
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

void RefreshTools(){auto tab=Active();chrome.navigationLoading=tab&&tab->loading.IsLoading();BuildChrome();if(!tooltip)return;static std::vector<int> previous;for(int id:previous){TOOLINFOW t{sizeof(t)};t.hwnd=window;t.uId=id;SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&t);}previous.clear();for(auto&r:chrome.regions){TOOLINFOW t{sizeof(t)};t.uFlags=TTF_SUBCLASS;t.hwnd=window;t.uId=r.id;t.rect={S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};t.lpszText=const_cast<wchar_t*>(r.tip);SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);previous.push_back(r.id);}}
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
    auto place=PlaceMenu(region->rect,chrome.width,GetDpiForWindow(window));
    MapWindowPoints(window,nullptr,(POINT*)&place.control,2);
    return contextMenu.Track(items,p,&place.control,true,motion.ReducedMotion(),place.alignRight);
}
// Commands reached by Enter/Space on a focused chrome control anchor their menu to it; a click clears keyboardFocus first.
int KeyboardAnchor(){return GetFocus()==window?keyboardFocus:0;}
std::vector<CM::Item> TabMenuItems(){
    std::vector<CM::Item> items;
    // Titles are verbatim: '&' would read as an access key.
    for(int i=0;i<(int)pages.size();++i)items.push_back(CM::Command(1000+i,NativeUI::EscapeMenuText(pages[i]->title.empty()?L"Onglet sans titre":pages[i]->title),L"",true,i==current));
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
// Tab right click (Paper "Clic droit menu — poli"): acts on the tab that was clicked.
void TabContextMenu(int tabIndex,int anchor){
    if(tabIndex<0||tabIndex>=(int)pages.size())return;
    enum{NewTabItem=1,ReloadItem,CopyItem,ReopenItem,CloseItem,CloseOthersItem,PrintItem,PdfItem};
    auto tab=pages[tabIndex];const bool link=!tab->home&&!tab->uri.empty(),page=link&&tab->view.p;
    const std::vector<CM::Item> items{CM::Command(NewTabItem,L"Nouvel onglet",L"Ctrl+T"),CM::Command(ReloadItem,L"Actualiser",L"Ctrl+R",tab->view.p!=nullptr),CM::Command(CopyItem,L"Copier le lien",L"",link),CM::Separator(),
        CM::Command(PrintItem,L"Imprimer…",L"Ctrl+P",page),CM::Command(PdfItem,L"Enregistrer en PDF",L"",page),CM::Separator(),
        CM::Command(ReopenItem,L"Rouvrir l'onglet fermé",L"Ctrl+Maj+T",!closedTabs.empty()&&pages.size()<128),CM::Separator(),
        CM::Command(CloseItem,L"Fermer l'onglet",L"Ctrl+W"),CM::Command(CloseOthersItem,L"Fermer les autres onglets",L"",pages.size()>1)};
    switch(ShowMenu(items,anchor)){
    case NewTabItem:Command('T');break;
    case ReloadItem:if(tabIndex==current)Command('R');else if(tab->view.p)Check(tab->view->Reload(),L"Reload");break;
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
    const std::vector<CM::Item> items{CM::Command(UndoItem,L"Annuler",L"Ctrl+Z",SendMessageW(address,EM_CANUNDO,0,0)!=0),CM::Separator(),
        CM::Command(CutItem,L"Couper",L"Ctrl+X",selection),CM::Command(CopyItem,L"Copier",L"Ctrl+C",selection),CM::Command(PasteItem,L"Coller",L"Ctrl+V",IsClipboardFormatAvailable(CF_UNICODETEXT)!=0),CM::Command(DeleteItem,L"Supprimer",L"Suppr",selection),CM::Separator(),
        CM::Command(SelectAllItem,L"Tout sélectionner",L"Ctrl+A",length>0&&!(from==0&&(int)to==length))};
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
        if(name==L"print")out.push_back(CM::Command(PagePdfItem,L"Enregistrer en PDF")); // beside the engine's own print
    }
    while(!out.empty()&&out.back().separator)out.pop_back();
}
HRESULT PageContextMenu(const std::shared_ptr<Tab>& tab,ICoreWebView2ContextMenuRequestedEventArgs* args){
    if(halted||!tab||!args||pendingPageMenu||contextMenu.Open())return S_OK;
    auto pending=std::make_shared<PendingPageMenu>();
    Com<ICoreWebView2ContextMenuItemCollection> list;if(FAILED(args->get_MenuItems(&list.p))||!list.p)return S_OK;
    ReadPageMenu(list.p,pending->items,pending->commands);if(pending->items.empty())return S_OK; // nothing to show: the engine keeps its own menu
    // No engine print item (e.g. on a link or selection): our print group goes before the last group.
    auto& items=pending->items;std::function<bool(const std::vector<CM::Item>&)> has=[&](const std::vector<CM::Item>& v){for(auto& m:v)if(m.id==PagePdfItem||has(m.children))return true;return false;};
    if(!tab->home&&!has(items)){
        auto last=std::find_if(items.rbegin(),items.rend(),[](const CM::Item& m){return m.separator;});auto at=last==items.rend()?items.end():std::prev(last.base());
        items.insert(at,{CM::Separator(),CM::Command(PagePrintItem,L"Imprimer…"),CM::Command(PagePdfItem,L"Enregistrer en PDF")});
    }
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
    if(auto tab=pending->tab.lock();tab&&!tab->closed){if(choice==PagePrintItem)PrintPage(tab);else if(choice==PagePdfItem)SavePageAsPdf(tab);}
}

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
int popoverReturn=0; bool bookmarksDismissClick=false;
struct BandDrag { int index=-1; BM::Id id=0; POINT start{}; float grab=0; bool active=false; BM::Id parent=0; size_t from=0; } bandDrag;
// Undo: each entry replays its steps backwards (16 entries, this session only).
struct BookmarkStep { enum Kind { Created, Removed, Moved, Renamed, Readdressed } kind; BM::Id id=0,parent=0; size_t index=0; std::wstring text; BM::Removed removed; };
struct BookmarkUndo { std::vector<BookmarkStep> steps; std::wstring done; };
std::vector<BookmarkUndo> bookmarkUndo;
std::wstring BookmarksFile(){return dataDir+L"\\bookmarks";}
void AnnounceBookmark(const std::wstring& text){Log(L"BOOKMARKS "+text);accessibility.Notify('K',text.c_str(),L"Bookmarks");}
void SaveBookmarks(){
    if(smoke)return;
    const auto path=std::filesystem::path(BookmarksFile()),temp=std::filesystem::path(BookmarksFile()+L".tmp");
    bool ok=true;{std::ofstream out(temp,std::ios::binary|std::ios::trunc);bookmarks.Write(out);out.close();ok=(bool)out;}
    if(ok&&!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))ok=false;
    if(ok){if(bookmarksSaveFailed){bookmarksSaveFailed=false;Log(L"BOOKMARKS saved again");}return;}
    // The data stays in memory; a sticky toast offers to try again.
    Log(L"BOOKMARKS save failed");bookmarksSaveFailed=true;
    if(bookmarkToast.Window())bookmarkToast.Show(L"Favoris non enregistrés sur le disque",[]{SaveBookmarks();},motion.ReducedMotion(),L"Réessayer",true);
}
std::wstring TimeStamp(){SYSTEMTIME t;GetLocalTime(&t);wchar_t s[32];swprintf(s,32,L"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return s;}
void LoadBookmarks(){
    if(smoke)return;
    namespace fs=std::filesystem;const fs::path path(BookmarksFile()),backup(BookmarksFile()+L".bak");std::error_code ec;
    if(!fs::exists(path,ec)){
        // No file: first run, or a lost one; a clean backup wins over an empty start.
        if(fs::exists(backup,ec)){std::ifstream in(backup,std::ios::binary);bool damaged=false;auto restored=BM::Store::Read(in,&damaged);if(!damaged&&restored.Size()){bookmarks=std::move(restored);Log(L"BOOKMARKS restored from bookmarks.bak");SaveBookmarks();}}
        return;
    }
    bool damaged=false;{std::ifstream in(path,std::ios::binary);bookmarks=BM::Store::Read(in,&damaged);}
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
std::wstring FolderSummary(BM::Id id){const auto* n=bookmarks.Find(id);const size_t items=n?n->children.size():0;return L"Dossier, "+std::to_wstring(items)+(items>1?L" éléments":L" élément");}
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
}
void PushUndo(std::vector<BookmarkStep> steps,std::wstring done){if(steps.empty())return;if(bookmarkUndo.size()==16)bookmarkUndo.erase(bookmarkUndo.begin());bookmarkUndo.push_back({std::move(steps),std::move(done)});}
std::wstring UniqueFolderName(BM::Id parent){
    std::wstring name=L"Nouveau dossier";int n=1;const auto* p=bookmarks.Find(parent);if(!p)return name;
    for(bool clash=true;clash&&n<1000;){clash=false;for(BM::Id c:p->children){auto* x=bookmarks.Find(c);if(x&&x->kind==BM::Kind::Folder&&x->title==name){clash=true;name=L"Nouveau dossier "+std::to_wstring(++n);break;}}}
    return name;
}
BM::Id NewFolder(BM::Id parent,size_t index=BM::End){
    const BM::Id id=bookmarks.AddFolder(parent,index,UniqueFolderName(parent),NativeChrome::DownloadsUI::UnixNowMs());
    if(!id){AnnounceBookmark(L"Trop de niveaux de dossiers");return 0;}
    PushUndo({{BookmarkStep::Created,id}},L"Dossier créé");SaveBookmarks();BookmarksChanged();AnnounceBookmark(L"Dossier « "+bookmarks.Find(id)->title+L" » créé");return id;
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
    PushUndo({std::move(step)},folder?L"Dossier restauré":L"Favori restauré");SaveBookmarks();BookmarksChanged();
    const std::wstring text=folder?L"Dossier « "+title+L" » supprimé"+(links?L" ("+std::to_wstring(links)+(links>1?L" favoris)":L" favori)"):L""):L"Favori supprimé";
    ShowBookmarkToast(text);AnnounceBookmark(text+L". Ctrl+Z pour annuler");
}
// Replays the newest entry backwards. Restoring a subtree whose folder is gone lands in "Autres favoris".
void UndoBookmark(){
    if(bookmarkUndo.empty()){AnnounceBookmark(L"Rien à annuler");return;}
    auto entry=std::move(bookmarkUndo.back());bookmarkUndo.pop_back();std::wstring note=entry.done;
    for(auto it=entry.steps.rbegin();it!=entry.steps.rend();++it){auto& s=*it;
        switch(s.kind){
        case BookmarkStep::Created:bookmarks.Remove(s.id);break;
        case BookmarkStep::Removed:if(bookmarks.Restore(s.removed)){const auto* n=bookmarks.Find(s.id);if(n&&n->parent!=s.removed.parent)note+=L" dans Autres favoris";}else note=L"Impossible d'annuler";break;
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
            out.emplace_back(id,depth,n->title);for(auto c=n->children.rbegin();c!=n->children.rend();++c)stack.push_back({*c,depth+1});}}
    return out;
}
RECT ClientScreen(){RECT r{};GetClientRect(window,&r);MapWindowPoints(window,nullptr,(POINT*)&r,2);return r;}
RECT RegionScreen(int id){for(auto& r:chrome.regions)if(r.id==id){RECT p{S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};MapWindowPoints(window,nullptr,(POINT*)&p,2);return p;}return {};}
// The popover under `anchor` (screen); `returnTo`: chrome region keeping the keyboard after Escape (0 = page).
void EditBookmark(BM::Id id,RECT anchor,bool alignRight,bool keyboard,bool added=false,int returnTo=0){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return;
    NativeChrome::BookmarkPopover::Request r;r.id=id;r.folder=n->kind==BM::Kind::Folder;r.added=added;r.title=n->title;r.url=n->url;r.parent=n->parent;
    popoverReturn=returnTo;bookmarksDrawer.holding_=bookmarksDrawer.Visible();
    bookmarkPopover.Show(r,anchor,ClientScreen(),alignRight,keyboard,motion.ReducedMotion());
}
// Ctrl+D / the star: save at once in the last folder used, then the popover; an
// already saved page opens its most recent bookmark instead of a second one.
void BookmarkStar(bool keyboard){
    auto t=Active();if(!t||t->home||!BM::Admissible(t->uri))return;
    HideSuggestions();
    const auto matches=bookmarks.Matching(t->uri);const RECT star=RegionScreen('K');
    if(!matches.empty()){EditBookmark(matches.front(),star,true,keyboard,false,keyboard?'K':0);return;}
    if(!bookmarks.Find(lastFolder)||bookmarks.Find(lastFolder)->kind!=BM::Kind::Folder)lastFolder=BM::BarRoot;
    std::wstring title=t->title==L"Nouvel onglet"?L"":t->title;
    const BM::Id id=bookmarks.Add(lastFolder,BM::End,title,t->uri,NativeChrome::DownloadsUI::UnixNowMs());
    if(!id){AnnounceBookmark(L"Impossible d'ajouter ce favori");return;}
    PushUndo({{BookmarkStep::Created,id}},L"Ajout annulé");SaveBookmarks();BookmarksChanged();
    AnnounceBookmark(L"Favori ajouté à "+bookmarks.Find(lastFolder)->title);
    EditBookmark(id,RegionScreen('K'),true,keyboard,true,keyboard?'K':0);
}
void CommitBookmark(unsigned id,const std::wstring& title,const std::wstring& url,unsigned parent){
    const auto* n=bookmarks.Find(id);if(!n)return;std::vector<BookmarkStep> steps;
    const std::wstring oldTitle=n->title,oldUrl=n->url;const BM::Id oldParent=n->parent;const size_t oldIndex=bookmarks.IndexOf(id);
    if(title!=oldTitle&&bookmarks.Rename(id,title))steps.push_back({BookmarkStep::Renamed,id,0,0,oldTitle});
    if(!url.empty()&&url!=oldUrl&&bookmarks.SetUrl(id,url))steps.push_back({BookmarkStep::Readdressed,id,0,0,oldUrl});
    if(parent&&parent!=oldParent){if(bookmarks.Move(id,parent,BM::End)){steps.push_back({BookmarkStep::Moved,id,oldParent,oldIndex});lastFolder=parent;}else AnnounceBookmark(L"Trop de niveaux de dossiers");}
    else if(parent)lastFolder=parent;
    if(steps.empty())return;
    PushUndo(std::move(steps),L"Modification annulée");SaveBookmarks();BookmarksChanged();AnnounceBookmark(L"Favori enregistré");
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
void OpenAllIn(BM::Id folder){
    std::vector<std::wstring> urls;for(BM::Id id:bookmarks.Subtree(folder)){auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Link)urls.push_back(n->url);}
    if(urls.empty())return;
    if(urls.size()>20&&MessageBoxW(window,(L"Ouvrir "+std::to_wstring(urls.size())+L" onglets ?").c_str(),L"Favoris",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
    const int previous=current;int at=current;for(auto& u:urls){if(pages.size()>=128)break;NewTab(u,++at);}
    if(previous>=0&&previous<(int)pages.size())Select(previous);
    AnnounceBookmark(std::to_wstring(urls.size())+L" favoris ouverts dans de nouveaux onglets");
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
    if(links){auto all=CM::Command(MenuId(MenuOpenAll,folder),L"Ouvrir les "+std::to_wstring(links)+(links>1?L" favoris":L" favori"));all.icon=L"open-out";items.push_back(all);items.push_back(CM::Separator());}
    for(BM::Id id:f->children){const auto* n=bookmarks.Find(id);if(!n)continue;
        if(n->kind==BM::Kind::Folder){CM::Item sub;sub.id=MenuId(MenuOpen,id);sub.label=NativeUI::EscapeMenuText(n->title.empty()?L"Dossier sans nom":n->title);sub.icon=L"folder-closed";sub.help=FolderSummary(id);
            if(depth<(int)BM::Store::MaxDepth)sub.children=BookmarkFolderItems(id,depth+1);if(sub.children.empty())sub.children.push_back(CM::Command(0,L"(Vide)",L"",false));items.push_back(sub);}
        else{auto item=CM::Command(MenuId(MenuOpen,id),NativeUI::EscapeMenuText(BM::DisplayTitle(*n)));item.image=SiteIcon(AddressBar::SiteOf(n->url));item.icon=L"favicon";item.help=ReadableUrl(n->url);items.push_back(item);}}
    if(f->children.empty()){items.push_back(CM::Command(0,L"(Vide)",L"",false));}
    auto t=Active();if(f->children.empty()&&t&&!t->home&&BM::Admissible(t->uri)){auto add=CM::Command(MenuId(MenuAddHere,folder),L"Ajouter la page ici");add.icon=L"star";items.push_back(add);}
    return items;
}
void BookmarkContextMenu(BM::Id id,POINT at,const RECT* anchor,bool keyboard);
// Runs a bookmark menu (folder, overflow) under `region`; handles the gesture it returns.
void RunBookmarkMenu(const std::vector<CM::Item>& items,int region,bool keyboard){
    RECT anchor=RegionScreen(region);chrome.bandOpen=region;InvalidateChrome();UpdateWindow(window);
    if(accessibility.Listening())accessibility.Raise(AccessibleBandItemKey(region),UIA_ExpandCollapseExpandCollapseStatePropertyId);
    POINT p{anchor.left,anchor.bottom};
    const auto choice=contextMenu.TrackChoice(items,p,&anchor,keyboard,motion.ReducedMotion(),false,[](int v){if(MenuKindOf(v)==MenuOpen){auto* n=bookmarks.Find(MenuBookmark(v));if(n&&n->kind==BM::Kind::Link)OpenBookmarkUrl(n->url,1);}});
    chrome.bandOpen=0;InvalidateChrome();
    if(keyboard&&region){SetFocus(window);keyboardFocus=region;keyboardModality=true;InvalidateChrome();}
    if(!choice.id)return;
    const BM::Id id=MenuBookmark(choice.id);
    if(choice.context){POINT c{};GetCursorPos(&c);BookmarkContextMenu(id,c,nullptr,false);return;}
    switch(MenuKindOf(choice.id)){
    case MenuOpen:{auto* n=bookmarks.Find(id);if(n&&n->kind==BM::Kind::Link)OpenBookmarkUrl(n->url,choice.shift?2:choice.ctrl?1:0);break;}
    case MenuOpenAll:OpenAllIn(id);break;
    case MenuAddHere:{lastFolder=id;BookmarkStar(keyboard);break;}
    case MenuManage:ToggleBookmarksDrawer(keyboard);break;
    default:break;
    }
}
void OverflowMenu(bool keyboard){
    std::vector<CM::Item> items;const auto& bar=bookmarks.Root(BM::BarRoot).children;
    for(size_t i=(size_t)chrome.band.visible;i<bar.size();++i){const auto* n=bookmarks.Find(bar[i]);if(!n)continue;
        if(n->kind==BM::Kind::Folder){CM::Item sub;sub.id=MenuId(MenuOpen,n->id);sub.label=NativeUI::EscapeMenuText(n->title);sub.icon=L"folder-closed";sub.help=FolderSummary(n->id);sub.children=BookmarkFolderItems(n->id,1);items.push_back(sub);}
        else{auto item=CM::Command(MenuId(MenuOpen,n->id),NativeUI::EscapeMenuText(BM::DisplayTitle(*n)));item.image=SiteIcon(AddressBar::SiteOf(n->url));item.icon=L"favicon";item.help=ReadableUrl(n->url);items.push_back(item);}}
    if(!bookmarks.Root(BM::OtherRoot).children.empty()){if(!items.empty())items.push_back(CM::Separator());CM::Item other;other.id=MenuId(MenuOpen,BM::OtherRoot);other.label=L"Autres favoris";other.icon=L"folder-closed";other.children=BookmarkFolderItems(BM::OtherRoot,1);items.push_back(other);}
    if(!items.empty())items.push_back(CM::Separator());
    auto manage=CM::Command(MenuId(MenuManage,0),L"Gérer les favoris",L"Ctrl+Maj+O");manage.icon=L"star";items.push_back(manage);
    RunBookmarkMenu(items,'O',keyboard);
}
void FolderMenu(int index,bool keyboard){
    if(index<0||index>=(int)chrome.bandItems.size())return;
    RunBookmarkMenu(BookmarkFolderItems(chrome.bandItems[index].id),NativeChrome::BookmarkBase+index,keyboard);
}
std::wstring BandToggleLabel(){return BandShown()?L"Masquer la barre de favoris":L"Afficher la barre de favoris";}
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
    PushUndo({{BookmarkStep::Created,copy}},L"Copie annulée");SaveBookmarks();BookmarksChanged();AnnounceBookmark(L"Copie insérée après l'original");return copy;
}
void MoveBookmarkTo(BM::Id id,BM::Id parent){
    const auto* n=bookmarks.Find(id);if(!n)return;const BM::Id oldParent=n->parent;const size_t oldIndex=bookmarks.IndexOf(id);
    if(!bookmarks.Move(id,parent,BM::End)){AnnounceBookmark(L"Impossible de déplacer ici");return;}
    PushUndo({{BookmarkStep::Moved,id,oldParent,oldIndex}},L"Déplacement annulé");SaveBookmarks();BookmarksChanged();AnnounceBookmark(L"Déplacé dans "+bookmarks.Find(parent)->title);
}
// One step within its folder (Ctrl+Maj+←/→ on the band, Alt+↑/↓ in the drawer).
void ShiftBookmark(BM::Id id,int delta){
    const auto* n=bookmarks.Find(id);if(!n)return;const BM::Id parent=n->parent;const size_t index=bookmarks.IndexOf(id);
    const size_t count=bookmarks.Find(parent)->children.size();const long long to=(long long)index+delta;if(to<0||to>=(long long)count)return;
    if(!bookmarks.Move(id,parent,(size_t)to))return;
    PushUndo({{BookmarkStep::Moved,id,parent,index}},L"Déplacement annulé");SaveBookmarks();BookmarksChanged();
    AnnounceBookmark(L"Déplacé en position "+std::to_wstring(to+1)+L" sur "+std::to_wstring(count));
}
std::vector<CM::Item> MoveToItems(BM::Id id){
    std::vector<CM::Item> items;const auto* n=bookmarks.Find(id);
    for(auto& [folder,depth,name]:FolderChoices(n&&n->kind==BM::Kind::Folder?id:0)){auto item=CM::Command(MenuId(MenuMoveTo,folder),std::wstring((size_t)depth*2,L' ')+NativeUI::EscapeMenuText(name),L"",n&&folder!=n->parent,n&&folder==n->parent);item.icon=L"folder-closed";items.push_back(item);}
    return items;
}
// A band item's (or menu row's, or drawer row's) own menu, in the order fixed in Paper (c-s207a4).
void BookmarkContextMenu(BM::Id id,POINT at,const RECT* anchor,bool keyboard){
    const auto* n=bookmarks.Find(id);if(!n||BM::Store::IsRoot(id))return;const bool folder=n->kind==BM::Kind::Folder;
    enum{OpenTab=1,OpenAll,Edit,Duplicate,Copy,Delete,NewFolderItem,AddHere,ToggleBar};
    std::vector<CM::Item> items;
    if(folder){const size_t links=bookmarks.CountLinks(id);auto all=CM::Command(OpenAll,L"Ouvrir les "+std::to_wstring(links)+(links>1?L" favoris":L" favori"),L"",links>0);items.push_back(all);}
    else items.push_back(CM::Command(OpenTab,L"Ouvrir dans un nouvel onglet"));
    items.push_back(CM::Separator());
    items.push_back(CM::Command(Edit,folder?L"Renommer…":L"Modifier…",L"F2"));
    items.push_back(CM::Command(Duplicate,L"Dupliquer"));
    if(!folder)items.push_back(CM::Command(Copy,L"Copier le lien"));
    items.push_back(CM::Separator());
    auto del=CM::Command(Delete,L"Supprimer",L"Suppr");del.danger=true;items.push_back(del);
    items.push_back(CM::Separator());
    items.push_back(CM::Command(NewFolderItem,L"Nouveau dossier…"));
    if(folder){auto t=Active();items.push_back(CM::Command(AddHere,L"Ajouter la page ici",L"",t&&!t->home&&BM::Admissible(t->uri)));}
    CM::Item move;move.label=L"Déplacer vers";move.children=MoveToItems(id);if(move.children.empty())move.enabled=false;items.push_back(move);
    items.push_back(CM::Command(ToggleBar,BandToggleLabel(),L"Ctrl+Maj+B"));
    const int region=[&]{for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==id)return NativeChrome::BookmarkBase+i;return 0;}();
    if(region){chrome.bandOpen=region;InvalidateChrome();UpdateWindow(window);}
    bookmarksDrawer.holding_=bookmarksDrawer.Visible();
    const int cmd=contextMenu.Track(items,at,anchor,keyboard,motion.ReducedMotion());
    bookmarksDrawer.holding_=false;chrome.bandOpen=0;InvalidateChrome();
    if(!bookmarks.Find(id))return;
    const RECT edit=region?RegionScreen(region):anchor?*anchor:RECT{at.x,at.y,at.x,at.y};
    if(MenuKindOf(cmd)==MenuMoveTo){MoveBookmarkTo(id,MenuBookmark(cmd));return;}
    switch(cmd){
    case OpenTab:OpenBookmarkUrl(n->url,2);break;
    case OpenAll:OpenAllIn(id);break;
    case Edit:EditBookmark(id,edit,false,keyboard,false,keyboard?region:0);break;
    case Duplicate:DuplicateBookmark(id);break;
    case Copy:if(CopyToClipboard(n->url))AnnounceBookmark(L"Lien copié");break;
    case Delete:RemoveBookmark(id);break;
    case NewFolderItem:{const BM::Id parent=folder?id:n->parent;const size_t at2=folder?BM::End:bookmarks.IndexOf(id)+1;if(BM::Id made=NewFolder(parent,at2))EditBookmark(made,edit,false,keyboard,false,keyboard?region:0);break;}
    case AddHere:lastFolder=id;BookmarkStar(keyboard);break;
    case ToggleBar:ToggleBookmarkBar();break;
    default:break;
    }
}
// Right click on the band outside an item.
void BandContextMenu(POINT at){
    enum{AddPage=1,NewFolderItem,ToggleBar};auto t=Active();
    const std::vector<CM::Item> items{CM::Command(AddPage,L"Ajouter la page",L"Ctrl+D",t&&!t->home&&BM::Admissible(t->uri)),CM::Command(NewFolderItem,L"Nouveau dossier…"),CM::Separator(),CM::Command(ToggleBar,BandToggleLabel(),L"Ctrl+Maj+B")};
    switch(contextMenu.Track(items,at,nullptr,false,motion.ReducedMotion())){
    case AddPage:lastFolder=BM::BarRoot;BookmarkStar(false);break;
    case NewFolderItem:if(BM::Id made=NewFolder(BM::BarRoot)){RECT r=RegionScreen(NativeChrome::BookmarkBase+(int)bookmarks.IndexOf(made));if(r.right<=r.left)r={at.x,at.y,at.x,at.y};EditBookmark(made,r,false,false);}break;
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
    HideSuggestions();CloseDownloadSurfaces();settingsDrawer.Hide(false,true);historyDrawer.Hide(false,true);
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
    d.edit=[](BM::Id id,RECT r){EditBookmark(id,r,false,true);};
    d.move=[](BM::Id id,int delta){ShiftBookmark(id,delta);bookmarksDrawer.FocusBookmark(id);};
    d.context=[](BM::Id id,POINT at,bool keyboard){BookmarkContextMenu(id,at,nullptr,keyboard);if(bookmarksDrawer.Visible()&&!bookmarkPopover.Visible()){SetForegroundWindow(bookmarksDrawer.Window());SetFocus(bookmarksDrawer.Window());}};
    d.closed=[](bool keyboard){SyncUpdateToastReserve();if(keyboard&&!halted)FocusPage();};
    d.icon=[](const std::wstring& site){return SiteIcon(site);};
    d.announce=AnnounceBookmark;
    if(!bookmarksDrawer.Create(instance,window,base,&bookmarks,std::move(d)))Log(L"BOOKMARKS drawer unavailable");
}
// Band pointer: press records a drag candidate; past the system threshold the item
// follows the pointer and the others reorder live; over a folder's middle it drops inside.
void BandDragMove(POINT point){
    if(bandDrag.index<0||halted)return;
    if(!bandDrag.active){if(std::abs(point.x-bandDrag.start.x)<=GetSystemMetrics(SM_CXDRAG)&&std::abs(point.y-bandDrag.start.y)<=GetSystemMetrics(SM_CYDRAG))return;bandDrag.active=true;Log(L"BOOKMARKS drag start");}
    const float x=point.x*96.f/GetDpiForWindow(window);
    const auto& bar=bookmarks.Root(BM::BarRoot).children;int index=(int)(std::find(bar.begin(),bar.end(),bandDrag.id)-bar.begin());if(index>=(int)bar.size()){bandDrag={};return;}
    int into=-1,slot=index;
    for(int i=0;i<chrome.band.visible;++i){const auto r=chrome.BandRect(i);if(x<r.left||x>=r.right)continue;
        const bool folder=chrome.bandItems[i].folder&&i!=index;const float w=r.right-r.left;
        if(folder&&x>r.left+w*.25f&&x<r.right-w*.25f&&!bookmarks.IsAncestor(bandDrag.id,chrome.bandItems[i].id))into=i;else slot=i;break;}
    if(x<BookmarksBar::Layout::padding)slot=0;
    if(into<0&&slot!=index&&slot<chrome.band.visible){bookmarks.Move(bandDrag.id,BM::BarRoot,(size_t)slot);index=slot;SyncBookmarkChrome();BuildChrome();}
    chrome.bandDrag=index;chrome.bandDropInto=into;
    if(index<chrome.band.visible)chrome.bandDragOffset=x-bandDrag.grab-chrome.BandRect(index).left;
    InvalidateChrome();
}
void BandDragEnd(bool cancel){
    if(bandDrag.index<0)return;const auto drag=bandDrag;const int into=chrome.bandDropInto;
    bandDrag={};chrome.bandDrag=-1;chrome.bandDropInto=-1;chrome.bandDragOffset=0;
    if(!drag.active){InvalidateChrome();return;}
    if(cancel){bookmarks.Move(drag.id,drag.parent,drag.from);BookmarksChanged();return;}
    if(into>=0&&into<(int)chrome.bandItems.size()){const BM::Id folder=chrome.bandItems[into].id;
        if(!bookmarks.Move(drag.id,folder,BM::End)){bookmarks.Move(drag.id,drag.parent,drag.from);AnnounceBookmark(L"Trop de niveaux de dossiers");BookmarksChanged();return;}
        PushUndo({{BookmarkStep::Moved,drag.id,drag.parent,drag.from}},L"Déplacement annulé");SaveBookmarks();BookmarksChanged();AnnounceBookmark(L"Déplacé dans "+bookmarks.Find(folder)->title);return;}
    const size_t now=bookmarks.IndexOf(drag.id);
    if(now!=drag.from){PushUndo({{BookmarkStep::Moved,drag.id,drag.parent,drag.from}},L"Déplacement annulé");SaveBookmarks();AnnounceBookmark(L"Déplacé en position "+std::to_wstring(now+1)+L" sur "+std::to_wstring(bookmarks.Root(BM::BarRoot).children.size()));}
    BookmarksChanged();
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
    case VK_F2:if(index>=0){EditBookmark(chrome.bandItems[index].id,RegionScreen(focus),false,true,false,focus);return true;}return false;
    case VK_DELETE:if(index>=0){RemoveBookmark(chrome.bandItems[index].id);if(chrome.band.visible>0)rove(std::min(at,chrome.band.visible-1));else ToggleChromeFocus();return true;}return false;
    case VK_ESCAPE:FocusPage();return true;
    case VK_RETURN:if(index>=0){keyboardModality=true;BandActivate(index,ctrl,shift,true);return true;}return false;
    default:return false;
    }
}
// Smoke run (no file read or written): the whole bookmark flow in memory, through the
// same functions the pointer and the keyboard use.
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
bool BookmarkPopoverFocus(){HWND f=GetFocus();return bookmarkPopover.Visible()&&f&&(f==bookmarkPopover.Window()||IsChild(bookmarkPopover.Window(),f));}
void ExtraCommand(int cmd){auto t=Active();if(NativeChrome::IsBookmark(cmd)){BandActivate(NativeChrome::BookmarkIndex(cmd),false,false,KeyboardAnchor()!=0||keyboardModality);return;}switch(cmd){
case 'K':BookmarkStar(KeyboardAnchor()!=0||keyboardModality);break;
case 'E':lastFolder=BM::BarRoot;BookmarkStar(KeyboardAnchor()!=0||keyboardModality);break;
case 'O':OverflowMenu(KeyboardAnchor()!=0);break;
case BookmarkBarCommand:ToggleBookmarkBar();break;
case BookmarksDrawerCommand:if(bookmarksDismissClick){bookmarksDismissClick=false;break;}ToggleBookmarksDrawer(keyboardModality);break;
case BookmarkUndoCommand:UndoBookmark();break;
case 'I':FocusAddress();break; // the security indicator opens the address
case 900:TabMenu(KeyboardAnchor());break;
case 'Y':if(historyDismissClick){historyDismissClick=false;break;}ToggleHistory(KeyboardAnchor()!=0||keyboardModality);break;
case PrintActive:PrintPage(t);break;
case CheckUpdatesCommand:CheckForUpdates();break;
case 'm':ShowWindow(window,SW_MINIMIZE);break;
case 'M':ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);break;
case 'X':PostMessageW(window,WM_CLOSE,0,0);break;
case 'C':if(t&&!t->home&&!t->uri.empty()&&OpenClipboard(window)){auto bytes=(t->uri.size()+1)*sizeof(wchar_t);HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(mem){void* data=GlobalLock(mem);if(data){memcpy(data,t->uri.c_str(),bytes);GlobalUnlock(mem);EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,mem)){auto now=NativeChrome::Motion::Now();motion.CopySucceeded(now);chrome.lottie.CopySucceeded(now);InvalidateChrome();ScheduleMotion(now);AnnounceCopied();}else GlobalFree(mem);}else GlobalFree(mem);}CloseClipboard();}break;
case 'D':if(downloadDismissClick){downloadDismissClick=false;break;}ToggleDownloads();break;
case 'P':MessageBoxW(window,L"La navigation privée n'est pas disponible dans cette version.\n\nLes onglets actuels utilisent le profil normal local (cookies et cache persistants). Aucun onglet privé n'a été ouvert.",L"Navigation privée — indisponible",MB_OK|MB_ICONINFORMATION);break;
case 'S':if(settingsDismissClick){settingsDismissClick=false;break;}ToggleSettings();break;
case 'A':MessageBoxW(window,(AboutTitle()+L"\nInterface native issue du canvas Paper.\nRecherche : DuckDuckGo.\nProfil normal local, sans synchronisation.\nCtrl+L / Alt+D : adresse - F6 : page / barre\nCtrl+H : historique\nCtrl+T/W : ouvrir / fermer - Ctrl+Maj+T : rouvrir\nCtrl+Tab : onglet suivant\nCtrl+1...8 : onglet - Ctrl+9 : dernier\nCtrl+Maj+PgPr\u00e9c/PgSuiv : d\u00e9placer l’onglet\nCtrl+F : rechercher - Alt+\u2190/\u2192 : historique\n\nLe menu regroupe les actions disponibles ; aucun réglage système n'est modifié.").c_str(),L"À propos",MB_OK);break;
}}
#ifndef BROWSER_UPDATES
void SyncUpdateToastReserve(){} void CheckForUpdates(){} void SetUpdateToastTop(float){} float UpdateToastHeight(){return 0;} bool UpdateCheckAvailable(){return false;} bool FocusUpdateToast(){return false;} bool UpdateToastHasFocus(){return false;}
NativeChrome::SettingsUI::Drawer::Version SettingsVersion(){return {AboutTitle(),false,false};}
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
void ShowUpdateView(){if(updatesStarted)updateToast.SetView(updates.View());settingsDrawer.Changed();}
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
        closingForInstall=true;Log(L"UPDATE install handoff: saving session and closing");PostMessageW(window,WM_CLOSE,0,0);
    }
}
std::wstring ExePath(){std::wstring exe(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,exe.data(),(DWORD)exe.size());exe.resize(n<exe.size()?n:0);return exe;}
UpdateCore::Config UpdateConfig(){
    UpdateCore::Config config;config.currentVersion=BROWSER_UPDATE_VERSION;config.repository=BROWSER_UPDATE_REPO_WIDE;config.exePath=ExePath();
    config.notifyWindow=window;config.notifyMessage=UpdateMessage;config.enabled=!smoke&&!config.exePath.empty();
    return config;
}
// Fixtures pass an isolated config (temp install root, mocked transport).
void StartUpdates(UpdateCore::Config config=UpdateConfig()){
    if(!updateToast.Init(window,base+L"\\assets",UpdateAction))Log(L"UPDATE toast unavailable");
    updateToast.SetFocusReleaseHandler([]{FocusPage();});
    updatesStarted=updates.Start(config);
    Log(updatesStarted?L"UPDATE engine started "+config.currentVersion:L"UPDATE engine disabled");
    ShowUpdateView();ArmUpdateTimer();
}
bool UpdateCheckAvailable(){return updatesStarted&&!closingForInstall;}
void CheckForUpdates(){if(UpdateCheckAvailable()){updates.CheckNow();ShowUpdateView();}}
// Settings footer: "browser <v> · <statut>", kept within ~200 DIP at 14 px (hence the terse wording).
// A manual check from the drawer goes through the engine like the menu command, so the toast
// surfaces Available/Ready/Error by itself; "À jour" only lives here (the toast stays hidden).
NativeChrome::SettingsUI::Drawer::Version SettingsVersion(){
    using P=BrowserUpdate::Phase;using L=UpdateCore::Engine::LastCheck;
    if(!updatesStarted||!updates.Enabled())return {AboutTitle(),false,false};
    const auto v=updates.View();std::wstring status;
    switch(v.phase){
    case P::Hidden:{const auto last=updates.LastCheckResult();status=last==L::UpToDate?L"À jour":last==L::Available?L"Disponible":last==L::Failed?L"Hors ligne":L"";break;}
    case P::Checking:status=L"Recherche…";break;
    case P::Available:status=L"Disponible";break;
    case P::Downloading:status=v.total?L"Téléch. "+std::to_wstring(std::min<std::uint64_t>(100,v.received*100/v.total))+L" %":std::wstring(L"Téléchargement…");break;
    case P::Ready:status=L"À installer";break;
    case P::Installing:status=L"Installation…";break;
    case P::Installed:status=L"Installée";break;
    case P::Error:status=v.version.empty()?L"Hors ligne":L"Échec";break; // empty version = the check itself failed
    }
    const bool busy=v.phase==P::Checking||v.phase==P::Downloading||v.phase==P::Installing;
    return {status.empty()?AboutTitle():AboutTitle()+L" · "+status,v.phase==P::Checking,UpdateCheckAvailable()&&!busy};
}
void StopUpdates(){if(window)KillTimer(window,UpdateTimerId);updateTimerDelayMs=0;if(updatesStarted){updatesStarted=false;updates.Shutdown();}updateToast.Close();}
// Legacy portable exe (old Start Menu shortcut target) after a successful first install: hand over to the
// installed current version (stable profile/session) instead of reopening the obsolete portable copy.
// Validation lives in UpdateCore::PortableForwardTarget; any failure keeps the portable browser running.
bool ForwardToInstalled(const std::wstring& installRoot=L""){
    const auto target=UpdateCore::PortableForwardTarget(ExePath(),installRoot);if(target.empty())return false;
    const auto dir=target.substr(0,target.find_last_of(L'\\'));std::wstring command=L"\""+target+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(target.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,dir.c_str(),&si,&pi)){Log(L"UPDATE forward to installed failed, staying portable");return false;}
    AllowSetForegroundWindow(pi.dwProcessId);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    Log(L"UPDATE portable launch forwarded to "+target);return true;
}
void LayoutUpdateToast(){if(updateToast.Window())updateToast.Layout();}
// The bottom-left card keeps clear of the download drawer docked on the right.
void SyncUpdateToastReserve(){const bool open=(downloadDrawer.Visible()&&!downloadDrawer.Closing())||settingsDrawer.Open()||historyDrawer.Open()||bookmarksDrawer.Open();updateToast.SetReservedRight(open?(settingsDrawer.Open()?NativeChrome::SettingsUI::Layout::width:NativeChrome::Downloads::DrawerLayout::width)+NativeChrome::Downloads::DrawerLayout::margin:0.f);}
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
        dataDir=paths.dataDir;profileDir=paths.profileDir;sessionFile=paths.sessionFile;downloadsFile=paths.downloadsFile.empty()?paths.dataDir+L"\\downloads":paths.downloadsFile;settingsFile=paths.dataDir+L"\\settings";if(!paths.logFile.empty())logFile=paths.logFile;}
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
constexpr long long AccessibleStrip=0x10000,AccessibleToolbar=0x10001,AccessibleSuggestions=0x10002;
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
    case 900:return {L"Liste des onglets",L"",std::to_wstring(pages.size())+(pages.size()>1?L" onglets ouverts":L" onglet ouvert"),L"TabList"};
    case 'Y':return {L"Historique",L"Ctrl+H",L"",L"History"};
    case 'T':return {L"Nouvel onglet",L"Ctrl+T",L"",L"NewTab"};
    case 'm':return {L"Réduire",L"",L"",L"Minimize"};
    case 'M':return {window&&IsZoomed(window)?L"Restaurer":L"Agrandir",L"",L"",L"Maximize"};
    case 'X':return {L"Fermer la fenêtre",L"Alt+F4",L"",L"Close"};
    case 'B':return {L"Précédent",L"Alt+Flèche gauche",L"",L"Back"};
    case 'F':return {L"Suivant",L"Alt+Flèche droite",L"",L"Forward"};
    case 'R':return {chrome.navigationLoading?L"Arrêter le chargement":L"Actualiser",L"Ctrl+R",L"",L"Reload"};
    case 'C':return {L"Copier le lien",L"",L"",L"CopyLink"};
    case 'K':return {chrome.starred?L"Modifier le favori":L"Ajouter aux favoris",L"Ctrl+D",L"",L"Bookmark.Star"};
    case 'O':{const size_t hidden=chrome.bandItems.size()-std::min(chrome.bandItems.size(),(size_t)chrome.band.visible),other=bookmarks.Root(BM::OtherRoot).children.size();return {L"Autres favoris ("+std::to_wstring(hidden+other)+L")",L"",L"Favoris masqués, Autres favoris et Gérer les favoris",L"Bookmarks.Overflow"};}
    case 'E':return {L"Ajouter cette page aux favoris",L"Ctrl+D",L"",L"Bookmarks.AddPage"};
    case 'D':return {L"Téléchargements",L"",L"",L"Downloads"};
    case 'P':return {L"Navigation privée",L"",L"Non disponible dans cette version",L"Private"};
    case 'S':return {L"Réglages",L"",L"",L"Settings"};
    default:return {L"",L"",L"",L""};
    }
}
bool AddressOpen(){return editing&&address&&IsWindowVisible(address);}
// Snapshot of the chrome as UI Automation nodes, in reading order:
// tab list button, tabs (each with its close button), new tab, window buttons, toolbar.
std::vector<Accessibility::Node> AccessibleNodes(){
    using Accessibility::Node;using Accessibility::Role;namespace TS=NativeChrome::Design::TabStrip;
    std::vector<Node> nodes;
    auto screen=[](D2D1_RECT_F r){RECT p{S(r.left),S(r.top),S(r.right),S(r.bottom)};MapWindowPoints(window,nullptr,(POINT*)&p,2);return p;};
    auto region=[](int id)->const NativeChrome::Region*{for(auto& r:chrome.regions)if(r.id==id)return &r;return nullptr;};
    auto button=[&](int id,long long parent){
        auto* r=region(id);if(!r)return;auto label=AccessibleButton(id);
        Node n;n.key=id;n.parent=parent;n.name=label.name;n.accelerator=label.accelerator;n.help=label.help;n.automationId=label.id;
        n.bounds=screen(r->rect);n.enabled=chrome.Enabled(id);n.focusable=r->focusable&&n.enabled;nodes.push_back(std::move(n));
    };
    button(900,0);
    Node strip;strip.key=AccessibleStrip;strip.role=Role::Tab;strip.name=L"Onglets";strip.automationId=L"Tabs";strip.focusable=false;
    strip.bounds=screen({chrome.strip.ViewLeft(),TS::regionTop,chrome.strip.ViewRight(),TS::regionBottom});nodes.push_back(strip);
    for(int i=0;i<(int)pages.size();++i){
        const auto& page=*pages[i];const std::wstring title=page.title.empty()?L"Onglet sans titre":page.title;
        Node tab;tab.key=AccessibleTabKey(i);tab.parent=AccessibleStrip;tab.role=Role::TabItem;tab.name=title;tab.automationId=L"Tab";
        tab.selected=i==current;tab.position=i+1;tab.count=(int)pages.size();
        if(i<8)tab.accelerator=L"Ctrl+"+std::to_wstring(i+1);else if(i==(int)pages.size()-1)tab.accelerator=L"Ctrl+9";
        if(page.loading.IsLoading())tab.status=L"Chargement en cours";
        if(page.sleeping)tab.status+=tab.status.empty()?L"En veille":L", en veille";
        // Tabs scrolled out of the strip stay listed (and selectable) but off screen.
        if(auto* r=region(NativeChrome::TabBase+i)){tab.bounds=screen(r->rect);tab.focusable=r->focusable;}else{tab.offscreen=true;tab.focusable=false;}
        nodes.push_back(tab);
        if(auto* r=region(NativeChrome::CloseBase+i)){
            Node close;close.key=AccessibleTabKey(i,true);close.parent=tab.key;close.name=L"Fermer l'onglet "+title;
            if(i==current)close.accelerator=L"Ctrl+W";
            close.automationId=L"CloseTab";close.bounds=screen(r->rect);close.enabled=chrome.Enabled(r->id);nodes.push_back(std::move(close));
        }
    }
    button('T',0);button('m',0);button('M',0);button('X',0);
    Node bar;bar.key=AccessibleToolbar;bar.role=Role::ToolBar;bar.name=L"Navigation";bar.automationId=L"Toolbar";bar.focusable=false;
    bar.bounds=screen({6,chrome.bar.top,chrome.width-6,chrome.bar.bottom});nodes.push_back(bar);
    button('B',AccessibleToolbar);button('F',AccessibleToolbar);
    // Security indicator (left of the address, so read first): icon + label; invoking it
    // opens the address.
    if(auto* r=region('I')){Node n;n.key='I';n.parent=AccessibleToolbar;n.name=chrome.address.tip();n.automationId=L"SiteSecurity";n.bounds=screen(r->rect);n.focusable=false;nodes.push_back(std::move(n));}
    // While the real EDIT is open it stands for the address itself (named through
    // AccessibleHost().label), with its own value, text and caret.
    if(auto* r=region('L');r&&!AddressOpen()){
        Node edit;edit.key='L';edit.parent=AccessibleToolbar;edit.role=Role::Edit;edit.name=L"Adresse ou recherche";edit.accelerator=L"Ctrl+L";edit.automationId=L"Address";
        edit.bounds=screen(r->rect);
        if(auto t=Active();t&&!t->home)edit.value=t->uri;
        nodes.push_back(std::move(edit));
    }
    // Suggestion list while the EDIT is open: focus stays in the EDIT, the active row
    // is the selected list item (combobox / aria-activedescendant model).
    if(AddressOpen()&&suggestions.Visible()){
        Node list;list.key=AccessibleSuggestions;list.parent=AccessibleToolbar;list.role=Role::List;list.name=L"Suggestions";list.automationId=L"AddressSuggestions";list.focusable=false;list.bounds=suggestions.Bounds();nodes.push_back(list);
        const int n=(int)suggestions.Data().rows.size();
        for(int i=0;i<n;++i){Node item;item.key=AccessibleSuggestionKey(i);item.parent=AccessibleSuggestions;item.role=Role::ListItem;item.name=suggestions.Spoken(i);item.selected=i==suggestions.Active();item.position=i+1;item.count=n;item.focusable=false;item.bounds=suggestions.RowScreen(i);nodes.push_back(std::move(item));}
    }
    for(int id:{'R','C'})button(id,AccessibleToolbar);
    // The star: a toggle (On = this page is bookmarked) that also invokes.
    if(auto* r=region('K')){auto label=AccessibleButton('K');Node n;n.key='K';n.parent=AccessibleToolbar;n.name=label.name;n.accelerator=label.accelerator;n.automationId=label.id;n.bounds=screen(r->rect);n.enabled=chrome.Enabled('K');n.focusable=n.enabled;n.toggle=chrome.starred?2:1;nodes.push_back(std::move(n));}
    for(int id:{'Y','D','P','S'})button(id,AccessibleToolbar);
    // "Barre de favoris": every item of the bar root, those behind » listed off screen (still invocable).
    if(chrome.bandShown){
        Node band;band.key=AccessibleBandKey;band.role=Role::ToolBar;band.name=L"Barre de favoris";band.automationId=L"BookmarksBar";band.focusable=false;
        band.bounds=screen({6,BookmarksBar::Layout::top,chrome.width-6,BookmarksBar::Layout::bottom});nodes.push_back(band);
        const int n=(int)chrome.bandItems.size();
        for(int i=0;i<n;++i){const auto& item=chrome.bandItems[i];Node b;b.key=(4LL<<32)|item.id;b.parent=AccessibleBandKey;
            b.name=item.title.empty()?L"Favori sans titre":item.title;b.automationId=L"Bookmark."+std::to_wstring(item.id);b.position=i+1;b.count=n;
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
    host.label=[](HWND child,Accessibility::Node& node){if(!child||child!=address)return false;node.name=L"Adresse ou recherche";node.accelerator=L"Ctrl+L";node.automationId=L"Address";return true;};
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
    const auto text=suggestions.Spoken(i)+L", "+std::to_wstring(i+1)+L" sur "+std::to_wstring(n);
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
void AnnounceCopied(){accessibility.Notify('C',L"Lien copié",L"CopyLink");}
// Raised on the Réglages button: it always exists, unlike drawer rows.
void AnnounceDataCleared(){accessibility.Notify('S',L"Données effacées",L"ClearData");}
// Raised on the History button that opened the drawer (the drawer is painted, it has no UIA tree of its own).
void AnnounceHistory(const std::wstring& text){accessibility.Notify('Y',text.c_str(),L"History");}
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
    if(msg==WM_KEYDOWN&&w==VK_RETURN&&addressKeys.returnHandled){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);editing=false;ShowWindow(h,SW_HIDE);Navigate(Active(),Destination(s));FocusPage();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_TAB&&addressKeys.HandleKey(VK_TAB)){editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);keyboardFocus='R';keyboardModality=true;InvalidateRect(window,nullptr,FALSE);SyncAccessibility();return 0;}
    return DefSubclassProc(h,msg,w,l);
}
void UpdateFont(){if(font)DeleteObject(font);font=CreateFontW(-S(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");SendMessageW(address,WM_SETFONT,(WPARAM)font,TRUE);}
LRESULT CALLBACK ChromeProc(HWND h,UINT msg,WPARAM w,LPARAM l){LRESULT frameResult;if(NativeUI::HandleFrame(h,msg,w,l,frameResult))return frameResult;switch(msg){
case WM_CREATE:window=h;address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,h,nullptr,instance,nullptr);SendMessageW(address,EM_SETLIMITTEXT,8192,0);SendMessageW(address,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowSubclass(address,AddressProc,1,0);accessibility.Attach(AccessibleHost());tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,h,nullptr,instance,nullptr);return 0;
case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);int edge=S(4);if(!IsZoomed(h)){bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<S(3),bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}float x=p.x*96.f/GetDpiForWindow(h),y=p.y*96.f/GetDpiForWindow(h);int id=chrome.Hit(x,y);if(id=='M')return HTMAXBUTTON;if(y<44&&!id)return HTCAPTION;return HTCLIENT;}
case WM_NCLBUTTONDOWN:if(w==HTMAXBUTTON){Command('M');return 0;}break;
case WM_NCMOUSEMOVE:if(w==HTMAXBUTTON){UpdateInteraction('M',0);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,h,0};TrackMouseEvent(&track);return 0;}break;
case WM_NCMOUSELEAVE:UpdateInteraction(0,0);return 0;
case WM_NCLBUTTONDBLCLK:if(w==HTCAPTION){Command('M');return 0;}break;
case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)l;info->ptMinTrackSize={S(560),S(300)};MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};}return 0;}
case WM_DPICHANGED:{auto r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);UpdateFont();Layout();
#ifdef BROWSER_UPDATES
    LayoutUpdateToast();
#endif
    return 0;}
case WM_SIZE:HideSuggestions();motionMinimized=w==SIZE_MINIMIZED;if(!motionMinimized){Layout();downloadDrawer.Reposition();settingsDrawer.Reposition();historyDrawer.Reposition();bookmarksDrawer.Reposition();bookmarkToast.Reposition();}else{CloseDownloadSurfaces(false);settingsDrawer.Hide(false,false);historyDrawer.Hide(false,false);bookmarksDrawer.Hide(false,false);bookmarkPopover.Hide();bookmarkToast.HideNow();}ScheduleMotion(NativeChrome::Motion::Now());
#ifdef BROWSER_UPDATES
    // Minimised: the toast stops its clock/timer (zero wakes); otherwise SetOwnerVisible(true)
    // re-lays the card out (bottom-left anchor, drawer reserve kept).
    updateToast.SetOwnerVisible(!motionMinimized);
#endif
    return 0;
case WM_MOVE:HideSuggestions();downloadDrawer.Reposition();settingsDrawer.Reposition();historyDrawer.Reposition();bookmarksDrawer.Reposition();bookmarkToast.Reposition();if(bookmarkPopover.Visible())bookmarkPopover.Hide();
#ifdef BROWSER_UPDATES
    LayoutUpdateToast();
#endif
    return 0;
#ifdef BROWSER_UPDATES
case WM_EXITSIZEMOVE:LayoutUpdateToast();return 0;
case UpdateMessage:if(updatesStarted&&updates.OnNotify(w)){ShowUpdateView();ArmUpdateTimer();}return 0;
#endif
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PAINTSTRUCT paint;BeginPaint(h,&paint);++chromePaints;SyncAddressDisplay();std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto&t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));{std::vector<bool> asleep;for(auto&t:pages)asleep.push_back(t->sleeping);chrome.sleeping=std::move(asleep);}auto t=Active();D2D1_RECT_F dirty{paint.rcPaint.left*96.f/GetDpiForWindow(h),paint.rcPaint.top*96.f/GetDpiForWindow(h),paint.rcPaint.right*96.f/GetDpiForWindow(h),paint.rcPaint.bottom*96.f/GetDpiForWindow(h)};chrome.loading=t?t->loading.Sample(NativeChrome::Motion::Now(),motion.ReducedMotion()):NativeChrome::Loading::Visual{};{auto b=chrome.LoadingDirty();if(dirty.left<=b.left&&dirty.top<=b.top&&dirty.right>=b.right&&dirty.bottom>=b.bottom)paintedLoading=chrome.LoadingSignature(chrome.loading);}chrome.keyboardModality=keyboardModality;chrome.Paint(titles,current,t&&!t->home?t->uri:L"",editing,hover,GetFocus()==address?'L':GetFocus()==h?keyboardFocus:0,&dirty);EndPaint(h,&paint);if(editing)RedrawWindow(address,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);return 0;}
case WM_MOUSEMOVE:{if(tabDrag.tab){DragTab({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(tabDrag.active)return 0;}if(bandDrag.index>=0){BandDragMove({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(bandDrag.active)return 0;}int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)<0)UnlockTabWidth();if(id!=hover)UpdateInteraction(id,pressed);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
case WM_COMMAND:if((HWND)l==address&&HIWORD(w)==EN_CHANGE){AddressEdited();return 0;}break;
case WM_MOUSELEAVE:UnlockTabWidth();UpdateInteraction(0,pressed);return 0;
case WM_LBUTTONDOWN:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));downloadDismissClick=id=='D'&&GetTickCount64()-std::max(downloadPanel.hiddenAt,downloadDrawer.hiddenAt)<150;settingsDismissClick=id=='S'&&GetTickCount64()-settingsDrawer.hiddenAt<150;historyDismissClick=id=='Y'&&GetTickCount64()-historyDrawer.hiddenAt<150;if(id){keyboardFocus=0;keyboardModality=false;if(!NativeChrome::IsClose(id))SetFocus(h);SetCapture(h);UpdateInteraction(id,id);if(NativeChrome::IsTab(id)){tabDrag.tab=pages[NativeChrome::TabIndex(id)];tabDrag.start={GET_X_LPARAM(l),GET_Y_LPARAM(l)};tabDrag.grab=GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.strip.Surface(NativeChrome::TabIndex(id)).left;}
    if(NativeChrome::IsBookmark(id)){const int i=NativeChrome::BookmarkIndex(id);if(i<(int)chrome.bandItems.size()){const BM::Id bid=chrome.bandItems[i].id;bandDrag={i,bid,{GET_X_LPARAM(l),GET_Y_LPARAM(l)},GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.BandRect(i).left,false,BM::BarRoot,bookmarks.IndexOf(bid)};}}}return 0;}
case WM_LBUTTONUP:{bool dragged=tabDrag.active||bandDrag.active;CancelTabDrag();BandDragEnd(false);int original=pressed;int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(GetCapture()==h)ReleaseCapture();UpdateInteraction(id,0);
    if(!dragged&&original&&original==id&&NativeChrome::IsBookmark(id)){BandActivate(NativeChrome::BookmarkIndex(id),(w&MK_CONTROL)!=0,(w&MK_SHIFT)!=0,false);return 0;}
    if(!dragged&&original&&original==id){if(NativeChrome::IsClose(id)){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}else if(NativeChrome::IsTab(id))Select(NativeChrome::TabIndex(id));else Command(id);}return 0;}
case WM_CAPTURECHANGED:case WM_CANCELMODE:CancelTabDrag();if(bandDrag.index>=0&&(HWND)l!=h)BandDragEnd(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;
case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE){CancelTabDrag();if(GetCapture()==h)ReleaseCapture();UpdateInteraction(0,0);}return 0;
case WM_SETTINGCHANGE:ApplyReducedMotion();return 0; // Windows' switch, OR'd with the app's own
case WM_MBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));
    if(NativeChrome::IsBookmark(id)){const int i=NativeChrome::BookmarkIndex(id);if(i<(int)chrome.bandItems.size()&&!chrome.bandItems[i].folder)BandActivate(i,true,false,false);return 0;}
    if(NativeChrome::TabIndex(id)>=0){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}return 0;}
case WM_RBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));const float y=GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h);
    if(NativeChrome::IsBookmark(id)&&NativeChrome::BookmarkIndex(id)<(int)chrome.bandItems.size()){POINT p{};GetCursorPos(&p);BookmarkContextMenu(chrome.bandItems[NativeChrome::BookmarkIndex(id)].id,p,nullptr,false);return 0;}
    if(chrome.bandShown&&y>=NativeChrome::Design::Vertical::toolbarBottom&&y<chrome.ContentTop()){POINT p{};GetCursorPos(&p);BandContextMenu(p);return 0;}
    if(NativeChrome::TabIndex(id)>=0)TabContextMenu(NativeChrome::TabIndex(id),0);else if(GET_Y_LPARAM(l)<S(44))TabMenu(0);return 0;}
// Menu key / Shift+F10 arrive with lParam (-1,-1): on a focused tab or its close button, open the right-click menu
// below it; on the flower or the gear, open their own menu.
case WM_CONTEXTMENU:if(GET_X_LPARAM(l)==-1&&GET_Y_LPARAM(l)==-1&&GetFocus()==h){int tabIndex=NativeChrome::TabIndex(keyboardFocus);if(tabIndex>=0&&tabIndex<(int)pages.size()){keyboardModality=true;TabContextMenu(tabIndex,keyboardFocus);return 0;}if(keyboardFocus==900||keyboardFocus=='S'){keyboardModality=true;Command(keyboardFocus);return 0;}
    if(NativeChrome::IsBookmark(keyboardFocus)&&NativeChrome::BookmarkIndex(keyboardFocus)<(int)chrome.bandItems.size()){keyboardModality=true;const RECT r=RegionScreen(keyboardFocus);BookmarkContextMenu(chrome.bandItems[NativeChrome::BookmarkIndex(keyboardFocus)].id,{r.left,r.bottom},&r,true);if(GetForegroundWindow()==window&&!bookmarkPopover.Visible())SetFocus(window);InvalidateChrome();return 0;}}break;
case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<S(44)&&!pages.empty())Command(GET_WHEEL_DELTA_WPARAM(w)>0?'[':']');return 0;}
case WM_KEYDOWN:if(w==VK_ESCAPE&&tabDrag.tab){CancelTabDrag();if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;}
    if(w==VK_ESCAPE&&bandDrag.index>=0){BandDragEnd(true);if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;}
    if(GetFocus()==h&&BandKey(w)){SyncAccessibility();return 0;}if(w==VK_TAB||((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus)||((w==VK_LEFT||w==VK_RIGHT)&&NativeChrome::IsTab(keyboardFocus)))keyboardModality=true;if(w==VK_TAB){auto order=chrome.FocusOrder();if(order.empty())return 0;auto it=std::find(order.begin(),order.end(),keyboardFocus);int index=it==order.end()?-1:(int)(it-order.begin());index=it==order.end()?(GetKeyState(VK_SHIFT)<0?(int)order.size()-1:0):(index+(GetKeyState(VK_SHIFT)<0?-1:1)+(int)order.size())%(int)order.size();keyboardFocus=order[index];InvalidateRect(h,nullptr,FALSE);return 0;}if((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus){if(NativeChrome::IsClose(keyboardFocus))CloseAt(NativeChrome::TabIndex(keyboardFocus));else if(NativeChrome::IsTab(keyboardFocus)){Select(NativeChrome::TabIndex(keyboardFocus));SetFocus(h);}else Command(keyboardFocus);return 0;}
    if((w==VK_LEFT||w==VK_RIGHT)&&NativeChrome::IsTab(keyboardFocus)&&!pages.empty()){int next=(NativeChrome::TabIndex(keyboardFocus)+(w==VK_LEFT?-1:1)+(int)pages.size())%(int)pages.size();keyboardFocus=NativeChrome::TabBase+next;Select(next);SetFocus(h);return 0;}break;
case WM_CTLCOLOREDIT:{
    using namespace NativeChrome::Design;
    static NativeUI::AddressBackground background;
    HDC dc=(HDC)w;RECT rect;GetClientRect(address,&rect);
    POINT origin{};MapWindowPoints(address,window,&origin,1);
    const float surface=Surface::raised;
    const float alpha=AddressLoading::opacity*chrome.loading.opacity;
    const int edge=S(chrome.bar.left+(chrome.bar.right-chrome.bar.left)*chrome.loading.fraction)-origin.x;
    SetBkMode(dc,TRANSPARENT);SetBkColor(dc,NativeGray(surface));
    SetDCBrushColor(dc,NativeGray(surface));SetBrushOrgEx(dc,0,0,nullptr);
    SetTextColor(dc,NativeGray(surface+(1-surface)*Text::address));
    return (LRESULT)background.Brush(rect.right,edge,NativeGray(surface),NativeGray(surface+(1-surface)*alpha));
}
case Shortcut:keyboardModality=true;Command((int)w);return 0;
case AccessibleMessage:AccessibleAction((int)w,(long long)l);return 0;
case DownloadAskMessage:AskPendingDownloads();return 0;
case PageMenuMessage:ShowPageMenu();return 0;
case WM_APP+2:SmokeCheck();return 0;
case WM_APP+3:SmokeAdvance();return 0;
case FrameMessage:{if(l==NativeChrome::FrameClock::ClockLost){frameClock.Stop();frameClockReady=false;ScheduleMotion(NativeChrome::Motion::Now());return 0;}
    frameClock.Acknowledge();auto now=NativeChrome::Motion::Now();if(!halted&&!motionMinimized)InvalidateMotion(now);ScheduleMotion(now);return 0;}
case WM_TIMER:
#ifdef BROWSER_UPDATES
    if(w==UpdateTimerId){UpdateTimerFired();return 0;}
#endif
    if(w==SleepTimerId){SleepTabs();return 0;}
    if(w==ExitClearTimerId){Log(L"SETTINGS clear on exit: timed out, closing");FinishExitClear();return 0;}
    if(w==DrawerWarmTimerId){KillTimer(h,DrawerWarmTimerId);if(downloadPanel.Visible())downloadDrawer.Warm();return 0;}
    if(w==MotionTimerId){KillTimer(h,MotionTimerId);motionDeadline.reset();auto now=NativeChrome::Motion::Now();InvalidateMotion(now);ScheduleMotion(now);return 0;}if(w==1&&smoke){KillTimer(h,1);Fail(L"Smoke timeout after 60 seconds",HRESULT_FROM_WIN32(WAIT_TIMEOUT));}return 0;
case WM_CLOSE:SaveSession();SaveDownloads();if(!bookmarksSaveFailed)SaveBookmarks();if(!ClearOnExitAndClose())DestroyWindow(h);return 0;
case WM_DESTROY:accessibility.Detach();
#ifdef BROWSER_UPDATES
    StopUpdates(); // bounded: cancels transfers; an already launched install helper is a separate process
#endif
    KillTimer(h,MotionTimerId);motionDeadline.reset();frameClock.Stop();frameClockReady=false;for(auto&t:pages){t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();}pages.clear();PostQuitMessage(0);return 0;
}return DefWindowProcW(h,msg,w,l);}
// Every message may move keyboard focus, the selection or the tab set: report the
// change to UI Automation clients once the message is handled.
LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_GETOBJECT){LRESULT result;if(accessibility.OnGetObject(h,w,l,result))return result;}
    const LRESULT result=ChromeProc(h,msg,w,l);
    if(h==window&&msg!=WM_GETOBJECT&&msg!=WM_DESTROY&&msg!=WM_NCDESTROY)SyncAccessibility();
    return result;
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR args,int show){smoke=wcsstr(args,L"--smoke-test")!=nullptr;instance=hi;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 1;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);PathRemoveFileSpecW(path);base=path;ResolveBrowserPaths();
#ifdef BROWSER_UPDATES
    if(!smoke&&ForwardToInstalled()){CoUninitialize();return 0;} // before any window, profile or WebView2
#endif
    Log(L"START browser");INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);paper=CreateSolidBrush(BG);WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"LisiereBrowser";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(101));wc.hbrBackground=paper;RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"browser",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,hi,nullptr);NativeUI::ConfigureFullClientFrame(window);if(FAILED(chrome.Init(window,base))){MessageBoxW(window,L"Initialisation graphique native impossible.",L"browser",MB_OK|MB_ICONERROR);return 3;}LoadSettings();motion.SetReducedMotion(ReducedMotionPreferred());NativeChrome::Design::Accent::Set(settings.accent);LoadDownloads();LoadHistory();LoadBookmarks();CreateDownloadPanel();CreateSuggestions();CreateSettingsPanel();CreateHistoryPanel();CreateBookmarkSurfaces();contextMenu.Init(instance,window,base);frameClockReady=frameClock.Start(window,FrameMessage);motion.EnablePolish();chrome.animateSelection=true;chrome.SetMotion(&motion);UpdateFont();Layout();ShowWindow(window,show);UpdateWindow(window);if(smoke)SetTimer(window,1,60000,nullptr);
#ifdef BROWSER_UPDATES
    StartUpdates(); // never blocks: the delayed startup check runs on the engine's thread
#endif
    loader=LoadLibraryExW((base+L"\\WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader)Fail(L"LoadLibrary WebView2Loader.dll",HRESULT_FROM_WIN32(GetLastError()));
    // Engine UI (page menu labels, print preview, dialogs) in French; any user-set arguments are kept.
    {const DWORD size=GetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",nullptr,0);std::wstring value(size,L'\0');
     if(size)value.resize(GetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",value.data(),size));
     if(value.find(L"--lang=")==std::wstring::npos){value+=(value.empty()?L"":L" ")+std::wstring(L"--lang=fr");SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",value.c_str());}}
    using CreateEnvironment=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=loader?reinterpret_cast<CreateEnvironment>(GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions")):nullptr;if(loader&&!create)Fail(L"GetProcAddress CreateEnvironment",HRESULT_FROM_WIN32(GetLastError()));
    if(create&&!halted){auto done=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{if(!Check(hr,L"CreateEnvironment callback"))return S_OK;if(!env){Fail(L"Environment null",E_POINTER);return S_OK;}environment.set(env);LPWSTR version=nullptr;if(Check(env->get_BrowserVersionString(&version),L"get_BrowserVersionString")&&version){Log(L"RUNTIME "+std::wstring(version));CoTaskMemFree(version);}RestoreSession();return S_OK;});HRESULT hr=create(nullptr,profileDir.c_str(),nullptr,done);done->Release();Check(hr,L"CreateEnvironment");}
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){int cmd=Key((UINT)msg.wParam);if(cmd){keyboardModality=true;Command(cmd);SyncAccessibility();continue;}}TranslateMessage(&msg);DispatchMessageW(&msg);}environment.set(nullptr);if(loader)FreeLibrary(loader);suggestions.Dispose();downloadPanel.Dispose();downloadDrawer.Dispose();settingsDrawer.Dispose();historyDrawer.Dispose();bookmarkPopover.Dispose();bookmarkToast.Dispose();bookmarksDrawer.Dispose();contextMenu.Dispose();chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return halted?2:0;
}
