#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace DHLS {
enum class Mode { AutoUp, AutoDown, ManualUp, ManualDown, PressureUp, TargetHeight };
enum class State { Idle, Checking, Preparing, Running, Stopping, Completed, Stopped, Fault, Emergency };
enum class Direction { Hold, Up, Down };
struct JackStatus {
    int id=0;
    double positionMm=0, pressureBar=0, minPositionMm=0, maxPositionMm=0;
    bool sensorValid=false, overload=false, overpressure=false, stopped=false;
};
inline std::uint64_t SequenceNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct PlantStatus {
    bool communicationOk=false, operationAllowed=false, emergency=false, oilLow=false;
    std::vector<JackStatus> jacks;
    std::uint64_t sampleId=0, sampledAtMs=0, acceptedCommandId=0;
};
struct JackCommand { int id=0; Direction direction=Direction::Hold; std::uint64_t commandId=0; };
struct Request {
    Mode mode=Mode::AutoUp;
    std::vector<int> jackIds;
    double travelMm=0, targetPressureBar=0;
    std::vector<double> targetHeightsMm;
    bool manualSync=true;
    double syncPauseMm=1, syncResumeMm=0.5, syncTripMm=3;
    double positionToleranceMm=0.1, pressureToleranceBar=0;
    double decelerationMm=0, minimumSpeedPercent=100;
    double accelerationMm=0, mockSpeedPercent=100;
    bool mockSyncAbsolute=false;
    bool mockStrandSegment=false;
    std::wstring mockStepName;
    int timeoutMs=120000, stopTimeoutMs=3000;
    int maxSampleAgeMs=500, noMotionTimeoutMs=5000;
    double maximumPressureBar=0, minimumLoweringPressureBar=0;
};
struct Update { State state=State::Idle; Mode mode=Mode::AutoUp; std::wstring message;
    std::uint64_t atMs=0,operationId=0;std::vector<int> jackIds;bool stopConfirmed=false;std::wstring mockStepName; };
