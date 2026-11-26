// Atelier B: a tab dragged onto the favorites bar (top layout), through the PRODUCTION window procedure, on a window
// that is never shown (no visible window, no input sent anywhere: WndProc is called directly, as tests/ui/tab_drag_test.cpp
// does) and the chrome painted off screen (Chrome::InitOffscreen). Demo data only, never a profile (smoke: nothing read
// or written). Checks the drag state machine and writes the drag-state renders (chrome + the drag overlay's preview).
//   1. Over the bar: the tab becomes the preview (not cut: drawn in the overlay, below the strip), its strip place hollow,
//      no zone highlight (no blue around the favorites: Val, 26 nov.), the grey place (board 08) opened where the release
//      would add it; back up: the strip reorder resumes.
//   2. Insertion index left / between / right, a folder's middle (into it), the 2 DIP gap.
//   3. Release: added at that index, one undo step, the edit panel NOT opened; into a folder: at its end.
//   4. Duplicate (the page already on the bar / in that folder): no place, the release adds nothing.
//   5. Échap: nothing added, the strip order restored, the bar at rest; lost capture too.
//   6. From the page (split lift) back up to the bar, and down again into the lift.
//   7. Full bar: the new one goes behind « » » (the button takes the folder drop look).
//   8. Reduced motion: no lift growth, the bar's springs jump; the place still fades.
//   9. 150 % (pointer in device pixels): the same targets.
// Test-unit shims (production code unchanged): nothing here reaches the real desktop — the mouse capture, the cursor
// shape and the foreground are recorded, never applied; GetDpiForWindow answers fixtureDpi (no display setting touched).
#include <windows.h>
static UINT fixtureDpi=96;
static UINT FixtureGetDpiForWindow(HWND){return fixtureDpi;}
static HWND fixtureCapture=nullptr;static HCURSOR fixtureCursor=nullptr;static int foregroundRequests=0;
static HWND FixtureSetCapture(HWND h){HWND o=fixtureCapture;fixtureCapture=h;return o;}
static BOOL FixtureReleaseCapture(){fixtureCapture=nullptr;return TRUE;}
static HWND FixtureGetCapture(){return fixtureCapture;}
static HCURSOR FixtureSetCursor(HCURSOR c){HCURSOR o=fixtureCursor;fixtureCursor=c;return o;}
static BOOL FixtureSetForegroundWindow(HWND){++foregroundRequests;return TRUE;}
#define GetDpiForWindow FixtureGetDpiForWindow
#define SetCapture FixtureSetCapture
#define ReleaseCapture FixtureReleaseCapture
#define GetCapture FixtureGetCapture
#define SetCursor FixtureSetCursor
#define SetForegroundWindow FixtureSetForegroundWindow
#include "../../src/main.cpp"
#undef GetDpiForWindow
#include <cstdio>

static int failures=0,checks=0;
static void Expect(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}else std::printf("ok   %s\n",what);}
static std::wstring out;
static float K(){return fixtureDpi/96.f;}
static void Down(float x,float y){WndProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((int)std::lround(x*K()),(int)std::lround(y*K())));}
static void Move(float x,float y){WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM((int)std::lround(x*K()),(int)std::lround(y*K())));}
static void Up(float x,float y){WndProc(window,WM_LBUTTONUP,0,MAKELPARAM((int)std::lround(x*K()),(int)std::lround(y*K())));}
static NativeChrome::Motion::Time clock0=NativeChrome::Motion::Now();
// The paint clock never goes back (springs read it): a step whose time is below the last one starts from there.
static double epochMs=0,lastMs=0;
static NativeChrome::Motion::Time At(double ms){if(ms+epochMs<lastMs)epochMs=lastMs-ms;lastMs=ms+epochMs;return clock0+std::chrono::duration_cast<NativeChrome::Motion::Clock::duration>(std::chrono::duration<double,std::milli>(ms+epochMs));}
static void Paint(double ms){std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;for(auto& p:pages)icons.push_back(p->favicon.image);
    chrome.SetFavicons(std::move(icons));chrome.sideHome.assign(pages.size(),false);chrome.sideDetails.assign(pages.size(),L"");
    chrome.Paint(Titles(),current,pages[current]->uri,false,0,0,nullptr,At(ms));}
