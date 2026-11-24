#include "../src/address.h"
#include "../src/local-pill.h"
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
    CHECK(Describe(L"").security==Security::Empty);
    // SEC-24: a blank document that is not the home tab (the host never describes the home tab) shows its address,
    // never the empty field of the browser's own new tab.
    {auto blank=Describe(L"about:blank");CHECK(blank.security==Security::Other&&Joined(blank)==L"about:blank");}
    CHECK(Describe(L"data:text/plain,x").security==Security::Other);
    // « Local » (Paper #127 « Adresse · pastille « Local » (validée) »): a project on this computer, any port, http or https,
    // instead of « Non sécurisé »; the same rule as the annotation mode (AnnotationSites::Place::Local).
    for(const wchar_t* local:{L"http://localhost:5173/checkout",L"https://localhost/",L"http://LOCALHOST:3000",L"http://127.0.0.1:8080/x",L"https://127.0.0.1",
                              L"http://127.4.5.6:9000/",L"http://[::1]:8080/a",L"https://[::1]/",L"http://foo.localhost:5173/",L"https://app.dev.localhost/x?y=1#z",L"http://localhost:65535/"}){
        auto d=Describe(local);
        CHECK(d.security==Security::Local&&std::wstring(d.chip())==L"Local"&&std::wstring(d.icon())==L"monitor"&&std::wstring(d.tip())==L"Projet sur cet ordinateur");
        CHECK(d.security==Security::Local&&AnnotationSites::Of(NativeChrome::Downloads::ToUtf8(local))==AnnotationSites::Place::Local);   // never disagrees with the mode
    }
    {auto d=Describe(L"http://localhost:5173/checkout");   // Paper 1ZX1-0: « localhost » primary, « :5173/checkout » secondary
     CHECK(d.runs.size()==2&&d.runs[0].text==L"localhost"&&d.runs[0].tone==Tone::Primary&&d.runs[1].text==L":5173/checkout"&&d.runs[1].tone==Tone::Secondary);}
    {auto d=Describe(L"http://[::1]:8080/a");CHECK(d.runs.size()==2&&d.runs[0].text==L"[::1]"&&d.runs[1].text==L":8080/a");}
    {auto d=Describe(L"https://localhost/");CHECK(d.runs.size()==1&&d.runs[0].text==L"localhost");}
    {auto d=Describe(L"http://foo.localhost/");CHECK(d.runs.size()==1&&d.runs[0].text==L"foo.localhost");}
    // A development server's own certificate: still « Local » (the traffic never leaves the machine).
    CHECK(Describe(L"https://localhost:8443/",true).security==Security::Local);
    // Not local: live sites keep « Non sécurisé » / the lock; look-alikes, LAN addresses and credentials are not local.
    for(const wchar_t* online:{L"http://neverssl.com/online",L"http://localhost.example.com/",L"http://127.0.0.1.example.com/",L"http://192.168.1.10:3000/",
                               L"http://10.0.0.2/",L"http://0x7f.1/",L"http://2130706433/",L"http://user@localhost:3000/"}){
        auto d=Describe(online);CHECK(d.security==Security::Insecure&&std::wstring(d.chip())==L"Non sécurisé");
    }
    CHECK(Describe(L"https://example.com/").security==Security::Secure);
    CHECK(Describe(L"https://localhost.example.com/",true).security==Security::CertificateError);
    // The pill's colours follow the profile (src/local-pill.h): Designer's reference values (Paper, 2026-11-24), the ink
    // derived by the profile button's own code (ProfileColor::Ink) from each profile's colour.
    {
        using ProfileColor::Rgb8;
        auto ink=[](const wchar_t* hex){int hue=0;float chroma=0;ProfileColor::FromHex(hex,hue,chroma);return ProfileColor::Ink(hue,chroma);};
        struct Row{const wchar_t* name;Rgb8 ink,fieldRest,fieldHover,bareRest,bareHover;};
        const Row rows[]={
            {L"Personnel",ProfileColor::Neutral(),{0x37,0x37,0x37},{0x3F,0x3F,0x3F},{0x27,0x27,0x27},{0x30,0x30,0x30}},
            {L"Travail",ink(L"#6FD087"),{0x30,0x3A,0x32},{0x34,0x44,0x38},{0x20,0x2A,0x22},{0x25,0x35,0x29}},
            // Paper's violet swatch #B49BFF is darker than any profile ink (OKLCH L 0.78): the formula is checked with it, and
            // a real violet profile's ink (#BCA7FF, the same hue) with the contrast rule just below.
            {L"Violet",{0xB4,0x9B,0xFF},{0x37,0x34,0x3E},{0x3F,0x3B,0x4B},{0x27,0x24,0x2E},{0x30,0x2C,0x3C}},
            {L"Violet (profile ink)",ink(L"#B49BFF"),ProfileColor::Over(ink(L"#B49BFF"),.10,{0x29,0x29,0x29}),ProfileColor::Over(ink(L"#B49BFF"),.16,{0x29,0x29,0x29}),ProfileColor::Over(ink(L"#B49BFF"),.10,{0x17,0x17,0x17}),ProfileColor::Over(ink(L"#B49BFF"),.16,{0x17,0x17,0x17})},
        };
        CHECK(rows[0].ink==(Rgb8{0xB3,0xB3,0xB3})&&rows[1].ink==(Rgb8{0x6F,0xD0,0x87})&&rows[3].ink==(Rgb8{0xBC,0xA7,0xFF}));   // the profiles' inks (Personnel, Travail exact)
        for(const auto& r:rows){
            const auto field=LocalPill::For(r.ink,true),bare=LocalPill::For(r.ink,false);
            CHECK(field.ink==r.ink&&field.plate==r.fieldRest&&field.hover==r.fieldHover&&field.divider==(Rgb8{0x54,0x54,0x54}));
            CHECK(bare.ink==r.ink&&bare.plate==r.bareRest&&bare.hover==r.bareHover&&bare.divider==(Rgb8{0x45,0x45,0x45}));
            // Text / plate contrast >= 5:1 with the inks the app draws (Paper's violet swatch is not one: 4.68:1 on its hover).
            const bool appInk=std::wstring(r.name)!=L"Violet";
            for(Rgb8 plate:{field.plate,field.hover,bare.plate,bare.hover}){
                const double c=ProfileColor::Contrast(r.ink,plate);
                std::printf("local pill %ls ink #%02X%02X%02X on #%02X%02X%02X: %.2f:1\n",r.name,r.ink.r,r.ink.g,r.ink.b,plate.r,plate.g,plate.b,c);
                if(appInk&&c<5){std::fprintf(stderr,"FAIL %ls: contrast %.2f on #%02X%02X%02X\n",r.name,c,plate.r,plate.g,plate.b);return 1;}
            }
        }
    }
    // Files keep « Fichier » (the mode treats them as local too, the address says what they are).
    {auto d=Describe(L"file:///C:/projets/boutique/index.html");CHECK(d.security==Security::File&&std::wstring(d.chip())==L"Fichier");}
    {auto d=Describe(L"file://localhost/C:/a.html");CHECK(d.security==Security::File);}

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
    // Local files typed in the address field (or given on the command line): a Windows path or a file: link
    // is an address, never a search, and never a « site » with a favicon to fetch.
    {CHECK(LooksLikeAddress(L"C:\\Users\\Ana\\My Pages\\index.html"));                    // a space in a folder name
     CHECK(LooksLikeAddress(L"\"C:\\Users\\Ana\\page.html\""));                            // Explorer's « Copier en tant que chemin d'accès »
     CHECK(LooksLikeAddress(L"file:///C:/Users/Ana/My%20Pages/index.html"));
     CHECK(LooksLikeAddress(L"\\\\nas\\partage\\doc.html"));
     auto path=Suggest(L"C:\\Users\\Ana\\My Pages\\index.html",History{});
     CHECK(path.rows.size()==2&&path.rows[0].kind==Kind::Address&&path.rows[0].site.empty()&&path.completion.empty());
     auto file=Suggest(L"file:///C:/x/a.html",History{});
     CHECK(file.rows[0].kind==Kind::Address&&file.rows[0].site.empty());}
    // Resolve: the host's Destination() and the command line, one rule.
    {auto url=[](const std::wstring& typed){const auto r=Resolve(typed);return r.kind==Typed::Url?r.value:L"<"+std::to_wstring((int)r.kind)+L">"+r.value;};
     // Existing behaviours, unchanged.
     CHECK(Resolve(L"   ").kind==Typed::Empty&&url(L"about:blank")==L"about:blank");
     CHECK(Resolve(L"browser://debug").kind==Typed::Sandbox&&Resolve(L"BROWSER://Sandbox").kind==Typed::Sandbox);
     CHECK(url(L" https://example.com/a?b=1 ")==L"https://example.com/a?b=1"&&url(L"HTTP://x.y")==L"HTTP://x.y");
     CHECK(url(L"example.org/docs")==L"https://example.org/docs"&&url(L"localhost:3000")==L"https://localhost:3000"&&url(L"127.0.0.1:8080/x")==L"https://127.0.0.1:8080/x");
     CHECK(Resolve(L"météo lyon").kind==Typed::Search&&Resolve(L"météo lyon").value==L"météo lyon"&&Resolve(L"git").kind==Typed::Search);
     CHECK(Resolve(L"ftp://x.y").kind==Typed::Search&&Resolve(L"javascript:alert(1)").kind==Typed::Search&&Resolve(L"mailto:a@b.c").kind==Typed::Url);   // (mailto:a@b.c has a dot: https://, as before)
     // SEC-21: an http(s) address the engine refuses is searched, never handed to Navigate (which would stop the browser).
     for(const wchar_t* bad:{L"http://",L"https://",L"https://a b",L"http://user@",L"https://a:99999/",L"https://a:x/",L"http://a<b.com",L"http://\x7f.com",L"https://1.2.3.4.5",L"https://300.1.1.1",L"https://1.2.3.256"})
         CHECK(!Navigable(bad)&&Resolve(bad).kind==Typed::Search);
     CHECK(Resolve(L"1.2.3.4.5").kind==Typed::Search&&Resolve(L"192.168.1.300").kind==Typed::Search);
     for(const wchar_t* good:{L"https://example.com",L"http://localhost:3000/x",L"https://127.0.0.1",L"https://1.2.3.4./",L"https://[::1]:8080/",L"https://user:pw@host.test/",L"https://a.com:/",L"https://3.14",L"https://10.0.0.1:65535/a b",L"https://0x7f.1/",L"file:///C:/a b.html",L"about:blank",L""})
         CHECK(Navigable(good));
     CHECK(url(L"https://1.2.3.4/")==L"https://1.2.3.4/"&&url(L"3.14")==L"https://3.14"&&url(L"https://a.com/a b")==L"https://a.com/a b");
     // file: links as typed.
     CHECK(url(L"file:///C:/x.html")==L"file:///C:/x.html"&&url(L"FILE:///c:/a%20b/c.html")==L"FILE:///c:/a%20b/c.html"&&url(L"file://nas/share/a.html")==L"file://nas/share/a.html");
     // Drive paths, either slash, quoted or not; spaces, %, # and non-ASCII names encoded.
     CHECK(url(L"C:\\x.html")==L"file:///C:/x.html");
     CHECK(url(L"c:/Users/Ana/page.html")==L"file:///c:/Users/Ana/page.html");
     CHECK(url(L"C:\\Users\\Ana\\My Pages\\index.html")==L"file:///C:/Users/Ana/My%20Pages/index.html");
     CHECK(url(L"\"C:\\Users\\Ana\\My Pages\\index.html\"")==L"file:///C:/Users/Ana/My%20Pages/index.html");
     CHECK(url(L"  \"D:\\a.html\"  ")==L"file:///D:/a.html");
     CHECK(url(L"C:\\docs\\100% #1?.html")==L"file:///C:/docs/100%25%20%231%3F.html");
     CHECK(url(L"C:\\Données\\été.html")==L"file:///C:/Donn%C3%A9es/%C3%A9t%C3%A9.html");
     CHECK(url(L"C:\\a\\b(1)&c=d;e.html")==L"file:///C:/a/b(1)&c=d;e.html");
     CHECK(url(L"\\\\?\\C:\\long\\a.html")==L"file:///C:/long/a.html");
     // UNC shares: the server is the host.
     CHECK(url(L"\\\\NAS\\partage\\dossier\\doc.html")==L"file://nas/partage/dossier/doc.html");
     CHECK(url(L"\\\\nas\\partage\\mon doc.html")==L"file://nas/partage/mon%20doc.html");
     CHECK(url(L"\\\\?\\UNC\\nas\\partage\\a.html")==L"file://nas/partage/a.html");
     // Not paths: hosts with a port, a lone drive, a single backslash.
     CHECK(url(L"a:80")==L"<3>a:80"&&url(L"C:")==L"<3>C:"&&url(L"\\x")==L"<3>\\x");
     // The address bar shows a typed path as the local file it is (Security::File, file name primary).
     {auto d=Describe(FileUrlFromPath(L"C:\\Users\\Ana\\My Pages\\index.html"));
      CHECK(d.security==Security::File&&d.runs.size()==2&&d.runs[0].text==L"C:/Users/Ana/My Pages/"&&d.runs[1].text==L"index.html"&&d.runs[1].tone==Tone::Primary);
      auto u=Describe(FileUrlFromPath(L"\\\\nas\\partage\\doc.html"));CHECK(u.security==Security::File&&u.runs.back().text==L"doc.html");}
     // History keeps local files (Recordable) and bookmarks still accept file: (Normalize), unchanged.
     CHECK(Recordable(FileUrlFromPath(L"C:\\x.html"))&&Normalize(L"file:///C:/x.html")==L"file:///C:/x.html");}
    std::puts("address tests passed");
    return 0;
}
