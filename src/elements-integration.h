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
#include "i18n.h"
#include "modal.h"
#include "origin-prompt.h"
#include "password-demo.h"
#include "find-bar.h"
#include "error-pages.h"
#include "crash-page.h"
#include "context-toast.h"
#include "tooltip.h"
#include "fullscreen-hint.h"
#include "debug-sandbox.h"
#include "shortcuts-panel.h"
#include "update-toast-motion.h" // FormatSize: the one validated progress format (toast, A6)
#include "release-notes.h"     // « Nouveautés »: content, read state, automatic display rule
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
std::wstring TakeString(LPWSTR s){std::wstring r=s?s:L"";if(s)SecureZeroMemory(s,wcslen(s)*sizeof(wchar_t));CoTaskMemFree(s);return r;}   // F6: the engine buffer is wiped (page messages may carry a password)
// « exemple.com » for a page address; « Cette page » for local documents.
std::wstring OriginLabel(const std::wstring& uri){
    const auto scheme=uri.find(L"://");if(scheme==std::wstring::npos)return TR(this_page);
    const auto start=scheme+3;auto end=uri.find_first_of(L"/?#",start);if(end==std::wstring::npos)end=uri.size();
    std::wstring host=uri.substr(start,end-start);const auto at=host.rfind(L'@');if(at!=std::wstring::npos)host.erase(0,at+1);
    if(host.rfind(L"www.",0)==0)host.erase(0,4);
    return host.empty()?std::wstring(TR(this_page)):host;
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
// The find bar hangs 8 DIP inside the page's top-right corner; in Mode Focus it keeps clear of the floating panel while
// the panel is on screen (it follows the slide: FocusApply), so it never covers the toolbar.
RECT FindBarArea(){RECT r=PageScreenRect();const RECT p=FocusPanelPixels();if(!IsRectEmpty(&p)&&window){POINT b{0,p.bottom};ClientToScreen(window,&b);r.top=std::min(r.bottom,std::max(r.top,b.y));}return r;}
RECT OriginAnchorRect(){
    FocusAnchor(L"permission prompt"); // i18n-exempt: log tag. Mode Focus: the permission prompt hangs from the address field: the panel at rest
    auto i=chrome.indicator;if(i.right<=i.left||i.bottom<=i.top)i=chrome.AddressSlot();
    RECT r=ChromePx(i);if(window)MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&r),2);return r;   // Mode Focus: the panel at rest
}

