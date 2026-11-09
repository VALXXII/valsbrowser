// Bookmarks in the real chrome (no WebView2): the production window procedure, store,
// popover, toast, drawer and native menus run on this thread, with demo data only
// (never a personal profile). A real UI Automation client on a second thread reads
// what Narrator or NVDA would. Every surface is also rendered to PNG for review in
// build/bookmarks-ui-qa.
//  1. Band geometry at 1180 and 560 DIP (Paper: 6 DIP padding, 2 DIP gaps, 160 cap,
//     96 floor, » reserved), unique hit regions, page top 81 / 121.
//  2. Offscreen renders at 96/144/192 DPI: rest, hover, folder open, 560 overflow,
//     empty "Toujours" band; hairline under the chrome.
//  3. UIA: "Barre de favoris" toolbar, bookmark buttons, folder ExpandCollapse,
//     star Toggle, native menus as Menu/MenuItem (submenu expand, Escape returns to
//     the folder), the Favoris drawer List with levels, popover named fields.
//  4. Interactions: Ctrl+D path, popover invalid address (red text) then valid,
//     keyboard roving and Ctrl+Maj+→ move, drag reorder, delete + toast + undo,
//     duplicate, restart (file written then read back), damaged file recovery.
#include "../../src/main.cpp"
#include <uiautomationclient.h>
#include <atomic>
#include <cstdio>
#include <thread>

static int failures=0;
static void Expect(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}else std::printf("ok   %s\n",what);}
constexpr UINT RunMessage=WM_APP+60,PostRunMessage=WM_APP+61;
static HWND helper=nullptr;static DWORD uiThread=0;static std::wstring out;
static LRESULT CALLBACK HelperProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==RunMessage){(*reinterpret_cast<std::function<void()>*>(l))();return 0;}
    if(msg==PostRunMessage){auto* f=reinterpret_cast<std::function<void()>*>(l);(*f)();delete f;return 0;}
    return DefWindowProcW(h,msg,w,l);
}
static void OnUi(std::function<void()> f){SendMessageW(helper,RunMessage,0,(LPARAM)&f);}
static void Later(std::function<void()> f){PostMessageW(helper,PostRunMessage,0,(LPARAM)new std::function<void()>(std::move(f)));}
template<class F> static bool WaitFor(F condition,int ms=4000){for(int waited=0;waited<ms;waited+=20){if(condition())return true;Sleep(20);}return condition();}

// ---- Window capture (PrintWindow, full content) -> PNG ----------------------------
static bool Capture(HWND h,const std::wstring& name){
    // Capture the settled surface, rather than a blurred menu's entrance frame.
    // This runs on the UI thread, so explicitly advance its frame after waiting.
    Sleep(700);
    bookmarksDrawer.Frame(NativeChrome::Motion::Now());
    wchar_t className[80]{};GetClassNameW(h,className,80);
    if(std::wstring(className)==L"LisiereContextMenu"||std::wstring(className)==L"LisiereBookmarkPopover")SendMessageW(h,WM_TIMER,1,0);
    RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT r{};if(!h||!GetWindowRect(h,&r))return false;const int w=r.right-r.left,hh=r.bottom-r.top;if(w<=0||hh<=0)return false;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-hh;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bmp=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bmp);
    BOOL ok=PrintWindow(h,dc,2/*PW_RENDERFULLCONTENT*/);SelectObject(dc,old);
    bool saved=false;
    if(ok){IWICImagingFactory* wic=nullptr;IWICBitmap* bitmap=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* enc=nullptr;IWICBitmapFrameEncode* frame=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp,nullptr,WICBitmapIgnoreAlpha,&bitmap))&&
           SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename((out+L"\\"+name+L".png").c_str(),GENERIC_WRITE))&&SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc))&&
           SUCCEEDED(enc->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(enc->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->WriteSource(bitmap,nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(enc->Commit()))saved=true;
        if(frame)frame->Release();if(enc)enc->Release();if(stream)stream->Release();if(bitmap)bitmap->Release();if(wic)wic->Release();}
    DeleteObject(bmp);DeleteDC(dc);ReleaseDC(nullptr,screen);
    std::wprintf(L"     png %ls %ls\n",name.c_str(),saved?L"":L"(capture failed)");return saved;
}

