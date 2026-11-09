// Host integration of the Paper « Éléments & popups » components (page p-D-0).
// Part of main.cpp (included once, after the HTTPS interstitial section): it uses the
// browser's globals and owns the WebView2 plumbing for script dialogs, permissions,
// HTTP authentication, external protocols, find in page, network error pages,
// fullscreen, the A-family confirmations and the internal debug sandbox.
//
// Rules held here (design/missing-elements-contracts.md):
// - every WebView2 deferral is completed exactly once (DeferralGuard), also when the tab
//   closes, the browser halts or the component is aborted;
// - a response is only ever written into the event args of the request that asked for it
//   (args are bound to their document), and prompts only show for the active tab;
// - the sandbox bridge accepts messages only from the sandbox tab, top-level source equal
//   to DebugSandbox::Url, with this launch's token and an allowlisted command.
#pragma once
#include "modal.h"
#include "origin-prompt.h"
#include "password-demo.h"
#include "find-bar.h"
#include "error-pages.h"
#include "context-toast.h"
#include "tooltip.h"
#include "fullscreen-hint.h"
#include "debug-sandbox.h"
#include "shortcuts-panel.h"
#include <deque>
#include <shellapi.h>
#include <netlistmgr.h>

namespace MD=NativeChrome::Modal;
namespace OP=NativeChrome::OriginPrompt;

// ---------------------------------------------------------------------------------------
// Deferrals: completed exactly once, whatever path ends the request.
struct DeferralGuard {
    ICoreWebView2Deferral* deferral=nullptr;
    explicit DeferralGuard(ICoreWebView2Deferral* d=nullptr):deferral(d){}
    DeferralGuard(const DeferralGuard&)=delete; DeferralGuard& operator=(const DeferralGuard&)=delete;
    void Complete(){if(!deferral)return;auto d=deferral;deferral=nullptr;d->Complete();d->Release();}
    ~DeferralGuard(){Complete();}
};
template<class T> struct ComRef { // shared owning reference to event args kept beyond the handler
    T* p=nullptr; ComRef()=default; explicit ComRef(T* q):p(q){if(p)p->AddRef();}
    ComRef(const ComRef& o):p(o.p){if(p)p->AddRef();} ComRef& operator=(const ComRef& o){if(o.p)o.p->AddRef();if(p)p->Release();p=o.p;return *this;}
    ~ComRef(){if(p)p->Release();} T* operator->()const{return p;}
};
std::wstring TakeString(LPWSTR s){std::wstring r=s?s:L"";CoTaskMemFree(s);return r;}
// « exemple.com » for a page address; « Cette page » for local documents.
std::wstring OriginLabel(const std::wstring& uri){
    const auto scheme=uri.find(L"://");if(scheme==std::wstring::npos)return L"Cette page";
    const auto start=scheme+3;auto end=uri.find_first_of(L"/?#",start);if(end==std::wstring::npos)end=uri.size();
    std::wstring host=uri.substr(start,end-start);const auto at=host.rfind(L'@');if(at!=std::wstring::npos)host.erase(0,at+1);
    if(host.rfind(L"www.",0)==0)host.erase(0,4);
    return host.empty()?L"Cette page":host;
}
std::wstring OriginOf(const std::wstring& uri){ // scheme://host[:port], exact comparison key
    const auto scheme=uri.find(L"://");if(scheme==std::wstring::npos)return uri;
    auto end=uri.find_first_of(L"/?#",scheme+3);return uri.substr(0,end==std::wstring::npos?uri.size():end);
}

// ---------------------------------------------------------------------------------------
// Geometry handed to the components (screen pixels).
RECT PageScreenRect(){
    auto c=ContentBox();RECT r{S(c.left),S(c.top),S(c.right),S(c.bottom)};
    if(window)MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&r),2);return r;
}
RECT WindowScreenRect(){RECT r{};if(window)GetWindowRect(window,&r);return r;}
RECT OriginAnchorRect(){
    auto i=chrome.indicator;if(i.right<=i.left||i.bottom<=i.top)i=chrome.AddressSlot();
    RECT r{S(i.left),S(i.top),S(i.right),S(i.bottom)};if(window)MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&r),2);return r;
}

// ---------------------------------------------------------------------------------------
// Components (created in CreateElementSurfaces, disposed before CoUninitialize).
MD::Dialog modal; OP::Prompt originPrompt; NativeChrome::FindBar::Bar findBar;
NativeChrome::ContextToast::Toast contextToast; NativeChrome::FullscreenHint::Hint fullscreenHint;
NativeChrome::PasswordDemo::Window passwordDemo; NativeChrome::ShortcutsPanel::Panel shortcutsPanel; NativeChrome::Tooltip::Tip sandboxTip; NativeChrome::Tooltip::Tip chromeTip;
void RequestElementFrame(){ScheduleMotion(NativeChrome::Motion::Now());}

// ---------------------------------------------------------------------------------------
// Modal queue. One modal at a time; a tab-scoped job waits until its tab is active and is
// cancelled (done with button -1) when the tab closes.
struct ModalJob { MD::Spec spec; std::weak_ptr<Tab> tab; bool scoped=false; std::function<void(const MD::Result&)> done; };
std::deque<ModalJob> modalQueue; ModalJob modalActive; bool modalOpen=false;
void PumpModal();
void FinishModal(const MD::Result& r){
    if(!modalOpen)return;modalOpen=false;auto done=std::move(modalActive.done);modalActive=ModalJob{};
    if(done)done(r);
    Defer([]{PumpModal();});
}
void PumpModal(){
    if(halted||modalOpen||modalQueue.empty()||!modal.Window())return;
    for(auto it=modalQueue.begin();it!=modalQueue.end();++it){
        if(it->scoped){auto t=it->tab.lock();if(!t||t->closed){auto done=std::move(it->done);modalQueue.erase(it);if(done)done(MD::Result{});PumpModal();return;}if(t!=Active())continue;}
        ModalJob job=std::move(*it);modalQueue.erase(it);
        modalActive=std::move(job);modalOpen=true;
        if(IsIconic(window))ShowWindow(window,SW_RESTORE);
        auto spec=modalActive.spec; // the active job keeps its own copy for logs
        if(!modal.Show(std::move(spec),PageScreenRect(),[](const MD::Result& r){FinishModal(r);})){
            Log(L"MODAL show failed: "+modalActive.spec.automationId);modalOpen=false;auto done=std::move(modalActive.done);modalActive=ModalJob{};if(done)done(MD::Result{});
        }
        return;
    }
}
void AskModal(MD::Spec spec,const std::shared_ptr<Tab>& tab,std::function<void(const MD::Result&)> done){
    if(halted||!modal.Window()){if(done)done(MD::Result{});return;}
    ModalJob job;job.spec=std::move(spec);job.tab=tab;job.scoped=tab!=nullptr;job.done=std::move(done);
    modalQueue.push_back(std::move(job));PumpModal();
}
// Tab closed, crashed or left: its dialogs end as cancelled (deferrals complete, no answer).
void AbortTabModals(const std::shared_ptr<Tab>& t){
    for(auto it=modalQueue.begin();it!=modalQueue.end();){auto q=it->tab.lock();if(it->scoped&&(!q||q==t)){auto done=std::move(it->done);it=modalQueue.erase(it);if(done)done(MD::Result{});}else ++it;}
    if(modalOpen&&modalActive.scoped){auto q=modalActive.tab.lock();if(!q||q==t)modal.Abort();}
}
// A tab switch: the active scoped modal is cancelled (its page answers "no"), others wait.
void ModalTabChanged(){
    if(modalOpen&&modalActive.scoped){auto q=modalActive.tab.lock();if(q!=Active())modal.Abort();}
    PumpModal();
}

// ---------------------------------------------------------------------------------------
// JavaScript dialogs (A2/A3/A4): ICoreWebView2::ScriptDialogOpening with the engine's
// default dialogs disabled (Settings::put_AreDefaultScriptDialogsEnabled(FALSE)).
std::set<unsigned> scriptDialogsBlocked; // tab serials whose page asked to stop dialogs (A2 checkbox)
HRESULT ScriptDialogOpening(const std::shared_ptr<Tab>& t,ICoreWebView2ScriptDialogOpeningEventArgs* a){
    if(!a)return S_OK;
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK; // no deferral: engine answers "cancel"
    auto deferral=std::make_shared<DeferralGuard>(raw);
    if(halted||!t||t->closed)return S_OK; // guard completes: dialog cancelled
    COREWEBVIEW2_SCRIPT_DIALOG_KIND kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;a->get_Kind(&kind);
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;a->get_Uri(&s);return s;}());
    const std::wstring message=TakeString([&]{LPWSTR s=nullptr;a->get_Message(&s);return s;}());
    const std::wstring defaultText=TakeString([&]{LPWSTR s=nullptr;a->get_DefaultText(&s);return s;}());
    if(kind!=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD&&scriptDialogsBlocked.count(t->serial)){Log(L"DIALOG suppressed for "+OriginLabel(uri));return S_OK;}
    const std::wstring origin=OriginLabel(uri);MD::Spec spec;
    switch(kind){
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT:spec=MD::AlertSpec(origin,message);break;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:spec=MD::ConfirmSpec(origin,message);break;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT:spec=MD::PromptSpec(origin,message,defaultText);break;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:spec=MD::BeforeUnloadSpec(origin,false);break;
    }
    Log(L"DIALOG kind="+std::to_wstring(kind)+L" "+origin);
    ComRef<ICoreWebView2ScriptDialogOpeningEventArgs> args(a);const unsigned serial=t->serial;
    AskModal(std::move(spec),t,[args,deferral,kind,serial](const MD::Result& r){
        if(r.checked&&kind!=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD)scriptDialogsBlocked.insert(serial);
        const bool accept=r.button==MD::AcceptButton;
        if(accept){if(kind==COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT)args->put_ResultText(r.values.empty()?L"":r.values[0].c_str());args->Accept();}
        Log(std::wstring(L"DIALOG ")+(accept?L"accepted":L"cancelled"));
        deferral->Complete();
    });
    return S_OK;
}