// ---------------------------------------------------------------------------------------
// Components (created in CreateElementSurfaces, disposed before CoUninitialize).
MD::Dialog modal; MD::Dialog notesCard; /* « Nouveautés »: its own bottom-right card, outside the modal queue (never holds a question back) */ OP::Prompt originPrompt; NativeChrome::FindBar::Bar findBar;
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
void PumpModal(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
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
// Dialogs already shown by a site in a tab: from the second one the A2 « Empêcher ce site d'ouvrir d'autres
// dialogues » check is offered, and a checked block holds. SEC-25: kept per tab AND origin, so a reload or another
// page of the same site keeps them (alert(); location.reload() cannot dodge the check); another origin starts over.
struct ScriptDialogState{int count=0;bool blocked=false;};
// Per tab, then per origin (review of SEC-25): alternating dialogs with a frame of another origin cannot reset a block.
// Capped per tab (64 origins: an unblocked origin's state goes first).
std::map<unsigned,std::map<std::wstring,ScriptDialogState>> scriptDialogs;
// SEC-25: a page's dialog text is laid out on the UI thread: capped (a whole surrogate pair or nothing).
std::wstring CapDialogText(std::wstring s,size_t limit){if(s.size()>limit){s.resize(limit);if(!s.empty()&&s.back()>=0xD800&&s.back()<=0xDBFF)s.pop_back();}return s;}
HRESULT ScriptDialogOpening(const std::shared_ptr<Tab>& t,ICoreWebView2ScriptDialogOpeningEventArgs* a){
    if(!a)return S_OK;
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK; // no deferral: engine answers "cancel"
    auto deferral=std::make_shared<DeferralGuard>(raw);
    if(halted||!t||t->closed)return S_OK; // guard completes: dialog cancelled
    COREWEBVIEW2_SCRIPT_DIALOG_KIND kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;a->get_Kind(&kind);
    const std::wstring uri=TakeString([&]{LPWSTR s=nullptr;a->get_Uri(&s);return s;}());
    const std::wstring message=CapDialogText(TakeString([&]{LPWSTR s=nullptr;a->get_Message(&s);return s;}()),10000);
    const std::wstring defaultText=CapDialogText(TakeString([&]{LPWSTR s=nullptr;a->get_DefaultText(&s);return s;}()),2000);
    const std::wstring site=OriginOf(uri);auto& perTab=scriptDialogs[t->serial];if(!perTab.count(site)&&perTab.size()>=64){for(auto it=perTab.begin();it!=perTab.end();++it)if(!it->second.blocked){perTab.erase(it);break;}}auto& dialogs=perTab[site];
    if(kind!=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD&&dialogs.blocked){Log(L"DIALOG suppressed for "+OriginLabel(uri));return S_OK;}
    const std::wstring origin=OriginLabel(uri);MD::Spec spec;
    const bool repeated=kind!=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD&&++dialogs.count>1;
    switch(kind){
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT:spec=MD::AlertSpec(origin,message,repeated);break;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:spec=MD::ConfirmSpec(origin,message,repeated);break;
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT:spec=MD::PromptSpec(origin,message,defaultText,repeated);break;
    // « Actualiser ce site ? » when our reload (button, Ctrl+R, F5) started the unload.
    case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:spec=MD::BeforeUnloadSpec(origin,t->refreshing);break;
    }
    Log(L"DIALOG kind="+std::to_wstring(kind)+L" "+origin);
    spec.modality=MD::Spec::Modality::Pointer; // the page's own input is not visible here: ring on Tab / arrows only
    ComRef<ICoreWebView2ScriptDialogOpeningEventArgs> args(a);const unsigned serial=t->serial;
    AskModal(std::move(spec),t,[args,deferral,kind,serial,site](const MD::Result& r){
        if(r.checked&&kind!=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD){auto it=scriptDialogs.find(serial);if(it!=scriptDialogs.end()){auto o=it->second.find(site);if(o!=it->second.end())o->second.blocked=true;}}
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
// A password just submitted (B04, real path) may sit in a queued or active job: every copy is wiped when
// the job is replaced, dropped or destroyed (review 885d322 #1), never left to the allocator.
inline void WipeSpecSecret(OP::Spec& s){if(s.secret.capacity())SecureZeroMemory(s.secret.data(),s.secret.capacity()*sizeof(wchar_t));s.secret.clear();}
struct Workspace;extern Workspace* activeWorkspace;
struct PromptJob {
    OP::Spec spec; std::weak_ptr<Tab> tab; std::wstring origin;
    Workspace* owner=nullptr;   // review V-1: the profile whose tab asked (shown only while it is on screen)
    std::vector<std::function<void(OP::Result&)>> answers; // one per merged request
    PromptJob()=default; PromptJob(const PromptJob&)=delete; PromptJob& operator=(const PromptJob&)=delete;
    PromptJob(PromptJob&& o) noexcept:spec(std::move(o.spec)),tab(std::move(o.tab)),origin(std::move(o.origin)),owner(o.owner),answers(std::move(o.answers)){WipeSpecSecret(o.spec);}
    PromptJob& operator=(PromptJob&& o) noexcept{if(this!=&o){WipeSpecSecret(spec);spec=std::move(o.spec);tab=std::move(o.tab);origin=std::move(o.origin);owner=o.owner;answers=std::move(o.answers);WipeSpecSecret(o.spec);}return *this;}
    ~PromptJob(){WipeSpecSecret(spec);}
};
std::deque<PromptJob> promptQueue; PromptJob promptActive; bool promptOpen=false;
void PumpPrompt();
bool VaultOfferStillValid(const std::shared_ptr<Tab>& t,const std::wstring& origin);
// B01 address-bar chip: shown exactly while a permission popover is open for the active tab
// (real request or sandbox demo), so it follows that request's tab and origin and disappears
// with its answer, cancellation, navigation, tab switch/close or shutdown. Nothing survives the
// answer: WebView2 gives no verified capture state, and « autorisé » is not « en cours ».
bool RequestChipOpen(){
    if(halted||!promptOpen||promptActive.spec.kind!=OP::Kind::Permission||!originPrompt.Visible())return false;
    auto t=promptActive.tab.lock();return t&&!t->closed&&t==Active();
}
// « Caméra, micro et position » / « Camera, microphone and location »: the nouns as one list, in the process language.
std::wstring NounList(const std::vector<int>& p){
    std::vector<std::wstring> n;for(size_t i=0;i<p.size();++i){std::wstring w=OP::Text::Noun(p[i]);if(i&&I18n::Current()==I18n::Locale::Fr)w[0]=towlower(w[0]);n.push_back(w);}
    if(n.empty())return TR(perm_one_permission);
    std::wstring head=n[0];for(size_t i=1;i+1<n.size();++i)head=TRF(list_comma,head,n[i]);
    return n.size()==1?head:TRF(list_and,head,n.back());
}
std::wstring RequestChipTip(const OP::Spec& s){return TRF(chip_pending_tip,s.origin,NounList(s.permissions));}
// Per-tab site state behind the chip once a request is answered. Real tabs: what WE granted or
// blocked for this document (deferral answers) plus what the engine has saved for its origin
// (ICoreWebView2Profile4::GetNonDefaultPermissionSettings, re-read after each navigation).
// « In use » is never inferred: WebView2 exposes no camera / microphone capture state (only
// ScreenCaptureStarting and IsDocumentPlayingAudio, which is audio OUTPUT), so `active` is only
// set by the sandbox demonstration, on its own demo state. Cleared on navigation and tab close.
// `origin`: the tab's top-level document (scope); `requester`: who asked (a frame may differ), used to revoke.
struct SiteState { std::wstring origin,requester; SiteRequest::Grants g; std::vector<int> remembered; bool demo=false; };
std::map<unsigned,SiteState> siteStates;
SiteState* SiteStateOf(const std::shared_ptr<Tab>& t){
    if(!t||t->closed)return nullptr;auto it=siteStates.find(t->serial);if(it==siteStates.end())return nullptr;
    if(!it->second.demo&&it->second.origin!=OriginOf(t->uri))return nullptr;   // another document's grants never show
    return &it->second;
}
void RecordSiteAnswer(const std::shared_ptr<Tab>& t,const std::wstring& requester,const std::vector<int>& kinds,bool allowed,bool remembered,bool demo){
    if(!t||t->closed||halted)return;auto& st=siteStates[t->serial];const std::wstring origin=demo?std::wstring(L"demo"):OriginOf(t->uri);
    if(st.origin!=origin||st.demo!=demo){st=SiteState{};st.origin=origin;st.demo=demo;}
    st.requester=requester;
    SiteRequest::Record(st.g,kinds,allowed);
    for(int k:kinds){if(allowed&&remembered)SiteRequest::Add(st.remembered,k);else SiteRequest::Remove(st.remembered,k);}
}
std::wstring Nouns(const std::vector<int>& p){return NounList(p);}
// What the bar should show for the active tab right now (glyphs empty: nothing).
struct ChipTarget { std::vector<const char*> glyphs; SiteRequest::State state=SiteRequest::State::Pending; std::wstring tip; };
ChipTarget ChipFor(){
    ChipTarget c;if(halted||editing)return c;
    if(RequestChipOpen()){c.glyphs=SiteRequest::Glyphs(promptActive.spec.permissions);c.tip=RequestChipTip(promptActive.spec);return c;}
    if(auto* st=SiteStateOf(Active())){
        const auto shown=SiteRequest::Resolve(st->g);if(shown.kinds.empty())return c;
        c.glyphs=SiteRequest::Glyphs(shown.kinds);c.state=shown.state;
        const std::wstring site=st->demo?std::wstring(L"meet.exemple.test"):OriginLabel(st->requester+L"/");
        c.tip=st->demo?TRF(chip_state_tip_demo,site,Nouns(shown.kinds),SiteRequest::StateLabel(shown.state)):TRF(chip_state_tip,site,Nouns(shown.kinds),SiteRequest::StateLabel(shown.state));
    }
    return c;
}
// Chip motion (Paper popover timings): appear = fade 150 ms + the URL easing aside; leave = fade
// 120 ms, URL easing back, no clicks meanwhile; state change = 120 ms cross-fade. Every tween
// retargets from its current value. Reduced motion: fades only, the URL moves at once.
struct ChipMotion {
    std::vector<const char*> glyphs; SiteRequest::State state=SiteRequest::State::Pending,from=SiteRequest::State::Pending; std::wstring tip;
    NativeChrome::Motion::Tween alpha,mix,shiftT; float shiftFrom=0,shiftTo=0; bool leaving=false;
    float Shift(NativeChrome::Motion::Time now)const{return shiftFrom+(shiftTo-shiftFrom)*shiftT.Value(now);}
    bool Moving(NativeChrome::Motion::Time now)const{return alpha.Moving(now)||mix.Moving(now)||shiftT.Moving(now);}
} chipMotion;
void ChipShiftTo(float to,NativeChrome::Motion::Time now,int ms){
    const bool reduced=motion.ReducedMotion();const float cur=chipMotion.Shift(now);
    chipMotion.shiftFrom=cur;chipMotion.shiftTo=to;chipMotion.shiftT.Snap(0);chipMotion.shiftT.Set(1,now,NativeChrome::Motion::Milliseconds(ms),reduced,NativeChrome::Motion::Curve::EaseOut);
}
void ApplyChipToChrome(NativeChrome::Motion::Time now){
    auto& m=chipMotion;chrome.requestGlyphs=m.glyphs;chrome.requestState=m.state;chrome.requestFrom=m.from;chrome.requestMix=m.mix.Value(now);
    chrome.requestAlpha=m.alpha.Value(now);chrome.requestShift=m.glyphs.empty()?-1:m.Shift(now);chrome.requestLive=!m.leaving;chrome.requestTip=m.tip;
}
void SyncRequestChip(){
    using namespace NativeChrome::Motion;namespace L=SiteRequest::Layout;
    const auto now=Now();auto& m=chipMotion;auto t=ChipFor();
    if(!t.glyphs.empty()){
        if(m.glyphs.empty()||m.leaving){
            const bool fresh=m.glyphs.empty();m.glyphs=t.glyphs;m.state=m.from=t.state;m.mix.Snap(1);m.leaving=false;
            if(fresh)m.alpha.Snap(0);m.alpha.Set(1,now,Milliseconds(L::enterMs),false,Curve::EaseOut);
            ChipShiftTo(SiteRequest::TextShift(t.glyphs.size(),t.state),now,L::enterMs);
        }else{
            if(t.state!=m.state){m.from=m.state;m.state=t.state;m.mix.Snap(0);m.mix.Set(1,now,Milliseconds(L::stateMs),false,Curve::EaseOut);}
            if(t.glyphs!=m.glyphs)m.glyphs=t.glyphs;
            const float to=SiteRequest::TextShift(m.glyphs.size(),m.state);if(to!=m.shiftTo)ChipShiftTo(to,now,L::stateMs);
        }
        m.tip=t.tip;
    }else if(!m.glyphs.empty()&&!m.leaving){
        m.leaving=true;m.tip.clear();m.alpha.Set(0,now,Milliseconds(L::exitMs),false,Curve::EaseOut);ChipShiftTo(0,now,L::exitMs);
    }
    ApplyChipToChrome(now);
    // A permission held back while the address was being edited shows once editing ends.
    if(!editing&&!promptOpen&&!halted){auto a=Active();for(auto& q:promptQueue)if(q.tab.lock()==a){Defer([]{PumpPrompt();});break;}}
}
NativeChrome::Motion::Wake ChipNextWake(NativeChrome::Motion::Time now){return chipMotion.Moving(now)?NativeChrome::Motion::Frame():NativeChrome::Motion::Wake{};}
void ChipFrame(NativeChrome::Motion::Time now){
    auto& m=chipMotion;if(m.glyphs.empty())return;
    const bool done=m.leaving&&!m.alpha.Moving(now)&&!m.shiftT.Moving(now);
    if(done){m=ChipMotion{};ApplyChipToChrome(now);BuildChrome();InvalidateChrome();if(accessibility.Listening())accessibility.ChildrenChanged(AccessibleToolbarKey);return;}
    if(m.Moving(now)||chrome.requestAlpha!=m.alpha.Value(now)){ApplyChipToChrome(now);InvalidateChrome();}
}
void RefreshRequestChip(){if(halted||!window)return;BuildChrome();InvalidateChrome();RequestElementFrame();if(accessibility.Listening())accessibility.ChildrenChanged(AccessibleToolbarKey);}
void ShowSiteStatus(const std::shared_ptr<Tab>& t);
void SandboxLog(const std::shared_ptr<Tab>& t,const std::wstring& line);
// The chip's click / UIA Invoke: back to the popover already open for this request, or the
// site's status popover. It never answers, re-grants or creates a request.
void RequestChipActivated(){
    if(RequestChipOpen()){HWND w=originPrompt.Window();if(w){SetForegroundWindow(w);SetFocus(w);}Log(L"PERMISSION chip: popover focused");return;}
    if(promptOpen&&promptActive.spec.kind==OP::Kind::SiteStatus){HWND w=originPrompt.Window();if(w){SetForegroundWindow(w);SetFocus(w);}return;}
    if(SiteStateOf(Active())){ShowSiteStatus(Active());return;}
    Log(L"PERMISSION chip: nothing to show");
}
void FinishPrompt(OP::Result& r){
    if(!promptOpen)return;promptOpen=false;auto job=std::move(promptActive);promptActive=PromptJob{};
    for(auto& answer:job.answers)answer(r);
    SecureZeroMemory(r.password.data(),r.password.size()*sizeof(wchar_t));
    if(job.spec.kind==OP::Kind::Permission){Log(L"PERMISSION chip cleared");RefreshRequestChip();}
    Defer([]{PumpPrompt();});
}
RECT RegionScreen(int id);
// Where an anchored prompt hangs: real blocked downloads under the downloads button (Paper B07),
// everything else under the address bar's origin indicator.
RECT PromptAnchor(const OP::Spec& s){
    if(s.kind==OP::Kind::DangerousDownload&&!s.demo){RECT r=RegionScreen('D');if(r.right>r.left)return r;}
    return OriginAnchorRect();
}
void PumpPrompt(){
    if(halted||promptOpen||promptQueue.empty()||!originPrompt.Window())return;
    // Review V-1: judged in the scope of the profile ON SCREEN (a deferred pump may run in a hidden profile's scope, where
    // Active() is that profile's hidden tab); a request of a hidden profile waits until that profile is shown again.
    WorkspaceScope onScreen(activeWorkspace);
    for(auto it=promptQueue.begin();it!=promptQueue.end();++it){
        auto t=it->tab.lock();
        if(!t||t->closed){auto job=std::move(*it);promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);PumpPrompt();return;}
        if(it->owner&&activeWorkspace&&it->owner!=activeWorkspace)continue;
        if(t!=Active())continue;
        // Never pull focus out of the address while it is being edited: the request waits (its
        // page waits too) and shows when editing ends (SyncRequestChip).
        if((it->spec.kind==OP::Kind::Permission||it->spec.kind==OP::Kind::ScreenCapture)&&editing)continue;
        if(!it->spec.demo&&(it->spec.kind==OP::Kind::PasswordSave||it->spec.kind==OP::Kind::PasswordUpdate)&&!VaultOfferStillValid(t,it->spec.topOrigin.empty()?it->spec.origin:it->spec.topOrigin)){   // N13: never above another page (a frame's offer: its page's origin)
            auto job=std::move(*it);promptQueue.erase(it);Log(L"VAULT capture dropped before showing: the tab is no longer on its origin");OP::Result none;for(auto& a:job.answers)a(none);PumpPrompt();return;}
        promptActive=std::move(*it);promptQueue.erase(it);promptOpen=true;originPrompt.SetPageArea(PageScreenRect());
        const bool permission=promptActive.spec.kind==OP::Kind::Permission;
        const bool shown=originPrompt.Show(promptActive.spec,PromptAnchor(promptActive.spec),[](OP::Result& r){FinishPrompt(r);});
        WipeSpecSecret(promptActive.spec);   // the prompt holds its own copy now (wiped when it closes)
        if(!shown){
            Log(L"PROMPT show failed");OP::Result none;FinishPrompt(none);
        }else if(permission){RefreshRequestChip();Log(L"PERMISSION chip shown glyphs="+std::to_wstring(chrome.requestGlyphs.size()));}
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
    PromptJob job;job.spec=std::move(spec);job.tab=tab;job.origin=origin;job.owner=WorkspaceNow();job.answers.push_back(std::move(answer));
    promptQueue.push_back(std::move(job));
    // Let a sibling request of the same getUserMedia() call join before showing.
    Defer([]{PumpPrompt();});
}
void AbortTabPrompts(const std::shared_ptr<Tab>& t){
    for(auto it=promptQueue.begin();it!=promptQueue.end();){auto q=it->tab.lock();if(!q||q==t){auto job=std::move(*it);it=promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);}else ++it;}
    if(promptOpen){auto q=promptActive.tab.lock();if(!q||q==t)originPrompt.Abort();}
}
void PromptTabChanged(){if(promptOpen){auto q=promptActive.tab.lock();if(q!=Active())originPrompt.Abort();}PumpPrompt();}
// A top-level navigation of `t` ends its pending permission requests (queued and open): they
// belonged to the document being left. Authentication and other prompts are left alone.
void ElementsNavigationStarting(const std::shared_ptr<Tab>& t){
    if(!t)return;
    for(auto it=promptQueue.begin();it!=promptQueue.end();){auto q=it->tab.lock();if((it->spec.kind==OP::Kind::Permission||it->spec.kind==OP::Kind::ClientCertificate||it->spec.kind==OP::Kind::ScreenCapture||it->spec.kind==OP::Kind::Unresponsive)&&(!q||q==t)){auto job=std::move(*it);it=promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);}else ++it;}
    if(promptOpen&&(promptActive.spec.kind==OP::Kind::Permission||promptActive.spec.kind==OP::Kind::SiteStatus||promptActive.spec.kind==OP::Kind::ClientCertificate||promptActive.spec.kind==OP::Kind::ScreenCapture||promptActive.spec.kind==OP::Kind::Unresponsive)){auto q=promptActive.tab.lock();if(!q||q==t){Log(L"PERMISSION navigation: popover closed");originPrompt.Abort();}}
    if(siteStates.erase(t->serial))Log(L"PERMISSION navigation: site state cleared");
    if(t==Active())RefreshRequestChip();
}
// After a successful top-level navigation: the engine's saved decisions for this origin (the same
// source as Réglages › Autorisations). Allowed ones show as Granted (« mémorisée »).
void QueryPermissions(std::function<void(std::vector<EnginePermission>)> done);
void ElementsNavigationCompleted(const std::shared_ptr<Tab>& t,bool ok){
    if(ok&&t&&t->errorPage.shown.empty()){t->errorPage.tries=0;t->errorPage.triedUri.clear();}   // a page loaded (not our error page): the « Essai » count starts over
    if(!ok||!t||t->closed||halted||t->home)return;
    const std::wstring origin=OriginOf(t->uri);if(origin.rfind(L"http",0)!=0)return;
    std::weak_ptr<Tab> weak=t;const unsigned stamp=t->navStamp;
    QueryPermissions([weak,origin,stamp](std::vector<EnginePermission> all){
        auto tab=weak.lock();if(!tab||tab->closed||halted||tab->navStamp!=stamp||OriginOf(tab->uri)!=origin)return;
        std::vector<int> allowed;
        for(auto& e:all){std::wstring o=e.origin;while(!o.empty()&&o.back()==L'/')o.pop_back();if(o==origin&&e.state==COREWEBVIEW2_PERMISSION_STATE_ALLOW)allowed.push_back((int)e.kind);}
        if(allowed.empty())return;
        RecordSiteAnswer(tab,origin,allowed,true,true,false);Log(L"PERMISSION saved grants shown: "+std::to_wstring(allowed.size()));
        if(tab==Active())RefreshRequestChip();
    });
}
// B02 status popover from the chip: rows with one-way toggles (revoke), « Autorisations du site ».
void ShowSiteStatus(const std::shared_ptr<Tab>& t){
    auto* st=SiteStateOf(t);if(!st)return;
    OP::Spec s;s.kind=OP::Kind::SiteStatus;s.demo=st->demo;s.modality=MD::Spec::Modality::Host;s.origin=st->demo?std::wstring(L"meet.exemple.test"):OriginLabel(st->requester+L"/");
    s.granted=st->g.granted;s.active=st->g.active;s.blocked=st->g.blocked;s.remembered=st->remembered;
    Log(L"PERMISSION status popover");
    AskPrompt(std::move(s),t,[](OP::Result& r){if(r.action==OP::Action::Manage)Defer([]{settingsDrawer.ShowSites();});});
}
// A toggle switched off in the status popover. Demo: the demo state only. Real: the engine's
// saved setting goes back to « Demander » and the page reloads, so the open document loses the
// access too (a grant cannot be withdrawn from a live document otherwise).
void RevokeSitePermission(int kind,const OP::Spec& s){
    auto t=promptActive.tab.lock();auto* st=SiteStateOf(t);if(!st)return;
    SiteRequest::Revoke(st->g,kind);SiteRequest::Remove(st->remembered,kind);
    if(st->demo){Log(L"PERMISSION demo revoked kind="+std::to_wstring(kind));SandboxLog(t,L"prompt.permission → autorisation de démonstration retirée");RefreshRequestChip();return;}
    const std::wstring origin=st->requester;Log(L"PERMISSION revoked kind="+std::to_wstring(kind)+L" "+origin);
    Com<ICoreWebView2Profile4> profile;if(PermissionProfile(profile))WritePermission(profile.p,origin,(COREWEBVIEW2_PERMISSION_KIND)kind,COREWEBVIEW2_PERMISSION_STATE_DEFAULT);
    (void)s;RefreshRequestChip();std::weak_ptr<Tab> weak=t;Defer([weak]{auto tab=weak.lock();if(tab&&!tab->closed&&tab->view.p){Log(L"PERMISSION revoke: page reloaded");tab->view->Reload();}});
}

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
    if(kind==COREWEBVIEW2_PERMISSION_KIND_MULTIPLE_AUTOMATIC_DOWNLOADS)DownloadNavigationAsked(t,uri);   // the navigation that waits for it is a download
    // Persistent storage only keeps the site's own data from automatic eviction when the disk is
    // short: Chrome and Edge never ask about it, and x.com asked on every visit. User decision
    // 2026-11-17: allowed silently, never saved in the profile (no lasting « autorisé » chip).
    if((int)kind==SiteRequest::Kind::PersistentStorage){
        args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
        {Com<ICoreWebView2PermissionRequestedEventArgs3> a3;if(SUCCEEDED(args->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs3,(void**)&a3.p))&&a3.p)a3->put_SavesInProfile(FALSE);}
        Log(L"PERMISSION kind=13 allowed silently");deferral->Complete();return S_OK;
    }
    // The same document asking again for a kind already answered (Meet re-requests camera and
    // microphone on every device check): the earlier answer stands for this document, no new
    // popover, nothing saved in the profile. A navigation clears it (ElementsNavigationStarting).
    if(auto* st=SiteStateOf(t);st&&!st->demo&&st->requester==key){
        const bool granted=SiteRequest::Has(st->g.granted,(int)kind),blocked=SiteRequest::Has(st->g.blocked,(int)kind);
        if(granted||blocked){
            if(granted){args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);Com<ICoreWebView2PermissionRequestedEventArgs3> a3;if(SUCCEEDED(args->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs3,(void**)&a3.p))&&a3.p)a3->put_SavesInProfile(FALSE);}
            else deny(false);
            Log(L"PERMISSION kind="+std::to_wstring(kind)+(granted?L" reused allow":L" reused block"));
            deferral->Complete();return S_OK;
        }
    }
    std::weak_ptr<Tab> weakTab=t;
    AskPrompt(std::move(spec),t,[args,deferral,deny,kind,weakTab,key](OP::Result& r){
        Com<ICoreWebView2PermissionRequestedEventArgs3> a3;const bool has3=SUCCEEDED(args->QueryInterface(IID_ICoreWebView2PermissionRequestedEventArgs3,(void**)&a3.p))&&a3.p;
        // The bar keeps what was actually written into the engine's answer (never Dismiss).
        if(r.action==OP::Action::Allow||r.action==OP::Action::AllowOnce||r.action==OP::Action::Block)
            RecordSiteAnswer(weakTab.lock(),key,{(int)kind},r.action!=OP::Action::Block,r.action==OP::Action::Allow&&r.remember,false);
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

// ---------------------------------------------------------------------------------------
// Screen share (#121, Paper p-V-0 « Partage d'écran, confirmation · A »): getDisplayMedia() first shows OUR popover
// (OP::Kind::ScreenCapture, under the origin indicator like B01). « Choisir… » lets the engine open its own picker:
// the choice of tab / window / screen stays the engine's (WebView2 has no API for the source). « Bloquer » and every
// dismissal (close button, Échap, click outside, the tab switched, closed or navigated) cancel the capture: the page
// gets the engine's normal refusal, NotAllowedError « Permission denied ». Engine facts (ICoreWebView2_27
// ScreenCaptureStarting, tests/crash-screenshare/engine_probe.py): raised for the page and for a cross-site frame
// (OriginalSourceFrameInfo names the frame), with or without a user gesture; getDisplayMedia() waits for our
// deferral; no PermissionRequested is raised for it. « Toujours bloquer ce site » with « Bloquer » (never « Choisir… »)
// keeps the site in Browser's own list (main.cpp ScreenShareBlocked, shown in Réglages › Autorisations): its next
// requests are refused at once, without the popover. Otherwise each request asks again; one waiting in a tab refuses
// the next ones of that tab at once (no stacked popovers).
bool ScreenCaptureWaiting(const std::shared_ptr<Tab>& t){
    for(auto& q:promptQueue)if(q.spec.kind==OP::Kind::ScreenCapture&&q.tab.lock()==t)return true;
    return promptOpen&&promptActive.spec.kind==OP::Kind::ScreenCapture&&promptActive.tab.lock()==t;
}
HRESULT ScreenCaptureStarting(const std::shared_ptr<Tab>& t,ICoreWebView2ScreenCaptureStartingEventArgs* a){
    if(!a)return S_OK;
    a->put_Cancel(TRUE);   // refused unless « Choisir… » says otherwise, whatever ends this request
    if(halted||!t||t->closed){Log(L"SCREEN CAPTURE refused: tab gone");return S_OK;}
    std::wstring source;{Com<ICoreWebView2FrameInfo> f;LPWSTR s=nullptr;if(SUCCEEDED(a->get_OriginalSourceFrameInfo(&f.p))&&f.p&&SUCCEEDED(f->get_Source(&s))&&s){source=s;CoTaskMemFree(s);}}
    if(source.empty())source=t->uri;
    const std::wstring key=OriginOf(source);
    if(ScreenShareIsBlocked(key)){Log(L"SCREEN CAPTURE refused: always blocked for "+key);return S_OK;}
    if(ScreenCaptureWaiting(t)){Log(L"SCREEN CAPTURE refused: a request of this tab is already waiting "+key);return S_OK;}
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw){Log(L"SCREEN CAPTURE refused: no deferral");return S_OK;}
    auto deferral=std::make_shared<DeferralGuard>(raw);ComRef<ICoreWebView2ScreenCaptureStartingEventArgs> args(a);
    OP::Spec spec;spec.kind=OP::Kind::ScreenCapture;spec.origin=OriginLabel(source);
    Log(L"SCREEN CAPTURE request "+key);
    AskPrompt(std::move(spec),t,[args,deferral,key](OP::Result& r){
        const bool choose=r.action==OP::Action::Choose;
        args->put_Cancel(choose?FALSE:TRUE);
        if(r.action==OP::Action::Block&&r.remember)SetScreenShareBlocked(key,true);   // « Toujours bloquer ce site »
        Log(std::wstring(L"SCREEN CAPTURE ")+(choose?L"choose: the engine's picker opens ":r.action==OP::Action::Block?L"blocked ":L"dismissed: refused ")+key);
        deferral->Complete();
    },key);
    return S_OK;
}

// ---------------------------------------------------------------------------------------
// B08 client certificate: ICoreWebView2_5::ClientCertificateRequested. The list is the engine's
// MutuallyTrustedCertificates for this server (never a server-certificate decision: TLS
// validation stays the engine's). Choose = SelectedCertificate; « Ne pas envoyer » / Échap =
// continue without a certificate (Handled, nothing selected); the tab closing, a navigation or
// shutdown cancels the request (Cancel). The deferral completes exactly once.
std::wstring CertificateDate(double seconds){
    if(seconds<=0)return L"";const time_t t=(time_t)seconds;tm local{};if(localtime_s(&local,&t)!=0)return L"";
    wchar_t d[4],m[4];swprintf(d,4,L"%02d",local.tm_mday);swprintf(m,4,L"%02d",local.tm_mon+1);return TRF(date_numeric,d,m,std::to_wstring(local.tm_year+1900));
}
struct CertificateRow { std::wstring name,hint; bool expired=false; };
CertificateRow DescribeCertificate(ICoreWebView2ClientCertificate* c,double now){
    CertificateRow row;if(!c)return row;
    row.name=TakeString([&]{LPWSTR s=nullptr;c->get_DisplayName(&s);return s;}());
    if(row.name.empty())row.name=TakeString([&]{LPWSTR s=nullptr;c->get_Subject(&s);return s;}());
    const std::wstring issuer=TakeString([&]{LPWSTR s=nullptr;c->get_Issuer(&s);return s;}());
    double to=0;c->get_ValidTo(&to);row.expired=to>0&&to<now;
    const std::wstring date=CertificateDate(to);
    const std::wstring expiry=date.empty()?std::wstring():row.expired?TRF(cert_expired_on,date):TRF(cert_expires_on,date);
    row.hint=issuer.empty()?expiry:expiry.empty()?TRF(cert_issued_by,issuer):TRF(cert_issued_by,issuer)+L" · "+expiry;
    return row;
}
HRESULT ClientCertificateRequested(const std::shared_ptr<Tab>& t,ICoreWebView2ClientCertificateRequestedEventArgs* a){
    if(!a)return S_OK;
    ICoreWebView2Deferral* raw=nullptr;if(FAILED(a->GetDeferral(&raw))||!raw)return S_OK;
    auto deferral=std::make_shared<DeferralGuard>(raw);ComRef<ICoreWebView2ClientCertificateRequestedEventArgs> args(a);
    a->put_Handled(TRUE); // our prompt replaces the engine's; every path below writes an explicit answer
    if(halted||!t||t->closed){a->put_Cancel(TRUE);return S_OK;}
    const std::wstring host=TakeString([&]{LPWSTR s=nullptr;a->get_Host(&s);return s;}());
    int port=0;a->get_Port(&port);BOOL proxy=FALSE;a->get_IsProxy(&proxy);
    Com<ICoreWebView2ClientCertificateCollection> list;UINT32 count=0;
    if(SUCCEEDED(a->get_MutuallyTrustedCertificates(&list.p))&&list.p)list->get_Count(&count);
    Log(L"CERTIFICATE requested "+host+L":"+std::to_wstring(port)+(proxy?L" (proxy)":L"")+L" candidates="+std::to_wstring(count));
    if(!count)return S_OK; // nothing to offer: continue without a certificate (Handled, no selection)
    OP::Spec spec;spec.kind=OP::Kind::ClientCertificate;
    spec.origin=host+(port&&port!=443?L":"+std::to_wstring(port):L"");if(proxy)spec.origin=TRF(cert_proxy,spec.origin);
    const double now=(double)time(nullptr);
    for(UINT32 i=0;i<count;++i){Com<ICoreWebView2ClientCertificate> c;if(FAILED(list->GetValueAtIndex(i,&c.p))||!c.p)continue;auto row=DescribeCertificate(c.p,now);spec.accounts.push_back({row.name,row.hint,row.expired});}
    ComRef<ICoreWebView2ClientCertificateCollection> keep(list.p);
    AskPrompt(std::move(spec),t,[args,deferral,keep](OP::Result& r){
        if(r.action==OP::Action::Choose&&r.choice>=0){
            Com<ICoreWebView2ClientCertificate> c;
            if(SUCCEEDED(keep->GetValueAtIndex((UINT32)r.choice,&c.p))&&c.p)args->put_SelectedCertificate(c.p);
            Log(L"CERTIFICATE chosen #"+std::to_wstring(r.choice));
        }else if(r.action==OP::Action::Cancel)Log(L"CERTIFICATE not sent");
        else{args->put_Cancel(TRUE);Log(L"CERTIFICATE request cancelled");}
        deferral->Complete();
    },L"cert:"+host+L":"+std::to_wstring(port));
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
    // SEC-22: a scheme is [a-z][a-z0-9+.-]+ (two characters at least: "c:" is a drive, not an application).
    if(s.size()<2||s[0]<L'a'||s[0]>L'z')return true;
    for(wchar_t c:s)if(!((c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'+'||c==L'.'||c==L'-'))return true;
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
    findBar.Show(FindBarArea(),findSession.tab.lock()==t?findSession.term:L"");findBar.Focus();
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
// The error pages' fonts (#121): Inter Display 400 / 500 / 600 (titles are SemiBold on the boards) and, only for
// a page with a technical sheet, JetBrains Mono for its code. The engine refuses a NavigateToString document over
// ~1.5 MB: Inter Display is embedded as the WOFF2 files the crashed-tab page uses too (assets\fonts\InterDisplay-
// *.woff2, Inter 4.1 like the .ttf), the mono as the reduced file of tests/error-pages/make_page_fonts.py (codes).
// tests/error_pages_test.cpp checks the heaviest page with the real files.
void PageFonts(NativeChrome::ErrorPages::Extras& x,bool mono){
    static const std::wstring regular=DataUri(base+L"\\assets\\fonts\\InterDisplay-Regular.woff2",L"font/woff2",512*1024);
    static const std::wstring medium=DataUri(base+L"\\assets\\fonts\\InterDisplay-Medium.woff2",L"font/woff2",512*1024);
    static const std::wstring semiBold=DataUri(base+L"\\assets\\fonts\\InterDisplay-SemiBold.woff2",L"font/woff2",512*1024);
    static const std::wstring code=DataUri(base+L"\\assets\\fonts\\JetBrainsMono-Codes.ttf",L"font/ttf",256*1024);
    x.fontRegular=regular;x.fontMedium=medium;x.fontSemiBold=semiBold;if(mono)x.fontMono=code;
}
// Local time for the sheet's « Essai » row, as Windows writes it for this user (« 14:32 »).
std::wstring ClockNow(){
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t text[64]{};
    return GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,text,64)?std::wstring(text):std::wstring();
}
bool CopyToClipboard(const std::wstring& text); // main.cpp
bool ErrorPageNavigationStarting(const std::shared_ptr<Tab>& t,const std::wstring& uri,UINT64 id){
    auto& e=t->errorPage;const bool blank=uri==L"about:blank"||uri.rfind(L"data:",0)==0;
    if(blank&&e.pending){e.pending=false;return false;}
    e.starting=uri;e.startingId=id;e.certStatus=-1;e.certUri.clear();e.certIssuer.clear(); // a new navigation invalidates a refused certificate
    if(!blank&&uri!=e.triedUri){e.triedUri.clear();e.tries=0;}   // another address: its own « Essai » count
    e.shown.clear();e.pending=false;e.token.clear();e.code.clear();return false;
}
// Only the failure of the tab's latest top-level http(s) navigation (not an external
// protocol hand-off, not an older navigation) replaces the document.
// `netCode`: the engine's own error code when main read it from its error document (« ERR_CONNECTION_REFUSED »).
bool ShowErrorPage(const std::shared_ptr<Tab>& t,COREWEBVIEW2_WEB_ERROR_STATUS error,UINT64 navigationId,const std::wstring& netCode=L""){
    if(halted||!t||t->closed||!t->view.p)return false;
    const std::wstring target=t->errorPage.starting;
    if(navigationId!=t->errorPage.startingId||(target.rfind(L"http://",0)!=0&&target.rfind(L"https://",0)!=0)||IsSandboxUri(target))return false;
    if(error==COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED||error==COREWEBVIEW2_WEB_ERROR_STATUS_VALID_AUTHENTICATION_CREDENTIALS_REQUIRED||error==COREWEBVIEW2_WEB_ERROR_STATUS_VALID_PROXY_AUTHENTICATION_REQUIRED)return false;
    // Certificate failures: our page only when WE refused this very top-level request (same URL as the
    // navigation, recorded by ServerCertificateErrorDetected); anything else keeps the engine's failure.
    if(NativeChrome::ErrorPages::IsCertificate((int)error)&&!NativeChrome::ErrorPages::CertificateMatches(t->errorPage.certStatus,t->errorPage.certUri,target))return false;
    GUID guid{};wchar_t token[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,token,40))return false;
    const bool online=error!=COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED&&NetworkOnline();
    auto& e=t->errorPage;
    const auto kind=e.forced>=0?(NativeChrome::ErrorPages::Kind)e.forced:NativeChrome::ErrorPages::Classify((int)error,online);
    if(kind==NativeChrome::ErrorPages::Kind::Certificate)Log(L"CERTIFICATE refused page for "+target);
    const bool certificate=kind==NativeChrome::ErrorPages::Kind::Certificate,sheet=certificate||NativeChrome::ErrorPages::CopyFor(kind,L"").secondaryAction==NativeChrome::ErrorPages::Action::Details;
    if(e.triedUri!=target){e.triedUri=target;e.tries=0;}
    ++e.tries;   // this address's consecutive failures in the tab, this one included
    NativeChrome::ErrorPages::Extras extras;PageFonts(extras,sheet);extras.reducedMotion=motion.ReducedMotion();
    const bool forcedCert=certificate&&e.forcedCert>=0;   // sandbox only: the certificate case and its example issuer
    const int status=forcedCert?e.forcedCert:(int)error;
    extras.code=certificate?std::wstring(NativeChrome::ErrorPages::CertificateCode(status)):!netCode.empty()&&!forcedCert?netCode:std::wstring(NativeChrome::ErrorPages::Code(status));
    extras.address=NativeChrome::ErrorPages::AddressOf(target);extras.tries=NativeChrome::ErrorPages::TriesText(e.tries,ClockNow());extras.triesCount=e.tries;
    extras.attempt=e.tries-1;   // time-out: 8, 16 then 32 s before the next automatic try, then none
    extras.restored=e.forcedRestored;extras.certStatus=status;extras.issuer=forcedCert?e.forcedIssuer:e.certIssuer;
    const bool retrying=e.forcedRetrying;e.forced=-1;e.forcedRetrying=e.forcedRestored=false;e.forcedCert=-1;e.forcedIssuer.clear();
    t->uri=target;t->home=false;e.shown=target;e.token=token;e.pending=true;e.kind=(int)kind;e.code=sheet?extras.code:std::wstring();
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
    // The time-out's automatic retry (8, 16 then 32 s): it was never wired, so the countdown ended on nothing
    // (night 25/11: « the automatic retry did not bring a new page »). Only the page that counts down sends it.
    case NativeChrome::ErrorPages::Action::AutoRetry:if(e.kind!=(int)NativeChrome::ErrorPages::Kind::TimedOut)break;
        Log(L"ERROR PAGE automatic retry");Defer([weak,target]{auto tab=weak.lock();if(tab&&!tab->closed)Navigate(tab,target);});break;
    case NativeChrome::ErrorPages::Action::Back:Defer([weak]{auto tab=weak.lock();if(!tab||tab->closed||!tab->view.p)return;BOOL yes=FALSE;tab->view->get_CanGoBack(&yes);if(yes)tab->view->GoBack();else Navigate(tab,L"");});break;
    // The sheet's copy button: the code this page shows (kept by the host, never taken from the page).
    // « Copié » on the button (1.5 s) only once the clipboard holds the code: the page is told « copied ».
    case NativeChrome::ErrorPages::Action::Copy:if(!e.code.empty()&&CopyToClipboard(e.code)){Log(L"ERROR PAGE code copied");accessibility.Notify('C',TR(error_code_copied),L"ErrorCodeCopied");if(t->view.p)t->view->PostWebMessageAsString(L"copied");}break;
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
        // The chrome leaves with its focus, hover and tooltip: keyboard focus on a toolbar control or in the address
        // field goes to the page (a page that asked for fullscreen already has it).
        hover=pressed=0;if(tooltip)SendMessageW(tooltip,TTM_POP,0,0);if(editing||keyboardFocus||GetFocus()==address)FocusPage();
        if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
        // Without its caption style while it covers the monitor, so Windows treats it as fullscreen (taskbar below).
        SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)&~static_cast<LONG_PTR>(WS_CAPTION));
        const RECT m=MonitorScreenRect();SetWindowPos(window,HWND_TOP,m.left,m.top,m.right-m.left,m.bottom-m.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER);}
    else if(!full&&wasFull){SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)|WS_CAPTION);if(fullscreen.saved)SetWindowPlacement(window,&fullscreen.placement);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}
    Layout();
    // C7: the pill names the real origin of the page that took the screen (« Cette page » only
    // without one). No keyboard-lock variant here: WebView2 exposes no keyboard-lock signal.
    if(full){auto t=Active();fullscreenHint.Show(MonitorScreenRect(),fullscreen.page,fullscreen.page&&t?OriginLabel(t->uri):std::wstring());}else fullscreenHint.Hide();
    RequestElementFrame();
}
void SetPageFullscreen(const std::shared_ptr<Tab>& t,bool on){
    if(on&&t!=Active())return;const bool was=InFullscreen();fullscreen.page=on;
    Log(std::wstring(L"FULLSCREEN page ")+(on?L"on":L"off"));if(was!=InFullscreen()||on)ApplyFullscreen(was);
    else Layout();   // the page left while F11 stays: the window keeps its size, its pages (a split) come back
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
// A5 local part: a deletion delta applied under the profile lock to the CURRENT files (src/trace-erase.h),
// so entries another window added in the period are erased too and nothing outside it is touched.
// Returns S_OK only if every selected category really is erased on disk.
HRESULT ClearLocalTracesSelected(const std::vector<bool>& c,std::int64_t since){
    auto on=[&](size_t i){return i<c.size()&&c[i];};
    HRESULT result=S_OK;
    auto report=[&](TraceErase::Outcome o,const wchar_t* what){if(o==TraceErase::Outcome::Done)return;result=o==TraceErase::Outcome::Busy?HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION):E_FAIL;Log(std::wstring(L"CLEAR ")+what+(o==TraceErase::Outcome::Busy?L": profile busy, NOT erased":o==TraceErase::Outcome::UnknownFormat?L": unknown file format, NOT erased (left untouched)":L": write failed, NOT erased"));};
    if(on(0)){
        closedTabs.clear();if(since<=0)ClearSiteIcons();
        if(smoke||privateWindow){std::vector<std::wstring> urls;for(auto& v:history.Items())if(since<=0||v.last>=since)urls.push_back(v.url);for(auto& u:urls)history.Remove(u);}
        else{std::vector<std::wstring> gone;report(TraceErase::History(dataDir,HistoryFile(),since,history,historyBase,5000,&gone),L"history");
            // Review F8: a period erase also forgets the favicons of sites no longer in the history.
            std::set<std::wstring> sites;for(const auto& u:gone)sites.insert(AddressBar::SiteOf(u));for(const auto& site:sites)if(!site.empty()&&!history.HasSite(site))ForgetSiteIcon(site);}
        if(!WorkspaceParked())historyDrawer.Changed();
    }
    if(on(3)){
        if(smoke||privateWindow)downloads.erase(std::remove_if(downloads.begin(),downloads.end(),[since](const DL::Item& d){return DL::Persistent(d)&&(since<=0||d.started>=since||d.started==0);}),downloads.end());
        else report(TraceErase::Downloads(dataDir,downloadsFile,since,downloads,downloadsBase,nextDownloadId),L"downloads");
        PruneDownloadOps();   // this profile's operations only (a parked profile's live downloads keep theirs)
        DownloadsChanged();
    }
    return result;
}
void StartClearSelected(const std::vector<bool>& c,int period,std::function<void(HRESULT)> done){
    auto on=[&](size_t i){return i<c.size()&&c[i];};
    const std::int64_t since=ClearPeriodStartMs(period);
    const HRESULT local=ClearLocalTracesSelected(c,since);
    // A local part that was not erased is reported to the A5 flow as a failure (never « effacé »).
    if(FAILED(local)){auto original=done;done=[original,local](HRESULT hr){if(original)original(FAILED(hr)?hr:local);};}
    int kinds=0;
    if(on(0))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_BROWSING_HISTORY;
    if(on(1))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_SITE;
    if(on(2))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE;
    if(on(3))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_DOWNLOAD_HISTORY;
    if(on(4))kinds|=COREWEBVIEW2_BROWSING_DATA_KINDS_SETTINGS;
    Log(L"SETTINGS clear selected kinds="+std::to_wstring(kinds)+L" period="+std::to_wstring(period));
    Com<ICoreWebView2Profile2> profile;
    // Engine categories selected but no profile to clear them (no live tab, older runtime): a real
    // failure for those categories, never « effacé » (the local part may have succeeded).
    if(!kinds){if(done)done(S_OK);return;}
    if(!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){Log(L"SETTINGS clear data: no ICoreWebView2Profile2: engine data NOT cleared");if(done)done(E_NOINTERFACE);return;}
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
        modal.SetBusy(true,TR(annotations_clearing));
        const MD::Result now=modal.Snapshot();
        if(demo){Defer([result]{if(result)result(true);modal.Finish(MD::AcceptButton);});return;}
        StartClearSelected(now.checks,now.select,[result](HRESULT hr){
            if(FAILED(hr)){modal.SetBusy(false);modal.SetError(TRF(clear_data_failed,HrText(hr)));return;}
            if(result)result(true);modal.Finish(MD::AcceptButton);AnnounceDataCleared();});
    };
    AskModal(std::move(spec),nullptr,[result](const MD::Result& r){if(r.button!=MD::AcceptButton&&result)result(false);});
}
bool closeConfirmed=false;
// WM_CLOSE: true = close now. Otherwise the modal asks and re-posts WM_CLOSE on « Fermer ».
int ParkedDownloadsRunning();
bool CloseWindowAllowed(bool demo,std::function<void(bool)> result){
    // Never asked for the updater's install handoff (closeWithoutPrompt) or a clear on exit
    // already under way: a question there would block the install / the shutdown.
    if(!demo&&(closeConfirmed||smoke||halted||closeWithoutPrompt||exitClear!=ExitClear::Idle))return true;
    const int n=(int)std::count_if(pages.begin(),pages.end(),[](const std::shared_ptr<Tab>& t){return t&&!t->closed;});
    // A6 variant: a download still running is cancelled by closing: always asked (independent of
    // « Ne plus demander », which is about tabs).
    const DL::Item* running=nullptr;for(const auto& d:downloads)if(d.state==DL::State::InProgress){running=&d;break;}
    // Same-window profiles: a download running in a profile cached in this window is cancelled too.
    const int parkedRunning=demo?0:ParkedDownloadsRunning();
    DL::Item parkedItem;if(!running&&parkedRunning){parkedItem.path=TRP(close_window_other_profile_downloads,parkedRunning);running=&parkedItem;}
    if(!demo&&!running&&(n<=1||!settings.confirmCloseTabs))return true;
    if(modalOpen&&!demo)return false; // one question at a time
    auto spec=running&&!demo?MD::CloseWindowSpec(std::max(n,1),DL::FileName(running->path),BrowserUpdate::ToastModel::FormatSize((std::uint64_t)std::max<std::int64_t>(running->received,0),(std::uint64_t)std::max<std::int64_t>(running->total,0)) /* E2: 1 decimal, total unknown = received only */):MD::CloseWindowSpec(std::max(n,2));
    if(running&&!demo&&n<=1)spec.title=TR(close_window_question);
    // F4: a private window's tabs cannot be reopened (no history, no Ctrl+Maj+T) and « Ne plus demander »
    // is not offered here (its settings write belongs to the normal windows' transaction).
    if(privateWindow&&!demo&&!running){spec.body=TR(close_window_private_note);spec.bodyKeys.clear();spec.checkLabel.clear();spec.checkInActions=false;}
    spec.demo=demo;
    AskModal(std::move(spec),nullptr,[demo,result](const MD::Result& r){
        const bool ok=r.button==MD::AcceptButton;if(result)result(ok);
        if(demo)return;
        if(!ok){Log(L"TABS window close cancelled");return;}
        if(r.checked&&settings.confirmCloseTabs){settings.confirmCloseTabs=false;SaveSettings();Log(L"TABS « Ne plus demander » saved");}
        closeConfirmed=true;PostMessageW(window,WM_CLOSE,0,0);
    });
    return false;
}
// A7: « Nouvelle fenêtre » really opens a normal window with the page that did not fit (D4: a
// failed launch is shown with Réessayer and its URLs are kept).
void OpenWindowWith(const std::vector<std::wstring>& urls);void RetryWindowLaunch(const std::wstring& id);
void ShowTabLimitToast(const std::wstring& url){
    NativeChrome::ContextToast::Spec spec=NativeChrome::ContextToast::TabLimitSpec(128);
    if(privateWindow)spec.action.clear(); // no handover from a private window: no inert action
    contextToast.Show(spec,PageScreenRect(),[url](bool acted){if(acted)OpenWindowWith({url});});Log(L"TABS limit toast");
}
void ShowPartialRestoreToast(const std::vector<std::wstring>& urls){
    auto spec=NativeChrome::ContextToast::PartialRestoreSpec((int)urls.size(),128);
    contextToast.Show(spec,PageScreenRect(),[urls](bool acted){if(acted)OpenWindowWith(urls);});Log(L"TABS partial restore: "+std::to_wstring(urls.size())+L" not opened");
}
void ShowWindowLaunchFailed(const std::wstring& id){
    NativeChrome::ContextToast::Spec s;s.automationId=L"WindowLaunchFailedToast";s.icon=NativeChrome::ContextToast::Icon::Error;s.error=true;s.durationMs=0;
    s.message=TR(window_launch_failed);
    s.detail=id.empty()?TR(window_launch_failed_tabs_lost):TR(window_launch_failed_tabs_kept);
    if(!id.empty())s.action=TR(downloads_action_retry);
    contextToast.Show(s,PageScreenRect(),[id](bool acted){if(acted)RetryWindowLaunch(id);});Log(L"WINDOW launch failed toast");
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
// A8 live status: the update engine's real view (defined with the update host in main.cpp).
MD::Status AboutStatusNow();
void AboutUpdateAction(int button);
// Public project destinations (the releases repository is public; the source repository is not).
constexpr const wchar_t* ReleasesUrl=L"https://github.com/VALXXII/browser-releases/releases";
constexpr const wchar_t* IssuesUrl=L"https://github.com/VALXXII/browser-releases/issues/new";
std::wstring ReleaseNotesUrl(const std::wstring& version){
    const bool numbered=!version.empty()&&iswdigit(version[0]);
    return numbered?std::wstring(ReleasesUrl)+L"/tag/v"+version:std::wstring(ReleasesUrl);
}
bool AboutOpen(){return modalOpen&&modalActive.spec.kind==MD::Kind::About&&!modalActive.spec.demo;}
// The update engine notified: an open « À propos » shows the new state at once.
void RefreshAboutStatus(){if(AboutOpen())modal.SetStatus(AboutStatusNow());}
void ShowReleaseNotes(bool mini,std::function<void()> shown=nullptr,bool activate=true,ReleaseNotes::Content card={});
void ShowAboutDialog(int demoState){
    auto spec=MD::AboutSpec(RunningVersion(),demoState<0?MD::AboutUpToDate:demoState);
    if(demoState>=0)spec.demo=true;
    bool live=UpdateCheckAvailable();
#ifdef BROWSER_PREVIEW
    live=false; // the preview's engine is compiled with updates off
#endif
    if(demoState>=0){}
    else if(!live){spec.status.icon=MD::StatusIcon::None;spec.status.title=TR(updates_disabled_title);spec.status.detail=TR(updates_disabled_preview);spec.status.hasButton=false;}
    else spec.status=AboutStatusNow();
    if(demoState>=0)spec.onButton=[](int id){if(id==MD::AboutCheckButton)modal.SetStatus(MD::AboutStatus(MD::AboutChecking));};   // sandbox: nothing is checked
    else spec.onButton=[](int id){AboutUpdateAction(id);RefreshAboutStatus();};
    const bool demo=demoState>=0;
    AskModal(std::move(spec),nullptr,[demo](const MD::Result& r){
        if(r.button==MD::AboutLicensesLink){std::wstring dir=base;ShellExecuteW(window,L"open",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        else if(r.button==MD::AboutNotesLink){Log(L"ABOUT release notes: Nouveautés (full)");Defer([]{ShowReleaseNotes(false,nullptr,true);});}
        else if(r.button==MD::AboutReportLink){Log(L"ABOUT report a problem");Defer([]{NewTab(IssuesUrl,-1,L"");});}
        else if(!demo&&r.button==MD::AboutRestartButton)AboutUpdateAction(MD::AboutRestartButton);
    });
}
void ShowAboutDialog(){ShowAboutDialog(-1);}
// « Nouveautés » (src/release-notes.h, Paper p-L-0 142E-0 / 144O-0): the build's own notes (release-notes/notes.json and
// its history), never fetched. `card`: what to show (the automatic Mini: the versions the user has not seen yet); empty =
// the most recent version's notes (À propos). A build without notes shows no card: À propos then opens the release's GitHub page.
// `mini`: the automatic Mini after an update (« Voir tout » expands it in place); else the Full view.
// `activate`: take the keyboard (opened by hand from À propos); the automatic card leaves it to the page.
// `shown` runs only once the card is really on screen. One card at a time: a new request replaces it.
std::wstring ReleaseNotesLogo(){return base+L"\\assets\\lottie\\logo-spin.json";}
void ShowReleaseNotes(bool mini,std::function<void()> shown,bool activate,ReleaseNotes::Content card){
    const auto& history=ReleaseNotes::History();
    const ReleaseNotes::Content content=!card.Empty()?std::move(card):ReleaseNotes::Latest(history);
    if(content.Empty()){
        if(mini){Log(L"RELEASE NOTES none in this build");return;}
        Log(L"RELEASE NOTES none in this build: release page");NewTab(ReleaseNotesUrl(RunningVersion()),-1,L"");return;
    }
    std::vector<MD::NoteSection> sections;const auto notes=ReleaseNotes::Sections(content);
    for(const auto& s:notes)sections.push_back({s.kind==ReleaseNotes::Kind::Fixed,s.title,s.items});
    const std::wstring subtitle=ReleaseNotes::Subtitle(content);   // « Depuis la version … »: several versions (Paper 26KK-0)
    auto spec=MD::ReleaseNotesSpec(std::move(sections),(subtitle.empty()?L"":subtitle+L". ")+ReleaseNotes::Spoken(notes),ReleaseNotesLogo(),mini,subtitle);
    spec.onShown=[mini,shown]{Log(std::wstring(L"RELEASE NOTES shown ")+(mini?L"mini":L"full"));if(shown)shown();};
    spec.onButton=[](int id){if(id==MD::Dialog::NotesExpandHit)Log(L"RELEASE NOTES expanded (Voir tout)");};
    spec.activate=activate;
    if(halted||!notesCard.Window())return;
    if(notesCard.Visible())notesCard.Abort();
    if(!notesCard.Show(std::move(spec),PageScreenRect(),[](const MD::Result&){Log(L"RELEASE NOTES closed");}))Log(L"RELEASE NOTES show failed");
}
// Automatic Mini after a successful update: the notes of every version the user has not seen yet (ReleaseNotes::Decide).
// `startupInstalled`: the update engine reported Installed when it started (the helper-launched process).
// `dataRoot`: the GLOBAL data root (profileRoot, else dataDir) where the read state lives; never a private window's.
// True when the card will open: it then replaces the « Mise à jour installée » toast.
bool ReleaseNotesAtStartup(bool startupInstalled,const std::wstring& installRoot,const std::wstring& version,const std::wstring& dataRoot){
    if(privateWindow||smoke||dataRoot.empty())return false;
    std::string previousBytes;ReleaseNotes::Previous previous;
    if(startupInstalled){if(ProfileStore::ReadAll(installRoot+L"\\previous",previousBytes))previous=ReleaseNotes::ParsePrevious(previousBytes);else previous.kind=ReleaseNotes::Previous::Kind::Invalid;}
    const auto state=startupInstalled?ReleaseNotes::Load(dataRoot):ReleaseNotes::State{};
    std::string current;for(wchar_t c:version)current.push_back(c<128?static_cast<char>(c):'?');
    const auto& history=ReleaseNotes::History();ReleaseNotes::Content card;
    const auto decision=ReleaseNotes::Decide(startupInstalled,previous,current,state,history,&card);
    if(!startupInstalled)return false; // ordinary launch, other window, dev build: nothing read, nothing logged
    {const char* d=ReleaseNotes::DecisionText(decision);const char* st=ReleaseNotes::StatusText(state.status);const std::string seen=ReleaseNotes::SeenVersion(state,history);
     Log(L"RELEASE NOTES startup: "+std::wstring(d,d+std::strlen(d))+L" (state "+std::wstring(st,st+std::strlen(st))+L") seen "+std::wstring(state.seen.begin(),state.seen.end())+L" = version "+(seen.empty()?std::wstring(L"unknown"):std::wstring(seen.begin(),seen.end()))+L", "+std::to_wstring(history.size())+L" version(s) with notes, card "+std::to_wstring(card.added.size())+L"+"+std::to_wstring(card.fixed.size()));}
    if(decision==ReleaseNotes::Decision::Baseline){const bool ok=ReleaseNotes::MarkSeen(dataRoot,current);Log(ok?L"RELEASE NOTES baseline recorded (first install)":L"RELEASE NOTES baseline NOT recorded");return false;}
    if(decision!=ReleaseNotes::Decision::Show)return false;
    Defer([dataRoot,card]{const std::string upTo=card.id;ShowReleaseNotes(true,[dataRoot,upTo]{const bool ok=ReleaseNotes::MarkSeen(dataRoot,upTo);Log(ok?L"RELEASE NOTES seen recorded":L"RELEASE NOTES seen NOT recorded (kept as it was)");},false,card);});
    return true;
}
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
    std::string body;int status=404;const wchar_t* reason=L"Not Found";   // i18n-exempt: HTTP reason phrase
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
// ---- Password vault, B04 capture → consent → save (decision 1 A). Active in dev/release since the independent
// review; nothing below runs while Vault::Active() is false (engine-control test hosts: no script, no message accepted).
// The page only reports a submitted login of ITS OWN top-level document; the origin is the one the
// ENGINE reports for the message (never a value from the page); the vault is written only after the
// user's explicit « Enregistrer » / « Mettre à jour » (B04), through Vault::Commit (fail closed).
// A private window never captures (D3). Secret copies held by the host are wiped after use (V7).
// Audit SEC-19: the path and stamp of the last completed read (VaultCached re-reads only when they change).
bool vaultStampRead=false;std::wstring vaultStampPath;Vault::FileStamp vaultStamp;
Vault::Store vault,vaultBase;bool vaultLoaded=false;std::set<unsigned> vaultPromptOpen;std::map<unsigned,Vault::CaptureBinding> vaultCapture;   // tab serial -> its pending capture (review N13)
// Review R-2: a private window reads (never writes, Vault::WritesForbidden) the vault of the profile it was opened
// from: set once at start by SelectProfile to Profiles::PrivateReadDir(root, --profile). "" = that profile is not a
// registered one: no vault at all (never another profile's). A normal window: its (current) profile's folder.
std::wstring privateVaultDir;
std::wstring VaultDir(){return privateWindow?privateVaultDir:dataDir;}
std::wstring VaultFile(){return VaultDir()+L"\\passwords";}
// Capture (review 885d322 #1, owner decision A): the capture script runs in an ISOLATED world of every document
// of the tab and its frames (Vault::CaptureScript, installed by VaultCaptureInstall) and reports ONCE, directly,
// through a DevTools binding that only exists there; it keeps nothing (no web storage, no resend). The secret then
// lives only in the host's memory, bound to this tab, the offer's origin (the reporting document's real origin)
// and the tab's top-level origin, until « Enregistrer » / « Plus tard », the tab's next navigation, the tab's or
// the window's close, or a newer capture; every copy is wiped (VaultCaptureDrop, PromptJob). The page's own
// message channel (chrome.webview.postMessage) never reaches the vault (review R-6).
void Wipe(std::wstring& s){if(!s.empty())SecureZeroMemory(s.data(),s.size()*sizeof(wchar_t));s.clear();}
// Review F11 (885d322 #2): what the file holds, or WHY it cannot be used. Only an ABSENT file is an empty
// vault; a file that exists with 0 bytes, a read or sharing error, an unknown format or damage is
// « unavailable » and is never taken as empty. `lockMs` 0 = never wait on the UI (periodic checks).
void VaultNotice(const std::wstring& message,const std::wstring& detail);
enum class VaultLoad { Ok, Busy, Unavailable };
VaultLoad ReadVault(DWORD lockMs){
    std::string bytes;bool exists=false,read=false,tooLarge=false;
    if(privateWindow&&privateVaultDir.empty()){vault=Vault::Store{};vaultBase=Vault::Store{};vaultLoaded=true;return VaultLoad::Ok;}   // R-2: no known opener profile: no vault, nothing read
    {ProfileStore::Lock lock(VaultDir(),lockMs);if(!lock.Held())return VaultLoad::Busy;   // the cache, if any, is left as it was
     const Vault::FileStamp stamp=Vault::StampOf(VaultFile());   // under the lock: no commit between the stamp and the read
     exists=stamp.exists;tooLarge=stamp.size>Vault::Store::MaxFileBytes;read=tooLarge||ProfileStore::ReadAll(VaultFile(),bytes);   // N-8: an oversized file is never read
     if(read){vaultStampRead=true;vaultStampPath=VaultFile();vaultStamp=stamp;}else vaultStampRead=false;}
    static std::wstring lastWhy;
    auto unavailable=[](const wchar_t* why){if(lastWhy!=why){Log(why);lastWhy=why;}vault=Vault::Store{};vaultBase=Vault::Store{};vaultLoaded=false;return VaultLoad::Unavailable;};
    if(!read)return unavailable(L"VAULT file could not be read (sharing or I/O error): unavailable, not empty");
    if(tooLarge)return unavailable(L"VAULT file over 16 MB: damaged, not read");
    if(exists&&bytes.empty())return unavailable(L"VAULT file exists with 0 bytes: damaged, not opened");
    if(!bytes.empty()&&ProfileStore::FirstLine(bytes)!=Vault::Store::Header)return unavailable(L"VAULT unknown file format: not opened");
    std::istringstream in(bytes);bool damaged=false;Vault::Store read2=Vault::Store::Read(in,&damaged);
    if(damaged)return unavailable(L"VAULT file damaged: not opened");
    vault=std::move(read2);vaultBase=vault;vaultLoaded=true;lastWhy.clear();return VaultLoad::Ok;
}
std::wstring VaultUnavailableText(VaultLoad r){return r==VaultLoad::Busy?TR(vault_busy):TR(vault_unreadable);}
// Explicit user actions (capture answer, fill, choice, manager): always a FRESH read (a removal made in
// another window is honoured, normal or private), bounded wait, a refusal is said.
bool LoadVault(const wchar_t* title){const auto r=ReadVault(1000);if(r==VaultLoad::Ok)return true;VaultNotice(title,VaultUnavailableText(r));return false;}
// Periodic UI checks (control visibility, Settings count): at most one check every 2 s, and the file is read and
// parsed again only when it changed since the last read (audit SEC-19: one attribute query otherwise, never a parse
// on the UI thread for nothing); a busy profile keeps the last known state. {usable, count}.
ULONGLONG vaultCheckedAt=0;   // last periodic check (0 = never)
std::pair<bool,std::size_t> VaultCached(){
    ULONGLONG& last=vaultCheckedAt;const ULONGLONG now=GetTickCount64();
    if(last==0||now-last>2000){last=now?now:1;
        const std::wstring path=VaultFile();
        if(Vault::CacheStale(vaultStampRead,vaultStampPath,vaultStamp,path,Vault::StampOf(path)))ReadVault(0);}
    return {vaultLoaded,vaultLoaded?vault.Entries().size():0};
}
// Minimal JSON string field reader for the capture message (the engine gives the message as JSON).
bool JsonField(const std::wstring& json,const std::wstring& key,std::wstring& out){
    out.clear();out.reserve(json.size());   // review N5: one buffer, no reallocated copy of a secret left behind
    const std::wstring k=L"\""+key+L"\":\"";auto at=json.find(k);if(at==std::wstring::npos)return false;at+=k.size();
    for(size_t i=at;i<json.size();++i){wchar_t c=json[i];
        if(c==L'"')return true;
        if(c==L'\\'&&i+1<json.size()){wchar_t e=json[++i];
            if(e==L'u'&&i+4<json.size()){out+=static_cast<wchar_t>(std::wcstoul(json.substr(i+1,4).c_str(),nullptr,16));i+=4;}
            else out+=e==L'n'?L'\n':e==L't'?L'\t':e==L'r'?L'\r':e==L'b'?L'\b':e==L'f'?L'\f':e;}
        else out+=c;}
    return false;
}
unsigned vaultNoticesShown=0;std::wstring vaultLastNotice;   // tests: what the vault last said
// Review N-7: an entry Windows cannot decrypt here (profile copied to another PC, Windows password reset by an
// administrator) is said, never a silent nothing.
bool VaultRevealForFill(const Vault::Entry& e,const std::wstring& origin,std::wstring& secret){
    if(vault.Reveal(e.id,origin,secret))return true;
    Wipe(secret);Log(L"VAULT fill not done: the entry cannot be decrypted on this device");VaultNotice(TR(vault_fill_not_done),TR(vault_secret_unreadable));return false;
}
void VaultNotice(const std::wstring& message,const std::wstring& detail){++vaultNoticesShown;vaultLastNotice=detail;NativeChrome::ContextToast::Spec s;s.automationId=L"VaultNotice";s.icon=NativeChrome::ContextToast::Icon::Error;s.error=true;s.message=message;s.detail=detail;contextToast.Show(s,PageScreenRect(),[](bool){});Log(L"VAULT notice: "+message);}
std::wstring EngineSource(const std::shared_ptr<Tab>& t);
void VaultCaptureDrop(const std::shared_ptr<Tab>& t);
// Review N13 (defence in depth): a B04 offer is shown and answered only while the tab's ENGINE source is
// still the capture's origin, with no error / TLS page shown. Otherwise it is dropped with its secret.
bool VaultOfferStillValid(const std::shared_ptr<Tab>& t,const std::wstring& origin){
    return t&&!t->closed&&t->view.p&&t->errorPage.shown.empty()&&t->https.shown.empty()&&Vault::CanonicalOrigin(EngineSource(t))==origin;
}
// Review N7: an unreadable vault is not re-read (nor announced again) for every page message while the file
// has not changed: a page cannot loop the UI on the profile lock or on notices.
struct VaultFileStamp{bool exists=false;FILETIME write{};ULONGLONG size=0;bool operator==(const VaultFileStamp& o)const{return exists==o.exists&&write.dwLowDateTime==o.write.dwLowDateTime&&write.dwHighDateTime==o.write.dwHighDateTime&&size==o.size;}};
VaultFileStamp VaultStampNow(){VaultFileStamp s;WIN32_FILE_ATTRIBUTE_DATA a{};if(GetFileAttributesExW(VaultFile().c_str(),GetFileExInfoStandard,&a)){s.exists=true;s.write=a.ftLastWriteTime;s.size=(ULONGLONG(a.nFileSizeHigh)<<32)|a.nFileSizeLow;}return s;}
bool vaultCaptureFailed=false;VaultFileStamp vaultCaptureFailedAt;
// Review V-5: the capture (DevTools domains on the tab) is attached only while it can be used: vault active, normal
// window, « Mots de passe » on. Turning the setting off detaches it from every tab (VaultCaptureSync).
bool VaultCaptureWanted(){return Vault::Active()&&!privateWindow&&settings.passwords;}
std::map<unsigned,Vault::TwoStep> vaultTwoStep;   // tab serial -> the username of a first login step (owner decision A)
// Review V-4: per tab, whether its pending offer (held or shown) came from the page itself, and when it was last replaced.
struct VaultPending{bool top=false;std::int64_t replacedAt=0;};
std::map<unsigned,VaultPending> vaultPending;
// Review V-7: a login that left its page waits here (its secret held by the host only) until the next document of the
// same origin and kind says whether it asks for a password again; 15 s at most; dropped with the tab's capture binding.
struct VaultAwait{Vault::CaptureMessage m;std::wstring top;std::uint64_t seq=0;};
std::map<unsigned,VaultAwait> vaultAwait;std::uint64_t vaultAwaitSeq=0;
// Review V-7: a login held until the next page is dropped after 15 s without an answer page. Review N-3: the timer runs
// in the scope of the profile that held it (DeferAfter), so a hidden profile's tab never clears the on-screen profile's
// vaultAwait / vaultCapture / vaultPromptOpen.
void DeferAfter(UINT ms,std::function<void()> job);   // main.cpp: a timer that runs in the scope of the profile that set it
void VaultCaptureDrop(const std::shared_ptr<Tab>& t);
void VaultHoldTimeout(unsigned serial,std::uint64_t seq,std::weak_ptr<Tab> weak){
    DeferAfter(15000,[serial,seq,weak]{auto it=vaultAwait.find(serial);if(it==vaultAwait.end()||it->second.seq!=seq)return;vaultAwait.erase(it);Log(L"VAULT capture dropped: no answer page within 15 s");if(auto tab=weak.lock())VaultCaptureDrop(tab);});
}
void After(UINT ms,std::function<void()> f);
void VaultOffer(const std::shared_ptr<Tab>& t,Vault::CaptureMessage& m,const std::wstring& top);
// Runtime.bindingCalled of the capture binding, from the tab's own DevTools session or one of its frames' sessions.
// Runs in the scope of the profile that owns the tab (the receiver's callback), also while that profile is hidden.
// Everything the page controls is a value; the offer's origin is the isolated world's self.origin (Vault::ParseCaptureEvent
// refuses "null" and http), a top-level report must match the engine's own top-level source, and the tab must still show
// that top-level origin (N13). What is offered comes from what the vault holds (Vault::DecideOffer).
void VaultCaptureEvent(const std::shared_ptr<Tab>& t,std::wstring json){
    struct WipeOnExit{std::wstring& m;~WipeOnExit(){Wipe(m);}} wipeJson{json};
    if(halted||!t||t->closed||json.size()>8192)return;
    {std::wstring name;if(!Vault::JsonStringAt(json,Vault::JsonMemberAt(json,Vault::JsonRoot(json),L"name"),name)||name!=Vault::CaptureBindingName)return;}   // #121: the forms world's binding is not a capture (src/forms-integration.h)
    if(!VaultCaptureWanted()||smoke)return;   // inactive / private / « Mots de passe » off (F4): nothing kept
    Vault::CaptureMessage m;
    if(!Vault::ParseCaptureEvent(json,m)){Log(L"VAULT capture ignored: not a capture of a secure, non-opaque document");return;}
    const std::wstring top=Vault::CanonicalOrigin(EngineSource(t));
    if(top.empty()||(m.top&&m.origin!=top)||!VaultOfferStillValid(t,top)){if(m.kind!=Vault::CaptureMessage::Kind::Doc)Log(L"VAULT capture ignored: the tab is not on a secure page of that document");return;}   // http top, or the tab is already elsewhere (D2, N13)
    const std::int64_t now=NativeChrome::DownloadsUI::UnixNowMs();
    if(m.kind==Vault::CaptureMessage::Kind::Doc){   // review V-7: the outcome of a login that left its page
        auto it=vaultAwait.find(t->serial);if(it==vaultAwait.end())return;
        const auto o=Vault::LoginOutcome(it->second.m.origin,it->second.m.top,m.origin,m.top,m.passwordField);
        if(o==Vault::Outcome::Wait)return;
        Vault::CaptureMessage held=it->second.m;const std::wstring heldTop=it->second.top;vaultAwait.erase(it);
        if(o==Vault::Outcome::Drop){Log(L"VAULT capture dropped: the next page asks for the password again (login refused)");VaultCaptureDrop(t);return;}
        VaultOffer(t,held,heldTop);return;}
    if(m.kind==Vault::CaptureMessage::Kind::User){vaultTwoStep[t->serial].Remember(m.origin,m.user,now);Log(L"VAULT two-step: username of a first step remembered for this tab");return;}
    {static std::map<unsigned,ULONGLONG> last;const ULONGLONG tick=GetTickCount64();auto& at=last[t->serial];   // review R1: at most one capture per tab per second
     if(at&&tick-at<1000)return;at=tick;}
    // Review V-4 / N3: a newer capture replaces the pending one (and its secret) only if allowed: never a frame's over the
    // page's own, and at most once every 5 s.
    const bool pending=vaultPromptOpen.count(t->serial)||vaultCapture.count(t->serial)||vaultAwait.count(t->serial);
    auto& p=vaultPending[t->serial];
    if(pending&&!Vault::MayReplace(p.top,m.top,now,p.replacedAt)){Log(L"VAULT capture ignored: it may not replace the pending offer of this tab");return;}
    if(pending){VaultCaptureDrop(t);p.replacedAt=now;}
    p.top=m.top;
    if(m.user.empty()){auto it=vaultTwoStep.find(t->serial);if(it!=vaultTwoStep.end())m.user=it->second.For(m.origin,now);}
    vaultCapture[t->serial].Start(top,t->navInFlight);   // bound to the TOP document's submission (a frame's capture: its page's)
    if(m.via==L"leave"){   // review V-7: offered only once the next document shows the login succeeded
        auto& w=vaultAwait[t->serial];w.m=m;w.top=top;w.seq=++vaultAwaitSeq;const std::uint64_t seq=w.seq;const unsigned serial=t->serial;std::weak_ptr<Tab> weak=t;
        Log(std::wstring(L"VAULT capture held until the next page loads (")+(m.top?L"page":L"frame")+L")");
        VaultHoldTimeout(serial,seq,weak);
        return;}
    VaultOffer(t,m,top);
}
void VaultOffer(const std::shared_ptr<Tab>& t,Vault::CaptureMessage& m,const std::wstring& top){
    if(vaultCaptureFailed&&VaultStampNow()==vaultCaptureFailedAt){VaultCaptureDrop(t);return;}   // N7: still the same unreadable file, already said
    {const auto load=ReadVault(1000);
     if(load!=VaultLoad::Ok){if(load==VaultLoad::Unavailable){vaultCaptureFailed=true;vaultCaptureFailedAt=VaultStampNow();}   // review R2: only a really unreadable file is remembered; busy is retried
      VaultCaptureDrop(t);VaultNotice(TR(vault_not_saved),VaultUnavailableText(load));return;}}   // F5/F11
    vaultCaptureFailed=false;
    const Vault::Offer offer=Vault::DecideOffer(vault,m.origin,m.user,m.secret,m.current,m.autofilled);
    if(offer.kind==Vault::Offer::Kind::None){Log(L"VAULT capture: nothing to offer (the vault already holds this password)");VaultCaptureDrop(t);return;}
    const std::wstring origin=m.origin;
    OP::Spec s;s.demo=false;s.origin=origin;s.topOrigin=top;s.inFrame=!m.top;s.user=offer.user;s.secret=m.secret;Wipe(m.secret);Wipe(m.current);s.migration=offer.migration;
    if(offer.kind==Vault::Offer::Kind::Save)s.kind=OP::Kind::PasswordSave;
    else{s.kind=OP::Kind::PasswordUpdate;if(offer.kind==Vault::Offer::Kind::Choose){s.user.clear();for(auto& u:offer.accounts)s.accounts.push_back({u,L""});}}   // several: explicit choice (D1)
    vaultPromptOpen.insert(t->serial);const unsigned serial=t->serial;std::weak_ptr<Tab> weak=t;
    // Review V-1: the offer belongs to the profile that captured it: shown only while that profile is on screen
    // (PumpPrompt), answered and saved in ITS scope, into ITS vault.
    Workspace* owner=WorkspaceNow();
    Log(std::wstring(L"VAULT capture: B04 prompt (")+(m.top?L"page":L"frame")+L", "+(m.via.size()<=8?m.via:std::wstring(L"?"))+(offer.migration?L", migration of an engine password":L"")+L")");   // review N4: no origin in the log
    AskPrompt(std::move(s),t,[origin,top,serial,weak,owner](OP::Result& r){
        if(WorkspaceDead(owner)){Wipe(r.password);return;}   // that profile left this window: nothing saved anywhere
        WorkspaceScope scope(owner);
        vaultPromptOpen.erase(serial);vaultCapture.erase(serial);Log(L"VAULT B04 answer action="+std::to_wstring((int)r.action)+(r.password.empty()?L" (no password)":L""));
        if(r.action==OP::Action::Save&&!r.password.empty()&&!VaultOfferStillValid(weak.lock(),top)){Log(L"VAULT capture dropped at the answer: the tab is no longer on its origin");Wipe(r.password);return;}   // N13
        if(r.action==OP::Action::Save&&!r.password.empty()&&LoadVault(TR(vault_not_saved))){   // F11: fresh file at the answer
            const auto saved=vault.Save(origin,r.user,r.password,NativeChrome::DownloadsUI::UnixNowMs());
            const auto res=saved==Vault::Store::Saved::Refused?ProfileStore::Result::Failed:Vault::Commit(VaultDir(),VaultFile(),vaultBase,vault);
            const bool ok=res==ProfileStore::Result::Written||res==ProfileStore::Result::Unchanged;
            Log(std::wstring(L"VAULT ")+(ok?L"saved":L"NOT saved (refused, busy, damaged or unknown file)"));
            if(!ok){vault=vaultBase;VaultNotice(TR(vault_not_saved),res==ProfileStore::Result::Busy?TR(vault_busy):TR(vault_write_failed));}   // F5: memory back to the file
        }
        Wipe(r.password);
    },L"vault:"+std::to_wstring(serial));
}
// A page message that still uses the retired capture channel: swallowed and wiped, never a capture (review R-6).
bool VaultCapture(const std::shared_ptr<Tab>&,const std::wstring&,std::wstring& message){
    if(message.find(L"\"browserVault\"")==std::wstring::npos)return false;
    Wipe(message);return true;
}
// Owner decision A: install the capture in the tab's DevTools session (isolated world + binding + new-document
// script) and in every out-of-process frame it attaches (same three, then resumed). Fails closed: a tab where it
// could not be installed captures nothing (logged). A window.open target waits for it (popup readiness barrier).
void VaultSessionSetup(const std::shared_ptr<Tab>& t,const std::wstring& session,std::shared_ptr<bool> ok=nullptr);
std::set<unsigned> vaultReceivers;std::map<unsigned,std::wstring> vaultScriptIds;   // tab serial -> its receivers / the installed script's id
// Review V-5: the binding is added to the capture world of the documents that exist in a session (Runtime.addBinding
// reaches existing contexts without the Runtime domain enabled), each time a frame of that session navigates, reaches
// DOMContentLoaded or stops loading. So the page's console calls are never serialised for us.
void VaultRebind(const std::shared_ptr<Tab>& tab,const std::wstring& session){
    if(!tab||!tab->view.p||!vaultScriptIds.count(tab->serial)||!VaultCaptureWanted())return;
    auto done=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([](HRESULT,LPCWSTR)->HRESULT{return S_OK;});
    if(session.empty())tab->view->CallDevToolsProtocolMethod(L"Runtime.addBinding",Vault::CaptureBindingParams().c_str(),done);
    else{Com<ICoreWebView2_11> v11;if(SUCCEEDED(tab->view->QueryInterface(IID_ICoreWebView2_11,(void**)&v11.p))&&v11.p)v11->CallDevToolsProtocolMethodForSession(session.c_str(),L"Runtime.addBinding",Vault::CaptureBindingParams().c_str(),done);}
    done->Release();
}
void VaultResume(const std::shared_ptr<Tab>& tab,const std::wstring& session){
    Com<ICoreWebView2_11> v11;if(session.empty()||FAILED(tab->view->QueryInterface(IID_ICoreWebView2_11,(void**)&v11.p))||!v11.p)return;
    auto done=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([](HRESULT,LPCWSTR)->HRESULT{return S_OK;});v11->CallDevToolsProtocolMethodForSession(session.c_str(),L"Runtime.runIfWaitingForDebugger",L"{}",done);done->Release();
}
void VaultCaptureInstall(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup){
    if(!VaultCaptureWanted()||!t||!t->view.p||vaultScriptIds.count(t->serial))return;
    std::weak_ptr<Tab> weak=t;EventRegistrationToken token{};
    auto receive=[&](const wchar_t* event,std::function<void(const std::shared_ptr<Tab>&,std::wstring,std::wstring)> f){
        Com<ICoreWebView2DevToolsProtocolEventReceiver> r;if(FAILED(t->view->GetDevToolsProtocolEventReceiver(event,&r.p))||!r.p)return false;
        auto h=Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler,ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>([weak,f](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a)->HRESULT{
            auto tab=weak.lock();if(halted||!tab||tab->closed||!a)return S_OK;LPWSTR j=nullptr;if(FAILED(a->get_ParameterObjectAsJson(&j))||!j)return S_OK;
            std::wstring json=j;SecureZeroMemory(j,wcslen(j)*sizeof(wchar_t));CoTaskMemFree(j);
            std::wstring session;{Com<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> a2;LPWSTR sid=nullptr;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2DevToolsProtocolEventReceivedEventArgs2,(void**)&a2.p))&&a2.p&&SUCCEEDED(a2->get_SessionId(&sid))&&sid){session=sid;CoTaskMemFree(sid);}}
            f(tab,std::move(json),session);return S_OK;});
        const bool ok=SUCCEEDED(r->add_DevToolsProtocolEventReceived(h,&token));h->Release();return ok;};
    const bool events=vaultReceivers.count(t->serial)||(receive(L"Runtime.bindingCalled",[](const std::shared_ptr<Tab>& tab,std::wstring json,std::wstring){VaultCaptureEvent(tab,std::move(json));})
        &&receive(L"Page.frameNavigated",[](const std::shared_ptr<Tab>& tab,std::wstring,std::wstring session){VaultRebind(tab,session);})
        &&receive(L"Page.domContentEventFired",[](const std::shared_ptr<Tab>& tab,std::wstring,std::wstring session){VaultRebind(tab,session);})
        &&receive(L"Page.frameStoppedLoading",[](const std::shared_ptr<Tab>& tab,std::wstring,std::wstring session){VaultRebind(tab,session);})
        &&receive(L"Target.attachedToTarget",[](const std::shared_ptr<Tab>& tab,std::wstring json,std::wstring){
            // A frame paused for us is ALWAYS resumed: set up first when the capture is wanted, else resumed as it is.
            Vault::AttachedTarget a;const bool parsed=Vault::ParseAttachedToTarget(json,a);
            if(parsed&&a.type==L"iframe"&&VaultCaptureWanted()&&vaultScriptIds.count(tab->serial))VaultSessionSetup(tab,a.sessionId);else VaultResume(tab,a.sessionId);}));
    if(!events){Log(L"VAULT capture NOT installed: DevTools events unavailable");if(popup)popups.Finish(popup,false);return;}
    vaultReceivers.insert(t->serial);vaultScriptIds[t->serial]=L"";
    auto ok=std::make_shared<bool>(true);VaultSessionSetup(t,L"",ok);
    // Popup barrier: the target loads only once the new-document script is in place.
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* barrier=PopupScriptBarrier(popup,L"vault");
    auto ready=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([barrier,ok](HRESULT hr,LPCWSTR)->HRESULT{
        if(SUCCEEDED(hr)&&!*ok)hr=E_FAIL;   // a setup call of this session failed: not installed (a popup target is refused)
        if(SUCCEEDED(hr))Log(L"VAULT capture coverage installed (isolated world, binding, frames)");else Log(L"VAULT capture NOT installed: "+HrText(hr));
        if(barrier){barrier->Invoke(hr,L"");barrier->Release();}return S_OK;});
    const HRESULT hr=t->view->CallDevToolsProtocolMethod(L"Runtime.runIfWaitingForDebugger",L"{}",ready);ready->Release();   // ordered after the setup calls of this session: completes once they were handled
    if(FAILED(hr)){Log(L"VAULT capture NOT installed: "+HrText(hr));if(barrier){popups.Finish(popup,false);barrier->Release();}}
}
// One session (L"" = the tab's own): binding, isolated-world script, auto-attach of out-of-process frames, then
// resume a frame that waits for us. Calls on one session are handled in order.
void VaultSessionSetup(const std::shared_ptr<Tab>& t,const std::wstring& session,std::shared_ptr<bool> ok){
    if(!t||!t->view.p)return;
    Com<ICoreWebView2_11> v11;const bool sessions=SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_11,(void**)&v11.p))&&v11.p;
    if(!session.empty()&&!sessions)return;   // (never reached: frames are only attached when sessions can be driven)
    const unsigned serial=t->serial;
    auto call=[&](const wchar_t* method,const std::wstring& params){
        auto done=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([m=std::wstring(method),frame=!session.empty(),ok,serial](HRESULT hr,LPCWSTR json)->HRESULT{
            if(SUCCEEDED(hr)&&!frame&&m==L"Page.addScriptToEvaluateOnNewDocument"&&json){std::wstring id;const std::wstring j=json;if(Vault::JsonPlainString(j,Vault::JsonMemberAt(j,Vault::JsonRoot(j),L"identifier"),id)){auto it=vaultScriptIds.find(serial);if(it!=vaultScriptIds.end())it->second=id;}}
            if(FAILED(hr)&&ok)*ok=false;if(FAILED(hr))Log(std::wstring(L"VAULT capture setup failed: ")+m+(frame?L" (frame)":L"")+L" "+HrText(hr));return S_OK;});
        const HRESULT hr=session.empty()?t->view->CallDevToolsProtocolMethod(method,params.c_str(),done):v11->CallDevToolsProtocolMethodForSession(session.c_str(),method,params.c_str(),done);done->Release();
        if(FAILED(hr)){if(ok)*ok=false;Log(std::wstring(L"VAULT capture setup call failed: ")+method+L" "+HrText(hr));}};
    call(L"Page.enable",L"{}");      // the engine runs new-document scripts only while its Page domain is enabled

    call(L"Runtime.addBinding",Vault::CaptureBindingParams());
    call(L"Page.addScriptToEvaluateOnNewDocument",Vault::CaptureScriptParams());
    if(sessions)call(L"Target.setAutoAttach",Vault::AutoAttachParams());   // a frame paused for us must be resumable (ForSession)
    else Log(L"VAULT frame capture unavailable on this runtime (no ICoreWebView2_11): frames of other sites are not covered");
    if(!session.empty())call(L"Runtime.runIfWaitingForDebugger",L"{}");   // the frame waited for its setup: never left paused
}
// Review V-5: « Mots de passe » turned off (or on) applies to the tabs already open: the capture is detached (its
// script and binding removed, frames no longer paused, the DevTools domains disabled) or attached.
extern std::set<unsigned> formsInstalled;   // src/forms-integration.h: its world needs the Page domain (P16)
void VaultCaptureRemove(const std::shared_ptr<Tab>& t){
    auto it=vaultScriptIds.find(t->serial);if(it==vaultScriptIds.end()||!t->view.p)return;
    const std::wstring id=it->second;vaultScriptIds.erase(it);
    auto call=[&](const wchar_t* method,const std::wstring& params){auto done=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([](HRESULT,LPCWSTR)->HRESULT{return S_OK;});t->view->CallDevToolsProtocolMethod(method,params.c_str(),done);done->Release();};
    call(L"Target.setAutoAttach",L"{\"autoAttach\":false,\"waitForDebuggerOnStart\":false,\"flatten\":true}");
    if(!id.empty())call(L"Page.removeScriptToEvaluateOnNewDocument",L"{\"identifier\":"+Vault::JsonString(id)+L"}");
    call(L"Runtime.removeBinding",std::wstring(L"{\"name\":\"")+Vault::CaptureBindingName+L"\"}");
    if(!formsInstalled.count(t->serial))call(L"Page.disable",L"{}");   // the forms world (#121) keeps it on
    Log(L"VAULT capture detached from a tab (« Mots de passe » off)");
}
void VaultCaptureSync(){
    for(auto& t:pages){if(!t||t->closed||!t->view.p||t->popupSetup)continue;
        if(VaultCaptureWanted())VaultCaptureInstall(t,nullptr);else VaultCaptureRemove(t);}
}
// ---- Engine-saved passwords, owner decision « option A ». The vault's activation date and its days of use are recorded
// per profile (each tab of an active vault; never in a private window, never while the vault is inactive). NOTHING is
// ever deleted automatically: from the 30th day of use, while « Mots de passe » is on and the vault is readable,
// Réglages › Mots de passe offers « Supprimer les anciens mots de passe… »; the engine's saved passwords are deleted
// (ClearBrowsingData PASSWORD_AUTOSAVE) only after that click AND the confirmation; the result is said (deleted, or
// the failure with its reason); once deleted the offer never comes back. An unreadable state file is never taken as an
// old date: the activation is recorded again (the offer only moves later).
static_assert(Vault::EngineCleanup==Vault::EngineCleanupMode::ManualOffer,"option A: the engine's passwords are deleted only on the person's request");
enum class EngineStep { Nothing, Recorded, OfferDue };
// The local calendar day of `ms` as yyyymmdd (days of use, review V-3).
int LocalDayKey(std::int64_t ms){FILETIME ft;ULARGE_INTEGER u;u.QuadPart=(ULONGLONG)(ms+11644473600000LL)*10000ULL;ft.dwLowDateTime=u.LowPart;ft.dwHighDateTime=u.HighPart;SYSTEMTIME st{},lt{};FileTimeToSystemTime(&ft,&st);if(!SystemTimeToTzSpecificLocalTime(nullptr,&st,&lt))lt=st;return lt.wYear*10000+lt.wMonth*100+lt.wDay;}
std::wstring VaultMigrationFile(){return dataDir+L"\\vault-engine-migration";}
bool ReadEngineMigration(Vault::EngineMigration& m){std::string b;return ProfileStore::ReadAll(VaultMigrationFile(),b)&&Vault::EngineMigration::Read(b,m);}
bool WriteEngineMigration(const Vault::EngineMigration& m){return ProfileStore::WriteAtomic(VaultMigrationFile(),m.Write());}
bool VaultEngineScope(){return Vault::Active()&&!privateWindow&&!smoke&&!dataDir.empty();}
// Whether Réglages › Mots de passe offers the deletion now (read only: nothing recorded, nothing deleted).
bool VaultEngineOfferDue(){
    if(!VaultEngineScope()||!settings.passwords)return false;
    Vault::EngineMigration m;if(!ReadEngineMigration(m)||m.cleared||m.useDays<Vault::EngineMigration::GraceDays)return false;
    return m.OfferDue(settings.passwords,ReadVault(200)==VaultLoad::Ok);
}
// A new day of use (a tab of the active vault); the activation date the first time.
EngineStep VaultEngineMigrationStep(std::int64_t now){
    if(!VaultEngineScope())return EngineStep::Nothing;
    Vault::EngineMigration m;bool recorded=false;
    if(!ReadEngineMigration(m)){m=Vault::EngineMigration{};m.activated=now;recorded=true;}
    const bool counted=m.RecordUse(LocalDayKey(now));   // review V-3: a new day of use (never a clock set back)
    if((recorded||counted)&&!WriteEngineMigration(m)){Log(L"VAULT activation date / day of use NOT recorded");return EngineStep::Nothing;}
    if(recorded){Log(L"VAULT activation date recorded");return EngineStep::Recorded;}
    return VaultEngineOfferDue()?EngineStep::OfferDue:EngineStep::Nothing;
}
void VaultEngineMigrationTick(){
    if(VaultEngineMigrationStep(NativeChrome::DownloadsUI::UnixNowMs())==EngineStep::OfferDue){static bool said=false;if(!said){said=true;Log(L"VAULT engine passwords: deletion offered in Réglages › Mots de passe (nothing deleted automatically)");}}
}
// Tests replace the engine call (no WebView2 there). `done` gets the engine's result.
std::function<void(std::function<void(HRESULT)>)> vaultEngineClearHook;
bool vaultEngineClearing=false;
// The deletion itself, only from the confirmed request below. The offer is checked again here (another window may
// have deleted them, the setting may have changed). `done(hr, reason)` on this thread; reason empty on success.
void VaultEngineDelete(std::function<void(HRESULT,const std::wstring&)> done){
    if(!VaultEngineOfferDue()){done(E_ABORT,L"");return;}
    if(vaultEngineClearing){done(E_PENDING,L"");return;}
    auto finish=[done](HRESULT hr){
        vaultEngineClearing=false;
        if(FAILED(hr)){Log(L"VAULT engine saved passwords NOT deleted "+HrText(hr));done(hr,HrText(hr));return;}
        Vault::EngineMigration m;const bool read=ReadEngineMigration(m);m.cleared=NativeChrome::DownloadsUI::UnixNowMs();
        if(!read||!WriteEngineMigration(m))Log(L"VAULT engine deletion NOT recorded (the offer may show again)");
        Log(L"VAULT engine saved passwords deleted on request (ClearBrowsingData PASSWORD_AUTOSAVE)");done(S_OK,L"");};
    vaultEngineClearing=true;
    if(vaultEngineClearHook){vaultEngineClearHook(finish);return;}
    Com<ICoreWebView2Profile2> profile;
    if(!LiveProfile(IID_ICoreWebView2Profile2,(void**)&profile.p)){vaultEngineClearing=false;Log(L"VAULT engine saved passwords NOT deleted: no engine profile (no tab open)");done(E_NOINTERFACE,TR(vault_engine_not_ready));return;}
    auto handler=Callback<ICoreWebView2ClearBrowsingDataCompletedHandler,HRESULT>([finish](HRESULT hr)->HRESULT{finish(hr);return S_OK;});
    const HRESULT hr=profile->ClearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS_PASSWORD_AUTOSAVE,handler);handler->Release();
    if(FAILED(hr))finish(hr);
}
// « Supprimer les anciens mots de passe de Browser… » (B05 list link, which closes the list): the confirmation (small-popup
// family, danger action), busy while Edge works; a failure stays in the dialog with a short reason; whatever the answer,
// the list comes back (one surface at a time), and after a deletion it says « Anciens mots de passe de Browser supprimés. ».
bool vaultEngineJustDeleted=false;void ShowVaultManager(bool keyboard);
// Review N-2: bumped when the window changes profile (CloseVaultSurfaces). A dialog or a deletion result of the profile
// that left is dropped: it never reopens the list nor touches a dialog for the profile now on screen.
std::uint64_t vaultSurfaceGen=0;
void AskVaultEngineDelete(){
    if(!VaultEngineOfferDue()){Log(L"VAULT engine deletion not offered now: nothing asked");return;}
    const std::uint64_t gen=vaultSurfaceGen;
    auto spec=MD::DeleteEnginePasswordsSpec();spec.buttons[1].closes=false;
    spec.onButton=[gen](int id){
        if(id!=MD::AcceptButton||gen!=vaultSurfaceGen)return;
        modal.SetBusy(true,TR(vault_engine_deleting));
        VaultEngineDelete([gen](HRESULT hr,const std::wstring& reason){
            if(gen!=vaultSurfaceGen){Log(L"VAULT engine deletion answered after a profile switch: its dialog is gone, nothing shown");return;}
            if(FAILED(hr)){modal.SetBusy(false);modal.SetError(hr==E_NOINTERFACE&&!reason.empty()?reason:TRF(vault_engine_delete_failed,HrText(hr)));return;}
            vaultEngineJustDeleted=true;modal.Finish(MD::AcceptButton);accessibility.Notify('S',TR(vault_engine_deleted),L"VaultEngineDeleted");});
    };
    AskModal(std::move(spec),nullptr,[gen](const MD::Result&){if(gen!=vaultSurfaceGen)return;Defer([]{if(!passwordDemo.Visible())ShowVaultManager(false);});});
}
// Review N-2: the window changes profile (SwitchWorkspace): the password list (it shows the profile that leaves) and the
// « Supprimer les anciens mots de passe de Browser ? » dialog (queued or open) close, like the drawers; nothing is deleted.
void CloseVaultSurfaces(){
    ++vaultSurfaceGen;
    if(passwordDemo.Visible())passwordDemo.Hide();
    for(auto it=modalQueue.begin();it!=modalQueue.end();){if(it->spec.automationId==L"DeleteEnginePasswords"){auto done=std::move(it->done);it=modalQueue.erase(it);if(done)done(MD::Result{});}else ++it;}
    if(modalOpen&&modalActive.spec.automationId==L"DeleteEnginePasswords"){Log(L"VAULT engine deletion dialog closed: the window changed profile");modal.Abort();}
}
// « Effacer en quittant » runs ClearBrowsingDataAll: the engine's saved passwords and form entries go too. With the
// vault active, what the vault keeps stays (only passwords not kept yet, in the engine's old store, go).
std::wstring ClearOnExitNote(){return Vault::Active()?TR(settings_clear_on_exit_note_vault):TR(settings_clear_on_exit_note);}
// The pending capture (queued or open B04 and every copy of its secret) is dropped for this tab.
void VaultCaptureDrop(const std::shared_ptr<Tab>& t){
    auto isCapture=[](const OP::Spec& s){return !s.demo&&(s.kind==OP::Kind::PasswordSave||s.kind==OP::Kind::PasswordUpdate);};
    for(auto it=promptQueue.begin();it!=promptQueue.end();){auto q=it->tab.lock();if(isCapture(it->spec)&&(!q||q==t)){auto job=std::move(*it);it=promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);}else ++it;}
    if(promptOpen&&isCapture(promptActive.spec)){auto q=promptActive.tab.lock();if(!q||q==t)originPrompt.Abort();}
    if(t){vaultCapture.erase(t->serial);vaultPromptOpen.erase(t->serial);auto w=vaultAwait.find(t->serial);if(w!=vaultAwait.end()){Wipe(w->second.m.secret);Wipe(w->second.m.current);vaultAwait.erase(w);}}
}
// Review N13: the capture survives only its own submission (Vault::CaptureBinding); the id is the engine's.
void VaultNavigationStarting(const std::shared_ptr<Tab>& t,std::uint64_t navigationId,const std::wstring& uri){
    if(!t)return;auto it=vaultCapture.find(t->serial);if(it==vaultCapture.end())return;
    if(!it->second.NavigationStarting(navigationId,Vault::CanonicalOrigin(uri))){Log(L"VAULT capture dropped: the tab navigated elsewhere");VaultCaptureDrop(t);}
}
void VaultNavigationCompleted(const std::shared_ptr<Tab>& t,std::uint64_t navigationId,bool ok){
    if(!t)return;auto it=vaultCapture.find(t->serial);if(it==vaultCapture.end())return;
    if(!it->second.NavigationCompleted(navigationId,ok)||!t->errorPage.shown.empty()||!t->https.shown.empty()){Log(L"VAULT capture dropped: the submission failed");VaultCaptureDrop(t);}
}
// B03: the user picks an
// account in the real B03 prompt; the target (top-level document, its navigation stamp, its origin) is
// captured when the user acts and checked again just before injecting (Vault::CanFill); the secret is
// decrypted only then, sent to the top-level document by FillScript (which re-checks its own origin and
// ancestors), and the host's copies of the secret and of the script are wiped (V7). No vault access is
// ever started by a page message.
void ShowVaultManager(bool keyboard);
// ---- B03 native control (review §5): shown only when every condition holds; armed 500 ms after it became
// visible and still, in a foreground window; the timer restarts on tab switch and on navigation.
void OfferVaultFill(const std::shared_ptr<Tab>& t);
ULONGLONG vaultKeyShownAt=0;unsigned vaultKeySerial=0;std::uint64_t vaultKeyNav=0;bool vaultKeyWasShown=false;
std::wstring EngineSource(const std::shared_ptr<Tab>& t){LPWSTR s=nullptr;if(!t||!t->view.p||FAILED(t->view->get_Source(&s))||!s)return L"";return TakeString(s);}
bool VaultKeyVisible(){
    auto t=Active();if(!Vault::Active()||halted||!t||t->closed||t->home||!t->view.p||!t->errorPage.shown.empty()||!t->https.shown.empty()||IsSandboxUri(t->uri))return false;
    const std::wstring origin=Vault::CanonicalOrigin(t->uri);if(origin.empty())return false;
    // §6/F9/F10: re-read at most every 2 s without waiting on the profile lock (normal and private); an explicit open always re-reads.
    return VaultCached().first&&!vault.For(origin).empty();
}
void SyncVaultKey(){
    auto t=Active();const bool show=VaultKeyVisible();
    if(show&&(!vaultKeyWasShown||!t||t->serial!=vaultKeySerial||t->navStamp!=vaultKeyNav)){vaultKeyShownAt=GetTickCount64();vaultKeySerial=t?t->serial:0;vaultKeyNav=t?t->navStamp:0;}
    vaultKeyWasShown=show;chrome.vaultKey=show;
    chrome.vaultKeyCount=show?(int)vault.For(Vault::CanonicalOrigin(t->uri)).size():0;   // #121 C: the pill's number (the vault's accounts for this origin, never the page's)
}
bool VaultKeyArmed(){auto t=Active();bool fg=GetForegroundWindow()==window;
#ifdef BROWSER_E2E_VAULT_HOST
    fg=true;   // TEST HOST ONLY (see modal.h Armed): the 500 ms arming still applies
#endif
    return chrome.vaultKey&&t&&t->serial==vaultKeySerial&&t->navStamp==vaultKeyNav&&GetTickCount64()>=vaultKeyShownAt+500&&fg;}
