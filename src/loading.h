#pragma once
#include "motion.h"
#include <algorithm>
namespace NativeChrome::Loading {
// Navigation milestones, not a byte percentage. No perpetual loading timer.
inline constexpr float WidthMs=360, FadeMs=200;
struct Visual { float fraction=0, opacity=0; };
class State {
    uint64_t id_=0; bool active_=false,complete_=false;
    float from_=0,target_=0; Motion::Time changed_{},finished_{};
    static double Ms(Motion::Time now,Motion::Time start){return std::max(0.,std::chrono::duration<double,std::milli>(now-start).count());}
    float Fraction(Motion::Time now) const {
        float t=std::clamp(float(Ms(now,changed_))/WidthMs,0.f,1.f);
        return from_+(target_-from_)*Motion::Eased(t,Motion::Curve::EaseOut);
    }
public:
    void Start(uint64_t id,Motion::Time now) {
        if(active_&&!complete_&&id==id_)return; // redirects keep their progress
        id_=id;active_=true;complete_=false;from_=0;target_=.15f;changed_=now;
    }
    void Committed(Motion::Time now) { if(!active_||complete_)return;from_=Fraction(now);target_=.65f;changed_=now; }
    void Finish(uint64_t id,Motion::Time now) {
        if(!active_||complete_||id!=id_)return;
        from_=Fraction(now);target_=1;changed_=finished_=now;complete_=true;
    }
    void Clear(){active_=complete_=false;}
    Visual Sample(Motion::Time now,bool reduced=false) const {
        if(!active_)return {};
        float elapsed=float(Ms(now,finished_));
        float opacity=complete_?std::clamp(1-(elapsed-WidthMs)/FadeMs,0.f,1.f):1;
        return {reduced?target_:Fraction(now),opacity};
    }
    Motion::Wake NextWake(Motion::Time now,bool reduced)const {
        if(!active_)return {};
        if(complete_)return Ms(now,finished_)<WidthMs+FadeMs?Motion::Frame():Motion::Wake{};
        return !reduced&&Ms(now,changed_)<WidthMs?Motion::Frame():Motion::Wake{};
    }
};
}
