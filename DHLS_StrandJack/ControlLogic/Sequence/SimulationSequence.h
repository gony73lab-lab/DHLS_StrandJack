#pragma once
#include "../EquipmentControlService.h"
#include "MockPlant.h"
#include "../PressureAutoAdapter.h"
#include "../../AuditLog.h"
#include <deque>
#include <optional>
namespace DHLS_StrandJack::Control {
// Owns the plant before the engine. Engine is destroyed first, on the service worker.
class SimulationSequence final {
    ServiceOptions options_;
    mutable std::mutex mutex_;
    std::wstring message_=L"Idle";
    std::uint64_t eventId_=0;
    std::deque<DHLS::Update> events_; // bounded history, never XAML or a View pointer
    std::array<std::array<double,8>,4> displayZero_{}, targets_{};
    std::array<std::array<double,8>,4> workTravelZero_{};
    DiagnosticRequest request_;
    LoadReference loadReference_;
    bool loadReferenceTripped_=false;
    std::string loadReferenceMessage_;
    std::array<bool,4> pumpRequested_{};
    std::map<std::string, bool> pumpRequestedByIp_;
    bool stopCycleRequested_=false,fullStopCycle_=false,automaticSequenceLatched_=false;
    PressureAutoController pressure_;
    PressureAutoState pressureStateSeen_=PressureAutoState::Idle;
    bool pressureSession_=false,pressureStopIssued_=false,pressureEmergency_=false,pressureResetRequired_=false;
    std::uint64_t pressureCommand_=0,pressureSample_=0;
    DHLS::MockPlant plant_;
    enum class JobStep { Idle, PrepareReturn, CycleReposition, Lift, LowerTransfer, Lower, TransferToLower, Return };
    JobStep jobStep_=JobStep::Idle;
    DiagnosticRequest jobRequest_;
    std::optional<DiagnosticRequest> pretensionReady_;
    std::array<std::array<double,8>,4> calibrationStrokeZero_{}, calibrationPressureZero_{};
    DHLS::Request jobSegment_;
    bool jobReady_=false, jobActive_=false, jobPaused_=false, jobFinished_=false;
    bool mockUpperOpen_=false, mockLowerOpen_=false, mockUpperSupports_=false;
    double jobTravel_=0, cycleTravel_=0, displayedJobTravel_=0;
    double pendingJobStrokeMm_=0;
    unsigned cycle_=0;
    std::array<bool,32> seeded_{};
    static constexpr double mockSafetyMm=30, mockTransferMm=25, mockToleranceMm=2;
    static constexpr double mockAccelMm=20, mockTransferSpeedPercent=25;
    std::pair<double,double> MockSyncLimits() const {
        if(std::isfinite(options_.mockSyncMm)&&std::isfinite(options_.mockTripMm)&&
           options_.mockSyncMm>0&&options_.mockSyncMm<options_.mockTripMm)
            return {options_.mockSyncMm,options_.mockTripMm};
        return {10,20};
    }