// ---------------------------------------------------------------------------------------
// Origin prompts (B01/B02 permissions, B06 HTTP auth + external application).
// Requests from one tab and origin arriving together (camera + microphone) share a prompt.
struct PromptJob {
    OP::Spec spec; std::weak_ptr<Tab> tab; std::wstring origin;
    std::vector<std::function<void(OP::Result&)>> answers; // one per merged request
};
std::deque<PromptJob> promptQueue; PromptJob promptActive; bool promptOpen=false;
void PumpPrompt();
void FinishPrompt(OP::Result& r){
    if(!promptOpen)return;promptOpen=false;auto job=std::move(promptActive);promptActive=PromptJob{};
    for(auto& answer:job.answers)answer(r);
    SecureZeroMemory(r.password.data(),r.password.size()*sizeof(wchar_t));
    Defer([]{PumpPrompt();});
}
void PumpPrompt(){
    if(halted||promptOpen||promptQueue.empty()||!originPrompt.Window())return;
    for(auto it=promptQueue.begin();it!=promptQueue.end();++it){
        auto t=it->tab.lock();
        if(!t||t->closed){auto job=std::move(*it);promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);PumpPrompt();return;}
        if(t!=Active())continue;
        promptActive=std::move(*it);promptQueue.erase(it);promptOpen=true;originPrompt.SetPageArea(PageScreenRect());
        if(!originPrompt.Show(promptActive.spec,OriginAnchorRect(),[](OP::Result& r){FinishPrompt(r);})){
            Log(L"PROMPT show failed");OP::Result none;FinishPrompt(none);
        }
        return;
    }
}
void AskPrompt(OP::Spec spec,const std::shared_ptr<Tab>& tab,std::function<void(OP::Result&)> answer,const std::wstring& key=L""){
    if(halted||!originPrompt.Window()||!tab){OP::Result none;answer(none);return;}
    const std::wstring origin=key.empty()?spec.origin:key;
    // Merge permission requests of the same tab and origin still waiting.
    if(spec.kind==OP::Kind::Permission){
        for(auto& q:promptQueue)if(q.spec.kind==OP::Kind::Permission&&q.tab.lock()==tab&&q.origin==origin){
            for(int p:spec.permissions)if(std::find(q.spec.permissions.begin(),q.spec.permissions.end(),p)==q.spec.permissions.end())q.spec.permissions.push_back(p);
            q.answers.push_back(std::move(answer));return;}
    }
    PromptJob job;job.spec=std::move(spec);job.tab=tab;job.origin=origin;job.answers.push_back(std::move(answer));
    promptQueue.push_back(std::move(job));
    // Let a sibling request of the same getUserMedia() call join before showing.
    Defer([]{PumpPrompt();});
}
void AbortTabPrompts(const std::shared_ptr<Tab>& t){
    for(auto it=promptQueue.begin();it!=promptQueue.end();){auto q=it->tab.lock();if(!q||q==t){auto job=std::move(*it);it=promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);}else ++it;}
    if(promptOpen){auto q=promptActive.tab.lock();if(!q||q==t)originPrompt.Abort();}
}
void PromptTabChanged(){if(promptOpen){auto q=promptActive.tab.lock();if(q!=Active())originPrompt.Abort();}PumpPrompt();}

HRESULT PermissionRequested(const std::shared_ptr<Tab>& t,ICoreWebView2PermissionRequestedEventArgs* a){
    if(!a)return S_OK;
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK;
    auto deferral=std::make_shared<DeferralGuard>(raw);
    ComRef<ICoreWebView2PermissionRequestedEventArgs> args(a);
    // Default UI never shows: whatever ends this request writes an explicit state.
    {Com<ICoreWebView2PermissionRequestedEventArgs2> a2;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs2,(void**)&a2.p))&&a2.p)a2->put_Handled(TRUE);}
    auto deny=[args](bool remember){args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        Com<ICoreWebView2PermissionRequestedEventArgs3> a3;if(SUCCEEDED(args->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs3,(void**)&a3.p))&&a3.p)a3->put_SavesInProfile(remember?TRUE:FALSE);};
    if(halted||!t||t->closed){deny(false);return S_OK;}
    COREWEBVIEW2_PERMISSION_KIND kind=COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;a->get_PermissionKind(&kind);
    BOOL user=FALSE;a->get_IsUserInitiated(&user);
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;a->get_Uri(&s);return s;}());
    OP::Spec spec;spec.kind=OP::Kind::Permission;spec.origin=OriginLabel(uri);spec.permissions={(int)kind};spec.userInitiated=user!=FALSE;
    const std::wstring key=OriginOf(uri);Log(L"PERMISSION request kind="+std::to_wstring(kind)+L" "+key);
    AskPrompt(std::move(spec),t,[args,deferral,deny,kind](OP::Result& r){
        Com<ICoreWebView2PermissionRequestedEventArgs3> a3;const bool has3=SUCCEEDED(args->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs3,(void**)&a3.p))&&a3.p;
        switch(r.action){
        case OP::Action::Allow:args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);if(has3)a3->put_SavesInProfile(r.remember?TRUE:FALSE);break;
        case OP::Action::AllowOnce:args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);if(has3)a3->put_SavesInProfile(FALSE);break;
        case OP::Action::Block:deny(r.remember);break;
        default:deny(false);break; // dismissed: no decision saved, the site may ask again
        }
        Log(L"PERMISSION kind="+std::to_wstring(kind)+L" action="+std::to_wstring((int)r.action)+(r.remember?L" saved":L""));
        deferral->Complete();
    },key);
    return S_OK;
}

HRESULT BasicAuthenticationRequested(const std::shared_ptr<Tab>& t,ICoreWebView2BasicAuthenticationRequestedEventArgs* a){
    if(!a)return S_OK;
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK;
    auto deferral=std::make_shared<DeferralGuard>(raw);ComRef<ICoreWebView2BasicAuthenticationRequestedEventArgs> args(a);
    if(halted||!t||t->closed){a->put_Cancel(TRUE);return S_OK;}
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;a->get_Uri(&s);return s;}());
    const std::wstring challenge=TakeString([&]{LPWSTR s=nullptr;a->get_Challenge(&s);return s;}());
    OP::Spec spec;spec.kind=OP::Kind::BasicAuth;spec.origin=OriginLabel(uri);spec.detail=challenge;spec.insecure=uri.rfind(L"http://",0)==0;
    Log(L"AUTH requested "+OriginOf(uri));
    AskPrompt(std::move(spec),t,[args,deferral](OP::Result& r){
        if(r.action==OP::Action::Submit){
            Com<ICoreWebView2BasicAuthenticationResponse> response;
            if(SUCCEEDED(args->get_Response(&response.p))&&response.p){response->put_UserName(r.user.c_str());response->put_Password(r.password.c_str());}
            else args->put_Cancel(TRUE);
        }else args->put_Cancel(TRUE);
        Log(std::wstring(L"AUTH ")+(r.action==OP::Action::Submit?L"submitted":L"cancelled")); // never the credentials
        deferral->Complete();
    });
    return S_OK;
}

