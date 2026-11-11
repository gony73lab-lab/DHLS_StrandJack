#pragma once
#include "SequenceEngine.h"
#include "../ControlSettingsRuntime.h"
#include "../DistanceDeceleration.h"
namespace DHLS {
// All plant state, output ownership and test fault injection are protected by m_.
struct MockLimits { double areaMm2=0,maximumPressureBar=0,maximumLoadTon=0,workingLimitTon=0; };
class MockPlant final : public IPlant {
    std::mutex m_;
    PlantStatus p_;
    std::vector<Direction> outputs_;
    std::vector<double> speeds_, loweringSpeeds_, velocity_;
    std::vector<bool> jammed_;
    int strokeFilterSamples_=1;
    std::vector<DHLS_StrandJack::Control::StrokeAverage> strokeFilters_;
    // Explicit demonstration assumptions, not calibrated hydraulic constants.
    double acceleration_=1000.0, braking_=2000.0, pressureRate_=20.0, pressureRecommendation_=1;
    std::vector<MockLimits> limits_;
    struct DistanceTarget { int id; double origin, target; };
    std::vector<DistanceTarget> distanceTargets_;
    double decelerationMm_=0, minimumPercent_=100, toleranceMm_=0;
    double accelerationMm_=0, speedCap_=1;
    std::uint64_t lastTick_=SequenceNowMs();
    std::vector<int> pressureZeroIds_;
    bool stopSucceeds_=true, reject_=false, freezeSamples_=false, pressureControl_=false;
public:
    bool SupportsMockStrandSegments() const noexcept override { return true; }
    explicit MockPlant(std::vector<JackStatus> jacks,std::vector<MockLimits> limits={},int strokeFilterSamples=1)
        :speeds_(jacks.size(),10.0),loweringSpeeds_(jacks.size(),10.0),velocity_(jacks.size(),0.0),jammed_(jacks.size(),false),limits_(std::move(limits)) {
        strokeFilterSamples_=std::clamp(strokeFilterSamples,1,1000);strokeFilters_.resize(jacks.size());
        p_.communicationOk=p_.operationAllowed=true;p_.jacks=std::move(jacks);
        outputs_.resize(p_.jacks.size(),Direction::Hold);
        for(auto& j:p_.jacks)j.stopped=true;
    }
    PlantStatus Read() override {
        std::lock_guard<std::mutex> lock(m_);
        const auto now=SequenceNowMs();const double dt=(std::min)(0.1,(now-lastTick_)/1000.0);lastTick_=now;
        if(!freezeSamples_) {
            double nearest=std::numeric_limits<double>::infinity();
            double progress=std::numeric_limits<double>::infinity();
            for(std::size_t i=0;i<p_.jacks.size()&&!distanceTargets_.empty();++i){
                if(outputs_[i]==Direction::Hold)continue;
                auto const& jack=p_.jacks[i];
                auto target=std::find_if(distanceTargets_.begin(),distanceTargets_.end(),
                    [&](auto const& item){return item.id==jack.id;});
                if(target==distanceTargets_.end())continue;
                const double sign=outputs_[i]==Direction::Up?1.0:-1.0;
                const double remaining=(target->target-jack.positionMm)*sign;
                if(remaining>toleranceMm_){
                    nearest=(std::min)(nearest,remaining);
                    progress=(std::min)(progress,(jack.positionMm-target->origin)*sign);
                }
            }
            const double distanceRatio=decelerationMm_>0&&std::isfinite(nearest)
                ? DHLS_StrandJack::Control::DistanceDecelerationPercent(nearest,decelerationMm_,minimumPercent_,toleranceMm_)/100.0 : 1.0;
            const double minimum=minimumPercent_/100.0;
            const double accelerationRatio=accelerationMm_>0&&std::isfinite(progress)
                ? minimum+(1-minimum)*std::clamp(progress/accelerationMm_,0.0,1.0) : 1.0;
            for(std::size_t i=0;i<p_.jacks.size();++i){auto& j=p_.jacks[i];const auto d=outputs_[i];
                const bool safetyHold=!p_.communicationOk||p_.emergency||!p_.operationAllowed;
                const double desired=safetyHold||jammed_[i]||d==Direction::Hold?0.0:
                    (d==Direction::Up?speeds_[i]:-loweringSpeeds_[i])*
                    (pressureControl_?pressureRecommendation_:(std::min)({distanceRatio,accelerationRatio,speedCap_}));
                const double rate=desired==0.0?braking_:acceleration_;
                velocity_[i]+=(std::clamp)(desired-velocity_[i],-rate*dt,rate*dt);
                const double previous=j.positionMm;
                j.positionMm=(std::clamp)(previous+velocity_[i]*dt,j.minPositionMm,j.maxPositionMm);
                const auto target=std::find_if(distanceTargets_.begin(),distanceTargets_.end(),
                    [&](auto const& item){return item.id==j.id;});
                if(target!=distanceTargets_.end()&&
                   ((previous<=target->target&&j.positionMm>=target->target)||
                    (previous>=target->target&&j.positionMm<=target->target))){
                    j.positionMm=target->target;
                    velocity_[i]=0;
                }
                if((j.positionMm<=j.minPositionMm&&velocity_[i]<0)||
                   (j.positionMm>=j.maxPositionMm&&velocity_[i]>0))velocity_[i]=0;
                j.stopped=std::abs(velocity_[i])<0.000001;
                // Position control carries an assumed load; pressure must not grow with elapsed travel.
                // Pressure control and a blocked UP cylinder still build pressure and exercise protections.
                if(!safetyHold&&d!=Direction::Hold){
                    if((pressureControl_||jammed_[i])&&d==Direction::Up)j.pressureBar+=pressureRate_*dt*pressureRecommendation_*std::clamp(speeds_[i]/10.0,0.0,3.0);
                    else if(pressureControl_&&d==Direction::Down)j.pressureBar=std::max(0.0,j.pressureBar-pressureRate_*dt*pressureRecommendation_*std::clamp(loweringSpeeds_[i]/10.0,0.0,3.0));
                    else {
                        double reference=100.0;
                        if(i<limits_.size()){
                            auto const& limit=limits_[i];
                            if(limit.maximumPressureBar>0)reference=(std::min)(reference,limit.maximumPressureBar);
                            if(limit.areaMm2>0){
                                for(double load:{limit.maximumLoadTon,limit.workingLimitTon})
                                    if(load>0)reference=(std::min)(reference,load*98066.5/limit.areaMm2);
                            }
                        }
                        const double demand=reference*0.5;
                        j.pressureBar+=(std::clamp)(demand-j.pressureBar,-pressureRate_*dt,pressureRate_*dt);
                    }
                }
                if(std::find(pressureZeroIds_.begin(),pressureZeroIds_.end(),j.id)!=pressureZeroIds_.end())
                    j.pressureBar=DHLS_StrandJack::Control::PressureZeroValue(j.pressureBar,true);
                if(i<limits_.size()){auto const& limit=limits_[i];
                    const double load=j.pressureBar*limit.areaMm2/98066.5;
                    j.overload=limit.maximumLoadTon>0&&load>=limit.maximumLoadTon;
                    j.overpressure=limit.maximumPressureBar>0&&j.pressureBar>=limit.maximumPressureBar;
                }
            }
            ++p_.sampleId;p_.sampledAtMs=now;
        }
        auto result=p_;
        for(std::size_t i=0;i<result.jacks.size();++i){
            if(!p_.communicationOk||!result.jacks[i].sensorValid){strokeFilters_[i].Clear();continue;}
            // Frozen samples are not new sensor observations.
            if(!freezeSamples_)result.jacks[i].positionMm=strokeFilters_[i].Push(result.jacks[i].positionMm,strokeFilterSamples_);
        }
        return result;
    }
    void SetDistanceProfile(const Request& request,const std::vector<double>& origins,const std::vector<double>& targets) override {
        std::lock_guard<std::mutex> lock(m_);
        distanceTargets_.clear();
        speedCap_=request.mockSpeedPercent/100.0;
        decelerationMm_=accelerationMm_=0;
        if(request.mode!=Mode::AutoUp&&request.mode!=Mode::AutoDown&&request.mode!=Mode::TargetHeight)return;
        for(std::size_t i=0;i<targets.size();++i)distanceTargets_.push_back({request.jackIds[i],origins[i],targets[i]});
        if(DHLS_StrandJack::Control::ValidDistanceDeceleration(request.decelerationMm,request.minimumSpeedPercent,request.positionToleranceMm)){
            decelerationMm_=request.decelerationMm;
            accelerationMm_=request.accelerationMm;
            minimumPercent_=request.minimumSpeedPercent;
        }
        toleranceMm_=request.positionToleranceMm;
    }
    bool SeedPosition(int id,double mm) {
        std::lock_guard<std::mutex> lock(m_);
        auto it=std::find_if(p_.jacks.begin(),p_.jacks.end(),[&](auto const& jack){return jack.id==id;});
        if(it==p_.jacks.end()||!it->sensorValid||!it->stopped||mm<it->minPositionMm||mm>it->maxPositionMm)return false;
        it->positionMm=mm;
        strokeFilters_[static_cast<std::size_t>(it-p_.jacks.begin())].Clear();
        return true;
    }
    bool Apply(const std::vector<JackCommand>& commands) override {
        std::lock_guard<std::mutex> lock(m_);
        if(reject_||!p_.communicationOk||p_.emergency||!p_.operationAllowed||commands.empty())return false;
        const auto id=commands.front().commandId;
        if(!id||id<=p_.acceptedCommandId)return false;
        std::vector<Direction> next(p_.jacks.size(),Direction::Hold);
        std::vector<int> seen;
        for(auto const& c:commands){
            if(c.commandId!=id||std::find(seen.begin(),seen.end(),c.id)!=seen.end())return false;
            seen.push_back(c.id);
            auto it=std::find_if(p_.jacks.begin(),p_.jacks.end(),[&](auto const& j){return j.id==c.id;});
            if(it==p_.jacks.end())return false;
            next[static_cast<std::size_t>(it-p_.jacks.begin())]=c.direction;
        }
        outputs_=std::move(next);p_.acceptedCommandId=id;return true;
    }
    bool StopAll() override {
        std::lock_guard<std::mutex> lock(m_);if(!stopSucceeds_)return false;
        std::fill(outputs_.begin(),outputs_.end(),Direction::Hold);
        return true; // Accepted output-off; subsequent Read verifies actual standstill.
    }
    bool ResetMotion(){
        std::lock_guard<std::mutex> lock(m_);
        if(!p_.communicationOk||p_.emergency||!p_.operationAllowed||freezeSamples_||
           !std::all_of(outputs_.begin(),outputs_.end(),[](auto d){return d==Direction::Hold;})||
           !std::all_of(velocity_.begin(),velocity_.end(),[](double v){return std::abs(v)<1e-6;}))return false;
        pressureZeroIds_.clear();
        for(auto& j:p_.jacks){j.positionMm=j.minPositionMm;j.pressureBar=0;j.overload=j.overpressure=false;j.stopped=true;}
        for(auto& filter:strokeFilters_)filter.Clear();
        std::fill(jammed_.begin(),jammed_.end(),false);pressureControl_=false;
        return true;
    }
    void PressureZero(std::vector<int> const& ids){std::lock_guard<std::mutex> lock(m_);for(int id:ids)if(std::find(pressureZeroIds_.begin(),pressureZeroIds_.end(),id)==pressureZeroIds_.end())pressureZeroIds_.push_back(id);}
    bool Reconfigure(std::vector<MockLimits> limits,std::vector<double> const& strokes,std::vector<double> const& factors,int filtering){
        std::lock_guard<std::mutex> lock(m_);
        if(strokes.size()!=p_.jacks.size()||factors.size()!=p_.jacks.size()||!std::all_of(outputs_.begin(),outputs_.end(),[](auto v){return v==Direction::Hold;}))return false;
        for(std::size_t i=0;i<p_.jacks.size();++i){p_.jacks[i].positionMm*=factors[i];p_.jacks[i].maxPositionMm=strokes[i];velocity_[i]*=factors[i];strokeFilters_[i].Clear();}
        limits_=std::move(limits);strokeFilterSamples_=std::clamp(filtering,1,1000);return true;
    }
    bool ZeroHeight(){std::lock_guard<std::mutex> lock(m_);return std::all_of(outputs_.begin(),outputs_.end(),[](auto d){return d==Direction::Hold;});}
    void Fault(bool communication,bool emergency,bool sensor=true,bool stop=true,bool reject=false,bool freeze=false) {
        std::lock_guard<std::mutex> lock(m_);p_.communicationOk=communication;p_.emergency=emergency;
        for(auto& j:p_.jacks)j.sensorValid=sensor;
        stopSucceeds_=stop;reject_=reject;freezeSamples_=freeze;
    }
    void Speed(std::size_t i,double mmPerSecond){std::lock_guard<std::mutex> lock(m_);if(!std::isfinite(mmPerSecond)||mmPerSecond<0)throw std::invalid_argument("Invalid simulation speed");speeds_.at(i)=mmPerSecond;loweringSpeeds_.at(i)=mmPerSecond;}
    void PressureRecommendation(double percent){std::lock_guard<std::mutex> lock(m_);pressureRecommendation_=std::clamp(percent/100,0.0,1.0);}
    void PressureControl(bool enabled){std::lock_guard<std::mutex> lock(m_);pressureControl_=enabled;}
    void DirectionSpeeds(std::size_t i,double up,double down){
        if(!std::isfinite(up)||!std::isfinite(down)||up<0||down<0)throw std::invalid_argument("Invalid simulation speed");
        std::lock_guard<std::mutex> lock(m_);speeds_.at(i)=up;loweringSpeeds_.at(i)=down;
    }
    void Jam(std::size_t i,bool jam){std::lock_guard<std::mutex> lock(m_);jammed_.at(i)=jam;}
    void SetJack(std::size_t i,JackStatus j){std::lock_guard<std::mutex> lock(m_);p_.jacks.at(i)=j;}
};
}