// ---- Demo data (explicit QA profile only) ----------------------------------------
static BM::Id demoPaper,veille,maquettes,projet,lemonde;
static void Seed(){
    bookmarks=BM::Store{};const std::int64_t t=1794100000000;
    demoPaper=bookmarks.Add(BM::BarRoot,BM::End,L"Paper",L"https://paper.design/",t);
    veille=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Veille",t);
    bookmarks.Add(veille,BM::End,L"Le Monde — International",L"https://www.lemonde.fr/international/",t);
    bookmarks.Add(veille,BM::End,L"MDN Web Docs",L"https://developer.mozilla.org/",t);
    maquettes=bookmarks.AddFolder(veille,BM::End,L"Maquettes",t);
    bookmarks.Add(maquettes,BM::End,L"Mobbin",L"https://mobbin.com/",t);bookmarks.Add(maquettes,BM::End,L"Luma",L"https://lu.ma/",t);
    bookmarks.Add(BM::BarRoot,BM::End,L"GitHub · ValXXII/Browser",L"https://github.com/ValXXII/Browser",t);
    projet=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Projet Lisière",t);
    for(auto* n:{L"Cahier des charges",L"Maquette v3",L"Notes de réunion"})bookmarks.Add(projet,BM::End,n,L"file:///C:/demo/"+std::wstring(n)+L".html",t);
    lemonde=bookmarks.Add(BM::BarRoot,BM::End,L"Le Monde",L"https://www.lemonde.fr/",t);
    bookmarks.Add(BM::BarRoot,BM::End,L"MDN Web Docs",L"https://developer.mozilla.org/fr/",t);
    bookmarks.Add(BM::BarRoot,BM::End,L"Mobbin",L"https://mobbin.com/",t);
    bookmarks.Add(BM::BarRoot,BM::End,L"France Culture",L"https://www.radiofrance.fr/franceculture",t);
    bookmarks.Add(BM::OtherRoot,BM::End,L"Wikipédia",L"https://fr.wikipedia.org/",t);
    bookmarks.Add(BM::OtherRoot,BM::End,L"",L"https://example.com/sans-titre",t);
}
static void Resize(int dip){SetWindowPos(window,nullptr,0,0,S((float)dip),S(815),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Layout();}

// ---- UI-thread checks: geometry and offscreen renders -------------------------------
static void Geometry(){
    Resize(1180);namespace L=BookmarksBar::Layout;
    Expect(chrome.bandShown&&chrome.ContentTop()==121,"band shown, page at 121 DIP");
    const auto& b=chrome.band;bool gaps=true,caps=true;
    for(int i=0;i<b.visible;++i){if(b.width[i]>L::maxItem+.5f)caps=false;if(i&&std::fabs(b.left[i]-(b.left[i-1]+b.width[i-1]+L::gap))>.5f)gaps=false;}
    Expect(b.visible>0&&std::fabs(b.left[0]-6)<.5f,"first band item at x=6, aligned with Précédent");
    Expect(gaps,"items 2 DIP apart (Paper band gap)");Expect(caps,"no item wider than 160 DIP");
    // Every point of the chrome reaches at most one region.
    bool unique=true;for(float y=2;y<chrome.ContentTop();y+=2)for(float x=1;x<chrome.width;x+=3){int n=0;for(auto& r:chrome.regions)if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom&&r.id!='I'&&!NativeChrome::IsClose(r.id))++n;if(n>1){unique=false;break;}}
    Expect(unique,"no overlapping hit regions at 1180 DIP");
    const D2D1_RECT_F* star=nullptr,*reload=nullptr;for(auto& r:chrome.regions){if(r.id=='K')star=&r.rect;if(r.id=='R')reload=&r.rect;}
    Expect(star&&star->right<=chrome.bar.right-4+.1f&&reload&&chrome.edit.right<=reload->left-10+.1f,"star inside the field after Copy; text stops 10 DIP before Reload");
    Resize(560);
    Expect(chrome.band.overflow&&chrome.band.visible<(int)chrome.bandItems.size()&&std::fabs(chrome.band.overflowLeft-(560-6-32))<.5f,"560 DIP: overflow button pinned at 522");
    bool floor=true;for(int i=0;i<chrome.band.visible;++i)if(chrome.band.width[i]+.5f<std::min(L::minItem,L::Natural(chrome.BandTitleWidth(chrome.bandItems[i]),chrome.bandItems[i].folder)))floor=false;
    Expect(floor,"560 DIP: titles shrink to the 96 DIP floor, never below");
    bool inside=true;for(auto& r:chrome.regions)if(r.rect.right>560-6+.5f&&r.id!='X'&&r.id!='S'&&r.id!='M'&&r.id!='m')inside=false;
    Expect(inside,"560 DIP: nothing past the 6 DIP right margin");
    std::printf("     560 DIP: address text width %.0f DIP, %d of %d items visible\n",chrome.edit.right-chrome.edit.left,chrome.band.visible,(int)chrome.bandItems.size());
    Expect(chrome.edit.right-chrome.edit.left>=190,"560 DIP: the address keeps at least 190 DIP of text");
    unique=true;for(float y=2;y<chrome.ContentTop();y+=2)for(float x=1;x<chrome.width;x+=2){int n=0;for(auto& r:chrome.regions)if(x>=r.rect.left&&x<r.rect.right&&y>=r.rect.top&&y<r.rect.bottom&&r.id!='I'&&!NativeChrome::IsClose(r.id))++n;if(n>1){unique=false;break;}}
    Expect(unique,"no overlapping hit regions at 560 DIP");
    Resize(1180);
}
static void Renders(){
    // Offscreen, same band data as the window.
    for(UINT dpi:{96u,144u,192u})for(int width:{1180,560}){
        NativeChrome::Chrome c;if(FAILED(c.InitOffscreen(base,dpi,(float)width,121))){Expect(false,"offscreen chrome");continue;}
        c.bandShown=true;c.bandItems=chrome.bandItems;c.bandButtonForced=chrome.bandButtonForced;c.starred=true;c.address=AddressBar::Describe(L"https://www.lemonde.fr/international/");
        NativeChrome::Motion::State m(false);c.SetMotion(&m);
        std::vector<std::wstring> titles{L"Le Monde — International",L"Paper"};
        auto name=[&](const wchar_t* s){return out+L"\\band-"+s+L"-"+std::to_wstring(width)+L"-"+std::to_wstring(dpi)+L".png";};
        c.Paint(titles,0,L"",false,0,0);c.SavePng(name(L"rest").c_str());
        const UINT32 hair=c.SurfacePixel(300,120.5f)&0xff,canvas=c.SurfacePixel(300,118)&0xff;
        if(width==1180&&dpi==96){Expect(hair>=26&&hair<=29&&canvas==23,"1 DIP #FFFFFF05 hairline at the chrome's lower edge (y=120)");}
        m.SetInteraction(NativeChrome::BookmarkBase+2,0,NativeChrome::Motion::Now()-std::chrono::milliseconds(500));
        c.Paint(titles,0,L"",false,NativeChrome::BookmarkBase+2,0,nullptr,NativeChrome::Motion::Now()+std::chrono::milliseconds(400));c.SavePng(name(L"hover").c_str());
        if(width==1180&&dpi==96){const auto r=c.BandRect(2);Expect((c.SurfacePixel(r.left+3,r.top+3)&0xff)==51,"hovered item: Ghost #333333, radius 6");}
        c.bandOpen=NativeChrome::BookmarkBase+1;m.ClearInteractions();c.keyboardModality=true;
        c.Paint(titles,0,L"",false,0,NativeChrome::BookmarkBase+1);c.SavePng(name(L"folder-open").c_str());c.bandOpen=0;
        if(width==1180){c.bandItems.clear();c.bandButtonForced=false;c.Paint(titles,0,L"",false,0,0);c.SavePng(name(L"empty-always").c_str());}
    }
    {NativeChrome::Chrome c;c.InitOffscreen(base,96,1180,84);std::vector<std::wstring> titles{L"Accueil"};c.Paint(titles,0,L"",false,0,0);c.SavePng((out+L"\\chrome-no-band-1180-96.png").c_str());
        Expect((c.SurfacePixel(300,80.5f)&0xff)>=26&&(c.SurfacePixel(300,82)&0xff)==23,"without the band the hairline sits at y=80, page edge 81");}
}