// Schemes never handed to the shell from a web page, whatever the user answers.
bool ExternalSchemeRefused(const std::wstring& uri){
    const auto colon=uri.find(L':');if(colon==std::wstring::npos||colon==0)return true;
    std::wstring s=uri.substr(0,colon);std::transform(s.begin(),s.end(),s.begin(),towlower);
    for(auto bad:{L"file",L"javascript",L"vbscript",L"data",L"shell",L"ms-msdt",L"search-ms",L"search",L"ms-officecmd",L"ms-cxh",L"ms-cxh-full",L"res",L"mk",L"its",L"ms-its",L"hcp",L"jar"})if(s==bad)return true;
    return false;
}
HRESULT LaunchingExternalUriScheme(const std::shared_ptr<Tab>& t,ICoreWebView2LaunchingExternalUriSchemeEventArgs* a){
    if(!a)return S_OK;
    a->put_Cancel(TRUE); // our prompt replaces the engine's; launching is done here on « Ouvrir »
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK;
    auto deferral=std::make_shared<DeferralGuard>(raw);
    if(halted||!t||t->closed)return S_OK;
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;a->get_Uri(&s);return s;}());
    const std::wstring from=TakeString([&]{LPWSTR s=nullptr;a->get_InitiatingOrigin(&s);return s;}());
    if(ExternalSchemeRefused(uri)){Log(L"EXTERNAL refused scheme");return S_OK;}
    OP::Spec spec;spec.kind=OP::Kind::ExternalApp;spec.origin=OriginLabel(t->uri);spec.detail=uri;
    if(!from.empty()&&OriginOf(from)!=OriginOf(t->uri))spec.frame=OriginLabel(from); // a third-party frame asks
    BOOL user=FALSE;a->get_IsUserInitiated(&user);spec.userInitiated=user!=FALSE;
    Log(L"EXTERNAL request "+uri.substr(0,uri.find(L':')));
    AskPrompt(std::move(spec),t,[deferral,uri](OP::Result& r){
        deferral->Complete();
        if(r.action!=OP::Action::Open&&r.action!=OP::Action::Allow)return;
        Defer([uri]{auto h=(INT_PTR)ShellExecuteW(window,L"open",uri.c_str(),nullptr,nullptr,SW_SHOWNORMAL);Log(L"EXTERNAL launched result="+std::to_wstring(h));});
    });
    return S_OK;
}

// ---------------------------------------------------------------------------------------
// Find in page (C1/C2): ICoreWebView2Find with the engine's own find bar suppressed.
struct FindSession { std::weak_ptr<Tab> tab; Com<ICoreWebView2Find> find; EventRegistrationToken activeToken{},countToken{}; bool events=false; std::wstring term; NativeChrome::FindBar::Options options; } findSession;
void FindPublish(){
    if(!findSession.find.p||!findBar.Visible())return;
    INT32 active=-1,count=0;findSession.find->get_ActiveMatchIndex(&active);findSession.find->get_MatchCount(&count);
    using FS=NativeChrome::FindBar::State;
    if(findSession.term.empty()){findBar.SetResult(0,0,FS::Idle);return;}
    findBar.SetResult(count>0?std::max(1,(int)active):0,count,count>0?FS::Found:FS::NoMatch);
    {static std::wstring last;std::wstring line=L"FIND "+std::to_wstring(active)+L"/"+std::to_wstring(count);if(line!=last){last=line;Log(line);}}
}
void FindDetach(){
    if(findSession.find.p&&findSession.events){findSession.find->remove_ActiveMatchIndexChanged(findSession.activeToken);findSession.find->remove_MatchCountChanged(findSession.countToken);}
    findSession=FindSession{};
}
void FindStop(){if(findSession.find.p&&!halted)findSession.find->Stop();}
bool FindAttach(const std::shared_ptr<Tab>& t){
    if(findSession.find.p&&findSession.tab.lock()==t)return true;
    FindStop();FindDetach();
    if(!t||!t->view.p||!environment.p)return false;
    Com<ICoreWebView2_28> view;if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_28,(void**)&view.p))||!view.p)return false;
    if(FAILED(view->get_Find(&findSession.find.p))||!findSession.find.p)return false;
    findSession.tab=t;
    auto changed=[](){Defer([]{FindPublish();});return S_OK;};
    auto active=Callback<ICoreWebView2FindActiveMatchIndexChangedEventHandler,ICoreWebView2Find*,IUnknown*>([changed](ICoreWebView2Find*,IUnknown*)->HRESULT{return changed();});
    auto count=Callback<ICoreWebView2FindMatchCountChangedEventHandler,ICoreWebView2Find*,IUnknown*>([changed](ICoreWebView2Find*,IUnknown*)->HRESULT{return changed();});
    findSession.events=SUCCEEDED(findSession.find->add_ActiveMatchIndexChanged(active,&findSession.activeToken))&&SUCCEEDED(findSession.find->add_MatchCountChanged(count,&findSession.countToken));
    active->Release();count->Release();
    return true;
}
void FindQuery(const std::wstring& term,const NativeChrome::FindBar::Options& options){
    using FS=NativeChrome::FindBar::State;
    auto t=Active();if(!FindAttach(t)){findBar.SetResult(0,0,FS::Unavailable);return;}
    findSession.term=term;findSession.options=options;
    if(term.empty()){FindStop();findBar.SetResult(0,0,FS::Idle);return;}
    Com<ICoreWebView2Environment15> env;Com<ICoreWebView2FindOptions> o;
    if(FAILED(environment->QueryInterface(IID_ICoreWebView2Environment15,(void**)&env.p))||!env.p||FAILED(env->CreateFindOptions(&o.p))||!o.p){findBar.SetResult(0,0,FS::Unavailable);return;}
    o->put_FindTerm(term.c_str());o->put_IsCaseSensitive(options.matchCase?TRUE:FALSE);o->put_ShouldMatchWord(options.wholeWord?TRUE:FALSE);
    o->put_SuppressDefaultFindDialog(TRUE);o->put_ShouldHighlightAllMatches(TRUE);
    findBar.SetResult(0,0,FS::Searching);
    auto done=Callback<ICoreWebView2FindStartCompletedHandler,HRESULT>([term](HRESULT hr)->HRESULT{
        if(halted||findSession.term!=term)return S_OK;
        if(FAILED(hr)){Log(L"FIND start failed "+HrText(hr));findBar.SetResult(0,0,NativeChrome::FindBar::State::Unavailable);return S_OK;}
        FindPublish();return S_OK;});
    if(FAILED(findSession.find->Start(o.p,done)))findBar.SetResult(0,0,FS::Unavailable);
    done->Release();
}
void OpenFindBar(){
    auto t=Active();if(!t||!t->view.p||!PageShown(*t)||halted)return;
    if(!findBar.Window()){FindInPage();return;} // component unavailable: engine find
    findBar.Show(PageScreenRect(),findSession.tab.lock()==t?findSession.term:L"");findBar.Focus();
    if(findSession.tab.lock()==t&&!findSession.term.empty())FindQuery(findSession.term,findSession.options);
}
void CloseFindBar(bool focusPage){
    if(!findBar.Visible())return;findBar.Hide();FindStop();
    if(focusPage)FocusPage();
}
void FindStep(bool next){
    if(!findBar.Visible()||findSession.tab.lock()!=Active()){OpenFindBar();return;}
    if(findSession.find.p&&!findSession.term.empty()){if(next)findSession.find->FindNext();else findSession.find->FindPrevious();}
}
void FindTabChanged(){if(findBar.Visible()&&findSession.tab.lock()!=Active())CloseFindBar(false);}

