// Minimal overlay scrollbar (Paper "Browser Design" V3 "Scrollbar minimale"), host side.
// Included by main.cpp (after elements-integration.h). Model, raster and motion:
// viewport-scrollbar.h.
//
// Ported from the composition viewport (VALXXII/Polish-animations-and-UI) onto the
// windowed pages that split view, home tiles, fullscreen and the resize preview rely on:
//  * a document-creation script (top-level document only) adopts one stylesheet hiding
//    the page's *root* scrollbar (`:root{scrollbar-width:none!important}`; nested
//    scrollers and iframes keep theirs), posts the root scroller's metrics (rAF-coalesced
//    on scroll, resize, ResizeObserver, fullscreen) and whether the pointer is near the
//    right/bottom edge (the track strip that reveals a resting handle);
//  * every shown page (a single page or each split panel) gets its handles as small
//    layered child windows above it, like the split grips: premultiplied pixels from
//    Scrollbar::Raster, opacity through the layered window's constant alpha (no re-raster
//    per fade frame), hit-tested only where the handle is drawn, so the page keeps every
//    click around it. Hover widens it (6 -> 8 DIP); dragging it scrolls the page through
//    ExecuteScript (one script in flight, the latest target queued).
// Windows without layered child windows (never on Windows 8+) keep the native scrollbar.

constexpr wchar_t ScrollbarScript[]=LR"JS((()=>{try{if(window!==window.top)return;const w=window.chrome&&window.chrome.webview;if(!w||typeof w.postMessage!=='function')return;
const sheet=new CSSStyleSheet();sheet.replaceSync(':root{scrollbar-width:none!important}');
const adopt=()=>{const l=document.adoptedStyleSheets;if(l.indexOf(sheet)<0)document.adoptedStyleSheets=[...l,sheet];};
let frame=0,scrolled=0,nearEdge=0;
const send=()=>{frame=0;try{adopt();const e=document.scrollingElement,d=document.documentElement;if(!e||!d)return;
const s=getComputedStyle(d),b=document.body;let ox=s.overflowX,oy=s.overflowY;
if(b&&ox==='visible'&&oy==='visible'){const bs=getComputedStyle(b);ox=bs.overflowX;oy=bs.overflowY;}
const fs=!!document.fullscreenElement,free=v=>!fs&&v!=='hidden'&&v!=='clip'?1:0;
w.postMessage('lisiere-scrollbar '+[scrolled,e.scrollLeft,e.scrollTop,e.clientWidth,e.clientHeight,e.scrollWidth,e.scrollHeight,free(ox),free(oy),s.direction==='rtl'?1:0].join(' '));}catch(_){}scrolled=0;};
const queue=k=>{if(k)scrolled=1;if(!frame)frame=requestAnimationFrame(send);};
const edge=v=>{if(v!==nearEdge){nearEdge=v;try{w.postMessage('lisiere-scrollbar-near '+v);}catch(_){}}};
addEventListener('scroll',()=>queue(1),{passive:true});
addEventListener('resize',()=>queue(0),{passive:true});
addEventListener('load',()=>queue(0));
addEventListener('pointermove',e=>{edge(innerWidth-e.clientX<=12||innerHeight-e.clientY<=12?1:0);},{passive:true});
document.addEventListener('pointerleave',()=>edge(0),{passive:true});
document.addEventListener('fullscreenchange',()=>queue(0));
const ro=new ResizeObserver(()=>queue(0));
const watch=()=>{if(document.documentElement)ro.observe(document.documentElement);if(document.body)ro.observe(document.body);queue(0);};
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',watch,{once:true});else watch();
adopt();queue(0);}catch(_){}})();)JS";
constexpr wchar_t ScrollbarMessage[]=L"lisiere-scrollbar ";
constexpr wchar_t ScrollbarNearMessage[]=L"lisiere-scrollbar-near ";

