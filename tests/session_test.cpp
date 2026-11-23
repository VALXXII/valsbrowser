#include "../src/session.h"
#include "../src/address.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSession;
    State original{{"https://example.org/a?x=1&y=2","","https://example.org/été","about:blank","https://example.org/\"quoted\""},2,{}};
    std::stringstream stream;Write(stream,original);auto restored=Read(stream);
    assert(restored.urls==original.urls&&restored.active==2);
    for(const auto* bad:{"","garbage","browser-session-1\n0\n129\n","browser-session-1\n-1\n1\n\"url\"","browser-session-1\n1\n1\n\"url\"","browser-session-1\n0\n2\n\"url\""}){std::istringstream input(bad);assert(Read(input).urls.empty());}
    State maximum;maximum.urls.resize(128,"https://example.org");maximum.active=127;
    std::stringstream full;Write(full,maximum);assert(Read(full).urls==maximum.urls);
    // Split view groups (format 2): orientation, panel order and weights survive.
    State split{{"https://a.test/","https://b.test/","","https://c.test/"},1,{{0,{1,0},{2.5f,1}},{1,{2,3},{1,1}}}};
    std::stringstream s2;Write(s2,split);auto back=Read(s2);
    assert(back.urls==split.urls&&back.active==1&&back.groups.size()==2);
    assert(back.groups[0].orientation==0&&back.groups[0].members==std::vector<int>({1,0})&&back.groups[0].weights[0]==2.5f);
    assert(back.groups[1].orientation==1&&back.groups[1].members==std::vector<int>({2,3}));
    // Format 1 files still restore their tabs (no groups).
    {std::istringstream v1("browser-session-1\n0\n2\n\"https://a.test/\"\n\"\"\n");auto old=Read(v1);assert(old.urls.size()==2&&old.groups.empty());}
    // A damaged group list drops the groups, never the tabs.
    for(const auto* bad:{"browser-session-2\n0\n2\n\"a\"\n\"b\"\n1\n0 2 0 1 0 1\n",   // same tab twice
                         "browser-session-2\n0\n2\n\"a\"\n\"b\"\n1\n0 2 0 1 5 1\n",   // index out of range
                         "browser-session-2\n0\n2\n\"a\"\n\"b\"\n1\n2 2 0 1 1 1\n",   // unknown orientation
                         "browser-session-2\n0\n2\n\"a\"\n\"b\"\n1\n0 1 0 1\n",       // a group of one
                         "browser-session-2\n0\n2\n\"a\"\n\"b\"\n1\n0 2 0 -1 1 1\n"}){ // negative weight
        std::istringstream input(bad);auto partial=Read(input);assert(partial.urls.size()==2&&partial.groups.empty());}
    // SEC-21: a saved address the engine refuses is restored as a new tab (RestoreSession's filter), not replayed into
    // Navigate at every start; the other tabs and the tab count (so the split groups) are kept.
    {State saved{{"http://","https://ok.test/","https://a b/","https://1.2.3.4.5/",""},1,{}};std::stringstream file;Write(file,saved);auto back=Read(file);
     std::vector<std::wstring> restored;for(const auto& url:back.urls){std::wstring uri;NativeChrome::Downloads::FromUtf8(url,uri);if(!AddressBar::Navigable(uri))uri.clear();restored.push_back(uri);}
     assert(restored==std::vector<std::wstring>({L"",L"https://ok.test/",L"",L"",L""}));}
}
