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
#include <cwctype>
#include <cmath>
#include <dwmapi.h>
#include "chrome.h"
#include "native-ui.h"

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
struct Tab { Com<ICoreWebView2Controller> controller; Com<ICoreWebView2> view; Com<ICoreWebView2_15> faviconView; NativeChrome::FaviconState favicon; std::wstring title=L"Nouvel onglet",uri; bool closed=false,home=true; };
HWND window,tabs,address,status,back,forward,reload,add,closeTab,home;
HFONT font; HBRUSH paper; HINSTANCE instance; HMODULE loader;
Com<ICoreWebView2Environment> environment;
std::vector<std::shared_ptr<Tab>> pages; int current=-1; bool halted=false;
std::wstring base; const COLORREF BG=NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::canvas);
NativeChrome::Chrome chrome; NativeUI::AddressKeyState addressKeys; NativeChrome::Motion::State motion; int pressed=0; bool motionMinimized=false; constexpr UINT_PTR MotionTimerId=72; std::optional<NativeChrome::Motion::Time> motionDeadline; HWND tooltip; int hover=0,keyboardFocus=0; bool editing=false;
int S(float value){return (int)std::lround(value*GetDpiForWindow(window)/96.f);}
void RefreshTools();
void ExtraCommand(int cmd);
void InvalidateChrome(){RECT r;GetClientRect(window,&r);r.bottom=std::min(r.bottom,(LONG)S(84));InvalidateRect(window,&r,FALSE);}
void ScheduleMotion(NativeChrome::Motion::Time now){
    auto wake=motion.NextWake(now);if(halted||motionMinimized||!wake.delay){KillTimer(window,MotionTimerId);motionDeadline.reset();return;}
    auto due=now+*wake.delay;if(motionDeadline&&*motionDeadline<=due)return;
    KillTimer(window,MotionTimerId);motionDeadline=due;
    if(!SetTimer(window,MotionTimerId,(UINT)wake.delay->count(),nullptr)){motionDeadline.reset();motion.SetReducedMotion(true);motion.copy.Clear();motion.SetInteraction(hover,0,now);InvalidateChrome();}
}
void UpdateInteraction(int newHover,int newPressed){hover=newHover;pressed=newPressed;auto now=NativeChrome::Motion::Now();int group=NativeChrome::IsClose(hover)?NativeChrome::TabBase+NativeChrome::TabIndex(hover):hover;motion.SetInteraction(group,pressed==hover?pressed:0,now);InvalidateChrome();ScheduleMotion(now);}

constexpr UINT Shortcut=WM_APP+1;
bool smoke=false; int testStage=0;
void SmokeCheck();
void SmokeAdvance();