struct PageScrollbar {
    HWND bars[2]{};                         // vertical, horizontal handle windows
    NativeChrome::Scrollbar::Motion motion;NativeChrome::Scrollbar::Layout layout;
    int hovered=-1,dragging=-1;bool nearEdge=false;float grab=0;
    RECT page{};                            // the page's client rectangle (px) the layout was computed for
    struct Drawn { float outer=-1,scale=0,hover=-1,origin=0;int length=-1,pad=0,w=0,h=0;std::vector<uint32_t> pixels; } drawn[2];
    bool scriptBusy=false;std::wstring scriptPending;
};
std::map<unsigned,PageScrollbar> pageScrollbars; // by tab serial
std::shared_ptr<Tab> TabBySerial(unsigned serial){for(auto& t:pages)if(t&&!t->closed&&t->serial==serial)return t;return nullptr;}
bool ScrollbarOnScreen(const Tab& t){return t.shown&&t.placedVisible==1&&!t.frozen&&!IsRectEmpty(&t.placed)&&!t.home;}
NativeChrome::Motion::Wake ScrollbarWake(NativeChrome::Motion::Time now){
    NativeChrome::Motion::Wake w;for(auto& [serial,s]:pageScrollbars)w=NativeChrome::Motion::Earlier(w,s.motion.NextWake(now));return w;}
void ScrollbarDestroy(){for(auto& [serial,s]:pageScrollbars)for(HWND b:s.bars)if(b)DestroyWindow(b);pageScrollbars.clear();}
void PlaceScrollbars(unsigned serial,PageScrollbar& s,NativeChrome::Motion::Time now);
LRESULT CALLBACK ScrollbarProc(HWND h,UINT msg,WPARAM w,LPARAM l);
HWND NewScrollbarWindow(){
    static bool registered=false;HINSTANCE hi=instance?instance:GetModuleHandleW(nullptr);
    if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=ScrollbarProc;wc.hInstance=hi;wc.lpszClassName=L"BrowserPageScrollbar";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);registered=true;}
    return CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE,L"BrowserPageScrollbar",L"",WS_CHILD,0,0,1,1,window,nullptr,hi,nullptr);
}
// One handle's pixels into its layered window at (x,y) px (main window client), opacity 0..1.
void ShowScrollbarWindow(HWND b,const PageScrollbar::Drawn& d,int x,int y,float opacity){
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=d.w;bi.bmiHeader.biHeight=-d.h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);HDC mem=CreateCompatibleDC(screen);void* bits=nullptr;HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(bmp&&bits){memcpy(bits,d.pixels.data(),d.pixels.size()*4);HGDIOBJ old=SelectObject(mem,bmp);
        POINT src{0,0};SIZE sz{d.w,d.h};BLENDFUNCTION blend{AC_SRC_OVER,0,(BYTE)std::lround(std::clamp(opacity,0.f,1.f)*255),AC_SRC_ALPHA};
        // Child layered window: placed by SetWindowPos in parent client pixels (as the split grips);
        // a pptDst here would be read as parent-client too and misplace it.
        SetWindowPos(b,HWND_TOP,x,y,d.w,d.h,SWP_NOACTIVATE|SWP_SHOWWINDOW);UpdateLayeredWindow(b,screen,nullptr,&sz,mem,&src,0,&blend,ULW_ALPHA);
        SelectObject(mem,old);}
    if(bmp)DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