// ---------------------------------------------------------------------------------------
// Network error pages (C3/C4): our document replaces the engine's error page, the
// address keeps the failed URL, « Réessayer » (or the network coming back, detected by the
// page's own `online` event) navigates to it again.
// Windows' own connectivity verdict (Network List Manager); unknown counts as online.
bool NetworkOnline(){
    Com<INetworkListManager> nlm;
    if(FAILED(CoCreateInstance(CLSID_NetworkListManager,nullptr,CLSCTX_ALL,IID_INetworkListManager,(void**)&nlm.p))||!nlm.p)return true;
    VARIANT_BOOL connected=VARIANT_TRUE;if(FAILED(nlm->IsConnectedToInternet(&connected)))return true;
    return connected!=VARIANT_FALSE;
}
// Inter Display as data: URIs for our internal documents (CSP allows font-src data:).
const std::wstring& PageFont(bool medium){
    static const std::wstring regular=DataUri(base+L"\\assets\\fonts\\InterDisplay-Regular.ttf",L"font/ttf",1024*1024);
    static const std::wstring bold=DataUri(base+L"\\assets\\fonts\\InterDisplay-Medium.ttf",L"font/ttf",1024*1024);
    return medium?bold:regular;
}
bool ErrorPageNavigationStarting(const std::shared_ptr<Tab>& t,const std::wstring& uri,UINT64 id){
    auto& e=t->errorPage;const bool blank=uri==L"about:blank"||uri.rfind(L"data:",0)==0;
    if(blank&&e.pending){e.pending=false;return false;}
    e.starting=uri;e.startingId=id;
    e.shown.clear();e.pending=false;e.token.clear();return false;
}
// Only the failure of the tab's latest top-level http(s) navigation (not an external
// protocol hand-off, not an older navigation) replaces the document.
bool ShowErrorPage(const std::shared_ptr<Tab>& t,COREWEBVIEW2_WEB_ERROR_STATUS error,UINT64 navigationId){
    if(halted||!t||t->closed||!t->view.p)return false;
    const std::wstring target=t->errorPage.starting;
    if(navigationId!=t->errorPage.startingId||(target.rfind(L"http://",0)!=0&&target.rfind(L"https://",0)!=0)||IsSandboxUri(target))return false;
    if(error==COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED||error==COREWEBVIEW2_WEB_ERROR_STATUS_VALID_AUTHENTICATION_CREDENTIALS_REQUIRED||error==COREWEBVIEW2_WEB_ERROR_STATUS_VALID_PROXY_AUTHENTICATION_REQUIRED)return false;
    // Certificate failures keep the engine's interstitial (its « continue » flow and details).
    switch(error){case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
        case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:return false;default:break;}
    GUID guid{};wchar_t token[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,token,40))return false;
    const bool online=error!=COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED&&NetworkOnline();
    auto& e=t->errorPage;
    const auto kind=e.forced>=0?(NativeChrome::ErrorPages::Kind)e.forced:NativeChrome::ErrorPages::Classify((int)error,online);
    NativeChrome::ErrorPages::Extras extras;extras.fontRegular=PageFont(false);extras.fontMedium=PageFont(true);extras.reducedMotion=motion.ReducedMotion();
    extras.code=NativeChrome::ErrorPages::Code((int)error);extras.restored=e.forcedRestored;
    const bool retrying=e.forcedRetrying;e.forced=-1;e.forcedRetrying=e.forcedRestored=false;
    t->uri=target;t->home=false;e.shown=target;e.token=token;e.pending=true;e.kind=(int)kind;
    const std::wstring html=NativeChrome::ErrorPages::Html(kind,target,token,retrying,extras);
    Log(L"ERROR PAGE kind="+std::to_wstring((int)kind)+L" status="+std::to_wstring(error));
    if(FAILED(t->view->NavigateToString(html.c_str()))){e=ErrorPageState{};return false;}
    return true;
}
bool ErrorPageMessage(const std::shared_ptr<Tab>& t,const std::wstring& source,const std::wstring& message){
    auto& e=t->errorPage;if(e.shown.empty()||e.pending||t->uri!=e.shown)return false;
    if(source!=L"about:blank"&&source.rfind(L"data:",0)!=0)return false;
    auto action=NativeChrome::ErrorPages::ParseMessage(message,e.token);if(!action)return false;
    const std::wstring target=e.shown;std::weak_ptr<Tab> weak=t;
    switch(*action){
    case NativeChrome::ErrorPages::Action::Retry:Log(L"ERROR PAGE retry");Defer([weak,target]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,target);});break;
    case NativeChrome::ErrorPages::Action::Back:Defer([weak]{auto tab=weak.lock();if(!tab||tab->closed||!tab->view.p)return;BOOL yes=FALSE;tab->view->get_CanGoBack(&yes);if(yes)tab->view->GoBack();else Navigate(tab,L"");});break;
    default:break;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Fullscreen (C6/C7): a page element (ContainsFullScreenElementChanged) or the whole
// browser (F11). The window covers its monitor and the chrome is hidden; the hint says how
// to leave (Échap for a page, F11 for the browser).
struct FullscreenState { bool page=false,browser=false; WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)}; bool saved=false; } fullscreen;
bool InFullscreen(){return fullscreen.page||fullscreen.browser;}
RECT MonitorScreenRect(){MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi);return mi.rcMonitor;}
void ApplyFullscreen(bool wasFull){
    const bool full=InFullscreen();
    if(full&&!wasFull){fullscreen.saved=GetWindowPlacement(window,&fullscreen.placement)!=FALSE;HideSuggestions();
        if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
        const RECT m=MonitorScreenRect();SetWindowPos(window,HWND_TOP,m.left,m.top,m.right-m.left,m.bottom-m.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER);}
    else if(!full&&wasFull){if(fullscreen.saved)SetWindowPlacement(window,&fullscreen.placement);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}
    Layout();
    if(full)fullscreenHint.Show(MonitorScreenRect(),fullscreen.page);else fullscreenHint.Hide();
    RequestElementFrame();
}
void SetPageFullscreen(const std::shared_ptr<Tab>& t,bool on){
    if(on&&t!=Active())return;const bool was=InFullscreen();fullscreen.page=on;
    Log(std::wstring(L"FULLSCREEN page ")+(on?L"on":L"off"));if(was!=InFullscreen()||on)ApplyFullscreen(was);
}
bool PageFullscreenActive(){auto t=Active();return fullscreen.page&&t&&t->pageFullscreen;}
// Échap / F11 during a page's fullscreen: the page leaves it (its fullscreenchange fires; the
// engine then raises ContainsFullScreenElementChanged, which restores the window).
void ExitPageFullscreen(){auto t=Active();if(t&&t->view.p&&t->pageFullscreen)t->view->ExecuteScript(L"document.fullscreenElement&&document.exitFullscreen()",nullptr);}
void ToggleBrowserFullscreen(){const bool was=InFullscreen();fullscreen.browser=!fullscreen.browser;Log(std::wstring(L"FULLSCREEN browser ")+(fullscreen.browser?L"on":L"off"));ApplyFullscreen(was);}
void FullscreenTabChanged(){ // the page in fullscreen is no longer shown: it leaves fullscreen
    if(!fullscreen.page)return;
    for(auto& p:pages)if(p&&p!=Active()&&p->view.p&&p->pageFullscreen){p->view->ExecuteScript(L"document.fullscreenElement&&document.exitFullscreen()",nullptr);}
}