class IPlant {
public:
    virtual ~IPlant()=default;
    // Bounded calls; fresh coherent snapshot; no UI access.
    virtual PlantStatus Read()=0;
    virtual bool Apply(const std::vector<JackCommand>&)=0;
    virtual bool SupportsMockStrandSegments() const noexcept { return false; }
    virtual void SetDistanceProfile(const Request&, const std::vector<double>&, const std::vector<double>&) {}
    // Stop scope must match all jacks returned by Read().
    virtual bool StopAll()=0;
};
class SequenceEngine {
    using Clock=std::chrono::steady_clock;
public:
    using Callback=std::function<void(const Update&)>;
    explicit SequenceEngine(IPlant& plant):plant_(plant),worker_([this]{ Worker(); }) {}
    ~SequenceEngine() {
        { std::lock_guard<std::mutex> lock(mutex_); exit_=true; stop_=true; }
        cv_.notify_all();
        if(worker_.joinable()) worker_.join();
    }
    SequenceEngine(const SequenceEngine&)=delete;
    SequenceEngine& operator=(const SequenceEngine&)=delete;
    void SetCallback(Callback cb) { std::lock_guard<std::mutex> lock(mutex_); callback_=std::move(cb); }
    State GetState() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
    bool Busy() const { std::lock_guard<std::mutex> lock(mutex_); return active_||pending_||emergencyPending_; }
    bool StopConfirmed() const { std::lock_guard<std::mutex> lock(mutex_); return stopConfirmed_; }
    std::vector<std::pair<int,double>> PlannedTargets() const {
        std::lock_guard<std::mutex> lock(mutex_);std::vector<std::pair<int,double>> result;
        if(request_.mode!=Mode::AutoUp&&request_.mode!=Mode::AutoDown)return result;
        for(std::size_t i=0;i<plannedIds_.size()&&i<plannedTargets_.size();++i)result.emplace_back(plannedIds_[i],plannedTargets_[i]);
        return result;
    }
    struct PlannedPosition {int id;double originMm,targetMm;};
    std::vector<PlannedPosition> PlannedPositions()const{
        std::lock_guard<std::mutex> lock(mutex_);std::vector<PlannedPosition> result;
        if(request_.mode!=Mode::AutoUp&&request_.mode!=Mode::AutoDown&&!request_.mockStrandSegment)return result;
        for(std::size_t i=0;i<plannedIds_.size()&&i<plannedOrigins_.size()&&i<plannedTargets_.size();++i)
            result.push_back({plannedIds_[i],plannedOrigins_[i],plannedTargets_[i]});
        return result;
    }
    bool RebasePositions(std::vector<std::pair<int,double>> const& factors){
        std::lock_guard<std::mutex> lock(mutex_);if(active_||pending_||emergencyPending_)return false;
        for(std::size_t i=0;i<plannedIds_.size();++i)for(auto const& f:factors)if(f.first==plannedIds_[i]){
            if(i<plannedOrigins_.size())plannedOrigins_[i]*=f.second;if(i<plannedTargets_.size())plannedTargets_[i]*=f.second;
        }
        return true;
    }
    bool Start(const Request& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(exit_||active_||pending_||emergencyPending_||emergency_||
           (state_!=State::Idle&&state_!=State::Completed&&state_!=State::Stopped)) return false;
        resumeNext_=state_==State::Stopped&&stopConfirmed_&&resumeReady_&&SameMotion(request_,r);
        if(!resumeNext_)resumeReady_=false;
        request_=r; ++operationId_; pending_=true; stop_=false; fullStop_=false; state_=State::Checking; cv_.notify_all(); return true;
    }
    void Stop(bool complete=false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(complete){fullStop_=true;resumeReady_=false;}
        if(active_||pending_)stop_=true;
        cv_.notify_all();
    }
    void EmergencyStop() {
        std::lock_guard<std::mutex> lock(mutex_);
        resumeReady_=false; emergency_=true; stop_=true; emergencyPending_=true; state_=State::Emergency; cv_.notify_all();
    }
    bool Reset(bool allowFullStop=false,std::function<bool()> resetPlant={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        // A failed stop confirmation cannot be cleared by a UI reset.
        if(active_||pending_||emergencyPending_||!stopConfirmed_||
           (state_!=State::Fault&&state_!=State::Emergency&&!(allowFullStop&&(state_==State::Stopped||state_==State::Completed||state_==State::Idle)))) return false;
        // Bounded plant read: external fault conditions must also be gone.
        try { auto p=plant_.Read();
            if(!Fresh(p,request_.maxSampleAgeMs)||!p.operationAllowed||p.emergency||p.oilLow||p.jacks.empty())return false;
            for(auto const& j:p.jacks)if(!j.stopped||!j.sensorValid||j.overload||j.overpressure||
                !std::isfinite(j.positionMm)||!std::isfinite(j.pressureBar))return false;
        } catch(...) { return false; }
        if(resetPlant){try{if(!resetPlant())return false;}catch(...){return false;}}
        plannedIds_.clear();plannedOrigins_.clear();plannedTargets_.clear();
        resumeReady_=resumeNext_=fullStop_=false; emergency_=false; stop_=false; state_=State::Idle; return true;
    }
private:
    static bool SameMotion(Request const& a,Request const& b) {
        return (b.mode==Mode::AutoUp||b.mode==Mode::AutoDown||b.mode==Mode::TargetHeight)&&
            a.mode==b.mode&&a.jackIds==b.jackIds&&a.targetHeightsMm==b.targetHeightsMm&&
            a.travelMm==b.travelMm&&a.syncPauseMm==b.syncPauseMm&&a.syncResumeMm==b.syncResumeMm&&
            a.syncTripMm==b.syncTripMm&&a.positionToleranceMm==b.positionToleranceMm&&
            a.maximumPressureBar==b.maximumPressureBar&&a.minimumLoweringPressureBar==b.minimumLoweringPressureBar;
    }
    static bool Fresh(const PlantStatus& p,int age) {
        const auto now=SequenceNowMs();
        return p.communicationOk && p.sampleId && p.sampledAtMs && p.sampledAtMs<=now &&
            now-p.sampledAtMs<=static_cast<unsigned>(age>0?age:500);
    }
    static const JackStatus& Find(const PlantStatus& p,int id) {
        auto it=std::find_if(p.jacks.begin(),p.jacks.end(),[id](const JackStatus& j){return j.id==id;});
        if(it==p.jacks.end())throw std::runtime_error("Selected jack missing");
        return *it;
    }
    void Publish(State s,const std::wstring& text) {
        Callback cb; Update u;
        { std::lock_guard<std::mutex> lock(mutex_);
          if(emergency_)s=State::Emergency;
          state_=s; u={s,request_.mode,text,SequenceNowMs(),operationId_,request_.jackIds,stopConfirmed_,
              s==State::Running?request_.mockStepName:std::wstring{}}; cb=callback_; }
        if(cb)try{cb(u);}catch(...){}
    }
    void Validate(const Request& r,const PlantStatus& p) const {
        if(!Fresh(p,r.maxSampleAgeMs)||!p.operationAllowed||p.emergency||p.oilLow)throw std::runtime_error("Plant not ready");
        if(r.mode==Mode::TargetHeight&&(!r.mockStrandSegment||!plant_.SupportsMockStrandSegments()))
            throw std::runtime_error("Angle/target-height mode not commissioned");
        if(r.maxSampleAgeMs<=0||r.noMotionTimeoutMs<=0||!std::isfinite(r.maximumPressureBar)||
           !std::isfinite(r.minimumLoweringPressureBar)||r.maximumPressureBar<0||r.minimumLoweringPressureBar<0)
            throw std::runtime_error("Invalid pressure/freshness limits");
        if(r.jackIds.empty())throw std::runtime_error("No jack selected");
        auto ids=r.jackIds; std::sort(ids.begin(),ids.end());
        if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())throw std::runtime_error("Duplicate jack ID");
        const bool synchronized=r.mode==Mode::AutoUp||r.mode==Mode::AutoDown||r.mockSyncAbsolute||
            ((r.mode==Mode::ManualUp||r.mode==Mode::ManualDown)&&r.manualSync);
        if(synchronized&&(!std::isfinite(r.syncResumeMm)||!std::isfinite(r.syncPauseMm)||!std::isfinite(r.syncTripMm)||
           r.syncResumeMm<0||r.syncResumeMm>=r.syncPauseMm||r.syncPauseMm>=r.syncTripMm))
            throw std::runtime_error("Invalid synchronization limits");
        if(!std::isfinite(r.positionToleranceMm)||r.positionToleranceMm<0||
           !std::isfinite(r.pressureToleranceBar)||r.pressureToleranceBar<0||r.timeoutMs<=0||r.stopTimeoutMs<=0)
            throw std::runtime_error("Invalid limits");
        if((r.mode==Mode::AutoUp||r.mode==Mode::AutoDown)&&(!std::isfinite(r.travelMm)||r.travelMm<=0))
            throw std::runtime_error("Invalid travel");
        if(r.mode==Mode::PressureUp&&(!std::isfinite(r.targetPressureBar)||r.targetPressureBar<=r.pressureToleranceBar))
            throw std::runtime_error("Invalid pressure target");
        if(r.mode==Mode::TargetHeight&&r.targetHeightsMm.size()!=r.jackIds.size())
            throw std::runtime_error("Target count mismatch");
        for(int id:r.jackIds) {
            const auto& j=Find(p,id);
            if(!j.sensorValid||!std::isfinite(j.positionMm)||!std::isfinite(j.pressureBar)||
               !std::isfinite(j.minPositionMm)||!std::isfinite(j.maxPositionMm)||j.minPositionMm>=j.maxPositionMm||
               j.positionMm<j.minPositionMm||j.positionMm>j.maxPositionMm||j.overload||j.overpressure)
                throw std::runtime_error("Invalid jack status");
        }
    }
    bool StopAndConfirm(const Request& r) {
        try {
            std::uint64_t before=0;
            try { before=plant_.Read().sampleId; } catch(...) {}
            if(!plant_.StopAll())return false;
            auto end=Clock::now()+std::chrono::milliseconds(r.stopTimeoutMs>0?r.stopTimeoutMs:3000);
            do {
                auto p=plant_.Read();
                if(Fresh(p,r.maxSampleAgeMs)&&p.sampleId>before&&!p.jacks.empty()&&std::all_of(p.jacks.begin(),p.jacks.end(),
                    [](const JackStatus& j){return j.stopped;}))return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }while(Clock::now()<end);
        }catch(...){}
        return false;
    }
    void Run(const Request& r) {
        State result=State::Fault; std::wstring message=L"Operation fault";
        bool planPrepared=false;
        try {
            Publish(State::Checking,L"Checking"); auto initial=plant_.Read();
            if(initial.emergency){result=State::Emergency;throw std::runtime_error("Plant emergency");}
            { std::lock_guard<std::mutex> lock(mutex_); if(stop_||exit_)throw std::runtime_error("Canceled before preparing"); }
            Validate(r,initial); Publish(State::Preparing,L"Preparing");
            size_t n=r.jackIds.size(); std::vector<double> start(n),target(n),progress(n);
            bool resume=false;
            {std::lock_guard<std::mutex> lock(mutex_);resume=resumeNext_&&plannedOrigins_.size()==n&&plannedTargets_.size()==n;
             if(resume){start=plannedOrigins_;target=plannedTargets_;}}

            std::vector<bool> paused(n,false),reached(n,false);
            for(size_t i=0;i<n;++i) {
                auto& j=Find(initial,r.jackIds[i]);
                if(!resume){
                    start[i]=target[i]=j.positionMm;
                    if(r.mode==Mode::AutoUp)target[i]+=r.travelMm;
                    if(r.mode==Mode::AutoDown)target[i]-=r.travelMm;
                    if(r.mode==Mode::TargetHeight)target[i]=r.targetHeightsMm[i];
                }
                if(!std::isfinite(target[i])||target[i]<j.minPositionMm||target[i]>j.maxPositionMm)
                    throw std::runtime_error("Target outside stroke");
            }
            {std::lock_guard<std::mutex> lock(mutex_);plannedOrigins_=start;plannedTargets_=target;plannedIds_=r.jackIds;}
            planPrepared=true;
            bool manual=r.mode==Mode::ManualUp||r.mode==Mode::ManualDown;
            plant_.SetDistanceProfile(r,start,target);
            bool down=r.mode==Mode::AutoDown||r.mode==Mode::ManualDown;
            bool sync=r.mode==Mode::AutoUp||r.mode==Mode::AutoDown||r.mockSyncAbsolute||(manual&&r.manualSync);
            auto end=Clock::now()+std::chrono::milliseconds(r.timeoutMs);
            std::vector<Clock::time_point> lastChange(n,Clock::now());
            std::vector<double> lastValue(n,std::numeric_limits<double>::quiet_NaN());
            std::uint64_t lastSample=initial.sampleId, lastCommand=0;
            if(!r.mockStrandSegment)Publish(State::Running,L"Running");
            while(true) {
                {std::lock_guard<std::mutex> lock(mutex_);
                 if(stop_||exit_){result=emergency_?State::Emergency:State::Stopped;message=L"Stop requested";break;}}
                if(Clock::now()>=end)throw std::runtime_error("Operation timeout");
                auto p=plant_.Read();
                if(p.emergency){result=State::Emergency;message=L"Plant emergency";break;}
                Validate(r,p);
                if(p.sampleId<=lastSample)throw std::runtime_error("Repeated/stale sample");
                lastSample=p.sampleId;
                if(lastCommand && p.acceptedCommandId!=lastCommand)throw std::runtime_error("Command ACK ID mismatch");
                for(size_t i=0;i<n;++i){
                    auto x=Find(p,r.jackIds[i]).positionMm;
                    const bool movingDown=r.mode==Mode::TargetHeight?target[i]<start[i]:down;
                    progress[i]=movingDown?start[i]-x:x-start[i];
                }
                auto mm=std::minmax_element(progress.begin(),progress.end());
                double minimum=*mm.first;
                if(sync&&*mm.second-minimum>r.syncTripMm)throw std::runtime_error("Synchronization trip");
                std::vector<JackCommand> commands; bool allReached=!manual;
                for(size_t i=0;i<n;++i) {
                    const auto& j=Find(p,r.jackIds[i]); Direction d=down?Direction::Down:Direction::Up;
                    if(r.maximumPressureBar>0 && j.pressureBar>=r.maximumPressureBar)
                        throw std::runtime_error("Maximum pressure trip");
                    if((down || (r.mode==Mode::TargetHeight&&target[i]<start[i])) &&
                       r.minimumLoweringPressureBar>0 && j.pressureBar<=r.minimumLoweringPressureBar)
                        throw std::runtime_error("Minimum lowering pressure trip");
                    if(r.mode==Mode::PressureUp)reached[i]=reached[i]||j.pressureBar>=r.targetPressureBar-r.pressureToleranceBar;
                    else if(r.mode==Mode::TargetHeight){double e=target[i]-j.positionMm;
                        reached[i]=std::abs(e)<=r.positionToleranceMm;d=e>=0?Direction::Up:Direction::Down;}
                    else if(!manual)reached[i]=down?j.positionMm<=target[i]+r.positionToleranceMm:j.positionMm>=target[i]-r.positionToleranceMm;
                    if(sync){double lead=progress[i]-minimum;if(lead>r.syncPauseMm)paused[i]=true;else if(lead<=r.syncResumeMm)paused[i]=false;}
                    if(reached[i]||paused[i])d=Direction::Hold;
                    if((d==Direction::Up&&j.positionMm>=j.maxPositionMm)||(d==Direction::Down&&j.positionMm<=j.minPositionMm))
                        throw std::runtime_error("Stroke limit");
                    const double sensed=r.mode==Mode::PressureUp?j.pressureBar:j.positionMm;
                    if(!std::isfinite(lastValue[i])||std::abs(sensed-lastValue[i])>1e-6||d==Direction::Hold)
                        lastChange[i]=Clock::now();
                    else if(Clock::now()-lastChange[i]>std::chrono::milliseconds(r.noMotionTimeoutMs))
                        throw std::runtime_error("No sensor response timeout");
                    lastValue[i]=sensed;
                    commands.push_back({r.jackIds[i],d});allReached=allReached&&reached[i];
                }
                if(allReached){result=State::Completed;message=L"Target reached";break;}
                // Recheck cancellation immediately before sending. PLC watchdog remains mandatory.
                {std::lock_guard<std::mutex> lock(mutex_);if(stop_||exit_)continue;}
                const bool firstCommand=lastCommand==0;
                lastCommand=++commandSequence_;
                for(auto& command:commands)command.commandId=lastCommand;
                if(!plant_.Apply(commands))throw std::runtime_error("Command rejected");
                if(r.mockStrandSegment&&firstCommand){
                    {std::lock_guard<std::mutex> lock(mutex_);stopConfirmed_=false;}
                    Publish(State::Running,L"Running");
                }
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock,std::chrono::milliseconds(20),[this]{return stop_||exit_;});
            }
        }catch(const std::exception& e){std::string s=e.what();message.assign(s.begin(),s.end());}
         catch(...){message=L"Unhandled operation error";}
        {std::lock_guard<std::mutex> lock(mutex_);
            if(stop_&&!emergency_&&result==State::Fault){result=State::Stopped;message=L"Stop requested";}}
        Publish(State::Stopping,L"Stopping"); bool confirmed=StopAndConfirm(r);
        {std::lock_guard<std::mutex> lock(mutex_);stopConfirmed_=confirmed;
         resumeReady_=result==State::Stopped&&confirmed&&planPrepared&&!emergency_&&!fullStop_;
         if(result==State::Stopped&&confirmed)message=fullStop_?L"Full stop confirmed":L"Paused";}
        if(!confirmed){if(result!=State::Emergency)result=State::Fault;message+=L" / stop not confirmed";}
        Publish(result,message);
    }
    void Worker() {
        while(true) {
            Request r; bool emergencyOnly;
            {std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[this]{return exit_||pending_||emergencyPending_;});
             if(exit_&&!pending_&&!emergencyPending_)break;
             r=request_;emergencyOnly=emergencyPending_&&!pending_;pending_=false;emergencyPending_=false;active_=true;}
            if(emergencyOnly){bool ok=StopAndConfirm(r);{std::lock_guard<std::mutex> lock(mutex_);stopConfirmed_=ok;}
                Publish(State::Emergency,ok?L"Emergency stop confirmed":L"Emergency stop not confirmed");}
            else Run(r);
            {std::lock_guard<std::mutex> lock(mutex_);active_=false;if(exit_)break;}
        }
        try{plant_.StopAll();}catch(...){}
    }
    std::uint64_t commandSequence_=0,operationId_=0;
    IPlant& plant_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    Request request_;
    std::vector<int> plannedIds_;
    std::vector<double> plannedOrigins_,plannedTargets_;
    bool resumeReady_=false,resumeNext_=false,fullStop_=false;
    Callback callback_;
    State state_=State::Idle;
    bool exit_=false,pending_=false,active_=false,stop_=false,emergency_=false,emergencyPending_=false,stopConfirmed_=true;
    std::thread worker_; // last: all preceding members initialized first
};
}