// The window as it would look: the chrome (offscreen), a white page card where the page sits, and the drag overlay's
// frame (the preview) over both, as the layered overlay window covers the client area.
static void Shot(const wchar_t* name,double ms){
    Paint(ms);auto* t=chrome.RenderTarget();if(!t)return;
    t->BeginDraw();ID2D1SolidColorBrush* b=nullptr;t->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&b);
    const auto c=ContentBox();const float rr=chrome.sidebar?8.f:6.f;if(b){t->FillRoundedRectangle(D2D1::RoundedRect({c.left,c.top,c.right,c.bottom},rr,rr),b);b->Release();}
    if(splitAnim.frame.card.shown){splitOverlay.painter.write=chrome.WriteFactory();splitOverlay.painter.fonts=chrome.FontCollection();splitOverlay.painter.Draw(t,splitAnim.frame);}
    t->EndDraw();
    const std::wstring path=out+L"\\"+name+L".png";Expect(SUCCEEDED(chrome.SavePng(path.c_str())),"render saved");std::wprintf(L"     png %ls\n",path.c_str());
}
static void Settle(){for(int i=0;i<40;++i){splitAnim.last=At(i*50.0);SplitFrame(At(i*50.0+50));}}   // the overlay's springs (display-paced in the app)
// The offscreen chrome's pixel (DIP; 32bpp PBGRA read as 0xAARRGGBB) and its channels; « neutral » = a grey (no tint).
static UINT32 Px(float x,float y){return chrome.SurfacePixel(x,y);}
static int Ch(UINT32 p,int shift){return (int)((p>>shift)&255);}
static bool Neutral(UINT32 p){return std::abs(Ch(p,16)-Ch(p,0))<=1&&std::abs(Ch(p,8)-Ch(p,0))<=1;}
static std::shared_ptr<Tab> MakeTab(const wchar_t* title,const wchar_t* uri){auto t=std::make_shared<Tab>();t->title=title;t->uri=uri;t->home=false;return t;}
static BM::Id bmVeille,bmPaper,bmMobbin,bmProjet,bmMdn;
static void Seed(bool full=false){
    bookmarks=BM::Store{};bookmarkUndo.clear();
    if(!smoke){bookmarksBase=BM::Store{};bookmarksSeen={};DeleteFileW(BookmarksFile().c_str());}   // a fresh store each time: no merge with an earlier step's saved file
    bmVeille=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Veille",1);
    bmPaper=bookmarks.Add(BM::BarRoot,BM::End,L"Paper",L"https://paper.design/",1);
    bmMobbin=bookmarks.Add(BM::BarRoot,BM::End,L"Mobbin",L"https://mobbin.com/",1);
    bmProjet=bookmarks.AddFolder(BM::BarRoot,BM::End,L"Projet Lisière",1);
    bmMdn=bookmarks.Add(BM::BarRoot,BM::End,L"MDN Web Docs",L"https://developer.mozilla.org/",1);
    bookmarks.Add(bmProjet,BM::End,L"Issues",L"https://github.com/VALXXII/browser/issues",1);
    if(full)for(int i=0;i<14;++i)bookmarks.Add(BM::BarRoot,BM::End,L"Lecture du matin "+std::to_wstring(i+1),L"https://example.com/read/"+std::to_wstring(i),1);
    SyncBookmarkChrome();chrome.Build((int)pages.size(),current,nullptr,At(0));
}
static float BandMid(int i){const auto r=chrome.BandRect(i);return (r.left+r.right)/2;}
static int Count(BM::Id folder){return (int)bookmarks.Find(folder)->children.size();}

