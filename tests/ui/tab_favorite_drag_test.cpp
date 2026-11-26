// Atelier B: a tab dragged onto the favorites bar (top layout), through the PRODUCTION window procedure, on a window
// that is never shown (no visible window, no input sent anywhere: WndProc is called directly, as tests/ui/tab_drag_test.cpp
// does) and the chrome painted off screen (Chrome::InitOffscreen). Demo data only, never a profile (smoke: nothing read
// or written). Checks the drag state machine and writes the drag-state renders (chrome + the drag overlay's preview).
//   1. Over the bar: the tab becomes the preview (not cut: drawn in the overlay, below the strip), its strip place hollow,
//      the bar lit, its place opened where the release would add it; back up: the strip reorder resumes.
//   2. Insertion index left / between / right, a folder's middle (into it), the 2 DIP gap.
//   3. Release: added at that index, one undo step, the edit panel NOT opened; into a folder: at its end.
//   4. Duplicate (the page already on the bar / in that folder): no place, the release adds nothing.
//   5. Échap: nothing added, the strip order restored, the bar at rest; lost capture too.
//   6. From the page (split lift) back up to the bar, and down again into the lift.
//   7. Full bar: the new one goes behind « » » (the button takes the folder drop look).
//   8. Reduced motion: no lift growth, the bar's springs jump; the highlight still fades.
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
static NativeChrome::Motion::Time At(double ms){return clock0+std::chrono::duration_cast<NativeChrome::Motion::Clock::duration>(std::chrono::duration<double,std::milli>(ms));}
static void Paint(double ms){chrome.Paint(Titles(),current,pages[current]->uri,false,0,0,nullptr,At(ms));}
// The window as it would look: the chrome (offscreen), a white page card where the page sits, and the drag overlay's
// frame (the preview) over both, as the layered overlay window covers the client area.
static void Shot(const wchar_t* name,double ms){
    Paint(ms);auto* t=chrome.RenderTarget();if(!t)return;
    t->BeginDraw();ID2D1SolidColorBrush* b=nullptr;t->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&b);
    const auto c=ContentBox();if(b){t->FillRoundedRectangle(D2D1::RoundedRect({c.left,c.top,c.right,c.bottom},6,6),b);b->Release();}
    if(splitAnim.frame.card.shown){splitOverlay.painter.write=chrome.WriteFactory();splitOverlay.painter.fonts=chrome.FontCollection();splitOverlay.painter.Draw(t,splitAnim.frame);}
    t->EndDraw();
    const std::wstring path=out+L"\\"+name+L".png";Expect(SUCCEEDED(chrome.SavePng(path.c_str())),"render saved");std::wprintf(L"     png %ls\n",path.c_str());
}
static void Settle(){for(int i=0;i<40;++i){splitAnim.last=At(i*50.0);SplitFrame(At(i*50.0+50));}}   // the overlay's springs (display-paced in the app)
static std::shared_ptr<Tab> MakeTab(const wchar_t* title,const wchar_t* uri){auto t=std::make_shared<Tab>();t->title=title;t->uri=uri;t->home=false;return t;}
static BM::Id bmVeille,bmPaper,bmMobbin,bmProjet,bmMdn;
static void Seed(bool full=false){
    bookmarks=BM::Store{};bookmarkUndo.clear();
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

    // ---- 1. Over the bar: the preview, the hollow strip place, the lit bar, the opened place -------------------------
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}
    Move(chrome.strip.Surface(1).left+60,20);
    Expect(tabDrag.active&&!tabDrag.favorite&&chrome.dragIndex>=0,"in the strip: reorder as before");
    Move(BandMid(2)+20,bandY);   // right half of Mobbin
    Expect(tabDrag.favorite&&chrome.bandTab.on,"over the bar: favorites mode, the bar lit");
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
    Expect(chrome.bandTabGlow.Value(At(1000))>.99f,"the highlight is fully on once settled");
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
    Paint(1500);Paint(3000);Expect(chrome.bandTabGlow.Value(At(3000))<.01f,"the highlight has faded");

    // ---- 3. Release on the bar: added there, no edit panel, one undo step ---------------------------------------------
    Move(BandMid(2)+20,bandY);Expect(tabDrag.favorite&&tabDrag.fav.slot==3,"over the bar again");
    const int before=Count(BM::BarRoot);popoverReturn=0x5A5A;const size_t undo=bookmarkUndo.size();
    Up(BandMid(2)+20,bandY);
    Expect(Count(BM::BarRoot)==before+1,"release: one favorite added");
    {const auto* n=bookmarks.Find(bookmarks.Root(BM::BarRoot).children[3]);Expect(n&&n->kind==BM::Kind::Link&&n->url==b->uri&&n->title==b->title,"at index 3, with the tab's title and address");}
    Expect(popoverReturn==0x5A5A,"the edit panel is NOT opened (EditBookmark never ran)");
    Expect(bookmarkUndo.size()==undo+1&&bookmarkUndo.back().steps.size()==1&&bookmarkUndo.back().steps[0].kind==BookmarkStep::Created,"one undo step « Created »");
    Expect(!tabDrag.tab&&!chrome.bandTab.on&&chrome.liftedIndex==-1&&pages[1]==b,"drag over, the tab back in its place, the bar at rest");
    Expect(chrome.band.visible==6,"the bar shows the new favorite");
    Shot(L"top-05-depose",1100);

    // ---- 4. Duplicates ---------------------------------------------------------------------------------------------
    Seed();
    {auto s=chrome.strip.Surface(2);Down(s.left+24,20);}Move(BandMid(2)+20,bandY);   // « Paper » tab: already on the bar
    Expect(tabDrag.favorite&&!tabDrag.fav.valid&&tabDrag.fav.duplicate==1&&chrome.bandTab.duplicate==1&&!chrome.bandTab.slot,"the page is already on the bar: no place, the existing item pointed at");
    Expect(fixtureCursor==LoadCursor(nullptr,IDC_NO),"the pointer says no (as the split refusal does)");
    Paint(0);Paint(1000);Settle();Shot(L"top-06-doublon",1000);
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
    Paint(0);Paint(1000);Settle();Shot(L"top-07-barre-pleine",1000);
    Move(chrome.BandRect(0).left+2,bandY);Expect(chrome.bandTab.slot&&chrome.bandTab.shown[0]&&!chrome.bandTab.shown[chrome.band.visible-1],"at the start of a full bar: its place opens, the last shown item goes behind « » »");
    Paint(1000);Paint(2000);Shot(L"top-08-barre-pleine-debut",2000);
    {const int n=Count(BM::BarRoot);Up(chrome.BandRect(0).left+2,bandY);Expect(Count(BM::BarRoot)==n+1&&bookmarks.Root(BM::BarRoot).children[0]!=bmVeille,"added first");}

    // ---- 8. Reduced motion --------------------------------------------------------------------------------------------
    Paint(3000);Paint(4000);Seed();motion.SetReducedMotion(true);   // the highlight at rest first
    {auto s=chrome.strip.Surface(1);Down(s.left+24,20);}Move(BandMid(2)+20,bandY);
    Expect(tabDrag.favorite&&splitAnim.cardScale.target==100,"reduced motion: no lift growth");
    Paint(5000);
    {const auto it=chrome.bandStep.find(bookmarks.Root(BM::BarRoot).children[3]);Expect(it!=chrome.bandStep.end()&&it->second.l.Value(At(5000))==chrome.bandTab.left[3],"reduced motion: the items are at once where they go");}
    Expect(chrome.bandTabGlow.Value(At(5000))<.99f&&chrome.bandTabGlow.Value(At(5200))>.99f,"reduced motion: the highlight still fades in (short)");
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

    std::printf(failures?"FAILED: %d of %d checks\n":"PASS: tab favorite drag, top layout (%d checks)\n",failures?failures:checks,checks);
    pages.clear();DestroyWindow(window);window=nullptr;
    std::fflush(stdout);TerminateProcess(GetCurrentProcess(),failures?1:0);return failures?1:0;
}
