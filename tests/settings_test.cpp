#include "../src/settings.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSettings;
    // Defaults: DuckDuckGo, tabs restored, system Downloads folder, no prompt, system motion.
    State defaults;
    assert(defaults.engine==Engine::DuckDuckGo&&defaults.restoreTabs&&defaults.downloadFolder.empty()&&!defaults.askWhereToSave&&!defaults.reduceMotion);
    // Round trip, including a non-ASCII folder.
    State original;original.engine=Engine::Qwant;original.restoreTabs=false;original.downloadFolder=L"D:\\Téléchargements\\été";original.askWhereToSave=true;original.reduceMotion=true;original.searchSuggestions=false;original.tracking=Tracking::Strict;original.passwords=false;original.autofill=false;original.clearOnExit=true;original.sleepMinutes=30;original.zoomPercent=125;original.customSearch=L"https://exemple.fr/?q=%s";original.newTabPage=NewTabPage::Custom;original.homepage=L"https://exemple.fr/accueil";original.tabPosition=TabPosition::NextToActive;original.confirmCloseTabs=false;original.closeWithLastTab=true;original.adBlock=false;original.httpsOnly=true;original.blockThirdPartyCookies=true;original.memorySaver=false;original.muteBackground=true;original.autoplay=Autoplay::Allowed;original.grayscaleText=true;
    std::stringstream stream;Write(stream,original);assert(Read(stream)==original);
    for(int i=0;i<EngineCount;++i){State s;s.engine=static_cast<Engine>(i);s.customSearch=L"https://s.fr/?q=%s";std::stringstream io;Write(io,s);assert(Read(io).engine==s.engine);}
    // Bookmarks visibility and the future custom accent must survive restart.
    for(auto bar:{BookmarksBar::Auto,BookmarksBar::Always,BookmarksBar::Never}){
        State s;s.bookmarksBar=bar;s.accent=0xA054CB;std::stringstream io;Write(io,s);assert(Read(io)==s);
    }
    {std::istringstream invalid("browser-settings 1\nbookmarks-bar=invalid\naccent=#XYZXYZ\n");assert(Read(invalid)==State{});}
    // « Position des onglets » (#127): En haut by default (new installs, files without the key); the three values round
    // trip; unknown values are ignored; the trial builds' two keys migrate (layout + sidebar), never written back.
    {assert(State{}.tabsPosition==TabsPosition::Top&&!SidePanel(TabsPosition::Top)&&SidePanel(TabsPosition::Side)&&SidePanel(TabsPosition::Compact));
     for(int i=0;i<TabsPositionCount;++i){State s;s.tabsPosition=static_cast<TabsPosition>(i);std::stringstream io;Write(io,s);const std::string text=io.str();assert(text.find("tabs-position=")!=std::string::npos&&text.find("layout=")==std::string::npos&&text.find("sidebar=")==std::string::npos);assert(Read(io)==s);}
     {std::istringstream f("browser-settings 1\nengine=bing\n");assert(Read(f).tabsPosition==TabsPosition::Top);}
     {std::istringstream f("browser-settings 1\r\ntabs-position=compact\r\ntabs-position=diagonal\r\n");assert(Read(f).tabsPosition==TabsPosition::Compact);}
     {std::istringstream f("browser-settings 1\nlayout=side-panel\nsidebar=compact\n");assert(Read(f).tabsPosition==TabsPosition::Compact);}
     {std::istringstream f("browser-settings 1\nsidebar=complete\nlayout=side-panel\n");assert(Read(f).tabsPosition==TabsPosition::Side);}
     {std::istringstream f("browser-settings 1\nlayout=top-bar\nsidebar=compact\n");assert(Read(f).tabsPosition==TabsPosition::Top);}
     {std::istringstream f("browser-settings 1\nsidebar=compact\n");assert(Read(f).tabsPosition==TabsPosition::Top);}
     {std::istringstream f("browser-settings 1\nlayout=side-panel\ntabs-position=top\n");assert(Read(f).tabsPosition==TabsPosition::Top);}
     // The side panel's width (#127 « Redimensionner »): 248 by default, written, read back, held between 220 and 360.
     {State d;assert(d.sideWidth==248);for(int w:{220,248,301,360}){State s;s.sideWidth=w;std::stringstream io;Write(io,s);assert(io.str().find("side-width="+std::to_string(w))!=std::string::npos);assert(Read(io)==s);}
      auto width=[](const char* v){std::istringstream f(std::string("browser-settings 1\nside-width=")+v+"\n");return Read(f).sideWidth;};
      assert(width("100")==220&&width("999")==360&&width("290")==290&&width("")==248&&width("wide")==248&&width("-5")==248&&width("12345")==248);}}
    // Annotations: prompt on, no « Plus tard », 30 days by default (V3: no launcher setting).
    {State d;assert(d.annotationConnectPrompt&&d.annotationConnectDeclines==0&&d.annotationRetentionDays==30&&ConnectPromptActive(d));
     // Round trip of every value.
     for(int days:RetentionChoices)for(int n=0;n<=MaxConnectDeclines;++n){State s;s.annotationConnectPrompt=!(n%2);s.annotationConnectDeclines=n;s.annotationRetentionDays=days;std::stringstream io;Write(io,s);assert(Read(io)==s);}
     // « Plus tard » on the default-browser card: stored, read back, invalid values ignored; away 30 days.
     {State s;s.defaultBrowserLaterAt=1800000000;std::stringstream io;Write(io,s);assert(Read(io)==s);
      std::istringstream bad("browser-settings 1\ndefault-browser-later=-5\ndefault-browser-later=12x\n");assert(Read(bad).defaultBrowserLaterAt==0);
      assert(DefaultBrowserCardDue(State{},0)&&!DefaultBrowserCardDue(s,1800000000+29LL*86400)&&DefaultBrowserCardDue(s,1800000000+30LL*86400)&&!DefaultBrowserCardDue(s,1800000000-10));}
     // A file from before these keys: defaults (missing keys keep them).
     {std::istringstream old("browser-settings 1\nengine=bing\n");State r=Read(old);assert(r.annotationConnectPrompt&&r.annotationConnectDeclines==0&&r.annotationRetentionDays==30);}
     // Invalid values are ignored, valid ones read (CRLF too).
     {std::istringstream odd("browser-settings 1\r\nannotation-launcher=maybe\r\nannotation-connect-prompt=2\r\nannotation-connect-declines=9\r\nannotation-retention-days=45\r\n");State r=Read(odd);
      assert(r.annotationConnectPrompt&&r.annotationConnectDeclines==0&&r.annotationRetentionDays==30);}
     {std::istringstream odd("browser-settings 1\nannotation-connect-declines=-1\nannotation-retention-days=07\nannotation-retention-days= 90\n");State r=Read(odd);assert(r.annotationConnectDeclines==0&&r.annotationRetentionDays==30);}
     {std::istringstream ok("browser-settings 1\r\nannotation-launcher=0\r\nannotation-connect-prompt=0\r\nannotation-connect-declines=2\r\nannotation-retention-days=7\r\n");State r=Read(ok);
      assert(!r.annotationConnectPrompt&&r.annotationConnectDeclines==2&&r.annotationRetentionDays==7);
      // V3: the V2 « annotation-launcher » key is read and ignored, never written back.
      std::stringstream io;Write(io,r);assert(io.str().find("annotation-launcher")==std::string::npos&&Read(io)==r);}
     // The switch: off after 3 « Plus tard »; turning it on again restarts the count at 0; off keeps the count.
     {State s;s.annotationConnectDeclines=MaxConnectDeclines;assert(!ConnectPromptActive(s));SetConnectPrompt(s,true);assert(ConnectPromptActive(s)&&s.annotationConnectDeclines==0);
      s.annotationConnectDeclines=2;SetConnectPrompt(s,false);assert(!ConnectPromptActive(s)&&s.annotationConnectDeclines==2);SetConnectPrompt(s,true);assert(s.annotationConnectDeclines==0);}
     assert(RetentionLabel(30)==L"30 jours"&&RetentionCount==3);}
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
     State d;assert(d.newTabPage==NewTabPage::Blank&&d.tabPosition==TabPosition::End&&d.confirmCloseTabs&&!d.closeWithLastTab&&d.adBlock&&!d.httpsOnly&&!d.blockThirdPartyCookies&&d.memorySaver&&!d.muteBackground&&d.autoplay==Autoplay::Muted&&!d.grayscaleText);
     std::istringstream orphan("browser-settings 1\nengine=custom\nnew-tab-page=custom\nhomepage=pas valide\n");State r=Read(orphan);assert(r.engine==Engine::DuckDuckGo&&r.newTabPage==NewTabPage::Blank);}
    // Search URLs take already-encoded terms; the panel order keeps three primary engines.
    assert(SearchUrl(Engine::DuckDuckGo,L"deux%20mots")==L"https://duckduckgo.com/?q=deux%20mots");
    assert(SearchUrl(Engine::Google,L"a")==L"https://www.google.com/search?q=a");
    assert(Primary(Engine::BraveSearch)&&!Primary(Engine::Bing)&&PrimaryEngines==3);
}
