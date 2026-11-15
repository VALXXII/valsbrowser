// Folder rename in place in the real chrome (design/bookmark-inline-rename-plan.md): the production window
// procedure, store, band, drawer, context menu and popover run on this thread with demo data only (never a
// personal profile). Driven by window messages on our own windows and posted keys (the context menu's
// modal loop reads them); no SendInput, no foreground change, no clipboard.
//  1. Band: F2, menu « Renommer… », band « Nouveau dossier » -> editor on the folder's title, not the popover.
//  2. Enter / Escape / empty / blur / Tab, store + one undo step, keys kept from app shortcuts.
//  3. Lifecycle: reorder moves it, 560 DIP relayout, deleted by « another window », profile change -> cancel.
//  4. Drawer row: F2 and menu, follows the scroll, commit.
//  5. Fallbacks: sub-folder in a native menu and links keep the popover; root folders never rename.
//  6. Persistence (written then read back) and a failed save never announced as saved.
//  7. Captures: the real window (system DPI), and the real editor over the offscreen band at 96 / 144 DPI,
//     1180 and 560 DIP, long name.
#include "../../src/main.cpp"
#include <cstdio>
#include <uiautomationclient.h>
#include <thread>
#include <atomic>

static int failures=0;
static void Expect(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what.c_str());++failures;}else std::printf("ok   %s\n",what.c_str());}
static std::wstring out;
static void PumpAll(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}

// ---- PNG of a window (PrintWindow, full content) -------------------------------------
static bool SaveBitmapPng(HBITMAP bmp,const std::wstring& name){
    IWICImagingFactory* wic=nullptr;IWICBitmap* bitmap=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* enc=nullptr;IWICBitmapFrameEncode* frame=nullptr;bool saved=false;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp,nullptr,WICBitmapIgnoreAlpha,&bitmap))&&
       SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename((out+L"\\"+name+L".png").c_str(),GENERIC_WRITE))&&SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc))&&
       SUCCEEDED(enc->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(enc->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->WriteSource(bitmap,nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(enc->Commit()))saved=true;
    if(frame)frame->Release();if(enc)enc->Release();if(stream)stream->Release();if(bitmap)bitmap->Release();if(wic)wic->Release();return saved;
}
static bool Capture(HWND h,const std::wstring& name,RECT crop={}){
    RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT r{};GetWindowRect(h,&r);if(crop.right<=crop.left)crop={0,0,r.right-r.left,r.bottom-r.top};
    const int fw=r.right-r.left,fh=r.bottom-r.top,w=crop.right-crop.left,hh=crop.bottom-crop.top;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen),cdc=CreateCompatibleDC(screen);HBITMAP full=CreateCompatibleBitmap(screen,fw,fh),part=CreateCompatibleBitmap(screen,w,hh);
    auto o1=SelectObject(dc,full);const BOOL ok=PrintWindow(h,dc,2/*PW_RENDERFULLCONTENT*/);
    // This window lives off-screen (never in the user's way): GDI never draws its child EDIT there, so the
    // composed surface can hold stale pixels. The live editor prints itself (WM_PRINT) where it sits.
    if(folderRename.Active()&&folderRename.Host()==h){POINT origin{0,0};ClientToScreen(h,&origin);RECT er{};GetWindowRect(folderRename.Edit(),&er);
        const int saved=SaveDC(dc);SetViewportOrgEx(dc,er.left-r.left,er.top-r.top,nullptr);IntersectClipRect(dc,0,0,er.right-er.left,er.bottom-er.top);
        SendMessageW(folderRename.Edit(),WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND|PRF_NONCLIENT);RestoreDC(dc,saved);}auto o2=SelectObject(cdc,part);BitBlt(cdc,0,0,w,hh,dc,crop.left,crop.top,SRCCOPY);SelectObject(cdc,o2);SelectObject(dc,o1);
    const bool saved=ok&&SaveBitmapPng(part,name);DeleteObject(full);DeleteObject(part);DeleteDC(dc);DeleteDC(cdc);ReleaseDC(nullptr,screen);
    std::wprintf(L"     png %ls %ls\n",name.c_str(),saved?L"":L"(capture failed)");return saved;
}

