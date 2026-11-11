#pragma once
#include "DistanceDeceleration.h"
#include <vector>
#include <string>
#include <set>
#include <cstdint>
#include <numeric>
namespace DHLS_StrandJack::Control {
// MPa is canonical only inside this controller. Legacy service/UI bar values convert at its boundary.
struct PressureAutoSettings {
    double toleranceMPa=0.05, slowdownMPa=1, minimumPercent=25, gainPercentPerMPa=100;
    double balanceToleranceMPa=0, ratePercentPerSecond=200;
    int settleMs=500, staleMs=1000, timeoutMs=120000, stopTimeoutMs=3000;
    bool Valid()const {return ValidDistanceDeceleration(slowdownMPa,minimumPercent,toleranceMPa)&&
        std::isfinite(gainPercentPerMPa)&&gainPercentPerMPa>0&&std::isfinite(balanceToleranceMPa)&&balanceToleranceMPa>=0&&
        std::isfinite(ratePercentPerSecond)&&ratePercentPerSecond>0&&settleMs>=0&&staleMs>0&&timeoutMs>0&&stopTimeoutMs>0;}
};
enum class PressureAutoState {Idle,Configuring,Ready,StartRequested,Running,Decelerating,TargetPending,StopRequested,StoppedConfirmed,Completed,Hold,Fault,CommunicationLost,SensorInvalid,PressureLimitExceeded,SyncLimitExceeded,OutputInhibited};
inline const char* PressureAutoStateName(PressureAutoState s){switch(s){
#define PA_NAME(x) case PressureAutoState::x:return #x;
PA_NAME(Idle) PA_NAME(Configuring) PA_NAME(Ready) PA_NAME(StartRequested) PA_NAME(Running) PA_NAME(Decelerating) PA_NAME(TargetPending) PA_NAME(StopRequested) PA_NAME(StoppedConfirmed) PA_NAME(Completed) PA_NAME(Hold) PA_NAME(Fault) PA_NAME(CommunicationLost) PA_NAME(SensorInvalid) PA_NAME(PressureLimitExceeded) PA_NAME(SyncLimitExceeded) PA_NAME(OutputInhibited)
#undef PA_NAME
}return "Unknown";}
struct PressureCylinder {
    int id=0;bool selected=false,valid=false,calibrated=false,stopped=false;
    double pressureMPa=0,strokeMm=0,strokeLimitMm=0,pressureLimitMPa=0,areaMm2=0,maximumLoadTon=0;
};
struct PressureInput {std::uint64_t sampleId=0,atMs=0;bool communication=false,permitted=false,emergency=false,outputsOff=false;std::vector<PressureCylinder> cylinders;};
struct PressurePortResult {int id=0;double targetMPa=0,pressureMPa=0,remainingMPa=0,forceKN=0;bool reached=false,hold=false;};
struct PressureAutoResult {
    PressureAutoState state=PressureAutoState::Idle;
    bool valid=false,allReached=false,partial=false,busy=false,stopRequested=false,stopConfirmed=false,commandOutputAllowed=false;
    double averageMPa=0,spreadMPa=0,workErrorMm=0,stopErrorMm=0,recommendedPercent=0;
    std::string reason;std::vector<PressurePortResult> ports;
};
class PressureAutoController {
    PressureAutoSettings settings_;double target_=0,work_=0,trip_=0;
    std::vector<double> targets_,origins_,lastPressure_,lastStroke_;
    std::vector<int> ids_;std::vector<bool> paused_;
    PressureAutoResult result_;
    std::uint64_t started_=0,lastSample_=0,lastUpdate_=0,settleAt_=0,stopAt_=0;
    std::uint64_t responseAt_=0;std::vector<double> responsePressure_;
    int stillCount_=0;bool active_=false,individual_=false,stopping_=false,lowering_=false;
    PressureAutoState terminal_=PressureAutoState::StoppedConfirmed;
    void Fail(PressureAutoState state,const char* reason,std::uint64_t now){
        result_.state=state;result_.reason=reason;result_.valid=false;result_.recommendedPercent=0;
        if(active_){stopping_=true;terminal_=state;if(!stopAt_)stopAt_=now;result_.stopRequested=true;result_.busy=true;}
    }
public:
    const PressureAutoResult& Result()const{return result_;}
    bool Busy()const{return result_.busy;}
    bool Configure(PressureAutoSettings settings,double target,double work,double trip,std::vector<double> perTargets={},bool lowering=false){
        if(Busy())return false;
        lowering_=lowering;settings_=settings;target_=target;work_=work;trip_=trip;targets_=std::move(perTargets);result_={};result_.state=PressureAutoState::Configuring;
        if(!settings.Valid()||!std::isfinite(target)||target<=settings.toleranceMPa||!std::isfinite(work)||!std::isfinite(trip)||work<=0||trip<=work){Fail(PressureAutoState::Fault,"Invalid pressure configuration",0);return false;}
        for(double p:targets_)if(!std::isfinite(p)||p<=settings.toleranceMPa){Fail(PressureAutoState::Fault,"Invalid cylinder target",0);return false;}
        active_=stopping_=false;ids_.clear();origins_.clear();paused_.clear();lastPressure_.clear();lastStroke_.clear();lastSample_=lastUpdate_=settleAt_=stopAt_=0;stillCount_=0;
        result_.state=PressureAutoState::Ready;result_.reason="Ready; explicit START required";return true;
    }
    bool StartRequest(PressureInput const& in,std::uint64_t now,bool outputAllowed,bool individual){
        if(result_.state!=PressureAutoState::Ready)return false;
        Update(in,now);
        if(!result_.valid)return false;
        if(!outputAllowed){result_.state=PressureAutoState::OutputInhibited;result_.reason="Monitor Only: pressure automatic PLC output not commissioned";return false;}
        individual_=individual;active_=true;started_=now;result_.busy=true;result_.stopConfirmed=false;result_.commandOutputAllowed=true;
        responseAt_=now;responsePressure_.clear();for(auto const& p:result_.ports)responsePressure_.push_back(p.pressureMPa);
        result_.recommendedPercent=settings_.minimumPercent;result_.state=PressureAutoState::StartRequested;result_.reason="Explicit pressure START accepted";lastUpdate_=now;return true;
    }
    void RequestStop(std::uint64_t now,const char* reason="Operator stop"){
        if(!active_)return;stopping_=true;terminal_=PressureAutoState::StoppedConfirmed;stopAt_=now;stillCount_=0;lastPressure_.clear();lastStroke_.clear();
        result_.stopRequested=true;result_.state=PressureAutoState::StopRequested;result_.reason=reason;result_.recommendedPercent=0;
    }
    void Abort(std::uint64_t now,const char* reason){Fail(PressureAutoState::Fault,reason,now);}
    PressureAutoResult Update(PressureInput const& in,std::uint64_t now){
        if(result_.state==PressureAutoState::Idle||result_.state==PressureAutoState::Configuring)return result_;
        if(!in.communication||!in.sampleId||!in.atMs||in.atMs>now||now-in.atMs>static_cast<std::uint64_t>(settings_.staleMs)){
            Fail(PressureAutoState::CommunicationLost,"Missing/stale pressure feedback; no automatic restart",now);return result_;}
        if(in.emergency||!in.permitted){Fail(PressureAutoState::Fault,"Plant permission or emergency interlock",now);return result_;}
        std::vector<PressureCylinder> chosen;std::set<int> unique;
        for(auto const& c:in.cylinders)if(c.selected){if(c.id<1||c.id>32||!unique.insert(c.id).second){Fail(PressureAutoState::SensorInvalid,"Invalid/duplicate selected port",now);return result_;}chosen.push_back(c);}
        if(chosen.empty()||(!targets_.empty()&&targets_.size()!=chosen.size())){Fail(PressureAutoState::SensorInvalid,"Select configured pressure ports",now);return result_;}
        std::vector<int> currentIds;for(auto const& c:chosen)currentIds.push_back(c.id);
        if(!ids_.empty()&&currentIds!=ids_){Fail(PressureAutoState::Fault,"Selection changed during pressure cycle",now);return result_;}
        if(ids_.empty()){ids_=currentIds;for(auto const& c:chosen)origins_.push_back(c.strokeMm);paused_.resize(chosen.size());}
        result_.ports.clear();result_.averageMPa=0;double minP=1e300,maxP=-1e300,minS=1e300,maxS=-1e300,sumS=0,minRemaining=1e300;int reached=0;
        for(std::size_t i=0;i<chosen.size();++i){auto const& c=chosen[i];const double target=targets_.empty()?target_:targets_[i];
            if(!c.valid||!c.calibrated||!std::isfinite(c.pressureMPa)||!std::isfinite(c.strokeMm)||!std::isfinite(c.strokeLimitMm)||!std::isfinite(c.pressureLimitMPa)||!std::isfinite(c.areaMm2)||!std::isfinite(c.maximumLoadTon)||c.strokeLimitMm<=0||c.pressureLimitMPa<=0||c.areaMm2<=0||c.maximumLoadTon<=0||c.strokeMm<0||c.strokeMm>c.strokeLimitMm){Fail(PressureAutoState::SensorInvalid,"Invalid sensor/scaling/hydraulic limits",now);return result_;}
            if(target>=c.pressureLimitMPa||(c.maximumLoadTon>0&&target*c.areaMm2/9806.65>=c.maximumLoadTon)){Fail(PressureAutoState::PressureLimitExceeded,"Pressure target exceeds cylinder/load operating limit",now);return result_;}
            if(c.pressureMPa>=c.pressureLimitMPa||(c.maximumLoadTon>0&&c.pressureMPa*c.areaMm2/9806.65>=c.maximumLoadTon)){Fail(PressureAutoState::PressureLimitExceeded,"Maximum pressure/load exceeded",now);return result_;}
            const double remaining=(target-c.pressureMPa)*(lowering_?-1:1);const bool hit=remaining<=settings_.toleranceMPa;
            if(!hit&&(lowering_?c.strokeMm<=0:c.strokeMm>=c.strokeLimitMm)){Fail(PressureAutoState::SyncLimitExceeded,"Stroke upper limit before pressure target",now);return result_;}
            result_.ports.push_back({c.id,target,c.pressureMPa,remaining,c.pressureMPa*c.areaMm2/1000,hit,hit});reached+=hit;
            result_.averageMPa+=c.pressureMPa;minP=std::min(minP,c.pressureMPa);maxP=std::max(maxP,c.pressureMPa);
            const double travel=(c.strokeMm-origins_[i])*(lowering_?-1:1);minS=std::min(minS,travel);maxS=std::max(maxS,travel);sumS+=travel;
            if(!hit)minRemaining=std::min(minRemaining,remaining);
        }
        result_.averageMPa/=chosen.size();result_.spreadMPa=maxP-minP;result_.workErrorMm=sumS/chosen.size()-minS;result_.stopErrorMm=maxS-minS;
        result_.allReached=reached==static_cast<int>(chosen.size());result_.partial=reached>0&&!result_.allReached;result_.valid=true;
        if(result_.stopErrorMm>trip_){Fail(PressureAutoState::SyncLimitExceeded,"Stroke synchronization trip",now);return result_;}
        if(settings_.balanceToleranceMPa>0&&result_.spreadMPa>settings_.balanceToleranceMPa){Fail(PressureAutoState::Hold,"Pressure imbalance; review load distribution",now);return result_;}
        const bool fresh=in.sampleId>lastSample_;if(fresh)lastSample_=in.sampleId;
        if(stopping_){
            bool stable=fresh&&in.outputsOff&&lastPressure_.size()==chosen.size();
            for(std::size_t i=0;i<chosen.size();++i)stable=stable&&chosen[i].stopped&&std::abs(chosen[i].pressureMPa-lastPressure_[i])<=1e-6&&std::abs(chosen[i].strokeMm-lastStroke_[i])<=1e-6;
            if(fresh){stillCount_=stable?stillCount_+1:0;lastPressure_.clear();lastStroke_.clear();for(auto const& c:chosen){lastPressure_.push_back(c.pressureMPa);lastStroke_.push_back(c.strokeMm);}}
            if(stillCount_>=2){result_.stopConfirmed=true;result_.busy=false;result_.commandOutputAllowed=false;active_=stopping_=false;
                result_.state=terminal_==PressureAutoState::Completed&&!result_.allReached?PressureAutoState::Hold:terminal_;
                result_.reason=result_.state==PressureAutoState::Completed?"All selected targets settled; output OFF and standstill confirmed":"Stopped; operator review/new START or RESET required";
            }else if(now-stopAt_>static_cast<std::uint64_t>(settings_.stopTimeoutMs)){result_.state=PressureAutoState::Fault;result_.reason="Stop unconfirmed: PLC OFF and stable P.T/Stroke required";}
            return result_;
        }
        if(!active_){
            if(result_.state==PressureAutoState::Completed&&!result_.allReached){result_.state=PressureAutoState::Hold;result_.reason="Pressure changed after completion; no automatic restart";}
            return result_;
        }
        if(now-started_>static_cast<std::uint64_t>(settings_.timeoutMs)){Fail(PressureAutoState::Fault,"Pressure operation timeout",now);return result_;}
        if(result_.partial&&!individual_){Fail(PressureAutoState::Hold,"PartialTargetReached / CannotCompleteWithSharedDrive",now);return result_;}
        if(result_.allReached){
            if(!settleAt_)settleAt_=now;result_.state=PressureAutoState::TargetPending;result_.recommendedPercent=0;
            if(fresh&&now-settleAt_>=static_cast<std::uint64_t>(settings_.settleMs)){RequestStop(now,"All targets settled; normal STOP requested");terminal_=PressureAutoState::Completed;}
            return result_;
        }
        settleAt_=0;
        bool changed=false;for(std::size_t i=0;i<chosen.size();++i)if(std::abs(chosen[i].pressureMPa-responsePressure_[i])>1e-6)changed=true;
        if(changed){responseAt_=now;for(std::size_t i=0;i<chosen.size();++i)responsePressure_[i]=chosen[i].pressureMPa;}
        else if(now-responseAt_>5000){Fail(PressureAutoState::Fault,"No pressure response timeout",now);return result_;}
        for(std::size_t i=0;i<chosen.size();++i){const double lead=(chosen[i].strokeMm-origins_[i])*(lowering_?-1:1)-minS;if(lead>work_)paused_[i]=true;else if(lead<=work_*0.5)paused_[i]=false;result_.ports[i].hold|=paused_[i];}
        double recommended=minRemaining>settings_.slowdownMPa?100:std::clamp(settings_.gainPercentPerMPa*minRemaining,settings_.minimumPercent,100.0);
        const double dt=lastUpdate_&&now>lastUpdate_?(now-lastUpdate_)/1000.0:0;const double step=settings_.ratePercentPerSecond*dt;
        if(fresh){result_.recommendedPercent+=std::clamp(recommended-result_.recommendedPercent,-step,step);lastUpdate_=now;}
        result_.state=result_.partial?PressureAutoState::TargetPending:minRemaining<=settings_.slowdownMPa?PressureAutoState::Decelerating:PressureAutoState::Running;
        result_.reason=result_.partial?"PartialTargetReached; reached ports held":"Pressure target tracking; COG diagnostic only";
        return result_;
    }
};
}
