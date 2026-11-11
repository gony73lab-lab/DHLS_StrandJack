#pragma once
#include <deque>
#include <array>
#include <limits>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace DHLS_StrandJack::Control {
class StrokeAverage {
    std::deque<double> values_;
public:
    void Clear(){values_.clear();}
    double Push(double value,int count){
        if(!std::isfinite(value)){Clear();return value;}
        count=std::clamp(count,1,1000);values_.push_back(value);
        while(values_.size()>static_cast<std::size_t>(count))values_.pop_front();
        double sum=0;for(double v:values_)sum+=v;return sum/values_.size();
    }
};
// Direction-normalized travel from START. A leading port pauses, then resumes
// below half the configured work deviation, matching the simulation sequence.
inline bool ManualSyncPlan(std::vector<double> const& travel,double pause,double trip,std::vector<bool>& paused){
    if(travel.empty()||!std::isfinite(pause)||!std::isfinite(trip)||pause<=0||trip<=pause)return false;
    for(double v:travel)if(!std::isfinite(v))return false;
    auto bounds=std::minmax_element(travel.begin(),travel.end());
    if(*bounds.second-*bounds.first>trip)return false;
    if(paused.size()!=travel.size())paused.assign(travel.size(),false);
    for(std::size_t i=0;i<travel.size();++i){double lead=travel[i]-*bounds.first;
        if(paused[i]){if(lead<=pause*0.5)paused[i]=false;}else if(lead>pause)paused[i]=true;
    }
    return true;
}
inline double AvailableTravelMm(double absoluteMm,double strokeMm,bool lowering){
    if(!std::isfinite(absoluteMm)||!std::isfinite(strokeMm)||strokeMm<=0||absoluteMm<0||absoluteMm>strokeMm)return std::numeric_limits<double>::quiet_NaN();
    return lowering?absoluteMm:strokeMm-absoluteMm;
}
inline double StrokeExtensionRatio(double absoluteMm,double strokeMm){
    return std::isfinite(absoluteMm)&&std::isfinite(strokeMm)&&strokeMm>0?std::clamp(absoluteMm/strokeMm,0.0,1.0):std::numeric_limits<double>::quiet_NaN();
}
inline double MovementCycleRatio(double absoluteMm,double originMm,double targetMm){
    const double distance=targetMm-originMm;
    return std::isfinite(absoluteMm)&&std::isfinite(originMm)&&std::isfinite(targetMm)&&std::abs(distance)>1e-9?
        std::clamp((absoluteMm-originMm)/distance,0.0,1.0):std::numeric_limits<double>::quiet_NaN();
}
// Session-only, worker-owned measured references. A non-finite reading is never zeroed.
inline double PressureZeroValue(double value,bool enabled){return enabled&&std::isfinite(value)?(std::max)(0.0,value):value;}
class LoadReference {
    std::array<double,32> values_{};
public:
    void Clear(){values_={};}
    bool Has(std::size_t i)const{return i<values_.size()&&values_[i]>0;}
    double Value(std::size_t i)const{return Has(i)?values_[i]:0;}
    bool Set(std::size_t i,double value){if(i>=values_.size()||!std::isfinite(value)||value<=0)return false;values_[i]=value;return true;}
    bool Exceeded(std::size_t i,double value,bool valid,double percent)const{
        if(!Has(i))return false;
        if(!valid||!std::isfinite(value)||!std::isfinite(percent)||percent<0)return true;
        return std::abs(value-values_[i])>values_[i]*(percent/100.0+1e-12);
    }
};
}