// ---- Demo data (QA only) ---------------------------------------------------------------
static BM::Id game,news,sub,link,other;
static void Seed(){
    bookmarks=BM::Store{};bookmarkUndo.clear();const std::int64_t t=1794100000000;
    link=bookmarks.Add(BM::BarRoot,BM::End,L"Paper",L"https://paper.design/",t);
    game=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Game",t);
    bookmarks.Add(game,BM::End,L"itch.io",L"https://itch.io/",t);
    sub=bookmarks.AddFolder(game,BM::End,L"Rétro",t);bookmarks.Add(sub,BM::End,L"Lexaloffle",L"https://www.lexaloffle.com/",t);
    news=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Actualités",t);bookmarks.Add(news,BM::End,L"Le Monde",L"https://www.lemonde.fr/",t);
    for(auto* n:{L"MDN Web Docs",L"GitHub",L"France Culture",L"Mobbin"})bookmarks.Add(BM::BarRoot,BM::End,n,std::wstring(L"https://example.com/")+n,t);
    other=bookmarks.Add(BM::OtherRoot,BM::End,L"Wikipédia",L"https://fr.wikipedia.org/",t);
    BookmarksChanged();
}
static void Resize(int dip){SetWindowPos(window,nullptr,0,0,S((float)dip),S(815),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Layout();PumpAll();}
static int BandIndex(BM::Id id){for(int i=0;i<chrome.band.visible&&i<(int)chrome.bandItems.size();++i)if(chrome.bandItems[i].id==id)return i;return -1;}
static RECT ClientRectOf(HWND h){RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,GetParent(h),(POINT*)&r,2);return r;}
static RECT ExpectedBandBox(BM::Id id){const int i=BandIndex(id);if(i<0)return {};const auto b=chrome.BandTitleBox(i);return {S(b.left),S(b.top),S(b.right),S(b.bottom)};}
static std::wstring Text(HWND e){const int n=GetWindowTextLengthW(e);std::wstring s(n+1,0);GetWindowTextW(e,s.data(),n+1);s.resize(n);return s;}
static void Type(HWND e,const std::wstring& s){for(wchar_t c:s)SendMessageW(e,WM_CHAR,c,1);}
static void KeyTo(HWND to,WORD vk,wchar_t ch=0){SendMessageW(to,WM_KEYDOWN,vk,1);if(ch)SendMessageW(to,WM_CHAR,ch,1);SendMessageW(to,WM_KEYUP,vk,0xC0000001);PumpAll();}
static void Modifiers(bool ctrl,bool shift){BYTE k[256];GetKeyboardState(k);k[VK_CONTROL]=k[VK_LCONTROL]=ctrl?0x80:0;k[VK_SHIFT]=k[VK_LSHIFT]=shift?0x80:0;SetKeyboardState(k);}
static void F2OnBand(BM::Id id){const int i=BandIndex(id);SetFocus(window);keyboardModality=true;keyboardFocus=NativeChrome::BookmarkBase+i;SendMessageW(window,WM_KEYDOWN,VK_F2,1);PumpAll();}
// Picks the n-th enabled row of the next context menu with posted keys (the menu opens keyboard-first).
static void PostMenuPick(int downs){for(int i=0;i<downs;++i)PostMessageW(window,WM_KEYDOWN,VK_DOWN,1);PostMessageW(window,WM_KEYDOWN,VK_RETURN,1);}
// What a screen reader gets for an HWND: a UIA client on another thread while this one keeps pumping.
struct UiaRead { CONTROLTYPEID type=0; std::wstring name,value,id; BOOL focus=FALSE; };
static UiaRead ReadUia(HWND h){
    UiaRead r;std::atomic<bool> done{false};
    std::thread client([&]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);IUIAutomation* uia=nullptr;
        if(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&uia))&&uia){IUIAutomationElement* e=nullptr;
            if(SUCCEEDED(uia->ElementFromHandle(h,&e))&&e){BSTR s=nullptr;e->get_CurrentControlType(&r.type);if(SUCCEEDED(e->get_CurrentName(&s))&&s){r.name=s;SysFreeString(s);}
                s=nullptr;if(SUCCEEDED(e->get_CurrentAutomationId(&s))&&s){r.id=s;SysFreeString(s);}e->get_CurrentHasKeyboardFocus(&r.focus);
                IUIAutomationValuePattern* v=nullptr;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId,__uuidof(IUIAutomationValuePattern),(void**)&v))&&v){s=nullptr;if(SUCCEEDED(v->get_CurrentValue(&s))&&s){r.value=s;SysFreeString(s);}v->Release();}
                e->Release();}uia->Release();}
        CoUninitialize();done=true;});
    while(!done){PumpAll();Sleep(10);}client.join();return r;
}
static bool PopoverShown(){return bookmarkPopover.Visible();}
static void ClosePopover(){if(bookmarkPopover.Visible())bookmarkPopover.Hide();PumpAll();}

