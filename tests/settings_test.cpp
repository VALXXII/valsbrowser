#include "../src/settings.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSettings;
    // Defaults: DuckDuckGo, tabs restored, system Downloads folder, no prompt, system motion.
    State defaults;
    assert(defaults.engine==Engine::DuckDuckGo&&defaults.restoreTabs&&defaults.downloadFolder.empty()&&!defaults.askWhereToSave&&!defaults.reduceMotion);
    // Round trip, including a non-ASCII folder.
    State original;original.engine=Engine::Qwant;original.restoreTabs=false;original.downloadFolder=L"D:\\Téléchargements\\été";original.askWhereToSave=true;original.reduceMotion=true;original.searchSuggestions=false;original.tracking=Tracking::Strict;original.passwords=false;original.autofill=false;original.clearOnExit=true;original.sleepMinutes=30;original.zoomPercent=125;original.customSearch=L"https://exemple.fr/?q=%s";original.newTabPage=NewTabPage::Custom;original.homepage=L"https://exemple.fr/accueil";original.tabPosition=TabPosition::NextToActive;original.confirmCloseTabs=false;original.closeWithLastTab=true;original.adBlock=false;original.httpsOnly=true;original.blockThirdPartyCookies=true;original.memorySaver=false;original.muteBackground=true;original.autoplay=Autoplay::Allowed;
    std::stringstream stream;Write(stream,original);assert(Read(stream)==original);
    for(int i=0;i<EngineCount;++i){State s;s.engine=static_cast<Engine>(i);s.customSearch=L"https://s.fr/?q=%s";std::stringstream io;Write(io,s);assert(Read(io).engine==s.engine);}
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
    // Custom engine and new tab page: validation, fallbacks, %s substitution.
    {assert(SearchTemplate(L"  exemple.fr/search?q=%s ")==L"https://exemple.fr/search?q=%s");assert(SearchTemplate(L"https://exemple.fr/")==L"");
     assert(SearchTemplate(L"ftp://x.fr/%s")==L"");assert(SearchTemplate(L"https://%s")==L"");assert(SearchTemplate(L"https://a.fr/?q=%s&x=1 2")==L"");
     assert(SearchUrl(Engine::Custom,L"a%20b",L"https://s.fr/?q=%s&r=%s")==L"https://s.fr/?q=a%20b&r=a%20b");
     assert(SearchUrl(Engine::Custom,L"x",L"")==L"https://duckduckgo.com/?q=x");
     assert(HomepageUrl(L"exemple.fr")==L"https://exemple.fr");assert(HomepageUrl(L"http://localhost:8080/")==L"http://localhost:8080/");
     assert(HomepageUrl(L"pas une adresse")==L"");assert(HomepageUrl(L"javascript://x.fr")==L"");
     State d;assert(d.newTabPage==NewTabPage::Blank&&d.tabPosition==TabPosition::End&&d.confirmCloseTabs&&!d.closeWithLastTab&&d.adBlock&&!d.httpsOnly&&!d.blockThirdPartyCookies&&d.memorySaver&&!d.muteBackground&&d.autoplay==Autoplay::Muted);
     std::istringstream orphan("browser-settings 1\nengine=custom\nnew-tab-page=custom\nhomepage=pas valide\n");State r=Read(orphan);assert(r.engine==Engine::DuckDuckGo&&r.newTabPage==NewTabPage::Blank);}
    // Search URLs take already-encoded terms; the panel order keeps three primary engines.
    assert(SearchUrl(Engine::DuckDuckGo,L"deux%20mots")==L"https://duckduckgo.com/?q=deux%20mots");
    assert(SearchUrl(Engine::Google,L"a")==L"https://www.google.com/search?q=a");
    assert(Primary(Engine::BraveSearch)&&!Primary(Engine::Bing)&&PrimaryEngines==3);
}
