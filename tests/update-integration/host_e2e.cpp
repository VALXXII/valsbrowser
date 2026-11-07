// Host integration fixture: the PRODUCTION src/main.cpp compiled with the real update
// engine (update-core/update-package) and the real toast (update-toast), driven through
// the same functions and window-procedure branches the browser uses. WebView2 is never
// loaded (no environment is created; tabs are plain records), no browser.exe is ever
// started, the transport serves files from a local directory and every path lives in a
// temp root given on the command line. Built and driven by e2e_install.py.
//
//   host_e2e.exe install <root> <serveDir> <version> <report>   Available->Download->Ready->RestartInstall
//   host_e2e.exe cancel  <root> <serveDir> <version> <report>   cancel mid-download, dismiss, manual re-check, close
//   host_e2e.exe offline <root> <serveDir> <version> <report>   silent startup failure, manual check shows Error
#include "../../src/main.cpp"
#include <cstdio>
#include <atomic>
#include <thread>

namespace {
std::atomic<std::int64_t> mockNow{1800000000};
std::wstring reportPath; std::vector<std::string> report; int failures=0;
void Note(const std::string& line){report.push_back(line);std::printf("%s\n",line.c_str());std::fflush(stdout);}
void Expect(bool ok,const std::string& what){Note(std::string(ok?"PASS ":"FAIL ")+what);if(!ok)++failures;}
void Flush(){std::ofstream out{std::filesystem::path(reportPath),std::ios::binary|std::ios::trunc};for(auto& l:report)out<<l<<"\n";}
const char* PhaseName(BrowserUpdate::Phase p){using P=BrowserUpdate::Phase;switch(p){case P::Hidden:return "Hidden";case P::Checking:return "Checking";case P::Available:return "Available";case P::Downloading:return "Downloading";case P::Ready:return "Ready";case P::Installing:return "Installing";case P::Installed:return "Installed";case P::Error:return "Error";}return "?";}

// Serves <dir>\<last URL segment>; missing file = HTTP 404 (no published release).
struct DiskTransport final:UpdateCore::Transport {
    std::wstring dir; DWORD chunkDelayMs=0; bool offline=false; std::atomic<bool> aborted{false}; std::atomic<int> requests{0},rejected{0};
    UpdateCore::HttpResult Get(const std::wstring& url,std::uint64_t maxBytes,const std::function<bool(std::uint64_t)>& onLength,
                               const std::function<bool(const void*,std::size_t)>& sink,const std::atomic<bool>& cancel) override{
        ++requests;aborted=false;UpdateCore::HttpResult r;
        if(offline){r.status=UpdateCore::HttpResult::Failed;r.error="fixture: network unreachable";return r;}
        // Only URLs the production policy accepts for the configured feed are served.
        if(!UpdateCore::AllowedUrl(url,BROWSER_UPDATE_REPO_WIDE,true)){r.status=UpdateCore::HttpResult::Rejected;++rejected;return r;}
        auto name=url.substr(url.find_last_of(L'/')+1);std::ifstream in(std::filesystem::path(dir)/name,std::ios::binary);
        if(!in){r.status=UpdateCore::HttpResult::NotFound;r.httpStatus=404;return r;}
        in.seekg(0,std::ios::end);std::uint64_t size=(std::uint64_t)in.tellg();in.seekg(0);
        if(size>maxBytes){r.status=UpdateCore::HttpResult::TooLarge;return r;}
        if(onLength&&!onLength(size)){r.status=UpdateCore::HttpResult::Rejected;return r;}
        std::vector<char> buffer(64*1024);
        while(in){
            if(cancel||aborted){r.status=UpdateCore::HttpResult::Cancelled;return r;}
            in.read(buffer.data(),buffer.size());auto n=(std::size_t)in.gcount();if(!n)break;
            if(!sink(buffer.data(),n)){r.status=UpdateCore::HttpResult::Cancelled;return r;}
            if(chunkDelayMs)Sleep(chunkDelayMs);
        }
        r.status=UpdateCore::HttpResult::Ok;r.httpStatus=200;return r;
    }
    void Abort() override{aborted=true;}
};

// Owner window: forwards exactly the host branches under test to the production WndProc.
LRESULT CALLBACK OwnerProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==UpdateMessage||msg==WM_CLOSE||msg==WM_DESTROY||(msg==WM_SIZE&&w==SIZE_MINIMIZED)||(msg==WM_TIMER&&w==UpdateTimerId))return WndProc(h,msg,w,l);
    return DefWindowProcW(h,msg,w,l);
}
}

