// Per-site audio, host side (included by main.cpp after elements-integration.h).
// Model: audio-mixer.h · page script: audio-script.h · popover: audio-panel.h.
//
//  * Every tab: the script is injected into every document and frame (re-injected with
//    the new starting gains when a volume changes), and ICoreWebView2_8's audible / muted
//    events feed the tab's Audio::TabAudio. Page reports arrive as web messages.
//  * A volume change goes to every live tab of that site at once (web message, then the
//    page relays it into its frames); the store is written when the gesture ends.
//  * Toolbar: the 'V' button exists while a site has made sound; its icon animates only
//    while music plays (debounced), otherwise it is the static, subdued speaker.
//  * Artwork (Media Session): fetched from its https URL by a worker thread (WinHTTP, no
//    cookies, 2 MB / 5 s caps), decoded and scaled to 96 px by WIC, cached (32 URLs).
#include <thread>
#include <winhttp.h>
#include "audio-panel.h"
#include "audio-script.h"

GUID_TEXT(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,"{b99369f3-9b11-47b5-bc6f-8e7895fcea17}")
GUID_TEXT(ICoreWebView2IsDocumentPlayingAudioChangedEventHandler,"{5def109a-2f4b-49fa-b7f6-11c39e513328}")
GUID_TEXT(ICoreWebView2IsMutedChangedEventHandler,"{57d90347-cd0e-4952-a4a2-7483a2756f08}")

namespace AU=NativeChrome::Audio;
AU::Volumes audioVolumes; AU::Toolbar audioToolbar; NativeChrome::AudioPanel::Popover audioPanel;
bool audioDismissClick=false,audioScriptDirty=false;
constexpr UINT_PTR AudioTimerId=97,AudioSaveTimerId=98; constexpr UINT AudioArtMessage=WM_APP+31;
std::map<std::wstring,std::shared_ptr<const NativeChrome::FaviconImage>> audioArt;std::set<std::wstring> audioArtPending,audioArtFailed;std::vector<std::wstring> audioArtOrder;

std::wstring AudioFile(){return dataDir+L"\\site-volumes";}
void LoadAudioVolumes(){if(smoke)return;ProfileStore::Lock lock(dataDir);std::ifstream in(std::filesystem::path(AudioFile()),std::ios::binary);audioVolumes.Read(in);}
void SaveAudioVolumes(){
    if(smoke||privateWindow)return; // a private window keeps its volumes in memory only
    ProfileStore::Lock lock(dataDir);if(!lock.Held()){Log(L"AUDIO volumes save skipped: profile busy");return;} // never write without the lock
    const auto path=std::filesystem::path(AudioFile()),temp=std::filesystem::path(AudioFile()+L".tmp");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);audioVolumes.Write(out);out.close();if(!out){Log(L"AUDIO volumes save failed");return;}}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))Log(L"AUDIO volumes replace failed");
}

