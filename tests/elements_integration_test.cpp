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
    HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR* v) override{*v=Dup(L"https://meet.exemple.test/");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_PermissionKind(COREWEBVIEW2_PERMISSION_KIND* v) override{*v=COREWEBVIEW2_PERMISSION_KIND_CAMERA;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL* v) override{*v=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_State(COREWEBVIEW2_PERMISSION_STATE* v) override{*v=state;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_State(COREWEBVIEW2_PERMISSION_STATE v) override{state=v;++writes;return S_OK;}
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
    // Origin labels shown in dialogs.
    Expect(OriginLabel(L"https://www.exemple.test:8443/a?b")==L"exemple.test:8443"&&OriginLabel(L"about:blank")==L"Cette page"&&OriginLabel(L"https://user@site.test/")==L"site.test","origin label");
    pages.clear();
    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    puts("PASS elements integration: deferrals once, deny by default, sandbox guard and bridge, schemes, labels");
    return 0;
}
