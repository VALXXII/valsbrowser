// Exercise the production dispatcher and tab identity without a WebView runtime.
#include "../src/main.cpp"
#include <cstdio>
#include <cstdlib>
void AssertAt(bool ok,int line){if(!ok){std::fprintf(stderr,"Shortcut assertion failed (line %d)\n",line);std::exit(1);}}
#define Assert(x) AssertAt((x),__LINE__)
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
    Assert(Key('J')=='D');   // Ctrl+J: our Downloads menu (2026-11-26: the engine's own flyout opened top-right of the page)
    Assert(Key('P')==PrintActive);   // Ctrl+P: our print window from anywhere, the page included (#121: never the engine's preview)
    Modifiers(true,true);Assert(Key(VK_PRIOR)==MoveTabLeft);Assert(Key(VK_NEXT)==MoveTabRight);Assert(Key(VK_TAB)=='[');Assert(Key('1')==0);Assert(Key('T')=='U');
    Assert(Key('N')=='P');Assert(chrome.Enabled('P'));   // Ctrl+Maj+N: nouvelle fenêtre privée, without any toolbar button
    // #127 layout: the top bar by default; Ctrl+Maj+, folds the side panel only while it is the layout (else the page
    // gets the key); turning the panel on and off keeps its complete / compact choice; --side-panel is not saved.
    {using P=BrowserSettings::TabsPosition;wchar_t tmp[MAX_PATH]{};GetTempPathW(MAX_PATH,tmp);dataDir=std::wstring(tmp)+L"browser-shortcuts-test-"+std::to_wstring(GetCurrentProcessId());CreateDirectoryW(dataDir.c_str(),nullptr);settingsFile=dataDir+L"\\settings";   // hermetic: SaveSettings writes here
     Assert(!chrome.sidebar&&settings.tabsPosition==P::Top);Assert(Key(VK_OEM_COMMA)==0);Assert(!NativeChrome::ShortcutsPanel::SidePanelRows());
     Command(SidebarToggleCommand);Assert(!chrome.sidebar&&settings.tabsPosition==P::Top);   // top bar: the fold does nothing
     // Réglages / the tab menu set the position: live, the panel appears; the fold button / Ctrl+Maj+, flip Side <-> Compact in the setting.
     chrome.width=1180;SetTabsPosition(P::Side);Assert(chrome.sidebar&&Key(VK_OEM_COMMA)==SidebarToggleCommand&&NativeChrome::ShortcutsPanel::SidePanelRows()&&SidebarEffective()==SBar::Mode::Complete);
     Command(Key(VK_OEM_COMMA));Assert(settings.tabsPosition==P::Compact&&SidebarEffective()==SBar::Mode::Compact);
     ToggleSidebar();Assert(settings.tabsPosition==P::Side);
     SetTabsPosition(P::Compact);Assert(chrome.sidebar&&SidebarEffective()==SBar::Mode::Compact);
     chrome.width=700;SetTabsPosition(P::Side);Assert(SidebarEffective()==SBar::Mode::Compact&&settings.tabsPosition==P::Side);chrome.width=1180;   // narrow: compact without touching the setting
     SetTabsPosition(P::Top);Assert(!chrome.sidebar&&Key(VK_OEM_COMMA)==0);
     // The tab menu's last row: « Position des onglets » as three icon segments, the current one active, each named in its tooltip.
     {const auto row=TabsPositionMenu(40);Assert(row.kind==NativeChrome::ContextMenu::Kind::Segmented&&row.label==TR(settings_tabs_position)&&row.segments.size()==3&&row.segments[0].active&&!row.segments[1].active&&row.segments[2].id==42&&row.segments[1].tip==BrowserSettings::Info(P::Side).label&&row.paintSegment);}
     // Fix's tests: --tab-layout=top|side|compact (--side-panel = side) forces the run's layout without saving it; the fold
     // stays in the run; Réglages / the menu take over again. One log line names the active layout (LayoutLine).
     Assert(TabLayoutArg(L"--tab-layout=side")==1&&TabLayoutArg(L"--x --tab-layout=compact --y")==2&&TabLayoutArg(L"--tab-layout=top")==0&&TabLayoutArg(L"--side-panel")==1);
     Assert(TabLayoutArg(L"--tab-layout=sideways")==-1&&TabLayoutArg(L"--tab-layout=")==-1&&TabLayoutArg(L"")==-1&&TabLayoutArg(nullptr)==-1);
     Assert(LayoutLine()==L"layout: top");
     forcedLayout=1;ApplyLayout();Assert(chrome.sidebar&&settings.tabsPosition==P::Top&&LayoutLine()==L"layout: side 248 (this run)");
     ToggleSidebar();Assert(settings.tabsPosition==P::Top&&SidebarEffective()==SBar::Mode::Compact&&LayoutLine()==L"layout: compact (this run)");
     forcedLayout=0;ApplyLayout();Assert(!chrome.sidebar&&Key(VK_OEM_COMMA)==0&&LayoutLine()==L"layout: top (this run)");
     forcedLayout=2;ApplyLayout();Assert(chrome.sidebar&&SidebarEffective()==SBar::Mode::Compact&&settings.tabsPosition==P::Top);
     SetTabsPosition(P::Side);Assert(forcedLayout<0&&settings.tabsPosition==P::Side&&LayoutLine()==L"layout: side 248");
     chrome.width=700;Assert(LayoutLine()==L"layout: compact (narrow window)");chrome.width=1180;
     // « Redimensionner »: the edge drag (220 .. 360, compact under 160, back at 220), the release saving the width and the
     // layout, double-click 248, the keyboard on the focused edge.
     {SidebarPrepare();Assert(chrome.sidebarResizable&&chrome.sidebarWidth==248);chrome.side=SBar::Compute(chrome.SideInput(SBar::Mode::Complete,1));
      edgeClickAt=0;Assert(SidebarEdgeDown(246)&&SidebarEdgeDragging());SidebarEdgeMove(298);Assert(chrome.sidebarWidth==300&&SidebarEffective()==SBar::Mode::Complete);
      SidebarEdgeMove(498);Assert(chrome.sidebarWidth>360&&chrome.sidebarWidth<420);SidebarEdgeMove(178);Assert(chrome.sidebarWidth<220&&chrome.sidebarWidth>190&&SidebarEffective()==SBar::Mode::Complete);   // §5: rubber band past 360 / under 220
      SidebarEdgeMove(120);Assert(SidebarEffective()==SBar::Mode::Complete&&chrome.sidebarWidth>170);SidebarEdgeMove(170);Assert(SidebarEffective()==SBar::Mode::Complete);   // §5: no compact during the drag
      SidebarEdgeMove(288);SidebarEdgeUp(true);Assert(!SidebarEdgeDragging()&&settings.sideWidth==290&&settings.tabsPosition==P::Side);SidebarPrepare();Assert(chrome.sidebarWidth==290&&LayoutLine()==L"layout: side 290");
      chrome.side=SBar::Compute(chrome.SideInput(SBar::Mode::Complete,1));Assert(chrome.side.column==290);
      edgeClickAt=0;SidebarEdgeDown(288);SidebarEdgeMove(100);SidebarEdgeUp(false);SidebarPrepare();Assert(settings.tabsPosition==P::Side&&chrome.sidebarWidth==290);   // Échap: undone
      edgeClickAt=0;SidebarEdgeDown(288);SidebarEdgeMove(100);SidebarEdgeUp(true);Assert(settings.tabsPosition==P::Compact&&settings.sideWidth==290);   // released compact
      SidebarEdgeReset();Assert(settings.tabsPosition==P::Side&&settings.sideWidth==248);SidebarPrepare();Assert(chrome.sidebarWidth==248);   // double-click
      Modifiers();Assert(SidebarEdgeKey(VK_RIGHT)&&settings.sideWidth==264&&chrome.sidebarWidth==264);Assert(SidebarEdgeKey(VK_END)&&settings.sideWidth==360);Assert(SidebarEdgeKey(VK_HOME)&&settings.sideWidth==220);
      SidebarPrepare();Assert(SidebarEdgeKey(VK_LEFT)&&settings.tabsPosition==P::Compact);Assert(SidebarEdgeKey(VK_RIGHT)&&settings.tabsPosition==P::Side&&settings.sideWidth==220);
      Assert(SidebarEdgeKey(VK_RETURN)&&settings.sideWidth==248);Assert(!SidebarEdgeKey('A'));
      chrome.width=700;SidebarPrepare();Assert(!chrome.sidebarResizable&&!SidebarEdgeDown(246));chrome.width=1180;SidebarPrepare();Modifiers(true,true);}
     // The empty-area right-click menu (Paper « Clic droit zone vide · barre du haut / panneau latéral / compact »): « Nouvel
     // onglet », « Rouvrir l'onglet fermé », a separator, the 3-view picker (the current layout active); where it answers.
     {SetTabsPosition(P::Side);const auto items=EmptyAreaMenuItems();
      Assert(items.size()==4&&items[0].id==EmptyNewTab&&items[0].label==TR(tab_new)&&items[1].id==EmptyReopen&&items[1].label==TR(cmd_reopen_tab)&&items[2].separator&&items[3].kind==NativeChrome::ContextMenu::Kind::Segmented&&items[3].segments.size()==3&&!items[3].segments[0].active&&items[3].segments[1].active&&items[3].segments[1].id==EmptyPosition+1);
      chrome.side=SBar::Compute(chrome.SideInput(SBar::Mode::Complete,3));const auto& L=chrome.side;
      Assert(EmptyAreaMenuAt(0,100,L.tabsView.bottom-4)&&EmptyAreaMenuAt('T',100,(L.newTab.top+L.newTab.bottom)/2)&&EmptyAreaMenuAt('Y',30,L.tools[0].top+4));   // below the list, « + », a tool
      Assert(!EmptyAreaMenuAt(NativeChrome::TabBase+1,100,(L.tabs[1].top+L.tabs[1].bottom)/2)&&!EmptyAreaMenuAt(NativeChrome::CloseBase+1,200,40)&&!EmptyAreaMenuAt(NativeChrome::BookmarkBase,20,100)&&!EmptyAreaMenuAt('L',100,60)&&!EmptyAreaMenuAt(0,L.column+20,300));   // a tab, its close, a bookmark, the address, the page
      SetTabsPosition(P::Compact);chrome.side=SBar::Compute(chrome.SideInput(SBar::Mode::Compact,3));Assert(EmptyAreaMenuAt(0,20,chrome.side.tabsView.bottom-4)&&!EmptyAreaMenuAt(0,60,300));
      SetTabsPosition(P::Top);Assert(EmptyAreaMenuAt(0,600,20)&&EmptyAreaMenuAt('T',300,20)&&!EmptyAreaMenuAt(NativeChrome::TabBase,100,20)&&!EmptyAreaMenuAt(0,600,60));   // the top bar's tab row only
      Assert(EmptyAreaMenuItems()[3].segments[0].active);}
     // #127 × Mode Focus (Designer 24 nov., Paper 2091-0 / 20EJ-0): in a side layout Focus hides the column (no floating top
     // panel): the page 4 DIP from every edge, no window buttons, nothing to hit; Ctrl+Maj+, brings the column (pinned) and
     // sends it away, Échap too; the drawers start at the page's top.
     {SetTabsPosition(P::Side);const bool wasSmoke=smoke;smoke=true;settings.focusMode=true;BuildChrome();
      Assert(!FocusWanted()&&SideFocusWanted()&&chrome.sideHidden&&!chrome.floating&&!chrome.sideShown);
      {const auto c=ContentBox();Assert(c.left==4&&c.top==4&&c.right==chrome.width-4&&c.bottom==chrome.height-4);}
      Assert(chrome.regions.empty()&&chrome.side.close.Empty()&&NativeChrome::Design::Vertical::DrawerTop()==4&&!chrome.sidebarResizable);
      Assert(Key(VK_OEM_COMMA)==SidebarToggleCommand);Command(SidebarToggleCommand);Assert(sideReveal.open&&sideReveal.pinned);
      Command(SidebarToggleCommand);Assert(!sideReveal.open);
      ToggleSidebar();Assert(sideReveal.open&&SideFocusEscape()&&!sideReveal.open&&!SideFocusEscape());
      chrome.annotationBand=32;BuildChrome();{const auto c=ContentBox();Assert(c.top==32&&chrome.side.top==32&&chrome.side.profile.top==40&&NativeChrome::Design::Vertical::DrawerTop()==32);}
      chrome.annotationBand=0;settings.focusMode=false;BuildChrome();Assert(!chrome.sideHidden&&ContentBox().left==chrome.SidebarZoneLeft()&&!chrome.regions.empty());
      smoke=wasSmoke;}
     SetTabsPosition(P::Top);Assert(!chrome.sidebar&&LayoutLine()==L"layout: top");
     // Files written by the trial builds migrate (layout + sidebar keys).
     {std::istringstream older("browser-settings 1\nlayout=side-panel\nsidebar=compact\n");settings=BrowserSettings::Read(older);ApplyLayout();Assert(chrome.sidebar&&settings.tabsPosition==P::Compact);settings=BrowserSettings::State{};ApplyLayout();Assert(!chrome.sidebar);}}   // Ctrl+Maj+N: nouvelle fenêtre privée, without any toolbar button
    Modifiers(true,true);
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