int vaultKeyPress=-1;   // review N6: -1 no mouse press on the key, 0 pressed before it was armed, 1 pressed armed
void VaultKeyActivated(){
    const int press=vaultKeyPress;vaultKeyPress=-1;
    if(press==0){Log(L"VAULT key: activation ignored (pressed before it was armed)");return;}
    if(!VaultKeyArmed()){Log(L"VAULT key: activation ignored (not armed)");return;}   // V5: a click the page arranged right after it appeared does nothing
    Log(L"VAULT key: activated");OfferVaultFill(Active());
}
// The source is read from the ENGINE now (review §5), never from our cached URL or a page message.
void VaultFillIsolated(const std::shared_ptr<Tab>& t,std::wstring script);
void VaultEvaluateIsolated(const std::shared_ptr<Tab>& t,std::wstring script,std::function<void(Vault::FillOutcome,const std::wstring&)> done);
void VaultFillNotDone(Vault::FillOutcome o,const std::wstring& why);
Vault::Target VaultTargetNow(const std::shared_ptr<Tab>& t,bool userAction){const std::wstring src=EngineSource(t);Vault::Target v;v.frameSource=src;v.topSource=src;v.frameId=0;v.navigation=t->navStamp;v.userAction=userAction;return v;}
// Guards at choice (§5): still the active tab, foreground window, no interstitial/error page, same document.
bool VaultChoiceStillValid(const std::shared_ptr<Tab>& t,const std::wstring& origin,const Vault::Target& atAction){
    if(!t||t->closed||!t->view.p)return false;
    bool foreground=GetForegroundWindow()==window;
#ifdef BROWSER_E2E_VAULT_HOST
    foreground=true;   // TEST HOST ONLY
#endif
    if(t!=Active()||!foreground||!t->errorPage.shown.empty()||!t->https.shown.empty()){VaultNotice(TR(vault_fill_canceled),Vault::RefusalText(Vault::Refusal::Stale));return false;}
    const auto refusal=Vault::CanFill(atAction,VaultTargetNow(t,false),origin);
    if(refusal!=Vault::Refusal::None){Log(std::wstring(L"VAULT fill refused: ")+Vault::RefusalText(refusal));VaultNotice(L"Remplissage annulé",Vault::RefusalText(refusal));return false;}   // F5: said, not only logged
    return true;
}
// The account the person chose (B03 from the key, or our list under the field, #121): every guard at choice (§5), then
// the fresh read. #121: first Vault::UserStepScript, which holds no secret: on an identifier-only step it fills the
// account's username and nothing is decrypted (the merged list's behaviour, review P10). Only when the page shows a
// password field are the guards checked again, the secret decrypted and FillScript run (review R-1, unchanged).
// `atAction`: the target captured at the person's action.
void VaultFillChosen(const std::shared_ptr<Tab>& t,std::uint64_t id,const std::wstring& origin,const Vault::Target& atAction){
    if(!VaultChoiceStillValid(t,origin,atAction))return;
    if(!LoadVault(TR(vault_fill_canceled)))return;   // F11: fresh read at the real choice
    const Vault::Entry* e=vault.Find(id);
    if(!e||Vault::CanonicalOrigin(e->origin)!=origin){VaultNotice(TR(vault_fill_canceled),TR(vault_account_gone));return;}
    std::weak_ptr<Tab> weak=t;
    VaultEvaluateIsolated(t,Vault::UserStepScript(origin,e->user),[weak,id,origin,atAction](Vault::FillOutcome o,const std::wstring& why){
        if(o==Vault::FillOutcome::User){Log(L"VAULT filled the username only (identifier step, no secret decrypted)");return;}
        if(o!=Vault::FillOutcome::NeedsPassword){VaultFillNotDone(o,why);return;}
        auto tab=weak.lock();if(!VaultChoiceStillValid(tab,origin,atAction))return;   // the page asks for a password: guards again
        const Vault::Entry* entry=vault.Find(id);
        if(!entry||Vault::CanonicalOrigin(entry->origin)!=origin){VaultNotice(TR(vault_fill_canceled),TR(vault_account_gone));return;}
        std::wstring secret;if(!VaultRevealForFill(*entry,origin,secret))return;
        std::wstring script=Vault::FillScript(origin,entry->user,secret);Wipe(secret);
        VaultFillIsolated(tab,std::move(script));   // review R-1: isolated world of this document, never the page's main world
    });
}
void OfferVaultFill(const std::shared_ptr<Tab>& t){
    if(!Vault::Active()||!t||t->closed||!t->view.p)return;
    if(!LoadVault(TR(vault_unavailable)))return;   // F11: fresh read (normal and private)
    const Vault::Target atAction=VaultTargetNow(t,true);const std::wstring origin=Vault::CanonicalOrigin(atAction.topSource);
    if(atAction.topSource!=t->uri){Log(L"VAULT fill: engine source differs from the tab");VaultNotice(TR(vault_fill_canceled),Vault::RefusalText(Vault::Refusal::Stale));return;}
    Log(L"VAULT fill: B03 prompt");
    if(origin.empty()){VaultNotice(TR(vault_unavailable),Vault::RefusalText(Vault::Refusal::NotHttps));return;}   // D2: short state on explicit request only
    const auto accounts=vault.For(origin);
    OP::Spec s;s.kind=OP::Kind::PasswordFill;s.demo=false;s.origin=origin;for(const auto& e:accounts)s.accounts.push_back({e.user,L"••••••••"});
    std::vector<std::uint64_t> ids;for(const auto& e:accounts)ids.push_back(e.id);
    std::weak_ptr<Tab> weak=t;
    AskPrompt(std::move(s),t,[weak,ids,atAction,origin](OP::Result& r){
        if(r.action==OP::Action::Manage){ShowVaultManager(false);return;}
        auto t=weak.lock();if(r.action!=OP::Action::Fill||!t||t->closed||!t->view.p||r.choice<0||r.choice>=(int)ids.size())return;
        VaultFillChosen(t,ids[(size_t)r.choice],origin,atAction);
    },L"vaultfill:"+std::to_wstring(t->serial));
}
// Review R-1: FillScript is evaluated in a FRESH isolated world of the top-level document (Vault::IsolatedEvaluateParams),
// where `self.origin` is the document's real origin (an opaque-origin page cannot fake it there). Three engine steps:
// the main frame's id, a new world on it, the evaluation in that world only. Every step re-checks that the tab is the
// same document (navigation stamp); a navigation destroys the world (evaluation fails). Any failure: nothing filled,
// said. The script (it holds the secret) is wiped as soon as it is handed over, or when the job ends.
void VaultFillNotDone(Vault::FillOutcome o,const std::wstring& why){Log(L"VAULT fill not done: outcome="+std::to_wstring((int)o)+L" "+why);VaultNotice(TR(vault_fill_not_done),Vault::FillOutcomeText(o));}
void VaultFillIsolated(const std::shared_ptr<Tab>& t,std::wstring script){
    VaultEvaluateIsolated(t,std::move(script),[](Vault::FillOutcome o,const std::wstring& why){
        if(o==Vault::FillOutcome::Filled){Log(L"VAULT filled (user action, same document, confirmed by the page)");return;}
        VaultFillNotDone(o,why);});
}
// The three engine steps for one script of the vault (FillScript, or #121 UserStepScript): `answer` gets the fixed word
// the page answered (or Failed and why), exactly once. The script is wiped as soon as it is handed over.
void VaultEvaluateIsolated(const std::shared_ptr<Tab>& t,std::wstring script,std::function<void(Vault::FillOutcome,const std::wstring&)> answer){
    struct Job{std::weak_ptr<Tab> tab;std::uint64_t nav=0;std::wstring script;std::function<void(Vault::FillOutcome,const std::wstring&)> done;~Job(){Wipe(script);}};
    auto job=std::make_shared<Job>();job->tab=t;job->nav=t->navStamp;job->script=std::move(script);job->done=std::move(answer);Wipe(script);
    auto VaultFillNotDone=[job](Vault::FillOutcome o,const std::wstring& why){auto d=std::move(job->done);job->done=nullptr;if(d)d(o,why);};
    auto same=[job]()->std::shared_ptr<Tab>{auto tab=job->tab.lock();return !halted&&tab&&!tab->closed&&tab->view.p&&tab->navStamp==job->nav?tab:nullptr;};
    auto frames=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([job,same](HRESULT hr,LPCWSTR json)->HRESULT{
        auto VaultFillNotDone=[job](Vault::FillOutcome o,const std::wstring& why){auto d=std::move(job->done);job->done=nullptr;if(d)d(o,why);};
        auto tab=same();const std::wstring frame=SUCCEEDED(hr)&&json?Vault::MainFrameId(json):L"";
        if(!tab||frame.empty()){Wipe(job->script);VaultFillNotDone(Vault::FillOutcome::Failed,tab?L"(no main frame)":L"(the document changed)");return S_OK;}
        auto world=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([job,same](HRESULT hr2,LPCWSTR json2)->HRESULT{
            auto VaultFillNotDone=[job](Vault::FillOutcome o,const std::wstring& why){auto d=std::move(job->done);job->done=nullptr;if(d)d(o,why);};
            auto tab2=same();const long long context=SUCCEEDED(hr2)&&json2?Vault::ContextIdOf(json2):0;
            if(!tab2||!context){Wipe(job->script);VaultFillNotDone(Vault::FillOutcome::Failed,tab2?L"(no isolated world)":L"(the document changed)");return S_OK;}
            std::wstring params=Vault::IsolatedEvaluateParams(context,job->script);Wipe(job->script);
            // Only a fixed status word comes back (never a page value or the secret); « rempli » only when the page says so.
            auto done=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([VaultFillNotDone](HRESULT hr3,LPCWSTR json3)->HRESULT{
                VaultFillNotDone(Vault::ParseIsolatedFillResult(hr3,json3),L"hr="+std::to_wstring((long)hr3));return S_OK;});
            const HRESULT h=tab2->view->CallDevToolsProtocolMethod(L"Runtime.evaluate",params.c_str(),done);done->Release();Wipe(params);
            if(FAILED(h))VaultFillNotDone(Vault::FillOutcome::Failed,L"Runtime.evaluate hr="+std::to_wstring((long)h));
            return S_OK;});
        const HRESULT h=tab->view->CallDevToolsProtocolMethod(L"Page.createIsolatedWorld",Vault::IsolatedWorldParams(frame).c_str(),world);world->Release();
        if(FAILED(h)){Wipe(job->script);VaultFillNotDone(Vault::FillOutcome::Failed,L"Page.createIsolatedWorld hr="+std::to_wstring((long)h));}
        return S_OK;});
    const HRESULT hr=t->view->CallDevToolsProtocolMethod(L"Page.getFrameTree",L"{}",frames);frames->Release();
    if(FAILED(hr)){Wipe(job->script);VaultFillNotDone(Vault::FillOutcome::Failed,L"Page.getFrameTree hr="+std::to_wstring((long)hr));}
}
// E8 / B05: the vault manager, real mode of the B05 surface (no decrypted secret, no reveal; removal
// through Vault::Commit, a failure is said and the row stays). Only while the vault is active.
std::wstring VaultModified(std::int64_t ms){if(ms<=0)return L"";FILETIME ft;ULARGE_INTEGER u;u.QuadPart=(ULONGLONG)(ms+11644473600000LL)*10000ULL;ft.dwLowDateTime=u.LowPart;ft.dwHighDateTime=u.HighPart;SYSTEMTIME st,lt;FileTimeToSystemTime(&ft,&st);SystemTimeToTzSpecificLocalTime(nullptr,&st,&lt);
    using I=I18n::Id;static const I months[]={I::month_long_1,I::month_long_2,I::month_long_3,I::month_long_4,I::month_long_5,I::month_long_6,I::month_long_7,I::month_long_8,I::month_long_9,I::month_long_10,I::month_long_11,I::month_long_12};
    return TRF(vault_modified_on,std::to_wstring(lt.wDay),I18n::T(months[(lt.wMonth+11)%12]),std::to_wstring(lt.wYear));}
