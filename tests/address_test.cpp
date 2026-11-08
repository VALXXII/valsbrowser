#include "../src/address.h"
#include <cstdio>
#include <sstream>
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
    std::puts("address tests passed");
    return 0;
}
