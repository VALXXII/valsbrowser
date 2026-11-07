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
#include <shlobj.h>

// COM callbacks use the official SDK interface IDs, independent of compiler UUID extensions.
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2SourceChangedEventHandler,"{3c067f9f-5388-4772-8b48-79f7ef1ab37c}")
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
struct Tab { Com<ICoreWebView2Controller> controller; Com<ICoreWebView2> view; Com<ICoreWebView2_15> faviconView; NativeChrome::FaviconState favicon; NativeChrome::Loading::State loading; std::wstring title=L"Nouvel onglet",uri; bool closed=false,home=true,refreshing=false; };
HWND window,tabs,address,back,forward,reload,add,closeTab,home;
HFONT font; HBRUSH paper; HINSTANCE instance; HMODULE loader;
Com<ICoreWebView2Environment> environment;
std::vector<std::shared_ptr<Tab>> pages; int current=-1; bool halted=false;
struct ClosedTab { std::wstring uri,title; int index; };
std::vector<ClosedTab> closedTabs; bool sessionReady=false;
std::wstring base; const COLORREF BG=NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::canvas);
NativeChrome::Chrome chrome; NativeChrome::DownloadsUI::Popover downloadPanel; NativeChrome::DownloadsUI::Drawer downloadDrawer; NativeUI::AddressKeyState addressKeys; NativeChrome::Motion::State motion; int pressed=0; bool motionMinimized=false; constexpr UINT_PTR MotionTimerId=72; std::optional<NativeChrome::Motion::Time> motionDeadline; HWND tooltip; int hover=0,keyboardFocus=0; bool editing=false,keyboardModality=false;
struct TabDrag { std::shared_ptr<Tab> tab; POINT start{}; float grab=0; bool active=false; } tabDrag;
void InvalidateChrome();
void CancelTabDrag(){tabDrag={};chrome.dragIndex=-1;chrome.dragOffset=0;InvalidateChrome();}
int S(float value){return (int)std::lround(value*GetDpiForWindow(window)/96.f);}
void RefreshTools();
void ExtraCommand(int cmd);
void InvalidateChrome(){RECT r;GetClientRect(window,&r);r.bottom=std::min(r.bottom,(LONG)S(84));InvalidateRect(window,&r,FALSE);}
// Frame wakes are display-paced by FrameClock (one outstanding notification, worker
// dormant when idle). Deadline-only dwells keep a single one-shot SetTimer. If the
// clock is unavailable the legacy SetTimer path remains (not display synchronised).
constexpr UINT FrameMessage=WM_APP+8; NativeChrome::FrameClock::Clock frameClock; bool frameClockReady=false,motionFullFrames=false; long long paintedLoading=-2;
std::shared_ptr<Tab> Active();
// Loading-only frames repaint just the URL surface, and only when the quantised fill
// differs from what was last painted; any other motion (or the frame right after it
// ends, so its endpoint is painted) repaints the whole header.
void InvalidateMotion(NativeChrome::Motion::Time now){
    downloadPanel.Frame(now);downloadDrawer.Frame(now);
    const bool full=motionFullFrames||motion.NextWake(now).delay||chrome.lottie.NextWake(now,motion.ReducedMotion()).delay;
    if(full){InvalidateChrome();return;}
    auto t=Active();if(chrome.LoadingSignature(t?t->loading.Sample(now,motion.ReducedMotion()):NativeChrome::Loading::Visual{})==paintedLoading)return;
    auto d=chrome.LoadingDirty();RECT r{S(d.left),S(d.top),S(d.right),S(d.bottom)};InvalidateRect(window,&r,FALSE);
}
void ScheduleMotion(NativeChrome::Motion::Time now){
    const bool reduced=motion.ReducedMotion();auto ui=NativeChrome::Motion::Earlier(motion.NextWake(now),chrome.lottie.NextWake(now,reduced));motionFullFrames=ui.delay.has_value();
    auto wake=NativeChrome::Motion::Earlier(ui,NativeChrome::Motion::Earlier(downloadPanel.NextWake(now),downloadDrawer.NextWake(now)));if(current>=0&&current<(int)pages.size())wake=NativeChrome::Motion::Earlier(wake,pages[current]->loading.NextWake(now,reduced));if(halted||motionMinimized||!wake.delay){KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();return;}
    // A deadline due within one nominal frame is served by the next compositor tick.
    if(frameClockReady&&(wake.frames||*wake.delay<=*NativeChrome::Motion::Frame().delay)){if(motionDeadline){KillTimer(window,MotionTimerId);motionDeadline.reset();}frameClock.Request();return;}
    frameClock.Cancel();
    auto due=now+*wake.delay;if(motionDeadline&&*motionDeadline<=due)return;
    KillTimer(window,MotionTimerId);motionDeadline=due;
    if(!SetTimer(window,MotionTimerId,(UINT)wake.delay->count(),nullptr)){motionDeadline.reset();motion.SetReducedMotion(true);motion.copy.Clear();chrome.lottie.Clear();motion.SetInteraction(hover,0,now);InvalidateChrome();}
}
void UpdateInteraction(int newHover,int newPressed){hover=newHover;pressed=newPressed;auto now=NativeChrome::Motion::Now();int group=NativeChrome::IsClose(hover)?NativeChrome::TabBase+NativeChrome::TabIndex(hover):hover;motion.SetInteraction(group,pressed==hover?pressed:0,now);InvalidateChrome();ScheduleMotion(now);}