// ---- The real editor over the offscreen band at a given DPI (96 / 144) --------------------
static std::vector<DWORD> offscreenPixels;static int offW=0,offH=0;
// The offscreen band as pixels: through its own PNG (Chrome keeps its WIC surface private).
static void LoadPng(NativeChrome::Chrome& c,int w,int h){
    const std::wstring file=out+L"\\offscreen-band.png";const HRESULT saved=c.SavePng(file.c_str());
    if(FAILED(saved))std::printf("     offscreen SavePng failed 0x%08lx\n",(unsigned long)saved);
    IWICImagingFactory* wic=nullptr;IWICBitmapDecoder* dec=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&SUCCEEDED(wic->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec))&&
       SUCCEEDED(dec->GetFrame(0,&frame))&&SUCCEEDED(wic->CreateFormatConverter(&conv))&&SUCCEEDED(conv->Initialize(frame,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){
        WICRect all{0,0,w,h};conv->CopyPixels(&all,w*4,(UINT)offscreenPixels.size()*4,(BYTE*)offscreenPixels.data());}
    if(conv)conv->Release();if(frame)frame->Release();if(dec)dec->Release();if(wic)wic->Release();std::filesystem::remove(file);
    unsigned long long sum=0;for(DWORD p:offscreenPixels)sum+=p&0xFF;std::printf("     offscreen %dx%d, mean blue %.1f\n",w,h,offscreenPixels.empty()?0.0:double(sum)/offscreenPixels.size());
}
static LRESULT CALLBACK BackdropProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_CTLCOLOREDIT&&folderRename.Owns((HWND)l))return folderRename.Color((HDC)w);
    auto blit=[](HDC dc){BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),offW,-offH,1,32};SetDIBitsToDevice(dc,0,0,offW,offH,0,0,0,offH,offscreenPixels.data(),&bi,DIB_RGB_COLORS);};
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);blit(dc);EndPaint(h,&ps);return 0;}
    if(msg==WM_PRINTCLIENT){blit((HDC)w);return 0;}
    if(msg==WM_ERASEBKGND)return 1;
    return DefWindowProcW(h,msg,w,l);
}
// The backdrop band, then the real editor drawing itself (WM_PRINT) at its own position, cropped to PNG.
static void ComposeCapture(const std::wstring& name,RECT crop){
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP full=CreateCompatibleBitmap(screen,offW,offH);auto old=SelectObject(dc,full);
    BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),offW,-offH,1,32};SetDIBitsToDevice(dc,0,0,offW,offH,0,0,0,offH,offscreenPixels.data(),&bi,DIB_RGB_COLORS);
    if(HWND e=folderRename.Edit()){const RECT r=ClientRectOf(e);const int saved=SaveDC(dc);SetViewportOrgEx(dc,r.left,r.top,nullptr);IntersectClipRect(dc,0,0,r.right-r.left,r.bottom-r.top);SendMessageW(e,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND|PRF_NONCLIENT);RestoreDC(dc,saved);}
    crop.left=std::max(0L,crop.left);crop.top=std::max(0L,crop.top);crop.right=std::min((LONG)offW,crop.right);crop.bottom=std::min((LONG)offH,crop.bottom);
    const int w=crop.right-crop.left,h=crop.bottom-crop.top;HDC cdc=CreateCompatibleDC(screen);HBITMAP part=CreateCompatibleBitmap(screen,w,h);auto o2=SelectObject(cdc,part);BitBlt(cdc,0,0,w,h,dc,crop.left,crop.top,SRCCOPY);SelectObject(cdc,o2);
    SelectObject(dc,old);const bool saved=SaveBitmapPng(part,name);DeleteObject(part);DeleteObject(full);DeleteDC(cdc);DeleteDC(dc);ReleaseDC(nullptr,screen);
    std::wprintf(L"     png %ls %ls\n",name.c_str(),saved?L"":L"(capture failed)");
}
static void DpiComposite(UINT dpi,int width,const std::wstring& title,const std::wstring& tag){
    NativeChrome::Chrome c;if(FAILED(c.InitOffscreen(base,dpi,(float)width,121))){Expect(false,"offscreen chrome");return;}
    c.bandShown=true;c.bandItems=chrome.bandItems;for(auto& it:c.bandItems)if(it.id==game)it.title=title;c.address=AddressBar::Describe(L"https://example.com/");
    NativeChrome::Motion::State m(false);c.SetMotion(&m);std::vector<std::wstring> titles{L"Example"};
    auto shoot=[&](bool editing){
        int index=-1;c.Paint(titles,0,L"",false,0,0);for(int i=0;i<c.band.visible&&i<(int)c.bandItems.size();++i)if(c.bandItems[i].id==game)index=i;
        if(index<0){std::printf("     offscreen: shown %d, items %d, visible %d\n",(int)c.bandShown,(int)c.bandItems.size(),c.band.visible);Expect(false,"Game visible on the offscreen band "+std::string(tag.begin(),tag.end()));return;}
        c.bandRenaming=editing?index:-1;c.Paint(titles,0,L"",false,0,0);
        offW=(int)(width*dpi/96);offH=(int)(121*dpi/96);offscreenPixels.assign((size_t)offW*offH,0);LoadPng(c,offW,offH);
        HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"RenameBackdrop",L"",WS_POPUP|WS_CLIPCHILDREN,-30000,-30000,offW,offH,nullptr,nullptr,instance,nullptr);ShowWindow(host,SW_SHOWNOACTIVATE);
        const auto b=c.BandTitleBox(index);auto P=[dpi](float v){return (int)std::lround(v*dpi/96.f);};
        if(editing){
            HFONT f=CreateFontW(-P(NativeChrome::Design::Type::label.size),0,0,0,FW_MEDIUM,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Inter Display");
            NativeUI::InlineRename::Style st;st.font=f;st.text=NativeChrome::Design::NativeGray(1);st.back=NativeChrome::Design::NativeGray(c.BandSurface(1,0));
            folderRename.Begin(host,game,{P(b.left),P(b.top),P(b.right),P(b.bottom)},title,st,L"Nom",[](unsigned long long,bool,const std::wstring&){});
            const RECT e=ClientRectOf(folderRename.Edit());
            Expect(e.left==P(b.left)&&e.right==P(b.right)&&e.top>=P(b.top)&&e.bottom<=P(b.bottom),"editor on the band title box "+std::string(tag.begin(),tag.end()));
            const auto box=c.BandRect(index);ComposeCapture(L"band-"+tag+L"-renaming",{P(box.left)-P(8),P(box.top)-P(6),std::min(offW,P(box.right)+P(160)),P(box.bottom)+P(6)});
            folderRename.Cancel();DeleteObject(f);
        }else{const auto box=c.BandRect(index);ComposeCapture(L"band-"+tag+L"-before",{P(box.left)-P(8),P(box.top)-P(6),std::min(offW,P(box.right)+P(160)),P(box.bottom)+P(6)});}
        DestroyWindow(host);
    };
    shoot(false);shoot(true);
}

