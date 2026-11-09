#pragma once
#include <cmath>
#include <istream>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include <iomanip>

namespace BrowserSession {
// Split view groups (design/split-view.md): tab indices in panel order, their
// orientation (0 side by side, 1 stacked) and relative weights.
struct Group { int orientation=0; std::vector<int> members; std::vector<float> weights; };
struct State { std::vector<std::string> urls; int active=0; std::vector<Group> groups; };
// Format 2 = format 1 + a trailing group list. Format 1 files are still read.
inline void Write(std::ostream& out,const State& state){
    out << "browser-session-2\n" << state.active << '\n' << state.urls.size() << '\n';
    for(const auto& url:state.urls)out << std::quoted(url) << '\n';
    out << state.groups.size() << '\n';
    for(const auto& g:state.groups){
        out << g.orientation << ' ' << g.members.size();
        for(size_t i=0;i<g.members.size();++i)out << ' ' << g.members[i] << ' ' << (i<g.weights.size()?g.weights[i]:1.f);
        out << '\n';
    }
}
inline State Read(std::istream& in){
    State state;std::string version;int count=0;
    if(!std::getline(in,version))return {};
    const bool v2=version=="browser-session-2";
    if((!v2&&version!="browser-session-1")||!(in>>state.active>>count)||count<1||count>128||state.active<0||state.active>=count)return {};
    for(int i=0;i<count;++i){std::string url;if(!(in>>std::quoted(url))||url.size()>1024*1024)return {};state.urls.push_back(std::move(url));}
    if(!v2)return state;
    // A damaged group list never costs the tabs: the groups alone are dropped.
    int groups=0;if(!(in>>groups)||groups<0||groups>64)return state;
    std::set<int> used;std::vector<Group> parsed;
    for(int g=0;g<groups;++g){
        Group group;int n=0;if(!(in>>group.orientation>>n)||(group.orientation!=0&&group.orientation!=1)||n<2||n>count)return state;
        for(int i=0;i<n;++i){int m=0;float w=0;if(!(in>>m>>w)||m<0||m>=count||!std::isfinite(w)||w<=0||!used.insert(m).second)return state;group.members.push_back(m);group.weights.push_back(w);}
        parsed.push_back(std::move(group));
    }
    state.groups=std::move(parsed);return state;
}
}
