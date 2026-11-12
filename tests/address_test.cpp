#include "../src/address.h"
#include <cstdio>
#include <sstream>
#include <fstream>
#include <filesystem>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
using namespace AddressBar;
static std::wstring Joined(const Display& d){std::wstring s;for(auto& r:d.runs)s+=r.text;return s;}
int main(){
    // Readable display: scheme and www. hidden, domain primary, rest secondary.
    auto secure=Describe(L"https://www.github.com/VALXXII/browser/issues/20");
    CHECK(secure.security==Security::Secure&&secure.runs.size()==2);
    CHECK(secure.runs[0].text==L"github.com"&&secure.runs[0].tone==Tone::Primary);
    CHECK(secure.runs[1].text==L"/VALXXII/browser/issues/20"&&secure.runs[1].tone==Tone::Secondary);
    CHECK(Joined(Describe(L"https://example.com/"))==L"example.com");
    CHECK(Describe(L"https://docs.github.com/en").runs[0].text==L"docs.github.com");
    // HTTP: icon + label, never colour only.
    auto http=Describe(L"http://neverssl.com/online");
    CHECK(http.security==Security::Insecure&&std::wstring(http.chip())==L"Non sécurisé"&&std::wstring(http.icon())==L"lock-open");
    // Certificate error: struck https in the danger tone, full host.
    auto bad=Describe(L"https://www.expired.badssl.com/",true);
    CHECK(bad.security==Security::CertificateError&&bad.runs[0].text==L"https"&&bad.runs[0].strike&&bad.runs[0].tone==Tone::Danger);
    CHECK(bad.runs[2].text==L"www.expired.badssl.com"&&std::wstring(bad.chip())==L"Non sécurisé");
    // Local file: directory secondary, file name primary, %20 decoded.
    auto file=Describe(L"file:///C:/Users/me/Mes%20documents/rapport.pdf");
    CHECK(file.security==Security::File&&file.runs.size()==2&&file.runs[0].text==L"C:/Users/me/Mes documents/"&&file.runs[1].text==L"rapport.pdf");
    CHECK(Describe(L"").security==Security::Empty&&Describe(L"about:blank").security==Security::Empty);
    CHECK(Describe(L"data:text/plain,x").security==Security::Other);

    // Typed text classification and encoding (mirrors Destination()).
    CHECK(LooksLikeAddress(L"example.org/docs")&&LooksLikeAddress(L"localhost:3000")&&LooksLikeAddress(L"https://x"));
    CHECK(!LooksLikeAddress(L"météo lyon")&&!LooksLikeAddress(L"git")&&!LooksLikeAddress(L""));
    CHECK(PercentEncode(L"deux mots")==L"deux%20mots"&&PercentEncode(L"é")==L"%C3%A9");

    // History: one entry per page, root "/" equals none, titles follow.
    History h;
    CHECK(h.Record(L"https://github.com/",L"GitHub",1000));
    CHECK(h.Record(L"https://GitHub.com",L"",2000)&&h.Items().size()==1&&h.Items()[0].visits==2&&h.Items()[0].title==L"GitHub");
    CHECK(h.Record(L"https://github.com/VALXXII/browser/issues",L"Issues · VALXXII/browser",3000));
    CHECK(h.Record(L"https://gist.github.com/",L"Discover gists",4000));
    CHECK(h.Record(L"https://git-scm.com/docs",L"Git Reference",5000));
    CHECK(h.Record(L"https://example.org/",L"Example Domain",6000));
    CHECK(!h.Record(L"about:blank",L"",1)&&!h.Record(L"data:text/plain,x",L"",1));
    CHECK(h.SetTitle(L"https://example.org",L"Example"));

    // "git" proposes github.com first, completed inline to the whole host.
    auto git=Suggest(L"git",h);
    CHECK(git.rows.size()==5&&git.rows[0].kind==Kind::Site&&git.rows[0].text==L"github.com"&&git.rows[0].url==L"https://github.com/");
    CHECK(git.completion==L"hub.com");
    CHECK(git.rows[0].site==L"github.com"&&git.rows.back().site.empty());
    CHECK(SiteOf(L"https://www.YouTube.com/watch?v=1")==L"youtube.com"&&SiteOf(L"http://localhost:3000/")==L"localhost"&&SiteOf(L"file:///C:/a").empty());
    CHECK(git.rows[0].match==0&&git.rows[0].matchLength==3);
    CHECK(git.rows.back().kind==Kind::Search&&git.rows.back().text==L"git");
    bool gist=false;for(auto& r:git.rows)if(r.text==L"gist.github.com")gist=r.match==5;CHECK(gist);
    // After a deletion the completion is not re-applied, and Enter searches what is shown.
    CHECK(Suggest(L"git",h,false).completion.empty()&&Suggest(L"git",h,false).rows[0].kind==Kind::Search);
    // A path is never inlined: Enter must open exactly what the field shows.
    CHECK(Suggest(L"github.com/V",h).completion.empty());
    // Only deep pages visited: the host row still exists and opens the root.
    History deep;deep.Record(L"https://news.ycombinator.com/item?id=1",L"Item",1);deep.Record(L"https://news.ycombinator.com/item?id=2",L"Item 2",2);
    auto news=Suggest(L"news",deep);
    CHECK(news.rows[0].text==L"news.ycombinator.com"&&news.rows[0].host&&news.rows[0].url==L"https://news.ycombinator.com/"&&news.completion==L".ycombinator.com");
    History single;single.Record(L"https://git-scm.com/docs",L"Git Reference",1);
    CHECK(Suggest(L"git",single).rows.size()==2&&Suggest(L"git",single).rows[1].text==L"git-scm.com/docs");
    // No match: only the search row.
    auto weather=Suggest(L"météo lyon",h);
    CHECK(weather.rows.size()==1&&weather.rows[0].kind==Kind::Search&&weather.completion.empty());
    // Unvisited address: "Ouvrir l'adresse" first, search second.
    auto docs=Suggest(L"example.net/docs",h);
    CHECK(docs.rows.size()==2&&docs.rows[0].kind==Kind::Address&&docs.rows[1].kind==Kind::Search);
    CHECK(docs.rows[0].site==L"example.net"&&Suggest(L"youtube.com",h).rows[0].site==L"youtube.com");
    // Title matches rank after URL matches.
    auto title=Suggest(L"reference",h);
    CHECK(title.rows[0].kind==Kind::Search&&title.rows[1].text==L"git-scm.com/docs"&&title.rows[1].match==std::wstring::npos);
    CHECK(Suggest(L"   ",h).rows.empty());
    // Removing a suggestion.
    CHECK(h.Remove(L"https://git-scm.com/docs")&&Suggest(L"git-scm",h).rows.size()==1);
    CHECK(h.HasSite(L"github.com"));
    CHECK(h.RemoveHost(L"github.com")&&Suggest(L"github",h).rows[1].text==L"gist.github.com");
    CHECK(!h.HasSite(L"github.com")&&h.HasSite(L"gist.github.com"));

    // Persistence round trip, garbage rejected, tabs in titles neutralised.
    History saved;saved.Record(L"https://example.org/été",L"Tab\there",7);
    std::stringstream stream;saved.Write(stream);auto restored=History::Read(stream);
    CHECK(restored.Items().size()==1&&restored.Items()[0].url==L"https://example.org/été"&&restored.Items()[0].title==L"Tab here"&&restored.Items()[0].last==7);
    for(const char* bad:{"","garbage","browser-history 1\nx\t1\thttps://a.b\tT\n","browser-history 1\n1\t1\tjavascript:alert(1)\tT\n"}){std::istringstream in(bad);CHECK(History::Read(in).Items().empty());}
    History full;for(int i=0;i<1100;++i)full.Record(L"https://site"+std::to_wstring(i)+L".com/",L"",i);
    CHECK(full.Items().size()==History::Limit);
    // Bundled popular sites: offered before any visit, after the history.
    std::istringstream list("# browser popular sites 1\n# comment\ngoogle.com\tGoogle\nyoutube.com\tYouTube\ngoogle.fr\tGoogle France\nmail.google.com\tGmail\tgmail\nx.com\tX\ttwitter\nbad host\tNo\n");
    auto popular=ReadPopular(list);
    CHECK(popular.size()==5&&popular[3].aliases.size()==1&&popular[3].aliases[0]==L"gmail"&&popular[1].name==L"YouTube");
    {std::istringstream wrong("garbage\nyoutube.com\n");CHECK(ReadPopular(wrong).empty());}
    History none;
    auto you=Suggest(L"yout",none,true,&popular);
    CHECK(you.rows[0].text==L"youtube.com"&&you.rows[0].popular&&you.rows[0].url==L"https://youtube.com/"&&you.completion==L"ube.com"&&you.rows[0].title==L"YouTube");
    CHECK(Suggest(L"y",none,true,&popular).completion.empty()&&Suggest(L"y",none,true,&popular).rows[0].kind==Kind::Search);
    auto goo=Suggest(L"google",none,true,&popular);
    CHECK(goo.rows[0].text==L"google.com"&&goo.completion==L".com"&&goo.rows[1].text==L"google.fr");
    auto gma=Suggest(L"gma",none,true,&popular);
    CHECK(gma.rows[0].text==L"mail.google.com"&&gma.rows[0].alias==L"gmail"&&gma.completion==L"il"&&gma.rows[0].match==std::wstring::npos);
    auto gmail=Suggest(L"gmail",none,true,&popular);
    CHECK(gmail.rows[0].text==L"mail.google.com"&&gmail.completion.empty()&&gmail.rows.back().kind==Kind::Search);
    CHECK(Suggest(L"twitter",none,true,&popular).rows[0].text==L"x.com");
    // The history wins over the list, and a visited host is not repeated.
    History mine;mine.Record(L"https://youtube.fr/",L"YouTube FR",1);mine.Record(L"https://youtube.com/",L"YouTube",2);
    auto ranked=Suggest(L"yout",mine,true,&popular);
    CHECK(!ranked.rows[0].popular&&!ranked.rows[1].popular);
    int youtubes=0;for(auto& r:ranked.rows)if(r.text==L"youtube.com")++youtubes;CHECK(youtubes==1);
    // The shipped list parses, and every site ships its icon (run from the repository root).
    {std::ifstream shipped("assets/popular-sites.txt",std::ios::binary);auto all=ReadPopular(shipped);
     CHECK(all.size()>=100);
     for(auto& p:all){std::error_code e;const bool icon=std::filesystem::exists(L"assets/sites/"+p.host+L".png",e)||std::filesystem::exists(L"assets/sites/"+p.host+L".ico",e);if(!icon)std::fwprintf(stderr,L"missing icon %ls\n",p.host.c_str());CHECK(icon);}
     auto you=Suggest(L"yout",History{},true,&all);CHECK(you.rows[0].text==L"youtube.com"&&you.completion==L"ube.com");
     auto gm=Suggest(L"gma",History{},true,&all);CHECK(gm.rows[0].text==L"mail.google.com"&&gm.completion==L"il");}
    // New tab page: most visited hosts (pages summed), best first, http(s) only, capped.
    {History t;t.Record(L"https://a.example/",L"A",1);t.Record(L"https://www.b.example/x",L"",5);t.Record(L"https://b.example/y",L"",6);
     t.Record(L"http://c.example/",L"C",7);t.Record(L"file:///C:/f.txt",L"",9);t.Record(L"https://a.example/",L"A",2);t.Record(L"https://a.example/z",L"",3);
     auto top=TopSites(t,8);CHECK(top.size()==3&&top[0].host==L"a.example"&&top[0].visits==3&&top[0].root&&top[0].url==L"https://a.example/");
     CHECK(top[1].host==L"b.example"&&top[1].url==L"https://www.b.example/"&&!top[1].root&&top[2].host==L"c.example");
     CHECK(TopSites(t,2).size()==2&&TopSites(History{},8).empty());}
    std::puts("address tests passed");
    return 0;
}
