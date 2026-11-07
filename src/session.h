#pragma once
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <iomanip>

namespace BrowserSession {
struct State { std::vector<std::string> urls; int active=0; };
inline void Write(std::ostream& out,const State& state){
    out << "browser-session-1\n" << state.active << '\n' << state.urls.size() << '\n';
    for(const auto& url:state.urls)out << std::quoted(url) << '\n';
}
inline State Read(std::istream& in){
    State state;std::string version;int count=0;
    if(!std::getline(in,version)||version!="browser-session-1"||!(in>>state.active>>count)||count<1||count>128||state.active<0||state.active>=count)return {};
    for(int i=0;i<count;++i){std::string url;if(!(in>>std::quoted(url))||url.size()>1024*1024)return {};state.urls.push_back(std::move(url));}
    return state;
}
}