// ---------------------------------------------------------------------------------------
// A-family confirmations, asynchronous (no nested modal loop).
void AnnounceDataCleared();
void StartClearBrowsingData(std::function<void(HRESULT)> done);
// A5: the user's categories and period. Engine data via ClearBrowsingDataInTimeRange; the
// browser's own traces (history, reopen stack, download rows) for the same period. Each
// category maps to engine kinds exactly; nothing outside the selection is touched.
std::int64_t ClearPeriodStartMs(int period){
    const std::int64_t now=NativeChrome::DownloadsUI::UnixNowMs();
    switch(period){case 0:return now-3600000LL;case 1:return now-86400000LL;case 2:return now-7*86400000LL;case 3:return now-28*86400000LL;default:return 0;}
}
void ClearLocalTracesSelected(const std::vector<bool>& c,std::int64_t since){
    auto on=[&](size_t i){return i<c.size()&&c[i];};
    if(on(0)){
        if(since<=0){closedTabs.clear();history.Clear();ClearSiteIcons();}
        else{std::vector<std::wstring> urls;for(auto& v:history.Items())if(v.last>=since)urls.push_back(v.url);for(auto& u:urls)history.Remove(u);closedTabs.clear();}
        SaveHistory();historyDrawer.Changed();
    }
    if(on(3)){
        downloads.erase(std::remove_if(downloads.begin(),downloads.end(),[since](const DL::Item& d){return d.state!=DL::State::InProgress&&d.state!=DL::State::Paused&&(since<=0||d.started>=since||d.started==0);}),downloads.end());
        for(auto it=downloadOps.begin();it!=downloadOps.end();)it=FindDownload(it->first)?std::next(it):downloadOps.erase(it);
        SaveDownloads();DownloadsChanged();
    }
}
void StartClearSelected(const std::vector<bool>& c,int period,std::function<void(HRESULT)> done){
    auto on=[&](size_t i){return i<c.size()&&c[i];};
    const std::int64_t since=ClearPeriodStartMs(period);
    ClearLocalTracesSelected(c,since);
    int kinds=0;
    if(on(0))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_BROWSING_HISTORY;
    if(on(1))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_SITE;
    if(on(2))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE;
    if(on(3))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_DOWNLOAD_HISTORY;
    if(on(4))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_SETTINGS;
    Log(L"SETTINGS clear selected kinds="+std::to_wstring(kinds)+L" period="+std::to_wstring(period));
    Com<ICoreWebView2Profile2> profile;
    if(!kinds||!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){if(kinds)Log(L"SETTINGS clear data: no ICoreWebView2Profile2; local traces only");if(done)done(S_OK);return;}
    auto handler=Callback<ICoreWebView2ClearBrowsingDataCompletedHandler,HRESULT>([done](HRESULT hr)->HRESULT{
        if(SUCCEEDED(hr))Log(L"SETTINGS browsing data cleared");else Log(L"SETTINGS clear data failed "+HrText(hr));if(done)done(hr);return S_OK;});
    const double end=NativeChrome::DownloadsUI::UnixNowMs()/1000.0+60,start=since<=0?0.0:since/1000.0;
    HRESULT hr=profile->ClearBrowsingDataInTimeRange((COREWEBVIEW2_BROWSING_DATA_KINDS)kinds,start,end,handler);handler->Release();
    if(FAILED(hr)){Log(L"SETTINGS ClearBrowsingDataInTimeRange failed "+HrText(hr));if(done)done(hr);}
}
// `demo`: the sandbox's run of the same modal; « Effacer » only logs (nothing is erased).
void AskClearBrowsingData(bool demo,std::function<void(bool)> result){
    auto spec=MD::ClearDataSpec();spec.demo=demo;
    // « Effacer » keeps the dialog open in its busy state until the engine answers (Paper A5).
    spec.buttons[1].closes=false;
    spec.onButton=[demo,result](int id){
        if(id!=MD::AcceptButton)return;
        modal.SetBusy(true,L"Effacement…");
        const MD::Result now=modal.Snapshot();
        if(demo){Defer([result]{if(result)result(true);modal.Finish(MD::AcceptButton);});return;}
        StartClearSelected(now.checks,now.select,[result](HRESULT hr){
            if(FAILED(hr)){modal.SetBusy(false);modal.SetError(L"L'effacement n'a pas abouti ("+HrText(hr)+L"). Réessayez.");return;}
            if(result)result(true);modal.Finish(MD::AcceptButton);AnnounceDataCleared();});
    };
    AskModal(std::move(spec),nullptr,[result](const MD::Result& r){if(r.button!=MD::AcceptButton&&result)result(false);});
}
bool closeConfirmed=false;
// WM_CLOSE: true = close now. Otherwise the modal asks and re-posts WM_CLOSE on « Fermer ».
bool CloseWindowAllowed(bool demo,std::function<void(bool)> result){
    if(!demo&&(closeConfirmed||smoke||halted||!settings.confirmCloseTabs))return true;
    const int n=(int)std::count_if(pages.begin(),pages.end(),[](const std::shared_ptr<Tab>& t){return t&&!t->closed;});
    if(!demo&&n<=1)return true;
    if(modalOpen&&!demo)return false; // one question at a time
    auto spec=MD::CloseWindowSpec(std::max(n,2));spec.demo=demo;
    AskModal(std::move(spec),nullptr,[demo,result](const MD::Result& r){
        const bool ok=r.button==MD::AcceptButton;if(result)result(ok);
        if(demo)return;
        if(!ok){Log(L"TABS window close cancelled");return;}
        closeConfirmed=true;PostMessageW(window,WM_CLOSE,0,0);
    });
    return false;
}
void ShowTabLimitToast(){
    NativeChrome::ContextToast::Spec spec=NativeChrome::ContextToast::TabLimitSpec(128);
    contextToast.Show(spec,PageScreenRect(),[](bool){});Log(L"TABS limit toast");
}
// A8. Without a live update engine (preview, portable without feed) the status says so
// instead of claiming « à jour ».
std::wstring RunningVersion(){
#ifdef BROWSER_UPDATES
    return BROWSER_VERSION_WIDE;
#else
    return L"dev";
#endif
}
void ShowAboutDialog(int demoState){
    auto spec=MD::AboutSpec(RunningVersion(),demoState<0?MD::AboutUpToDate:demoState);
    if(demoState>=0)spec.demo=true;
    bool live=UpdateCheckAvailable();
#ifdef BROWSER_PREVIEW
    live=false; // the preview's engine is compiled with updates off
#endif
    if(demoState>=0){}
    else if(!live){spec.status.icon=MD::StatusIcon::None;spec.status.title=L"Mises à jour désactivées";spec.status.detail=L"Build de prévisualisation : aucune recherche automatique";spec.status.hasButton=false;}
    else{spec.status.detail=L"Vérification automatique au démarrage";spec.status.button.closes=false;}
    spec.onButton=[](int id){if(id==MD::AboutCheckButton){CheckForUpdates();modal.SetStatus(MD::AboutStatus(MD::AboutChecking));}};
    AskModal(std::move(spec),nullptr,[](const MD::Result& r){
        if(r.button==MD::AboutLicensesLink){std::wstring dir=base;ShellExecuteW(window,L"open",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
    });
}
void ShowAboutDialog(){ShowAboutDialog(-1);}
void ShowPrivateDialog(bool future){AskModal(MD::PrivateSpec(future),nullptr,[future](const MD::Result& r){if(r.button==MD::PrivateClearDataButton)AskClearBrowsingData(future,nullptr);});}

// ---------------------------------------------------------------------------------------
// Debug sandbox (browser://debug, --debug-sandbox, Ctrl+Maj+F12).
std::wstring sandboxToken;
const std::wstring& SandboxToken(){
    if(sandboxToken.empty()){GUID g{};wchar_t text[40]{};if(SUCCEEDED(CoCreateGuid(&g))&&StringFromGUID2(g,text,40))sandboxToken=text;
        sandboxToken.erase(std::remove_if(sandboxToken.begin(),sandboxToken.end(),[](wchar_t c){return c==L'{'||c==L'}'||c==L'-';}),sandboxToken.end());}
    return sandboxToken;
}
bool IsSandboxUri(const std::wstring& uri){const std::wstring origin=std::wstring(L"https://")+NativeChrome::DebugSandbox::Host;
    return _wcsnicmp(uri.c_str(),origin.c_str(),origin.size())==0&&(uri.size()==origin.size()||uri[origin.size()]==L'/'||uri[origin.size()]==L'?'||uri[origin.size()]==L'#'||uri[origin.size()]==L':');}
// `in`: the tab whose address bar asked for browser://debug (navigates there); otherwise an
// open sandbox tab is selected, the active empty tab reused, or a new tab opened.
void OpenSandbox(std::shared_ptr<Tab> in){
    if(halted)return;
    if(in&&!in->closed&&in->view.p){in->sandboxPending=true;Navigate(in,NativeChrome::DebugSandbox::Url);return;}
    for(int i=0;i<(int)pages.size();++i)if(pages[i]&&pages[i]->sandbox&&IsSandboxUri(pages[i]->uri)){Select(i);return;}
    auto t=Active();
    if(t&&t->view.p&&t->home){t->sandboxPending=true;Navigate(t,NativeChrome::DebugSandbox::Url);return;}
    if(pages.size()>=128){ShowTabLimitToast();return;}
    sandboxNextTab=true;NewTab(NativeChrome::DebugSandbox::Url,current+1);
}
// NavigationStarting for the sandbox host: only the navigation we started (or a reload /
// history step inside a tab the user opened it in) proceeds.
bool SandboxNavigationStarting(const std::shared_ptr<Tab>& t,ICoreWebView2NavigationStartingEventArgs* a,const std::wstring& uri){
    if(!IsSandboxUri(uri)){return false;}
    COREWEBVIEW2_NAVIGATION_KIND kind=COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT;
    {Com<ICoreWebView2NavigationStartingEventArgs3> a3;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2NavigationStartingEventArgs3,(void**)&a3.p))&&a3.p)a3->get_NavigationKind(&kind);}
    if(t->sandboxPending){t->sandboxPending=false;t->sandbox=true;return false;}
    if(t->sandbox&&kind!=COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT)return false;
    a->put_Cancel(TRUE);Log(L"SANDBOX navigation refused (not opened by the user)");return true;
}
HRESULT SandboxResource(const std::shared_ptr<Tab>& t,ICoreWebView2WebResourceRequestedEventArgs* a){
    if(!a||halted||!environment.p)return S_OK;
    Com<ICoreWebView2WebResourceRequest> request;if(FAILED(a->get_Request(&request.p))||!request.p)return S_OK;
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;request->get_Uri(&s);return s;}());
    if(!IsSandboxUri(uri))return S_OK;
    std::string body;int status=404;const wchar_t* reason=L"Not Found";
    std::wstring path=uri.substr(wcslen(L"https://")+wcslen(NativeChrome::DebugSandbox::Host));const auto q=path.find_first_of(L"?#");if(q!=std::wstring::npos)path.resize(q);
    if(t&&!t->closed&&t->sandbox&&path==L"/sandbox"){body=NativeChrome::DebugSandbox::Html(SandboxToken(),PageFont(false),PageFont(true));status=200;reason=L"OK";}
    else body="<!doctype html><meta charset=utf-8><title>Introuvable</title>";
    IStream* stream=SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()),(UINT)body.size());if(!stream)return S_OK;
    Com<ICoreWebView2WebResourceResponse> response;
    const wchar_t* headers=L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; font-src data:; media-src data: blob:; frame-ancestors 'none'";
    if(SUCCEEDED(environment->CreateWebResourceResponse(stream,status,reason,headers,&response.p))&&response.p)a->put_Response(response.p);
    stream->Release();
    return S_OK;
}
void SandboxLog(const std::shared_ptr<Tab>& t,const std::wstring& line){
    if(!t||t->closed||!t->view.p||!t->sandbox||!IsSandboxUri(t->uri))return;
    t->view->ExecuteScript(NativeChrome::DebugSandbox::ResultScript(line).c_str(),nullptr);
}
void SandboxCommand(const std::shared_ptr<Tab>& t,const NativeChrome::DebugSandbox::Command& c);
bool SandboxMessage(const std::shared_ptr<Tab>& t,const std::wstring& source,const std::wstring& message){
    if(!t->sandbox||source!=NativeChrome::DebugSandbox::Url||!IsSandboxUri(t->uri))return false;
    auto command=NativeChrome::DebugSandbox::ParseCommand(message,SandboxToken());
    if(!command){Log(L"SANDBOX message rejected");return true;}
    std::weak_ptr<Tab> weak=t;auto c=*command;
    Defer([weak,c]{auto tab=weak.lock();if(tab&&!tab->closed&&!halted)SandboxCommand(tab,c);});
    return true;
}