// {shown, count}; count SIZE_MAX = the vault file could not be read (Settings says so instead of « 0 », F5).
std::pair<bool,std::size_t> VaultInfo(){if(!Vault::Active())return {false,0};const auto c=VaultCached();return {true,c.first?c.second:SIZE_MAX};}   // drawn often: never blocks
void ShowVaultManager(bool keyboard){
    if(!Vault::Active()||passwordDemo.Visible())return;   // review N8: never re-populated while shown
    if(!LoadVault(TR(vault_unavailable)))return;   // F11: fresh read, read-only in private
    keyboardModality=keyboard;std::vector<NativeChrome::PasswordDemo::Account> rows;std::vector<std::uint64_t> ids;
    for(const auto& e:vault.Entries()){std::wstring host=e.origin.substr(e.origin.find(L"://")+3);NativeChrome::PasswordDemo::Account a{host,e.user,L"",L"",VaultModified(e.updated)};a.id=e.id;rows.push_back(a);ids.push_back(e.id);}
    passwordDemo.SetReal(std::move(rows),[](std::uint64_t id)->bool{   // review F1: the row's own vault id, never a shifting index
        if(!vault.Remove(id))return false;
        const auto r=Vault::Commit(VaultDir(),VaultFile(),vaultBase,vault);
        if(r!=ProfileStore::Result::Written&&r!=ProfileStore::Result::Unchanged){Log(L"VAULT removal NOT written");vault=vaultBase;return false;}   // back to what the file holds
        return true;});
    passwordDemo.SetReadOnly(privateWindow);   // §6: no removal from a private window (Vault::Commit refuses anyway)
    passwordDemo.SetEngineOffer(!privateWindow&&VaultEngineOfferDue(),[]{AskVaultEngineDelete();});   // option A: Edge's old passwords
    if(vaultEngineJustDeleted){vaultEngineJustDeleted=false;passwordDemo.EngineDeleted();}   // said once, in the list that comes back
    passwordDemo.Show(PageScreenRect());
}
// ---------------------------------------------------------------------------------------
// Frozen page (#121, Paper p-V-0 « Page qui ne répond plus · A »). Engine facts (tests/crash-screenshare/engine_probe.py
// hang-*, window never shown): ProcessFailed RENDER_PROCESS_UNRESPONSIVE comes about 16.6 s after the page stops
// answering input, and only ONCE per freeze (one report in 70 s of a page frozen for good, and none either when new
// input comes after it: hang-again, 24/11); a script sent to the page answers the moment it runs again (the recovery
// signal); Reload() of a frozen page never completes (12 s), its renderer has to end first (found through the tab's
// main frame id among the renderers' frames), after which Reload() brings the page back. Hence:
//   a report for the shown tab  -> our window over the veiled page (OP::Kind::Unresponsive), one at a time;
//   « Attendre » / Échap        -> closed; it comes back by itself 15 s later while the page is still frozen
//                                  (src/crash-page.h AfterWait; a hidden tab gets it once shown);
//   the page answers again      -> the window closes by itself, nothing happens to the page;
//   « Recharger »               -> that renderer ends (other tabs it hosts get the crashed-tab page, as with Chrome's
//                                  « Quitter la page »), then the page reloads.
bool UnresponsivePromptFor(const std::shared_ptr<Tab>& t){
    for(auto& q:promptQueue)if(q.spec.kind==OP::Kind::Unresponsive&&q.tab.lock()==t)return true;
    return promptOpen&&promptActive.spec.kind==OP::Kind::Unresponsive&&promptActive.tab.lock()==t;
}
void HangRecovered(const std::shared_ptr<Tab>& t){
    for(auto it=promptQueue.begin();it!=promptQueue.end();){auto q=it->tab.lock();if(it->spec.kind==OP::Kind::Unresponsive&&q==t){auto job=std::move(*it);it=promptQueue.erase(it);OP::Result none;for(auto& a:job.answers)a(none);}else ++it;}
    if(promptOpen&&promptActive.spec.kind==OP::Kind::Unresponsive&&promptActive.tab.lock()==t){Log(L"PAGE UNRESPONSIVE answered again: window closed");originPrompt.Abort();}
}
// A script that runs only once the page runs again.
void HangProbe(const std::shared_ptr<Tab>& t){
    if(t->hang.probing||!t->view.p)return;
    t->hang.probing=true;t->hang.uri=t->uri;std::weak_ptr<Tab> weak=t;
    auto done=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([weak](HRESULT,LPCWSTR)->HRESULT{
        auto tab=weak.lock();if(halted||!tab||tab->closed||!tab->hang.probing)return S_OK;
        tab->hang.probing=false;Log(L"PAGE UNRESPONSIVE recovered");HangRecovered(tab);return S_OK;});
    if(FAILED(t->view->ExecuteScript(L"0",done)))t->hang.probing=false;
    done->Release();
}
// « Recharger »: end the renderer that hosts this tab's main frame, then reload (ProcessFailed sees `reloading`).
void HangReload(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed||!t->view.p)return;
    UINT32 mainFrame=0;{Com<ICoreWebView2_20> v20;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_20,(void**)&v20.p))&&v20.p)v20->get_FrameId(&mainFrame);}
    Com<ICoreWebView2Environment13> e13;
    if(!mainFrame||!environment.p||FAILED(environment->QueryInterface(IID_ICoreWebView2Environment13,(void**)&e13.p))||!e13.p){Log(L"PAGE UNRESPONSIVE reload: renderer unknown, plain reload");t->view->Reload();return;}
    std::weak_ptr<Tab> weak=t;
    auto found=Callback<ICoreWebView2GetProcessExtendedInfosCompletedHandler,HRESULT,ICoreWebView2ProcessExtendedInfoCollection*>([weak,mainFrame](HRESULT hr,ICoreWebView2ProcessExtendedInfoCollection* list)->HRESULT{
        auto tab=weak.lock();if(halted||!tab||tab->closed||!tab->view.p)return S_OK;
        DWORD pid=0;UINT n=0;
        if(SUCCEEDED(hr)&&list&&SUCCEEDED(list->get_Count(&n)))for(UINT i=0;i<n&&!pid;++i){
            Com<ICoreWebView2ProcessExtendedInfo> x;Com<ICoreWebView2ProcessInfo> p;Com<ICoreWebView2FrameInfoCollection> frames;Com<ICoreWebView2FrameInfoCollectionIterator> it;
            if(FAILED(list->GetValueAtIndex(i,&x.p))||!x.p||FAILED(x->get_ProcessInfo(&p.p))||!p.p||FAILED(x->get_AssociatedFrameInfos(&frames.p))||!frames.p||FAILED(frames->GetIterator(&it.p))||!it.p)continue;
            BOOL more=FALSE;it->get_HasCurrent(&more);
            while(more&&!pid){Com<ICoreWebView2FrameInfo> f;Com<ICoreWebView2FrameInfo2> f2;UINT32 id=0;
                if(SUCCEEDED(it->GetCurrent(&f.p))&&f.p&&SUCCEEDED(f->QueryInterface(IID_ICoreWebView2FrameInfo2,(void**)&f2.p))&&f2.p&&SUCCEEDED(f2->get_FrameId(&id))&&id==mainFrame){INT32 v=0;p->get_ProcessId(&v);pid=(DWORD)v;}
                if(FAILED(it->MoveNext(&more)))more=FALSE;}
        }
        HANDLE h=pid?OpenProcess(PROCESS_TERMINATE,FALSE,pid):nullptr;
        if(!h){Log(L"PAGE UNRESPONSIVE reload: renderer not found, plain reload");tab->view->Reload();return S_OK;}
        tab->hang.reloading=true;Log(L"PAGE UNRESPONSIVE reload: ending the tab's renderer");
        if(!TerminateProcess(h,1)){tab->hang.reloading=false;Log(L"PAGE UNRESPONSIVE reload: renderer not ended, plain reload");tab->view->Reload();}
        CloseHandle(h);return S_OK;});
    if(FAILED(e13->GetProcessExtendedInfos(found))){Log(L"PAGE UNRESPONSIVE reload: process list unavailable, plain reload");t->view->Reload();}
    found->Release();
}
void PageUnresponsive(const std::shared_ptr<Tab>& t);
// The engine reports a freeze once: after « Attendre » (or a report for a hidden tab), the window comes back from here.
void HangAskAgainLater(const std::shared_ptr<Tab>& t){
    std::weak_ptr<Tab> weak=t;
    DeferAfter(NativeChrome::CrashPage::HangAskAgainMs,[weak]{
        auto tab=weak.lock();if(halted||!tab)return;
        namespace CP=NativeChrome::CrashPage;
        switch(CP::AfterWait(tab->closed,tab->hang.probing,tab->hang.reloading||CrashPageOn(tab),tab->uri==tab->hang.uri,tab==Active(),UnresponsivePromptFor(tab))){
        case CP::HangNext::Ask:Log(L"PAGE UNRESPONSIVE still frozen: asked again");PageUnresponsive(tab);break;
        case CP::HangNext::Later:HangAskAgainLater(tab);break;
        default:break;
        }
    });
}
void PageUnresponsive(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed||!t->view.p||t->home||t->hang.reloading||CrashPageOn(t))return;
    HangProbe(t);
    if(t!=Active()){Log(L"PAGE UNRESPONSIVE in a hidden tab: asked once it is shown, if still frozen");HangAskAgainLater(t);return;}
    if(UnresponsivePromptFor(t))return;
    OP::Spec s;s.kind=OP::Kind::Unresponsive;s.origin=NativeChrome::ErrorPages::HostOf(t->uri);
    Log(L"PAGE UNRESPONSIVE window");
    std::weak_ptr<Tab> weak=t;
    AskPrompt(std::move(s),t,[weak](OP::Result& r){
        auto tab=weak.lock();if(!tab||tab->closed)return;
        if(r.action==OP::Action::Reload){Log(L"PAGE UNRESPONSIVE « Recharger »");Defer([weak]{auto t2=weak.lock();if(t2&&!t2->closed)HangReload(t2);});return;}
        if(r.action==OP::Action::Wait)Log(L"PAGE UNRESPONSIVE « Attendre »: asked again in 15 s if still frozen");
        HangAskAgainLater(tab);   // « Attendre », or closed by the host (the page answered: nothing more; the tab switched: once shown)
    });
}

