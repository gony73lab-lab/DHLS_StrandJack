#pragma once
#include "EquipmentControlService.h"
#include <mutex>
#include <sstream>
#include <iomanip>
#include <locale>
#include <stdexcept>
#include <map>
namespace DHLS_StrandJack::Control {
// Application owner. Connect/Disconnect/Shutdown may join a worker: call off the UI thread.
// Current() only takes a short state lock and never performs transport I/O.
class EquipmentSession final {
    mutable std::mutex stateMutex_;
    std::mutex transitionMutex_;
    std::shared_ptr<EquipmentControlService> current_;
    std::wstring key_;
    ServiceOptions options_;
public:
    struct DashboardMemory {
        bool valid=false,automatic=false,locked=false,lifting=true,stopSelected=false,pauseRequested=false,fullStop=false;
        std::uint64_t appliedReset=0;
        std::array<std::array<bool,8>,4> selected{};
        std::array<int,4> up{{20,20,20,20}},down{{20,20,20,20}};
    };
private:
    std::map<std::wstring,DashboardMemory> dashboards_;
    static std::wstring MemoryKey(std::wstring const& model,bool simulation){return (simulation?L"SIM:":L"PLC:")+model;}
public:
    DashboardMemory Dashboard(std::wstring const& model,bool simulation)const{
        std::lock_guard<std::mutex> lock(stateMutex_);auto it=dashboards_.find(MemoryKey(model,simulation));return it==dashboards_.end()?DashboardMemory{}:it->second;
    }
    void Dashboard(std::wstring const& model,bool simulation,DashboardMemory state){std::lock_guard<std::mutex> lock(stateMutex_);dashboards_[MemoryKey(model,simulation)]=std::move(state);}
    std::shared_ptr<EquipmentControlService> CurrentFor(ServiceOptions const& options,std::wstring const& model)const{
        auto key=Key(options,model);std::lock_guard<std::mutex> lock(stateMutex_);return key==key_?current_:nullptr;
    }
private:
    bool suppressed_=false,closing_=false;
    std::uint64_t request_=0;
public:
    static EquipmentSession& Shared(){static EquipmentSession value;return value;}
    ~EquipmentSession(){try{Shutdown();}catch(...){}}
    static std::wstring Key(ServiceOptions const& o,std::wstring const& model){
        std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17);
        s<<o.simulation<<'|'<<o.automaticScalesMatch<<'|'<<o.automaticMotion<<'|'<<o.pollIntervalMs<<'|'<<o.timeoutMs<<'|'<<o.maxSampleAgeMs
         <<'|'<<o.riseWorkMm<<'|'<<o.riseStopMm<<'|'<<o.fallWorkMm<<'|'<<o.fallStopMm<<'|'<<o.pressureSpreadToleranceBar
         <<'|'<<o.manualSync<<'|'<<o.strokeFilterSamples<<'|'<<o.decelerationStartMm<<'|'<<o.minimumSpeedPercent<<'|'<<o.positionToleranceMm
         <<'|'<<o.mockSyncMm<<'|'<<o.mockTripMm
         <<'|'<<o.maximumStopPressureBar<<'|'<<o.minimumLoweringPressureBar<<'|'<<o.loadTolerancePercent<<'|'<<o.loadToleranceEnabled<<'|'<<o.displayLoadZeroBar;
        auto const& pa=o.pressureAuto;s<<";PA:"<<pa.toleranceMPa<<','<<pa.slowdownMPa<<','<<pa.minimumPercent<<','<<pa.gainPercentPerMPa<<','<<pa.balanceToleranceMPa<<','<<pa.ratePercentPerSecond<<','<<pa.settleMs<<','<<pa.staleMs<<','<<pa.timeoutMs<<','<<pa.stopTimeoutMs;
        for(auto const& plc:o.plcs){s<<";PLC:"<<plc.ip<<':'<<plc.channels.size();for(auto const& c:plc.channels)
            s<<';'<<c.sensor<<','<<c.pressureBarPerCount<<','<<c.strokeMmPerCount<<','<<c.plcStrokeDisplayMmPerCount<<','<<c.effectiveAreaMm2<<','<<c.maximumPressureBar
             <<','<<c.xMm<<','<<c.yMm<<','<<c.strokeLimitMm<<','<<c.maximumLoadTon<<','<<c.workingLoadTon<<','<<c.scalingVerified<<','<<c.areaVerified
             <<','<<c.geometryVerified<<','<<c.pressureSigned<<','<<c.strokeSigned;}
        auto text=s.str();return std::to_wstring(model.size())+L":"+model+L"|"+std::wstring(text.begin(),text.end());
    }
    std::uint64_t Request(){std::lock_guard<std::mutex> lock(stateMutex_);return ++request_;}
    std::shared_ptr<EquipmentControlService> Current()const{std::lock_guard<std::mutex> lock(stateMutex_);return current_;}
    bool IsCurrent(std::shared_ptr<EquipmentControlService> const& service)const{return service&&service==Current();}
    std::shared_ptr<EquipmentControlService> Connect(ServiceOptions options,std::wstring const& model,bool reconnect=false,EquipmentControlService::Factory factory={},std::uint64_t ticket=0){
        std::lock_guard<std::mutex> transition(transitionMutex_);auto key=Key(options,model);std::shared_ptr<EquipmentControlService> old;
        {std::lock_guard<std::mutex> lock(stateMutex_);if(closing_||(ticket&&ticket!=request_))return {};
            if(key==key_){if(current_)return current_;if(suppressed_&&!reconnect)return {};}
            old=current_;
        }
        if(old){auto s=old->Latest();if(s.sequenceBusy||s.manualRunning||s.manualStopPending||s.automaticResetRequired||s.loadReferenceTripped||s.faultActive)
            throw std::runtime_error("Stop and RESET before changing the PLC session");}
        if(old&&EquipmentControlService::SameTopology(options_,options)){
            if(!old->Reconfigure(options))throw std::runtime_error("PLC configuration update rejected; stop first");
            std::lock_guard<std::mutex> lock(stateMutex_);key_=key;options_=std::move(options);return old;
        }
        {std::lock_guard<std::mutex> lock(stateMutex_);current_.reset();key_=key;options_=options;suppressed_=false;}
        if(old)old->Stop(); // No replacement socket can start before the previous worker joins.
        {std::lock_guard<std::mutex> lock(stateMutex_);if(closing_||(ticket&&ticket!=request_))return {};}
        auto service=std::make_shared<EquipmentControlService>(std::move(options),std::move(factory));service->Start();
        {std::lock_guard<std::mutex> lock(stateMutex_);current_=service;}return service;
    }
    bool Disconnect(std::shared_ptr<EquipmentControlService> const& expected){
        std::lock_guard<std::mutex> transition(transitionMutex_);
        {std::lock_guard<std::mutex> lock(stateMutex_);if(current_!=expected)return false;}
        if(!expected)return false;auto s=expected->Latest();if(s.sequenceBusy||s.manualRunning||s.manualStopPending||s.faultActive||s.loadReferenceTripped)return false;
        {std::lock_guard<std::mutex> lock(stateMutex_);current_.reset();suppressed_=true;}
        expected->Stop();return true;
    }
    void Shutdown(){std::lock_guard<std::mutex> transition(transitionMutex_);std::shared_ptr<EquipmentControlService> old;
        {std::lock_guard<std::mutex> lock(stateMutex_);closing_=true;old=std::move(current_);}if(old)old->Stop();}
};
}
