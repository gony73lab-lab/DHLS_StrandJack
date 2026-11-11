#pragma once
#include "EquipmentControlService.h"
#include "DistanceDeceleration.h"
#include "PressureAutoAdapter.h"
#include "ControlSettingsRuntime.h"
#include "Sequence/SequenceEngine.h"
#include <array>
#include <cmath>
#include <limits>
#include <string>
namespace DHLS_StrandJack::Control {
// Synchronous state machine owned exclusively by the communication worker.
// It never opens a socket or retains a UI object. All outputs require explicit
// ACK/readback by that same worker before Commit is called.
class AutomaticDistanceSequence {
public:
    struct Output {std::array<std::uint16_t,4> masks{},hz{};};
private:
    PressureAutoController pressure_;
    ServiceOptions options_;
    DiagnosticRequest request_;
    SequencePhase phase_=SequencePhase::Idle,terminal_=SequencePhase::Stopped;
    Output output_,applied_;
    std::array<std::array<double,8>,4> origins_{},targets_{},furthestPressureTravel_{};
    std::array<std::array<bool,8>,4> reached_{};
    std::array<std::array<unsigned,8>,4> lastStroke_{},stopStroke_{},stopPressure_{};
    std::array<std::array<std::uint64_t,8>,4> lastMove_{},stopStamp_{};
    std::vector<bool> paused_;
    std::uint64_t started_=0,offAt_=0,event_=0;
    bool offCommitted_=false,stopConfirmed_=true,resume_=false,fullStop_=false;
    std::string reason_="Automatic sequence idle",terminalReason_;
    DHLS::Request limits_; // Existing runtime timeouts, not new invented hydraulic gains.
    static bool Word(PlcSample const& plc,unsigned address,unsigned& value){
        for(auto const& w:plc.mapWords)if(w.family=='D'&&w.address==address&&w.valid){value=w.value;return true;}return false;
    }
    bool Fresh(PlcSample const& p,std::uint64_t now)const{return p.connected&&p.readOk&&p.usable&&p.sampledAtMs&&p.sampledAtMs<=now&&now-p.sampledAtMs<=static_cast<unsigned>(options_.maxSampleAgeMs);}
    bool Selected(std::size_t s,std::size_t p)const{return request_.selected[s][p];}
    void Change(SequencePhase phase,std::string reason){if(phase_!=phase||reason_!=reason){phase_=phase;reason_=std::move(reason);++event_;}}
    bool Same(DiagnosticRequest const& r)const{return r.selected==request_.selected&&r.lowering==request_.lowering&&r.targetMm==request_.targetMm&&!r.pressureMode;}
    double RawPosition(std::size_t s,std::size_t p,PortSample const& v)const{
        auto const& c=options_.plcs[s].channels[p];return (c.strokeSigned?static_cast<double>(static_cast<std::int16_t>(v.rawStroke)):static_cast<double>(v.rawStroke))*c.strokeMmPerCount;
    }
    bool SensorValid(PortSample const& p)const{return p.strokeValid&&p.pressureValid&&p.loadValid&&!p.strokeFault&&!p.pressureFault&&std::isfinite(p.absoluteMm)&&std::isfinite(p.pressureBar)&&std::isfinite(p.loadTon);}
    bool Guard(EquipmentSnapshot const& sample,std::uint64_t now,std::string& why,bool initial)const{
        if(sample.faultActive||sample.emergencyActive||sample.pcEmergencyRequested){why="PLC fault or emergency";return false;}
        if(sample.plcs.size()!=options_.plcs.size()){why="PLC feedback incomplete";return false;}
        for(std::size_t s=0;s<options_.plcs.size();++s){auto const& plc=sample.plcs[s];unsigned command=0,mask=0;
            if(!Fresh(plc,now)||!plc.pcSelected||!Word(plc,51,command)||!Word(plc,52,mask)){why="PLC feedback stale or PC mode OFF";return false;}
            if((command&1)||plc.emergencyBit||plc.error){why="PLC emergency or error";return false;}
            if((mask&255)!=(initial?0u:static_cast<unsigned>(applied_.masks[s]))){why="D052 feedback differs from confirmed output";return false;}
            for(std::size_t p=0;p<options_.plcs[s].channels.size();++p)if(Selected(s,p)){
                if(p>=plc.ports.size()||!SensorValid(plc.ports[p])){why="P.T / Stroke calibration or feedback invalid";return false;}
                if(!plc.pumpOn||(command&6)!=(request_.lowering?4u:2u)){why="HPU feedback OFF or direction differs";return false;}
                auto const& v=plc.ports[p];auto const& c=options_.plcs[s].channels[p];
                if(!std::isfinite(c.strokeMmPerCount)||c.strokeMmPerCount<=0||!c.areaVerified||!c.scalingVerified||!std::isfinite(c.strokeLimitMm)||c.strokeLimitMm<=0||!std::isfinite(c.maximumPressureBar)||c.maximumPressureBar<=0||!std::isfinite(c.maximumLoadTon)||c.maximumLoadTon<=0||!std::isfinite(c.workingLoadTon)||c.workingLoadTon<=0){why="Jack range / area / rated limits invalid";return false;}
                const double rawMm=RawPosition(s,p,v);
                if(v.absoluteMm<0||v.absoluteMm>c.strokeLimitMm||rawMm<0||rawMm>c.strokeLimitMm){why="Stroke outside physical range";return false;}
                if(request_.pressureMode){
                    const double remainingBar=(request_.targetBar-v.pressureBar)*(request_.lowering?-1:1);
                    if(remainingBar>options_.pressureAuto.toleranceMPa*10&&
                       (request_.lowering?rawMm<=0:rawMm>=c.strokeLimitMm)){
                        why="Raw Stroke limit reached before pressure target";return false;
                    }
                }
                const double ceiling=options_.maximumStopPressureBar>0?std::min(options_.maximumStopPressureBar,c.maximumPressureBar):c.maximumPressureBar;
                if(v.pressureBar>=ceiling||v.loadTon>=c.maximumLoadTon){why="P.T pressure or maximum load limit";return false;}
                if(request_.lowering&&options_.minimumLoweringPressureBar>0&&v.pressureBar<=options_.minimumLoweringPressureBar){why="Lowering minimum pressure limit";return false;}
            }
        }
        return true;
    }
public:
    bool Busy()const{return phase_==SequencePhase::Checking||phase_==SequencePhase::Preparing||phase_==SequencePhase::Running||phase_==SequencePhase::Stopping;}
    bool Running()const{return phase_==SequencePhase::Running;}
    bool Preparing()const{return phase_==SequencePhase::Preparing;}
    bool Stopping()const{return phase_==SequencePhase::Stopping;}
    bool StopConfirmed()const{return stopConfirmed_;}
    bool HasOperation()const{return started_!=0;}
    bool RequiresReset()const{return phase_==SequencePhase::Fault||phase_==SequencePhase::Emergency||(fullStop_&&phase_==SequencePhase::Stopped);}
    bool Lowering()const{return request_.lowering;}
    SequencePhase Phase()const{return phase_;}
    std::string const& Reason()const{return reason_;}
    std::uint64_t Event()const{return event_;}
    Output const& Desired()const{return output_;}
    Output const& Applied()const{return applied_;}
    bool OutputsOff()const{return offCommitted_;}
    double Target(std::size_t s,std::size_t p)const{return targets_[s][p];}
    bool Reconfigure(ServiceOptions const& next){
        if(Busy())return false;
        if(HasOperation())for(std::size_t s=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p){
            const double old=options_.plcs[s].channels[p].strokeMmPerCount,now=next.plcs[s].channels[p].strokeMmPerCount;
            const double ratio=old>0?now/old:1;origins_[s][p]*=ratio;targets_[s][p]*=ratio;
        }
        options_=next;return true;
    }
    double Origin(std::size_t s,std::size_t p)const{return origins_[s][p];}
    bool HasPositionTarget(std::size_t s,std::size_t p)const{return HasOperation()&&!request_.pressureMode&&Selected(s,p);}
    bool PressureMode()const{return request_.pressureMode&&HasOperation();}
    PressureAutoResult PressureResult()const{auto value=pressure_.Result();value.busy=Busy();value.stopRequested=Stopping();value.stopConfirmed=StopConfirmed();value.commandOutputAllowed=Running()||Preparing();if(phase_==SequencePhase::Stopping){value.state=PressureAutoState::StopRequested;value.recommendedPercent=0;value.commandOutputAllowed=false;}else if(phase_==SequencePhase::Completed)value.state=PressureAutoState::Completed;else if(phase_==SequencePhase::Stopped)value.state=PressureAutoState::StoppedConfirmed;else if(phase_==SequencePhase::Fault||phase_==SequencePhase::Emergency)value.state=PressureAutoState::Fault;value.reason=reason_;return value;}
    bool Begin(ServiceOptions const& options,DiagnosticRequest const& r,EquipmentSnapshot const& sample,std::uint64_t now){
        if(Busy()||RequiresReset())return false;
        const bool resume=resume_&&!fullStop_&&stopConfirmed_&&Same(r);
        options_=options;request_=r;
        auto reject=[&](char const* reason){Change(phase_,reason);return false;};
        if(r.pressureMode)return BeginPressure(options,r,sample,now);
        if(r.locked||!r.automatic||!options.automaticScalesMatch||!std::isfinite(r.targetMm)||r.targetMm<=0||!ValidDistanceDeceleration(options.decelerationStartMm,options.minimumSpeedPercent,options.positionToleranceMm))return reject("Automatic START: invalid mode, distance or deceleration settings");
        std::size_t count=0;
        for(std::size_t s=0;s<4;++s)for(std::size_t p=0;p<8;++p)if(r.selected[s][p]){
            if(s>=options.plcs.size()||p>=options.plcs[s].channels.size())return reject("Automatic START: invalid selected port");++count;
        }
        if(!count)return reject("Automatic START: select a port");
        const double work=r.lowering?options.fallWorkMm:options.riseWorkMm,trip=r.lowering?options.fallStopMm:options.riseStopMm;
        if(count>1&&(!std::isfinite(work)||!std::isfinite(trip)||work<=0||trip<=work))return reject("Automatic START: invalid synchronization limits");
        std::string why;if(!Guard(sample,now,why,true))return reject(("Automatic START: "+why).c_str());
        for(std::size_t s=0;s<options.plcs.size();++s){const double hz=r.lowering?r.loweringHz[s]:r.liftingHz[s];bool used=false;
            for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(Selected(s,p)){
                used=true;const double start=RawPosition(s,p,sample.plcs[s].ports[p]);
                const double target=resume?targets_[s][p]:start+(r.lowering?-r.targetMm:r.targetMm);
                if(!std::isfinite(target)||target<0||target>options.plcs[s].channels[p].strokeLimitMm)return reject("Automatic START: target exceeds jack stroke");
            }
            if(used&&(!std::isfinite(hz)||hz<1||hz>60||std::floor(hz)!=hz))return reject("Automatic START: speed must be 1..60 Hz");
        }
        if(!resume){for(auto& row:reached_)row.fill(false);for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(Selected(s,p)){
            origins_[s][p]=RawPosition(s,p,sample.plcs[s].ports[p]);furthestPressureTravel_[s][p]=0;targets_[s][p]=origins_[s][p]+(r.lowering?-r.targetMm:r.targetMm);
        }}
        paused_.clear();output_={};applied_={};offCommitted_=false;stopConfirmed_=false;fullStop_=false;resume_=false;offAt_=0;
        terminal_=SequencePhase::Stopped;terminalReason_.clear();started_=now?now:1;
        for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p){lastMove_[s][p]=now;lastStroke_[s][p]=sample.plcs[s].ports[p].rawStroke;}
        Change(SequencePhase::Preparing,"Automatic preparing: P.T / Stroke verified");Build(sample,now);return true;
    }
    bool BeginPressure(ServiceOptions const& options,DiagnosticRequest const& r,EquipmentSnapshot const& sample,std::uint64_t now){
        options_=options;request_=r;
        auto reject=[&](std::string reason){Change(phase_,std::move(reason));return false;};
        if(r.locked||!r.automatic||!options.automaticScalesMatch||!std::isfinite(r.targetBar)||r.targetBar<=0)return reject("Pressure START: invalid settings");
        bool any=false;for(std::size_t s=0;s<4;++s)for(std::size_t p=0;p<8;++p)if(r.selected[s][p]){
            if(s>=options.plcs.size()||p>=options.plcs[s].channels.size())return reject("Pressure START: invalid port");any=true;
        }
        if(!any)return reject("Pressure START: select a port");
        std::string why;if(!Guard(sample,now,why,true))return reject("Pressure START: "+why);
        if(r.lowering&&options.minimumLoweringPressureBar>0&&r.targetBar<=options.minimumLoweringPressureBar)return reject("Pressure target must exceed lowering stop pressure");
        for(std::size_t s=0;s<options.plcs.size();++s){double hz=r.lowering?r.loweringHz[s]:r.liftingHz[s];
            for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(Selected(s,p)){
                if(!std::isfinite(hz)||hz<1||hz>60||std::floor(hz)!=hz)return reject("Pressure START: speed must be 1..60 Hz");
                const double current=sample.plcs[s].ports[p].pressureBar;
                if(r.lowering?current<r.targetBar:current>r.targetBar)return reject("Pressure START: target is opposite selected direction");
                origins_[s][p]=RawPosition(s,p,sample.plcs[s].ports[p]);furthestPressureTravel_[s][p]=0;
            }
        }
        pressure_=PressureAutoController{};
        if(!pressure_.Configure(options.pressureAuto,r.targetBar/10,r.lowering?options.fallWorkMm:options.riseWorkMm,r.lowering?options.fallStopMm:options.riseStopMm,{},r.lowering)||!pressure_.StartRequest(PressureFeedback(options,r,sample),now,true,true))return reject("Pressure START: "+pressure_.Result().reason);
        output_={};applied_={};offCommitted_=false;stopConfirmed_=false;fullStop_=false;resume_=false;offAt_=0;started_=now?now:1;
        terminal_=SequencePhase::Stopped;terminalReason_.clear();Change(SequencePhase::Preparing,"Pressure preparing: P.T / Stroke and direction verified");BuildPressure(sample,now);return true;
    }
    void BuildPressure(EquipmentSnapshot const& sample,std::uint64_t now){
        std::string why;if(!Guard(sample,now,why,Preparing())){Stop("Pressure stopped: "+why,SequencePhase::Fault);return;}
        for(std::size_t s=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p)if(Selected(s,p)){
            const double travel=(RawPosition(s,p,sample.plcs[s].ports[p])-origins_[s][p])*(request_.lowering?-1:1);
            if(travel < furthestPressureTravel_[s][p]-options_.positionToleranceMm){Stop("Pressure stopped: raw Stroke moved opposite direction",SequencePhase::Fault);return;}
            furthestPressureTravel_[s][p]=std::max(furthestPressureTravel_[s][p],travel);
        }
        auto result=pressure_.Update(PressureFeedback(options_,request_,sample),now);
        if(result.stopRequested||!result.valid){Stop(result.reason,result.allReached&&result.valid&&result.state==PressureAutoState::StopRequested?SequencePhase::Completed:SequencePhase::Fault);return;}
        output_={};
        for(auto const& port:result.ports)if(!port.hold){const auto s=static_cast<std::size_t>((port.id-1)/8),p=static_cast<std::size_t>((port.id-1)%8);output_.masks[s]|=static_cast<std::uint16_t>(1u<<options_.plcs[s].channels[p].sensor);}
        for(std::size_t s=0;s<options_.plcs.size();++s)if(output_.masks[s]){
            double base=request_.lowering?request_.loweringHz[s]:request_.liftingHz[s];
            output_.hz[s]=static_cast<std::uint16_t>(std::clamp(std::max(std::ceil(base*options_.pressureAuto.minimumPercent/100),std::floor(base*result.recommendedPercent/100)),1.0,base));
        }
    }
    void Stop(std::string reason="Automatic stop requested",SequencePhase terminal=SequencePhase::Stopped,bool full=false){
        if(!HasOperation())return;
        auto rank=[](SequencePhase phase){return phase==SequencePhase::Emergency?3:phase==SequencePhase::Fault?2:phase==SequencePhase::Stopped?1:0;};
        fullStop_|=full;
        // Operator STOP must never downgrade an active or already-confirmed fault.
        if((Stopping()||phase_==SequencePhase::Fault||phase_==SequencePhase::Emergency)&&rank(terminal)<rank(terminal_)){resume_=false;return;}
        if(!Stopping()){offCommitted_=false;offAt_=0;for(auto& row:stopStamp_)row.fill(0);}
        terminal_=terminal;terminalReason_=reason;resume_=terminal==SequencePhase::Stopped&&!fullStop_;
        output_={};stopConfirmed_=false;Change(SequencePhase::Stopping,std::move(reason));
    }
    void Build(EquipmentSnapshot const& sample,std::uint64_t now){
        if(!Preparing()&&!Running())return;
        if(request_.pressureMode){BuildPressure(sample,now);return;}
        std::string why;if(!Guard(sample,now,why,Preparing())){Stop("Automatic stopped: "+why,SequencePhase::Fault);return;}
        if(now-started_>static_cast<unsigned>(limits_.timeoutMs)){Stop("Automatic operation timeout",SequencePhase::Fault);return;}
        std::vector<double> travel;std::vector<std::pair<std::size_t,std::size_t>> selected;
        double nearest=std::numeric_limits<double>::infinity();bool allReached=true;
        for(std::size_t s=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p)if(Selected(s,p)){
            auto const& v=sample.plcs[s].ports[p];const double filteredRemaining=(targets_[s][p]-v.absoluteMm)*(request_.lowering?-1:1);
            const double rawRemaining=(targets_[s][p]-RawPosition(s,p,v))*(request_.lowering?-1:1);
            // A moving average must not delay a hard limit or the port OFF request.
            const double remaining=std::min(filteredRemaining,rawRemaining);
            const double rawTravel=(RawPosition(s,p,v)-origins_[s][p])*(request_.lowering?-1:1);
            if(rawTravel < -options_.positionToleranceMm){Stop("Automatic Stroke moved opposite direction",SequencePhase::Fault);return;}
            if(remaining < -options_.positionToleranceMm){Stop("Automatic target overshoot",SequencePhase::Fault);return;}
            if(rawRemaining<=options_.positionToleranceMm)reached_[s][p]=true;
            if(!reached_[s][p]){allReached=false;nearest=std::min(nearest,remaining);}
            travel.push_back((v.absoluteMm-origins_[s][p])*(request_.lowering?-1:1));selected.emplace_back(s,p);
        }
        if(allReached){Stop("Automatic targets reached; waiting for D052 OFF and P.T / Stroke stability",SequencePhase::Completed);return;}
        if(selected.size()>1&&!ManualSyncPlan(travel,request_.lowering?options_.fallWorkMm:options_.riseWorkMm,request_.lowering?options_.fallStopMm:options_.riseStopMm,paused_)){Stop("Automatic synchronization deviation",SequencePhase::Fault);return;}
        if(paused_.size()!=selected.size())paused_.assign(selected.size(),false);
        output_={};
        const double ratio=DistanceDecelerationPercent(nearest,options_.decelerationStartMm,options_.minimumSpeedPercent,options_.positionToleranceMm);
        for(std::size_t i=0;i<selected.size();++i){const auto s=selected[i].first,p=selected[i].second;auto const& port=sample.plcs[s].ports[p];
            const bool moving=!reached_[s][p]&&!paused_[i];
            if(port.rawStroke!=lastStroke_[s][p]||!moving){lastMove_[s][p]=now;lastStroke_[s][p]=port.rawStroke;}
            if(moving&&now-lastMove_[s][p]>static_cast<unsigned>(limits_.noMotionTimeoutMs)){Stop("Automatic Stroke feedback did not move",SequencePhase::Fault);return;}
            if(moving)output_.masks[s]|=static_cast<std::uint16_t>(1u<<options_.plcs[s].channels[p].sensor);
        }
        for(std::size_t s=0;s<options_.plcs.size();++s)if(output_.masks[s]){
            const double base=request_.lowering?request_.loweringHz[s]:request_.liftingHz[s];
            // D050 is whole Hz: round down the speed cap, but never below the configured minimum.
            const auto floorHz=static_cast<unsigned>(std::ceil(base*options_.minimumSpeedPercent/100.0));
            output_.hz[s]=static_cast<std::uint16_t>(std::clamp(std::max<double>(floorHz,std::floor(base*ratio/100.0)),1.0,base));
        }
    }
    void Commit(Output const& applied,std::uint64_t now){
        applied_=applied;
        if(Stopping()){
            bool off=true;for(auto mask:applied.masks)off&=mask==0;
            if(off&&!offCommitted_){offCommitted_=true;offAt_=now;for(auto& row:stopStamp_)row.fill(0);}
        }else if(Preparing())Change(SequencePhase::Running,"Automatic running: D050 / D052 confirmed");
    }
    bool ChangeSpeed(std::size_t set,bool lowering,double hz){
        if(!Running()||set>=options_.plcs.size()||!std::isfinite(hz)||hz<1||hz>60||std::floor(hz)!=hz)return false;
        (lowering?request_.loweringHz:request_.liftingHz)[set]=hz;return true;
    }
    void InvalidateOff(){offCommitted_=false;offAt_=0;for(auto& row:stopStamp_)row.fill(0);}
    void ObserveStop(EquipmentSnapshot const& sample,std::uint64_t now){
        if(!Stopping()||!offCommitted_||sample.plcs.size()!=options_.plcs.size())return;
        bool stable=true;
        for(std::size_t s=0;s<options_.plcs.size();++s){auto const& plc=sample.plcs[s];unsigned mask=0;
            if(!Fresh(plc,now)||!Word(plc,52,mask)||(mask&255)||plc.sampledAtMs<=offAt_){stable=false;for(auto& stamp:stopStamp_[s])stamp=0;continue;}
            for(std::size_t p=0;p<options_.plcs[s].channels.size();++p)if(Selected(s,p)){
                if(p>=plc.ports.size()||!SensorValid(plc.ports[p])){stable=false;stopStamp_[s][p]=0;continue;}
                auto const& v=plc.ports[p];
                // User-defined stop feedback: both raw Stroke and P.T unchanged
                // across two distinct, fresh communication samples AFTER D052 OFF.
                const bool same=stopStamp_[s][p]&&plc.sampledAtMs>stopStamp_[s][p]&&stopStroke_[s][p]==v.rawStroke&&stopPressure_[s][p]==v.rawPressure;
                stable&=same;
                if(terminal_==SequencePhase::Completed)stable&=request_.pressureMode?(request_.lowering?v.pressureBar<=request_.targetBar+options_.pressureAuto.toleranceMPa*10:v.pressureBar>=request_.targetBar-options_.pressureAuto.toleranceMPa*10):std::abs(v.absoluteMm-targets_[s][p])<=options_.positionToleranceMm;
                stopStamp_[s][p]=plc.sampledAtMs;stopStroke_[s][p]=v.rawStroke;stopPressure_[s][p]=v.rawPressure;
            }
        }
        if(stable){stopConfirmed_=true;Change(terminal_,terminal_==SequencePhase::Completed?"Automatic complete: D052 OFF and P.T / Stroke unchanged":terminal_==SequencePhase::Stopped?"Automatic stopped: D052 OFF and P.T / Stroke unchanged":"Automatic fault stopped: "+terminalReason_+"; D052 OFF and P.T / Stroke unchanged");}
        else if(now-offAt_>static_cast<unsigned>(limits_.stopTimeoutMs))Change(SequencePhase::Stopping,"Automatic D052 OFF confirmed; P.T / Stroke stop feedback unconfirmed");
    }
    bool Reset(EquipmentSnapshot const& sample){
        if(Busy()||!stopConfirmed_||sample.emergencyActive||sample.pcEmergencyRequested||sample.faultActive)return false;
        terminal_=SequencePhase::Stopped;terminalReason_.clear();resume_=false;fullStop_=false;output_={};applied_={};started_=0;Change(SequencePhase::Idle,"Reset accepted; new START required");return true;
    }
};
}