// Every top-level web message of a tab: interstitial, error page, then sandbox.
HRESULT InterstitialMessage(const std::shared_ptr<Tab>& t,ICoreWebView2WebMessageReceivedEventArgs* a);
HRESULT PageMessage(const std::shared_ptr<Tab>& t,ICoreWebView2WebMessageReceivedEventArgs* a){
    if(halted||!t||t->closed||!a)return S_OK;
    if(!t->https.shown.empty())return InterstitialMessage(t,a);
    const std::wstring source=TakeString([&]{LPWSTR s=nullptr;a->get_Source(&s);return s;}());
    LPWSTR raw=nullptr;std::wstring message;
    if(SUCCEEDED(a->TryGetWebMessageAsString(&raw))&&raw)message=TakeString(raw);
    else{CoTaskMemFree(raw);raw=nullptr;if(SUCCEEDED(a->get_WebMessageAsJson(&raw))&&raw)message=TakeString(raw);}
    if(message.size()>4096)return S_OK;
    if(ErrorPageMessage(t,source,message))return S_OK;
    SandboxMessage(t,source,message);
    return S_OK;
}

// ---------------------------------------------------------------------------------------
// Per-tab WebView2 registrations for this section (called from NewTab's controller callback).
void RegisterElementEvents(const std::shared_ptr<Tab>& t,ICoreWebView2Settings* settings){
    std::weak_ptr<Tab> weak=t;EventRegistrationToken token{};
    if(settings&&FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)))Log(L"DIALOG default dialogs could not be disabled");
    {auto h=Callback<ICoreWebView2ScriptDialogOpeningEventHandler,ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs* a)->HRESULT{return ScriptDialogOpening(weak.lock(),a);});
     if(FAILED(t->view->add_ScriptDialogOpening(h,&token)))Log(L"DIALOG add_ScriptDialogOpening failed");h->Release();}
    {auto h=Callback<ICoreWebView2PermissionRequestedEventHandler,ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* a)->HRESULT{return PermissionRequested(weak.lock(),a);});
     if(FAILED(t->view->add_PermissionRequested(h,&token)))Log(L"PERMISSION add_PermissionRequested failed");h->Release();}
    {Com<ICoreWebView2_10> v10;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_10,(void**)&v10.p))&&v10.p){
        auto h=Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler,ICoreWebView2*,ICoreWebView2BasicAuthenticationRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2BasicAuthenticationRequestedEventArgs* a)->HRESULT{return BasicAuthenticationRequested(weak.lock(),a);});
        if(FAILED(v10->add_BasicAuthenticationRequested(h,&token)))Log(L"AUTH add_BasicAuthenticationRequested failed");h->Release();}else Log(L"AUTH ICoreWebView2_10 unavailable: engine dialog kept");}
    {Com<ICoreWebView2_18> v18;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_18,(void**)&v18.p))&&v18.p){
        auto h=Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler,ICoreWebView2*,ICoreWebView2LaunchingExternalUriSchemeEventArgs*>([weak](ICoreWebView2*,ICoreWebView2LaunchingExternalUriSchemeEventArgs* a)->HRESULT{return LaunchingExternalUriScheme(weak.lock(),a);});
        if(FAILED(v18->add_LaunchingExternalUriScheme(h,&token)))Log(L"EXTERNAL add_LaunchingExternalUriScheme failed");h->Release();}else Log(L"EXTERNAL ICoreWebView2_18 unavailable: engine dialog kept");}
    {auto h=Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{
        auto tab=weak.lock();if(!tab||tab->closed||halted||!v)return S_OK;BOOL full=FALSE;v->get_ContainsFullScreenElement(&full);tab->pageFullscreen=full!=FALSE;
        Defer([weak,full]{auto t2=weak.lock();if(t2&&!t2->closed)SetPageFullscreen(t2,full!=FALSE);});return S_OK;});
     if(FAILED(t->view->add_ContainsFullScreenElementChanged(h,&token)))Log(L"FULLSCREEN add_ContainsFullScreenElementChanged failed");h->Release();}
    if(SUCCEEDED(t->view->AddWebResourceRequestedFilter((std::wstring(L"https://")+NativeChrome::DebugSandbox::Host+L"/*").c_str(),COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))){
        auto h=Callback<ICoreWebView2WebResourceRequestedEventHandler,ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* a)->HRESULT{return SandboxResource(weak.lock(),a);});
        if(FAILED(t->view->add_WebResourceRequested(h,&token)))Log(L"SANDBOX add_WebResourceRequested failed");h->Release();}
}
// A tab is closing (or its renderer died): end everything that waits on it.
void ElementsTabClosed(const std::shared_ptr<Tab>& t){
    AbortTabModals(t);AbortTabPrompts(t);
    if(findSession.tab.lock()==t){if(findBar.Visible())findBar.Hide();FindDetach();}
    if(t->pageFullscreen){t->pageFullscreen=false;if(fullscreen.page){const bool was=InFullscreen();fullscreen.page=false;ApplyFullscreen(was);}}
    scriptDialogsBlocked.erase(t->serial);
}
// Keys pressed while one of our dialogs, prompts or the find bar has focus: their fields keep
// them, except Ctrl+F / F3 in the find bar. True when Key() must return `cmd` (0: none).
bool InWindow(HWND root,HWND f){return root&&f&&(f==root||IsChild(root,f));}
bool ElementSurfaceKey(UINT key,bool ctrl,bool alt,bool shift,int& cmd){
    cmd=0;HWND f=GetFocus();if(!f)return false;
    if(InWindow(findBar.Window(),f)){
        if(ctrl&&!alt&&!shift&&key=='F'){cmd=FindPage;return true;}
        if(!ctrl&&!alt&&key==VK_F3){cmd=shift?FindPreviousCommand:FindNextCommand;return true;}
        return true;
    }
    return InWindow(shortcutsPanel.Window(),f)||InWindow(modal.Window(),f)||InWindow(originPrompt.Window(),f)||InWindow(passwordDemo.Window(),f)||InWindow(contextToast.Window(),f);
}
void ElementsTabChanged(){ModalTabChanged();PromptTabChanged();FindTabChanged();FullscreenTabChanged();}
void RepositionElementSurfaces(){
    if(modal.Visible())modal.Reposition(PageScreenRect());
    if(originPrompt.Visible())originPrompt.Reposition(OriginAnchorRect());
    if(findBar.Visible())findBar.Reposition(PageScreenRect());
    if(contextToast.Visible())contextToast.Reposition(PageScreenRect());
    originPrompt.SetPageArea(PageScreenRect());
}
void ElementsReducedMotion(bool reduced){modal.SetReducedMotion(reduced);originPrompt.SetReducedMotion(reduced);passwordDemo.SetReducedMotion(reduced);findBar.SetReducedMotion(reduced);contextToast.SetReducedMotion(reduced);fullscreenHint.SetReducedMotion(reduced);shortcutsPanel.SetReducedMotion(reduced);sandboxTip.SetReducedMotion(reduced);chromeTip.SetReducedMotion(reduced);}
NativeChrome::Motion::Wake ElementsNextWake(NativeChrome::Motion::Time now){
    using NativeChrome::Motion::Earlier;
    return Earlier(Earlier(Earlier(Earlier(modal.NextWake(now),originPrompt.NextWake(now)),Earlier(findBar.NextWake(now),contextToast.NextWake(now))),fullscreenHint.NextWake(now)),Earlier(Earlier(shortcutsPanel.NextWake(now),Earlier(sandboxTip.NextWake(now),chromeTip.NextWake(now))),passwordDemo.NextWake(now)));
}
void ElementsFrame(NativeChrome::Motion::Time now){modal.Frame(now);originPrompt.Frame(now);passwordDemo.Frame(now);shortcutsPanel.Frame(now);sandboxTip.Frame(now);chromeTip.Frame(now);findBar.Frame(now);contextToast.Frame(now);fullscreenHint.Frame(now);}
void CreateElementSurfaces(){
    const std::wstring root=base;
    MD::Dialog::Hooks mh;mh.wake=[]{RequestElementFrame();};if(!modal.Create(instance,window,root,std::move(mh)))Log(L"MODAL unavailable");
    OP::Prompt::Hooks ph;ph.wake=[]{RequestElementFrame();};if(!originPrompt.Create(instance,window,root,std::move(ph)))Log(L"PROMPT unavailable");
    NativeChrome::FindBar::Hooks fh;
    fh.query=[](const std::wstring& s,const NativeChrome::FindBar::Options& o){FindQuery(s,o);};
    fh.next=[]{if(findSession.find.p&&!findSession.term.empty())findSession.find->FindNext();};
    fh.previous=[]{if(findSession.find.p&&!findSession.term.empty())findSession.find->FindPrevious();};
    fh.close=[]{CloseFindBar(true);};fh.wake=[]{RequestElementFrame();};
    if(!findBar.Create(instance,window,root,std::move(fh)))Log(L"FIND bar unavailable: engine find kept");
    if(!contextToast.Create(instance,window,root,[]{RequestElementFrame();}))Log(L"TOAST unavailable");
    if(!fullscreenHint.Create(instance,window,root,[]{RequestElementFrame();}))Log(L"FULLSCREEN hint unavailable");
    NativeChrome::PasswordDemo::Window::Hooks pd;pd.wake=[]{RequestElementFrame();};pd.announce=[](const std::wstring& s){Log(L"PASSWORDS demo "+s);};
    if(!passwordDemo.Create(instance,window,root,std::move(pd)))Log(L"PASSWORDS demo unavailable");
    if(!shortcutsPanel.Create(instance,window,root,[]{RequestElementFrame();}))Log(L"SHORTCUTS panel unavailable");
    else shortcutsPanel.SetClosed([]{FocusPage();});
    sandboxTip.Create(instance,window,root);
    // C5: the toolbar's tooltips use the Paper tooltip; the comctl one stays only as a fallback.
    {NativeChrome::Tooltip::Hooks th;th.wake=[]{RequestElementFrame();};
     if(chromeTip.Create(instance,window,root,std::move(th))&&tooltip){DestroyWindow(tooltip);tooltip=nullptr;}}
    ElementsReducedMotion(motion.ReducedMotion());
}
void DisposeElementSurfaces(){
    while(!modalQueue.empty()){auto job=std::move(modalQueue.front());modalQueue.pop_front();if(job.done)job.done(MD::Result{});}
    if(modalOpen)modal.Abort();
    while(!promptQueue.empty()){auto job=std::move(promptQueue.front());promptQueue.pop_front();OP::Result none;for(auto& a:job.answers)a(none);}
    if(promptOpen)originPrompt.Abort();
    FindDetach();
    modal.Dispose();originPrompt.Dispose();findBar.Dispose();contextToast.Dispose();fullscreenHint.Dispose();passwordDemo.Dispose();shortcutsPanel.Dispose();sandboxTip.Dispose();chromeTip.Dispose();
}