// ---- Artwork --------------------------------------------------------------------------------
struct AudioArtResult{std::wstring url;std::shared_ptr<NativeChrome::FaviconImage> image;};
// winhttp.dll bound at run time (like accessibility.h): no new link dependency for the
// browser's native fixtures that compile main.cpp.
struct AudioHttp {
    decltype(&::WinHttpCrackUrl) crack=nullptr;decltype(&::WinHttpOpen) open=nullptr;decltype(&::WinHttpSetTimeouts) timeouts=nullptr;
    decltype(&::WinHttpConnect) connect=nullptr;decltype(&::WinHttpOpenRequest) request=nullptr;decltype(&::WinHttpSetOption) option=nullptr;
    decltype(&::WinHttpSendRequest) send=nullptr;decltype(&::WinHttpReceiveResponse) receive=nullptr;decltype(&::WinHttpQueryHeaders) headers=nullptr;
    decltype(&::WinHttpQueryDataAvailable) available=nullptr;decltype(&::WinHttpReadData) read=nullptr;decltype(&::WinHttpCloseHandle) close=nullptr;
    bool Ready()const{return crack&&open&&timeouts&&connect&&request&&option&&send&&receive&&headers&&available&&read&&close;}
    static const AudioHttp& Get(){static const AudioHttp api=[]{AudioHttp a;HMODULE m=LoadLibraryExW(L"winhttp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!m)return a;
        auto bind=[m](auto& f,const char* name){FARPROC p=GetProcAddress(m,name);static_assert(sizeof(f)==sizeof(p),"function pointer size");memcpy(&f,&p,sizeof(p));};
        bind(a.crack,"WinHttpCrackUrl");bind(a.open,"WinHttpOpen");bind(a.timeouts,"WinHttpSetTimeouts");bind(a.connect,"WinHttpConnect");bind(a.request,"WinHttpOpenRequest");bind(a.option,"WinHttpSetOption");
        bind(a.send,"WinHttpSendRequest");bind(a.receive,"WinHttpReceiveResponse");bind(a.headers,"WinHttpQueryHeaders");bind(a.available,"WinHttpQueryDataAvailable");bind(a.read,"WinHttpReadData");bind(a.close,"WinHttpCloseHandle");return a;}();return api;}
};
std::shared_ptr<NativeChrome::FaviconImage> DownloadArtwork(const std::wstring& url){
    const auto& H=AudioHttp::Get();if(!H.Ready())return nullptr;
    URL_COMPONENTS parts{sizeof(parts)};wchar_t host[256]{},path[2048]{};parts.lpszHostName=host;parts.dwHostNameLength=255;parts.lpszUrlPath=path;parts.dwUrlPathLength=2047;
    wchar_t extra[2048]{};parts.lpszExtraInfo=extra;parts.dwExtraInfoLength=2047;
    if(!H.crack(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)return nullptr;
    std::vector<unsigned char> bytes;
    HINTERNET session=H.open(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) browser-artwork",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return nullptr;
    H.timeouts(session,5000,5000,5000,5000);
    HINTERNET connect=H.connect(session,host,parts.nPort,0);
    HINTERNET request=connect?H.request(connect,L"GET",(std::wstring(path)+extra).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    if(request){DWORD noCookies=WINHTTP_DISABLE_COOKIES;H.option(request,WINHTTP_OPTION_DISABLE_FEATURE,&noCookies,sizeof(noCookies));}
    DWORD status=0,size=sizeof(status);
    if(request&&H.send(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&H.receive(request,nullptr)&&
       H.headers(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)&&status==200){
        DWORD available=0;while(H.available(request,&available)&&available){if(bytes.size()+available>2*1024*1024){bytes.clear();break;}const size_t at=bytes.size();bytes.resize(at+available);DWORD read=0;if(!H.read(request,bytes.data()+at,available,&read)){bytes.clear();break;}bytes.resize(at+read);}
    }
    if(request)H.close(request);if(connect)H.close(connect);H.close(session);
    if(bytes.empty())return nullptr;
    std::shared_ptr<NativeChrome::FaviconImage> image;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return nullptr;
    {IWICImagingFactory* wic=nullptr;IWICStream* stream=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICBitmapScaler* scaler=nullptr;IWICFormatConverter* converter=nullptr;UINT w=0,h=0;
        HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
        if(SUCCEEDED(hr))hr=wic->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromMemory(bytes.data(),(DWORD)bytes.size());
        if(SUCCEEDED(hr))hr=wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);
        if(SUCCEEDED(hr)&&(!w||!h||w>8192||h>8192))hr=E_INVALIDARG;
        // 96 px on the short side: the 40 DIP tile stays sharp up to 240 % scaling.
        const double k=SUCCEEDED(hr)?std::min(1.0,96.0/std::min(w,h)):1;const UINT sw=std::max(1u,(UINT)std::lround(w*k)),sh=std::max(1u,(UINT)std::lround(h*k));
        if(SUCCEEDED(hr))hr=wic->CreateBitmapScaler(&scaler);if(SUCCEEDED(hr))hr=scaler->Initialize(frame,sw,sh,WICBitmapInterpolationModeFant);
        if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&converter);if(SUCCEEDED(hr))hr=converter->Initialize(scaler,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
        if(SUCCEEDED(hr)){image=std::make_shared<NativeChrome::FaviconImage>();image->width=sw;image->height=sh;image->stride=sw*4;image->pixels.resize(image->stride*sh);hr=converter->CopyPixels(nullptr,image->stride,(UINT)image->pixels.size(),image->pixels.data());if(FAILED(hr))image.reset();}
        for(IUnknown* p:std::initializer_list<IUnknown*>{converter,scaler,frame,decoder,stream,wic})if(p)p->Release();}
    CoUninitialize();
    return image;
}
std::shared_ptr<const NativeChrome::FaviconImage> AudioArtwork(const std::wstring& url){
    if(url.empty())return nullptr;auto it=audioArt.find(url);if(it!=audioArt.end())return it->second;
    if(audioArtPending.count(url)||audioArtFailed.count(url)||audioArtPending.size()>=4||smoke)return nullptr;
    audioArtPending.insert(url);HWND target=window;
    std::thread([url,target]{auto* r=new AudioArtResult{url,DownloadArtwork(url)};if(!PostMessageW(target,AudioArtMessage,0,(LPARAM)r))delete r;}).detach();
    return nullptr;
}
void RefreshAudio();
void AudioArtArrived(LPARAM l){
    std::unique_ptr<AudioArtResult> r((AudioArtResult*)l);if(!r)return;audioArtPending.erase(r->url);
    if(!r->image){audioArtFailed.insert(r->url);Log(L"AUDIO artwork unavailable "+r->url);return;}
    audioArt[r->url]=r->image;audioArtOrder.push_back(r->url);
    while(audioArtOrder.size()>32){audioArt.erase(audioArtOrder.front());audioArtOrder.erase(audioArtOrder.begin());}
    RefreshAudio();
}

// ---- Model -> chrome / panel ---------------------------------------------------------------
std::vector<AU::Row> AudioRows(NativeChrome::Motion::Time now){
    std::vector<AU::TabView> views;
    for(int i=0;i<(int)pages.size();++i){auto& t=pages[i];views.push_back({t->closed?nullptr:&t->audio,t->title,i==current});}
    return AU::Rows(views,audioVolumes,now);
}
std::vector<NativeChrome::AudioPanel::Item> AudioItems(const std::vector<AU::Row>& rows){
    std::vector<NativeChrome::AudioPanel::Item> items;
    for(auto& r:rows){NativeChrome::AudioPanel::Item item;item.row=r;
        if(r.tab>=0&&r.tab<(int)pages.size())item.favicon=pages[r.tab]->favicon.image;if(!item.favicon)item.favicon=SiteIcon(r.site);
        item.artwork=AudioArtwork(r.artwork);items.push_back(std::move(item));}
    return items;
}
void InvalidateAudioIcon(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */if(!chrome.audioShown)return;auto d=chrome.AudioIconDirty();RECT r{S(d.left),S(d.top),S(d.right),S(d.bottom)};InvalidateRect(window,&r,FALSE);}
// Housekeeping deadlines (sustained sound, the music hold, forgetting quiet entries).
void ArmAudioTimer(NativeChrome::Motion::Time now){
    NativeChrome::Motion::Wake w=audioToolbar.NextWake(now);bool listed=false;
    for(auto& t:pages){if(t->closed)continue;w=NativeChrome::Motion::Earlier(w,t->audio.NextWake(now));listed=listed||t->audio.heard;}
    if(listed)w=NativeChrome::Motion::Earlier(w,{NativeChrome::Motion::Milliseconds(60000),false}); // forget check
    KillTimer(window,AudioTimerId);if(w.delay&&!halted)SetTimer(window,AudioTimerId,(UINT)std::max<long long>(10,w.delay->count()),nullptr);
}
void RefreshAudio(){if(WorkspaceParked())return; /* a hidden profile's event: the window shows another profile */
    if(halted||!window)return;
    const auto now=NativeChrome::Motion::Now();bool music=false;
    for(auto& t:pages){if(t->closed)continue;auto& a=t->audio;
        if(a.heard&&a.Forgettable(now,audioVolumes.Custom(a.site)))a.heard=false;
        if(a.At(now)==AU::Kind::Media)music=true;}
    audioToolbar.Update(music,now);
    const auto rows=AudioRows(now);
    const bool shown=!rows.empty()||audioPanel.Visible();
    if(shown!=chrome.audioShown){chrome.audioShown=shown;chrome.audioIcon.Show(shown,now);if(!shown&&keyboardFocus=='V')keyboardFocus='Y';Layout();SyncAccessibility();
        Log(shown?L"AUDIO toolbar button shown":L"AUDIO toolbar button hidden");}
    const bool wasMusic=chrome.audioIcon.Music();
    chrome.audioIcon.Update(audioToolbar.Music(),now,motion.ReducedMotion());
    if(wasMusic!=chrome.audioIcon.Music())Log(audioToolbar.Music()?L"AUDIO music playing":L"AUDIO music stopped");
    if(audioPanel.Visible())audioPanel.SetItems(AudioItems(rows));
    InvalidateAudioIcon();ScheduleMotion(now);ArmAudioTimer(now);
}
// Icon frames: the button only (called from InvalidateMotion).
void AudioFrame(NativeChrome::Motion::Time now){
    // A reduced-motion switch while the meter loops lands here first: the icon goes static at once.
    chrome.audioIcon.Update(audioToolbar.Music(),now,motion.ReducedMotion());chrome.audioIcon.Settle(now);
    static bool painting=false;const bool moving=chrome.audioIcon.NextWake(now).delay.has_value();if(moving||painting)InvalidateAudioIcon();painting=moving;
}
NativeChrome::Motion::Wake AudioNextWake(NativeChrome::Motion::Time now){return chrome.audioShown?chrome.audioIcon.NextWake(now):NativeChrome::Motion::Wake{};}

// A window.open target's initial document script (scrollbar, vault): one readiness step each (popup-target.h).
// Unowned: it must settle the request even after its profile left the window. nullptr for an ordinary tab.
ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* PopupScriptBarrier(const PopupHost::RequestPtr& popup,const wchar_t* what){
    if(!popup||popup->terminal)return nullptr;
    unownedCallbacks=true;
    auto* h=Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,HRESULT,LPCWSTR>([popup,what=std::wstring(what)](HRESULT hr,LPCWSTR)->HRESULT{
        Workspace* owner=static_cast<Workspace*>(const_cast<void*>(popup->owner));
        if(popup->terminal||WorkspaceDead(owner))return S_OK;
        WorkspaceScope scope(owner);if(PopupQa()==L"script-fail")hr=E_FAIL;if(FAILED(hr))Log(L"POPUP initial script failed: "+what);popups.Step(popup,SUCCEEDED(hr));return S_OK;});
    unownedCallbacks=false;PopupHost::Registry::Arm(popup,1);return h;
}
// ---- Gains -----------------------------------------------------------------------------------
void InjectAudioScript(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup){
    if(!t||t->closed||!t->view.p)return;
    if(!popup&&t->popupSetup){t->audioReinject=true;return;}   // a window.open target being set up: no removal now, re-injected once assigned
    if(!t->audioScript.empty()){t->view->RemoveScriptToExecuteOnDocumentCreated(t->audioScript.c_str());t->audioScript.clear();}
    std::weak_ptr<Tab> weak=t;const unsigned generation=++t->audioScriptGeneration;
    if(popup)unownedCallbacks=true;   // part of a window.open target's readiness: must settle it even after its profile left
    auto done=Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,HRESULT,LPCWSTR>([weak,generation,popup](HRESULT hr,LPCWSTR id)->HRESULT{
        Workspace* popupOwner=popup?static_cast<Workspace*>(const_cast<void*>(popup->owner)):nullptr;
        if(popup&&(popup->terminal||WorkspaceDead(popupOwner)))return S_OK;
        std::optional<WorkspaceScope> popupScope;if(popup)popupScope.emplace(popupOwner);
        auto tab=weak.lock();if(!tab||tab->closed||!tab->view.p){if(popup)popups.Finish(popup,false);return S_OK;}
        if(FAILED(hr)||!id){Log(L"AUDIO script injection failed");if(popup)popups.Step(popup,false);return S_OK;}
        if(generation!=tab->audioScriptGeneration){tab->view->RemoveScriptToExecuteOnDocumentCreated(id);return S_OK;} // a newer table won
        tab->audioScript=id;if(popup)popups.Step(popup,true);return S_OK;});
    if(popup){unownedCallbacks=false;PopupHost::Registry::Arm(popup,1);}
    if(FAILED(t->view->AddScriptToExecuteOnDocumentCreated(AU::Script(audioVolumes).c_str(),done))){Log(L"AUDIO AddScriptToExecuteOnDocumentCreated failed");if(popup)popups.Finish(popup,false);}
    done->Release();
}
void PostAudioGain(const std::shared_ptr<Tab>& t){
    if(!t||t->closed||!t->view.p)return;const auto site=AU::SiteKey(t->uri);if(site.empty())return;
    t->view->PostWebMessageAsJson(AU::GainMessage(audioVolumes.Get(site)).c_str());
}
// The panel moved a slider (every pointer move) or a gesture ended (commit).
void AudioVolumeChanged(const std::wstring& site,int percent,bool commit){
    const bool changed=audioVolumes.Set(site,percent);
    if(changed)for(auto& t:pages)if(!t->closed&&AU::SiteKey(t->uri)==site)PostAudioGain(t);
    if(changed)audioScriptDirty=true;
    // Persisting and re-injecting the starting table wait for the end of the gesture (and 400 ms of rest).
    if(commit&&audioScriptDirty){KillTimer(window,AudioSaveTimerId);SetTimer(window,AudioSaveTimerId,400,nullptr);}
    if(changed)Log(L"AUDIO volume "+site+L" "+std::to_wstring(audioVolumes.Get(site))+L"%");
}
void AudioSaveDue(){
    KillTimer(window,AudioSaveTimerId);if(!audioScriptDirty)return;audioScriptDirty=false;
    SaveAudioVolumes();for(auto& t:pages)InjectAudioScript(t,nullptr);
}

// ---- Engine events ---------------------------------------------------------------------------
bool AudioPageMessage(const std::shared_ptr<Tab>& t,const std::wstring& message){
    if(!AU::IsReport(message))return false;
    if(!t||t->closed)return true;
    AU::Report r;if(!AU::ParseReport(message,r))return true;
    const auto site=AU::SiteKey(t->uri);if(site.empty())return true;
    t->audio.SiteChanged(site,NativeChrome::Motion::Now());
    // First report of a document: make sure it plays at the site's current level.
    if(message.find(L"\"h\":1")!=std::wstring::npos&&audioVolumes.Custom(site))PostAudioGain(t);
    if(r!=t->audio.report){t->audio.report=r;RefreshAudio();}
    return true;
}
void AudioSiteChanged(const std::shared_ptr<Tab>& t){
    if(!t||t->closed)return;const auto site=AU::SiteKey(t->uri);
    if(site!=t->audio.site){t->audio.SiteChanged(site,NativeChrome::Motion::Now());RefreshAudio();}
}
void AudioAudibleChanged(const std::shared_ptr<Tab>& t){
    if(!t||t->closed||!t->view.p)return;Com<ICoreWebView2_8> v8;BOOL audible=FALSE,muted=FALSE;
    if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_8,(void**)&v8.p))||!v8.p)return;
    v8->get_IsDocumentPlayingAudio(&audible);v8->get_IsMuted(&muted);
    const auto now=NativeChrome::Motion::Now();t->audio.SiteChanged(AU::SiteKey(t->uri),now);t->audio.muted=muted!=FALSE;
    if(t->audio.site.empty())return; // only web sites are listed (their volume is per site)
    if(t->audio.SetAudible(audible!=FALSE,now)){Log(std::wstring(L"AUDIO ")+(audible?L"audible ":L"silent ")+t->audio.site);RefreshAudio();}
}
void RegisterAudio(const std::shared_ptr<Tab>& t,const PopupHost::RequestPtr& popup){
    InjectAudioScript(t,popup);
    std::weak_ptr<Tab> weak=t;EventRegistrationToken token{};Com<ICoreWebView2_8> v8;
    if(FAILED(t->view->QueryInterface(IID_ICoreWebView2_8,(void**)&v8.p))||!v8.p){Log(L"AUDIO ICoreWebView2_8 unavailable: sites are not listed");return;}
    auto audible=Callback<ICoreWebView2IsDocumentPlayingAudioChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{if(!halted)AudioAudibleChanged(weak.lock());return S_OK;});
    if(FAILED(v8->add_IsDocumentPlayingAudioChanged(audible,&token)))Log(L"AUDIO add_IsDocumentPlayingAudioChanged failed");audible->Release();
    auto muted=Callback<ICoreWebView2IsMutedChangedEventHandler,ICoreWebView2*,IUnknown*>([weak](ICoreWebView2*,IUnknown*)->HRESULT{if(!halted)AudioAudibleChanged(weak.lock());return S_OK;});
    if(FAILED(v8->add_IsMutedChanged(muted,&token)))Log(L"AUDIO add_IsMutedChanged failed");muted->Release();
}