int wmain(int argc,wchar_t** argv){
    if(argc<6){std::fprintf(stderr,"usage: host_e2e <install|cancel|offline|norelease|periodic|relaunch> <root> <serveDir> <version|expect-forward|expect-stay> <report>\n");return 64;}
    const std::wstring mode=argv[1],root=argv[2],serve=argv[3],version=argv[4];reportPath=argv[5];
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 70;
    // Same path resolution as wWinMain, against the temp install root.
    auto exe=ExePath();base=exe.substr(0,exe.find_last_of(L'\\'));ResolveBrowserPaths(root);
    Note("mode "+Utf8(mode));Note("exe "+Utf8(exe));Note("dataDir "+Utf8(dataDir));Note("sessionFile "+Utf8(sessionFile));Note("profileDir "+Utf8(profileDir));Note("downloadsFile "+Utf8(downloadsFile));
    Expect(Utf8(BROWSER_UPDATE_REPO_WIDE)=="VALXXII/browser-releases","production feed repository is the public releases repo");
    if(mode==L"relaunch"){
        // The legacy exe (old Start Menu shortcut target) started again: the production early hand-over,
        // called exactly as wWinMain does, before any window, profile or WebView2.
        const bool expect=version==L"expect-forward";
        const auto target=UpdateCore::PortableForwardTarget(exe,root);Note("forward target "+Utf8(target));
        const bool forwarded=ForwardToInstalled(root);
        Expect(forwarded==expect,expect?"legacy exe handed over to the installed current version":"legacy exe kept running portable (no hand-over)");
        Note(failures?"RESULT FAIL":"RESULT PASS");Flush();CoUninitialize();return failures?1:0;
    }

    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=OwnerProc;wc.lpszClassName=L"HostE2EOwner";RegisterClassW(&wc);
    window=CreateWindowExW(0,wc.lpszClassName,L"host_e2e (native fixture, no WebView2)",WS_OVERLAPPEDWINDOW,-30000,-30000,900,600,nullptr,nullptr,instance,nullptr);
    if(!window)return 71;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    // Tabs as the browser holds them after RestoreSession (no WebView2 controllers).
    for(auto uri:{L"https://example.com/a",L"https://example.org/b?q=canary"}){auto t=std::make_shared<Tab>();t->uri=uri;t->home=false;pages.push_back(t);}
    {auto t=std::make_shared<Tab>();t->home=true;pages.push_back(t);}
    current=1;sessionReady=true;
    LoadDownloads();const size_t historyBefore=downloads.size();Note("history rows loaded "+std::to_string(historyBefore));
    {DL::Item item;item.id=nextDownloadId++;item.path=dataDir+L"\\canary-download.bin";item.uri=L"https://example.com/canary-download.bin";
     item.state=DL::State::Completed;item.received=item.total=4096;item.started=1790000000000LL;downloads.insert(downloads.begin(),item);}
    // Real download surfaces (popover + drawer) beside the toast, as in the browser.
    CreateDownloadPanel();

    auto transport=std::make_shared<DiskTransport>();transport->dir=mode==L"periodic"?serve+L"\\not-yet-published":serve;transport->chunkDelayMs=mode==L"cancel"?40:4;transport->offline=mode==L"offline";const bool quietMode=mode==L"offline"||mode==L"norelease";
    UpdateCore::Config config=UpdateConfig();
    config.currentVersion=version;config.installRoot=root;config.transport=transport;config.startupDelayMs=quietMode?0:50;
    config.enabled=true;
    if(mode==L"periodic")config.clock=[]{return mockNow.load();};
    StartUpdates(config);
    Expect(updatesStarted,"engine started from the host");
    Expect(updateToast.Window()!=nullptr,"toast popup created (owned layered popup)");
    if(updateToast.Window()){
        Expect(GetWindow(updateToast.Window(),GW_OWNER)==window,"toast is owned by the browser window (above WebView2 children)");
        Expect((GetWindowLongPtrW(updateToast.Window(),GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0,"toast never activates (no focus stealing)");
    }
    Expect(!updates.OnNotify((WPARAM)0x1234560),"stale/foreign notification generation is refused");

    if(mode==L"periodic"){
        using P=BrowserUpdate::Phase;
        auto pump=[&](auto done,DWORD ms){auto end=GetTickCount64()+ms;MSG m;for(;;){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(done())return true;if(GetTickCount64()>end)return false;Sleep(5);}};
        const DWORD hour=3600u*1000u;
        Expect(pump([&]{return updates.LastCheckResult()==UpdateCore::Engine::LastCheck::UpToDate;},5000),"startup check: no release published yet, quiet");
        pump([]{return false;},200);
        Expect(updates.View().phase==P::Hidden,"nothing shown after the startup check");
        Expect(updateTimerDelayMs==6*hour,"ONE one-shot timer armed for the 6 h TTL (armed "+std::to_string(updateTimerDelayMs)+" ms), no idle polling");
        const int r0=transport->requests;
        mockNow+=3600;PostMessageW(window,WM_TIMER,UpdateTimerId,0);pump([]{return false;},300);
        Expect(transport->requests==r0,"timer firing before the TTL makes no request");
        Expect(updateTimerDelayMs==5*hour,"re-armed for the remaining 5 h");
        // A merge is published while the browser keeps running.
        transport->dir=serve;mockNow+=5*3600+1;PostMessageW(window,WM_TIMER,UpdateTimerId,0);
        Expect(pump([&]{return updates.View().phase==P::Available;},10000),"running browser finds the newer release without restart");
        Expect(updates.View().version==L"0.3.6","offer is the published version");
        Expect(pump([&]{return updateToast.Shown();},5000),"toast offers it");
        Expect(transport->requests==r0+1,"exactly one manifest request for the due check");
        Expect(updateTimerDelayMs>=15*60*1000u&&updateTimerDelayMs<=6*hour,"timer re-armed with a bounded delay while the offer is visible ("+std::to_string(updateTimerDelayMs)+" ms)");
        const int r1=transport->requests;mockNow+=7*3600;PostMessageW(window,WM_TIMER,UpdateTimerId,0);pump([]{return false;},300);
        Expect(transport->requests==r1&&updates.View().phase==P::Available,"no automatic check while an offer is on screen");
        PostMessageW(window,WM_CLOSE,0,0);
    }
    int coexistFrames=0;
    std::vector<std::string> phases;bool drawerChecked=false,coexist=false,toastSeen=false,sawManualCheck=false,noReleaseDone=false,downloaded=false,installAsked=false,cancelled=false,dismissed=false,rechecked=false,sawProgress=false,manualOffline=false;
    BrowserUpdate::View last;auto started=GetTickCount64();
    SetTimer(window,99,quietMode?3000:60000,nullptr); // watchdog / offline settle time
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.hwnd==window&&msg.message==WM_TIMER&&msg.wParam==99){
            KillTimer(window,99);
            if(quietMode&&!manualOffline){
                Expect(updates.View().phase==BrowserUpdate::Phase::Hidden,mode==L"offline"?"offline startup check stays silent (Hidden)":"startup check without a published release stays silent (Hidden)");
                Expect(transport->requests>0,"startup check actually ran");
                manualOffline=true;Command(CheckUpdatesCommand);SetTimer(window,99,15000,nullptr);continue;
            }
            Expect(false,"watchdog: flow did not finish in time");PostMessageW(window,WM_CLOSE,0,0);continue;
        }
        TranslateMessage(&msg);DispatchMessageW(&msg);
        if(!updatesStarted)continue;
        auto v=updates.View();
        if(updateToast.Shown()&&v.phase!=BrowserUpdate::Phase::Hidden)toastSeen=true;
        if(v.phase!=last.phase){phases.push_back(PhaseName(v.phase));Note(std::string("phase ")+PhaseName(v.phase)+" "+Utf8(v.version)+(v.error.empty()?"":" error="+Utf8(v.error)));}
        if(v.phase==BrowserUpdate::Phase::Downloading&&v.received>0&&v.total>0){if(!sawProgress)Note("progress "+std::to_string(v.received)+"/"+std::to_string(v.total));sawProgress=true;}
        last=v;
        using P=BrowserUpdate::Phase;
        if(mode==L"install"){
            if(v.phase==P::Available&&!drawerChecked&&updateToast.Shown()&&downloadDrawer.Window()){drawerChecked=true;
                ShowDownloadDrawer();coexist=true;}
            else if(coexist&&!downloaded&&updateToast.Shown()&&downloadDrawer.Visible()&&IsWindowVisible(downloadDrawer.Window())){
                RECT toast{},drawer{},overlap{};GetWindowRect(updateToast.Window(),&toast);GetWindowRect(downloadDrawer.Window(),&drawer);
                if(++coexistFrames<30&&IntersectRect(&overlap,&toast,&drawer))continue; // let both settle (spring/slide)
                Expect(!IntersectRect(&overlap,&toast,&drawer),"toast card and open download drawer do not overlap (SetReservedRight)");
                Expect(GetWindow(updateToast.Window(),GW_OWNER)==window&&GetWindow(downloadDrawer.Window(),GW_OWNER)==window,"drawer and toast share the browser window as owner");
                CloseDownloadSurfaces(false);Expect(!downloadDrawer.Visible(),"drawer closed, toast still offered");
                downloaded=true;Expect(true,"Available card presented on screen by the toast before acting");UpdateAction(BrowserUpdate::Action::Download);}
            else if(v.phase==P::Ready&&!installAsked&&updateToast.Shown()){installAsked=true;UpdateAction(BrowserUpdate::Action::RestartInstall);
                Expect(closingForInstall,"RestartInstall accepted: helper launched, host closing");
                Expect(!UpdateCheckAvailable(),"manual check disabled while closing for install");}
            else if(v.phase==P::Error){Expect(false,"unexpected Error");PostMessageW(window,WM_CLOSE,0,0);}
        }else if(mode==L"cancel"){
            if(v.phase==P::Available&&!downloaded){downloaded=true;UpdateAction(BrowserUpdate::Action::Download);}
            else if(v.phase==P::Downloading&&v.received>0&&!cancelled){cancelled=true;UpdateAction(BrowserUpdate::Action::Cancel);
                auto after=updates.View();Expect(after.phase==P::Available,"Cancel returns to Available immediately");
                std::error_code ec;size_t partial=0;for(auto& e:std::filesystem::directory_iterator(std::filesystem::path(root)/L"updates"/L"downloads",ec)){(void)e;++partial;}
                Expect(partial==0,"partial download deleted on cancel");}
            else if(cancelled&&!dismissed&&v.phase==P::Available){dismissed=true;UpdateAction(BrowserUpdate::Action::Dismiss);
                Expect(updates.View().phase==P::Hidden,"Dismiss hides the card");
                rechecked=true;Command(CheckUpdatesCommand);}
            else if(rechecked&&dismissed&&v.phase==P::Available){
                Expect(true,"manual check from the menu command shows the dismissed version again");
                auto t0=GetTickCount64();UpdateAction(BrowserUpdate::Action::Download);
                // close while a slow download is in flight: shutdown must be bounded
                PostMessageW(window,WM_CLOSE,0,0);rechecked=false;Note("closing during download t="+std::to_string(GetTickCount64()-t0));
            }
        }else if(mode==L"offline"){
            if(manualOffline&&v.phase==P::Error){Expect(true,"manual check reports the failure (Error with Retry)");PostMessageW(window,WM_CLOSE,0,0);}
        }else if(mode==L"norelease"){
            if(manualOffline&&v.phase==P::Checking)sawManualCheck=true;
            if(manualOffline&&sawManualCheck&&v.phase==P::Hidden&&!noReleaseDone){noReleaseDone=true;
                Expect(updates.LastCheckResult()==UpdateCore::Engine::LastCheck::UpToDate,"manual check with no published release ends up-to-date (no card)");PostMessageW(window,WM_CLOSE,0,0);}
        }
    }
    const auto elapsed=GetTickCount64()-started;
    Note("elapsed_ms "+std::to_string(elapsed));
    Expect(!updatesStarted&&updateToast.Window()==nullptr,"WM_DESTROY stopped the engine and closed the toast");
    Expect(updateTimerDelayMs==0,"periodic check timer cancelled on close");
    // The session the browser wrote on close (WM_CLOSE -> SaveSession -> atomic replace).
    std::ifstream in(std::filesystem::path(sessionFile),std::ios::binary);auto saved=BrowserSession::Read(in);
    Expect(saved.urls.size()==3&&saved.urls[0]=="https://example.com/a"&&saved.urls[1]=="https://example.org/b?q=canary"&&saved.urls[2].empty()&&saved.active==1,"session saved to the resolved session file before exit");
    Expect(!std::filesystem::exists(std::filesystem::path(sessionFile+L".tmp")),"no session temp file left");
    {std::ifstream hin(std::filesystem::path(downloadsFile),std::ios::binary);int next=1;auto saved=DL::ReadHistory(hin,next);
     bool found=false;for(auto& d:saved)found=found||d.uri==L"https://example.com/canary-download.bin";
     Expect(found&&saved.size()==historyBefore+1,"download history saved to the resolved downloads file on close (existing rows kept)");}
    Expect(!std::filesystem::exists(std::filesystem::path(downloadsFile+L".tmp")),"no downloads temp file left");
    if(mode==L"install")Expect(drawerChecked&&coexist,"drawer/toast coexistence exercised");
    downloadPanel.Dispose();downloadDrawer.Dispose();
    if(mode==L"install")Expect(installAsked&&sawProgress,"real byte progress observed before Ready");
    if(mode==L"install"||mode==L"cancel")Expect(toastSeen,"toast card was presented while an update was offered");
    if(mode==L"cancel")Expect(cancelled&&dismissed,"cancel and dismiss exercised");
    if(quietMode)Expect(manualOffline,"manual check exercised");
    std::string seq;for(auto& p:phases)seq+=p+" ";Note("phases "+seq);
    Expect(transport->rejected==0,"engine only requested allowlisted HTTPS feed URLs");
    Note(failures?"RESULT FAIL":"RESULT PASS");Flush();
    CoUninitialize();
    return failures?1:0;
}
