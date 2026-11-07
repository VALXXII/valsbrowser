// Exercise the production dispatcher and tab identity without a WebView runtime.
#include "../src/main.cpp"
#include <cstdio>
#include <cstdlib>
void Assert(bool ok){if(!ok){std::fprintf(stderr,"Shortcut assertion failed\n");std::exit(1);}}
void Modifiers(bool ctrl=false,bool shift=false,bool alt=false){BYTE state[256]{};state[VK_CONTROL]=ctrl?128:0;state[VK_SHIFT]=shift?128:0;state[VK_MENU]=alt?128:0;Assert(SetKeyboardState(state));}
int main(){
    Modifiers(true);for(UINT k='1';k<='9';++k)Assert(Key(k)==SelectTabBase+int(k-'1'));
    Assert(Key('L')=='L');Assert(Key(VK_TAB)==']');
    Modifiers(true,true);Assert(Key(VK_PRIOR)==MoveTabLeft);Assert(Key(VK_NEXT)==MoveTabRight);Assert(Key(VK_TAB)=='[');Assert(Key('1')==0);Assert(Key('T')=='U');
    Modifiers(false,false,true);Assert(Key('D')=='L');
    Modifiers();Assert(Key(VK_F6)==FocusChrome);Assert(Key('1')==0);
    for(int i=0;i<10;++i)pages.push_back(std::make_shared<Tab>());
    current=4;auto selected=Active();auto neighbor=pages[3];
    Command(MoveTabLeft);Assert(current==3&&Active()==selected&&pages[4]==neighbor);
    Command(MoveTabRight);Assert(current==4&&Active()==selected&&pages[3]==neighbor);
    Command(SelectTabBase);Assert(current==0);Command(MoveTabLeft);Assert(current==0);
    Command(SelectTabBase+8);Assert(current==9);Command(MoveTabRight);Assert(current==9);
    pages.resize(2);current=0;Command(SelectTabBase+7);Assert(current==0);Command(SelectTabBase+8);Assert(current==1);
    Modifiers();pages.clear();std::puts("Keyboard shortcut checks passed");
}
