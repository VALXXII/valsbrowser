#pragma once
#include "motion.h"
#include <algorithm>
#include <cmath>
namespace NativeChrome::Lottie {
// Host lifecycle for authored segments; ThorVG evaluates JSON paths and easing.
class Playback {
    Motion::Time copyStart_{},refreshStart_{},refreshEnd_{};
    bool copied_=false,refreshing_=false,finishing_=false;
    double copySeconds_=0,refreshSeconds_=0,peak_=10./19,segmentSeconds_=.3,from_=0,revealSeconds_=.3;
    static Motion::Clock::duration Seconds(double s){return std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(s));}
public:
    static double Elapsed(Motion::Time now,Motion::Time begin){return std::max(0.,std::chrono::duration<double>(now-begin).count());}
    void Configure(double copySeconds,double refreshSeconds,unsigned copyFrames=20){
        copySeconds_=copySeconds;refreshSeconds_=refreshSeconds;
        if(copyFrames>2){peak_=double(copyFrames/2)/(copyFrames-1);segmentSeconds_=copySeconds*(copyFrames/2-1)/copyFrames;}
    }
    void CopySucceeded(Motion::Time now){
        double current=CopyActive(now)?CopyProgress(now):0;
        from_=current>peak_?1-current:current;
        revealSeconds_=segmentSeconds_*(1-from_/peak_);
        copyStart_=now;copied_=copySeconds_>0;
    }
    void RefreshBegin(Motion::Time now){if(!refreshing_||!RefreshActive(now))refreshStart_=now;refreshing_=refreshSeconds_>0;finishing_=false;}
    void RefreshEnd(Motion::Time now=Motion::Now()){
        if(!refreshing_||refreshSeconds_<=0)return;
        auto elapsed=Elapsed(now,refreshStart_);
        refreshEnd_=refreshStart_+Seconds(std::max(1.,std::ceil(elapsed/refreshSeconds_))*refreshSeconds_);
        finishing_=true;
    }
    void Clear(){copied_=refreshing_=finishing_=false;}
    bool CopyActive(Motion::Time now)const{return copied_&&Elapsed(now,copyStart_)<revealSeconds_+1.5+segmentSeconds_;}
    bool RefreshActive(Motion::Time now)const{return refreshing_&&Elapsed(now,refreshStart_)<5.&&(!finishing_||now<refreshEnd_);}
    double CopyProgress(Motion::Time now)const{
        if(copySeconds_<=0)return 0;
        auto t=Elapsed(now,copyStart_);
        if(t<revealSeconds_)return from_+(peak_-from_)*t/revealSeconds_;
        if(t<revealSeconds_+1.5)return peak_;
        return std::min(1.,peak_+(1-peak_)*(t-revealSeconds_-1.5)/segmentSeconds_);
    }
    double RefreshProgress(Motion::Time now)const{return refreshSeconds_>0?std::fmod(Elapsed(now,refreshStart_),refreshSeconds_)/refreshSeconds_:0;}
    Motion::Wake NextWake(Motion::Time now,bool reduced)const{
        if(reduced)return {};
        auto wake=RefreshActive(now)?Motion::Frame():Motion::Wake{};
        if(CopyActive(now)){
            auto t=Elapsed(now,copyStart_);
            if(t>=revealSeconds_&&t<revealSeconds_+1.5)return Motion::Earlier(wake,Motion::Deadline(now,copyStart_+Seconds(revealSeconds_+1.5)));
            return Motion::Earlier(wake,Motion::Frame());
        }
        return wake;
    }
};
}
