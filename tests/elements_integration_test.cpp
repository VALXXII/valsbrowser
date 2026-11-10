// Contracts of src/elements-integration.h exercised on the production handlers, without
// a window, WebView2 or any component window (they are not created): every deferral is
// completed exactly once, nothing is granted by default, the sandbox bridge only accepts
// its own tab, exact source and token, and the sandbox host only loads when the user asked.
#include "../src/main.cpp"
#include <cstdio>
#include <cstdlib>

static int failures=0;
void Expect(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}
template<class I> struct Fake : I {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out) override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 2;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
};
LPWSTR Dup(const wchar_t* s){size_t n=(wcslen(s)+1)*sizeof(wchar_t);auto p=(LPWSTR)CoTaskMemAlloc(n);memcpy(p,s,n);return p;}
struct Deferral : Fake<ICoreWebView2Deferral> { int completed=0; HRESULT STDMETHODCALLTYPE Complete() override{++completed;return S_OK;} };
struct DialogArgs : Fake<ICoreWebView2ScriptDialogOpeningEventArgs> {
    Deferral deferral; int accepted=0; COREWEBVIEW2_SCRIPT_DIALOG_KIND kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM;
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(L"https://exemple.test/page");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Kind(COREWEBVIEW2_SCRIPT_DIALOG_KIND* v) override{*v=kind;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Message(LPWSTR* v) override{*v=Dup(L"Continuer ?");return S_OK;}
    HRESULT STDMETHODCALLTYPE Accept() override{++accepted;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_DefaultText(LPWSTR* v) override{*v=Dup(L"");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_ResultText(LPWSTR* v) override{*v=Dup(L"");return S_OK;}
    HRESULT STDMETHODCALLTYPE put_ResultText(LPCWSTR) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** d) override{*d=&deferral;return S_OK;}
};
struct PermissionArgs : Fake<ICoreWebView2PermissionRequestedEventArgs> {
    Deferral deferral; COREWEBVIEW2_PERMISSION_STATE state=COREWEBVIEW2_PERMISSION_STATE_DEFAULT; int writes=0;
    COREWEBVIEW2_PERMISSION_KIND kind=COREWEBVIEW2_PERMISSION_KIND_CAMERA;
    explicit PermissionArgs(COREWEBVIEW2_PERMISSION_KIND k=COREWEBVIEW2_PERMISSION_KIND_CAMERA):kind(k){}
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(L"https://meet.exemple.test/");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_PermissionKind(COREWEBVIEW2_PERMISSION_KIND* v) override{*v=kind;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL* v) override{*v=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_State(COREWEBVIEW2_PERMISSION_STATE* v) override{*v=state;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_State(COREWEBVIEW2_PERMISSION_STATE v) override{state=v;++writes;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** d) override{*d=&deferral;return S_OK;}
};
struct FakeCert : Fake<ICoreWebView2ClientCertificate> {
    std::wstring name, issuer; double to = 0;
    FakeCert(std::wstring n, std::wstring i, double t) : name(std::move(n)), issuer(std::move(i)), to(t) {}
    HRESULT STDMETHODCALLTYPE get_Subject(LPWSTR* v) override{*v=Dup((L"CN="+name).c_str());return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Issuer(LPWSTR* v) override{*v=Dup(issuer.c_str());return S_OK;}
    HRESULT STDMETHODCALLTYPE get_ValidFrom(double* v) override{*v=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_ValidTo(double* v) override{*v=to;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_DerEncodedSerialNumber(LPWSTR* v) override{*v=Dup(L"01");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_DisplayName(LPWSTR* v) override{*v=Dup(name.c_str());return S_OK;}
    HRESULT STDMETHODCALLTYPE ToPemEncoding(LPWSTR* v) override{*v=Dup(L"");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_PemEncodedIssuerCertificateChain(ICoreWebView2StringCollection** v) override{*v=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE get_Kind(COREWEBVIEW2_CLIENT_CERTIFICATE_KIND* v) override{*v=COREWEBVIEW2_CLIENT_CERTIFICATE_KIND_OTHER;return S_OK;}
};
struct FakeCertList : Fake<ICoreWebView2ClientCertificateCollection> {
    std::vector<FakeCert*> items;
    HRESULT STDMETHODCALLTYPE get_Count(UINT32* v) override{*v=(UINT32)items.size();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetValueAtIndex(UINT32 i,ICoreWebView2ClientCertificate** v) override{if(i>=items.size())return E_INVALIDARG;*v=items[i];return S_OK;}
};
struct CertArgs : Fake<ICoreWebView2ClientCertificateRequestedEventArgs> {
    Deferral deferral; FakeCertList list; ICoreWebView2ClientCertificate* selected=nullptr; BOOL cancel=FALSE,handled=FALSE; int port=443;
    HRESULT STDMETHODCALLTYPE get_Host(LPWSTR* v) override{*v=Dup(L"intranet.exemple.test");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Port(int* v) override{*v=port;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsProxy(BOOL* v) override{*v=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_AllowedCertificateAuthorities(ICoreWebView2StringCollection** v) override{*v=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE get_MutuallyTrustedCertificates(ICoreWebView2ClientCertificateCollection** v) override{*v=&list;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_SelectedCertificate(ICoreWebView2ClientCertificate** v) override{*v=selected;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_SelectedCertificate(ICoreWebView2ClientCertificate* v) override{selected=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Cancel(BOOL* v) override{*v=cancel;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override{cancel=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Handled(BOOL* v) override{*v=handled;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Handled(BOOL v) override{handled=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** d) override{*d=&deferral;return S_OK;}
};
struct AuthArgs : Fake<ICoreWebView2BasicAuthenticationRequestedEventArgs> {
    Deferral deferral; BOOL cancel=FALSE;
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(L"https://intranet.exemple.test/");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Challenge(LPWSTR* v) override{*v=Dup(L"Basic realm=\"x\"");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Response(ICoreWebView2BasicAuthenticationResponse** r) override{*r=nullptr;return E_FAIL;}
    HRESULT STDMETHODCALLTYPE get_Cancel(BOOL* v) override{*v=cancel;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override{cancel=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** d) override{*d=&deferral;return S_OK;}
};
struct ExternalArgs : Fake<ICoreWebView2LaunchingExternalUriSchemeEventArgs> {
    Deferral deferral; BOOL cancel=FALSE; const wchar_t* uri=L"mailto:demo@exemple.test";
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(uri);return S_OK;}
    HRESULT STDMETHODCALLTYPE get_InitiatingOrigin(LPWSTR* v) override{*v=Dup(L"https://exemple.test");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL* v) override{*v=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Cancel(BOOL* v) override{*v=cancel;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override{cancel=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral** d) override{*d=&deferral;return S_OK;}
};
struct StartingArgs : Fake<ICoreWebView2NavigationStartingEventArgs> {
    BOOL cancel=FALSE;
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(L"");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL* v) override{*v=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsRedirected(BOOL* v) override{*v=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_RequestHeaders(ICoreWebView2HttpRequestHeaders** v) override{*v=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE get_Cancel(BOOL* v) override{*v=cancel;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_Cancel(BOOL v) override{cancel=v;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_NavigationId(UINT64* v) override{*v=1;return S_OK;}
};
std::wstring Message(const std::wstring& token,const wchar_t* command,const wchar_t* variant){
    return L"{\"token\":\""+token+L"\",\"command\":\""+command+L"\",\"variant\":\""+variant+L"\"}";
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    base=L"build\\elements-integration";
    auto live=std::make_shared<Tab>(),closed=std::make_shared<Tab>();live->uri=L"https://exemple.test/page";live->home=false;closed->closed=true;
    pages={live};current=0;

    // Script dialogs: no dialog surface (window-less run) = cancelled, deferral completed once.
    {DialogArgs a;ScriptDialogOpening(live,&a);Expect(a.deferral.completed==1&&a.accepted==0,"dialog without surface: cancelled, deferral completed once");}
    {DialogArgs a;ScriptDialogOpening(closed,&a);Expect(a.deferral.completed==1&&a.accepted==0,"dialog of a closed tab: deferral completed once, never accepted");}
    {DialogArgs a;a.kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD;ScriptDialogOpening(nullptr,&a);Expect(a.deferral.completed==1&&a.accepted==0,"beforeunload of a vanished tab: stays on the page");}
    // Permissions: never granted without an answer; explicit deny, deferral completed once.
    {PermissionArgs a;PermissionRequested(live,&a);Expect(a.deferral.completed==1&&a.state==COREWEBVIEW2_PERMISSION_STATE_DENY,"permission without prompt surface: denied, completed once");}
    {PermissionArgs a;PermissionRequested(closed,&a);Expect(a.deferral.completed==1&&a.state==COREWEBVIEW2_PERMISSION_STATE_DENY,"permission of a closed tab: denied, completed once");}
    // HTTP authentication: cancelled, completed once, no credentials written.
    {AuthArgs a;BasicAuthenticationRequested(live,&a);Expect(a.deferral.completed==1&&a.cancel==TRUE,"auth without prompt surface: cancelled, completed once");}
    {AuthArgs a;BasicAuthenticationRequested(closed,&a);Expect(a.deferral.completed==1&&a.cancel==TRUE,"auth of a closed tab: cancelled, completed once");}
    // External protocols: the engine's own launch is always cancelled; refused schemes never reach a prompt.
    {ExternalArgs a;LaunchingExternalUriScheme(live,&a);Expect(a.cancel==TRUE&&a.deferral.completed==1,"external scheme: engine launch cancelled, deferral completed once");}
    {ExternalArgs a;a.uri=L"ms-msdt:/id PCWDiagnostic";LaunchingExternalUriScheme(live,&a);Expect(a.cancel==TRUE&&a.deferral.completed==1,"dangerous scheme refused");}
    Expect(ExternalSchemeRefused(L"FILE:///c:/windows")&&ExternalSchemeRefused(L"search-ms:query=x")&&ExternalSchemeRefused(L"javascript:alert(1)")&&!ExternalSchemeRefused(L"mailto:a@b.c")&&!ExternalSchemeRefused(L"zoommtg://join"),"external scheme denylist");
    // Sandbox host matching: exact host only.
    Expect(IsSandboxUri(L"https://browser-debug.invalid/sandbox")&&IsSandboxUri(L"HTTPS://BROWSER-DEBUG.INVALID/sandbox?x")&&IsSandboxUri(L"https://browser-debug.invalid"),"sandbox URI recognised");
    Expect(!IsSandboxUri(L"https://browser-debug.invalid.evil.test/sandbox")&&!IsSandboxUri(L"http://browser-debug.invalid/sandbox")&&!IsSandboxUri(L"https://evil.test/https://browser-debug.invalid/"),"look-alike hosts are not the sandbox");
    // Sandbox navigation guard: only a navigation the browser started for this tab.
    {StartingArgs a;auto t=std::make_shared<Tab>();Expect(SandboxNavigationStarting(t,&a,NativeChrome::DebugSandbox::Url)&&a.cancel==TRUE&&!t->sandbox,"page-initiated sandbox navigation cancelled");}
    {StartingArgs a;auto t=std::make_shared<Tab>();t->sandboxPending=true;Expect(!SandboxNavigationStarting(t,&a,NativeChrome::DebugSandbox::Url)&&a.cancel==FALSE&&t->sandbox&&!t->sandboxPending,"user-opened sandbox navigation allowed once");
     StartingArgs again;Expect(SandboxNavigationStarting(t,&again,NativeChrome::DebugSandbox::Url)&&again.cancel==TRUE,"a new-document navigation back to the sandbox needs the user again");}
    {StartingArgs a;auto t=std::make_shared<Tab>();Expect(!SandboxNavigationStarting(t,&a,L"https://exemple.test/")&&a.cancel==FALSE,"other navigations untouched");}
    // Sandbox bridge: own tab, exact top-level source, this launch's token, allowlisted command.
    {
        auto sb=std::make_shared<Tab>();sb->sandbox=true;sb->uri=NativeChrome::DebugSandbox::Url;
        const std::wstring token=SandboxToken();const size_t before=deferredJobs.size();
        Expect(token.size()>=32,"per-launch token generated");
        Expect(!SandboxMessage(live,NativeChrome::DebugSandbox::Url,Message(token,L"modal.alert",L"default")),"message from a non-sandbox tab ignored");
        Expect(!SandboxMessage(sb,L"https://evil.test/",Message(token,L"modal.alert",L"default")),"message from another source ignored");
        Expect(!SandboxMessage(sb,L"about:blank",Message(token,L"modal.alert",L"default")),"message from about:blank ignored");
        SandboxMessage(sb,NativeChrome::DebugSandbox::Url,Message(L"0000",L"modal.alert",L"default"));
        SandboxMessage(sb,NativeChrome::DebugSandbox::Url,Message(token,L"system.exec",L""));
        SandboxMessage(sb,NativeChrome::DebugSandbox::Url,L"not json");
        Expect(deferredJobs.size()==before,"wrong token, unknown command or malformed message never runs");
        auto moved=std::make_shared<Tab>();moved->sandbox=true;moved->uri=L"https://exemple.test/";
        Expect(!SandboxMessage(moved,NativeChrome::DebugSandbox::Url,Message(token,L"modal.alert",L"default")),"sandbox tab that left the sandbox ignored");
        Expect(SandboxMessage(sb,NativeChrome::DebugSandbox::Url,Message(token,L"modal.alert",L"default"))&&deferredJobs.size()==before+1,"valid command accepted once");
        deferredJobs.clear();
    }
    // B01 address-bar chip on the real prompt surface: follows the open permission popover of the
    // active tab and nothing else (merge, answer, navigation, tab switch / close, address editing).
    {
        instance=GetModuleHandleW(nullptr);
        window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"owner",WS_POPUP,0,0,1200,800,nullptr,nullptr,instance,nullptr);
        wchar_t cwd[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,cwd);
        OP::Prompt::Hooks ph;ph.revoke=[](int k,const OP::Spec& sp){RevokeSitePermission(k,sp);};Expect(window&&originPrompt.Create(instance,window,cwd,std::move(ph)),"prompt surface created");
        auto run=[]{for(int i=0;i<6&&!deferredJobs.empty();++i){auto jobs=std::move(deferredJobs);deferredJobs.clear();for(auto& j:jobs)j();}};
        auto chip=[]{return chrome.requestLive?chrome.requestGlyphs:std::vector<const char*>{};};
        using SiteRequest::State;
        // Camera + microphone of one getUserMedia(): one popover, one chip with both glyphs.
        PermissionArgs cam(COREWEBVIEW2_PERMISSION_KIND_CAMERA),mic(COREWEBVIEW2_PERMISSION_KIND_MICROPHONE);
        PermissionRequested(live,&cam);PermissionRequested(live,&mic);
        Expect(chip().empty(),"chip: nothing before the popover shows");
        run();
        Expect(promptOpen&&RequestChipOpen()&&chip()==std::vector<const char*>{SiteRequest::Path::camera,SiteRequest::Path::microphone},"chip: camera + microphone grouped (B01)");
        Expect(chrome.requestTip.find(L"meet.exemple.test")==0&&chrome.requestTip.find(L"Caméra et micro")!=std::wstring::npos,"chip: tooltip / UIA name names the origin and the request");
        RequestChipActivated();
        Expect(cam.deferral.completed==0&&mic.deferral.completed==0&&cam.writes==0&&promptOpen,"chip click: back to the popover, never an answer");
        ElementsNavigationStarting(live);run();
        Expect(cam.deferral.completed==1&&mic.deferral.completed==1&&cam.state==COREWEBVIEW2_PERMISSION_STATE_DENY&&mic.state==COREWEBVIEW2_PERMISSION_STATE_DENY,"navigation: pending request denied (not saved), completed once");
        Expect(!promptOpen&&chip().empty(),"navigation: chip cleared");
        originPrompt.Surface().FinishAnimationsForTest();
        // Position, answered « Autoriser »: granted, and no indicator survives the answer.
        PermissionArgs geo(COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION);PermissionRequested(live,&geo);run();
        Expect(chip()==std::vector<const char*>{SiteRequest::Path::position},"chip: position glyph");
        originPrompt.Surface().Finish(OP::Id::Accept);run();
        Expect(geo.state==COREWEBVIEW2_PERMISSION_STATE_ALLOW&&geo.deferral.completed==1,"allow: granted, completed once");
        Expect(chip()==std::vector<const char*>{SiteRequest::Path::position}&&chrome.requestState==State::Granted,"allow: the chip stays as « autorisé » (persistent)");
        Expect(chrome.requestState!=State::Active&&siteStates[live->serial].g.active.empty(),"allow: never shown as in use (no engine capture signal)");
        Expect(chrome.requestTip.find(L"autorisé")!=std::wstring::npos,"granted: tooltip / UIA says « autorisé »");
        originPrompt.Surface().FinishAnimationsForTest();
        // Chip click on a granted site: the B02 status popover, never a new request.
        RequestChipActivated();run();
        Expect(promptOpen&&promptActive.spec.kind==OP::Kind::SiteStatus&&geo.writes==1,"granted chip click: status popover, no new request or answer");
        Expect(originPrompt.Surface().CurrentSpec().rows.size()==1&&originPrompt.Surface().CurrentSpec().rows[0].toggle==1,"status: one row, toggle on");
        // The motion settles: no wake once the chip is still.
        Expect(!ChipNextWake(NativeChrome::Motion::Now()+NativeChrome::Motion::Milliseconds(400)).frames,"chip motion: no frame clock at rest");
        originPrompt.Surface().ActivateForTest(MD::Dialog::RowHit(0));run();
        Expect(siteStates[live->serial].g.granted.empty()&&chip().empty(),"status toggle off: permission revoked, chip leaves");
        ChipFrame(NativeChrome::Motion::Now()+NativeChrome::Motion::Milliseconds(400));
        Expect(chrome.requestGlyphs.empty()&&!ChipNextWake(NativeChrome::Motion::Now()).frames,"chip exit finished: nothing drawn, no wake");
        Expect(originPrompt.Visible()&&originPrompt.Surface().CurrentSpec().rows.size()==1&&originPrompt.Surface().CurrentSpec().rows[0].toggle==0,"status: revoked row turned off, inert (no re-grant from here)");
        originPrompt.Abort();run();originPrompt.Surface().FinishAnimationsForTest();
        // Granted again, then a navigation of the tab: state cleared (another document).
        PermissionArgs cam2(COREWEBVIEW2_PERMISSION_KIND_CAMERA);PermissionRequested(live,&cam2);run();
        originPrompt.Surface().Finish(OP::Id::Accept);run();originPrompt.Surface().FinishAnimationsForTest();
        Expect(chip()==std::vector<const char*>{SiteRequest::Path::camera}&&chrome.requestState==State::Granted,"camera granted: persistent chip");
        ElementsNavigationStarting(live);run();
        Expect(chip().empty()&&!siteStates.count(live->serial),"navigation: granted state cleared");
        // A blocked request: the chip shows « bloqué » for this page.
        PermissionArgs mic2(COREWEBVIEW2_PERMISSION_KIND_MICROPHONE);PermissionRequested(live,&mic2);run();
        originPrompt.Surface().Finish(OP::Id::Cancel);run();originPrompt.Surface().FinishAnimationsForTest();
        Expect(mic2.state==COREWEBVIEW2_PERMISSION_STATE_DENY&&chip()==std::vector<const char*>{SiteRequest::Path::microphone}&&chrome.requestState==State::Blocked,"block: chip shows « bloqué »");
        ElementsNavigationStarting(live);run();
        // Notifications, then the user switches tab: cancelled, chip gone.
        auto other=std::make_shared<Tab>();other->uri=L"https://autre.exemple.test/";other->home=false;pages={live,other};
        PermissionArgs bell(COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS);PermissionRequested(live,&bell);run();
        Expect(chip()==std::vector<const char*>{SiteRequest::Path::bell},"chip: notifications glyph");
        current=1;ElementsTabChanged();run();
        Expect(bell.deferral.completed==1&&bell.state==COREWEBVIEW2_PERMISSION_STATE_DENY&&chip().empty(),"tab switch: request cancelled, chip gone");
        // Another tab never gets this site's badge.
        RecordSiteAnswer(live,OriginOf(live->uri),{COREWEBVIEW2_PERMISSION_KIND_CAMERA},true,false,false);SyncRequestChip();
        Expect(chip().empty(),"other tab: no badge from the first tab's grants");
        current=0;SyncRequestChip();
        Expect(chip()==std::vector<const char*>{SiteRequest::Path::camera},"back on the tab: its grant shows again");
        siteStates.erase(live->serial);SyncRequestChip();ChipFrame(NativeChrome::Motion::Now()+NativeChrome::Motion::Milliseconds(400));
        originPrompt.Surface().FinishAnimationsForTest();
        // Clipboard asked while the address is being edited: held (no focus theft), shown after.
        editing=true;PermissionArgs clip(COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ);PermissionRequested(live,&clip);run();
        Expect(!promptOpen&&clip.deferral.completed==0&&chip().empty(),"editing: the request waits, the address keeps focus, no chip");
        editing=false;SyncRequestChip();run();
        Expect(promptOpen&&chip()==std::vector<const char*>{SiteRequest::Path::clipboard}&&chrome.requestState==State::Pending,"editing ended: popover and clipboard chip shown (pending)");
        editing=true;SyncRequestChip();
        Expect(chip().empty()&&promptOpen,"editing again: chip hidden, request kept");
        editing=false;SyncRequestChip();
        ElementsTabClosed(live);run();
        Expect(clip.deferral.completed==1&&clip.state==COREWEBVIEW2_PERMISSION_STATE_DENY&&chip().empty()&&!promptOpen,"tab closed: request denied, chip gone");
        originPrompt.Surface().FinishAnimationsForTest();
        // B08 client certificates: the engine's real candidates, one deferral, explicit answers.
        {
            FakeCert good(L"Camille Démo",L"Autorité Démo",(double)time(nullptr)+86400*365),old(L"Ancien poste",L"CA Démo",1000);
            CertArgs a;a.list.items={&good,&old};ClientCertificateRequested(live,&a);run();
            Expect(a.handled&&promptOpen&&promptActive.spec.kind==OP::Kind::ClientCertificate&&promptActive.spec.accounts.size()==2,"certificate: our prompt lists the engine's candidates (engine dialog suppressed)");
            Expect(promptActive.spec.origin==L"intranet.exemple.test"&&promptActive.spec.accounts[0].user==L"Camille Démo"&&promptActive.spec.accounts[0].hint.find(L"Émis par Autorité Démo")==0,"certificate: host and issuer shown");
            Expect(!promptActive.spec.accounts[0].disabled&&promptActive.spec.accounts[1].disabled&&promptActive.spec.accounts[1].hint.find(L"expiré le")!=std::wstring::npos,"certificate: expired one disabled and labelled");
            originPrompt.Surface().ActivateForTest(MD::Dialog::AccountHit(1));
            Expect(a.deferral.completed==0&&promptOpen,"certificate: an expired row cannot be chosen");
            originPrompt.Surface().ActivateForTest(MD::Dialog::AccountHit(0));run();
            Expect(a.deferral.completed==1&&a.selected==&good&&!a.cancel,"certificate: choice = SelectedCertificate, completed once");
            originPrompt.Surface().FinishAnimationsForTest();
        }
        {
            FakeCert good(L"Camille Démo",L"Autorité Démo",(double)time(nullptr)+86400);
            CertArgs a;a.port=8443;a.list.items={&good};ClientCertificateRequested(live,&a);run();
            Expect(promptActive.spec.origin==L"intranet.exemple.test:8443","certificate: non-default port shown");
            originPrompt.Surface().KeyForTest(VK_ESCAPE);run();
            Expect(a.deferral.completed==1&&!a.selected&&!a.cancel&&a.handled,"certificate: « Ne pas envoyer » / Échap = continue without a certificate");
            originPrompt.Surface().FinishAnimationsForTest();
        }
        {
            FakeCert good(L"Camille Démo",L"Autorité Démo",(double)time(nullptr)+86400);
            CertArgs a;a.list.items={&good};ClientCertificateRequested(live,&a);run();
            ElementsNavigationStarting(live);run();
            Expect(a.deferral.completed==1&&a.cancel&&!a.selected,"certificate: navigation away cancels the request");
            originPrompt.Surface().FinishAnimationsForTest();
            CertArgs b;b.list.items={&good};ClientCertificateRequested(live,&b);run();ElementsTabClosed(live);run();
            Expect(b.deferral.completed==1&&b.cancel,"certificate: tab closed cancels, completed once");
            originPrompt.Surface().FinishAnimationsForTest();
            CertArgs none;ClientCertificateRequested(live,&none);run();
            Expect(none.deferral.completed==1&&none.handled&&!none.cancel&&!promptOpen,"certificate: no candidate = continue without one, no prompt");
            CertArgs gone;gone.list.items={&good};ClientCertificateRequested(closed,&gone);
            Expect(gone.deferral.completed==1&&gone.cancel,"certificate: request of a closed tab cancelled");
        }
        // B07 real path: a download the engine blocked; real « Supprimer », no « Conserver ».
        {
            const std::wstring file=std::wstring(cwd)+L"\\build\\elements-integration\\blocked-fixture.exe.tmp";
            {std::ofstream f(std::filesystem::path(file),std::ios::binary);f<<"fixture";}
            ElementsDangerousDownload(424242,file,L"https://telechargements.exemple.test/blocked-fixture.exe",COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS);run();
            Expect(promptOpen&&promptActive.spec.kind==OP::Kind::DangerousDownload&&!promptActive.spec.demo&&!promptActive.spec.keepAvailable,"blocked download: real B07, no demo badge, no keep");
            Expect(promptActive.spec.detail==L"blocked-fixture.exe.tmp"&&promptActive.spec.origin==L"telechargements.exemple.test"&&promptActive.spec.reason.find(L"Defender")!=std::wstring::npos,"blocked download: real file, site and engine reason");
            Expect(OP::Prompt::MoreItems(false).size()==2&&OP::Prompt::MoreItems(true).size()==4,"blocked download: « Conserver quand même » only where it is real (demo)");
            originPrompt.Surface().Finish(OP::Id::Accept);run();
            Expect(!std::filesystem::exists(std::filesystem::path(file))&&!promptOpen,"blocked download: « Supprimer le fichier » removes the leftover file");
            originPrompt.Surface().FinishAnimationsForTest();
            const auto spec=DangerousDownloadSpec(L"C:\\x\\a.zip",L"https://s.exemple.test/a.zip",COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED_BY_POLICY);
            Expect(spec.reason.find(L"stratégie")!=std::wstring::npos&&spec.link==L"https://s.exemple.test/a.zip"&&spec.detail==L"a.zip","blocked by policy: own reason; link kept for « Copier le lien »");
        }
        // A2/A4/A6 real variants on the production modal surface.
        {
            MD::Dialog::Hooks mh;Expect(modal.Create(instance,window,cwd,std::move(mh)),"modal surface created");
            auto finish=[&](int button){modal.Finish(button);run();modal.FinishAnimationsForTest();};
            DialogArgs first;ScriptDialogOpening(live,&first);run();
            Expect(modalOpen&&modalActive.spec.checkLabel.empty(),"first dialog of a document: no blocking check");
            finish(MD::AcceptButton);
            DialogArgs second;ScriptDialogOpening(live,&second);run();
            Expect(modalOpen&&modalActive.spec.checkLabel==L"Empêcher ce site d'ouvrir d'autres dialogues","second dialog: A2 « Empêcher… » offered in the real path");
            modal.ActivateForTest(MD::Dialog::SimpleCheckHit);finish(MD::CancelButton);
            DialogArgs third;ScriptDialogOpening(live,&third);run();
            Expect(third.deferral.completed==1&&third.accepted==0&&!modalOpen,"checked: the site's next dialogs are suppressed (answered « cancel »)");
            ElementsNavigationStarting(live);run();
            DialogArgs fourth;ScriptDialogOpening(live,&fourth);run();
            Expect(modalOpen&&modalActive.spec.checkLabel.empty(),"navigation lifts the block and restarts the count");
            finish(MD::AcceptButton);
            DialogArgs leave;leave.kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD;ScriptDialogOpening(live,&leave);run();
            Expect(modalActive.spec.title==L"Quitter ce site ?","beforeunload: « Quitter » by default");
            finish(MD::CancelButton);
            live->refreshing=true;DialogArgs reload;reload.kind=COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD;ScriptDialogOpening(live,&reload);run();
            Expect(modalActive.spec.title==L"Actualiser ce site ?"&&modalActive.spec.buttons[1].label==L"Actualiser","beforeunload during our reload: « Actualiser »");
            finish(MD::CancelButton);live->refreshing=false;
            Expect(reload.deferral.completed==1&&reload.accepted==0,"« Rester sur la page »: unload refused, completed once");
            // Closing with a download running (one tab): always asked, destructive wording.
            NativeChrome::Downloads::Item d;d.id=777001;d.path=L"C:\\fixture\\rapport.pdf";d.state=NativeChrome::Downloads::State::InProgress;d.received=42;d.total=100;downloads.insert(downloads.begin(),d);
            const bool allowed=CloseWindowAllowed(false,nullptr);run();
            Expect(!allowed&&modalOpen&&modalActive.spec.kind==MD::Kind::CloseWindow&&!modalActive.spec.infos.empty()&&modalActive.spec.infos[0].text==L"rapport.pdf"&&modalActive.spec.infos[0].trailing==L"42 %","close with a running download: A6 download variant with the real file");
            Expect(modalActive.spec.buttons[1].label==L"Fermer et annuler le téléchargement","close with a download: destructive action named");
            finish(MD::CancelButton);
            downloads.erase(downloads.begin());
            {auto keep=pages;pages={live};Expect(CloseWindowAllowed(false,nullptr),"one tab, no download: closes without asking");pages=keep;}
        }
        // Shutdown with a pending request: denied once, nothing left.
        PermissionArgs last(COREWEBVIEW2_PERMISSION_KIND_CAMERA);PermissionRequested(live,&last);run();
        DisposeElementSurfaces();
        Expect(last.deferral.completed==1&&last.state==COREWEBVIEW2_PERMISSION_STATE_DENY&&!RequestChipOpen(),"shutdown: pending request denied once, no chip");
        deferredJobs.clear();pages={live};current=0;
        DestroyWindow(window);window=nullptr;
    }
    // A9 window close with several tabs (« Confirmer avant de fermer plusieurs onglets » on), on
    // the real modal surface: a user close asks and can be cancelled or confirmed; the updater's
    // install handoff and a clear-on-exit already under way close without asking (a question
    // there would block the install / the shutdown); the sandbox demo still only demonstrates.
    {
        instance=GetModuleHandleW(nullptr);
        window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"owner",WS_POPUP,0,0,1200,800,nullptr,nullptr,instance,nullptr);
        wchar_t cwd[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,cwd);
        Expect(window&&modal.Create(instance,window,cwd,{}),"modal surface created");
        auto run=[]{for(int i=0;i<6&&!deferredJobs.empty();++i){auto jobs=std::move(deferredJobs);deferredJobs.clear();for(auto& j:jobs)j();}};
        auto closePosted=[]{MSG m{};bool any=false;while(PeekMessageW(&m,window,WM_CLOSE,WM_CLOSE,PM_REMOVE))any=true;return any;};
        auto a=std::make_shared<Tab>(),b=std::make_shared<Tab>(),c=std::make_shared<Tab>();
        for(auto& t:{a,b,c}){t->uri=L"https://exemple.test/";t->home=false;}
        pages={a,b,c};current=0;settings.confirmCloseTabs=true;closeConfirmed=false;closeWithoutPrompt=false;exitClear=ExitClear::Idle;
        // User close: asks, « Annuler » keeps the window.
        Expect(!CloseWindowAllowed()&&modalOpen&&modal.CurrentSpec().automationId==L"CloseWindow","user close with 3 tabs: asks first");
        modal.Finish(MD::CancelButton);run();
        Expect(!modalOpen&&!closeConfirmed&&!closePosted(),"user close cancelled: window kept");
        // User close confirmed: closes (WM_CLOSE re-posted, then allowed).
        Expect(!CloseWindowAllowed()&&modalOpen,"user close asks again");
        modal.Finish(MD::AcceptButton);run();
        Expect(!modalOpen&&closeConfirmed&&closePosted()&&CloseWindowAllowed(),"user close confirmed: window closes");
        closeConfirmed=false;
        // Updater install handoff (RestartInstall): never a question.
        closeWithoutPrompt=true;
        Expect(CloseWindowAllowed()&&!modalOpen,"install handoff closes with several tabs, no question");
        // Sandbox demo: still demonstrates (asks, reports the answer, never closes) even then.
        bool demoAnswered=false,demoOk=false;
        Expect(!CloseWindowAllowed(true,[&](bool ok){demoAnswered=true;demoOk=ok;})&&modalOpen,"demo: shows the question");
        modal.Finish(MD::AcceptButton);run();
        Expect(demoAnswered&&demoOk&&!closeConfirmed&&!closePosted(),"demo confirmed: reported, window kept");
        closeWithoutPrompt=false;
        // Clear on exit already waiting for the engine: the repeated WM_CLOSE goes through.
        exitClear=ExitClear::Waiting;
        Expect(CloseWindowAllowed()&&!modalOpen,"clear on exit under way: close continues, no new question");
        exitClear=ExitClear::Idle;
        modal.Abort();run();deferredJobs.clear();pages={live};current=0;
        DestroyWindow(window);window=nullptr;
    }
    // A8 destinations: the public releases repository (notes of the running version, issues).
    Expect(ReleaseNotesUrl(L"0.3.132")==L"https://github.com/VALXXII/browser-releases/releases/tag/v0.3.132"&&ReleaseNotesUrl(L"dev")==L"https://github.com/VALXXII/browser-releases/releases","about: release notes of the running version");
    Expect(std::wstring(IssuesUrl)==L"https://github.com/VALXXII/browser-releases/issues/new","about: report a problem on the public issues");
    // A9 private window: the app writes nothing at all (session, history, downloads, settings, bookmarks, log).
    {
        const std::wstring dir=L"build/elements-integration/private-fixture";std::error_code ec;std::filesystem::remove_all(dir,ec);std::filesystem::create_directories(dir,ec);
        const auto saved=std::make_tuple(dataDir,sessionFile,downloadsFile,settingsFile,logFile,smoke,sessionReady);
        dataDir=dir;sessionFile=dir+L"/session";downloadsFile=dir+L"/downloads";settingsFile=dir+L"/settings";logFile=dir+L"/log.txt";smoke=false;sessionReady=true;privateWindow=true;
        SaveSession();SaveHistory();SaveDownloads();SaveSettings();SaveBookmarks();Log(L"NAV https://secret.example/");
        Expect(std::filesystem::is_empty(dir),"private window: no session, history, downloads, settings, bookmarks or log file written");
        privateWindow=false;Log(L"control");
        Expect(!std::filesystem::is_empty(dir),"control: the same paths are written by a normal window");
        std::tie(dataDir,sessionFile,downloadsFile,settingsFile,logFile,smoke,sessionReady)=saved;std::filesystem::remove_all(dir,ec);
        Expect(PrivateCommandLine(L"C:/b/browser.exe")==L"\"C:/b/browser.exe\" --private","private window: same executable, only --private passed");
        Expect(std::wstring(PrivateProfileName)==L"browser-private","private window: dedicated InPrivate profile name, never the live profile");
    }
    Expect(AboutStatusNow().title==L"Mises à jour indisponibles"&&!AboutStatusNow().hasButton,"about: build without the update engine says so, no fake check");
    // Origin labels shown in dialogs.
    Expect(OriginLabel(L"https://www.exemple.test:8443/a?b")==L"exemple.test:8443"&&OriginLabel(L"about:blank")==L"Cette page"&&OriginLabel(L"https://user@site.test/")==L"site.test","origin label");
    pages.clear();
    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    puts("PASS elements integration: deferrals once, deny by default, sandbox guard and bridge, schemes, labels, B01 request chip lifecycle");
    return 0;
}
