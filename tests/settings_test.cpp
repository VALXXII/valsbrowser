#include "../src/settings.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSettings;
    // Defaults: DuckDuckGo, tabs restored, system Downloads folder, no prompt, system motion.
    State defaults;
    assert(defaults.engine==Engine::DuckDuckGo&&defaults.restoreTabs&&defaults.downloadFolder.empty()&&!defaults.askWhereToSave&&!defaults.reduceMotion);
    // Round trip, including a non-ASCII folder.
    State original;original.engine=Engine::Qwant;original.restoreTabs=false;original.downloadFolder=L"D:\\Téléchargements\\été";original.askWhereToSave=true;original.reduceMotion=true;
    std::stringstream stream;Write(stream,original);assert(Read(stream)==original);
    for(int i=0;i<EngineCount;++i){State s;s.engine=static_cast<Engine>(i);std::stringstream io;Write(io,s);assert(Read(io).engine==s.engine);}
    // Missing, foreign or damaged files keep the defaults; unknown keys and bad values are ignored.
    for(const char* bad:{"","garbage\nengine=google\n","browser-settings 2\nengine=google\n"}){std::istringstream in(bad);assert(Read(in)==State{});}
    std::istringstream partial("browser-settings 1\r\nengine=bing\r\nrestore-tabs=maybe\r\nfuture-key=1\r\nreduce-motion=1\r\nengine=nope\r\n");
    State p=Read(partial);assert(p.engine==Engine::Bing&&p.restoreTabs&&p.reduceMotion&&!p.askWhereToSave);
    std::istringstream badUtf8("browser-settings 1\ndownload-folder=\xC3\x28\n");assert(Read(badUtf8).downloadFolder.empty());
    // Search URLs take already-encoded terms; the panel order keeps three primary engines.
    assert(SearchUrl(Engine::DuckDuckGo,L"deux%20mots")==L"https://duckduckgo.com/?q=deux%20mots");
    assert(SearchUrl(Engine::Google,L"a")==L"https://www.google.com/search?q=a");
    assert(Primary(Engine::BraveSearch)&&!Primary(Engine::Bing)&&PrimaryEngines==3);
}