std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.c_str(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
void Log(const std::wstring& s){std::ofstream f(Utf8(base+L"\\prototype.log"),std::ios::app);f<<Utf8(s)<<"\n";if(smoke&&s.rfind(L"NAVIGATION OK",0)==0)PostMessageW(window,WM_APP+2,0,0);}
void Fail(const std::wstring& where,HRESULT hr){if(halted)return;halted=true;KillTimer(window,MotionTimerId);motionDeadline.reset();wchar_t code[32];swprintf(code,32,L"0x%08lX",(unsigned long)hr);auto msg=where+L" : "+code+L"\nOpérations arrêtées. Aucun correctif automatique.";Log(L"WEBVIEW2 ERROR "+msg);SetWindowTextW(status,msg.c_str());MessageBoxW(window,msg.c_str(),L"WebView2 — arrêt demandé",MB_OK|MB_ICONERROR);}
bool Check(HRESULT hr,const wchar_t* op){if(FAILED(hr)){Fail(op,hr);return false;}return !halted;}
std::shared_ptr<Tab> Active(){return current>=0&&current<(int)pages.size()?pages[current]:nullptr;}
void FocusAddress(){editing=true;ShowWindow(address,SW_SHOW);SetFocus(address);SendMessageW(address,EM_SETSEL,0,-1);InvalidateRect(window,nullptr,FALSE);}
void Layout(){if(!window)return;chrome.Resize();chrome.Build((int)pages.size(),current);auto r=chrome.edit;
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
void Update(){auto t=Active();if(!t)return;chrome.SetEnabled('C',!t->home&&!t->uri.empty());if(!editing)SetWindowTextW(address,t->home?L"":t->uri.c_str());SetWindowTextW(window,(t->title+L" — browser").c_str());chrome.Build((int)pages.size(),current);RefreshTools();InvalidateRect(window,nullptr,FALSE);}
void Select(int n){if(n<0||n>=(int)pages.size()||halted)return;editing=false;ShowWindow(address,SW_HIDE);current=n;motion.SelectTab(n+1,NativeChrome::Motion::Now());ScheduleMotion(NativeChrome::Motion::Now());for(int i=0;i<(int)pages.size();++i)if(pages[i]->controller.p)Check(pages[i]->controller->put_IsVisible(i==n&&!pages[i]->home),L"put_IsVisible");Update();auto t=Active();if(t->controller.p&&!t->home)Check(t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC),L"MoveFocus");}
const wchar_t* HomeHtml=LR"HTML(<!doctype html><html lang="fr"><meta charset="utf-8"><title>Nouvel onglet</title><style>html,body{margin:0;background:#171717;color:white;min-height:100%;color-scheme:dark}</style><body></body></html>)HTML";
std::wstring Destination(std::wstring s){auto first=s.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)return L"";s=s.substr(first,s.find_last_not_of(L" \t\r\n")-first+1);if(s==L"about:blank")return s;
    auto low=s;std::transform(low.begin(),low.end(),low.begin(),towlower);if(low.rfind(L"https://",0)==0||low.rfind(L"http://",0)==0)return s;
    if(s.find_first_of(L" \t")==std::wstring::npos && s.find(L"://")==std::wstring::npos && (s.find(L'.')!=std::wstring::npos||s.rfind(L"localhost",0)==0))return L"https://"+s;
    std::wstring q;for(unsigned char c:Utf8(s)){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')q+=(wchar_t)c;else{wchar_t b[4];swprintf(b,4,L"%%%02X",c);q+=b;}}return L"https://duckduckgo.com/?q="+q;
}
void NewTab(std::wstring uri=L"");
void Navigate(std::shared_ptr<Tab> t,const std::wstring& uri){if(!t||!t->view.p||halted)return;t->home=uri.empty();Check(uri.empty()?t->view->NavigateToString(HomeHtml):t->view->Navigate(uri.c_str()),uri.empty()?L"NavigateToString":L"Navigate");}
void CloseAt(int index){if(halted||index<0||index>=(int)pages.size())return;
    const bool active=index==current;const int next=NativeChrome::SelectionAfterClose((int)pages.size(),current,index);
    motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;auto t=pages[index];t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();pages.erase(pages.begin()+index);
    if(pages.empty()){current=-1;NewTab();}else if(active)Select(next);else{current=next;motion.SelectTab(current+1,NativeChrome::Motion::Now());Update();}
}
void Close(){CloseAt(current);}
void Command(int cmd){if(halted||!chrome.Enabled(cmd))return;auto t=Active();switch(cmd){case 'L':FocusAddress();break;case ']':if(!pages.empty())Select((current+1)%(int)pages.size());break;case '[':if(!pages.empty())Select((current+(int)pages.size()-1)%(int)pages.size());break;case 'T':NewTab();FocusAddress();break;case 'W':Close();break;case 'R':if(t&&t->view.p)Check(t->view->Reload(),L"Reload");break;case 'H':Navigate(t,L"");break;case 'B':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoBack(&yes);if(yes)Check(t->view->GoBack(),L"GoBack");}break;case 'F':if(t&&t->view.p){BOOL yes=FALSE;t->view->get_CanGoForward(&yes);if(yes)Check(t->view->GoForward(),L"GoForward");}break;default:ExtraCommand(cmd);break;}}
int Key(UINT key){bool ctrl=GetKeyState(VK_CONTROL)<0,alt=GetKeyState(VK_MENU)<0;if(ctrl&&key==VK_TAB)return GetKeyState(VK_SHIFT)<0?'[':']';if(ctrl&&(key=='L'||key=='T'||key=='W'||key=='R'))return key;if(key==VK_F5)return 'R';if(alt&&key==VK_LEFT)return 'B';if(alt&&key==VK_RIGHT)return 'F';return 0;}

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