// ---- UIA client --------------------------------------------------------------------
static const CLSID ClsidUIAutomation8={0xe22ad333,0xb25f,0x460c,{0x83,0xd0,0x05,0x81,0x10,0x73,0x95,0xc9}};
static IUIAutomation* uia=nullptr;
static std::wstring Str(BSTR b){std::wstring s=b?b:L"";SysFreeString(b);return s;}
static std::wstring Name(IUIAutomationElement* e){BSTR b=nullptr;e->get_CurrentName(&b);return Str(b);}
static std::wstring Aid(IUIAutomationElement* e){BSTR b=nullptr;e->get_CurrentAutomationId(&b);return Str(b);}
static CONTROLTYPEID Type(IUIAutomationElement* e){CONTROLTYPEID t=0;e->get_CurrentControlType(&t);return t;}
struct El{IUIAutomationElement* e;std::wstring name,parent;CONTROLTYPEID type;};
static void Walk(IUIAutomationTreeWalker* w,IUIAutomationElement* p,std::wstring pn,std::vector<El>& outv,int depth=0){
    IUIAutomationElement* c=nullptr;w->GetFirstChildElement(p,&c);
    while(c&&depth<8&&outv.size()<400){outv.push_back({c,Name(c),pn,Type(c)});Walk(w,c,outv.back().name,outv,depth+1);IUIAutomationElement* n=nullptr;w->GetNextSiblingElement(c,&n);c=n;}
}
static std::vector<El> Tree(HWND h){std::vector<El> v;IUIAutomationElement* root=nullptr;IUIAutomationTreeWalker* w=nullptr;if(FAILED(uia->ElementFromHandle(h,&root))||!root)return v;uia->get_ControlViewWalker(&w);Walk(w,root,L"<root>",v);w->Release();root->Release();return v;}
static void Free(std::vector<El>& v){for(auto& i:v)i.e->Release();v.clear();}
static int ExpandState(IUIAutomationElement* e){IUIAutomationExpandCollapsePattern* p=nullptr;ExpandCollapseState s=ExpandCollapseState_LeafNode;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId,__uuidof(IUIAutomationExpandCollapsePattern),(void**)&p))&&p){p->get_CurrentExpandCollapseState(&s);p->Release();return s==ExpandCollapseState_Expanded?2:1;}return 0;}
static bool Expand(IUIAutomationElement* e){IUIAutomationExpandCollapsePattern* p=nullptr;HRESULT hr=E_FAIL;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId,__uuidof(IUIAutomationExpandCollapsePattern),(void**)&p))&&p){hr=p->Expand();p->Release();}return SUCCEEDED(hr);}
static int Toggle(IUIAutomationElement* e){IUIAutomationTogglePattern* p=nullptr;ToggleState s=ToggleState_Indeterminate;if(SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId,__uuidof(IUIAutomationTogglePattern),(void**)&p))&&p){p->get_CurrentToggleState(&s);p->Release();return s==ToggleState_On?2:1;}return 0;}
static int IntProp(IUIAutomationElement* e,PROPERTYID id){VARIANT v;VariantInit(&v);int r=0;if(SUCCEEDED(e->GetCurrentPropertyValue(id,&v))&&v.vt==VT_I4)r=v.lVal;VariantClear(&v);return r;}
static bool Offscreen(IUIAutomationElement* e){BOOL b=FALSE;e->get_CurrentIsOffscreen(&b);return b!=FALSE;}
struct NotificationHandler final:IUIAutomationNotificationEventHandler{
    std::atomic<long> refs{1};CRITICAL_SECTION lock;std::vector<std::wstring> texts;NotificationHandler(){InitializeCriticalSection(&lock);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** o)override{if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomationNotificationEventHandler)){*o=this;AddRef();return S_OK;}*o=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE HandleNotificationEvent(IUIAutomationElement*,NotificationKind,NotificationProcessing,BSTR d,BSTR)override{EnterCriticalSection(&lock);texts.push_back(d?d:L"");LeaveCriticalSection(&lock);return S_OK;}
    bool Has(const std::wstring& s){EnterCriticalSection(&lock);bool f=false;for(auto& t:texts)if(t.find(s)!=std::wstring::npos)f=true;LeaveCriticalSection(&lock);return f;}
};