int wmain(int argc,wchar_t** argv){setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<3){std::fputs("usage: tab_favorite_drag_test <root> <out>\n",stderr);return 2;}
    base=argv[1];out=argv[2];std::filesystem::create_directories(out);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);instance=GetModuleHandleW(nullptr);smoke=true;
    window=CreateWindowExW(0,L"STATIC",L"favorites drag test",WS_POPUP,0,0,1180,815,nullptr,nullptr,instance,nullptr);   // never shown
    if(!window||FAILED(chrome.InitOffscreen(base,96,1180,815))){std::fputs("FAIL: offscreen chrome\n",stderr);return 1;}
    chrome.width=1180;chrome.height=815;chrome.SetMotion(&motion);motion.SetReducedMotion(false);
    settings.bookmarksBar=BrowserSettings::BookmarksBar::Always;
    auto a=MakeTab(L"Planète — Le Monde",L"https://lemonde.fr/planete"),b=MakeTab(L"VALXXII/browser · GitHub",L"https://github.com/VALXXII/browser"),c=MakeTab(L"Paper",L"https://paper.design/");
    pages={a,b,c};current=0;Seed();
    Expect(chrome.bandShown&&chrome.band.visible==5,"bar shown with its 5 items");
    const float bandY=100;   // the bar's row (84..116)
    Paint(0);const UINT32 restL=Px(3,bandY),restR=Px(chrome.width-3,bandY);   // the bar's margins at rest (canvas)

    // ---- 1. Over the bar: the preview, the hollow strip place, the opened place (no zone highlight) -------------------
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}
    Move(chrome.strip.Surface(1).left+60,20);
    Expect(tabDrag.active&&!tabDrag.favorite&&chrome.dragIndex>=0,"in the strip: reorder as before");
    Move(BandMid(2)+20,bandY);   // right half of Mobbin
    Expect(tabDrag.favorite&&chrome.bandTab.on,"over the bar: favorites mode");
    Expect(chrome.liftedIndex==TabIndexOf(b)&&chrome.dragIndex==-1,"its strip place stays hollow, no strip reorder");
    Expect(pages[1]==b,"the strip order is untouched");
    {const auto& box=splitAnim.frame.card.box;Expect(splitAnim.frame.card.shown&&splitAnim.frame.card.tab,"the preview is the overlay's tab card");
     Expect(box.top>=NativeChrome::Design::TabStrip::regionBottom&&box.top<=bandY&&box.bottom>bandY,"the preview follows the pointer below the strip (never held in it)");
     Expect(std::fabs((box.right-box.left)-chrome.strip.tab)<.5f&&std::fabs((box.bottom-box.top)-32)<.5f,"the preview has the tab's size (tab width x 32)");
     Expect(std::fabs(box.left-(BandMid(2)+20-tabDrag.grab))<.5f&&std::fabs(box.top-(bandY-tabDrag.favGrabY))<.5f,"1:1 from the grab point");}
    Expect(tabDrag.fav.valid&&tabDrag.fav.slot==3&&chrome.bandTab.slot,"insertion index 3 (after Mobbin), its place opened");
    Expect(chrome.bandTab.left.size()==5&&chrome.bandTab.left[3]>chrome.BandRect(3).left&&chrome.bandTab.left[2]==chrome.BandRect(2).left,"the items after it step aside, the ones before stay");
    Expect(std::fabs(chrome.bandTab.slotLeft-(chrome.BandRect(2).right+BookmarksBar::Layout::gap))<.5f,"the place opens right after Mobbin");
    Expect(splitAnim.cardScale.target==102,"lifted: grows to x1.02 around the grab point");
    Paint(0);Paint(60);Shot(L"top-01-entree-60ms",60);
    Settle();Shot(L"top-02-entre-deux-favoris",1000);
    Expect(chrome.bandTabGlow.Value(At(1000))>.99f,"the place is fully shown once settled");
    Paint(1000);   // the chrome alone (the preview lives in the overlay): the bar around its items, and the place
    Expect(Px(3,bandY)==restL&&Px(chrome.width-3,bandY)==restR&&Neutral(Px(3,bandY)),"no zone highlight: the bar's margins stay the canvas (no blue, Val 26 nov.)");
    {const UINT32 p=Px(chrome.bandTab.slotLeft+chrome.bandTab.slotWidth/2,BookmarksBar::Layout::top+4);Expect(Neutral(p)&&Ch(p,16)>Ch(restL,16)+4,"the place is drawn: board 08's grey hollow slot");}
    // Left of the first, the 2 DIP gap, past the last.
    Move(2,bandY);Expect(tabDrag.fav.slot==0,"left of the first: index 0");
    {const auto r=chrome.BandRect(1);Move(r.right+1,bandY);Expect(tabDrag.fav.slot==2,"in the gap after Paper: index 2");}
    Move(chrome.BandRect(4).right+200,bandY);Expect(tabDrag.fav.slot==5&&chrome.bandTab.slot,"past the last: at the end");
    Shot(L"top-03-a-la-fin",1000);
    // A folder's middle: into it, the folder's drop look, no place opened.
    Move(BandMid(3),bandY);Expect(tabDrag.fav.valid&&tabDrag.fav.into==3&&tabDrag.fav.parent==bmProjet&&chrome.bandDropInto==3&&!chrome.bandTab.slot,"a folder's middle: into it (folder look, no place)");
    Paint(1000);Paint(1400);Shot(L"top-04-dans-un-dossier",1400);
    // Back up to the strip: the bar rests, reorder resumes.
    Move(200,20);
    Expect(!tabDrag.favorite&&!chrome.bandTab.on&&chrome.liftedIndex==-1&&chrome.dragIndex>=0&&chrome.bandDropInto==-1,"back up: the bar rests, the strip reorder resumes");
    Expect(!splitAnim.frame.card.shown,"the preview is gone");
    Paint(1500);Paint(3000);Expect(chrome.bandTabGlow.Value(At(3000))<.01f,"the place has faded");

    // ---- 3. Release on the bar: added there, no edit panel, one undo step ---------------------------------------------
    Move(BandMid(2)+20,bandY);Expect(tabDrag.favorite&&tabDrag.fav.slot==3,"over the bar again");
    const int before=Count(BM::BarRoot);popoverReturn=0x5A5A;const size_t undo=bookmarkUndo.size();
    Paint(4000);Paint(5000);   // the hovered frames before the release (the app paints them continuously)
    Up(BandMid(2)+20,bandY);
    Paint(5000);Paint(5060);Shot(L"top-05-depose-60ms",5060);
    Expect(Count(BM::BarRoot)==before+1,"release: one favorite added");
    {const auto* n=bookmarks.Find(bookmarks.Root(BM::BarRoot).children[3]);Expect(n&&n->kind==BM::Kind::Link&&n->url==b->uri&&n->title==b->title,"at index 3, with the tab's title and address");}
    Expect(popoverReturn==0x5A5A,"the edit panel is NOT opened (EditBookmark never ran)");
    Expect(bookmarkUndo.size()==undo+1&&bookmarkUndo.back().steps.size()==1&&bookmarkUndo.back().steps[0].kind==BookmarkStep::Created,"one undo step « Created »");
    Expect(!tabDrag.tab&&!chrome.bandTab.on&&chrome.liftedIndex==-1&&pages[1]==b,"drag over, the tab back in its place, the bar at rest");
    Expect(chrome.band.visible==6,"the bar shows the new favorite");
    Paint(6000);Shot(L"top-06-depose",6000);

    // ---- 4. Duplicates ---------------------------------------------------------------------------------------------
    Seed();
    {auto s=chrome.strip.Surface(2);Down(s.left+24,20);}Move(BandMid(2)+20,bandY);   // « Paper » tab: already on the bar
    Expect(tabDrag.favorite&&!tabDrag.fav.valid&&tabDrag.fav.duplicate==1&&chrome.bandTab.duplicate==1&&!chrome.bandTab.slot,"the page is already on the bar: no place, the existing item pointed at");
    Expect(fixtureCursor==LoadCursor(nullptr,IDC_NO),"the pointer says no (as the split refusal does)");
    Paint(0);Paint(1000);Settle();Shot(L"top-07-doublon",1000);
    {const int n=Count(BM::BarRoot);Up(BandMid(2)+20,bandY);Expect(Count(BM::BarRoot)==n,"a duplicate release adds nothing");}
    Expect(!tabDrag.tab&&!chrome.bandTab.on,"drag over after a duplicate");
    // Into a folder that already holds it.
    pages.push_back(MakeTab(L"Issues · VALXXII/browser",L"https://github.com/VALXXII/browser/issues"));chrome.Build((int)pages.size(),current,nullptr,At(0));
    {auto s=chrome.strip.Surface(3);Down(s.left+24,20);}Move(BandMid(3),bandY);
    Expect(!tabDrag.fav.valid&&tabDrag.fav.duplicate==3&&chrome.bandDropInto==-1,"already in that folder: refused, the folder pointed at");
    Move(BandMid(0),bandY);Expect(tabDrag.fav.valid&&tabDrag.fav.into==0,"another folder takes it");
    {const int n=Count(bmVeille);Up(BandMid(0),bandY);Expect(Count(bmVeille)==n+1&&bookmarks.Find(bookmarks.Find(bmVeille)->children.back())->url==pages[3]->uri,"into a folder: added at its end");}
    pages.pop_back();chrome.Build((int)pages.size(),current,nullptr,At(0));

    // ---- 5. Échap and a lost capture ---------------------------------------------------------------------------------
    Seed();
    {auto s=chrome.strip.Surface(0);Down(s.left+24,20);}Move(chrome.strip.Surface(2).left+40,20);   // reordered to the end first
    Expect(pages[2]==a,"reordered in the strip on the way");
    Move(BandMid(1),bandY);Expect(tabDrag.favorite,"then over the bar");
    {const int n=Count(BM::BarRoot);WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);
     Expect(Count(BM::BarRoot)==n&&!tabDrag.tab&&!tabDrag.favorite&&!chrome.bandTab.on&&chrome.bandDropInto==-1,"Échap: nothing added, the bar at rest");
     Expect(pages[0]==a,"Échap: the strip order from before the press comes back");}
    WndProc(window,WM_LBUTTONUP,0,MAKELPARAM(10,10));Expect(Count(BM::BarRoot)==5,"the release after Échap adds nothing");
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(BandMid(1),bandY);WndProc(window,WM_CANCELMODE,0,0);
    Expect(!tabDrag.tab&&!chrome.bandTab.on&&Count(BM::BarRoot)==5,"a lost capture cancels too");

    // ---- 6. Page (split lift) <-> bar ------------------------------------------------------------------------------
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(400,400);
    Expect(tabDrag.lifted&&!tabDrag.favorite,"down into the page: the split lift");
    Move(BandMid(2),bandY);Expect(tabDrag.favorite&&!tabDrag.lifted&&splitAnim.frame.card.tab&&!splitAnim.frame.plate.shown,"back up onto the bar: favorites (the split preview gives way)");
    Move(400,400);Expect(tabDrag.lifted&&!tabDrag.favorite&&!chrome.bandTab.on,"down again: the split lift");
    WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);FinishSplitAnimation();

    // ---- 7. Full bar ------------------------------------------------------------------------------------------------
    Seed(true);Expect(chrome.band.overflow,"a full bar (« » » shown)");
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(chrome.band.overflowLeft-4,bandY);
    Expect(tabDrag.fav.valid&&tabDrag.fav.slot==chrome.band.visible&&chrome.bandTab.toOverflow&&!chrome.bandTab.slot,"past the last shown item of a full bar: it goes behind « » »");
    Paint(0);Paint(1000);Settle();Shot(L"top-08-barre-pleine",1000);
    Move(chrome.BandRect(0).left+2,bandY);Expect(chrome.bandTab.slot&&chrome.bandTab.shown[0]&&!chrome.bandTab.shown[chrome.band.visible-1],"at the start of a full bar: its place opens, the last shown item goes behind « » »");
    Paint(1000);Paint(2000);Shot(L"top-09-barre-pleine-debut",2000);
    {const int n=Count(BM::BarRoot);Up(chrome.BandRect(0).left+2,bandY);Expect(Count(BM::BarRoot)==n+1&&bookmarks.Root(BM::BarRoot).children[0]!=bmVeille,"added first");}

    // ---- 8. Reduced motion --------------------------------------------------------------------------------------------
    Paint(3000);Paint(4000);Seed();motion.SetReducedMotion(true);   // the place at rest first
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(BandMid(2)+20,bandY);
    Expect(tabDrag.favorite&&splitAnim.cardScale.target==100,"reduced motion: no lift growth");
    Paint(5000);
    {const auto it=chrome.bandStep.find(bookmarks.Root(BM::BarRoot).children[3]);Expect(it!=chrome.bandStep.end()&&it->second.l.Value(At(5000))==chrome.bandTab.left[3],"reduced motion: the items are at once where they go");}
    Expect(chrome.bandTabGlow.Value(At(5000))<.99f&&chrome.bandTabGlow.Value(At(5200))>.99f,"reduced motion: the place still fades in (short)");
    WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);motion.SetReducedMotion(false);

    // ---- 9. 150 %: the pointer in device pixels, the same targets ------------------------------------------------------
    fixtureDpi=144;Seed();
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(chrome.strip.Surface(1).left+60,20);Move(BandMid(2)+20,bandY);
    Expect(tabDrag.favorite&&tabDrag.fav.slot==3,"150 %: same index over the same item");
    {const auto& box=splitAnim.frame.card.box;Expect(std::fabs(box.top-(bandY-tabDrag.favGrabY))<1&&box.bottom>bandY,"150 %: the preview at the pointer (DIP)");}
    WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);fixtureDpi=96;

    // A home tab (new tab page) is not a favorite: no favorites mode, the strip as before.
    Seed();pages[1]->home=true;
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(chrome.strip.Surface(1).left+60,20);Move(BandMid(2),bandY);
    Expect(!tabDrag.favorite&&!chrome.bandTab.on,"a new tab page never becomes a favorite");
    WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);pages[1]->home=false;
    Expect(foregroundRequests==0&&!fixtureCapture,"never asked for the foreground, no capture left");

    // ---- 10. Side panel and compact: the column's grid ------------------------------------------------------------------
    for(const auto position:{BrowserSettings::TabsPosition::Side,BrowserSettings::TabsPosition::Compact}){
        const bool compact=position==BrowserSettings::TabsPosition::Compact;const std::wstring tag=compact?L"compact":L"panneau";
        auto name=[&](const wchar_t* s){return tag+L"-"+s;};
        auto shot=[&](const wchar_t* s,double ms){const auto n=name(s);Shot(n.c_str(),ms);};
        // A smoke run keeps the top layout (SidePanelWanted): the column needs a data folder of its own, a throwaway one
        // under the output (bookmarks saved there; never a profile), as tests/ui/sidebar_split_drag_test.cpp does.
        smoke=false;dataDir=out+L"\\data";std::filesystem::create_directories(dataDir);DeleteFileW(BookmarksFile().c_str());
        settings.tabsPosition=position;ApplyLayout();Seed();Layout();BuildChrome();
        const auto* Lp=&chrome.SideColumn();
        Expect(chrome.sidebar&&!Lp->gridBlock.Empty()&&chrome.SideBookmarkTiles(*Lp)==5&&(Lp->mode==NativeChrome::Sidebar::Mode::Compact)==compact,compact?"compact: the column's grid shows the 5 favorites":"side panel: the column's grid shows the 5 favorites");
        auto rowAt=[&](int i){return chrome.SideColumn().tabs[i];};
        auto tile=[&](int i){return chrome.SideBookmarkTile(chrome.SideColumn(),i);};
        auto press=[&](int i){const auto r=rowAt(i);Down(r.left+std::min(40.f,r.Width()/2),(r.top+r.bottom)/2);};
        const auto list=Lp->tabsView;
        // Where a zone highlight would show (left of the grid in the side panel; on the first tile's edge in the compact
        // column, whose tiles reach its edges): its pixel at rest.
        const auto gb=Lp->gridBlock;const float zx=compact?gb.left+1:gb.left-3,zy=compact?(tile(0).top+tile(0).bottom)/2:(gb.top+gb.bottom)/2;
        Paint(0);const UINT32 restZone=Px(zx,zy);
        // Up through the list: the reorder as before, the lifted row drawn over the list's edge (not cut by it).
        press(1);{const auto r=rowAt(1);Move(r.left+std::min(40.f,r.Width()/2),list.top+6);}
        Expect(tabDrag.active&&!tabDrag.favorite&&chrome.sidebarDrag==1,"in the list: the reorder as before");
        {const auto r=rowAt(1);Move(r.left+std::min(40.f,r.Width()/2),list.top+2);}
        Paint(0);Paint(400);shot(L"00-rangee-au-bord-de-la-liste",400);
        Expect(chrome.sidebarDragY<list.top,"the row rises above the list's top (it is drawn outside the list's clip)");
        // Over the grid: the preview, the hollow row, the place (no zone highlight).
        const auto t2=tile(2);const float tx=compact?(t2.left+t2.right)/2:t2.right-3,ty=compact?t2.bottom-3:(t2.top+t2.bottom)/2;
        Move(tx,ty);
        Expect(tabDrag.favorite&&chrome.gridTab.on&&chrome.liftedIndex==1&&chrome.sidebarDrag==-1,"over the grid: favorites mode, the row's place hollow");
        Expect(tabDrag.fav.valid&&tabDrag.fav.slot==3&&chrome.gridTab.slot,"insertion index 3 (after the third tile), its place opened");
        {const auto& box=splitAnim.frame.card.box;Expect(splitAnim.frame.card.tab&&std::fabs(box.top-(ty-tabDrag.favGrabY))<.5f&&box.left<tx&&box.right>tx,"the preview at the pointer, in the overlay");
         Expect(std::fabs((box.right-box.left)-tabDrag.cardW)<.5f&&tabDrag.cardW<=240,"the preview fits its title (as the column's split card)");}
        Expect(compact?chrome.gridTab.tiles[3].top>tile(3).top:(chrome.gridTab.tiles[3].left>tile(3).left||chrome.gridTab.tiles[3].top>tile(3).top),"the tiles after it step aside");
        Paint(0);Paint(60);shot(L"01-entree-60ms",60);Paint(1000);Settle();shot(L"02-entre-deux-favoris",1000);
        Paint(1000);   // the chrome alone (the preview lives in the overlay)
        Expect(Px(zx,zy)==restZone&&Neutral(Px(zx,zy)),"no zone highlight around the grid (no blue, Val 26 nov.)");
        {const auto& pl=chrome.gridTab.place;const UINT32 p=Px((pl.left+pl.right)/2,(pl.top+pl.bottom)/2);Expect(chrome.gridTab.slot&&Neutral(p)&&Ch(p,16)>Ch(Px(zx,list.bottom-2),16),"the place is drawn: board 08's grey hollow tile");}
        // Before the first, a folder's middle, a duplicate.
        {const auto t0=tile(1);Move(compact?(t0.left+t0.right)/2:t0.left+2,compact?t0.top+2:(t0.top+t0.bottom)/2);Expect(tabDrag.fav.valid&&tabDrag.fav.slot==1,"before the second tile: index 1");}
        {const auto f=tile(0);Move((f.left+f.right)/2,(f.top+f.bottom)/2);Expect(tabDrag.fav.valid&&tabDrag.fav.into==0&&chrome.bandDropInto==0&&!chrome.gridTab.slot,"a folder's middle: into it (folder look)");}
        Paint(1000);Paint(1500);shot(L"03-dans-un-dossier",1500);
        // Back down into the list: the row again, the grid at rest.
        {const auto r=rowAt(2);Move(r.left+20,(r.top+r.bottom)/2);}
        Expect(!tabDrag.favorite&&!chrome.gridTab.on&&chrome.sidebarDrag==1&&chrome.liftedIndex==-1,"back into the list: the row follows again, the grid rests");
        // Release on the grid: added at the place, no edit panel, one undo step.
        Move(tx,ty);const int before=Count(BM::BarRoot);popoverReturn=0x5A5A;const size_t undo=bookmarkUndo.size();
        Paint(20000);Paint(21000);   // the hovered frames before the release
        Up(tx,ty);
        Paint(21000);Paint(21060);shot(L"04-depose-60ms",21060);
        Expect(Count(BM::BarRoot)==before+1&&bookmarks.Find(bookmarks.Root(BM::BarRoot).children[3])->url==pages[1]->uri,"release: added at index 3");
        Expect(popoverReturn==0x5A5A&&bookmarkUndo.size()==undo+1,"no edit panel, one undo step");
        Expect(!tabDrag.tab&&!chrome.gridTab.on&&chrome.liftedIndex==-1&&chrome.sidebarDrag==-1,"drag over, the row back in its place");
        Paint(22000);shot(L"05-depose",22000);
        // Duplicate: « Paper » is already a favorite.
        Seed();Layout();BuildChrome();
        press(2);Move(tx,ty);Expect(tabDrag.favorite&&!tabDrag.fav.valid&&chrome.gridTab.duplicate==1&&!chrome.gridTab.slot,"a page already in the favorites: no place, the tile that holds it pointed at");
        Paint(0);Paint(1000);shot(L"06-doublon",1000);
        {const int n=Count(BM::BarRoot);Up(tx,ty);Expect(Count(BM::BarRoot)==n,"a duplicate release adds nothing");}
        // Échap.
        press(1);Move(tx,ty);Expect(tabDrag.favorite,"over the grid again");
        {const int n=Count(BM::BarRoot);WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);Expect(Count(BM::BarRoot)==n&&!tabDrag.tab&&!chrome.gridTab.on&&chrome.bandDropInto==-1,"Échap: nothing added, the grid at rest");}
        WndProc(window,WM_LBUTTONUP,0,MAKELPARAM((int)tx,(int)ty));Expect(Count(BM::BarRoot)==5,"the release after Échap adds nothing");
        // Reduced motion: no growth, the tiles at once.
        motion.SetReducedMotion(true);press(1);Move(tx,ty);Expect(tabDrag.favorite&&splitAnim.cardScale.target==100,"reduced motion: no lift growth");
        Paint(5000);{const auto it=chrome.gridStep.find(bookmarks.Root(BM::BarRoot).children[3]);Expect(it!=chrome.gridStep.end()&&NativeChrome::Chrome::MorphAt(it->second,At(5000)).top==chrome.gridTab.tiles[3].top,"reduced motion: the tiles are at once where they go");}
        WndProc(window,WM_KEYDOWN,VK_ESCAPE,0);motion.SetReducedMotion(false);Paint(6000);Paint(7000);
    }
    settings.tabsPosition=BrowserSettings::TabsPosition::Top;ApplyLayout();

    std::printf(failures?"FAILED: %d of %d checks\n":"PASS: tab favorite drag, top bar + side panel + compact (%d checks)\n",failures?failures:checks,checks);
    pages.clear();DestroyWindow(window);window=nullptr;
    std::fflush(stdout);TerminateProcess(GetCurrentProcess(),failures?1:0);return failures?1:0;
}