constexpr UINT Shortcut=WM_APP+1;
bool smoke=false; int testStage=0;
void SmokeCheck();
void SmokeAdvance();

std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
void Log(const std::wstring& s){std::ofstream f(Utf8(base+L"\\prototype.log"),std::ios::app);f<<Utf8(s)<<"\n";if(smoke&&s.rfind(L"NAVIGATION OK",0)==0)PostMessageW(window,WM_APP+2,0,0);}
void Fail(const std::wstring& where,HRESULT hr){if(halted)return;halted=true;for(auto& tab:pages)tab->loading.Clear();chrome.lottie.Clear();KillTimer(window,MotionTimerId);motionDeadline.reset();frameClock.Cancel();wchar_t code[32];swprintf(code,32,L"0x%08lX",(unsigned long)hr);auto msg=where+L" : "+code+L"\nOpérations arrêtées. Aucun correctif automatique.";Log(L"WEBVIEW2 ERROR "+msg);MessageBoxW(window,msg.c_str(),L"WebView2 — arrêt demandé",MB_OK|MB_ICONERROR);}
bool Check(HRESULT hr,const wchar_t* op){if(FAILED(hr)){Fail(op,hr);return false;}return !halted;}
std::shared_ptr<Tab> Active(){return current>=0&&current<(int)pages.size()?pages[current]:nullptr;}
void FocusAddress(){editing=true;ShowWindow(address,SW_SHOW);SetFocus(address);SendMessageW(address,EM_SETSEL,0,-1);InvalidateRect(window,nullptr,FALSE);}
std::vector<std::wstring> Titles(){std::vector<std::wstring> titles;for(auto& t:pages)titles.push_back(t->title);return titles;}
void BuildChrome(){auto titles=Titles();chrome.Build((int)pages.size(),current,&titles);}
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
    if(GetFocus()==window||GetFocus()==address){FocusPage();return;}
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
    RECT bounds{S(4),S(84),S(chrome.width-4),std::max(S(84),S(chrome.height-4))};for(auto& t:pages)if(t->controller.p&&!halted)Check(t->controller->put_Bounds(bounds),L"put_Bounds");
    RefreshTools();InvalidateRect(window,nullptr,FALSE);
}
void Update(){auto t=Active();if(!t)return;chrome.SetEnabled('C',!t->home&&!t->uri.empty());if(!editing)SetWindowTextW(address,t->home?L"":t->uri.c_str());SetWindowTextW(window,(t->title+L" — browser").c_str());BuildChrome();RefreshTools();InvalidateRect(window,nullptr,FALSE);}
void Select(int n){if(n<0||n>=(int)pages.size()||halted)return;editing=false;ShowWindow(address,SW_HIDE);current=n;chrome.lottie.Clear();motion.SelectTab(n+1,NativeChrome::Motion::Now());ScheduleMotion(NativeChrome::Motion::Now());for(int i=0;i<(int)pages.size();++i)if(pages[i]->controller.p)Check(pages[i]->controller->put_IsVisible(i==n&&!pages[i]->home),L"put_IsVisible");Update();auto t=Active();if(t->controller.p&&!t->home)Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus");}
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
    std::wstring q;for(unsigned char c:Utf8(s)){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')q+=(wchar_t)c;else{wchar_t b[4];swprintf(b,4,L"%%%02X",c);q+=b;}}return L"https://duckduckgo.com/?q="+q;
}
void NewTab(std::wstring uri=L"",int index=-1,std::wstring title=L"Nouvel onglet");
void ReopenTab(){if(closedTabs.empty()||pages.size()>=128)return;auto tab=closedTabs.back();NewTab(tab.uri,tab.index,tab.title);if(!halted)closedTabs.pop_back();}
void Navigate(std::shared_ptr<Tab> t,const std::wstring& uri){if(!t||!t->view.p||halted)return;t->home=uri.empty();t->uri=uri;Check(uri.empty()?t->view->NavigateToString(HomeHtml):t->view->Navigate(uri.c_str()),uri.empty()?L"NavigateToString":L"Navigate");}
void CloseAt(int index){if(halted||index<0||index>=(int)pages.size())return;
    CancelTabDrag();const bool active=index==current;const int next=NativeChrome::SelectionAfterClose((int)pages.size(),current,index);
    motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;auto t=pages[index];if(!t->home&&!t->uri.empty()){if(closedTabs.size()==25)closedTabs.erase(closedTabs.begin());closedTabs.push_back({t->uri,t->title,index});}t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();pages.erase(pages.begin()+index);
    if(pages.empty()){current=-1;NewTab();}else if(active)Select(next);else{current=next;motion.SelectTab(current+1,NativeChrome::Motion::Now());Update();}
}
enum ShortcutCommand { FocusChrome=3000, FindPage, MoveTabLeft='{', MoveTabRight='}', SelectTabBase=3010 };
void Close(){CloseAt(current);}
void Command(int cmd){if(halted||!chrome.Enabled(cmd))return;if(tabDrag.tab){CancelTabDrag();if(GetCapture()==window)ReleaseCapture();UpdateInteraction(hover,0);}auto t=Active();if(cmd>=SelectTabBase&&cmd<SelectTabBase+9){Select(cmd==SelectTabBase+8?(int)pages.size()-1:cmd-SelectTabBase);return;}switch(cmd){case FocusChrome:ToggleChromeFocus();break;case FindPage:FindInPage();break;case '{':MoveTab(current,current-1);break;case '}':MoveTab(current,current+1);break;case 'L':FocusAddress();break;case ']':if(!pages.empty())Select((current+1)%(int)pages.size());break;case '[':if(!pages.empty())Select((current+(int)pages.size()-1)%(int)pages.size());break;case 'T':NewTab();FocusAddress();break;case 'W':Close();break;case 'U':ReopenTab();break;case 'R':if(t&&t->view.p&&t->loading.IsLoading()){Check(t->view->Stop(),L"Stop");break;}if(t&&t->view.p&&Check(t->view->Reload(),L"Reload")){t->refreshing=true;auto now=NativeChrome::Motion::Now();chrome.lottie.RefreshBegin(now);InvalidateChrome();ScheduleMotion(now);}break;case 'H':Navigate(t,L"");break;case 'B':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoBack(&yes);if(yes)Check(t->view->GoBack(),L"GoBack");}break;case 'F':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoForward(&yes);if(yes)Check(t->view->GoForward(),L"GoForward");}break;default:ExtraCommand(cmd);break;}}
int Key(UINT key){
    const bool ctrl=GetKeyState(VK_CONTROL)<0,alt=GetKeyState(VK_MENU)<0,shift=GetKeyState(VK_SHIFT)<0;
    if(ctrl&&!alt){
        if(shift&&key=='T')return 'U';
        if(!shift&&key>='1'&&key<='9')return SelectTabBase+key-'1';
        if(shift&&key==VK_PRIOR)return MoveTabLeft;
        if(shift&&key==VK_NEXT)return MoveTabRight;
        if(key==VK_TAB)return shift?'[':']';
        // In the page WebView2 handles Ctrl+F itself, retaining its native find state.
        if(!shift&&key=='F')return GetFocus()==window||GetFocus()==address?FindPage:0;
        if(!shift&&(key=='L'||key=='T'||key=='W'||key=='R'))return key;
    }
    if(!ctrl&&!alt&&!shift&&key==VK_F6)return FocusChrome;
    if(!ctrl&&!alt&&key==VK_F5)return 'R';
    if(alt&&!ctrl&&!shift){if(key=='D')return 'L';if(key==VK_LEFT)return 'B';if(key==VK_RIGHT)return 'F';}
    return 0;
}