// ---------------------------------------------------------------------------------------
// Sandbox commands (allowlisted by DebugSandbox::ParseCommand). Each opens the PRODUCTION
// component; « demo » marks those whose confirmation only logs. Results go to the page's
// journal; nothing here erases data, closes the window or reads a real credential.
void OpenShortcutsPanel(){
    if(!shortcutsPanel.Window()||halted)return;
    if(shortcutsPanel.Visible()){shortcutsPanel.Hide();return;}
    shortcutsPanel.Show(PageScreenRect());
}
std::map<UINT_PTR,std::function<void()>> sandboxTimers;
void CALLBACK SandboxTimer(HWND,UINT,UINT_PTR id,DWORD){KillTimer(nullptr,id);auto it=sandboxTimers.find(id);if(it==sandboxTimers.end())return;auto f=std::move(it->second);sandboxTimers.erase(it);if(!halted)f();}
void After(UINT ms,std::function<void()> f){UINT_PTR id=SetTimer(nullptr,0,ms,SandboxTimer);if(id)sandboxTimers[id]=std::move(f);}
std::wstring ModalOutcome(const MD::Result& r){
    std::wstring s=r.button==-1?L"annulée (Échap / abandon)":r.button==MD::CancelButton?L"Annuler":r.button==MD::AcceptButton?L"accepté":L"bouton "+std::to_wstring(r.button);
    if(!r.values.empty()&&!r.values[0].empty())s+=L" · saisie « "+r.values[0]+L" »";
    if(r.checked)s+=L" · case cochée";
    return s;
}
std::wstring PromptOutcome(const OP::Result& r){
    static const wchar_t* names[]={L"fermé",L"autorisé",L"autorisé cette fois",L"bloqué",L"validé",L"annulé",L"ouvert",L"conservé",L"supprimé",L"choisi",L"enregistré",L"jamais",L"rempli",L"gérer"};
    const int a=(int)r.action;std::wstring s=a>=0&&a<14?names[a]:L"?";
    if(r.remember)s+=L" · mémorisé";
    if(!r.user.empty())s+=L" · utilisateur « "+r.user+L" »";
    if(!r.password.empty())s+=L" · mot de passe saisi (non journalisé)";
    if(r.choice>=0)s+=L" · choix "+std::to_wstring(r.choice+1);
    return s;
}
// Variant flags shared by the permission / auth simulations.
void SandboxPromptVariant(OP::Spec& s,const std::wstring& v){
    if(s.kind==OP::Kind::Permission&&v==L"blocked")s.userInitiated=false;
    if(s.kind==OP::Kind::BasicAuth){s.user=L"camille.demo";if(v==L"error")After(400,[]{originPrompt.AuthRefused();});}
}
bool SandboxPasswordManager(const std::wstring& v,std::function<void(const std::wstring&)> log){
    if(!passwordDemo.Window())return false;
    if(passwordDemo.Visible())passwordDemo.Hide();
    if(!passwordDemo.Show(PageScreenRect(),[log]{log(L"password.manager → fermé");}))return false;
    log(L"password.manager · "+v+L" → ouvert (comptes fictifs en mémoire)");return true;
}
void SandboxCommand(const std::shared_ptr<Tab>& t,const NativeChrome::DebugSandbox::Command& c){
    std::weak_ptr<Tab> weak=t;
    auto log=[weak](const std::wstring& line){Log(L"SANDBOX "+line);SandboxLog(weak.lock(),line);};
    const std::wstring& n=c.name;const std::wstring& v=c.variant;const std::wstring what=n+(v.empty()?L"":L" · "+v);
    auto modalDone=[log,what](const MD::Result& r){log(what+L" → "+ModalOutcome(r));};
    auto promptDone=[log,what](OP::Result& r){log(what+L" → "+PromptOutcome(r));if(r.action==OP::Action::Manage)Defer([log]{SandboxPasswordManager(L"list",log);});};
    const std::wstring demoOrigin=L"meet.exemple.test";
    if(n==L"sandbox.ping"){log(L"Pont actif · jeton vérifié · onglet "+std::to_wstring(t->serial));return;}
    // ---- A: modals
    if(n==L"modal.generic"){
        MD::Spec s;s.automationId=L"SandboxGeneric";s.title=L"Titre de la modale";s.body=L"Corps de texte sur deux lignes au maximum, d'après le composant A1.";
        s.buttons={MD::Make(MD::CancelButton,L"Annuler",MD::Role::Secondary,false,true,true),MD::Make(MD::AcceptButton,v==L"danger"?L"Supprimer":L"Continuer",v==L"danger"?MD::Role::Danger:MD::Role::Primary,true,false,false)};
        if(v==L"disabled")s.buttons[1].disabled=true;
        if(v==L"error"){MD::Field f;f.label=L"Nom";f.required=true;s.fields.push_back(f);s.validate=[](const std::vector<std::wstring>& x){return x.empty()||x[0].empty()?std::wstring(L"Ce champ est obligatoire."):std::wstring();};}
        if(v==L"busy"){s.buttons[1].closes=false;s.onButton=[](int id){if(id!=MD::AcceptButton)return;modal.SetBusy(true,L"Traitement…");After(1500,[]{modal.Finish(MD::AcceptButton);});};}
        AskModal(std::move(s),nullptr,modalDone);return;}
    if(n==L"modal.alert"){std::wstring m=L"Ceci est un message d'alerte envoyé par la page.";if(v==L"long")for(int i=0;i<12;++i)m+=L" Ligne supplémentaire pour vérifier le défilement et le retour à la ligne d'un très long message JavaScript.";
        AskModal(MD::AlertSpec(L"exemple.test",m,true),nullptr,modalDone);return;}
    if(n==L"modal.confirm"){AskModal(MD::ConfirmSpec(L"exemple.test",L"Voulez-vous vraiment continuer ?"),nullptr,modalDone);return;}
    if(n==L"modal.prompt"){auto s=MD::PromptSpec(L"exemple.test",L"Nom du dossier",v==L"error"?L"":L"Projets");
        if(v==L"error")s.validate=[](const std::vector<std::wstring>& x){return x.empty()||x[0].empty()?std::wstring(L"Saisissez un nom."):std::wstring();};
        AskModal(std::move(s),nullptr,modalDone);return;}
    if(n==L"modal.beforeUnload"){AskModal(MD::BeforeUnloadSpec(L"exemple.test",v==L"reload"),nullptr,modalDone);return;}
    if(n==L"modal.clearData"){AskClearBrowsingData(true,[log,what](bool ok){log(what+(ok?L" → confirmé (démonstration : rien n'a été effacé)":L" → annulé"));});return;}
    if(n==L"modal.closeWindow"){CloseWindowAllowed(true,[log,what](bool ok){log(what+(ok?L" → confirmé (démonstration : la fenêtre reste ouverte)":L" → annulé"));});return;}
    if(n==L"modal.openManyTabs"){auto s=MD::OpenManyTabsSpec(24,L"Lecture");s.demo=true;AskModal(std::move(s),nullptr,modalDone);return;}
    if(n==L"modal.deleteBookmarks"){auto s=MD::DeleteBookmarksSpec(v==L"many"?12:1);s.demo=true;AskModal(std::move(s),nullptr,modalDone);return;}
    if(n==L"modal.quit"){auto s=MD::CloseWindowSpec(3,L"rapport-annuel.pdf",L"42 %");s.demo=true;AskModal(std::move(s),nullptr,modalDone);return;}
    if(n==L"modal.about"){ShowAboutDialog(v==L"checking"?MD::AboutChecking:v==L"available"?MD::AboutReady:v==L"error"?MD::AboutOffline:MD::AboutUpToDate);log(what+L" → ouverte");return;}
    if(n==L"modal.private"){AskModal(MD::PrivateSpec(v==L"concept"),nullptr,modalDone);return;}
    if(n==L"toast.tabLimit"){auto s=v==L"partial"?NativeChrome::ContextToast::PartialRestoreSpec(5,128):NativeChrome::ContextToast::TabLimitSpec(128);
        contextToast.Show(s,PageScreenRect(),[log,what](bool a){log(what+(a?L" → action":L" → fermé"));});return;}
    // ---- B: origin prompts (demo origin, nothing granted to any site)
    if(n==L"password.manager"){if(!SandboxPasswordManager(v,log))log(what+L" → indisponible");return;}
    if(n.rfind(L"prompt.",0)==0){
        OP::Spec s;s.origin=demoOrigin;s.demo=true;
        if(n==L"prompt.permission"){s.kind=OP::Kind::Permission;
            if(v==L"camera")s.permissions={COREWEBVIEW2_PERMISSION_KIND_CAMERA};
            else if(v==L"microphone")s.permissions={COREWEBVIEW2_PERMISSION_KIND_MICROPHONE};
            else if(v==L"cameraMicrophone"||v==L"blocked")s.permissions={COREWEBVIEW2_PERMISSION_KIND_CAMERA,COREWEBVIEW2_PERMISSION_KIND_MICROPHONE};
            else if(v==L"geolocation")s.permissions={COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION};
            else if(v==L"notifications")s.permissions={COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS};
            else s.permissions={COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ};
            SandboxPromptVariant(s,v);}
        else if(n==L"prompt.basicAuth"){s.kind=OP::Kind::BasicAuth;s.origin=L"intranet.exemple.test";s.detail=L"Zone protégée";SandboxPromptVariant(s,v);}
        else if(n==L"prompt.externalApp"){s.kind=OP::Kind::ExternalApp;s.detail=L"mailto:camille.demo@exemple.test";}
        else if(n==L"prompt.dangerousDownload"){s.kind=OP::Kind::DangerousDownload;s.origin=L"telechargements.exemple.test";s.detail=L"facture-2026.pdf.exe";s.reason=L"Microsoft Defender SmartScreen signale ce fichier comme dangereux (démonstration).";}
        else if(n==L"prompt.clientCertificate"){s.kind=OP::Kind::ClientCertificate;s.origin=L"banque.exemple.test";s.accounts={{L"Camille Démo",L"Émis par Autorité Démo · expire en 2027"},{L"Poste de travail démo",L"Émis par CA Interne Démo"}};}
        else if(n==L"prompt.screenShare"){s.kind=OP::Kind::ScreenShare;s.sources={L"Onglet",L"Fenêtre",L"Écran entier"};}
        else if(n==L"prompt.keepFile"){s.kind=OP::Kind::KeepFile;s.origin=L"telechargements.exemple.test";s.detail=L"archive-photos.zip";}
        else if(n==L"prompt.passwordFill"){s.kind=OP::Kind::PasswordFill;s.origin=L"compte.exemple.test";s.accounts={{L"camille.demo@exemple.test",L"Utilisé il y a 2 jours"}};if(v==L"two")s.accounts.push_back({L"equipe.demo@exemple.test",L"Utilisé le 3 mars"});}
        else if(n==L"prompt.passwordSave"){s.kind=v==L"update"?OP::Kind::PasswordUpdate:OP::Kind::PasswordSave;s.origin=L"compte.exemple.test";s.user=L"camille.demo@exemple.test";}
        else{log(what+L" → inconnue");return;}
        // The sandbox tab is the active one: the prompt anchors to its address bar as a real one would.
        AskPrompt(std::move(s),t,promptDone);return;}
    // ---- C
    if(n==L"find.open"){OpenFindBar();if(v==L"seed"){findBar.SetText(L"oxyde");FindQuery(L"oxyde",findBar.CurrentOptions());}log(what+L" → barre ouverte (Ctrl+F)");return;}
    if(n==L"error.page"||n==L"error.offline"){
        using EK=NativeChrome::ErrorPages::Kind;EK kind=EK::NameNotResolved;
        if(n==L"error.offline")kind=EK::Offline;else if(v==L"refused")kind=EK::Refused;else if(v==L"timedOut")kind=EK::TimedOut;else if(v==L"reset")kind=EK::Reset;else if(v==L"unreachable")kind=EK::Unreachable;else if(v==L"generic")kind=EK::Generic;
        if(pages.size()>=128){ShowTabLimitToast();return;}
        // A real navigation to a reserved, unresolvable host (RFC 2606 .invalid): the engine fails it,
        // our error page is shown for the requested variant.
        NewTab(L"https://"+(v.empty()?std::wstring(L"hors-ligne"):v)+L".sandbox.invalid/",current+1);auto nt=Active();
        if(nt){nt->errorPage.forced=(int)kind;nt->errorPage.forcedRetrying=v==L"retrying";nt->errorPage.forcedRestored=v==L"restored";}
        log(what+L" → nouvel onglet");return;}
    if(n==L"tooltip.show"){
        NativeChrome::Tooltip::Content content{L"Actualiser",v==L"label"?L"":L"F5",v==L"detail"?L"Recharge la page depuis le serveur":L""};
        if(v==L"tab")content={L"Sandbox des composants — browser://debug",L"",L""};
        if(v==L"disabled")content={L"Suivant",L"Alt+→",L"Aucune page suivante"};
        RECT p=PageScreenRect();RECT target{p.left+S(24),p.top+S(8),p.left+S(56),p.top+S(40)};sandboxTip.Show(content,target,&p);
        After(4000,[]{sandboxTip.Hide();});log(what+L" → info-bulle affichée 4 s");return;}
    if(n==L"fullscreen.hint"){fullscreenHint.Show(MonitorScreenRect(),v!=L"browser",L"sandbox",v==L"locked");log(what+L" → indication affichée");return;}
    if(n==L"toast.status"||n==L"toast.pageSaved"){
        namespace CT=NativeChrome::ContextToast;CT::Spec s;
        if(n==L"toast.pageSaved")s=CT::PageSavedSpec(L"Sandbox des composants.mhtml");
        else if(v==L"failure")s=CT::SaveFailedSpec(L"Espace disque insuffisant (démonstration)");
        else if(v==L"progress"){s=CT::SavingSpec(.42f);After(1200,[]{contextToast.Update(NativeChrome::ContextToast::SavingSpec(.86f));});}
        else if(v==L"brief")s=CT::BriefSpec(L"Lien copié",CT::Icon::Link);
        else s=CT::BriefSpec(L"Téléchargement terminé",CT::Icon::Success);
        contextToast.Show(s,PageScreenRect(),[log,what](bool a){log(what+(a?L" → action":L" → fermé"));});return;}
    if(n==L"shortcuts.open"){OpenShortcutsPanel();log(what+L" → panneau ouvert");return;}
    log(what+L" → commande non branchée");
}