// ---------------------------------------------------------------------------------------
// Crashed tab (#121, src/crash-page.h, Paper p-V-0 « Onglet planté · A »). When the tab's own renderer dies
// (ProcessFailed RENDER_PROCESS_EXITED: crash, out of memory, killed), our page replaces the engine's « Cette page a un
// problème ». Engine facts (tests/crash-screenshare/engine_probe.py, no window shown): NavigateToString right in the
// handler shows our page and adds ONE history entry right after the crashed one; Reload() of that entry reloads OUR
// page; GoBack() reloads the crashed entry in a new renderer (the site comes back, its earlier entries kept). Hence:
//   « Recharger », the toolbar reload, F5, Ctrl+R  -> back to the crashed entry (its address is the one shown);
//   Back                                           -> the entry BEFORE the crashed one (DevTools history), as Chrome;
//   « Fermer l'onglet »                            -> closes this tab only.
// Our entry stays in front after « Recharger » (Forward brings the same page back, working: same token) until the
// next navigation prunes it. A crashed error / HTTPS-only page (one of ours) opens its address again instead.
// Its three weights as WOFF2 data: URIs (the three TTF weights exceed what NavigateToString accepts).
const std::wstring& CrashPageFont(int weight){
    static const std::wstring regular=DataUri(base+L"\\assets\\fonts\\InterDisplay-Regular.woff2",L"font/woff2",512*1024);
    static const std::wstring medium=DataUri(base+L"\\assets\\fonts\\InterDisplay-Medium.woff2",L"font/woff2",512*1024);
    static const std::wstring semibold=DataUri(base+L"\\assets\\fonts\\InterDisplay-SemiBold.woff2",L"font/woff2",512*1024);
    return weight>=600?semibold:weight>=500?medium:regular;
}
bool CrashPageOn(const std::shared_ptr<Tab>& t){return t&&!t->closed&&t->errorPage.kind==NativeChrome::CrashPage::PageKind&&!t->errorPage.shown.empty();}
void ShowCrashPage(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed||!t->view.p)return;
    namespace CP=NativeChrome::CrashPage;
    switch(CP::ExitResponse(false,CrashPageOn(t),t->home||t->uri.empty()||IsSandboxUri(t->uri))){
    case CP::OnExit::ReshowPage:
        // Our page's own renderer died (night 24/11: a second crash of a background tab sent it back to the site).
        // Reloading our entry renders our page again; `pending` lets that reload through and keeps its state.
        t->errorPage.pending=true;Log(L"CRASH PAGE crashed itself: shown again");
        if(FAILED(t->view->Reload())){t->errorPage.pending=false;Log(L"CRASH PAGE reload failed");}
        return;
    case CP::OnExit::ReloadDocument:   // the native home, the debug sandbox: their own document back
        Log(L"CRASH PAGE not shown (home or internal page): reloaded");if(FAILED(t->view->Reload()))Log(L"CRASH PAGE reload failed");return;
    default:break;
    }
    GUID guid{};wchar_t token[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,token,40)){Log(L"CRASH PAGE no token: the engine's page stays");return;}
    auto& c=t->crash;auto& e=t->errorPage;BOOL back=FALSE;t->view->get_CanGoBack(&back);
    c=CrashPageState{};c.url=t->uri;c.token=token;c.pending=true;c.entryBefore=back!=FALSE;c.ownPage=!e.shown.empty()||!t->https.shown.empty();
    e.shown=c.url;e.token=c.token;e.pending=true;e.kind=NativeChrome::CrashPage::PageKind;t->home=false;
    NativeChrome::CrashPage::Extras x;x.fontRegular=CrashPageFont(400);x.fontMedium=CrashPageFont(500);x.fontSemiBold=CrashPageFont(600);x.reducedMotion=motion.ReducedMotion();
    Log(std::wstring(L"CRASH PAGE shown")+(c.ownPage?L" (over one of our pages)":L""));
    if(FAILED(t->view->NavigateToString(NativeChrome::CrashPage::Html(c.url,c.token,x).c_str()))){
        Log(L"CRASH PAGE NavigateToString failed: the engine's page stays");e.shown.clear();e.token.clear();e.pending=false;e.kind=0;c=CrashPageState{};}
}
// « Recharger » (and every reload of our page).
void CrashPageReload(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed||!t->view.p||t->crash.url.empty())return;
    auto& c=t->crash;BOOL back=FALSE;t->view->get_CanGoBack(&back);
    if(!c.ownPage&&back){c.recharging=true;Log(L"CRASH PAGE reload: back to the crashed entry");if(SUCCEEDED(t->view->GoBack()))return;c.recharging=false;}
    Log(L"CRASH PAGE reload: address opened again");Navigate(t,c.url);
}
// Back from our page: the entry two places back (ours sits right after the crashed one), read from DevTools history.
void CrashPageBack(const std::shared_ptr<Tab>& t){
    if(halted||!t||t->closed||!t->view.p)return;
    if(!t->crash.entryBefore){Log(L"CRASH PAGE back: no page before the crashed one");return;}
    std::weak_ptr<Tab> weak=t;
    auto read=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,HRESULT,LPCWSTR>([weak](HRESULT hr,LPCWSTR json)->HRESULT{
        auto tab=weak.lock();if(halted||!tab||tab->closed||!tab->view.p||!CrashPageOn(tab))return S_OK;
        const long long id=SUCCEEDED(hr)&&json?NativeChrome::CrashPage::HistoryEntryBefore(json,2):-1;
        if(id<0){Log(L"CRASH PAGE back: history unreadable");return S_OK;}
        tab->crash.skipping=true;Log(L"CRASH PAGE back: the page before the crashed one");
        if(FAILED(tab->view->CallDevToolsProtocolMethod(L"Page.navigateToHistoryEntry",(L"{\"entryId\":"+std::to_wstring(id)+L"}").c_str(),nullptr))){tab->crash.skipping=false;Log(L"CRASH PAGE back: navigateToHistoryEntry failed");}
        return S_OK;});
    if(FAILED(t->view->CallDevToolsProtocolMethod(L"Page.getNavigationHistory",L"{}",read)))Log(L"CRASH PAGE back: history unavailable");read->Release();
}
// NavigationStarting, before the error pages' bookkeeping. true: this navigation was cancelled (handled here).
bool CrashPageNavigationStarting(const std::shared_ptr<Tab>& t,ICoreWebView2NavigationStartingEventArgs* a,const std::wstring& uri){
    auto& c=t->crash;if(c.url.empty())return false;
    const bool blank=uri==L"about:blank"||uri.rfind(L"data:",0)==0;
    if(c.pending){   // our own NavigateToString starting: remember its entry (a hash: the address is megabytes long)
        if(blank){c.pending=false;c.dataSize=uri.size();c.dataHash=std::hash<std::wstring>{}(uri);}else c=CrashPageState{};
        return false;}
    if(CrashPageOn(t)&&!t->errorPage.pending){
        namespace CP=NativeChrome::CrashPage;
        COREWEBVIEW2_NAVIGATION_KIND kind=COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT;
        {Com<ICoreWebView2NavigationStartingEventArgs3> a3;if(SUCCEEDED(a->QueryInterface(IID_ICoreWebView2NavigationStartingEventArgs3,(void**)&a3.p))&&a3.p)a3->get_NavigationKind(&kind);}
        std::weak_ptr<Tab> weak=t;
        switch(CP::NavigationWhileShown(false,blank,kind,c.recharging,c.skipping,c.ownPage)){
        case CP::OnNavigation::ToCrashedEntry:   // toolbar reload, F5, Ctrl+R on our page
            if(SUCCEEDED(a->put_Cancel(TRUE))){Defer([weak]{auto tab=weak.lock();if(tab&&!tab->closed)CrashPageReload(tab);});return true;}
            return false;
        case CP::OnNavigation::ToEntryBefore:
            if(SUCCEEDED(a->put_Cancel(TRUE))){Defer([weak]{auto tab=weak.lock();if(tab&&!tab->closed)CrashPageBack(tab);});return true;}
            return false;
        default:   // ours: « Recharger », or Back already resolved
            if(kind==COREWEBVIEW2_NAVIGATION_KIND_BACK_OR_FORWARD)c.recharging=c.skipping=false;
            return false;
        }
    }
    c.recharging=c.skipping=false;
    // Our entry brought back by history (Forward after « Recharger »): the same page, working again.
    if(blank&&c.dataSize&&uri.size()==c.dataSize&&std::hash<std::wstring>{}(uri)==c.dataHash){
        auto& e=t->errorPage;e.shown=c.url;e.token=c.token;e.kind=NativeChrome::CrashPage::PageKind;e.pending=true;   // pending: kept by ErrorPageNavigationStarting
        Log(L"CRASH PAGE back from history");}
    return false;
}
bool CrashPageMessage(const std::shared_ptr<Tab>& t,const std::wstring& source,const std::wstring& message){
    auto& e=t->errorPage;if(!CrashPageOn(t)||e.pending||t->uri!=e.shown)return false;
    if(source!=L"about:blank"&&source.rfind(L"data:",0)!=0)return false;
    const auto action=NativeChrome::CrashPage::ParseMessage(message,e.token);if(!action)return false;
    std::weak_ptr<Tab> weak=t;
    if(*action==NativeChrome::CrashPage::Action::Reload){Log(L"CRASH PAGE « Recharger »");Defer([weak]{auto tab=weak.lock();if(tab&&!tab->closed&&CrashPageOn(tab))CrashPageReload(tab);});}
    else{Log(L"CRASH PAGE « Fermer l'onglet »");Defer([weak]{auto tab=weak.lock();if(!tab||tab->closed)return;const int i=TabIndexOf(tab);if(i>=0)CloseAt(i);});}
    return true;
}
HRESULT PageMessage(const std::shared_ptr<Tab>& t,ICoreWebView2WebMessageReceivedEventArgs* a){
    if(halted||!t||t->closed||!a)return S_OK;
    if(!t->https.shown.empty())return InterstitialMessage(t,a);
    if(AnnotationPageMessage(t,a))return S_OK; // annotation mode (src/annotation-mode.h): only while it is on in this tab
    const std::wstring source=TakeString([&]{LPWSTR s=nullptr;a->get_Source(&s);return s;}());
    LPWSTR raw=nullptr;std::wstring message;
    if(SUCCEEDED(a->TryGetWebMessageAsString(&raw))&&raw)message=TakeString(raw);
    else{CoTaskMemFree(raw);raw=nullptr;if(SUCCEEDED(a->get_WebMessageAsJson(&raw))&&raw)message=TakeString(raw);}
    if(message.size()>4096){Wipe(message);return S_OK;}
    struct WipeAtEnd{std::wstring& m;~WipeAtEnd(){Wipe(m);}} wipeAll{message};   // F6: a page message may carry a submitted password
    if(AudioPageMessage(t,message,source))return S_OK; // per-site audio reports (audio-integration.h)
    if(ScrollbarMessageFrom(t,message))return S_OK; // overlay scrollbar metrics (viewport-scrollbar-host.h)
    if(PrintPageMessage(t,message))return S_OK; // window.print() of the top document (print-integration.h)
    if(VaultCapture(t,source,message))return S_OK;
    if(CrashPageMessage(t,source,message))return S_OK;
    if(ErrorPageMessage(t,source,message))return S_OK;
    SandboxMessage(t,source,message);
    return S_OK;
}

