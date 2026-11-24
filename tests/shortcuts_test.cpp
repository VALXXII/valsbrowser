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
     // A trial run forced with --side-panel shows the panel without saving; its fold stays in the run.
     sidePanelForced=true;ApplyLayout();Assert(chrome.sidebar&&settings.tabsPosition==P::Top);ToggleSidebar();Assert(settings.tabsPosition==P::Top&&SidebarEffective()==SBar::Mode::Compact);
     SetTabsPosition(P::Side);Assert(!sidePanelForced&&settings.tabsPosition==P::Side);SetTabsPosition(P::Top);forcedCompact=false;Assert(!chrome.sidebar);
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
