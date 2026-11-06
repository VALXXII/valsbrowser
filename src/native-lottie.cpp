#include "native-lottie.h"
#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace NativeChrome::Lottie {
struct Asset::Impl {
    bool initialized=false;
    tvg::Animation* animation=nullptr;
    tvg::SwCanvas* canvas=nullptr;
    unsigned side=0, count=0;
    double duration=0;
    std::vector<Frame> cache;
    std::vector<uint32_t> scratch;
    Impl(){ initialized=tvg::Initializer::init(0)==tvg::Result::Success; }
    ~Impl(){delete canvas; delete animation; if(initialized)tvg::Initializer::term();}
};
Asset::Asset()=default;
Asset::~Asset()=default;
void Asset::Clear(){impl_.reset();}
bool Asset::Load(const std::wstring& path){
    Clear(); auto p=std::make_unique<Impl>(); if(!p->initialized)return false;
    std::ifstream input(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if(!input||input.tellg()<=0||input.tellg()>256*1024)return false;
    input.seekg(0);std::string json((std::istreambuf_iterator<char>(input)),{});
    p->animation=tvg::Animation::gen();if(!p->animation)return false;
    if(p->animation->picture()->load(json.data(),static_cast<uint32_t>(json.size()),"lottie",nullptr,true)!=tvg::Result::Success)return false;
    p->duration=p->animation->duration();const auto frames=p->animation->totalFrame();
    if(!std::isfinite(p->duration)||p->duration<=0||p->duration>10||!std::isfinite(frames)||frames<2||frames>120)return false;
    p->count=static_cast<unsigned>(std::ceil(frames));
    p->cache.resize(p->count);impl_=std::move(p);return true;
}
double Asset::Duration()const{return impl_?impl_->duration:0;}
unsigned Asset::FrameCount()const{return impl_?impl_->count:0;}
size_t Asset::CacheBytes()const{size_t bytes=0;if(impl_){bytes=impl_->scratch.size()*4;for(auto& f:impl_->cache)bytes+=f.bgra.size()*4;}return bytes;}
const Frame* Asset::Sample(double progress,unsigned side){
    if(!impl_||!std::isfinite(progress)||side==0||side>64)return nullptr;
    auto& p=*impl_;
    if(p.side!=side){
        for(auto& f:p.cache)f={};
        p.side=side;p.scratch=std::vector<uint32_t>(side*side);
        bool first=!p.canvas;
        if(first)p.canvas=tvg::SwCanvas::gen();
        if(!p.canvas)return nullptr;
        if(p.canvas->target(p.scratch.data(),side,side,side,tvg::ColorSpace::ARGB8888)!=tvg::Result::Success)return nullptr;
        if(p.animation->picture()->size(static_cast<float>(side),static_cast<float>(side))!=tvg::Result::Success)return nullptr;
        // Animation and canvas each retain their own reference (Canvas::add refs).
        if(first&&p.canvas->add(p.animation->picture())!=tvg::Result::Success)return nullptr;
    }
    unsigned index=std::min(p.count-1,static_cast<unsigned>(std::clamp(progress,0.,1.)*(p.count-1)));
    auto& frame=p.cache[index];if(!frame.bgra.empty())return &frame;
    auto result=p.animation->frame(static_cast<float>(index));
    if(result!=tvg::Result::Success&&result!=tvg::Result::InsufficientCondition)return nullptr;
    if(!p.canvas||p.canvas->update()!=tvg::Result::Success||p.canvas->draw(true)!=tvg::Result::Success||p.canvas->sync()!=tvg::Result::Success)return nullptr;
    frame.side=side;frame.bgra=p.scratch;return &frame;
}
}