void NewTab(std::wstring uri){if(halted||!environment.p)return;if(pages.size()>=128){MessageBoxW(window,L"Limite de 128 onglets atteinte.",L"browser",MB_OK);return;}motion.selection.Reset();motion.ClearInteractions();hover=pressed=keyboardFocus=0;auto t=std::make_shared<Tab>();pages.push_back(t);Select((int)pages.size()-1);std::weak_ptr<Tab> weak=t;
    auto done=Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>([weak,uri](HRESULT hr,ICoreWebView2Controller* c)->HRESULT{
        if(!Check(hr,L"CreateController callback"))return S_OK;auto t=weak.lock();if(!t||t->closed){if(c)c->Close();return S_OK;}if(!c){Fail(L"CreateController null",E_POINTER);return S_OK;}t->controller.set(c);if(!Check(c->get_CoreWebView2(&t->view.p),L"get_CoreWebView2"))return S_OK;
        EventRegistrationToken token{};
        IID faviconInterface;CLSIDFromString(L"{517B2D1D-7DAE-4A66-A4F4-10352FFB9518}",&faviconInterface);
        if(!Check(t->view->QueryInterface(faviconInterface,(void**)&t->faviconView.p),L"QueryInterface favicon"))return S_OK;
        auto faviconChanged=Callback<ICoreWebView2FaviconChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{auto tab=weak.lock();if(tab&&!tab->closed&&!halted)RequestFavicon(tab);return S_OK;});
        HRESULT faviconResult=t->faviconView->add_FaviconChanged(faviconChanged,&token);faviconChanged->Release();if(!Check(faviconResult,L"add_FaviconChanged"))return S_OK;
        auto starting=Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{auto tab=weak.lock();if(!tab||tab->closed||halted)return S_OK;LPWSTR uri=nullptr;if(!Check(args->get_Uri(&uri),L"NavigationStarting get_Uri"))return S_OK;tab->favicon.Navigation(uri?uri:L"");CoTaskMemFree(uri);InvalidateChrome();return S_OK;});
        HRESULT startingResult=t->view->add_NavigationStarting(starting,&token);starting->Release();if(!Check(startingResult,L"add_NavigationStarting favicon"))return S_OK;
        auto source=Callback<ICoreWebView2SourceChangedEventHandler,ICoreWebView2*,ICoreWebView2SourceChangedEventArgs*>([weak](ICoreWebView2* v,ICoreWebView2SourceChangedEventArgs*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_Source(&s),L"get_Source")&&s){t->uri=s;CoTaskMemFree(s);t->home=t->uri==L"about:blank";}if(t==Active())Update();return S_OK;});
        HRESULT result=t->view->add_SourceChanged(source,&token);source->Release();if(!Check(result,L"add_SourceChanged"))return S_OK;
        auto title=Callback<ICoreWebView2DocumentTitleChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{if(halted)return S_OK;auto t=weak.lock();if(!t||t->closed)return S_OK;LPWSTR s=nullptr;if(Check(v->get_DocumentTitle(&s),L"get_DocumentTitle")&&s){t->title=s;CoTaskMemFree(s);}if(t==Active())Update();return S_OK;});
        result=t->view->add_DocumentTitleChanged(title,&token);title->Release();if(!Check(result,L"add_DocumentTitleChanged"))return S_OK;
        auto nav=Callback<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* a)->HRESULT{if(halted)return S_OK;BOOL ok=FALSE;a->get_IsSuccess(&ok);auto t=weak.lock();if(!t||t->closed)return S_OK;if(!ok){COREWEBVIEW2_WEB_ERROR_STATUS error;a->get_WebErrorStatus(&error);Fail(L"NavigationCompleted WebErrorStatus="+std::to_wstring(error),E_FAIL);return S_OK;}RequestFavicon(t);if(halted)return S_OK;Log(L"NAVIGATION OK "+t->uri);if(t==Active()){Check(t->controller->put_IsVisible(!t->home),L"put_IsVisible navigation");SetWindowTextW(status,L"Prêt · Ctrl+L pour une adresse ou une recherche");Update();}return S_OK;});
        result=t->view->add_NavigationCompleted(nav,&token);nav->Release();if(!Check(result,L"add_NavigationCompleted"))return S_OK;
        auto popup=Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{if(halted)return S_OK;a->put_Handled(TRUE);LPWSTR uri=nullptr;if(Check(a->get_Uri(&uri),L"NewWindow get_Uri")&&uri){std::wstring s=uri;CoTaskMemFree(uri);if(s.rfind(L"https://",0)==0||s.rfind(L"http://",0)==0)NewTab(s);}return S_OK;});
        result=t->view->add_NewWindowRequested(popup,&token);popup->Release();if(!Check(result,L"add_NewWindowRequested"))return S_OK;
        auto key=Callback<ICoreWebView2AcceleratorKeyPressedEventHandler,ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs*>([](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* a)->HRESULT{if(halted)return S_OK;COREWEBVIEW2_KEY_EVENT_KIND kind;a->get_KeyEventKind(&kind);UINT vk;a->get_VirtualKey(&vk);if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN){int cmd=Key(vk);if(cmd){a->put_Handled(TRUE);PostMessageW(window,Shortcut,cmd,0);}}return S_OK;});
        result=c->add_AcceleratorKeyPressed(key,&token);key->Release();if(!Check(result,L"add_AcceleratorKeyPressed"))return S_OK;
        auto failed=Callback<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>([](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* a)->HRESULT{COREWEBVIEW2_PROCESS_FAILED_KIND kind;a->get_ProcessFailedKind(&kind);Fail(L"ProcessFailed kind="+std::to_wstring(kind),E_FAIL);return S_OK;});
        result=t->view->add_ProcessFailed(failed,&token);failed->Release();if(!Check(result,L"add_ProcessFailed"))return S_OK;
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
case 6:{ICoreWebView2_9* downloads=nullptr;IID iid;CLSIDFromString(L"{4d7b2eab-9fdc-468d-b998-a9260b5ed651}",&iid);if(!Check(Active()->view->QueryInterface(iid,(void**)&downloads),L"Smoke downloads interface"))return;Com<ICoreWebView2_9> owner;owner.p=downloads;Command('D');if(halted)return;BOOL open=FALSE;if(!Check(downloads->get_IsDefaultDownloadDialogOpen(&open),L"Smoke download dialog state"))return;if(!open){Fail(L"Smoke download dialog did not open",E_FAIL);return;}if(!Check(downloads->CloseDefaultDownloadDialog(),L"Smoke close download dialog"))return;Log(L"SMOKE native downloads opened and closed");}
Command('M');if(!IsZoomed(window)){Fail(L"Smoke maximize assertion",E_FAIL);return;}Command('M');if(IsZoomed(window)){Fail(L"Smoke restore assertion",E_FAIL);return;}Log(L"SMOKE native maximize/restore verified DPI="+std::to_wstring(GetDpiForWindow(window)));Command('W');Command('L');if(!chrome.AssetsReady()||pages.size()!=1||GetFocus()!=address||Destination(L"deux mots")!=L"https://duckduckgo.com/?q=deux%20mots"){Fail(L"Smoke native controls assertion",E_FAIL);return;}Log(L"SMOKE PASS: blank #171717 home, bundled font families, 11 exported SVG icons, 2 tabs, HTTPS, document titles, back, forward, reload, close tab, address focus, search encoding");KillTimer(window,1);PostMessageW(window,WM_CLOSE,0,0);break;
}}