// ---------------------------------------------------------------------------------------
// Per-tab WebView2 registrations for this section (called from NewTab's controller callback).
// popup (a window.open target): its vault script joins the readiness barrier, the sandbox resource handlers are
// installed after the target's assignment (RegisterSandboxResources from PopupAssign), and *required is cleared
// when a security handler could not be registered (the target is refused before it can load).
bool RegisterSandboxResources(const std::shared_ptr<Tab>& t);
void FormsInstall(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup);   // src/forms-integration.h (#121)
void RegisterElementEvents(const std::shared_ptr<Tab>& t,ICoreWebView2Settings* settings,const PopupHost::RequestPtr& popup,bool* required){
    std::weak_ptr<Tab> weak=t;EventRegistrationToken token{};
    auto need=[required](HRESULT hr){if(FAILED(hr)&&required)*required=false;return hr;};
    if(settings&&FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)))Log(L"DIALOG default dialogs could not be disabled");
    VaultCaptureInstall(t,popup);   // owner decision A: isolated world + binding, frames included (only while the vault is active, never private)
    FormsInstall(t,popup);          // #121: our list under the field and our validation bubble (own world, the tab's own session)
    VaultEngineMigrationTick();     // owner decision B: activation date, then the engine's old passwords cleared after 30 days (active vault only)
    {auto h=Callback<ICoreWebView2ScriptDialogOpeningEventHandler,ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ScriptDialogOpeningEventArgs* a)->HRESULT{return ScriptDialogOpening(weak.lock(),a);});
     if(FAILED(need(t->view->add_ScriptDialogOpening(h,&token))))Log(L"DIALOG add_ScriptDialogOpening failed");h->Release();}
    {auto h=Callback<ICoreWebView2PermissionRequestedEventHandler,ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* a)->HRESULT{return PermissionRequested(weak.lock(),a);});
     if(FAILED(need(t->view->add_PermissionRequested(h,&token))))Log(L"PERMISSION add_PermissionRequested failed");h->Release();}
    {Com<ICoreWebView2_27> v27;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_27,(void**)&v27.p))&&v27.p){   // #121 screen-share confirmation
        auto h=Callback<ICoreWebView2ScreenCaptureStartingEventHandler,ICoreWebView2*,ICoreWebView2ScreenCaptureStartingEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ScreenCaptureStartingEventArgs* a)->HRESULT{return ScreenCaptureStarting(weak.lock(),a);});
        if(FAILED(v27->add_ScreenCaptureStarting(h,&token)))Log(L"SCREEN CAPTURE add_ScreenCaptureStarting failed: the engine's picker opens directly");h->Release();}
     else Log(L"SCREEN CAPTURE ICoreWebView2_27 unavailable: the engine's picker opens directly");}
    {Com<ICoreWebView2_10> v10;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_10,(void**)&v10.p))&&v10.p){
        auto h=Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler,ICoreWebView2*,ICoreWebView2BasicAuthenticationRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2BasicAuthenticationRequestedEventArgs* a)->HRESULT{return BasicAuthenticationRequested(weak.lock(),a);});
        if(FAILED(need(v10->add_BasicAuthenticationRequested(h,&token))))Log(L"AUTH add_BasicAuthenticationRequested failed");h->Release();}else Log(L"AUTH ICoreWebView2_10 unavailable: engine dialog kept");}
    {Com<ICoreWebView2_5> v5;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_5,(void**)&v5.p))&&v5.p){
        auto h=Callback<ICoreWebView2ClientCertificateRequestedEventHandler,ICoreWebView2*,ICoreWebView2ClientCertificateRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2ClientCertificateRequestedEventArgs* a)->HRESULT{return ClientCertificateRequested(weak.lock(),a);});
        if(FAILED(need(v5->add_ClientCertificateRequested(h,&token))))Log(L"CERTIFICATE add_ClientCertificateRequested failed");h->Release();}else Log(L"CERTIFICATE ICoreWebView2_5 unavailable: engine dialog kept");}
    {Com<ICoreWebView2_18> v18;if(SUCCEEDED(t->view->QueryInterface(IID_ICoreWebView2_18,(void**)&v18.p))&&v18.p){
        auto h=Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler,ICoreWebView2*,ICoreWebView2LaunchingExternalUriSchemeEventArgs*>([weak](ICoreWebView2*,ICoreWebView2LaunchingExternalUriSchemeEventArgs* a)->HRESULT{return LaunchingExternalUriScheme(weak.lock(),a);});
        if(FAILED(need(v18->add_LaunchingExternalUriScheme(h,&token))))Log(L"EXTERNAL add_LaunchingExternalUriScheme failed");h->Release();}else Log(L"EXTERNAL ICoreWebView2_18 unavailable: engine dialog kept");}
    {auto h=Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2* v,IUnknown*)->HRESULT{
        auto tab=weak.lock();if(!tab||tab->closed||halted||!v)return S_OK;BOOL full=FALSE;v->get_ContainsFullScreenElement(&full);tab->pageFullscreen=full!=FALSE;
        Defer([weak,full]{auto t2=weak.lock();if(t2&&!t2->closed)SetPageFullscreen(t2,full!=FALSE);});return S_OK;});
     if(FAILED(t->view->add_ContainsFullScreenElementChanged(h,&token)))Log(L"FULLSCREEN add_ContainsFullScreenElementChanged failed");h->Release();}
    if(!popup)RegisterSandboxResources(t);   // a window.open target: after its assignment (Microsoft's contract)
}
// Debug sandbox resources (served by the host). Returns false when they could not be installed.
bool RegisterSandboxResources(const std::shared_ptr<Tab>& t){
    std::weak_ptr<Tab> weak=t;EventRegistrationToken token{};
    if(SUCCEEDED(t->view->AddWebResourceRequestedFilter((std::wstring(L"https://")+NativeChrome::DebugSandbox::Host+L"/*").c_str(),COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL))){
        auto h=Callback<ICoreWebView2WebResourceRequestedEventHandler,ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*>([weak](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* a)->HRESULT{return SandboxResource(weak.lock(),a);});
        const HRESULT hr=t->view->add_WebResourceRequested(h,&token);if(FAILED(hr))Log(L"SANDBOX add_WebResourceRequested failed");h->Release();return SUCCEEDED(hr);}
    return false;
}
// A tab is closing (or its renderer died): end everything that waits on it.
void ElementsTabClosed(const std::shared_ptr<Tab>& t){
    AbortTabModals(t);AbortTabPrompts(t);
    PrintTabClosed(t);
    if(findSession.tab.lock()==t){if(findBar.Visible())findBar.Hide();FindDetach();}
    if(t->pageFullscreen){t->pageFullscreen=false;if(fullscreen.page){const bool was=InFullscreen();fullscreen.page=false;ApplyFullscreen(was);}}
    scriptDialogs.erase(t->serial);
    siteStates.erase(t->serial);
}
// Keys pressed while one of our dialogs, prompts or the find bar has focus: their fields keep
// them, except Ctrl+F / F3 in the find bar. True when Key() must return `cmd` (0: none).
bool InWindow(HWND root,HWND f){return root&&f&&(f==root||IsChild(root,f));}
bool ElementSurfaceKey(UINT key,bool ctrl,bool alt,bool shift,int& cmd){
    cmd=0;HWND f=GetFocus();if(!f)return false;
    if(InWindow(findBar.Window(),f)){
        if(ctrl&&!alt&&!shift&&key=='F'){cmd=FindPage;return true;}
        if(ctrl&&!alt&&shift&&key=='F'){cmd=FocusModeCommand;return true;}   // Ctrl+Maj+F: Mode Focus, from the find field too
        if(!ctrl&&!alt&&key==VK_F3){cmd=shift?FindPreviousCommand:FindNextCommand;return true;}
        return true;
    }
    if(PrintSurfaceFocus(f))return true;   // our print window keeps its keys (#121)
    return InWindow(shortcutsPanel.Window(),f)||InWindow(modal.Window(),f)||InWindow(notesCard.Window(),f)||InWindow(originPrompt.Window(),f)||InWindow(passwordDemo.Window(),f)||InWindow(contextToast.Window(),f);
}
void ElementsTabChanged(){ModalTabChanged();PromptTabChanged();FindTabChanged();FullscreenTabChanged();RefreshRequestChip();PrintTabChanged();}
void RepositionElementSurfaces(){
    if(modal.Visible())modal.Reposition(PageScreenRect());
    if(notesCard.Visible())notesCard.Reposition(PageScreenRect());
    if(originPrompt.Visible())originPrompt.Reposition(OriginAnchorRect());
    if(findBar.Visible())findBar.Reposition(FindBarArea());
    if(contextToast.Visible())contextToast.Reposition(PageScreenRect());
    originPrompt.SetPageArea(PageScreenRect());
    PrintReposition();
}
void ElementsReducedMotion(bool reduced){modal.SetReducedMotion(reduced);notesCard.SetReducedMotion(reduced);originPrompt.SetReducedMotion(reduced);passwordDemo.SetReducedMotion(reduced);findBar.SetReducedMotion(reduced);contextToast.SetReducedMotion(reduced);fullscreenHint.SetReducedMotion(reduced);shortcutsPanel.SetReducedMotion(reduced);sandboxTip.SetReducedMotion(reduced);chromeTip.SetReducedMotion(reduced);}
NativeChrome::Motion::Wake ElementsNextWake(NativeChrome::Motion::Time now){
    using NativeChrome::Motion::Earlier;
    return Earlier(ChipNextWake(now),Earlier(Earlier(Earlier(Earlier(Earlier(modal.NextWake(now),notesCard.NextWake(now)),originPrompt.NextWake(now)),Earlier(findBar.NextWake(now),contextToast.NextWake(now))),fullscreenHint.NextWake(now)),Earlier(Earlier(shortcutsPanel.NextWake(now),Earlier(sandboxTip.NextWake(now),chromeTip.NextWake(now))),passwordDemo.NextWake(now))));
}
void ElementsFrame(NativeChrome::Motion::Time now){ChipFrame(now);modal.Frame(now);notesCard.Frame(now);originPrompt.Frame(now);passwordDemo.Frame(now);shortcutsPanel.Frame(now);sandboxTip.Frame(now);chromeTip.Frame(now);findBar.Frame(now);contextToast.Frame(now);fullscreenHint.Frame(now);}
// ---------------------------------------------------------------------------------------
// B07 real path: the engine interrupted a download for safety (ICoreWebView2DownloadOperation
// InterruptReason FILE_MALICIOUS / FILE_SECURITY_CHECK_FAILED / FILE_BLOCKED_BY_POLICY; SmartScreen
// and policies stay the engine's). « Supprimer le fichier » removes the row and any leftover
// file; « Copier le lien » / « Signaler » are real; there is no « Conserver » (no SDK override).
void DownloadAction(int id,DL::Action action);
std::wstring DangerText(COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON r){
    switch(r){
    // E5: the engine's reason only; never « not saved » (a residual file may exist) and a failed
    // check is not a malware verdict.
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS:return TR(download_blocked_reason_dangerous);
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:return TR(download_blocked_reason_check);
    default:return TR(download_blocked_reason_policy);
    }
}
OP::Spec DangerousDownloadSpec(const std::wstring& path,const std::wstring& uri,COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason){
    OP::Spec s;s.kind=OP::Kind::DangerousDownload;s.origin=OriginLabel(uri);s.detail=DL::FileName(path);if(s.detail.empty())s.detail=TR(download_file_generic);
    s.reason=DangerText(reason);s.link=uri;
    // Header by the real reason: « dangereux » only for FILE_MALICIOUS (same words as the list row, E6).
    s.reportable=reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS;
    s.blockedTitle=reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS?TR(download_blocked_title_dangerous):reason==COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED?TR(download_blocked_title_check):TR(download_blocked_title_policy);s.keepAvailable=false;s.demo=false;return s;
}
void ElementsDangerousDownload(int id,const std::wstring& path,const std::wstring& uri,COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason){
    auto t=Active();if(halted||!t)return;
    Log(L"DOWNLOAD blocked by the engine: "+DL::FileName(path)+L" reason="+std::to_wstring((int)reason));
    AskPrompt(DangerousDownloadSpec(path,uri,reason),t,[id,path](OP::Result& r){
        if(r.action!=OP::Action::Delete)return;
        DownloadAction(id,DL::Action::Remove);
        std::error_code ec;if(!path.empty()&&std::filesystem::exists(path,ec)){std::filesystem::remove(path,ec);Log(std::wstring(L"DOWNLOAD blocked file ")+(ec?L"could not be removed":L"removed"));}
    },L"download:"+std::to_wstring(id));
}
// B07 « Plus d'options » non-terminal commands. Only the sandbox demonstration reaches them (real
// dangerous downloads keep the engine's own UI and protections): the link is a fictitious
// demo address, the report is simulated, and the result goes to the sandbox journal.
bool CopyToClipboard(const std::wstring& text);
void NewTab(std::wstring uri,int index,std::wstring title);
// Microsoft's official page to report a file wrongly flagged by Defender SmartScreen.
constexpr const wchar_t* SmartScreenReportUrl=L"https://www.microsoft.com/wdsi/filesubmission";
void PromptDownloadCommand(OP::Prompt::Command c,const OP::Spec& s){
    auto t=promptActive.tab.lock();
    if(!s.demo){   // a real blocked download: real effects only
        if(c==OP::Prompt::CopyLink){const bool ok=!s.link.empty()&&CopyToClipboard(s.link);Log(std::wstring(L"DOWNLOAD blocked: link ")+(ok?L"copied":L"not copied"));}
        else if(s.reportable){Log(L"DOWNLOAD blocked: report page opened");Defer([]{NewTab(SmartScreenReportUrl,-1,L"");});}
        return;
    }
    std::wstring line;
    // i18n-exempt-begin: sandbox log lines (QA journal, not interface text)
    if(c==OP::Prompt::CopyLink){const std::wstring link=L"https://"+s.origin+L"/"+s.detail;line=CopyToClipboard(link)?L"prompt.dangerousDownload → lien copié (démonstration) : "+link:L"prompt.dangerousDownload → copie du lien impossible";}
    else line=L"prompt.dangerousDownload → signalement simulé (démonstration : rien n'est envoyé à Microsoft)";   // i18n-exempt-end
    Log(L"SANDBOX "+line);if(t&&!t->closed)SandboxLog(t,line);
}
void CreateElementSurfaces(){
    const std::wstring root=base;
    // The Période select (Effacer les données) drops the shared shadowless menu under its field.
    MD::Dialog::Hooks mh;mh.wake=[]{RequestElementFrame();};mh.keyboard=[]{return keyboardModality;};
    mh.choose=[](const MD::Select& select,RECT anchor,bool keyboard){return MD::ChooseFromMenu(contextMenu,select,anchor,keyboard,motion.ReducedMotion());};
    mh.dismiss=[]{contextMenu.Cancel();};
    {MD::Dialog::Hooks nh;nh.wake=mh.wake;nh.keyboard=mh.keyboard;if(!notesCard.Create(instance,window,root,std::move(nh)))Log(L"RELEASE NOTES card unavailable");}
    if(!modal.Create(instance,window,root,std::move(mh)))Log(L"MODAL unavailable");
    OP::Prompt::Hooks ph;ph.wake=[]{RequestElementFrame();};ph.keyboard=[]{return keyboardModality;};
    // B07 « Plus d'options » and the B08 selects drop the same shared, shadowless menu.
    ph.choose=[](const MD::Select& select,RECT anchor,bool keyboard){return MD::ChooseFromMenu(contextMenu,select,anchor,keyboard,motion.ReducedMotion());};ph.dismiss=[]{contextMenu.Cancel();};
    ph.menu=[](const std::vector<NativeChrome::ContextMenu::Item>& items,RECT anchor,bool keyboard){return contextMenu.Track(items,{anchor.right,anchor.bottom},&anchor,keyboard,motion.ReducedMotion(),true);};
    ph.download=[](OP::Prompt::Command c,const OP::Spec& s){PromptDownloadCommand(c,s);};
    ph.revoke=[](int kind,const OP::Spec& s){RevokeSitePermission(kind,s);};
    if(!originPrompt.Create(instance,window,root,std::move(ph)))Log(L"PROMPT unavailable");
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
    CreatePrintSurfaces();   // our print window (#121)
    ElementsReducedMotion(motion.ReducedMotion());
}
void DisposeElementSurfaces(){
    while(!modalQueue.empty()){auto job=std::move(modalQueue.front());modalQueue.pop_front();if(job.done)job.done(MD::Result{});}
    if(modalOpen)modal.Abort();
    while(!promptQueue.empty()){auto job=std::move(promptQueue.front());promptQueue.pop_front();OP::Result none;for(auto& a:job.answers)a(none);}
    if(promptOpen)originPrompt.Abort();
    siteStates.clear();chipMotion=ChipMotion{};
    FindDetach();
    DisposePrintSurfaces();
    notesCard.Dispose();modal.Dispose();originPrompt.Dispose();findBar.Dispose();contextToast.Dispose();fullscreenHint.Dispose();passwordDemo.Dispose();shortcutsPanel.Dispose();sandboxTip.Dispose();chromeTip.Dispose();
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
// i18n-exempt-begin: QA sandbox dispatcher (browser://debug): French review journal and fictitious demo data;
// the production components it opens are translated.
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
    passwordDemo.SetDemo();   // the sandbox always shows the fictitious demonstration
    if(!passwordDemo.Show(PageScreenRect(),[log]{log(L"password.manager → fermé");}))return false;
    log(L"password.manager · "+v+L" → ouvert (comptes fictifs en mémoire)");return true;
}
void SandboxCommand(const std::shared_ptr<Tab>& t,const NativeChrome::DebugSandbox::Command& c){
    std::weak_ptr<Tab> weak=t;
    // The page button's input (Entrée / Espace vs pointer) is the host's last input for what it opens.
    keyboardModality=c.keyboard;
    auto log=[weak](const std::wstring& line){Log(L"SANDBOX "+line);SandboxLog(weak.lock(),line);};
    const std::wstring& n=c.name;const std::wstring& v=c.variant;const std::wstring what=n+(v.empty()?L"":L" · "+v);
    auto modalDone=[log,what](const MD::Result& r){log(what+L" → "+ModalOutcome(r));};
    auto promptDone=[log,what](OP::Result& r){log(what+L" → "+PromptOutcome(r)+(r.action==OP::Action::Keep||r.action==OP::Action::Delete?L" (résultat simulé : aucun fichier réel)":L""));if(r.action==OP::Action::Manage)Defer([log]{SandboxPasswordManager(L"list",log);});};
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
        OP::Spec s;s.origin=demoOrigin;s.demo=true;s.modality=c.keyboard?MD::Spec::Modality::Keyboard:MD::Spec::Modality::Pointer;
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
        else if(n==L"prompt.screenShare"){s.kind=OP::Kind::ScreenShare;s.sources={L"Onglets",L"Fenêtres",L"Écrans"};
            // Fictitious sources only (titles and thumbnails): no real tab, window or screen is listed or captured.
            if(v!=L"empty")s.shareSources={{L"Ordre du jour — onglet démo",0,0},{L"Visioconférence — onglet démo",0,5},
                {L"Budget 2027 — tableur démo",1,3},{L"Notes — éditeur démo",1,1},{L"Navigateur — fenêtre démo",1,4},{L"Présentation — fenêtre démo",1,0},
                {L"Écran 1 (démo)",2,2},{L"Écran 2 (démo)",2,4}};
            auto titles=std::make_shared<std::vector<std::wstring>>();for(auto& x:s.shareSources)titles->push_back(x.title);
            AskPrompt(std::move(s),t,[log,what,titles](OP::Result& r){
                if(r.action==OP::Action::Choose&&r.choice>=0&&r.choice<(int)titles->size())log(what+L" → partage simulé de « "+(*titles)[(size_t)r.choice]+L" » (démonstration : aucune capture)");
                else log(what+L" → "+PromptOutcome(r));});
            return;}
        else if(n==L"prompt.keepFile"){s.kind=OP::Kind::KeepFile;s.origin=L"telechargements.exemple.test";s.detail=L"archive-photos.zip";}
        else if(n==L"prompt.passwordFill"){s.kind=OP::Kind::PasswordFill;s.origin=L"compte.exemple.test";s.accounts={{L"camille.demo@exemple.test",L"Utilisé il y a 2 jours"}};if(v==L"two")s.accounts.push_back({L"equipe.demo@exemple.test",L"Utilisé le 3 mars"});}
        else if(n==L"prompt.passwordSave"){s.kind=v==L"update"?OP::Kind::PasswordUpdate:OP::Kind::PasswordSave;s.origin=L"compte.exemple.test";s.user=L"camille.demo@exemple.test";}
        else{log(what+L" → inconnue");return;}
        // The sandbox tab is the active one: the prompt anchors to its address bar as a real one would.
        if(s.kind==OP::Kind::Permission){
            // Demo answers drive the demo chip state of the sandbox tab only (origin meet.exemple.test, « démonstration »).
            const auto kinds=s.permissions;std::weak_ptr<Tab> wt=t;
            AskPrompt(std::move(s),t,[promptDone,kinds,wt](OP::Result& r){
                if(r.action==OP::Action::Allow||r.action==OP::Action::AllowOnce||r.action==OP::Action::Block){RecordSiteAnswer(wt.lock(),L"demo",kinds,r.action!=OP::Action::Block,r.action==OP::Action::Allow&&r.remember,true);Defer([]{RefreshRequestChip();});}
                promptDone(r);});
            return;}
        AskPrompt(std::move(s),t,promptDone);return;}
    // ---- B02 after the decision (demo state of the sandbox tab; a real tab is never touched)
    if(n==L"permission.demo"){
        auto it=siteStates.find(t->serial);SiteState* st=it!=siteStates.end()&&it->second.demo?&it->second:nullptr;
        if(v==L"reset"){if(st)siteStates.erase(it);RefreshRequestChip();log(what+L" → état de démonstration effacé");return;}
        if(!st||st->g.granted.empty()){log(what+L" → accordez d'abord une autorisation de démonstration (B02 : Démo caméra…, puis Autoriser)");return;}
        if(v==L"active"){for(int k:st->g.granted)SiteRequest::SetActive(st->g,k,true);RefreshRequestChip();log(what+L" → utilisation simulée en cours (point bleu)");return;}
        if(v==L"stopped"){st->g.active.clear();RefreshRequestChip();log(what+L" → utilisation simulée arrêtée, autorisation conservée");return;}
        if(v==L"status"){ShowSiteStatus(t);log(what+L" → état du site ouvert");return;}
        log(what+L" → inconnue");return;}
    // ---- C
    if(n==L"find.open"){OpenFindBar();if(v==L"seed"){findBar.SetText(L"oxyde");FindQuery(L"oxyde",findBar.CurrentOptions());}log(what+L" → barre ouverte (Ctrl+F)");return;}
    if(n==L"error.page"||n==L"error.offline"){
        using EK=NativeChrome::ErrorPages::Kind;EK kind=EK::NameNotResolved;
        if(n==L"error.offline")kind=EK::Offline;else if(v==L"refused")kind=EK::Refused;else if(v==L"timedOut")kind=EK::TimedOut;else if(v==L"reset")kind=EK::Reset;else if(v==L"unreachable")kind=EK::Unreachable;else if(v==L"generic")kind=EK::Generic;
        // #121 certificate cases (board 1M3Q-0): the engine's status and the board's example issuer (none for the client case).
        int cert=-1;std::wstring issuer;
        if(v==L"certInvalid"){cert=COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID;issuer=L"Example Internal CA";}
        else if(v==L"certExpired"){cert=COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED;issuer=L"Example Issuing CA";}
        else if(v==L"certWrongName"){cert=COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT;issuer=L"Example Issuing CA";}
        else if(v==L"certRevoked"){cert=COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED;issuer=L"Example Issuing CA";}
        else if(v==L"certClient")cert=COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS;
        if(cert>=0)kind=EK::Certificate;
        if(pages.size()>=128){ShowTabLimitToast();return;}
        // A real navigation to a reserved, unresolvable host (RFC 2606 .invalid): the engine fails it,
        // our error page is shown for the requested variant.
        NewTab(L"https://"+(v.empty()?std::wstring(L"hors-ligne"):v)+L".sandbox.invalid/",current+1);auto nt=Active();
        if(nt){nt->errorPage.forced=(int)kind;nt->errorPage.forcedRetrying=v==L"retrying"||v==L"restored"; /* « Connexion rétablie » = the reprise board (1M20-0): back online AND reloading */nt->errorPage.forcedRestored=v==L"restored";nt->errorPage.forcedCert=cert;nt->errorPage.forcedIssuer=issuer;}
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
// i18n-exempt-end
NativeChrome::Tooltip::Content TipContent(const wchar_t* tip){
    std::wstring s=tip?tip:L"";NativeChrome::Tooltip::Content c{s,L"",L""};
    if(!s.empty()&&s.back()==L')'){const auto open=s.rfind(L" (");
        if(open!=std::wstring::npos){std::wstring key=s.substr(open+2,s.size()-open-3);
            if(key.rfind(L"Ctrl",0)==0||key.rfind(L"Alt",0)==0||key.rfind(L"Maj",0)==0||key.rfind(L"Shift",0)==0||(key.size()>=2&&key[0]==L'F'&&iswdigit(key[1]))){c.label=s.substr(0,open);c.shortcut=key;}}}
    return c;
}
// #143 (owner's capture 2026-11-25: « Téléchargements » over its open popover): a button whose menu, popover or drawer is
// open keeps its tooltip hidden, and after it closes until the pointer has left the button (Tooltip::MenuMute). Every
// toolbar / column button with a menu: Téléchargements, Son, Réglages, Historique (chrome.latched), a band folder and « » »
// (bandOpen), the profile menu, the compact column's « » » popover, « Gérer les favoris » (the Favoris drawer), the star /
// « add this page » (the bookmark popover). « Annoter »'s card blocks every tip already (ChromeTipBlock).
NativeChrome::Tooltip::MenuMute chromeTipMute;
bool ChromeTipMenuOpen(int id){
    if(!id)return false;
    if(id==chrome.latched||id==chrome.bandOpen)return true;
    if(id==900&&chrome.profileButton.open)return true;   // the profile button (Chrome region 900)
    if(id==NativeChrome::Chrome::SidebarMoreId&&chrome.sidebarPopover)return true;
    if(id==NativeChrome::Chrome::SidebarManageId&&bookmarksDrawer.Open())return true;
    if((id=='K'||id=='E')&&bookmarkPopover.Visible())return true;
    return false;
}
// A menu may have opened (every InvalidateChrome): a new one mutes its button and the tip showing (or due) goes at once.
void ChromeTipMenuSync(){if(!chromeTip.Window())return;int open=0;for(auto& r:chrome.regions)if(r.tip&&*r.tip&&ChromeTipMenuOpen(r.id)){open=r.id;break;}if(chromeTipMute.Opened(open))chromeTip.HideNow();}
void ChromeTipHover(int x,int y){
    if(!chromeTip.Window())return;
    const float d=GetDpiForWindow(window)/96.f;float fx=x/d,fy=y/d;
    if(!chrome.Local(fx,fy)){chromeTipMute.Muted(0,ChromeTipMenuOpen(chromeTipMute.id));chromeTip.Hide();return;}   // Mode Focus: only over the floating panel's visible part
    if(chrome.Hit(fx,fy)==NativeChrome::Chrome::SidebarEdgeId){chromeTipMute.Muted(NativeChrome::Chrome::SidebarEdgeId,ChromeTipMenuOpen(chromeTipMute.id));chromeTip.Hide();return;}   // #127: the column edge shows the ↔ cursor, no tooltip (Paper)
    ChromeTipMenuSync();
    for(auto& r:chrome.regions){
        if(!r.tip||!*r.tip||fx<r.rect.left||fx>=r.rect.right||fy<r.rect.top||fy>=r.rect.bottom)continue;
        if(chromeTipMute.Muted(r.id,ChromeTipMenuOpen(chromeTipMute.id))){chromeTip.Hide();return;}   // #143: over its own open menu
        RECT target=ChromePx(r.rect);MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&target),2);
        RECT bounds=NativeChrome::TooltipPlacement::VisibleRect(window);chromeTip.Hover(TipContent(r.tip),target,&bounds);return;
    }
    chromeTipMute.Muted(0,ChromeTipMenuOpen(chromeTipMute.id));   // off every button: a closed menu's mute ends
    chromeTip.Hide();
}
void ChromeTipHide(){if(chromeTip.Window())chromeTip.Hide();}
// The pointer left the window (into a popover, a drawer, another window): it has left the muted button too (#143).
void ChromeTipLeft(){chromeTipMute.Muted(0,ChromeTipMenuOpen(chromeTipMute.id));ChromeTipHide();}
// #127 compact column (Val, 24 nov.): the icons carry their names as tooltips, so the keyboard focus shows the focused
// control's tooltip too (a tab's title, a bookmark's, a tool's); anywhere else it just hides the pointer's.
void ChromeTipFocus(int id){
    if(!chromeTip.Window())return;
    ChromeTipMenuSync();if(chromeTipMute.Muted(id,ChromeTipMenuOpen(chromeTipMute.id))){chromeTip.HideNow();return;}   // #143: never over the menu it opened (until the focus leaves it)
    if(id&&chrome.sidebar&&!chrome.sidebarOverlay&&chrome.side.mode==NativeChrome::Sidebar::Mode::Compact)
        for(auto& r:chrome.regions){if(r.id!=id||!r.tip||!*r.tip)continue;
            RECT target{S(r.rect.left),S(r.rect.top),S(r.rect.right),S(r.rect.bottom)};MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&target),2);
            RECT bounds=NativeChrome::TooltipPlacement::VisibleRect(window);chromeTip.Show(TipContent(r.tip),target,&bounds);return;}
    chromeTip.Hide();
}
// #125 E1b: while the live-site card hangs under the Annoter button, no toolbar tip shows (the one there goes at once).
void ChromeTipBlock(bool on){chromeTip.Block(on);}
