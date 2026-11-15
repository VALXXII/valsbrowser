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
    Assert(Key('N')=='P');Assert(chrome.Enabled('P'));   // Ctrl+Maj+N: nouvelle fenêtre privée, without any toolbar button
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
