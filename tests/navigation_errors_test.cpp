// Exercise the production event handlers without starting WebView2 or a browser.
#include "../src/main.cpp"
#include <cstdio>
#include <cstdlib>

void Require(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class I> struct EventArgs : I {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
};
struct NavigationArgs : EventArgs<ICoreWebView2NavigationCompletedEventArgs> {
    UINT64 id=1;COREWEBVIEW2_WEB_ERROR_STATUS error=COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED;
    HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL* value) override{*value=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_WebErrorStatus(COREWEBVIEW2_WEB_ERROR_STATUS* value) override{*value=error;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_NavigationId(UINT64* value) override{*value=id;return S_OK;}
};
struct ProcessArgs : EventArgs<ICoreWebView2ProcessFailedEventArgs> {
    COREWEBVIEW2_PROCESS_FAILED_KIND kind=COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED;
    HRESULT STDMETHODCALLTYPE get_ProcessFailedKind(COREWEBVIEW2_PROCESS_FAILED_KIND* value) override{*value=kind;return S_OK;}
};
int main(){
    auto broken=std::make_shared<Tab>(),other=std::make_shared<Tab>();
    broken->uri=L"https://exemple.cooom/";broken->home=false;
    pages={broken,other};current=-1; // No window/controller: test background tabs.
    base=L"build\\navigation-errors";
    auto now=NativeChrome::Motion::Now();other->loading.Start(9,now);
    NavigationArgs nav;
    for(auto error:{COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED,
                    COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT,
                    COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT,
                    COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED}){
        nav.error=error;broken->loading.Start(nav.id,now);broken->refreshing=true;
        Require(NavigationCompleted(broken,&nav)==S_OK,"navigation handler succeeds");
        Require(!halted,"ordinary navigation error must not halt browser");
        Require(!broken->loading.IsLoading()&&!broken->refreshing,"failed load ends refresh state");
        Require(other->loading.IsLoading(),"other tab keeps loading");
        Require(broken->uri==L"https://exemple.cooom/","retry URL preserved");
        ++nav.id;
    }
    {   // A possible download (CONNECTION_ABORTED of the tab's latest navigation) is held, never shown at once;
        // a transport failure a download never produces keeps the existing path.
        const auto savedId=nav.id;const auto savedError=nav.error;
        broken->downloadNav.Starting(50,L"https://exemple.cooom/fichier.zip",GetTickCount64());
        nav.id=50;nav.error=COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED;
        Require(NavigationCompleted(broken,&nav)==S_OK&&!halted,"aborted navigation handled");
        Require(broken->downloadNav.Pending(50)&&broken->uri==L"https://exemple.cooom/","a possible download is held: page and address stay");
        broken->downloadNav.Starting(51,L"https://exemple.cooom/autre",GetTickCount64());
        nav.id=51;nav.error=COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT;
        Require(NavigationCompleted(broken,&nav)==S_OK&&!halted,"refused navigation handled");
        Require(!broken->downloadNav.Pending(51)&&!broken->downloadNav.Pending(50),"a refused connection is never held; the newer navigation voids the older hold");
        nav.id=savedId;nav.error=savedError;
    }
    // A canceled older navigation cannot finish the replacement's progress.
    broken->loading.Start(nav.id+1,now);
    NavigationCompleted(broken,&nav);
    Require(broken->loading.IsLoading(),"stale completion preserves newer load");
    ProcessArgs process;broken->refreshing=true;
    Require(ProcessFailed(broken,&process)==S_OK,"renderer crash handled");
    Require(!halted&&!broken->loading.IsLoading()&&!broken->refreshing,"renderer crash stays recoverable");
    Require(other->loading.IsLoading(),"renderer crash does not stop other tab");
    process.kind=COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED;
    ProcessFailed(broken,&process);Require(!halted,"auxiliary crash is nonfatal");
    broken->closed=true;process.kind=COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
    ProcessFailed(broken,&process);NavigationCompleted(nullptr,&nav);
    Require(!halted,"late callbacks for closed or missing tabs ignored");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"WebView2 never loaded");
    pages.clear();puts("PASS navigation failures, cancellations, per-tab isolation, renderer recovery, late callbacks");
}
