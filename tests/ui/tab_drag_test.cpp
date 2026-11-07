#include "../../src/main.cpp"
#include <cassert>
int main(){
    window=CreateWindowExW(0,L"STATIC",L"drag test",WS_POPUP,0,0,1180,815,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(window);chrome.width=1180;
    auto a=std::make_shared<Tab>(),b=std::make_shared<Tab>(),c=std::make_shared<Tab>();
    pages={a,b,c};current=1;chrome.Build(3,current);
    auto start=NativeChrome::Design::TabStrip::Surface(2);
    WndProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((int)start.left+12,20));
    WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM((int)start.left+13,20));
    assert(!tabDrag.active&&pages[2]==c);
    WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(56,20));
    assert(tabDrag.active&&pages[0]==c&&Active()==b&&chrome.dragIndex==0);
    // Release over the first close slot must neither close nor select the dragged tab.
    WndProc(window,WM_LBUTTONUP,0,MAKELPARAM(156,20));
    assert(pages.size()==3&&pages[0]==c&&Active()==b&&!tabDrag.tab&&chrome.dragIndex==-1);
    Command('{');assert(Active()==b&&current==1);Command('}');assert(Active()==b&&current==2);
    Command('}');assert(current==2);
    // Drag cancellation clears the visual and suppresses a later click.
    chrome.Build(3,current);
    WndProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(56,20));
    WndProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(200,20));
    WndProc(window,WM_CANCELMODE,0,0);
    assert(!tabDrag.tab&&chrome.dragIndex==-1&&pressed==0);
    WndProc(window,WM_LBUTTONUP,0,MAKELPARAM(200,20));assert(pages.size()==3&&Active()==b);
    pages.clear();DestroyWindow(window);window=nullptr;
    std::puts("PASS: threshold, third-to-first drag, active identity, release over close, keyboard moves, bounds, cancellation");
}