// ---------------------------------------------------------------------------------------
// C5 in the real toolbar: « Libellé (Raccourci) » becomes label + key caps.
NativeChrome::Tooltip::Content TipContent(const wchar_t* tip){
    std::wstring s=tip?tip:L"";NativeChrome::Tooltip::Content c{s,L"",L""};
    if(!s.empty()&&s.back()==L')'){const auto open=s.rfind(L" (");
        if(open!=std::wstring::npos){std::wstring key=s.substr(open+2,s.size()-open-3);
            if(key.rfind(L"Ctrl",0)==0||key.rfind(L"Alt",0)==0||key.rfind(L"Maj",0)==0||key.rfind(L"Shift",0)==0||(key.size()>=2&&key[0]==L'F'&&iswdigit(key[1]))){c.label=s.substr(0,open);c.shortcut=key;}}}
    return c;
}
void ChromeTipHover(int x,int y){
    if(!chromeTip.Window())return;
    const float d=GetDpiForWindow(window)/96.f,fx=x/d,fy=y/d;
    for(auto& r:chrome.regions){
        if(!r.tip||!*r.tip||fx<r.rect.left||fx>=r.rect.right||fy<r.rect.top||fy>=r.rect.bottom)continue;
        RECT target{S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&target),2);
        RECT bounds=WindowScreenRect();chromeTip.Hover(TipContent(r.tip),target,&bounds);return;
    }
    chromeTip.Hide();
}
void ChromeTipHide(){if(chromeTip.Window())chromeTip.Hide();}