// Store UTF-8 URLs alongside the profile, preserving blank tabs and selection.
void SaveSession(){
    if(smoke||!sessionReady)return;
    BrowserSession::State state;state.active=current;
    for(auto& tab:pages)state.urls.push_back(tab->home?"":Utf8(tab->uri));
    const auto path=std::filesystem::path(base)/L"session",temp=std::filesystem::path(base)/L"session.tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);BrowserSession::Write(out,state);out.close();if(!out){Log(L"SESSION save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"SESSION replace failed");
}
void RestoreSession(){
    BrowserSession::State state;
    if(!smoke){std::ifstream in(std::filesystem::path(base)/L"session",std::ios::binary);state=BrowserSession::Read(in);}
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
    const auto path=std::filesystem::path(base)/L"downloads",temp=std::filesystem::path(base)/L"downloads.tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);DL::WriteHistory(out,downloads);out.close();if(!out){Log(L"DOWNLOADS save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"DOWNLOADS replace failed");
}
// History written before start times existed: date each row from its file's
// creation time (when the download began writing it), else from the history
// file itself, then save so the date sticks.
std::int64_t FileTimeUnixMs(const FILETIME& t){return NativeChrome::DownloadsUI::FileTimeMs(t)-11644473600000LL;}
void LoadDownloads(){
    if(smoke)return;
    const auto path=std::filesystem::path(base)/L"downloads";
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
void ShowDownloadDrawer();
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
    RefreshMissing();
    // Right edge on the toolbar's 6 DIP margin, just under the toolbar row.
    RECT anchor{0,S(48),S(chrome.width-NativeChrome::Design::Space::sm),S(76)};MapWindowPoints(window,nullptr,(POINT*)&anchor,2);
    chrome.latched='D';InvalidateChrome();downloadPanel.Show(anchor);
}
// "View all": the popover gives way to the drawer docked on the right.
void ShowDownloadDrawer(){
    if(!downloadDrawer.Window())return;
    RefreshMissing();chrome.latched='D';InvalidateChrome();
    // From the popover: its rectangle morphs into the drawer.
    RECT from{};const bool morph=downloadPanel.Visible()&&GetWindowRect(downloadPanel.Window(),&from);
    downloadDrawer.Show(morph?&from:nullptr);downloadPanel.Hide(false);
}
void CloseDownloadSurfaces(bool animate=true){downloadPanel.Hide(false);downloadDrawer.Hide(false,animate);}
void ToggleDownloads(){
    if(downloadDrawer.Visible()&&!downloadDrawer.Closing()){downloadDrawer.Hide(true,true);return;}
    if(downloadPanel.Visible()){downloadPanel.Hide(true);return;}
    ShowDownloads();
}
HRESULT DownloadStarting(ICoreWebView2DownloadStartingEventArgs* args){
    if(halted||!args)return S_OK;
    args->put_Handled(TRUE); // the native popover replaces WebView2's flyout
    if(smoke){const auto target=base+L"\\smoke-download.txt";DeleteFileW(target.c_str());args->put_ResultFilePath(target.c_str());}
    ICoreWebView2DownloadOperation* raw=nullptr;if(FAILED(args->get_DownloadOperation(&raw))||!raw){Log(L"DOWNLOAD operation unavailable");return S_OK;}
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
    return S_OK;
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
    hooks.closed=[](bool keyboard){chrome.latched=(downloadPanel.Visible()||(downloadDrawer.Visible()&&!downloadDrawer.Closing()))?'D':0;InvalidateChrome();if(keyboard&&!halted)FocusPage();};
    hooks.viewAll=[](){ShowDownloadDrawer();};
    auto drawerHooks=hooks;
    if(!downloadPanel.Create(instance,window,base,&downloads,std::move(hooks)))Log(L"DOWNLOADS panel unavailable");
    if(!downloadDrawer.Create(instance,window,base,&downloads,std::move(drawerHooks)))Log(L"DOWNLOADS drawer unavailable");
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
        if(tab->favicon.Complete(ticket,chrome.DecodeFavicon(stream)))InvalidateChrome();
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
        auto starting=Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;LPWSTR uri=nullptr;if(!Check(args->get_Uri(&uri),L"NavigationStarting get_Uri"))return S_OK;UINT64 navigationId=0;if(!Check(args->get_NavigationId(&navigationId),L"NavigationStarting id")){CoTaskMemFree(uri);return S_OK;}if(uri&&wcscmp(uri,L"about:blank")!=0)tab->loading.Start(navigationId,NativeChrome::Motion::Now());else tab->loading.Clear();if(tab==Active()){RefreshTools();ScheduleMotion(NativeChrome::Motion::Now());}tab->favicon.Navigation(uri?uri:L"");CoTaskMemFree(uri);InvalidateChrome();return S_OK;});
        HRESULT startingResult=t->view->add_NavigationStarting(starting,&token);starting->Release();if(!Check(startingResult,L"add_NavigationStarting favicon"))return S_OK;
        auto source=Callback<ICoreWebView2SourceChangedEventHandler,ICoreWebView2*,ICoreWebView2SourceChangedEventArgs*>([weak](ICoreWebView2* v,ICoreWebView2SourceChangedEventArgs*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_Source(&s),L"get_Source")&&s){t->uri=s;CoTaskMemFree(s);t->home=t->uri==L"about:blank";}t->loading.Committed(NativeChrome::Motion::Now());if(t==Active()){Update();ScheduleMotion(NativeChrome::Motion::Now());}return S_OK;});
        HRESULT result=t->view->add_SourceChanged(source,&token);source->Release();if(!Check(result,L"add_SourceChanged"))return S_OK;
        auto title=Callback<ICoreWebView2DocumentTitleChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_DocumentTitle(&s),L"get_DocumentTitle")&&s){t->title=s;CoTaskMemFree(s);}Update();return S_OK;});
        result=t->view->add_DocumentTitleChanged(title,&token);title->Release();if(!Check(result,L"add_DocumentTitleChanged"))return S_OK;
        auto nav=Callback<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* a)->HRESULT{return NavigationCompleted(weak.lock(),a);});
        result=t->view->add_NavigationCompleted(nav,&token);nav->Release();if(!Check(result,L"add_NavigationCompleted"))return S_OK;
        auto popup=Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{if(halted)return S_OK;a->put_Handled(TRUE);LPWSTR uri=nullptr;if(Check(a->get_Uri(&uri),L"NewWindow get_Uri")&&uri){std::wstring s=uri;CoTaskMemFree(uri);if(s.rfind(L"https://",0)==0||s.rfind(L"http://",0)==0)NewTab(s);}return S_OK;});
        result=t->view->add_NewWindowRequested(popup,&token);popup->Release();if(!Check(result,L"add_NewWindowRequested"))return S_OK;
        auto key=Callback<ICoreWebView2AcceleratorKeyPressedEventHandler,ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs*>([](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* a)->HRESULT{if(halted)return S_OK;COREWEBVIEW2_KEY_EVENT_KIND kind;a->get_KeyEventKind(&kind);UINT vk;a->get_VirtualKey(&vk);if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN){int cmd=Key(vk);if(cmd){a->put_Handled(TRUE);PostMessageW(window,Shortcut,cmd,0);}}return S_OK;});
        result=c->add_AcceleratorKeyPressed(key,&token);key->Release();if(!Check(result,L"add_AcceleratorKeyPressed"))return S_OK;
        auto failed=Callback<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* a)->HRESULT{return ProcessFailed(weak.lock(),a);});
        result=t->view->add_ProcessFailed(failed,&token);failed->Release();if(!Check(result,L"add_ProcessFailed"))return S_OK;
        {Com<ICoreWebView2_4> view4;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_4,(void**)&view4.p))&&view4.p){auto download=Callback<ICoreWebView2DownloadStartingEventHandler,ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* a)->HRESULT{return DownloadStarting(a);});if(FAILED(view4->add_DownloadStarting(download,&token)))Log(L"DOWNLOADS add_DownloadStarting failed");download->Release();}else Log(L"DOWNLOADS ICoreWebView2_4 unavailable: engine flyout kept");}
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
case 6:{Command('D');if(halted)return;if(!downloadPanel.Visible()||!IsWindowVisible(downloadPanel.Window())){Fail(L"Smoke download history did not open",E_FAIL);return;}downloadPanel.Hide(false);if(downloadPanel.Visible()||chrome.latched){Fail(L"Smoke download history did not close",E_FAIL);return;}Log(L"SMOKE native download history opened and closed");
    auto cb=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([](HRESULT hr,LPCWSTR)->HRESULT{Check(hr,L"Smoke download script");return S_OK;});
    HRESULT hr=Active()->view->ExecuteScript(L"(()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob(['browser smoke download'],{type:'text/plain'}));a.download='browser-smoke.txt';document.body.appendChild(a);a.click();})()",cb);cb->Release();Check(hr,L"Smoke download ExecuteScript");break;}