void RefreshTools(){if(!tooltip)return;static std::vector<int> previous;for(int id:previous){TOOLINFOW t{sizeof(t)};t.hwnd=window;t.uId=id;SendMessageW(tooltip,TTM_DELTOOLW,0,(LPARAM)&t);}previous.clear();for(auto&r:chrome.regions){TOOLINFOW t{sizeof(t)};t.uFlags=TTF_SUBCLASS;t.hwnd=window;t.uId=r.id;t.rect={S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};t.lpszText=const_cast<wchar_t*>(r.tip);SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);previous.push_back(r.id);}}
void TabMenu(){HMENU menu=CreatePopupMenu();for(int i=0;i<(int)pages.size();++i)AppendMenuW(menu,MF_STRING|(i==current?MF_CHECKED:0),1000+i,pages[i]->title.c_str());POINT p;GetCursorPos(&p);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);if(cmd>=1000)Select(cmd-1000);}
void ExtraCommand(int cmd){auto t=Active();switch(cmd){
case 900:TabMenu();break;
case 'm':ShowWindow(window,SW_MINIMIZE);break;
case 'M':ShowWindow(window,IsZoomed(window)?SW_RESTORE:SW_MAXIMIZE);break;
case 'X':PostMessageW(window,WM_CLOSE,0,0);break;
case 'C':if(t&&!t->home&&!t->uri.empty()&&OpenClipboard(window)){auto bytes=(t->uri.size()+1)*sizeof(wchar_t);HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(mem){void* data=GlobalLock(mem);if(data){memcpy(data,t->uri.c_str(),bytes);GlobalUnlock(mem);EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,mem)){auto now=NativeChrome::Motion::Now();motion.CopySucceeded(now);InvalidateChrome();ScheduleMotion(now);}else GlobalFree(mem);}else GlobalFree(mem);}CloseClipboard();}break;
case 'D':if(t&&t->view.p){ICoreWebView2_9* view9=nullptr;IID iid;CLSIDFromString(L"{4d7b2eab-9fdc-468d-b998-a9260b5ed651}",&iid);if(Check(t->view->QueryInterface(iid,(void**)&view9),L"QueryInterface downloads")){Check(view9->OpenDefaultDownloadDialog(),L"OpenDefaultDownloadDialog");view9->Release();}}break;
case 'P':MessageBoxW(window,L"La navigation privée n'est pas disponible dans cette version.\n\nLes onglets actuels utilisent le profil normal local (cookies et cache persistants). Aucun onglet privé n'a été ouvert.",L"Navigation privée — indisponible",MB_OK|MB_ICONINFORMATION);break;
case 'S':{HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,'T',L"Nouvel onglet\tCtrl+T");AppendMenuW(menu,MF_STRING,'W',L"Fermer cet onglet\tCtrl+W");AppendMenuW(menu,MF_STRING,900,L"Liste des onglets");AppendMenuW(menu,MF_STRING,'D',L"Téléchargements");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,'A',L"À propos de browser");POINT p;GetCursorPos(&p);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);if(selected)Command(selected);break;}
case 'A':MessageBoxW(window,L"browser\nInterface native issue du canvas Paper.\nRecherche : DuckDuckGo.\nProfil normal local, sans synchronisation.\nCtrl+L/T/W · Ctrl+Tab · Alt+←/→\n\nLe menu regroupe les actions disponibles ; aucun réglage système n'est modifié.",L"À propos",MB_OK);break;
}}
LRESULT CALLBACK AddressProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_IME_STARTCOMPOSITION)addressKeys.SetComposing(true);
    if(msg==WM_IME_ENDCOMPOSITION)addressKeys.SetComposing(false);
    if(msg==WM_CHAR&&addressKeys.ConsumeChar(w))return 0;
    if(msg==WM_GETDLGCODE){LRESULT code=DefSubclassProc(h,msg,w,l)|DLGC_WANTCHARS|DLGC_WANTARROWS;auto message=(MSG*)l;if(message&&!addressKeys.composing&&(message->wParam==VK_RETURN||message->wParam==VK_ESCAPE))code|=DLGC_WANTMESSAGE;return code;}
    if(msg==WM_KILLFOCUS){editing=false;ShowWindow(h,SW_HIDE);InvalidateRect(window,nullptr,FALSE);}
    if(msg==WM_KEYDOWN&&w==VK_ESCAPE&&addressKeys.HandleKey(VK_ESCAPE)){editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);Update();return 0;}
    if(msg==WM_KEYDOWN&&w==VK_RETURN&&addressKeys.HandleKey(VK_RETURN)){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);Navigate(Active(),Destination(s));return 0;}
    if(msg==WM_KEYDOWN&&w==VK_TAB&&addressKeys.HandleKey(VK_TAB)){editing=false;ShowWindow(h,SW_HIDE);SetFocus(window);keyboardFocus='R';InvalidateRect(window,nullptr,FALSE);return 0;}
    return DefSubclassProc(h,msg,w,l);
}
void UpdateFont(){if(font)DeleteObject(font);font=CreateFontW(-S(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");SendMessageW(address,WM_SETFONT,(WPARAM)font,TRUE);}
LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM w,LPARAM l){LRESULT frameResult;if(NativeUI::HandleFrame(h,msg,w,l,frameResult))return frameResult;switch(msg){
case WM_CREATE:window=h;address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,h,nullptr,instance,nullptr);SendMessageW(address,EM_SETLIMITTEXT,8192,0);SendMessageW(address,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowSubclass(address,AddressProc,1,0);status=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,h,nullptr,instance,nullptr);tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,h,nullptr,instance,nullptr);return 0;
case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);int edge=S(4);if(!IsZoomed(h)){bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<S(3),bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}float x=p.x*96.f/GetDpiForWindow(h),y=p.y*96.f/GetDpiForWindow(h);int id=chrome.Hit(x,y);if(id=='M')return HTMAXBUTTON;if(y<44&&!id)return HTCAPTION;return HTCLIENT;}
case WM_NCLBUTTONDOWN:if(w==HTMAXBUTTON){Command('M');return 0;}break;
case WM_NCMOUSEMOVE:if(w==HTMAXBUTTON){UpdateInteraction('M',0);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,h,0};TrackMouseEvent(&track);return 0;}break;
case WM_NCMOUSELEAVE:UpdateInteraction(0,0);return 0;
case WM_NCLBUTTONDBLCLK:if(w==HTCAPTION){Command('M');return 0;}break;
case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)l;info->ptMinTrackSize={S(560),S(300)};MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};}return 0;}
case WM_DPICHANGED:{auto r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);UpdateFont();Layout();return 0;}
case WM_SIZE:motionMinimized=w==SIZE_MINIMIZED;if(!motionMinimized)Layout();ScheduleMotion(NativeChrome::Motion::Now());return 0;
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PAINTSTRUCT paint;BeginPaint(h,&paint);std::vector<std::wstring> titles;std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto&t:pages){titles.push_back(t->title);icons.push_back(t->favicon.image);}chrome.SetFavicons(std::move(icons));auto t=Active();D2D1_RECT_F dirty{paint.rcPaint.left*96.f/GetDpiForWindow(h),paint.rcPaint.top*96.f/GetDpiForWindow(h),paint.rcPaint.right*96.f/GetDpiForWindow(h),paint.rcPaint.bottom*96.f/GetDpiForWindow(h)};chrome.Paint(titles,current,t&&!t->home?t->uri:L"",editing,hover,GetFocus()==address?'L':GetFocus()==h?keyboardFocus:0,&dirty);EndPaint(h,&paint);return 0;}
case WM_MOUSEMOVE:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(id!=hover)UpdateInteraction(id,pressed);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
case WM_MOUSELEAVE:UpdateInteraction(0,pressed);return 0;
case WM_LBUTTONDOWN:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(id){keyboardFocus=0;if(!NativeChrome::IsClose(id))SetFocus(h);SetCapture(h);UpdateInteraction(id,id);}return 0;}
case WM_LBUTTONUP:{int original=pressed;int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(GetCapture()==h)ReleaseCapture();UpdateInteraction(id,0);if(original&&original==id){if(NativeChrome::IsClose(id))CloseAt(NativeChrome::TabIndex(id));else if(NativeChrome::IsTab(id))Select(NativeChrome::TabIndex(id));else Command(id);}return 0;}
case WM_CAPTURECHANGED:case WM_CANCELMODE:UpdateInteraction(hover,0);return 0;
case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE){if(GetCapture()==h)ReleaseCapture();UpdateInteraction(0,0);}return 0;
case WM_SETTINGCHANGE:motion.SetReducedMotion(NativeChrome::Motion::SystemReducedMotion());InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());return 0;
case WM_MBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)>=0)CloseAt(NativeChrome::TabIndex(id));return 0;}
case WM_RBUTTONUP:{int id=chrome.Hit(GET_X_LPARAM(l)*96.f/GetDpiForWindow(h),GET_Y_LPARAM(l)*96.f/GetDpiForWindow(h));if(NativeChrome::TabIndex(id)>=0){int tabIndex=NativeChrome::TabIndex(id);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,'W',L"Fermer cet onglet\tCtrl+W");POINT p;GetCursorPos(&p);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD,p.x,p.y,0,h,nullptr);DestroyMenu(menu);if(cmd=='W')CloseAt(tabIndex);}else if(GET_Y_LPARAM(l)<S(44))TabMenu();return 0;}
case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<S(44)&&!pages.empty())Command(GET_WHEEL_DELTA_WPARAM(w)>0?'[':']');return 0;}
case WM_KEYDOWN:if(w==VK_TAB){auto order=chrome.FocusOrder();if(order.empty())return 0;auto it=std::find(order.begin(),order.end(),keyboardFocus);int index=it==order.end()?-1:(int)(it-order.begin());index=it==order.end()?(GetKeyState(VK_SHIFT)<0?(int)order.size()-1:0):(index+(GetKeyState(VK_SHIFT)<0?-1:1)+(int)order.size())%(int)order.size();keyboardFocus=order[index];InvalidateRect(h,nullptr,FALSE);return 0;}if((w==VK_RETURN||w==VK_SPACE)&&keyboardFocus){if(NativeChrome::IsClose(keyboardFocus))CloseAt(NativeChrome::TabIndex(keyboardFocus));else if(NativeChrome::IsTab(keyboardFocus)){Select(NativeChrome::TabIndex(keyboardFocus));SetFocus(h);}else Command(keyboardFocus);return 0;}
    if((w==VK_LEFT||w==VK_RIGHT)&&NativeChrome::IsTab(keyboardFocus)&&!pages.empty()){int next=(NativeChrome::TabIndex(keyboardFocus)+(w==VK_LEFT?-1:1)+(int)pages.size())%(int)pages.size();keyboardFocus=NativeChrome::TabBase+next;Select(next);SetFocus(h);return 0;}break;