void HideScrollbarWindow(HWND b){if(b&&(GetWindowLongW(b,GWL_STYLE)&WS_VISIBLE))ShowWindow(b,SW_HIDE);}
void PlaceScrollbars(unsigned serial,PageScrollbar& s,NativeChrome::Motion::Time now){
    namespace SB=NativeChrome::Scrollbar;auto t=TabBySerial(serial);
    if(!t||!ScrollbarOnScreen(*t)){for(HWND b:s.bars)HideScrollbarWindow(b);if(s.dragging<0){s.motion.Reset();s.hovered=-1;s.nearEdge=false;}return;}
    const float scale=GetDpiForWindow(window)/96.f;const RECT p=t->placed;s.page=p;
    // Outer bottom corners are rounded only where the page meets the viewport's own (8 DIP; square maximised or full screen).
    const float corner=IsZoomed(window)||InFullscreen()?0.f:NativeChrome::Design::Viewport::radius;
    s.layout=SB::Compute(t->scroll,(float)(p.right-p.left),(float)(p.bottom-p.top),scale,corner);
    s.motion.Advance(now);const auto v=s.motion.Sample(now);
    for(int k=0;k<2;++k){
        const bool vertical=k==0;const SB::Axis& a=vertical?s.layout.v:s.layout.h;
        if(!a.present||v.opacity<=.001f){HideScrollbarWindow(s.bars[k]);continue;}
        if(!s.bars[k]&&!(s.bars[k]=NewScrollbarWindow()))continue;
        const float outer=SB::Outer(s.layout,vertical);const int length=(int)std::lround(a.thumbLength);auto& d=s.drawn[k];
        if(d.outer!=outer||d.scale!=scale||d.length!=length||d.hover!=v.hover){
            auto tile=SB::Raster(outer,length,scale,v.hover,vertical);
            d.outer=outer;d.scale=scale;d.hover=v.hover;d.length=length;d.origin=tile.originAcross;d.pad=(tile.along-length)/2;
            d.w=vertical?tile.across:tile.along;d.h=vertical?tile.along:tile.across;d.pixels=std::move(tile.pixels);
        }
        if(d.w<=0||d.h<=0){HideScrollbarWindow(s.bars[k]);continue;}
        // Whole-pixel position along the track, fractional width across it.
        const int along=(int)std::round(a.thumbStart)-d.pad,across=(int)std::floor(d.origin);
        ShowScrollbarWindow(s.bars[k],d,p.left+(vertical?across:along),p.top+(vertical?along:across),v.opacity);
    }
}
void ScrollbarRefresh(NativeChrome::Motion::Time now){
    if(!window||halted)return;
    for(auto it=pageScrollbars.begin();it!=pageScrollbars.end();){
        if(!TabBySerial(it->first)){for(HWND b:it->second.bars)if(b)DestroyWindow(b);it=pageScrollbars.erase(it);continue;}
        PlaceScrollbars(it->first,it->second,now);++it;
    }
}
void ScrollbarSettleHover(){for(auto& [serial,s]:pageScrollbars)s.motion.SettleHover();ScrollbarRefresh(NativeChrome::Motion::Now());}
void ScrollbarReset(){for(auto& [serial,s]:pageScrollbars){if(s.dragging>=0&&s.bars[s.dragging]&&GetCapture()==s.bars[s.dragging])ReleaseCapture();s.dragging=-1;s.motion.Reset();s.hovered=-1;s.nearEdge=false;s.scriptPending.clear();}ScrollbarRefresh(NativeChrome::Motion::Now());}
// Untrusted page text: ten finite numbers within sane bounds, else ignored.
bool ScrollbarMessageFrom(const std::shared_ptr<Tab>& t,const std::wstring& text){
    const bool metrics=text.rfind(ScrollbarMessage,0)==0,nearing=text.rfind(ScrollbarNearMessage,0)==0;
    if(!metrics&&!nearing)return false;if(!t||t->closed)return true;
    auto& s=pageScrollbars[t->serial];const auto now=NativeChrome::Motion::Now();
    if(nearing){s.nearEdge=text.size()>wcslen(ScrollbarNearMessage)&&text[wcslen(ScrollbarNearMessage)]==L'1';
        if(s.dragging<0)s.motion.Pointer(s.nearEdge,s.hovered>=0,false,now,motion.ReducedMotion());}
    else{int kind=0,nx=0,ny=0,rtl=0;float x=0,y=0,cw=0,ch=0,sw=0,sh=0;
        if(swscanf(text.c_str(),L"lisiere-scrollbar %d %f %f %f %f %f %f %d %d %d",&kind,&x,&y,&cw,&ch,&sw,&sh,&nx,&ny,&rtl)!=10)return true;
        for(float f:{x,y,cw,ch,sw,sh})if(!std::isfinite(f)||std::fabs(f)>1e8f)return true;
        if(cw<0||ch<0||sw<0||sh<0)return true;
        t->scroll={x,y,cw,ch,sw,sh,nx!=0,ny!=0,rtl!=0,true};if(kind==1)s.motion.Scrolled(now);}
    PlaceScrollbars(t->serial,s,now);ScheduleMotion(now);return true;
}
// Drag: one script in flight per page, the latest target queued behind it.
void RunScrollbarScript(unsigned serial){
    auto it=pageScrollbars.find(serial);auto t=TabBySerial(serial);if(it==pageScrollbars.end()||!t||!t->view.p)return;auto& s=it->second;
    if(s.scriptBusy||s.scriptPending.empty())return;
    std::wstring js;js.swap(s.scriptPending);s.scriptBusy=true;
    auto done=Callback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>([serial](HRESULT,LPCWSTR)->HRESULT{auto i=pageScrollbars.find(serial);if(i!=pageScrollbars.end()){i->second.scriptBusy=false;if(!halted)RunScrollbarScript(serial);}return S_OK;});
    if(FAILED(t->view->ExecuteScript(js.c_str(),done)))s.scriptBusy=false;done->Release();
}
void ScrollPageTo(unsigned serial,int axis,float value){
    auto t=TabBySerial(serial);if(!t)return;
    if(axis==1&&t->scroll.rtl)value-=t->scroll.RangeX(); // RTL roots scroll 0 .. -range
    wchar_t js[192];swprintf(js,192,L"(()=>{const e=document.scrollingElement;if(e)e.scrollTo({%ls:%.3f,behavior:'instant'})})()",axis==0?L"top":L"left",(double)value);
    pageScrollbars[serial].scriptPending=js;RunScrollbarScript(serial);
}
// Pointer on a handle window: hover widens it, a press drags it (the grab offset is kept).
LRESULT CALLBACK ScrollbarProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    namespace SB=NativeChrome::Scrollbar;
    unsigned serial=0;int k=-1;
    for(auto& [id,s]:pageScrollbars)for(int i=0;i<2;++i)if(s.bars[i]==h){serial=id;k=i;}
    auto it=pageScrollbars.find(serial);
    if(k<0||it==pageScrollbars.end())return DefWindowProcW(h,msg,w,l);
    auto& s=it->second;const auto now=NativeChrome::Motion::Now();const bool reduced=motion.ReducedMotion();
    // Pointer position along the track, in the page's px.
    auto along=[&]{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};MapWindowPoints(h,window,&p,1);return (float)(k==0?p.y-s.page.top:p.x-s.page.left);};
    switch(msg){
    case WM_NCHITTEST:return HTCLIENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if(s.dragging==k){const SB::Axis& a=k==0?s.layout.v:s.layout.h;ScrollPageTo(serial,k,SB::ScrollFor(a,along()-s.grab));return 0;}
        if(s.hovered!=k){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);s.hovered=k;s.motion.Pointer(true,true,false,now,reduced);PlaceScrollbars(serial,s,now);ScheduleMotion(now);}
        return 0;
    case WM_MOUSELEAVE:if(s.dragging<0){s.hovered=-1;s.motion.Pointer(s.nearEdge,false,false,now,reduced);PlaceScrollbars(serial,s,now);ScheduleMotion(now);}return 0;
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{
        const SB::Axis& a=k==0?s.layout.v:s.layout.h;SetCapture(h);s.dragging=k;s.hovered=k;s.grab=along()-a.thumbStart;
        s.motion.Pointer(true,true,true,now,reduced);PlaceScrollbars(serial,s,now);ScheduleMotion(now);return 0;}
    case WM_LBUTTONUP:if(GetCapture()==h)ReleaseCapture();[[fallthrough]];
    case WM_CAPTURECHANGED:if(s.dragging==k){s.dragging=-1;POINT p;GetCursorPos(&p);const bool over=WindowFromPoint(p)==h;s.hovered=over?k:-1;
        s.motion.Pointer(over||s.nearEdge,over,false,now,reduced);PlaceScrollbars(serial,s,now);ScheduleMotion(now);}return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }
    return DefWindowProcW(h,msg,w,l);
}
// Pages that just appeared (tab switch, split change) get their entry so a first metric
// report has somewhere to land; pages that went away hide theirs (ApplyPages calls this).
void SyncScrollbars(){for(auto& t:pages)if(t&&!t->closed&&ScrollbarOnScreen(*t))pageScrollbars[t->serial];ScrollbarRefresh(NativeChrome::Motion::Now());}
