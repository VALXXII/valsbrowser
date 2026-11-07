#include "../src/session.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace BrowserSession;
    State original{{"https://example.org/a?x=1&y=2","","https://example.org/été","about:blank","https://example.org/\"quoted\""},2};
    std::stringstream stream;Write(stream,original);auto restored=Read(stream);
    assert(restored.urls==original.urls&&restored.active==2);
    for(const auto* bad:{"","garbage","browser-session-1\n0\n129\n","browser-session-1\n-1\n1\n\"url\"","browser-session-1\n1\n1\n\"url\"","browser-session-1\n0\n2\n\"url\""}){std::istringstream input(bad);assert(Read(input).urls.empty());}
    State maximum;maximum.urls.resize(128,"https://example.org");maximum.active=127;
    std::stringstream full;Write(full,maximum);assert(Read(full).urls==maximum.urls);
}