static void Client(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(CoCreateInstance(ClsidUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&uia))){Expect(false,"UIA client");PostThreadMessageW(uiThread,WM_QUIT,0,0);return;}
    IUIAutomation5* uia5=nullptr;uia->QueryInterface(__uuidof(IUIAutomation5),(void**)&uia5);NotificationHandler note;IUIAutomationElement* root=nullptr;uia->ElementFromHandle(window,&root);
    // Register before exercising the controls: UIA installs remote event listeners asynchronously.
    if(uia5&&root)Expect(SUCCEEDED(uia5->AddNotificationEventHandler(root,TreeScope_Subtree,nullptr,&note)),"notification listener registered");

    // 1. The band and the star.
    auto items=Tree(window);
    IUIAutomationElement* band=nullptr;for(auto& i:items)if(i.name==L"Barre de favoris"&&i.type==UIA_ToolBarControlTypeId)band=i.e;
    Expect(band!=nullptr,"« Barre de favoris » toolbar exposed");
    std::vector<El*> kids;for(auto& i:items)if(i.parent==L"Barre de favoris")kids.push_back(&i);
    size_t barCount=0;OnUi([&]{barCount=bookmarks.Root(BM::BarRoot).children.size();});
    Expect(kids.size()==barCount+1,"one button per bar bookmark, plus the » button");
    if(!kids.empty())Expect(kids[0]->name==L"Paper"&&Aid(kids[0]->e)==L"Bookmark."+std::to_wstring(demoPaper)&&IntProp(kids[0]->e,UIA_PositionInSetPropertyId)==1,"bookmark button: name = title, AutomationId Bookmark.<id>, position 1");
    IUIAutomationElement* veilleEl=nullptr;for(auto* k:kids)if(k->name==L"Veille")veilleEl=k->e;
    Expect(veilleEl&&ExpandState(veilleEl)==1,"folder button: ExpandCollapse, collapsed");
    IUIAutomationElement* star=nullptr;for(auto& i:items)if(Aid(i.e)==L"Bookmark.Star")star=i.e;
    Expect(star&&Name(star)==L"Modifier le favori"&&Toggle(star)==2,"star: « Modifier le favori », Toggle On on a bookmarked page");
    Free(items);

    // 2. Folder menu through UIA: Menu + MenuItems, submenu expand, Escape returns to the folder.
    items=Tree(window);for(auto& i:items)if(i.name==L"Veille"&&i.parent==L"Barre de favoris")veilleEl=i.e;
    if(veilleEl)Expand(veilleEl);
    Expect(WaitFor([]{int d=0;OnUi([&]{d=contextMenu.Depth();});return d==1;}),"Expand opens the folder's native menu");
    HWND menu=nullptr;OnUi([&]{menu=contextMenu.LevelWindow(0);});
    {auto m=Tree(menu);IUIAutomationElement* list=nullptr;std::vector<std::wstring> rows;IUIAutomationElement* sub=nullptr;
        for(auto& i:m){if(i.type==UIA_MenuControlTypeId)list=i.e;if(i.type==UIA_MenuItemControlTypeId){rows.push_back(i.name);if(i.name==L"Maquettes")sub=i.e;}}
        Expect(list!=nullptr,"menu exposed as Menu");
        Expect(rows.size()==4&&rows[0]==L"Ouvrir les 4 favoris"&&rows[1]==L"Le Monde — International"&&rows[3]==L"Maquettes","MenuItems in folder order, « Ouvrir les 4 favoris » first");
        OnUi([&]{Capture(menu,L"menu-folder");});
        Expect(sub&&ExpandState(sub)==1,"subfolder row: ExpandCollapse collapsed");
        if(sub)Expand(sub);
        Expect(WaitFor([]{int d=0;OnUi([&]{d=contextMenu.Depth();});return d==2;}),"Expand on a submenu row opens it");
        HWND child=nullptr;OnUi([&]{child=contextMenu.LevelWindow(1);});
        auto c=Tree(child);std::vector<std::wstring> subRows;for(auto& i:c)if(i.type==UIA_MenuItemControlTypeId)subRows.push_back(i.name);
        Expect(subRows==std::vector<std::wstring>{L"Ouvrir les 2 favoris",L"Mobbin",L"Luma"},"submenu exposes open-all and both saved links");Free(c);Free(m);
        OnUi([&]{Capture(child,L"menu-submenu");});
        PostMessageW(child,WM_KEYDOWN,VK_ESCAPE,0);
        Expect(WaitFor([]{int d=0;OnUi([&]{d=contextMenu.Depth();});return d==1;}),"Escape closes the submenu only");
        PostMessageW(menu,WM_KEYDOWN,VK_ESCAPE,0);
        Expect(WaitFor([]{int d=1;OnUi([&]{d=contextMenu.Depth();});return d==0;}),"Escape closes the menu");
        int focus=0;OnUi([&]{focus=keyboardFocus;});Expect(NativeChrome::IsBookmark(focus)&&NativeChrome::BookmarkIndex(focus)==1,"focus returns to the folder, not the page");}
    Free(items);

    // 3. Overflow at 560 DIP: hidden items listed off screen and invocable; » menu with icons in every row.
    OnUi([]{Resize(560);});
    items=Tree(window);int off=0;IUIAutomationElement* more=nullptr;for(auto& i:items){if(i.parent==L"Barre de favoris"&&Offscreen(i.e))++off;if(Aid(i.e)==L"Bookmarks.Overflow")more=i.e;}
    Expect(off>0,"560 DIP: overflowed bookmarks stay in the tree, off screen");
    Expect(more&&ExpandState(more)==1,"» button: ExpandCollapse");
    if(more)Expand(more);
    Expect(WaitFor([]{int d=0;OnUi([&]{d=contextMenu.Depth();});return d==1;}),"» opens the overflow menu");
    OnUi([&]{menu=contextMenu.LevelWindow(0);Capture(menu,L"menu-overflow-560");Capture(window,L"window-560-overflow-open");});
    {auto m=Tree(menu);std::vector<std::wstring> rows;for(auto& i:m)if(i.type==UIA_MenuItemControlTypeId)rows.push_back(i.name);
        Expect(!rows.empty()&&rows.back()==L"Gérer les favoris"&&std::find(rows.begin(),rows.end(),L"Autres favoris")!=rows.end(),"overflow menu: hidden items, Autres favoris, Gérer les favoris");Free(m);}
    PostMessageW(menu,WM_KEYDOWN,VK_ESCAPE,0);WaitFor([]{int d=1;OnUi([&]{d=contextMenu.Depth();});return d==0;});
    Free(items);
    OnUi([]{Capture(window,L"window-560");Resize(1180);Capture(window,L"window-1180");});

    // 4. Context menu order (Paper c-s207a4), opened on a bookmark.
    Later([]{RECT r=RegionScreen(NativeChrome::BookmarkBase);BookmarkContextMenu(demoPaper,{r.left,r.bottom},&r,true);});
    Expect(WaitFor([]{int d=0;OnUi([&]{d=contextMenu.Depth();});return d==1;}),"bookmark context menu opens");
    OnUi([&]{menu=contextMenu.LevelWindow(0);Capture(menu,L"menu-context");});
    {auto m=Tree(menu);std::vector<std::wstring> rows;for(auto& i:m)if(i.type==UIA_MenuItemControlTypeId)rows.push_back(i.name);
        const std::vector<std::wstring> expected{L"Ouvrir dans un nouvel onglet",L"Modifier…",L"Dupliquer",L"Copier le lien",L"Supprimer",L"Nouveau dossier…",L"Déplacer vers",L"Masquer la barre de favoris"};
        Expect(rows==expected,"context menu order is the user's (Ouvrir… / Modifier… Dupliquer Copier / Supprimer / Nouveau dossier… Déplacer vers Masquer)");Free(m);}
    PostMessageW(menu,WM_KEYDOWN,VK_ESCAPE,0);WaitFor([]{int d=1;OnUi([&]{d=contextMenu.Depth();});return d==0;});

    // 5. Popover: named fields, invalid address in danger red until valid.
    Later([]{EditBookmark(lemonde,RegionScreen('K'),true,true);});
    Expect(WaitFor([]{bool v=false;OnUi([&]{v=bookmarkPopover.Visible();});return v;}),"popover opens");
    {HWND name=nullptr,url=nullptr,pop=nullptr;OnUi([&]{name=bookmarkPopover.NameEdit();url=bookmarkPopover.UrlEdit();pop=bookmarkPopover.Window();});
        IUIAutomationElement* e=nullptr;uia->ElementFromHandle(name,&e);Expect(e&&Name(e)==L"Nom","Nom field named");if(e)e->Release();
        e=nullptr;uia->ElementFromHandle(url,&e);Expect(e&&Name(e)==L"Adresse","Adresse field named");if(e)e->Release();
        OnUi([&]{Capture(pop,L"popover-valid");});
        OnUi([]{bookmarkPopover.SetUrlForTest(L"lemonde..fr/international");bookmarkPopover.CommitForTest();SendMessageW(bookmarkPopover.UrlEdit(),EM_SETSEL,-1,-1);});
        COLORREF ink=0;bool error=false;OnUi([&]{error=bookmarkPopover.ShowsError();HDC dc=GetDC(url);SendMessageW(pop,WM_CTLCOLOREDIT,(WPARAM)dc,(LPARAM)url);ink=GetTextColor(dc);ReleaseDC(url,dc);RedrawWindow(pop,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);Capture(pop,L"popover-invalid");});
        Expect(error&&ink==RGB(0xFF,0x7B,0x6E),"invalid address: text in danger #FF7B6E, message shown");
        e=nullptr;uia->ElementFromHandle(url,&e);BSTR help=nullptr;if(e){e->get_CurrentHelpText(&help);e->Release();}
        Expect(Str(help).find(L"Adresse non valide")==0,"the error is the field's UIA help text");
        OnUi([&]{bookmarkPopover.SetUrlForTest(L"lemonde.fr/international");error=bookmarkPopover.ShowsError();HDC dc=GetDC(url);SendMessageW(pop,WM_CTLCOLOREDIT,(WPARAM)dc,(LPARAM)url);ink=GetTextColor(dc);ReleaseDC(url,dc);});
        Expect(!error&&ink==RGB(255,255,255),"valid again while typing: white text, message gone");
        OnUi([]{bookmarkPopover.KeyForTest(VK_ESCAPE);});}

    // 6. Drawer: List with levels and expand states.
    OnUi([]{ShowBookmarksDrawer(true);});
    {HWND d=nullptr;OnUi([&]{d=bookmarksDrawer.Window();Capture(window,L"window-drawer");Capture(d,L"drawer");});
        auto t=Tree(d);IUIAutomationElement* list=nullptr;int rows=0;IUIAutomationElement* folder=nullptr;IUIAutomationElement* search=nullptr;
        for(auto& i:t){if(i.type==UIA_ListControlTypeId&&i.name==L"Favoris")list=i.e;if(i.type==UIA_ListItemControlTypeId){++rows;if(i.name==L"Veille")folder=i.e;}if(i.type==UIA_EditControlTypeId)search=i.e;}
        Expect(list&&rows>0&&search&&Name(search)==L"Rechercher dans les favoris","drawer: search field and « Favoris » list");
        Expect(folder&&IntProp(folder,UIA_LevelPropertyId)==2&&ExpandState(folder)==1,"drawer folder row: level 2, collapsed");
        if(folder)Expand(folder);
        Expect(WaitFor([&]{size_t n=0;OnUi([&]{n=bookmarksDrawer.Rows().size();});return n>(size_t)rows;}),"Expand unfolds the folder in the drawer");
        Free(t);OnUi([&]{Capture(d,L"drawer-expanded");bookmarksDrawer.TypeForTest(L"monde");Capture(d,L"drawer-search");bookmarksDrawer.Hide(false,false);});}

    // 7. Notifications reach screen readers.
    OnUi([]{RemoveBookmark(demoPaper);});
    Expect(WaitFor([&]{return note.Has(L"Favori supprimé");}),"removal announced « Favori supprimé. Ctrl+Z pour annuler »");
    OnUi([]{
        HWND toast=bookmarkToast.Window();
        HRGN outline=CreateRectRgn(0,0,0,0);const int shape=GetWindowRgn(toast,outline);DeleteObject(outline);
        // Windows 10 / Server 2022 (CI) cannot report the corner preference: only Windows 11 is checked for ROUND.
        int corner=0;const bool reported=SUCCEEDED(DwmGetWindowAttribute(toast,33,&corner,sizeof(corner)));
        Expect(shape==ERROR&&(!reported||corner==2),"toast HWND is rounded by DWM (antialiased), never by an aliased GDI region");
        Capture(toast,L"toast-undo");Capture(window,L"window-toast");UndoBookmark();
    });
    Expect(WaitFor([&]{return note.Has(L"Favori restauré");}),"undo announced « Favori restauré »");
    if(uia5&&root)uia5->RemoveNotificationEventHandler(root,&note);
    if(root)root->Release();if(uia5)uia5->Release();uia->Release();uia=nullptr;CoUninitialize();
    PostThreadMessageW(uiThread,WM_QUIT,0,0);
}

