// Exercise the production dispatcher and tab identity without a WebView runtime.
#include "../src/main.cpp"
#include <cstdio>
#include <cstdlib>
void Assert(bool ok){if(!ok){std::fprintf(stderr,"Shortcut assertion failed\n");std::exit(1);}}
void Modifiers(bool ctrl=false,bool shift=false,bool alt=false){BYTE state[256]{};state[VK_CONTROL]=ctrl?128:0;state[VK_SHIFT]=shift?128:0;state[VK_MENU]=alt?128:0;Assert(SetKeyboardState(state));}
int main(){
    const std::vector<std::wstring> titles={L"Q&A",L"Rock && Roll &",L"&",L"",L"Caf\u00e9 & th\u00e8",L"\u00c9\u00c8\u00c0 e\u0301 & a\u0300"};
    const std::vector<std::wstring> labels={L"Q&&A",L"Rock &&&& Roll &&",L"&&",L"Onglet sans titre",L"Caf\u00e9 && th\u00e8",L"\u00c9\u00c8\u00c0 e\u0301 && a\u0300"};
    for(const auto& title:titles){auto tab=std::make_shared<Tab>();tab->title=title;pages.push_back(tab);}
    current=4;const auto menu=TabMenuItems();
    Assert(menu.size()==titles.size());
    for(size_t i=0;i<titles.size();++i){
        const auto& item=menu[i];Assert(item.label==labels[i]);
        Assert(CM::Plain(item.label)==(titles[i].empty()?L"Onglet sans titre":titles[i]));
        Assert(item.id==1000+(int)i);Assert(item.checked==(i==4));Assert(item.enabled);Assert(pages[i]->title==titles[i]);
    }
    pages.clear();current=-1;
    Modifiers(true);for(UINT k='1';k<='9';++k)Assert(Key(k)==SelectTabBase+int(k-'1'));
    Assert(Key('L')=='L');Assert(Key(VK_TAB)==']');
    Assert(Key('P')==PrintActive);   // Ctrl+P: our print window from anywhere, the page included (#121: never the engine's preview)
    Modifiers(true,true);Assert(Key(VK_PRIOR)==MoveTabLeft);Assert(Key(VK_NEXT)==MoveTabRight);Assert(Key(VK_TAB)=='[');Assert(Key('1')==0);Assert(Key('T')=='U');
    Assert(Key('N')=='P');Assert(chrome.Enabled('P'));   // Ctrl+Maj+N: nouvelle fenêtre privée, without any toolbar button
    // Ctrl+Maj+F: Mode Focus (toggle); Ctrl+F stays the find bar. The command flips the setting both ways (smoke: never written to disk).
    Assert(Key('F')==FocusModeCommand);Modifiers(true);Assert(Key('F')==FindPage);Modifiers(true,true,true);Assert(Key('F')==0);
    {const bool wasSmoke=smoke;smoke=true;settings.focusMode=false;Command(FocusModeCommand);Assert(settings.focusMode);Command(FocusModeCommand);Assert(!settings.focusMode);smoke=wasSmoke;}
    Modifiers(false,false,true);Assert(Key('D')=='L');
    Modifiers();Assert(Key(VK_F6)==FocusChrome);Assert(Key('1')==0);
    for(int i=0;i<10;++i)pages.push_back(std::make_shared<Tab>());
    current=4;auto selected=Active();auto neighbor=pages[3];
    Command(MoveTabLeft);Assert(current==3&&Active()==selected&&pages[4]==neighbor);
    Command(MoveTabRight);Assert(current==4&&Active()==selected&&pages[3]==neighbor);
    Command(SelectTabBase);Assert(current==0);Command(MoveTabLeft);Assert(current==0);
    Command(SelectTabBase+8);Assert(current==9);Command(MoveTabRight);Assert(current==9);
    pages.resize(2);current=0;Command(SelectTabBase+7);Assert(current==0);Command(SelectTabBase+8);Assert(current==1);
    // Keyboard-opened menus sit under their control: the flower's opens rightward, the gear's leftward, in device pixels.
    {auto flower=PlaceMenu({8,6,40,38},1180,144);Assert(!flower.alignRight&&flower.control.left==12&&flower.control.top==9&&flower.control.right==60&&flower.control.bottom==57);
     auto gear=PlaceMenu({1150,48,1174,72},1180,96);Assert(gear.alignRight&&gear.control.right==1174&&gear.control.bottom==72);}
    // Échap while a drawer has the keyboard (Mode Focus + Annotation, release run 2026-11-25: Échap in Historique also left the
    // annotation mode): the drawer's own Échap first (the top-most layer), the mode only from the window. The real drawer,
    // created hidden on a hidden owner (never shown); Key() uses EscapeLeavesAnnotation(AnnotationModeActive(), GetFocus()).
    {   instance=GetModuleHandleW(nullptr);window=CreateWindowExW(0,L"STATIC",L"escape test",WS_POPUP,0,0,1180,815,nullptr,nullptr,instance,nullptr);Assert(window!=nullptr);
        wchar_t cwd[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,cwd);base=cwd;CreateHistoryPanel();HWND drawer=historyDrawer.Window();Assert(drawer!=nullptr);
        HWND inner=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,10,10,drawer,nullptr,instance,nullptr),page=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,10,10,window,nullptr,instance,nullptr);
        address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,10,10,window,nullptr,instance,nullptr);Assert(address!=nullptr);   // the field exists in the product
        std::printf("escape: owned %d %d, leaves drawer %d inner %d window %d page %d field %d\n",(int)EscapeOwnedBySurface(drawer),(int)EscapeOwnedBySurface(inner),(int)EscapeLeavesAnnotation(true,drawer),(int)EscapeLeavesAnnotation(true,inner),(int)EscapeLeavesAnnotation(true,window),(int)EscapeLeavesAnnotation(true,page),(int)EscapeLeavesAnnotation(true,address));
        Assert(EscapeOwnedBySurface(drawer)&&EscapeOwnedBySurface(inner));                       // the drawer, or a field in it: its Échap
        Assert(!EscapeLeavesAnnotation(true,drawer)&&!EscapeLeavesAnnotation(true,inner));       // the mode stays while the drawer closes
        Assert(EscapeLeavesAnnotation(true,window)&&EscapeLeavesAnnotation(true,page)&&!EscapeLeavesAnnotation(true,address));   // from the window (a page, the chat panel): the mode leaves, as before; the address field keeps it
        Assert(!EscapeLeavesAnnotation(false,window)&&!EscapeOwnedBySurface(window)&&!EscapeOwnedBySurface(nullptr));
        DestroyWindow(inner);DestroyWindow(page);}
    // The annotation banner holding the keyboard (night 25/11, published 0.3.842: Ctrl+Maj+F did nothing then): it keeps its
    // navigation keys, the browser's shortcuts go through. A child window stands for the banner (its class is a child too).
    {   HWND banner=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,10,10,window,nullptr,instance,nullptr),button=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,4,4,banner,nullptr,instance,nullptr);
        HWND note=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,10,10,window,nullptr,instance,nullptr);Assert(banner&&button&&note);
        Assert(BannerLetsShortcutThrough(banner,banner,'F',true,false)&&BannerLetsShortcutThrough(button,banner,'F',true,false));   // Ctrl+Maj+F (shift is not read: every Ctrl chord)
        Assert(BannerLetsShortcutThrough(banner,banner,'T',true,false)&&BannerLetsShortcutThrough(banner,banner,'D',false,true)&&BannerLetsShortcutThrough(banner,banner,VK_F6,false,false));
        Assert(!BannerLetsShortcutThrough(banner,banner,VK_TAB,false,false)&&!BannerLetsShortcutThrough(banner,banner,VK_RETURN,false,false)&&!BannerLetsShortcutThrough(banner,banner,VK_ESCAPE,false,false)&&
               !BannerLetsShortcutThrough(banner,banner,VK_SPACE,false,false)&&!BannerLetsShortcutThrough(banner,banner,VK_RIGHT,false,false));   // its own keys stay its own
        Assert(!BannerLetsShortcutThrough(note,banner,'F',true,false)&&!BannerLetsShortcutThrough(window,banner,'F',true,false)&&!BannerLetsShortcutThrough(banner,nullptr,'F',true,false)&&!BannerLetsShortcutThrough(nullptr,banner,'F',true,false));   // only the banner: a note's text keeps Ctrl+…
        Modifiers(true,true);Assert(Key('F')==FocusModeCommand);   // and Key() itself, with no annotation surface focused, unchanged
        DestroyWindow(button);DestroyWindow(banner);DestroyWindow(note);}
    Modifiers();pages.clear();std::puts("Keyboard shortcut checks passed");
}
