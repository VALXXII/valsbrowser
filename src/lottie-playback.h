#pragma once
#include "motion.h"
#include <algorithm>
#include <cmath>
namespace NativeChrome::Lottie {
// Host lifecycle for authored segments; ThorVG evaluates JSON paths and easing.
class Playback {
    Motion::Time copyStart_{},refreshStart_{};
    bool copied_=false,refreshing_=false;
    double copySeconds_=0,refreshSeconds_=0,peak_=10./19,segmentSeconds_=.3,from_=0,revealSeconds_=.3;
    // Motion lab tunables, captured when a segment starts (0 = authored timing).
    double tunedTurn_=0,copySpeed_=1,tunedDwell_=1.5;
    double turnSeconds_=0,seg_=.3,dwell_=1.5;
    static Motion::Clock::duration Seconds(double s){return std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(s));}
public:
    static double Elapsed(Motion::Time now,Motion::Time begin){return std::max(0.,std::chrono::duration<double>(now-begin).count());}
    void Configure(double copySeconds,double refreshSeconds,unsigned copyFrames=20){
        copySeconds_=copySeconds;refreshSeconds_=refreshSeconds;
        if(copyFrames>2){peak_=double(copyFrames/2)/(copyFrames-1);segmentSeconds_=copySeconds*(copyFrames/2-1)/copyFrames;}
        seg_=segmentSeconds_;turnSeconds_=refreshSeconds_;
    }
    // turnSeconds <= 0 keeps the asset's turn; copySpeed scales both copy morph segments.
    void Tune(double turnSeconds,double copySpeed,double dwellSeconds){tunedTurn_=turnSeconds;copySpeed_=std::max(.1,copySpeed);tunedDwell_=std::max(0.,dwellSeconds);}
    void CopySucceeded(Motion::Time now){
        double current=CopyActive(now)?CopyProgress(now):0;
        from_=current>peak_?1-current:current;
        seg_=segmentSeconds_/copySpeed_;dwell_=tunedDwell_;
        revealSeconds_=seg_*(1-from_/peak_);
        copyStart_=now;copied_=copySeconds_>0;
    }
    // One authored turn per activation. Repeated clicks cannot rewind an active turn.
    void RefreshBegin(Motion::Time now){if(RefreshActive(now))return;refreshStart_=now;refreshing_=refreshSeconds_>0;turnSeconds_=tunedTurn_>0?tunedTurn_:refreshSeconds_;}
    // Network completion does not shorten, extend or repeat the visual turn.
    void RefreshEnd(Motion::Time =Motion::Now()){}
    void Clear(){copied_=refreshing_=false;}
    bool CopyActive(Motion::Time now)const{return copied_&&Elapsed(now,copyStart_)<revealSeconds_+dwell_+seg_;}
    bool RefreshActive(Motion::Time now)const{return refreshing_&&Elapsed(now,refreshStart_)<turnSeconds_;}
    // End of the current (or last) turn; the Stop morph waits for it. Epoch when none played.
    Motion::Time TurnEnd()const{return refreshing_?refreshStart_+Seconds(turnSeconds_):Motion::Time{};}
    double CopyProgress(Motion::Time now)const{
        if(copySeconds_<=0)return 0;
        auto t=Elapsed(now,copyStart_);
        if(t<revealSeconds_)return from_+(peak_-from_)*t/revealSeconds_;
        if(t<revealSeconds_+dwell_)return peak_;
        return std::min(1.,peak_+(1-peak_)*(t-revealSeconds_-dwell_)/seg_);
    }
    double RefreshProgress(Motion::Time now)const{return turnSeconds_>0?std::min(1.,Elapsed(now,refreshStart_)/turnSeconds_):0;}
    Motion::Wake NextWake(Motion::Time now,bool reduced)const{
        if(reduced)return {};
        auto wake=RefreshActive(now)?Motion::Frame():Motion::Wake{};
        if(CopyActive(now)){
            auto t=Elapsed(now,copyStart_);
            if(t>=revealSeconds_&&t<revealSeconds_+dwell_)return Motion::Earlier(wake,Motion::Deadline(now,copyStart_+Seconds(revealSeconds_+dwell_)));
            return Motion::Earlier(wake,Motion::Frame());
        }
        return wake;
    }
};
}