// ---- UI-thread interactions -----------------------------------------------------------
static void Interactions(){
    Resize(1180);
    // Keyboard: one Tab stop for the whole band; arrows rove; Ctrl+Maj+→ moves.
    const auto order=chrome.FocusOrder();int bandStops=0;for(int id:order)if(NativeChrome::IsBookmark(id)||id=='O')++bandStops;
    Expect(bandStops==1,"one Tab stop for the whole band");
    SetFocus(window);keyboardFocus=NativeChrome::BookmarkBase;BandKey(VK_RIGHT);
    Expect(keyboardFocus==NativeChrome::BookmarkBase+1&&chrome.bandRover==1,"Right moves to the next item and becomes the Tab stop");
    BandKey(VK_END);Expect(keyboardFocus=='O'||keyboardFocus==NativeChrome::BookmarkBase+chrome.band.visible-1,"End reaches the last item (» included)");
    keyboardFocus=NativeChrome::BookmarkBase;const BM::Id first=chrome.bandItems[0].id;
    ShiftBookmark(first,1);Expect(bookmarks.IndexOf(first)==1,"Ctrl+Maj+→ equivalent: moved one step");UndoBookmark();Expect(bookmarks.IndexOf(first)==0,"move undone");
    // Drag: press on item 0, move past the threshold onto item 2, release.
    auto px=[](float v){return (int)std::lround(v*GetDpiForWindow(window)/96.f);};
    auto r0=chrome.BandRect(0),r2=chrome.BandRect(3);const float y=(r0.top+r0.bottom)/2;
    WndProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(r0.left+10),px(y)));
    WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(r0.left+30),px(y)));
    WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(r2.left+4),px(y)));
    Capture(window,L"window-drag");
    WndProc(window,WM_LBUTTONUP,0,MAKELPARAM(px(r2.left+4),px(y)));
    Expect(bookmarks.IndexOf(first)>0&&!bookmarkUndo.empty(),"drag reorders the band live and is undoable");
    UndoBookmark();Expect(bookmarks.IndexOf(first)==0,"drag undone");
    // Drop onto a folder's middle: moves inside.
    {const BM::Id gh=chrome.bandItems[2].id;auto a=chrome.BandRect(2),f=chrome.BandRect(3);
        WndProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(a.left+10),px(y)));WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(a.left+40),px(y)));
        WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px((f.left+f.right)/2),px(y)));WndProc(window,WM_LBUTTONUP,0,MAKELPARAM(px((f.left+f.right)/2),px(y)));
        Expect(bookmarks.Find(gh)->parent==projet,"dropping on a folder's middle moves the bookmark inside");UndoBookmark();Expect(bookmarks.Find(gh)->parent==BM::BarRoot,"drop undone");}
    // Delete from the keyboard, then undo.
    keyboardFocus=NativeChrome::BookmarkBase+1;const BM::Id folder=chrome.bandItems[1].id;BandKey(VK_DELETE);
    Expect(!bookmarks.Find(folder)&&bookmarkToast.Visible()&&bookmarkToast.Message().find(L"Veille")!=std::wstring::npos,"Suppr removes the folder at once, toast names it");
    Command(BookmarkUndoCommand);Expect(bookmarks.Find(folder)&&bookmarks.IndexOf(folder)==1&&bookmarks.CountLinks(folder)==4,"Ctrl+Z restores folder, place and contents");
    // Duplicate and remove the copy.
    const BM::Id copy=DuplicateBookmark(lemonde);Expect(copy&&bookmarks.IndexOf(copy)==bookmarks.IndexOf(lemonde)+1,"Dupliquer inserts the copy right after");UndoBookmark();Expect(!bookmarks.Find(copy),"duplicate undone");
    // Reduced motion: the popover arrives fully opaque at once.
    settings.reduceMotion=true;ApplyReducedMotion();
    EditBookmark(lemonde,RegionScreen('K'),true,false);BYTE alpha=0;DWORD flags=0;GetLayeredWindowAttributes(bookmarkPopover.Window(),nullptr,&alpha,&flags);
    Expect(alpha==255,"reduced motion: popover final state on the first frame");bookmarkPopover.Hide();
    settings.reduceMotion=false;ApplyReducedMotion();
    // 560 DIP: the popover stays inside the window.
    Resize(560);EditBookmark(lemonde,RegionScreen('K'),true,false);RECT p{},c=ClientScreen();GetWindowRect(bookmarkPopover.Window(),&p);
    Expect(p.left>=c.left+S(6)-1&&p.right<=c.right-S(6)+1,"560 DIP: popover fully inside the client area");Capture(bookmarkPopover.Window(),L"popover-560");bookmarkPopover.Hide();Resize(1180);
    // Ctrl+D on a page not yet saved: added at once, star filled.
    pages[0]->uri=L"https://example.org/nouvelle-page";pages[0]->title=L"Nouvelle page";BuildChrome();
    Expect(!chrome.starred,"star empty on an unsaved page");
    BookmarkStar(false);Expect(chrome.starred&&!bookmarks.Matching(L"https://example.org/nouvelle-page").empty()&&bookmarkPopover.Visible(),"Ctrl+D saves at once and opens « Favori ajouté »");
    Capture(bookmarkPopover.Window(),L"popover-added");Capture(window,L"window-added");bookmarkPopover.KeyForTest(VK_ESCAPE);
}
static void Persistence(){
    // Restart: what was saved reads back identical; a damaged file is kept aside and recovered from the backup.
    namespace fs=std::filesystem;smoke=false;dataDir=out+L"\\profile";fs::remove_all(dataDir);fs::create_directories(dataDir);
    SaveBookmarks();
    {std::ifstream in(fs::path(BookmarksFile()),std::ios::binary);bool damaged=true;auto again=BM::Store::Read(in,&damaged);Expect(!damaged&&again==bookmarks,"restart: the saved file reads back identical");}
    const auto before=bookmarks;LoadBookmarks();Expect(bookmarks==before&&fs::exists(BookmarksFile()+L".bak"),"launch keeps bookmarks.bak of the last clean file");
    {std::string text;{std::ifstream in(fs::path(BookmarksFile()),std::ios::binary);text.assign(std::istreambuf_iterator<char>(in),{});}
        std::ofstream outf(fs::path(BookmarksFile()),std::ios::binary|std::ios::trunc);outf<<text.substr(0,text.size()/2);}
    LoadBookmarks();bool aside=false;for(auto& e:fs::directory_iterator(dataDir))if(e.path().filename().wstring().rfind(L"bookmarks.damaged-",0)==0)aside=true;
    Expect(aside&&bookmarks.Size()>0,"truncated file: launch succeeds, damaged copy kept, valid lines loaded");
    fs::remove(BookmarksFile());LoadBookmarks();Expect(bookmarks==before,"missing file: restored from bookmarks.bak");
    // "Effacer les données" never touches bookmarks.
    std::string hash1;{std::ifstream in(fs::path(BookmarksFile()),std::ios::binary);hash1.assign(std::istreambuf_iterator<char>(in),{});}
    ClearLocalTraces();std::string hash2;{std::ifstream in(fs::path(BookmarksFile()),std::ios::binary);hash2.assign(std::istreambuf_iterator<char>(in),{});}
    Expect(hash1==hash2&&bookmarks==before,"clearing browsing data leaves the bookmarks file identical");
    smoke=true;
}