case 7:Command('M');if(!IsZoomed(window)){Fail(L"Smoke maximize assertion",E_FAIL);return;}Command('M');if(IsZoomed(window)){Fail(L"Smoke restore assertion",E_FAIL);return;}Log(L"SMOKE native maximize/restore verified DPI="+std::to_wstring(GetDpiForWindow(window)));auto closed=Active();int closedIndex=current;Command('W');Command('U');if(!Active()||Active()->uri!=closed->uri||current!=closedIndex||pages.size()!=2||!closedTabs.empty()){Fail(L"Smoke reopen tab assertion",E_FAIL);return;}Command('W');Log(L"SMOKE reopen closed tab at original index verified");Command('L');if(!chrome.AssetsReady()||pages.size()!=1||GetFocus()!=address||Destination(L"deux mots")!=L"https://duckduckgo.com/?q=deux%20mots"){Fail(L"Smoke native controls assertion",E_FAIL);return;}Log(L"SMOKE PASS: blank #171717 home, bundled font families, 11 exported SVG icons, 2 tabs, HTTPS, document titles, back, forward, reload, close tab, address focus, search encoding");KillTimer(window,1);PostMessageW(window,WM_CLOSE,0,0);break;
}}

void RefreshTools(){auto tab=Active();chrome.navigationLoading=tab&&tab->loading.IsLoading();BuildChrome();if(!tooltip)return;static std::vector<int> previous;for(int id:previous){TOOLINFOW t{sizeof(t)};t.hwnd=window;t.uId=id;SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&t);}previous.clear();for(auto&r:chrome.regions){TOOLINFOW t{sizeof(t)};t.uFlags=TTF_SUBCLASS;t.hwnd=window;t.uId=r.id;t.rect={S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};t.lpszText=const_cast<wchar_t*>(r.tip);SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);previous.push_back(r.id);}}
HMENU CreateTabMenu(){
    HMENU menu=CreatePopupMenu();
    for(int i=0;i<(int)pages.size();++i){
        // Menu strings interpret '&' as a mnemonic. Keep page titles verbatim.
        const auto label=NativeUI::EscapeMenuText(pages[i]->title);
        AppendMenuW(menu,MF_STRING|(i==current?MF_CHECKED:0),1000+i,label.c_str());
    }
    return menu;
}
void TabMenu(){HMENU menu=CreateTabMenu();POINT p;GetCursorPos(&p);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);if(cmd>=1000)Select(cmd-1000);}
void ExtraCommand(int cmd){auto t=Active();switch(cmd){
case 900:TabMenu();break;
case 'm':ShowWindow(window,SW_MINIMIZE);break;
case 'M':ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);break;
case 'X':PostMessageW(window,WM_CLOSE,0,0);break;
case 'C':if(t&&!t->home&&!t->uri.empty()&&OpenClipboard(window)){auto bytes=(t->uri.size()+1)*sizeof(wchar_t);HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(mem){void* data=GlobalLock(mem);if(data){memcpy(data,t->uri.c_str(),bytes);GlobalUnlock(mem);EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,mem)){auto now=NativeChrome::Motion::Now();motion.CopySucceeded(now);chrome.lottie.CopySucceeded(now);InvalidateChrome();ScheduleMotion(now);}else GlobalFree(mem);}else GlobalFree(mem);}CloseClipboard();}break;
case 'D':if(downloadDismissClick){downloadDismissClick=false;break;}ToggleDownloads();break;
case 'P':MessageBoxW(window,L"La navigation privée n'est pas disponible dans cette version.\n\nLes onglets actuels utilisent le profil normal local (cookies et cache persistants). Aucun onglet privé n'a été ouvert.",L"Navigation privée — indisponible",MB_OK|MB_ICONINFORMATION);break;
case 'S':{HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,'T',L"Nouvel onglet\tCtrl+T");AppendMenuW(menu,MF_STRING,'W',L"Fermer cet onglet\tCtrl+W");AppendMenuW(menu,MF_STRING|((closedTabs.empty()||pages.size()>=128)?MF_GRAYED:0),'U',L"Rouvrir l'onglet fermé\tCtrl+Maj+T");AppendMenuW(menu,MF_STRING,900,L"Liste des onglets");AppendMenuW(menu,MF_STRING,'D',L"Téléchargements");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,'A',L"À propos de browser");POINT p;GetCursorPos(&p);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);if(selected)Command(selected);break;}
case 'A':MessageBoxW(window,L"browser\nInterface native issue du canvas Paper.\nRecherche : DuckDuckGo.\nProfil normal local, sans synchronisation.\nCtrl+L / Alt+D : adresse - F6 : page / barre\nCtrl+T/W : ouvrir / fermer - Ctrl+Maj+T : rouvrir\nCtrl+Tab : onglet suivant\nCtrl+1...8 : onglet - Ctrl+9 : dernier\nCtrl+Maj+PgPr\u00e9c/PgSuiv : d\u00e9placer l’onglet\nCtrl+F : rechercher - Alt+\u2190/\u2192 : historique\n\nLe menu regroupe les actions disponibles ; aucun réglage système n'est modifié.",L"À propos",MB_OK);break;
}}
LRESULT CALLBACK AddressProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_IME_STARTCOMPOSITION)addressKeys.SetComposing(true);
    if(msg==WM_IME_ENDCOMPOSITION)addressKeys.SetComposing(false);
    if(msg==WM_CHAR&&addressKeys.ConsumeChar(w))return 0;
    if(msg==WM_GETDLGCODE){LRESULT code=DefSubclassProc(h,msg,w,l)|DLGC_WANTCHARS|DLGC_WANTARROWS;auto message=(MSG*)l;if(message&&!addressKeys.composing&&(message->wParam==VK_RETURN||message->wParam==VK_ESCAPE))code|=DLGC_WANTMESSAGE;return code;}
    if(msg==WM_LBUTTONDOWN&&keyboardModality){keyboardModality=false;InvalidateRect(window,nullptr,FALSE);}
    if(msg==WM_KILLFOCUS){editing=false;ShowWindow(h,SW_HIDE);InvalidateRect(window,nullptr,FALSE);}
    if(msg==WM_KEYDOWN&&w==VK_ESCAPE&&addressKeys.HandleKey(VK_ESCAPE)){editing=false;ShowWindow(h,SW_HIDE);FocusPage();Update();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_RETURN&&addressKeys.HandleKey(VK_RETURN)){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);editing=false;ShowWindow(h,SW_HIDE);Navigate(Active(),Destination(s));FocusPage();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_TAB&&addressKeys.HandleKey(VK_TAB)){editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);keyboardFocus='R';keyboardModality=true;InvalidateRect(window,nullptr,FALSE);return 0;}
    return DefSubclassProc(h,msg,w,l);
}
void UpdateFont(){if(font)DeleteObject(font);font=CreateFontW(-S(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");SendMessageW(address,WM_SETFONT,(WPARAM)font,TRUE);}
LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM w,LPARAM l){LRESULT frameResult;if(NativeUI::HandleFrame(h,msg,w,l,frameResult))return frameResult;switch(msg){
case WM_CREATE:window=h;address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,h,nullptr,instance,nullptr);SendMessageW(address,EM_SETLIMITTEXT,8192,0);SendMessageW(address,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowSubclass(address,AddressProc,1,0);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,h,nullptr,instance,nullptr);return 0;
case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);int edge=S(4);if(!IsZoomed(h)){bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<S(3),bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}float x=p.x*96.f/GetDpiForWindow(h),y=p.y*96.f/GetDpiForWindow(h);int id=chrome.Hit(x,y);if(id=='M')return HTMAXBUTTON;if(y<44&&!id)return HTCAPTION;return HTCLIENT;}
case WM_NCLBUTTONDOWN:if(w==HTMAXBUTTON){Command('M');return 0;}break;
case WM_NCMOUSEMOVE:if(w==HTMAXBUTTON){UpdateInteraction('M',0);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,h,0};TrackMouseEvent(&track);return 0;}break;
case WM_NCMOUSELEAVE:UpdateInteraction(0,0);return 0;
case WM_NCLBUTTONDBLCLK:if(w==HTCAPTION){Command('M');return 0;}break;
case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)l;info->ptMinTrackSize={S(560),S(300)};MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};}return 0;}
case WM_DPICHANGED:{auto r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);UpdateFont();Layout();return 0;}
case WM_SIZE:motionMinimized=w==SIZE_MINIMIZED;if(!motionMinimized){Layout();downloadDrawer.Reposition();}else CloseDownloadSurfaces(false);ScheduleMotion(NativeChrome::Motion::Now());return 0;
case WM_MOVE:downloadDrawer.Reposition();return 0;
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PAINTSTRUCT paint;BeginPaint(h,&paint);std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto&t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));auto t=Active();D2D1_RECT_F dirty{paint.rcPaint.left*96.f/GetDpiForWindow(h),paint.rcPaint.top*96.f/GetDpiForWindow(h),paint.rcPaint.right*96.f/GetDpiForWindow(h),paint.rcPaint.bottom*96.f/GetDpiForWindow(h)};chrome.loading=t?t->loading.Sample(NativeChrome::Motion::Now(),motion.ReducedMotion()):NativeChrome::Loading::Visual{};{auto b=chrome.LoadingDirty();if(dirty.left<=b.left&&dirty.top<=b.top&&dirty.right>=b.right&&dirty.bottom>=b.bottom)paintedLoading=chrome.LoadingSignature(chrome.loading);}chrome.keyboardModality=keyboardModality;chrome.Paint(titles,current,t&&!t->home?t->uri:L"",editing,hover,GetFocus()==address?'L':GetFocus()==h?keyboardFocus:0,&dirty);EndPaint(h,&paint);if(editing)RedrawWindow(address,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);return 0;}
case WM_MOUSEMOVE:{if(tabDrag.tab){DragTab({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(tabDrag.active)return 0;}int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)<0)UnlockTabWidth();if(id!=hover)UpdateInteraction(id,pressed);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
case WM_MOUSELEAVE:UnlockTabWidth();UpdateInteraction(0,pressed);return 0;
case WM_LBUTTONDOWN:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));downloadDismissClick=id=='D'&&GetTickCount64()-std::max(downloadPanel.hiddenAt,downloadDrawer.hiddenAt)<150;if(id){keyboardFocus=0;keyboardModality=false;if(!NativeChrome::IsClose(id))SetFocus(h);SetCapture(h);UpdateInteraction(id,id);if(NativeChrome::IsTab(id)){tabDrag.tab=pages[NativeChrome::TabIndex(id)];tabDrag.start={GET_X_LPARAM(l),GET_Y_LPARAM(l)};tabDrag.grab=GET_X_LPARAM(l)*96.f/GetDpiForWindow(h)-chrome.strip.Surface(NativeChrome::TabIndex(id)).left;}}return 0;}
case WM_LBUTTONUP:{bool dragged=tabDrag.active;CancelTabDrag();int original=pressed;int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(GetCapture()==h)ReleaseCapture();UpdateInteraction(id,0);if(!dragged&&original&&original==id){if(NativeChrome::IsClose(id)){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}else if(NativeChrome::IsTab(id))Select(NativeChrome::TabIndex(id));else Command(id);}return 0;}
case WM_CAPTURECHANGED:case WM_CANCELMODE:CancelTabDrag();if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;
case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE){CancelTabDrag();if(GetCapture()==h)ReleaseCapture();UpdateInteraction(0,0);}return 0;
case WM_SETTINGCHANGE:motion.SetReducedMotion(NativeChrome::Motion::SystemReducedMotion());InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());return 0;
case WM_MBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)>=0){LockTabWidth();CloseAt(NativeChrome::TabIndex(id));}return 0;}
case WM_RBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)>=0){int tabIndex=NativeChrome::TabIndex(id);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,'W',L"Fermer cet onglet\tCtrl+W");AppendMenuW(menu,MF_STRING|((closedTabs.empty()||pages.size()>=128)?MF_GRAYED:0),'U',L"Rouvrir l'onglet fermé\tCtrl+Maj+T");POINT p;GetCursorPos(&p);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD,p.x,p.y,0,h,nullptr);DestroyMenu(menu);if(cmd=='W')CloseAt(tabIndex);else if(cmd=='U')Command(cmd);}else if(GET_Y_LPARAM(l)<S(44))TabMenu();return 0;}
case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<S(44)&&!pages.empty())Command(GET_WHEEL_DELTA_WPARAM(w)>0?'[':']');return 0;}
case WM_KEYDOWN:if(w==VK_ESCAPE&&tabDrag.tab){CancelTabDrag();if(GetCapture()==h)ReleaseCapture();UpdateInteraction(hover,0);return 0;}if(w==VK_TAB||((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus)||((w==VK_LEFT||w==VK_RIGHT)&&NativeChrome::IsTab(keyboardFocus)))keyboardModality=true;if(w==VK_TAB){auto order=chrome.FocusOrder();if(order.empty())return 0;auto it=std::find(order.begin(),order.end(),keyboardFocus);int index=it==order.end()?-1:(int)(it-order.begin());index=it==order.end()?(GetKeyState(VK_SHIFT)<0?(int)order.size()-1:0):(index+(GetKeyState(VK_SHIFT)<0?-1:1)+(int)order.size())%(int)order.size();keyboardFocus=order[index];InvalidateRect(h,nullptr,FALSE);return 0;}if((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus){if(NativeChrome::IsClose(keyboardFocus))CloseAt(NativeChrome::TabIndex(keyboardFocus));else if(NativeChrome::IsTab(keyboardFocus)){Select(NativeChrome::TabIndex(keyboardFocus));SetFocus(h);}else Command(keyboardFocus);return 0;}
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
case WM_APP+2:SmokeCheck();return 0;
case WM_APP+3:SmokeAdvance();return 0;
case FrameMessage:{if(l==NativeChrome::FrameClock::ClockLost){frameClock.Stop();frameClockReady=false;ScheduleMotion(NativeChrome::Motion::Now());return 0;}
    frameClock.Acknowledge();auto now=NativeChrome::Motion::Now();if(!halted&&!motionMinimized)InvalidateMotion(now);ScheduleMotion(now);return 0;}
case WM_TIMER:if(w==MotionTimerId){KillTimer(h,MotionTimerId);motionDeadline.reset();auto now=NativeChrome::Motion::Now();InvalidateMotion(now);ScheduleMotion(now);return 0;}if(w==1&&smoke){KillTimer(h,1);Fail(L"Smoke timeout after 60 seconds",HRESULT_FROM_WIN32(WAIT_TIMEOUT));}return 0;
case WM_CLOSE:SaveSession();SaveDownloads();DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,MotionTimerId);motionDeadline.reset();frameClock.Stop();frameClockReady=false;for(auto&t:pages){t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();}pages.clear();PostQuitMessage(0);return 0;
}return DefWindowProcW(h,msg,w,l);}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR args,int show){smoke=wcsstr(args,L"--smoke-test")!=nullptr;instance=hi;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 1;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);PathRemoveFileSpecW(path);base=path;Log(L"START browser");INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);paper=CreateSolidBrush(BG);WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"LisiereBrowser";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(101));wc.hbrBackground=paper;RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"browser",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,hi,nullptr);NativeUI::ConfigureFullClientFrame(window);if(FAILED(chrome.Init(window,base))){MessageBoxW(window,L"Initialisation graphique native impossible.",L"browser",MB_OK|MB_ICONERROR);return 3;}LoadDownloads();CreateDownloadPanel();frameClockReady=frameClock.Start(window,FrameMessage);motion.EnablePolish();chrome.animateSelection=true;chrome.SetMotion(&motion);UpdateFont();Layout();ShowWindow(window,show);UpdateWindow(window);if(smoke)SetTimer(window,1,60000,nullptr);
    loader=LoadLibraryExW((base+L"\\WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader)Fail(L"LoadLibrary WebView2Loader.dll",HRESULT_FROM_WIN32(GetLastError()));
    using CreateEnvironment=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=loader?reinterpret_cast<CreateEnvironment>(GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions")):nullptr;if(loader&&!create)Fail(L"GetProcAddress CreateEnvironment",HRESULT_FROM_WIN32(GetLastError()));
    if(create&&!halted){auto done=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{if(!Check(hr,L"CreateEnvironment callback"))return S_OK;if(!env){Fail(L"Environment null",E_POINTER);return S_OK;}environment.set(env);LPWSTR version=nullptr;if(Check(env->get_BrowserVersionString(&version),L"get_BrowserVersionString")&&version){Log(L"RUNTIME "+std::wstring(version));CoTaskMemFree(version);}RestoreSession();return S_OK;});HRESULT hr=create(nullptr,(base+L"\\profile").c_str(),nullptr,done);done->Release();Check(hr,L"CreateEnvironment");}
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){int cmd=Key((UINT)msg.wParam);if(cmd){keyboardModality=true;Command(cmd);continue;}}TranslateMessage(&msg);DispatchMessageW(&msg);}environment.set(nullptr);if(loader)FreeLibrary(loader);downloadPanel.Dispose();downloadDrawer.Dispose();chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return halted?2:0;
}