case WM_CTLCOLOREDIT:{static HBRUSH field=CreateSolidBrush(NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::raised));SetBkColor((HDC)w,NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::raised));SetTextColor((HDC)w,NativeChrome::Design::NativeGray(NativeChrome::Design::Surface::raised+(1-NativeChrome::Design::Surface::raised)*NativeChrome::Design::Text::address));return (LRESULT)field;}
case Shortcut:Command((int)w);return 0;
case WM_APP+2:SmokeCheck();return 0;
case WM_APP+3:SmokeAdvance();return 0;
case WM_TIMER:if(w==MotionTimerId){KillTimer(h,MotionTimerId);motionDeadline.reset();InvalidateChrome();ScheduleMotion(NativeChrome::Motion::Now());return 0;}if(w==1&&smoke){KillTimer(h,1);Fail(L"Smoke timeout after 60 seconds",HRESULT_FROM_WIN32(WAIT_TIMEOUT));}return 0;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,MotionTimerId);motionDeadline.reset();for(auto&t:pages){t->closed=true;t->favicon.Close();if(t->controller.p)t->controller->Close();}pages.clear();PostQuitMessage(0);return 0;
}return DefWindowProcW(h,msg,w,l);}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR args,int show){smoke=wcsstr(args,L"--smoke-test")!=nullptr;instance=hi;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 1;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);PathRemoveFileSpecW(path);base=path;Log(L"START browser");INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);paper=CreateSolidBrush(BG);WNDCLASSW wc{};wc.hInstance=hi;wc.lpfnWndProc=WndProc;wc.lpszClassName=L"LisiereBrowser";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(hi,MAKEINTRESOURCEW(101));wc.hbrBackground=paper;RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"browser",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,hi,nullptr);NativeUI::ConfigureFullClientFrame(window);if(FAILED(chrome.Init(window,base))){MessageBoxW(window,L"Initialisation graphique native impossible.",L"browser",MB_OK|MB_ICONERROR);return 3;}motion.EnablePolish();chrome.animateSelection=true;chrome.SetMotion(&motion);UpdateFont();Layout();ShowWindow(window,show);UpdateWindow(window);if(smoke)SetTimer(window,1,60000,nullptr);
    loader=LoadLibraryExW((base+L"\\WebView2Loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader)Fail(L"LoadLibrary WebView2Loader.dll",HRESULT_FROM_WIN32(GetLastError()));
    using CreateEnvironment=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=loader?reinterpret_cast<CreateEnvironment>(GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions")):nullptr;if(loader&&!create)Fail(L"GetProcAddress CreateEnvironment",HRESULT_FROM_WIN32(GetLastError()));
    if(create&&!halted){auto done=Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{if(!Check(hr,L"CreateEnvironment callback"))return S_OK;if(!env){Fail(L"Environment null",E_POINTER);return S_OK;}environment.set(env);LPWSTR version=nullptr;if(Check(env->get_BrowserVersionString(&version),L"get_BrowserVersionString")&&version){Log(L"RUNTIME "+std::wstring(version));CoTaskMemFree(version);}NewTab();return S_OK;});HRESULT hr=create(nullptr,(base+L"\\profile").c_str(),nullptr,done);done->Release();Check(hr,L"CreateEnvironment");}
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){int cmd=Key((UINT)msg.wParam);if(cmd){Command(cmd);continue;}}TranslateMessage(&msg);DispatchMessageW(&msg);}environment.set(nullptr);if(loader)FreeLibrary(loader);chrome.Dispose();DeleteObject(font);DeleteObject(paper);for(auto name:NativeChrome::FontFiles)RemoveFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);CoUninitialize();return halted?2:0;
}
