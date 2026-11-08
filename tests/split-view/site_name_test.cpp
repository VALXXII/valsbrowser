// Compact site names for the split-view chip and lifted tab: deterministic, local.
#include "../../src/site-name.h"
#include <cassert>
#include <cstdio>
using NativeChrome::SiteName::Compact;

int main() {
    // GitHub: long, localised and repeated titles collapse to the brand.
    assert(Compact(L"GitHub · Le changement est constant. GitHub vous permet de garder une longueur d’avance. · GitHub · GitHub", L"https://github.com/?locale=fr-fr") == L"GitHub");
    assert(Compact(L"GitHub · Change is constant. GitHub keeps you ahead. · GitHub", L"https://github.com/") == L"GitHub");
    assert(Compact(L"anthropics/claude-code: Claude Code is an agentic coding tool", L"https://github.com/anthropics/claude-code") == L"GitHub");
    assert(Compact(L"Notes · GitHub", L"https://gist.github.com/someone/abc") == L"GitHub Gist");
    // YouTube, whatever the video title or host variant.
    assert(Compact(L"Rick Astley - Never Gonna Give You Up (Official Video) - YouTube", L"https://www.youtube.com/watch?v=dQw4w9WgXcQ") == L"YouTube");
    assert(Compact(L"(12) YouTube", L"https://m.youtube.com/") == L"YouTube");
    assert(Compact(L"", L"https://youtu.be/abc") == L"YouTube");
    // The title segment that names the domain keeps the site's own spelling.
    assert(Compact(L"Home - BBC News", L"https://www.bbc.co.uk/news") == L"BBC News");
    assert(Compact(L"Le Monde.fr - Actualités et Infos en France et dans le monde", L"https://www.lemonde.fr/") == L"Le Monde.fr");
    assert(Compact(L"Accueil | Service-Public.fr", L"https://www.service-public.fr/") == L"Service-Public.fr");
    // Fallbacks: the brand is not assumed to be the first title segment.
    assert(Compact(L"Welcome to our shop!", L"https://foo-bar.io/cart") == L"Foo-bar");
    assert(Compact(L"Dashboard", L"http://localhost:3000/admin") == L"localhost");
    assert(Compact(L"Router", L"http://192.168.1.1/") == L"192.168.1.1");
    assert(Compact(L"Youtube", L"file:///C:/tests/split-qa/Youtube.html") == L"Youtube");
    assert(Compact(L"Nouvel onglet", L"") == L"Nouvel onglet");
    assert(Compact(L"Page · Section", L"about:blank") == L"Page");
    assert(Compact(L"", L"data:text/html,hello") == L"");
    std::puts("PASS: compact site names (GitHub long/localised, YouTube variants, title segment, domain, local file, fallbacks)");
}