    double ReadyPosition() const {
        return mockSafetyMm+(jobRequest_.lowering?mockTransferMm+jobRequest_.targetMm:0);
    }
    bool SelectedPumpsOn(DiagnosticRequest const& input) const {
        for(std::size_t s=0;s<options_.plcs.size()&&s<input.selected.size();++s)
            for(std::size_t p=0;p<options_.plcs[s].channels.size()&&p<input.selected[s].size();++p){
                const auto& ip=options_.plcs[s].channels[p].pumpIp;
                if(!input.selected[s][p]||ip.empty())continue;
                const auto pump=pumpRequestedByIp_.find(ip);
                if(pump==pumpRequestedByIp_.end()||!pump->second)return false;
            }
        return true;
    }
    bool PretensionTargets(DiagnosticRequest const& input, DHLS::PlantStatus const& sample,
                           DHLS::Request& profile, std::vector<double>& origins,
                           std::vector<double>& limits, std::vector<double>& pressures) const {
        if(input.locked||!SelectedPumpsOn(input)||!sample.communicationOk||!sample.operationAllowed||sample.emergency||
           !std::isfinite(input.targetMm)||input.targetMm<=0||!std::isfinite(input.pretensionLoadPercent)||
           input.pretensionLoadPercent<=0||input.pretensionLoadPercent>100)return false;
        profile.mode=DHLS::Mode::AutoUp;
        for(auto const& jack:sample.jacks){
            const auto s=(jack.id-1)/8,p=(jack.id-1)%8;
            if(!input.selected[s][p])continue;
            auto const& channel=options_.plcs[s].channels[p];
            const double target=(channel.workingLoadTon*input.pretensionLoadPercent/100.0)*98066.5/channel.effectiveAreaMm2/10.0;
            const double limit=jack.positionMm+input.targetMm;
            if(!jack.stopped||!jack.sensorValid||jack.overload||jack.overpressure||!std::isfinite(target)||
               target<=options_.pressureAuto.toleranceMPa||limit>jack.maxPositionMm||
               jack.pressureBar>target*10+options_.pressureAuto.toleranceMPa*10)return false;
            profile.jackIds.push_back(jack.id);origins.push_back(jack.positionMm);
            limits.push_back(limit);pressures.push_back(target);
        }
        return !pressures.empty();
    }
    void PrepareJob(DiagnosticRequest const& input) {
        jobReady_=false;
        if(!SelectedPumpsOn(input)){Note(L"READY rejected: selected jack pump is OFF");return;}
        if(input.locked||!std::isfinite(input.targetMm)||input.targetMm<=0||
           !std::isfinite(input.mockJobGoalMm)||input.mockJobGoalMm<=0||
           !options_.automaticScalesMatch||!ValidDistanceDeceleration(options_.decelerationStartMm,
               options_.minimumSpeedPercent,options_.positionToleranceMm)){
            Note(L"READY rejected: check movement distance, goal, sensor ranges and deceleration");return;
        }
        if(mockUpperOpen_||mockLowerOpen_){Note(L"READY rejected: both mock clamps must be closed");return;}
        auto sample=plant_.Read();
        if(!sample.communicationOk||!sample.operationAllowed||sample.emergency){Note(L"READY rejected: mock plant unavailable");return;}
        std::vector<int> seed;
        DHLS::Request readyMove;
        bool reposition=false;
        bool selected=false;
        const double ready=mockSafetyMm+(input.lowering?mockTransferMm+input.targetMm:0);
        const double highest=mockSafetyMm+input.targetMm+(input.lowering?2*mockTransferMm:0);
        for(std::size_t s=0;s<input.selected.size();++s)for(std::size_t p=0;p<input.selected[s].size();++p){
            if(!input.selected[s][p])continue;
            selected=true;
            const int id=static_cast<int>(s*8+p+1);
            readyMove.jackIds.push_back(id);
            auto jack=std::find_if(sample.jacks.begin(),sample.jacks.end(),[id](auto const& j){return j.id==id;});
            if(jack==sample.jacks.end()||!jack->sensorValid||!jack->stopped||jack->overload||jack->overpressure||
               highest>jack->maxPositionMm-mockSafetyMm){
                Note(L"READY rejected: invalid jack feedback or insufficient stroke");return;
            }
            if(!seeded_[id-1]&&std::abs(jack->positionMm)<0.01)seed.push_back(id);
            else if(std::abs(jack->positionMm-ready)>mockToleranceMm){
                if(!jobFinished_||mockUpperSupports_||!jobRequest_.selected[s][p]){
                    Note(L"READY rejected: move selected mock jacks to the required start position");return;
                }
                reposition=true;
            }
        }
        if(!selected){Note(L"No jack selected");return;}
        for(const int id:seed){
            if(!plant_.SeedPosition(id,ready)){Note(L"READY rejected: mock position initialization failed");return;}
            seeded_[id-1]=true;
        }
        jobRequest_=input;jobFinished_=false;jobTravel_=displayedJobTravel_=0;cycle_=0;workTravelZero_={};pendingJobStrokeMm_=0;
        if(reposition){
            readyMove.positionToleranceMm=options_.positionToleranceMm;
            readyMove.minimumSpeedPercent=options_.minimumSpeedPercent;
            readyMove.maximumPressureBar=options_.maximumStopPressureBar;
            readyMove.minimumLoweringPressureBar=options_.minimumLoweringPressureBar;
            jobSegment_=std::move(readyMove);
            mockUpperOpen_=true;
            if(!StartJobSegment(JobStep::PrepareReturn)){
                mockUpperOpen_=false;Note(L"READY rejected: mock return could not start");return;
            }
            jobActive_=true;stopCycleRequested_=fullStopCycle_=false;
            return;
        }
        jobReady_=true;
        Note(L"Mock READY confirmed | Position mm="+std::to_wstring(ready),true);
    }
    bool StartJobSegment(JobStep step) {
        auto next=jobSegment_;
        next.mode=DHLS::Mode::TargetHeight;
        next.mockStrandSegment=true;
        next.targetHeightsMm.clear();
        next.accelerationMm=mockAccelMm;
        next.decelerationMm=options_.decelerationStartMm;
        next.mockSpeedPercent=100;
        next.mockSyncAbsolute=step==JobStep::Lift||step==JobStep::Lower;
        double target=ReadyPosition();
        const wchar_t* name=jobRequest_.lowering?L"Extending":L"Retracting";
        switch(step){
        case JobStep::PrepareReturn: name=L"Ready return with lower support";break;
        case JobStep::CycleReposition: name=L"Reposition for next stroke";break;
        case JobStep::Lift:
            target=mockSafetyMm+cycleTravel_;name=L"Lifting";break;
        case JobStep::LowerTransfer:
            target=ReadyPosition()+mockTransferMm;name=L"Transfer to upper clamp";break;
        case JobStep::Lower:
            target=ReadyPosition()-cycleTravel_;name=L"Lowering";break;
        case JobStep::TransferToLower:
            target=(jobRequest_.lowering?ReadyPosition()-cycleTravel_:mockSafetyMm+cycleTravel_)-mockTransferMm;
            target=(std::max)(mockSafetyMm,target);name=L"Transfer to lower clamp";break;
        default: break;
        }
        if(step==JobStep::LowerTransfer||step==JobStep::TransferToLower){
            next.accelerationMm=next.decelerationMm=0;
            next.mockSpeedPercent=mockTransferSpeedPercent;
        }
        next.targetHeightsMm.assign(next.jackIds.size(),target);
        next.mockStepName=name;
        if(!engine_.Start(next))return false;
        jobSegment_=std::move(next);jobStep_=step;
        Note(std::wstring{name}+L" | Cycle="+std::to_wstring(cycle_)+L" | Target mm="+std::to_wstring(target)+
             L" | Verified mock load travel mm="+std::to_wstring(jobTravel_));
        return true;
    }
    bool StartNextCycle() {
        if(mockUpperOpen_||mockLowerOpen_)return false;
        if(pendingJobStrokeMm_>0){
            const bool reposition=jobRequest_.lowering&&jobRequest_.targetMm!=pendingJobStrokeMm_;
            jobRequest_.targetMm=std::exchange(pendingJobStrokeMm_,0.0);
            if(reposition){
                if(mockUpperSupports_)return false;
                mockUpperOpen_=true;
                return StartJobSegment(JobStep::CycleReposition);
            }
        }
        ++cycle_;
        Note(L"Both clamps closed | Cycle="+std::to_wstring(cycle_),true);
        cycleTravel_=(std::min)(jobRequest_.targetMm,jobRequest_.mockJobGoalMm-jobTravel_);
        if(!jobRequest_.lowering)mockUpperSupports_=true;
        return StartJobSegment(jobRequest_.lowering?JobStep::LowerTransfer:JobStep::Lift);
    }
    void AdvanceJob() {
        if(!jobActive_||engine_.Busy())return;
        const auto state=engine_.GetState();
        if(state!=DHLS::State::Completed){
            jobActive_=false;
            jobPaused_=state==DHLS::State::Stopped&&!fullStopCycle_&&engine_.StopConfirmed();
            return;
        }
        if(stopCycleRequested_||fullStopCycle_){
            jobActive_=false;jobReady_=false;
            return;
        }
        bool started=false;
        switch(jobStep_){
        case JobStep::PrepareReturn:
            mockUpperOpen_=false;jobReady_=true;jobActive_=false;
            Note(L"Mock READY confirmed | Both clamps closed",true);
            return;
        case JobStep::LowerTransfer:
            // Mock transfer completed and upper clamp is closed before lower opens.
            mockUpperSupports_=true;mockLowerOpen_=true;
            Note(L"Mock upper support confirmed | Lower clamp opened",true);
            started=StartJobSegment(JobStep::Lower);break;
        case JobStep::Lift:
        case JobStep::Lower:
        {
            if(!mockUpperSupports_||mockUpperOpen_){
                jobActive_=jobReady_=false;fullStopCycle_=true;Note(L"Mock job stopped: upper support not verified",true);return;
            }
            auto const sample=plant_.Read();
            auto const positions=engine_.PlannedPositions();
            bool verified=sample.communicationOk&&positions.size()==jobSegment_.jackIds.size();
            std::wstring feedback=L"Mock travel feedback | Cycle="+std::to_wstring(cycle_);
            for(auto const& position:positions){
                auto jack=std::find_if(sample.jacks.begin(),sample.jacks.end(),[&](auto const& j){return j.id==position.id;});
                if(jack==sample.jacks.end()||!jack->sensorValid||!jack->stopped||
                   std::abs(jack->positionMm-position.targetMm)>options_.positionToleranceMm)verified=false;
                else feedback+=L" | Jack "+std::to_wstring(position.id)+L" mm "+std::to_wstring(position.originMm)+
                    L" -> "+std::to_wstring(jack->positionMm)+L" (target "+std::to_wstring(position.targetMm)+L")";
            }
            if(!verified){jobActive_=jobReady_=false;fullStopCycle_=true;Note(L"Mock job stopped: load travel feedback not verified",true);return;}
            if(jobStep_==JobStep::Lift)Note(L"Upper Anchor check (mock) | Upper clamp closed; support and stopped position confirmed",true);
            ::DHLS_StrandJack::Services::AuditLog::Write("SIM_FEEDBACK",AsciiMessage(feedback));
            jobTravel_=(std::min)(jobRequest_.mockJobGoalMm,jobTravel_+cycleTravel_);
            mockLowerOpen_=false;
            if(jobStep_==JobStep::Lift&&jobRequest_.mockJobGoalMm-jobTravel_<=1e-6){
                jobActive_=jobReady_=false;jobFinished_=true;
                Note(L"Mock job completed | Both clamps closed | Upper support retained | Load travel mm="+
                    std::to_wstring(jobTravel_),true);
                return;
            }
            started=StartJobSegment(JobStep::TransferToLower);break;
        }
        case JobStep::TransferToLower:
        {
            const auto sample=plant_.Read();
            const auto plan=engine_.PlannedPositions();
            bool verified=sample.communicationOk&&sample.operationAllowed&&!sample.emergency&&
                !mockLowerOpen_&&engine_.StopConfirmed()&&plan.size()==jobSegment_.jackIds.size();
            for(auto const& position:plan){
                const auto jack=std::find_if(sample.jacks.begin(),sample.jacks.end(),[&](auto const& j){return j.id==position.id;});
                verified=verified&&jack!=sample.jacks.end()&&jack->sensorValid&&jack->stopped&&
                    !jack->overload&&!jack->overpressure&&
                    std::abs(jack->positionMm-position.targetMm)<=options_.positionToleranceMm;
            }
            if(!verified){jobActive_=jobReady_=false;fullStopCycle_=true;Note(L"Mock job stopped: lower support not verified",true);return;}
            mockUpperSupports_=false;mockLowerOpen_=false;
            Note(L"Safe load support verified (mock) | Lower clamp closed; transfer position and stop confirmed",true);
            if(jobRequest_.mockJobGoalMm-jobTravel_<=1e-6){
                mockUpperOpen_=false;jobActive_=jobReady_=false;jobFinished_=true;
                Note(L"Mock job completed | Both clamps closed | Load travel mm="+std::to_wstring(jobTravel_),true);
                return;
            }
            mockUpperOpen_=true;
            Note(L"Upper OPEN / Lower CLOSED | Mock lower support retained",true);
            started=StartJobSegment(JobStep::Return);break;
        }
        case JobStep::Return:
        case JobStep::CycleReposition:
            mockUpperOpen_=false;
            Note(L"Upper clamp re-engaged | Return stopped; both mock clamps closed",true);
            started=StartNextCycle();break;
        default: break;
        }
        if(!started){jobActive_=jobReady_=false;Note(L"Mock job stopped: next segment could not start",true);}
    }
    PressureInput PressureSample(DHLS::PlantStatus const& sample){
        PressureInput in;in.sampleId=sample.sampleId;in.atMs=sample.sampledAtMs;in.communication=sample.communicationOk;in.permitted=sample.operationAllowed;in.emergency=sample.emergency;
        in.outputsOff=pressureStopIssued_;
        for(std::size_t s=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p){
            auto const& c=options_.plcs[s].channels[p];const int id=static_cast<int>(s*8+p+1);
            auto it=std::find_if(sample.jacks.begin(),sample.jacks.end(),[&](auto const& j){return j.id==id;});if(it==sample.jacks.end()){in.communication=false;continue;}
            const double maxP=options_.maximumStopPressureBar>0?std::min(c.maximumPressureBar,options_.maximumStopPressureBar):c.maximumPressureBar;
            in.cylinders.push_back({id,request_.selected[s][p],it->sensorValid&&!it->overload&&!it->overpressure,true,it->stopped,it->pressureBar/10,it->positionMm,c.strokeLimitMm,maxP/10,c.effectiveAreaMm2,c.maximumLoadTon});
            if(request_.lowering&&options_.minimumLoweringPressureBar>0&&it->pressureBar<=options_.minimumLoweringPressureBar)in.cylinders.back().valid=false;
        }return in;
    }
    void TickPressure(){
        const auto now=DHLS::SequenceNowMs();auto sample=plant_.Read();auto result=pressure_.Update(PressureSample(sample),now);
        if(request_.mockPretension&&result.busy&&!result.stopRequested)
            for(auto const& jack:sample.jacks){
                const auto s=(jack.id-1)/8,p=(jack.id-1)%8;
                if(request_.selected[s][p]&&jack.positionMm>=targets_[s][p]){
                    const auto port=std::find_if(result.ports.begin(),result.ports.end(),[&](auto const& item){return item.id==jack.id;});
                    if(port!=result.ports.end()&&!port->reached){
                        pressure_.RequestStop(now,"Pretension travel limit reached before target load");
                        result=pressure_.Result();break;
                    }
                }
            }
        if(result.state==PressureAutoState::Fault||result.state==PressureAutoState::Hold||result.state==PressureAutoState::CommunicationLost||result.state==PressureAutoState::SensorInvalid||result.state==PressureAutoState::PressureLimitExceeded||result.state==PressureAutoState::SyncLimitExceeded)pressureResetRequired_=true;
        if(result.state!=pressureStateSeen_){pressureStateSeen_=result.state;Note(std::wstring(result.reason.begin(),result.reason.end()));::DHLS_StrandJack::Services::AuditLog::Write("SIM_PRESSURE_STATE",std::string(PressureAutoStateName(result.state))+": "+result.reason);}
        if(result.stopRequested){if(!pressureStopIssued_)pressureStopIssued_=plant_.StopAll();return;}
        if(!result.busy)return;
        plant_.PressureRecommendation(result.recommendedPercent);std::vector<DHLS::JackCommand> commands;
        pressureCommand_=std::max(pressureCommand_,sample.acceptedCommandId)+1;
        for(auto const& p:result.ports)commands.push_back({p.id,p.hold?DHLS::Direction::Hold:(request_.lowering?DHLS::Direction::Down:DHLS::Direction::Up),pressureCommand_});
        if(!plant_.Apply(commands)){pressure_.Abort(now,"Simulated pressure output rejected");pressureStopIssued_=plant_.StopAll();}
    }
    DHLS::SequenceEngine engine_; // must remain last: callbacks use preceding members
    static std::vector<DHLS::JackStatus> Jacks(ServiceOptions const& options) {
        std::vector<DHLS::JackStatus> result;
        for(std::size_t s=0;s<options.plcs.size();++s)
            for(std::size_t p=0;p<options.plcs[s].channels.size();++p){
                auto const& c=options.plcs[s].channels[p];
                // No hydraulic/model calibration is inferred. Missing stroke is invalid for START.
                result.push_back({static_cast<int>(s*8+p+1),0,0,0,c.strokeLimitMm,
                    std::isfinite(c.strokeLimitMm)&&c.strokeLimitMm>0,false,false,true});
            }
        return result;
    }
    static std::vector<DHLS::MockLimits> Limits(ServiceOptions const& options) {
        std::vector<DHLS::MockLimits> limits;
        for(auto const& plc:options.plcs)for(auto const& c:plc.channels)
            limits.push_back({c.effectiveAreaMm2,c.maximumPressureBar,c.maximumLoadTon,
                c.workingLoadTon*(1+options.loadTolerancePercent/100.0)});
        return limits;
    }
    static std::string AsciiMessage(std::wstring const& text){std::string result;for(auto c:text)result+=c>=0&&c<128?static_cast<char>(c):'?';return result;}
    void Note(std::wstring text,bool primary=false){
        const bool warning=text.find(L"rejected")!=std::wstring::npos||text.find(L"stopped:")!=std::wstring::npos||text==L"No jack selected";
        ::DHLS_StrandJack::Services::AuditLog::Write(warning?"SIM_WARNING":primary?"SIM_STEP":"SIM_EVENT",AsciiMessage(text));
        std::lock_guard<std::mutex> lock(mutex_);
        message_=std::move(text);
        ++eventId_;
    }
public:
    explicit SimulationSequence(ServiceOptions options)
        :options_(std::move(options)),plant_(Jacks(options_),Limits(options_),options_.strokeFilterSamples),engine_(plant_) {
        engine_.SetCallback([this](DHLS::Update const& update){
            constexpr const char* states[]{"Idle","Checking","Preparing","Running","Stopping","Completed","Stopped","Fault","Emergency"};
            std::string detail=AsciiMessage(update.message)+" | State: "+states[static_cast<int>(update.state)]
                +" | Operation: "+std::to_string(update.operationId)
                +" | Stop confirmed: "+(update.stopConfirmed?"yes":"no");
            if(!update.jackIds.empty()){
                detail+=" | Selected jacks:";
                for(const auto id:update.jackIds)
                    detail+=" SET "+std::to_string((id-1)/8+1)+"/PORT "+std::to_string((id-1)%8+1);
            }
            ::DHLS_StrandJack::Services::AuditLog::Write("SIM_SEQUENCE",std::move(detail));
            if(!update.mockStepName.empty())
                ::DHLS_StrandJack::Services::AuditLog::Write("SIM_STEP",AsciiMessage(update.mockStepName)+
                    " | Motion command accepted by mock | Operation: "+std::to_string(update.operationId));
            std::lock_guard<std::mutex> lock(mutex_);message_=update.message;++eventId_;
            events_.push_back(update);if(events_.size()>128)events_.pop_front();
        });
    }
    void Submit(std::string const& action,DiagnosticRequest const& input) {
        if(pressureSession_){
            if(action=="StopRequested"||action=="FullStopRequested"||action=="Locked"||action=="EmergencyRequested"){
                if(action=="FullStopRequested"||action=="EmergencyRequested")pressureResetRequired_=true;
                if(action=="EmergencyRequested"){pressureEmergency_=true;pressure_.Abort(DHLS::SequenceNowMs(),"Emergency stop");}else pressure_.RequestStop(DHLS::SequenceNowMs());
                pressureStopIssued_=plant_.StopAll();return;
            }
            if(pressure_.Busy()){Note(L"Pressure cycle active; settings/mode change rejected");return;}
            if(action=="ResetSequence"){
                if(pressure_.Result().stopConfirmed&&plant_.ResetMotion()){
                    pressureSession_=pressureStopIssued_=automaticSequenceLatched_=pressureEmergency_=pressureResetRequired_=false;pressure_=PressureAutoController{};
                    loadReference_.Clear();loadReferenceTripped_=false;displayZero_={};targets_={};request_={};
                    pretensionReady_.reset();calibrationStrokeZero_={};calibrationPressureZero_={};
                    Note(L"Reset accepted; new START required");
                }return;
            }
        }
        if(action=="EmergencyRequested"){jobReady_=jobPaused_=false;engine_.EmergencyStop();return;}
        if(action=="FullStopRequested"){
            const auto state=engine_.GetState();
            if(state==DHLS::State::Fault||state==DHLS::State::Emergency)return;
            stopCycleRequested_=fullStopCycle_=true;engine_.Stop(true);
            jobReady_=jobPaused_=false;
            if(engine_.Busy())Note(L"Full stop requested");
            else if(engine_.StopConfirmed())Note(L"Full stop confirmed",true);
            return;
        }
        if(action=="StopRequested"){
            const auto state=engine_.GetState();
            if(state==DHLS::State::Fault||state==DHLS::State::Emergency){engine_.Stop(true);return;}
            if(!stopCycleRequested_&&(engine_.Busy()||jobActive_)){
                stopCycleRequested_=true;engine_.Stop();
            }
            return;
        }
        if(action=="ResetSequence"){
            const auto resetState=engine_.GetState();
            const bool localReset=loadReferenceTripped_&&!engine_.Busy()&&(resetState==DHLS::State::Idle||resetState==DHLS::State::Completed);
            const bool accepted=localReset?plant_.ResetMotion():engine_.Reset(true,[this]{return plant_.ResetMotion();});
            if(accepted){automaticSequenceLatched_=false;loadReference_.Clear();loadReferenceTripped_=false;loadReferenceMessage_.clear();pumpRequested_={};pumpRequestedByIp_.clear();displayZero_={};targets_={};request_={};stopCycleRequested_=fullStopCycle_=false;
                pretensionReady_.reset();calibrationStrokeZero_={};calibrationPressureZero_={};
                jobReady_=jobActive_=jobPaused_=jobFinished_=false;jobStep_=JobStep::Idle;jobTravel_=displayedJobTravel_=0;
                mockUpperOpen_=mockLowerOpen_=mockUpperSupports_=false;seeded_={};}
            Note(accepted?L"Reset accepted; new START required":L"Reset rejected: cause/stop confirmation required");return;
        }
        if(automaticSequenceLatched_&&((action=="Back"&&input.automatic)||action.rfind("Arrangement",0)==0)){Note(L"Sequence RESET required before changing screen or mode");return;}
        if(input.locked&&action=="Back"){Note(L"Unlock before leaving dashboard");return;}
        if(fullStopCycle_&&(action=="StartRequested"||action=="PrepareStrandJob")){
            Note(L"Full stop: sequence RESET required before restart");return;
        }
        if(input.locked&&(action=="ModeAuto"||action=="ModeManual"||action=="ModePressure"||action=="ModeDisplacement")){Note(L"Unlock before changing control mode");return;}
        const bool pumpToggle=action.rfind("PumpIpToggle:",0)==0;
        if(engine_.Busy()||jobActive_||(jobPaused_&&action!="StartRequested"&&!pumpToggle)){
            Note(L"Operation active: settings/selection changes rejected");return;
        }
        if(action=="PreparePretension"){
            pretensionReady_.reset();
            DHLS::Request profile;std::vector<double> origins,limits,pressures;
            if(!input.mockPretension||fullStopCycle_||pressureResetRequired_||
               engine_.GetState()==DHLS::State::Fault||engine_.GetState()==DHLS::State::Emergency||
               !PretensionTargets(input,plant_.Read(),profile,origins,limits,pressures)){
                Note(L"Pretension READY rejected: check selected pumps, travel and working load");return;
            }
            pretensionReady_=input;Note(L"Pretension READY confirmed",true);return;
        }
        if(action=="CalibrationPressureZero"||action=="CalibrationStrokeZero"){
            const auto sample=plant_.Read();
            std::vector<DHLS::JackStatus> selected;
            for(auto const& jack:sample.jacks)if(input.selected[(jack.id-1)/8][(jack.id-1)%8])selected.push_back(jack);
            if(input.locked||fullStopCycle_||pressureResetRequired_||loadReferenceTripped_||
               engine_.GetState()==DHLS::State::Fault||engine_.GetState()==DHLS::State::Emergency||
               !sample.communicationOk||sample.emergency||selected.empty()||
               std::any_of(selected.begin(),selected.end(),[](auto const& jack){
                   return !jack.stopped||!jack.sensorValid||jack.overload||jack.overpressure;
               })){
                Note(L"Calibration rejected: select valid stopped jacks and clear faults");return;
            }
            for(auto const& jack:selected){
                const auto s=(jack.id-1)/8,p=(jack.id-1)%8;
                if(action=="CalibrationPressureZero")calibrationPressureZero_[s][p]=jack.pressureBar;
                else calibrationStrokeZero_[s][p]=jack.positionMm;
            }
            Note(action=="CalibrationPressureZero"?L"Calibration pressure display zero confirmed":L"Calibration stroke display zero confirmed",true);return;
        }
        if(action=="PrepareStrandJob"&&input.mockStrandJob){
            const auto state=engine_.GetState();
            if(jobActive_||jobPaused_||state==DHLS::State::Fault||state==DHLS::State::Emergency){
                Note(L"READY rejected: stop or reset the previous job first");return;
            }
            PrepareJob(input);return;
        }
        if (pumpToggle)
        {
            const auto state=engine_.GetState();
            if (input.locked || pressure_.Busy() || state == DHLS::State::Emergency ||
                state == DHLS::State::Fault || (jobPaused_ && !engine_.StopConfirmed()) ||
                !plant_.Read().communicationOk) return;
            const auto ip = action.substr(sizeof("PumpIpToggle:") - 1);
            if (ip.empty()) return;
            for (const auto& plc : options_.plcs)
                for (const auto& channel : plc.channels)
                    if (channel.pumpIp == ip)
                    {
                        auto& requested = pumpRequestedByIp_[ip];
                        requested = !requested;
                        ::DHLS_StrandJack::Services::AuditLog::Write("SIM_PUMP",std::string(requested?"Pump ON confirmed":"Pump OFF confirmed")+" | IP="+ip);
                        return;
                    }
            return;
        }
        if(action.rfind("PumpToggle:",0)==0){
            if(input.locked||!plant_.Read().communicationOk)return;
            try {auto index=std::stoi(action.substr(11))-1;
                if(index>=0&&index<static_cast<int>(options_.plcs.size()))pumpRequested_[index]=!pumpRequested_[index];
            }catch(...){}return;
        }
        if(action=="HeightZero"){
            auto sample=plant_.Read();
            for(auto const& j:sample.jacks){
                const auto s=(j.id-1)/8,p=(j.id-1)%8;
                if(!input.selected[s][p])continue;
                displayZero_[s][p]=j.positionMm;
                if(jobFinished_&&jobRequest_.selected[s][p])workTravelZero_[s][p]=displayedJobTravel_;
            }
            Note(L"HEIGHT ZERO: display reference only");return;
        }
        if(action=="PressureZero"||action=="LoadSet"){
            if(input.locked||loadReferenceTripped_){Note(L"Setup rejected: unlock or RESET first");return;}
            auto sample=plant_.Read();LoadReference candidate;std::vector<int> ids;bool valid=sample.communicationOk;
            for(auto const& j:sample.jacks){const auto s=(j.id-1)/8,p=(j.id-1)%8;if(!input.selected[s][p])continue;
                ids.push_back(j.id);valid &= j.sensorValid&&std::isfinite(j.pressureBar);
                if(action=="LoadSet")valid &= candidate.Set(j.id-1,j.pressureBar*options_.plcs[s].channels[p].effectiveAreaMm2/98066.5);
            }
            if(!valid||ids.empty()){Note(L"Setup rejected: select fresh valid sensors; LOAD SET requires positive load");return;}
            if(action=="PressureZero"){plant_.PressureZero(ids);Note(L"PRESSURE ZERO: negative P.T clamped to zero; positive values unchanged");}
            else{loadReference_=candidate;Note(L"LOAD SET: measured port references saved");}
            return;
        }
        if(action!="StartRequested"){
            if(action=="TargetChanged"||action=="Locked"||action.rfind("PortSelected:",0)==0||action.rfind("AllSelected:",0)==0)
                jobReady_=false;
            request_=input;
            if(action=="CalibrationZero"||action=="CalibrationSpan")
                Note(L"Calibration/weight setup not implemented by simulation");
            return;
        }
        if(loadReferenceTripped_){Note(L"LOAD TOLERANCE: RESET required");return;}
        if(!SelectedPumpsOn(input)){Note(L"START rejected: selected jack pump is OFF");return;}
        if(options_.loadToleranceEnabled){auto sample=plant_.Read();for(auto const& j:sample.jacks){auto s=(j.id-1)/8,p=(j.id-1)%8;
            if(input.selected[s][p]&&loadReference_.Exceeded(j.id-1,j.pressureBar*options_.plcs[s].channels[p].effectiveAreaMm2/98066.5,sample.communicationOk&&j.sensorValid,options_.loadTolerancePercent)){
                loadReferenceTripped_=true;loadReferenceMessage_="LOAD TOLERANCE: reference changed before START";Note(L"LOAD TOLERANCE: RESET required");return;
            }
        }}
        if(input.automatic&&!options_.automaticScalesMatch){Note(L"Jack measurement ranges differ: automatic START rejected");return;}
        if(input.locked){Note(L"Unlock before START");return;}
        if(input.pressureMode&&input.automatic){
            if(pressureResetRequired_||fullStopCycle_||engine_.GetState()==DHLS::State::Fault||engine_.GetState()==DHLS::State::Emergency){Note(L"Pressure START rejected: RESET required");return;}
            for(std::size_t set=0;set<options_.plcs.size();++set)for(bool selected:input.selected[set])if(selected&&(!std::isfinite((input.lowering?input.loweringHz[set]:input.liftingHz[set]))||(input.lowering?input.loweringHz[set]:input.liftingHz[set])<=0||(input.lowering?input.loweringHz[set]:input.liftingHz[set])>60)){Note(L"Pressure START rejected: rising speed must be 1-60 Hz");return;}
            if(input.lowering&&options_.minimumLoweringPressureBar>0&&input.targetBar<=options_.minimumLoweringPressureBar){Note(L"Pressure START rejected: target must exceed lowering stop pressure");return;}
            request_=input;auto sample=plant_.Read();
            DHLS::Request profile;std::vector<double> origins,limits,pressures;
            if(input.mockPretension){
                const auto ready=std::exchange(pretensionReady_,std::nullopt);
                if(!ready||ready->selected!=input.selected||ready->targetMm!=input.targetMm||
                   ready->pretensionLoadPercent!=input.pretensionLoadPercent||
                   !PretensionTargets(input,sample,profile,origins,limits,pressures)){
                    Note(L"Pretension START rejected: repeat READY for the selected jacks and settings");return;
                }
            }else for(auto const& j:sample.jacks){auto set=(j.id-1)/8,port=(j.id-1)%8;if(input.selected[set][port]&&(input.lowering?j.pressureBar<input.targetBar:j.pressureBar>input.targetBar)){Note(L"Pressure START rejected: target is opposite selected direction");return;}}
            pressure_=PressureAutoController{};
            const auto [syncMm,tripMm]=MockSyncLimits();
            const double target=pressures.empty()?input.targetBar/10:pressures.front();
            if(!pressure_.Configure(options_.pressureAuto,target,syncMm,tripMm,std::move(pressures),input.lowering)||
                !pressure_.StartRequest(PressureSample(sample),DHLS::SequenceNowMs(),true,true)){Note(L"Pressure START rejected: configuration/sensor/rated limits");return;}
            plant_.SetDistanceProfile(profile,origins,limits);
            for(std::size_t i=0;i<profile.jackIds.size();++i){
                const auto id=profile.jackIds[i]-1;targets_[id/8][id%8]=limits[i];
            }
            pressureSession_=true;pressureStopIssued_=false;automaticSequenceLatched_=true;plant_.PressureControl(true);
            for(std::size_t s=0,index=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p,++index)plant_.DirectionSpeeds(index,input.liftingHz[s]*.5,input.loweringHz[s]*.5);
            Note(L"SIMULATION: pressure automatic START; no PLC output");return;
        }
        pressureSession_=false;
        if(input.automatic&&!ValidDistanceDeceleration(options_.decelerationStartMm,options_.minimumSpeedPercent,options_.positionToleranceMm)){
            Note(L"START rejected: invalid distance deceleration settings");return;
        }
        // Session-only speed assumption: 20 Hz => 10 mm/s. Never used by real PLC transport.
        std::size_t index=0;
        for(std::size_t set=0;set<options_.plcs.size();++set)
            for(auto const& channel:options_.plcs[set].channels){
                (void)channel;
                const double up=input.liftingHz[set],down=input.loweringHz[set];
                if(!std::isfinite(up)||!std::isfinite(down)||up<0||down<0){Note(L"Invalid simulation speed");return;}
                // Fixed, repeatable port flow variation exercises synchronization without random noise.
                // All selected ports remain owned by the single cross-PLC sequence.
                const double flowFactor=1.0-0.03*(index%3);
                plant_.DirectionSpeeds(index++,up*0.5*flowFactor,down*0.5*flowFactor);
            }
        plant_.PressureControl(input.pressureMode&&input.automatic);
        DHLS::Request r;
        r.mode=input.pressureMode&&input.automatic?DHLS::Mode::PressureUp:input.automatic
            ?(input.lowering?DHLS::Mode::AutoDown:DHLS::Mode::AutoUp)
            :(input.lowering?DHLS::Mode::ManualDown:DHLS::Mode::ManualUp);
        r.travelMm=input.targetMm;r.targetPressureBar=input.targetBar;r.manualSync=options_.manualSync;
        if(input.automatic){
            r.positionToleranceMm=options_.positionToleranceMm;
            r.decelerationMm=options_.decelerationStartMm;
            r.minimumSpeedPercent=options_.minimumSpeedPercent;
        }
        const auto [syncMm,tripMm]=MockSyncLimits();
        r.syncPauseMm=syncMm;
        r.syncTripMm=tripMm;
        // Mock proposal only: current model has no independent Resume setting.
        r.syncResumeMm=r.syncPauseMm*0.5;
        r.maximumPressureBar=options_.maximumStopPressureBar;
        r.minimumLoweringPressureBar=options_.minimumLoweringPressureBar;
        auto sample=plant_.Read();
        for(std::size_t s=0;s<4;++s)for(std::size_t p=0;p<8;++p)if(input.selected[s][p]){
            const int id=static_cast<int>(s*8+p+1);r.jackIds.push_back(id);
            auto it=std::find_if(sample.jacks.begin(),sample.jacks.end(),[id](auto const& j){return j.id==id;});
            if(it!=sample.jacks.end())targets_[s][p]=input.automatic
                ?it->positionMm+(input.lowering?-input.targetMm:input.targetMm)
                :(input.lowering?it->minPositionMm:it->maxPositionMm);
        }
        request_=input;
        if(input.mockStrandJob){
            const bool sameJob=input.selected==jobRequest_.selected&&input.lowering==jobRequest_.lowering
                &&(jobPaused_||input.targetMm==jobRequest_.targetMm||input.targetMm==pendingJobStrokeMm_)
                &&input.mockJobGoalMm==jobRequest_.mockJobGoalMm;
            if(!sameJob||(!jobReady_&&!jobPaused_)){Note(L"START rejected: run READY for the selected jacks and distances");return;}
            if(jobPaused_){
                const double highest=mockSafetyMm+input.targetMm+(input.lowering?2*mockTransferMm:0);
                if(!std::isfinite(input.targetMm)||input.targetMm<=0||
                   std::any_of(sample.jacks.begin(),sample.jacks.end(),[&](auto const& j){
                       return std::find(jobSegment_.jackIds.begin(),jobSegment_.jackIds.end(),j.id)!=jobSegment_.jackIds.end()&&
                           highest>j.maxPositionMm-mockSafetyMm;
                   })){
                    Note(L"START rejected: updated stroke exceeds available range");return;
                }
                if(!engine_.Start(jobSegment_)){Note(L"START rejected: paused segment could not resume");return;}
                pendingJobStrokeMm_=input.targetMm;
                jobPaused_=false;
            }else{
                jobSegment_=r;
                ::DHLS_StrandJack::Services::AuditLog::Write("SIM_JOB_START","Checking start conditions");
                Note(L"Mock sync settings | Resume mm="+std::to_wstring(r.syncResumeMm)+
                    L" | Deviation mm="+std::to_wstring(r.syncPauseMm)+L" | Stop mm="+std::to_wstring(r.syncTripMm));
                if(!StartNextCycle()){jobReady_=false;Note(L"START rejected: first segment could not start");return;}
            }
            jobReady_=false;jobActive_=true;stopCycleRequested_=fullStopCycle_=false;automaticSequenceLatched_=true;
            return;
        }
        if(!engine_.Start(r))Note(L"START rejected: busy, Fault or Emergency; RESET/new START required");
        else {stopCycleRequested_=fullStopCycle_=false;if(input.automatic)automaticSequenceLatched_=true;}
    }
    bool Reconfigure(ServiceOptions const& next){
        if(engine_.Busy()||pressureSession_)return false;
        std::vector<std::pair<int,double>> factors;std::vector<double> ratios,strokes;
        for(std::size_t s=0;s<options_.plcs.size();++s)for(std::size_t p=0;p<options_.plcs[s].channels.size();++p){
            const auto& old=options_.plcs[s].channels[p];const auto& now=next.plcs[s].channels[p];
            const double ratio=old.strokeMmPerCount>0?now.strokeMmPerCount/old.strokeMmPerCount:1;
            factors.emplace_back(static_cast<int>(s*8+p+1),ratio);ratios.push_back(ratio);strokes.push_back(now.strokeLimitMm);
        }
        if(!engine_.RebasePositions(factors)||!plant_.Reconfigure(Limits(next),strokes,ratios,next.strokeFilterSamples))return false;
        for(auto const& f:factors){auto s=(f.first-1)/8,p=(f.first-1)%8;displayZero_[s][p]*=f.second;targets_[s][p]*=f.second;}
        options_=next;return true;
    }
    void Shutdown() {
        if(pressureSession_){pressure_.RequestStop(DHLS::SequenceNowMs());pressureStopIssued_=plant_.StopAll();const auto end=DHLS::SequenceNowMs()+3500;
            while(pressure_.Busy()&&DHLS::SequenceNowMs()<end){TickPressure();std::this_thread::sleep_for(std::chrono::milliseconds(20));}}

        engine_.Stop();
        const auto end=DHLS::SequenceNowMs()+3500;
        while(engine_.Busy()&&DHLS::SequenceNowMs()<end)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EquipmentSnapshot Latest() {
        if(pressureSession_)TickPressure();
        AdvanceJob();
        auto sample=plant_.Read();
        const auto planned=engine_.PlannedPositions();
        for(auto const& goal:planned)
            if(goal.id>0&&goal.id<=32)targets_[(goal.id-1)/8][(goal.id-1)%8]=goal.targetMm;
        EquipmentSnapshot next;
        next.simulation=true;next.readOnly=true;next.connected=sample.communicationOk;
        if(options_.loadToleranceEnabled&&(engine_.Busy()||pressure_.Busy())&&!loadReferenceTripped_){for(auto const& j:sample.jacks){auto s=(j.id-1)/8,p=(j.id-1)%8;
            if(request_.selected[s][p]&&loadReference_.Exceeded(j.id-1,j.pressureBar*options_.plcs[s].channels[p].effectiveAreaMm2/98066.5,sample.communicationOk&&j.sensorValid,options_.loadTolerancePercent)){
                loadReferenceTripped_=true;loadReferenceMessage_="LOAD TOLERANCE exceeded: SET "+std::to_string(s+1)+" PORT "+std::to_string(p+1);
                fullStopCycle_=true;engine_.Stop(true);if(pressureSession_){pressure_.Abort(DHLS::SequenceNowMs(),"LOAD TOLERANCE exceeded");pressureStopIssued_=plant_.StopAll();}Note(L"LOAD TOLERANCE exceeded; stop requested; RESET required");break;
            }
        }}
        next.loadReferenceTripped=loadReferenceTripped_;next.loadReferenceMessage=loadReferenceMessage_;
        const auto state=engine_.GetState();
        next.sequenceNavigationLocked=automaticSequenceLatched_;next.sequencePhase=static_cast<SequencePhase>(state);next.sequenceBusy=engine_.Busy();
        next.sequenceStopConfirmed=engine_.StopConfirmed();next.monitoring=state==DHLS::State::Running;
        next.mockJobReady=jobReady_;
        next.mockJobFinished=jobFinished_;
        next.mockJobChangesLocked=jobActive_||jobPaused_;
        next.mockJobPaused=jobPaused_;
        next.mockJobValid=jobReady_||jobActive_||jobPaused_||jobFinished_;
        next.mockJobRunning=jobActive_&&jobStep_!=JobStep::PrepareReturn&&!stopCycleRequested_&&!fullStopCycle_&&
            state!=DHLS::State::Fault&&state!=DHLS::State::Emergency;
        displayedJobTravel_=(std::max)(displayedJobTravel_,jobTravel_);
        if((jobActive_||jobPaused_)&&(jobStep_==JobStep::Lift||jobStep_==JobStep::Lower)&&
           (state==DHLS::State::Running||state==DHLS::State::Stopping||state==DHLS::State::Stopped)&&
           !planned.empty()&&planned.size()==jobSegment_.targetHeightsMm.size()){
            double groupTravel=cycleTravel_;
            bool valid=sample.communicationOk;
            for(std::size_t i=0;i<planned.size();++i){
                auto const& goal=planned[i];
                const auto jack=std::find_if(sample.jacks.begin(),sample.jacks.end(),[&](auto const& j){return j.id==goal.id;});
                if(jack==sample.jacks.end()||!jack->sensorValid||!std::isfinite(jack->positionMm)||
                   goal.targetMm!=jobSegment_.targetHeightsMm[i]){
                    valid=false;break;
                }
                const double travel=jobStep_==JobStep::Lift?jack->positionMm-goal.originMm:
                    goal.originMm-jack->positionMm-mockTransferMm;
                groupTravel=(std::min)(groupTravel,std::clamp(travel,0.0,cycleTravel_));
            }
            if(valid)displayedJobTravel_=(std::max)(displayedJobTravel_,jobTravel_+groupTravel);
        }
        next.mockJobTravelMm=(std::min)(displayedJobTravel_,jobRequest_.mockJobGoalMm);
        next.mockJobGoalMm=jobRequest_.mockJobGoalMm;
        next.mockJobStrokeBaseMm=mockSafetyMm+(jobRequest_.lowering?mockTransferMm:0);
        next.mockJobStrokeSpanMm=jobRequest_.targetMm;
        next.automaticResetRequired=state==DHLS::State::Fault||state==DHLS::State::Emergency||fullStopCycle_;
        if(jobActive_&&state==DHLS::State::Completed){next.sequencePhase=SequencePhase::Running;next.sequenceBusy=true;}
        if(jobStep_==JobStep::PrepareReturn&&state==DHLS::State::Completed&&jobReady_)
            next.sequencePhase=SequencePhase::Idle;
        if(jobStep_==JobStep::PrepareReturn)next.monitoring=false;
        if(fullStopCycle_&&state==DHLS::State::Stopped&&!next.sequenceBusy&&next.sequenceStopConfirmed){
            std::lock_guard<std::mutex> lock(mutex_);
            if(message_!=L"Full stop confirmed"){
                message_=L"Full stop confirmed";++eventId_;
                ::DHLS_StrandJack::Services::AuditLog::Write("SIM_STEP","Full stop confirmed");
            }
        }
        next.pcEmergencyRequested=state==DHLS::State::Emergency;
        next.emergencyActive=sample.emergency||next.pcEmergencyRequested;
        next.emergencyStateVerified=true;
        {std::lock_guard<std::mutex> lock(mutex_);next.sequenceMessage=message_;next.sequenceEventId=eventId_;
            for(auto const& e:events_)next.sequenceHistory.push_back({e.atMs,e.operationId,
                static_cast<SequencePhase>(e.state),static_cast<int>(e.mode),e.message,e.jackIds,e.stopConfirmed});}
        next.message="SIMULATION SEQUENCE";
        next.plcs.resize(options_.plcs.size());
        for(std::size_t s=0;s<next.plcs.size();++s){auto& plc=next.plcs[s];
            plc.connected=plc.readOk=plc.usable=sample.communicationOk;plc.sampledAtMs=sample.sampledAtMs;
            plc.pumpOn=pumpRequested_[s]&&!next.emergencyActive;
            plc.error=state==DHLS::State::Fault;plc.emergencyBit=next.emergencyActive;
            plc.ports.resize(options_.plcs[s].channels.size());
            for(std::size_t p=0;p<plc.ports.size();++p){auto& port=plc.ports[p];
                const int id=static_cast<int>(s*8+p+1);
                auto const& j=*std::find_if(sample.jacks.begin(),sample.jacks.end(),[id](auto const& a){return a.id==id;});
                port.absoluteMm=j.positionMm;port.relativeMm=j.positionMm-displayZero_[s][p];
                port.pressureBar=j.pressureBar;port.loadTon=j.pressureBar*options_.plcs[s].channels[p].effectiveAreaMm2/98066.5;
                port.pressureValid=port.strokeValid=j.sensorValid;port.loadValid=j.sensorValid&&options_.plcs[s].channels[p].effectiveAreaMm2>0;
                port.pressureDisplayValid=j.sensorValid;
                port.displayPressureBar=j.pressureBar-calibrationPressureZero_[s][p];
                port.mockStrokeZeroMm=calibrationStrokeZero_[s][p];
                port.referenceLoadTon=loadReference_.Value(s*8+p);
                port.mockClampsValid=next.mockJobValid&&jobRequest_.selected[s][p];
                if(port.mockClampsValid)
                    port.relativeMm=(std::max)(0.0,next.mockJobTravelMm-workTravelZero_[s][p]);
                port.mockUpperOpen=mockUpperOpen_;port.mockLowerOpen=mockLowerOpen_;
                port.sequenceTargetMm=targets_[s][p];
                for(auto const& goal:planned)if(goal.id==id){port.sequenceOriginMm=goal.originMm;port.sequenceTargetMm=goal.targetMm;port.sequencePositionValid=true;break;}
                const auto& pumpIp = options_.plcs[s].channels[p].pumpIp;
                if (!pumpIp.empty() && sample.communicationOk)
                {
                    const auto requested = pumpRequestedByIp_.find(pumpIp);
                    const bool on = !next.emergencyActive &&
                        requested != pumpRequestedByIp_.end() && requested->second;
                    next.pumpByIp[pumpIp] |= on;
                    plc.pumpOn |= on;
                }
                plc.pumpOn|=!j.stopped;
            }
        }
        if(pressureSession_){auto result=pressure_.Result();next.pressureAuto=result;next.pcEmergencyRequested=next.emergencyActive=pressureEmergency_;next.automaticOperation=true;next.automaticOutputsOff=pressureStopIssued_;
            next.sequenceBusy=result.busy;next.sequenceStopConfirmed=result.stopConfirmed;next.monitoring=result.busy&&!result.stopRequested;
            next.sequencePhase=result.busy?(result.stopRequested?SequencePhase::Stopping:SequencePhase::Running):result.state==PressureAutoState::Completed?SequencePhase::Completed:result.stopConfirmed?SequencePhase::Stopped:SequencePhase::Fault;
            next.automaticResetRequired=pressureResetRequired_||result.state==PressureAutoState::Fault||result.state==PressureAutoState::SensorInvalid||result.state==PressureAutoState::CommunicationLost||result.state==PressureAutoState::PressureLimitExceeded||result.state==PressureAutoState::SyncLimitExceeded;
            next.sequenceMessage=std::wstring(result.reason.begin(),result.reason.end());
        }
        if (next.emergencyActive)
            for (auto& pump : next.pumpByIp) pump.second = false;
        next.cog.status=COGCalculator::Status::InvalidInput;
        next.cog.reason="Simulation: stable supported load measurement not commissioned";
        return next;
    }
};
}
