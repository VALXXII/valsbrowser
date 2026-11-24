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
    Modifiers(true,true);Assert(Key(VK_PRIOR)==MoveTabLeft);Assert(Key(VK_NEXT)==MoveTabRight);Assert(Key(VK_TAB)=='[');Assert(Key('1')==0);Assert(Key('T')=='U');
    Assert(Key('N')=='P');Assert(chrome.Enabled('P'));
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
     SetTabsPosition(P::Top);Assert(!chrome.sidebar&&LayoutLine()==L"layout: top");
     // Files written by the trial builds migrate (layout + sidebar keys).
     {std::istringstream older("browser-settings 1\nlayout=side-panel\nsidebar=compact\n");settings=BrowserSettings::Read(older);ApplyLayout();Assert(chrome.sidebar&&settings.tabsPosition==P::Compact);settings=BrowserSettings::State{};ApplyLayout();Assert(!chrome.sidebar);}}   // Ctrl+Maj+N: nouvelle fenêtre privée, without any toolbar button
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
    Modifiers();pages.clear();std::puts("Keyboard shortcut checks passed");
}