int wmain(int argc,wchar_t** argv){setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<3){std::fputs("usage: bookmarks_ui_test <root> <out>\n",stderr);return 2;}
    base=argv[1];out=argv[2];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);instance=GetModuleHandleW(nullptr);uiThread=GetCurrentThreadId();
    smoke=true; // never reads or writes a profile except in Persistence(), which uses its own folder
    for(auto name:NativeChrome::FontFiles)AddFontResourceExW((base+L"\\assets\\fonts\\"+name).c_str(),FR_PRIVATE,nullptr);
    WNDCLASSW hc{};hc.lpfnWndProc=HelperProc;hc.hInstance=instance;hc.lpszClassName=L"BookmarksUiTestHelper";RegisterClassW(&hc);
    helper=CreateWindowW(hc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.lpszClassName=L"BookmarksUiTest";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    CreateWindowExW(0,wc.lpszClassName,L"browser",WS_POPUP|WS_CLIPCHILDREN,40,40,MulDiv(1180,GetDpiForSystem(),96),MulDiv(815,GetDpiForSystem(),96),nullptr,nullptr,instance,nullptr);
    if(!window||FAILED(chrome.Init(window,base))){std::fputs("FAIL: window\n",stderr);return 1;}
    motion.SetReducedMotion(false);chrome.SetMotion(&motion);contextMenu.Init(instance,window,base);UpdateFont();CreateBookmarkSurfaces();
    Seed();
    auto t=std::make_shared<Tab>();t->title=L"Le Monde — International";t->uri=L"https://www.lemonde.fr/";t->home=false;pages={t};current=0;
    ShowWindow(window,SW_SHOW);Layout();Update();UpdateWindow(window);
    Geometry();Renders();
    std::thread client(Client);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    client.join();
    Interactions();Persistence();
    bookmarkPopover.Hide();bookmarkToast.HideNow();
    DestroyWindow(window);DestroyWindow(helper);
    if(failures){std::fprintf(stderr,"%d bookmark UI check(s) failed\n",failures);return 1;}
    std::puts("PASS: bookmarks band, star, menus (UIA), popover, drawer, toast, keyboard, drag, undo, persistence");return 0;
}