int wmain(int argc,wchar_t** argv){setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<3){std::fputs("usage: bookmark_rename_test <root> <out>\n",stderr);return 2;}
    base=argv[1];out=argv[2];std::filesystem::create_directories(out);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);instance=GetModuleHandleW(nullptr);
    smoke=true; // nothing read or written except in the persistence part, in its own folder
    logFile=out+L"\\rename.log";std::filesystem::remove(logFile);
    for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);
    WNDCLASSW bc{};bc.lpfnWndProc=BackdropProc;bc.hInstance=instance;bc.lpszClassName=L"RenameBackdrop";RegisterClassW(&bc);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.lpszClassName=L"BookmarkRenameTest";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    // Own window, shown without activation, off the user's way.
    CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"browser",WS_POPUP|WS_CLIPCHILDREN,-30000,-30000,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,instance,nullptr);
    if(!window||FAILED(chrome.Init(window,base))){std::fputs("FAIL: window\n",stderr);return 1;}
    motion.SetReducedMotion(true);chrome.SetMotion(&motion);contextMenu.Init(instance,window,base);UpdateFont();CreateBookmarkSurfaces();
    auto t=std::make_shared<Tab>();t->title=L"Example";t->uri=L"https://example.com/";t->home=false;pages={t};current=0;
    ShowWindow(window,SW_SHOWNOACTIVATE);Seed();Layout();Update();UpdateWindow(window);PumpAll();
    std::printf("     window DPI %u\n",GetDpiForWindow(window));

    // ---- 1. Band F2: the editor sits on the title, not the popover --------------------------------
    Capture(window,L"window-band-before",{0,0,S(700),S(121)});
    F2OnBand(game);
    HWND e=folderRename.Edit();
    Expect(folderRename.Active()&&e&&GetParent(e)==window&&!PopoverShown(),"F2 on a bar folder: editor in place, no popover");
    { const RECT got=ClientRectOf(e),want=ExpectedBandBox(game);
      Expect(got.left==want.left&&got.right==want.right&&got.top>=want.top&&got.bottom<=want.bottom,"editor exactly on the folder's title box (after the icon, before the chevron)"); }
    Expect(chrome.bandRenaming==BandIndex(game),"band hides its own title and keeps the item's hover surface");
    { DWORD a=0,b=0;SendMessageW(e,EM_GETSEL,(WPARAM)&a,(LPARAM)&b);Expect(a==0&&b==4&&Text(e)==L"Game","whole name « Game » selected"); }
    { HFONT f=(HFONT)SendMessageW(e,WM_GETFONT,0,0);LOGFONTW lf{};GetObjectW(f,sizeof(lf),&lf);Expect(std::wstring(lf.lfFaceName)==L"Inter Display"&&lf.lfWeight==FW_MEDIUM&&lf.lfHeight==-S(14),"Inter Display Medium 14 DIP, like the band title"); }
    Expect(GetFocus()==e,"keyboard focus in the editor");
    { const auto u=ReadUia(e);std::printf("     UIA band editor: type %d name %ls value %ls id %ls focus %d\n",(int)u.type,u.name.c_str(),u.value.c_str(),u.id.c_str(),(int)u.focus);Expect(u.type==UIA_EditControlTypeId&&u.name==TR(bookmark_field_name)&&u.value==L"Game"&&u.id==L"BookmarkRename","UIA (band; keyboard focus is checked in the real app, this window is never activated): Edit « "+Utf8(TR(bookmark_field_name))+" », value « Game »"); }
    // App shortcuts never fire from the field (Ctrl+W, Ctrl+D, Ctrl+Z, F5, Delete).
    { Modifiers(true,false);const int w=Key('W'),d=Key('D'),z=Key('Z');Modifiers(false,false);const int f5=Key(VK_F5);
      Expect(!w&&!d&&!z&&!f5,"Ctrl+W, Ctrl+D, Ctrl+Z and F5 stay with the text field"); }
    Capture(window,L"window-band-renaming",{0,0,S(700),S(121)});
    Type(e,L"Jeux vidéo");KeyTo(e,VK_RETURN,L'\r');
    Expect(!folderRename.Active()&&bookmarks.Find(game)->title==L"Jeux vidéo","Enter commits « Jeux vidéo »");
    Expect(!bookmarkUndo.empty()&&bookmarkUndo.back().steps.size()==1&&bookmarkUndo.back().steps[0].kind==BookmarkStep::Renamed,"one undo step « Renamed »");
    Expect(GetFocus()==window&&keyboardFocus==NativeChrome::BookmarkBase+BandIndex(game)&&chrome.bandRenaming==-1,"keyboard focus back on the band item, title drawn again");
    Capture(window,L"window-band-after",{0,0,S(700),S(121)});
    UndoBookmark();Expect(bookmarks.Find(game)->title==L"Game","bookmark undo (Ctrl+Z from the chrome) restores « Game »");

    // ---- 2. Escape, empty, blur, Tab ------------------------------------------------------------
    F2OnBand(game);e=folderRename.Edit();const size_t undoBefore=bookmarkUndo.size();
    Type(e,L"XYZ");KeyTo(e,VK_ESCAPE,27);
    Expect(!folderRename.Active()&&bookmarks.Find(game)->title==L"Game"&&bookmarkUndo.size()==undoBefore,"Escape restores the name, nothing written");
    F2OnBand(game);e=folderRename.Edit();KeyTo(e,VK_DELETE);Type(e,L"   ");KeyTo(e,VK_RETURN,L'\r');
    Expect(bookmarks.Find(game)&&bookmarks.Find(game)->title==L"Game"&&bookmarkUndo.size()==undoBefore,"an empty / blank name keeps the old name, the folder is not deleted");
    F2OnBand(game);e=folderRename.Edit();Type(e,L"Jeux");SetFocus(window);PumpAll();
    Expect(!folderRename.Active()&&bookmarks.Find(game)->title==L"Jeux","focus loss (press elsewhere) commits");
    F2OnBand(game);e=folderRename.Edit();Type(e,L"Game");KeyTo(e,VK_TAB,L'\t');
    Expect(!folderRename.Active()&&bookmarks.Find(game)->title==L"Game","Tab commits");
    F2OnBand(game);e=folderRename.Edit();Type(e,L"Spiele 🎮 ゲーム");KeyTo(e,VK_RETURN,L'\r');
    Expect(bookmarks.Find(game)->title==L"Spiele 🎮 ゲーム","Unicode name (emoji, Japanese) stored as typed");
    bookmarks.Rename(game,L"Game");BookmarksChanged();

    // ---- 3. Context menu, new folder ------------------------------------------------------------
    { const RECT r=RegionScreen(NativeChrome::BookmarkBase+BandIndex(game));PostMenuPick(1);BookmarkContextMenu(game,{r.left,r.bottom},&r,true);PumpAll(); }
    Expect(folderRename.Active()&&folderRename.Id()==game&&folderRename.Host()==window&&!PopoverShown(),"menu « Renommer… » on a bar folder: in place on the band");
    folderRename.Cancel();
    { RECT r=RegionScreen(NativeChrome::BookmarkBase);PostMenuPick(2); /* pointer menu: no row hovered yet */const size_t before=bookmarks.Root(BM::BarRoot).children.size();BandContextMenu({r.right+S(400),r.bottom});PumpAll();
      const BM::Id made=bookmarks.Root(BM::BarRoot).children.size()>before?bookmarks.Root(BM::BarRoot).children.back():0;
      Expect(made&&folderRename.Active()&&folderRename.Id()==made&&!PopoverShown(),"band « Nouveau dossier »: the new folder's name edited in place");
      if(folderRename.Active()){Expect(Text(folderRename.Edit())==TR(bookmark_new_folder_name),"its default name is selected for replacement");Type(folderRename.Edit(),L"Musique");KeyTo(folderRename.Edit(),VK_RETURN,L'\r');}
      Expect(made&&bookmarks.Find(made)&&bookmarks.Find(made)->title==L"Musique","new folder named « Musique »");
      if(made){bookmarks.Remove(made);BookmarksChanged();} }

    // ---- 4. Lifecycle -----------------------------------------------------------------------------
    F2OnBand(game);
    bookmarks.Move(news,BM::BarRoot,0);BookmarksChanged();PumpAll();
    { const RECT got=ClientRectOf(folderRename.Edit()),want=ExpectedBandBox(game);Expect(folderRename.Active()&&got.left==want.left&&got.right==want.right,"another item moved before it: the editor follows the label"); }
    bookmarks.Move(news,BM::BarRoot,2);BookmarksChanged();
    Resize(560);
    { const int i=BandIndex(game);if(i>=0){const RECT got=ClientRectOf(folderRename.Edit()),want=ExpectedBandBox(game);Expect(folderRename.Active()&&got.left==want.left&&got.right==want.right,"560 DIP: the editor follows the narrowed title");}
      else Expect(!folderRename.Active(),"560 DIP: label gone behind », editor cancelled"); }
    { DWORD a=0,b=0;if(folderRename.Active())SendMessageW(folderRename.Edit(),EM_GETSEL,(WPARAM)&a,(LPARAM)&b);
      std::wprintf(L"     560 DIP: folder « %ls », editor « %ls », selection %lu-%lu\n",bookmarks.Find(game)->title.c_str(),folderRename.Active()?Text(folderRename.Edit()).c_str():L"-",a,b); }
    Capture(window,L"window-band-renaming-560",{0,0,S(560),S(121)});
    Resize(1180);folderRename.Cancel();
    F2OnBand(game);Type(folderRename.Edit(),L"Zombie");
    { auto removed=bookmarks.Remove(game);BookmarksChanged(); // « another window » deleted it (merge)
      Expect(!folderRename.Active()&&!bookmarks.Find(game),"folder deleted elsewhere mid-edit: editor cancelled, no resurrection");
      PumpAll();Expect(!bookmarks.Find(game),"still gone after the queue drains");
      if(removed)bookmarks.Restore(*removed);BookmarksChanged(); }
    F2OnBand(game);Type(folderRename.Edit(),L"Autre profil");
    { const std::wstring was=dataDir;dataDir=out+L"\\other-profile";BookmarksChanged();
      Expect(!folderRename.Active()&&bookmarks.Find(game)->title==L"Game","profile changed mid-edit: cancelled, nothing written");dataDir=was;BookmarksChanged(); }

    // ---- 5. Fallbacks -----------------------------------------------------------------------------
    { POINT p{100,100};PostMenuPick(1);BookmarkContextMenu(sub,p,nullptr,true);PumpAll();
      Expect(!folderRename.Active()&&PopoverShown(),"sub-folder (row of a native folder menu): its label is gone, the popover is kept");ClosePopover(); }
    { const int i=BandIndex(link);SetFocus(window);keyboardFocus=NativeChrome::BookmarkBase+i;SendMessageW(window,WM_KEYDOWN,VK_F2,1);PumpAll();
      Expect(!folderRename.Active()&&PopoverShown(),"link F2: the edit popover, unchanged");ClosePopover(); }
    Expect(!StartFolderRename(BM::BarRoot,RenameBand,true,0)&&!StartFolderRename(BM::OtherRoot,RenameDrawer,true,0),"root folders never rename");
    Expect(!StartFolderRename(link,RenameBand,true,0),"a link never gets the folder editor");

    // ---- 6. Drawer row ------------------------------------------------------------------------------
    ShowBookmarksDrawer(true);PumpAll();
    if(bookmarksDrawer.Visible()){
        bookmarksDrawer.FocusBookmark(game);bookmarksDrawer.KeyForTest(VK_F2);PumpAll();
        Expect(folderRename.Active()&&folderRename.Host()==bookmarksDrawer.Window()&&!PopoverShown(),"drawer F2 on a folder row: in place on the row");
        RECT want{};const bool shown=bookmarksDrawer.TitleBox(game,want);const RECT got=folderRename.Active()?ClientRectOf(folderRename.Edit()):RECT{};
        std::printf("     drawer title box %ld,%ld-%ld,%ld editor %ld,%ld-%ld,%ld\n",want.left,want.top,want.right,want.bottom,got.left,got.top,got.right,got.bottom);
        Expect(shown&&got.left==want.left&&got.right==want.right&&std::abs((got.top+got.bottom)-(want.top+want.bottom))<=1,"editor on the drawer row's title box, centred on its line (the font cell overhangs the 16 DIP line onto the canvas)");
        Expect(bookmarksDrawer.Renaming()==game,"drawer hides that row's own title");
        if(folderRename.Active()){const auto u=ReadUia(folderRename.Edit());Expect(u.type==UIA_EditControlTypeId&&u.name==TR(bookmark_field_name)&&u.value==L"Game","UIA (drawer): Edit « "+Utf8(TR(bookmark_field_name))+" », value « Game »");}
        Sleep(700);bookmarksDrawer.Frame(NativeChrome::Motion::Now());Capture(bookmarksDrawer.Window(),L"drawer-renaming");
        if(folderRename.Active()){Type(folderRename.Edit(),L"Jeux (tiroir)");KeyTo(folderRename.Edit(),VK_RETURN,L'\r');}
        Expect(bookmarks.Find(game)->title==L"Jeux (tiroir)"&&bookmarksDrawer.Renaming()==0,"drawer commit renames the folder");
        Sleep(700);bookmarksDrawer.Frame(NativeChrome::Motion::Now());Capture(bookmarksDrawer.Window(),L"drawer-after");
        bookmarksDrawer.Hide(false,false);PumpAll();
        bookmarks.Rename(game,L"Game");BookmarksChanged();
    }else Expect(false,"drawer visible");

    // ---- 7. Persistence and save failure ----------------------------------------------------------
    { namespace fs=std::filesystem;smoke=false;dataDir=out+L"\\profile";fs::remove_all(dataDir);fs::create_directories(dataDir);SaveBookmarks();
      F2OnBand(game);Type(folderRename.Edit(),L"Jeux persistés");KeyTo(folderRename.Edit(),VK_RETURN,L'\r');
      const BM::Store before=bookmarks;LoadBookmarks();
      Expect(bookmarks.Find(game)&&bookmarks.Find(game)->title==L"Jeux persistés"&&!bookmarksSaveFailed,"written to the profile file and read back");
      bookmarks=before;
      // Unwritable profile: the data folder is a file.
      // A bookmarks file of an unknown (future) format is never overwritten (ProfileStore fail-closed): the save fails.
      const std::wstring bad=out+L"\\future-profile";fs::remove_all(bad);fs::create_directories(bad);{std::ofstream f{fs::path(bad+L"\\bookmarks"),std::ios::binary};f<<"browser-bookmarks 99\nfuture\n";}dataDir=bad;bookmarksBase=bookmarks;
      std::filesystem::remove(logFile);
      F2OnBand(game);Type(folderRename.Edit(),L"Jamais écrit");KeyTo(folderRename.Edit(),VK_RETURN,L'\r');
      std::ifstream logIn{fs::path(logFile)};std::string log((std::istreambuf_iterator<char>(logIn)),{});
      Expect(bookmarksSaveFailed&&bookmarks.Find(game)->title==L"Jamais écrit","save failed: the name stays in memory, the retry toast is up");
      Expect(log.find(Utf8(std::wstring(L"BOOKMARKS ")+TR(bookmark_saved)))==std::string::npos&&log.find(Utf8(std::wstring(L"BOOKMARKS ")+TR(bookmark_save_failed)))!=std::string::npos,"never announced « saved » when the file was not written");
      bookmarkToast.HideNow();smoke=true;dataDir.clear();bookmarksSaveFailed=false;bookmarks.Rename(game,L"Game");BookmarksChanged(); }

    // ---- 8. The real editor over the band at 96 and 144 DPI, wide / narrow, long name ---------------
    for(UINT dpi:{96u,144u}){const std::wstring d=std::to_wstring(dpi);
        DpiComposite(dpi,1180,L"Game",L"1180-"+d);
        DpiComposite(dpi,560,L"Game",L"560-"+d);
        DpiComposite(dpi,1180,L"Jeux vidéo et émulateurs rétro des années 90",L"long-"+d);}

    folderRename.Cancel();bookmarkPopover.Hide();bookmarkToast.HideNow();DestroyWindow(window);
    if(failures){std::fprintf(stderr,"%d folder rename check(s) failed\n",failures);return 1;}
    std::puts("PASS: folder rename in place on the band and the drawer (F2, menu, new folder), Enter/Escape/empty/blur/Tab, shortcuts kept, undo, lifecycle, fallbacks, persistence, save failure, 96/144 DPI captures");
    return 0;
}