// ---- Toolbar button and panel ----------------------------------------------------------------
void ShowAudio(bool keyboard){
    if(!audioPanel.Window()||!chrome.audioShown)return;
    const auto* r=[]()->const NativeChrome::Region*{for(auto& x:chrome.regions)if(x.id=='V')return &x;return nullptr;}();if(!r)return;
    audioPanel.SetItems(AudioItems(AudioRows(NativeChrome::Motion::Now())));
    RECT anchor{S(r->rect.left),S(r->rect.top),S(r->rect.right),S(r->rect.bottom)};MapWindowPoints(window,nullptr,(POINT*)&anchor,2);
    RECT bounds{};GetClientRect(window,&bounds);MapWindowPoints(window,nullptr,(POINT*)&bounds,2);
    chrome.latched='V';InvalidateChrome();audioPanel.Show(anchor,bounds,keyboard);
    Log(L"AUDIO panel opened rows="+std::to_wstring(audioPanel.Items().size()));
}
// Docked drawers share the window's right strip: one at a time.
void CloseAudioDrawer(){if(audioPanel.Docked())audioPanel.Close(false);}
void CloseOtherDrawers(){HideSuggestions();CloseDownloadSurfaces();settingsDrawer.Hide(false,true);historyDrawer.Hide(false,true);bookmarksDrawer.Hide(false,true);SyncUpdateToastReserve();}
void ToggleAudio(bool keyboard){if(audioPanel.Visible())audioPanel.Close(keyboard);else ShowAudio(keyboard);}
void CreateAudioPanel(){
    NativeChrome::AudioPanel::Hooks hooks;
    hooks.volume=[](const std::wstring& site,int percent,bool commit){AudioVolumeChanged(site,percent,commit);};
    hooks.show=[](int tab){if(tab>=0&&tab<(int)pages.size())Select(tab);};
    // « Voir tout »: the drawer takes the window's right strip, from the chrome's lower edge down.
    hooks.drawerOpening=[]{CloseOtherDrawers();};
    hooks.drawerArea=[]()->RECT{RECT r{};GetClientRect(window,&r);r.top=S(chrome.ContentTop());MapWindowPoints(window,nullptr,(POINT*)&r,2);return r;};
    hooks.closed=[](bool keyboard){
        SyncLatch();
        // Keyboard: back to the button that opened it (it may have gone if the last site left).
        if(keyboard&&!halted){SetFocus(window);keyboardFocus=chrome.audioShown?'V':'Y';keyboardModality=true;InvalidateChrome();SyncAccessibility();}
        RefreshAudio();};
    if(!audioPanel.Create(instance,window,base,std::move(hooks)))Log(L"AUDIO panel unavailable");
}
