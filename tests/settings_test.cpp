#include "../src/settings.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSettings;
    // Defaults: DuckDuckGo, tabs restored, system Downloads folder, no prompt, system motion.
    State defaults;
    assert(defaults.engine==Engine::DuckDuckGo&&defaults.restoreTabs&&defaults.downloadFolder.empty()&&!defaults.askWhereToSave&&!defaults.reduceMotion);
    // Round trip, including a non-ASCII folder.
    State original;original.engine=Engine::Qwant;original.restoreTabs=false;original.downloadFolder=L"D:\\Téléchargements\\été";original.askWhereToSave=true;original.reduceMotion=true;original.searchSuggestions=false;original.tracking=Tracking::Strict;original.passwords=false;original.autofill=false;original.clearOnExit=true;original.sleepMinutes=30;original.zoomPercent=125;
    std::stringstream stream;Write(stream,original);assert(Read(stream)==original);
    for(int i=0;i<EngineCount;++i){State s;s.engine=static_cast<Engine>(i);std::stringstream io;Write(io,s);assert(Read(io).engine==s.engine);}
    // Bookmarks visibility and the future custom accent must survive restart.
    for(auto bar:{BookmarksBar::Auto,BookmarksBar::Always,BookmarksBar::Never}){
        State s;s.bookmarksBar=bar;s.accent=0xA054CB;std::stringstream io;Write(io,s);assert(Read(io)==s);
    }
    {std::istringstream invalid("browser-settings 1\nbookmarks-bar=invalid\naccent=#XYZXYZ\n");assert(Read(invalid)==State{});}
    // Missing, foreign or damaged files keep the defaults; unknown keys and bad values are ignored.
    for(const char* bad:{"","garbage\nengine=google\n","browser-settings 2\nengine=google\n"}){std::istringstream in(bad);assert(Read(in)==State{});}
    std::istringstream partial("browser-settings 1\r\nengine=bing\r\nrestore-tabs=maybe\r\nfuture-key=1\r\nreduce-motion=1\r\nengine=nope\r\n");
    State p=Read(partial);assert(p.engine==Engine::Bing&&p.restoreTabs&&p.reduceMotion&&!p.askWhereToSave);
    std::istringstream badUtf8("browser-settings 1\ndownload-folder=\xC3\x28\n");assert(Read(badUtf8).downloadFolder.empty());
    // New fields: defaults and rejected values.
    {State d;assert(d.searchSuggestions&&d.tracking==Tracking::Balanced&&d.passwords&&d.autofill&&!d.clearOnExit&&d.sleepMinutes==120&&d.zoomPercent==100);
     std::istringstream odd("browser-settings 1\nsleep-minutes=45\nzoom-percent=300\ntracking=paranoid\n");State r=Read(odd);assert(r.sleepMinutes==120&&r.zoomPercent==100&&r.tracking==Tracking::Balanced);}
    // Search URLs take already-encoded terms; the panel order keeps three primary engines.
    assert(SearchUrl(Engine::DuckDuckGo,L"deux%20mots")==L"https://duckduckgo.com/?q=deux%20mots");
    assert(SearchUrl(Engine::Google,L"a")==L"https://www.google.com/search?q=a");
    assert(Primary(Engine::BraveSearch)&&!Primary(Engine::Bing)&&PrimaryEngines==3);
}
