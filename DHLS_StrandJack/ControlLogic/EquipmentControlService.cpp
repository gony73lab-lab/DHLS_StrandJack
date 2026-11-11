#include "EquipmentControlService.h"
#include "ControlSettingsRuntime.h"
#include "PressureAutoAdapter.h"
#include "AutomaticDistanceSequence.h"
#include <optional>
#include "../JackScaleConsistency.h"
#include "../AuditLog.h"
#include <sstream>
#include "DeveloperSimulation.h"
#include "Sequence/SimulationSequence.h"
// This TU deliberately uses no WinUI precompiled header: Winsock precedes windows.h.
#include "../JackTypeLabel.h"
#include "Evo/COGDiagnosticPipeline.h"
#include "Evo/COGIntegration.h"
#include "Evo/LoweringControl.h"
#include "Evo/PressureBalancedLiftUp.h"
#include "Evo/PressureLiftUp.h"
#include "XgtEmergencyTransport.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>

namespace DHLS_StrandJack::Control
{
namespace
{
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
class TcpReader final : public IPlcReadTransport
{
    XgtEmergencyTransport reader_;

  public:
    TcpReader(PlcConfig const &plc, int timeout) : reader_(plc.ip, 2004, timeout)
    {
    }
    bool ReadDeviceWords(char area,std::vector<std::uint16_t> const& addresses,std::vector<std::uint16_t>& out) override { return reader_.ReadDeviceWords(area,addresses,out); }
    bool Connect() override
    {
        return reader_.Connect();
    }
    void Disconnect() override
    {
        reader_.Disconnect();
    }
    bool ReadWords(std::uint16_t first, std::size_t count, std::vector<std::uint16_t> &out) override
    {
        return reader_.ReadWords(first, count, out);
    }
};
// Each poll is collected once. All diagnostics consume this same cached sample.
struct CachedDWords final : DRegionDiagnostics::IReadOnlyDWords
{
    std::vector<std::uint16_t> state, values, pressureFaults, strokeFaults;
    bool ReadWords(std::uint16_t first, std::size_t count, std::vector<std::uint16_t> &out) override
    {
        if (first == 10 && count == 1)
            out = state;
        else if (first == 11 && count == 16)
            out = values;
        else if (first == 56 && count == 1)
            out = pressureFaults;
        else if (first == 59 && count == 1)
            out = strokeFaults;
        else
            return false;
        return out.size() == count;
    }
};
double Decode(unsigned raw, bool signed16)
{
    return signed16 ? static_cast<std::int16_t>(raw) : static_cast<double>(raw);
}
} // namespace
std::uint64_t EquipmentControlService::MonotonicMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
struct EquipmentControlService::Impl
{
    std::atomic<bool> mapMonitoring{false};
    std::atomic<unsigned> mapAreas{0};
    std::atomic<unsigned> mapGeneration{0};
    ServiceOptions options;

    Factory factory;
    const bool liveMonitorOnly;
    std::optional<ServiceOptions> pendingOptions;
    std::uint64_t optionsRequested=0,optionsApplied=0;
    bool optionsAccepted=false;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wake;
    std::atomic<bool> stopping{false};
    bool started = false;
    std::thread worker;
    struct Command
    {
        std::string action;
        DiagnosticRequest request;
    };
    std::deque<Command> commands;
    EquipmentSnapshot published;
    DiagnosticRequest request;
    std::array<std::array<double, 8>, 4> origins{};
    LoadReference loadReference;
    bool loadReferenceTripped=false;
    std::string loadReferenceMessage;
    std::array<std::array<bool,8>,4> pressureZeroEnabled{};
    std::array<std::array<double,8>,4> heightZeroOrigins{}; // Worker-owned local display offsets; never PLC writes.
    bool captureOrigins = false, monitoring = false;
    AutomaticDistanceSequence automatic;
    std::optional<Command> automaticStart;
    std::atomic<bool> automaticPending{false},automaticCancel{false};
    bool automaticEngaged=false;
    PressureAutoController pressureMonitor;
    DiagnosticRequest pressureMonitorRequest;bool pressureMonitorConfigured=false;std::uint64_t pressureMonitorRevision=0;
    std::uint64_t automaticEventSeen=0,automaticOperationId=0;
    std::vector<SequenceRecord> automaticHistory;
    std::atomic<bool> manualHeld{false},manualClearRequested{false};
    bool manualNeedsClear=false,manualActive=false;
    std::array<std::uint16_t,4> manualMasks{}, manualExpectedMasks{};
    std::array<std::array<StrokeAverage,8>,4> strokeFilters{};
    std::array<std::array<double,8>,4> manualOrigins{};
    std::vector<bool> manualPaused;
    bool emergencyInterlock = false, pcEmergencyLatched = false, sendEmergency = false;
    std::vector<EmergencyWriteState> emergencyResults;
    struct FaultWords {std::array<std::uint16_t,4> bits{};std::array<bool,4> known{};};
    std::vector<FaultWords> faultWords;
    bool faultLatched=false;
    std::string controlMessage;
    std::deque<Command> controlCommands;
    std::string message = "Disconnected";
    explicit Impl(ServiceOptions value, Factory transport)
        : options(std::move(value)), factory(std::move(transport)),
          liveMonitorOnly(!options.simulation && !factory)
    {
        if (options.plcs.size() > 4)
            throw std::invalid_argument("at most four PLCs");
        if (options.pollIntervalMs < 20 || options.timeoutMs < 1 || options.timeoutMs > 2000 ||
            options.maxSampleAgeMs < 1)
            throw std::invalid_argument("invalid timing");
        for (auto const &plc : options.plcs)
        {
            if (plc.channels.empty() || plc.channels.size() > 8)
                throw std::invalid_argument("1..8 ports per PLC");
            bool used[8]{};
            for (auto const &channel : plc.channels)
            {
                if (channel.sensor < 0 || channel.sensor >= 8 || used[channel.sensor])
                    throw std::invalid_argument("duplicate or invalid PT/HT mapping");
                used[channel.sensor] = true;
            }
        }
        faultWords.resize(options.plcs.size());
        emergencyResults.resize(options.plcs.size(), EmergencyWriteState::NotRequested);
        published.message = message;
        published.plcs.resize(options.plcs.size());
        for (std::size_t s = 0; s < options.plcs.size(); ++s)
            published.plcs[s].ports.resize(options.plcs[s].channels.size());
    }
    void Drain()
    {
        std::deque<Command> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.swap(commands);
        }
        for (auto const &command : pending)
        {
            if(command.action=="ModePressure"||command.action=="ResetSequence")pressureMonitorConfigured=false;
            const bool selectionChanged = request.selected != command.request.selected;
            request = command.request;
            if(command.action=="StartRequested"&&options.loadToleranceEnabled&&!loadReferenceTripped){
                std::lock_guard<std::mutex> lock(mutex);
                for(std::size_t s=0;s<published.plcs.size();++s)for(std::size_t p=0;p<published.plcs[s].ports.size();++p)if(request.selected[s][p]){
                    auto const& plc=published.plcs[s];auto const& v=plc.ports[p];
                    if(loadReference.Exceeded(s*8+p,v.loadTon,plc.usable&&v.loadValid,options.loadTolerancePercent)){
                        loadReferenceTripped=true;loadReferenceMessage="LOAD TOLERANCE: reference changed before START";
                    }
                }
            }
            if(command.action=="StartRequested"&&loadReferenceTripped){message="LOAD TOLERANCE: RESET required";automaticPending=false;continue;}
            if(options.automaticMotion){
                if(command.action=="StartRequested"&&request.automatic){
                    if(manualActive||manualNeedsClear||automatic.Busy()||automatic.RequiresReset()){
                        automaticPending=false;controlMessage="Automatic START rejected: stop or reset required";
                    }else{automaticStart=command;automaticEngaged=true;}
                    continue;
                }
                if((automatic.HasOperation()||automaticPending.load())&&(command.action=="StopRequested"||command.action=="FullStopRequested"||command.action=="Locked"||command.action=="EmergencyRequested")){
                    automaticCancel=true;automaticStart.reset();automaticPending=false;
                    automatic.Stop(command.action=="EmergencyRequested"?"Automatic emergency stop requested":"Automatic stop requested",command.action=="EmergencyRequested"?SequencePhase::Emergency:SequencePhase::Stopped,command.action=="FullStopRequested"||command.action=="EmergencyRequested");
                    if(command.action!="EmergencyRequested")continue;
                }
                if(command.action=="ResetSequence"){
                    EquipmentSnapshot sample;{std::lock_guard<std::mutex> lock(mutex);sample=published;}
                    bool emergencyCleared=!sample.plcs.empty();const auto now=MonotonicMs();
                    for(auto const& plc:sample.plcs){bool known=false;for(auto const& word:plc.mapWords)if(word.family=='D'&&word.address==51&&word.valid){known=true;emergencyCleared&=(word.value&1)==0;}
                        emergencyCleared&=known&&plc.connected&&plc.readOk&&plc.usable&&!plc.emergencyBit&&plc.sampledAtMs&&now-plc.sampledAtMs<=static_cast<unsigned>(options.maxSampleAgeMs);}
                    if(emergencyCleared){sample.pcEmergencyRequested=false;sample.emergencyActive=false;}
                    if(!manualActive&&!manualNeedsClear&&automatic.Reset(sample)){
                        loadReference.Clear();loadReferenceTripped=false;loadReferenceMessage.clear();pressureZeroEnabled={};heightZeroOrigins={};
                        automaticEngaged=true;automaticCancel=false;controlMessage="Reset accepted; new START required";
                        if(emergencyCleared){pcEmergencyLatched=false;emergencyInterlock=false;sendEmergency=false;std::fill(emergencyResults.begin(),emergencyResults.end(),EmergencyWriteState::NotRequested);std::lock_guard<std::mutex> lock(mutex);published.pcEmergencyRequested=published.emergencyActive=false;}
                    }
                    else controlMessage="Automatic RESET rejected: stop feedback or fault clearance required";
                    continue;
                }
                const bool upSpeed=command.action.rfind("LiftingSpeed:",0)==0,downSpeed=command.action.rfind("LoweringSpeed:",0)==0;
                if(automatic.Busy()&&(upSpeed||downSpeed)){
                    int set=-1;try{set=std::stoi(command.action.substr(command.action.find(':')+1))-1;}catch(...){}
                    if(set>=0&&set<static_cast<int>(options.plcs.size())&&automatic.ChangeSpeed(set,downSpeed,downSpeed?request.loweringHz[set]:request.liftingHz[set]))controlMessage="Automatic speed limit updated; distance cap retained";
                    else controlMessage="Automatic speed update rejected";
                    continue;
                }
            }
            if (selectionChanged && monitoring)
            {
                monitoring = false;
                message = "Diagnostic selection changed; restart diagnostics";
            }
            if (command.action == "UpRequested" || command.action == "DownRequested" || command.action.rfind("PumpToggle:",0)==0 || command.action.rfind("LiftingSpeed:",0)==0 || command.action.rfind("LoweringSpeed:",0)==0) {
                const bool speed=command.action.rfind("LiftingSpeed:",0)==0||command.action.rfind("LoweringSpeed:",0)==0;
                if (request.locked || manualNeedsClear || manualActive || (monitoring&&!speed) || emergencyInterlock || pcEmergencyLatched || faultLatched) {
                    controlMessage="Command rejected: locked, running or emergency";
                } else controlCommands.push_back(command);
                continue;
            }
            if(command.action=="StartRequested"&&!request.automatic){
                if(options.automaticMotion){
                    if(automatic.Busy()||automatic.RequiresReset()){manualHeld=false;controlMessage="Manual START rejected: automatic stop or RESET required";continue;}
                    EquipmentSnapshot sample;{std::lock_guard<std::mutex> lock(mutex);sample=published;}
                    if(automatic.HasOperation())automatic.Reset(sample);automaticEngaged=false;
                }
                if(manualHeld.load()&&!request.locked&&!faultLatched&&!emergencyInterlock&&!pcEmergencyLatched)controlCommands.push_back(command);
                continue;
            }
            if (command.action == "StartRequested")
            {
                if(manualNeedsClear||manualActive){monitoring=false;message="Manual D052 OFF confirmation required";continue;}
                if(request.automatic&&!options.automaticScalesMatch){monitoring=false;message="Jack measurement ranges differ: automatic START rejected";continue;}
                if (emergencyInterlock || pcEmergencyLatched || faultLatched)
                {
                    monitoring = false;
                    message = "PLC emergency input active: diagnostics halted; PLC outputs NOT verified stopped";
                    continue;
                }
                if (monitoring)
                {
                    message = "Read-only diagnostics already running";
                    continue;
                }
                const double target = request.pressureMode ? request.targetBar : request.targetMm;
                bool mappingValid = true;
                for (std::size_t s = 0; s < 4; ++s)
                    for (std::size_t p = 0; p < 8; ++p)
                        if (request.selected[s][p] &&
                            (s >= options.plcs.size() || p >= options.plcs[s].channels.size()))
                            mappingValid = false;
                if (!mappingValid || !std::isfinite(target) || target <= 0)
                {
                    message = "Invalid diagnostic target or selection";
                    continue;
                }
                bool any = false;
                for (auto const &selected : request.selected)
                    for (bool on : selected)
                        any |= on;
                if (request.locked || !any)
                {
                    message = "Diagnostics require an unlocked selected port";
                    continue;
                }
                bool ready = true;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    for (std::size_t s = 0; s < options.plcs.size(); ++s)
                        for (std::size_t p = 0; p < options.plcs[s].channels.size(); ++p)
                            if (request.selected[s][p])
                            {
                                auto const &plc = published.plcs[s];
                                auto const &port = plc.ports[p];
                                ready &= plc.usable &&
                                         MonotonicMs() - plc.sampledAtMs <=
                                             static_cast<unsigned>(options.maxSampleAgeMs) &&
                                         (request.pressureMode ? port.pressureValid : port.strokeValid);
                            }
                }
                if (!ready)
                {
                    monitoring = false;
                    message = "Diagnostics require fresh calibrated measurements";
                    continue;
                }
                monitoring = true;
                captureOrigins = true;
                message = "Read-only diagnostics; PLC writes disabled";
            }
            else if (command.action == "StopRequested" || command.action == "EmergencyRequested")
            {
                monitoring = false;
                if (command.action == "EmergencyRequested")
                {
                    pcEmergencyLatched = true;
                    sendEmergency = true;
                    std::fill(emergencyResults.begin(), emergencyResults.end(), EmergencyWriteState::Pending);
                }
                message = command.action == "EmergencyRequested"
                              ? "PC emergency ON requested"
                              : "Diagnostics stopped; no PLC stop command sent";
            }
            else if(command.action=="HeightZero"){
                if(request.locked||monitoring||faultLatched||emergencyInterlock||pcEmergencyLatched){message="HEIGHT ZERO rejected: stop and unlock first";continue;}
                bool changed=false,unavailable=false;
                std::lock_guard<std::mutex> lock(mutex);
                const auto now=MonotonicMs();
                for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p){
                    if(!request.selected[s][p])continue;
                    auto const& plc=published.plcs[s];auto const& port=plc.ports[p];
                    if(plc.connected&&plc.readOk&&plc.sampledAtMs&&now-plc.sampledAtMs<=static_cast<unsigned>(options.maxSampleAgeMs)&&port.strokeDisplayValid){heightZeroOrigins[s][p]=port.displayAbsoluteMm;changed=true;}
                    else unavailable=true;
                }
                message=unavailable?"HEIGHT ZERO: unavailable selected ports unchanged":changed?"HEIGHT ZERO: display reference updated; absolute position unchanged":"HEIGHT ZERO: select a valid port";
                ::DHLS_StrandJack::Services::AuditLog::Write("HEIGHT_ZERO",message);
            }
            else if(command.action=="PressureZero"||command.action=="LoadSet"){
                if(request.locked||monitoring||manualActive||manualNeedsClear||automatic.Busy()||faultLatched||emergencyInterlock||pcEmergencyLatched||loadReferenceTripped){
                    message="Setup rejected: stop, unlock or RESET first";continue;
                }
                EquipmentSnapshot sample;{std::lock_guard<std::mutex> lock(mutex);sample=published;}
                LoadReference candidate;bool any=false,valid=true;const auto now=MonotonicMs();
                for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(request.selected[s][p]){
                    any=true;auto const& plc=sample.plcs[s];auto const& v=plc.ports[p];auto const& c=options.plcs[s].channels[p];
                    valid &= plc.connected&&plc.readOk&&plc.usable&&plc.sampledAtMs&&now-plc.sampledAtMs<=static_cast<unsigned>(options.maxSampleAgeMs);
                    if(command.action=="LoadSet")valid &= candidate.Set(s*8+p,v.loadTon)&&v.loadValid;
                    else valid &= !v.pressureFault&&std::isfinite(c.pressureBarPerCount)&&c.pressureBarPerCount>0;
                }
                if(any&&valid){
                    if(command.action=="LoadSet"){loadReference=candidate;message="LOAD SET: measured port references saved";}
                    else{for(std::size_t s=0;s<4;++s)for(std::size_t p=0;p<8;++p)if(request.selected[s][p])pressureZeroEnabled[s][p]=true;
                        message="PRESSURE ZERO: negative P.T clamped to zero; positive values unchanged";}
                }else message="Setup rejected: select fresh valid sensors; LOAD SET requires positive load";
                ::DHLS_StrandJack::Services::AuditLog::Write(command.action,message);
            }
            else if (command.action == "CalibrationZero" ||
                     command.action == "CalibrationSpan" || command.action.rfind("HpuVisual:", 0) == 0 ||
                     command.action.rfind("LiftingSpeed:", 0) == 0 ||
                     command.action.rfind("LoweringSpeed:", 0) == 0)
            {
                message = "Read-only: PLC command not transmitted";
            }
            else if (command.action == "ModePressure" || command.action == "ModeDisplacement" ||
                     command.action == "UpRequested" || command.action == "DownRequested" ||
                     command.action == "TargetChanged")
            {
                monitoring = false;
                message = "Diagnostic target updated; no PLC motion command sent";
            }
        }
    }
    bool EmergencyQueued()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::any_of(commands.begin(), commands.end(), [](Command const& command) {
            return command.action == "EmergencyRequested";
        });
    }
    void ApplyPendingOptions(SimulationSequence* simulation=nullptr){
        std::optional<ServiceOptions> next;std::uint64_t revision=0;
        {std::lock_guard<std::mutex> lock(mutex);if(!pendingOptions)return;next=std::move(pendingOptions);pendingOptions.reset();revision=optionsRequested;}
        bool accepted=!manualActive&&!manualNeedsClear&&!automatic.Busy();
        if(accepted){
            accepted=simulation?simulation->Reconfigure(*next):automatic.Reconfigure(*next);
            if(accepted){
                for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p){
                    auto const& old=options.plcs[s].channels[p];auto const& now=next->plcs[s].channels[p];
                    const double ratio=old.strokeMmPerCount>0?now.strokeMmPerCount/old.strokeMmPerCount:1;
                    const double oldDisplay=old.StrokeDisplayScale(), newDisplay=now.StrokeDisplayScale();
                    if(oldDisplay>0)heightZeroOrigins[s][p]*=newDisplay/oldDisplay;
                    origins[s][p]*=ratio;
                    const double oldLoad=old.pressureBarPerCount*old.effectiveAreaMm2,newLoad=now.pressureBarPerCount*now.effectiveAreaMm2;
                    if(loadReference.Has(s*8+p)&&oldLoad>0)loadReference.Set(s*8+p,loadReference.Value(s*8+p)*newLoad/oldLoad);
                }
                std::lock_guard<std::mutex> lock(mutex);options=*next;
            }
        }
        {std::lock_guard<std::mutex> lock(mutex);optionsApplied=revision;optionsAccepted=accepted;}
        wake.notify_all();
        if(accepted)::DHLS_StrandJack::Services::AuditLog::Write("PLC_CONFIGURATION","Scale/range updated; runtime state retained");
    }
    void RunSimulation()
    {
        SimulationSequence sequence(options);
        ::DHLS_StrandJack::Services::AuditLog::Write("SIM_SESSION","Virtual equipment connected");
        while (!stopping || EmergencyQueued())
        {
            ApplyPendingOptions(&sequence);
            std::deque<Command> pending;
            { std::lock_guard<std::mutex> lock(mutex); pending.swap(commands); }
            for (auto const& command : pending) sequence.Submit(command.action, command.request);
            auto next=sequence.Latest();
            std::unique_lock<std::mutex> lock(mutex);
            // A UI request arriving during Latest() must not publish an older idle/confirmed state.
            const bool emergencyPending=std::any_of(commands.begin(),commands.end(),[](Command const& c){return c.action=="EmergencyRequested";});
            const bool startPending=std::any_of(commands.begin(),commands.end(),[](Command const& c){return c.action=="StartRequested";});
            if(emergencyPending){
                next.pcEmergencyRequested=next.emergencyActive=true;
                next.sequenceBusy=true;next.sequenceStopConfirmed=false;
                next.sequencePhase=SequencePhase::Emergency;
            }else if(startPending){next.sequenceBusy=true;next.sequencePhase=SequencePhase::Checking;}
            next.sequence=published.sequence+1;next.configurationRevision=optionsApplied;
            published=std::move(next);wake.notify_all();
            wake.wait_for(lock,std::chrono::milliseconds(20),[&]{return stopping.load()||!commands.empty()||pendingOptions.has_value();});
        }
        sequence.Shutdown();
        auto finalSnapshot=sequence.Latest();
        { std::lock_guard<std::mutex> lock(mutex);
          finalSnapshot.sequence=published.sequence+1;published=std::move(finalSnapshot); }
        ::DHLS_StrandJack::Services::AuditLog::Write("SIM_SESSION","Virtual equipment disconnected");
        // The engine joins here after publishing confirmed shutdown, never on the UI thread.
    }
    void Run() noexcept
    {
        try
        {
            if (options.simulation) { RunSimulation(); return; }
            std::vector<std::unique_ptr<IPlcReadTransport>> readers;
            for (auto const &plc : options.plcs)
                readers.push_back(factory ? factory(plc, options.timeoutMs)
                                          : std::make_unique<TcpReader>(plc, options.timeoutMs));
            std::vector<bool> connected(readers.size(), false),extendedMapEnabled(readers.size(),true);
            std::vector<unsigned> mapAreaFailures(readers.size(),0);
            auto writeAllowed=[&](std::size_t index){
                if (liveMonitorOnly) return false;
                std::vector<std::uint16_t> state;
                try{return connected[index]&&readers[index]&&readers[index]->ReadWords(10,1,state)&&state.size()==1&&(state[0]&2)!=0;}catch(...){return false;}
            };
            auto clearManual=[&]{
                if(!manualNeedsClear)return;
                manualActive=false;bool allOff=true;
                for(std::size_t s=0;s<readers.size();++s){bool off=false;
                    try{std::vector<std::uint16_t> value;
                        if(connected[s]&&readers[s]&&readers[s]->ReadWords(52,1,value)&&value.size()==1){
                            off=(value[0]&255)==0;
                            if(!off&&writeAllowed(s))off=readers[s]->SetPortMask(0);
                        }
                    }catch(...){off=false;}
                    allOff&=off;
                }
                manualNeedsClear=!allOff;
                const std::string status=allOff?"Manual D052 OFF confirmed":"Manual D052 OFF unconfirmed";
                if(controlMessage!=status)::DHLS_StrandJack::Services::AuditLog::Write("PLC_MANUAL_D052",status);
                controlMessage=status;
            };
            struct ManualCleanup{std::function<void()> finish;~ManualCleanup(){try{finish();}catch(...){}}};
            ManualCleanup manualCleanup{[&]{manualHeld=false;if(manualClearRequested.exchange(false))manualNeedsClear=true;clearManual();std::lock_guard<std::mutex> lock(mutex);published.manualRunning=false;published.manualStopPending=manualNeedsClear;}};
            auto clearAutomatic=[&]{
                if(!automatic.Stopping())return;
                bool allOff=true;auto confirmed=automatic.Applied();
                for(std::size_t s=0;s<readers.size();++s){bool off=false;
                    try{std::vector<std::uint16_t> words;
                        if(connected[s]&&readers[s]&&readers[s]->ReadWords(52,1,words)&&words.size()==1){
                            off=(words[0]&255)==0;
                            if(!off){automatic.InvalidateOff();if(writeAllowed(s))off=readers[s]->SetPortMask(0);}
                        }
                    }catch(...){}
                    allOff&=off;if(off)confirmed.masks[s]=0;
                }
                if(allOff)automatic.Commit(confirmed,MonotonicMs());
            };
            auto automaticEnabled=[&]{
                if(stopping.load()||automaticCancel.load()||EmergencyQueued()||faultLatched||emergencyInterlock||pcEmergencyLatched)return false;
                EquipmentSnapshot sample;{std::lock_guard<std::mutex> lock(mutex);sample=published;}
                const auto now=MonotonicMs();
                for(auto const& plc:sample.plcs)if(!plc.connected||!plc.readOk||!plc.usable||!plc.pcSelected||!plc.sampledAtMs||now-plc.sampledAtMs>static_cast<unsigned>(options.maxSampleAgeMs))return false;
                return true;
            };
            auto automaticDirectionAllowed=[&](std::size_t s){
                std::vector<std::uint16_t> word;
                try{return writeAllowed(s)&&readers[s]->ReadWords(51,1,word)&&word.size()==1&&(word[0]&1)==0&&(word[0]&6)==(automatic.Lowering()?4u:2u);}catch(...){return false;}
            };
            auto applyAutomatic=[&]{
                if(!automatic.Preparing()&&!automatic.Running())return;
                const auto desired=automatic.Desired();auto confirmed=automatic.Applied();bool ok=automaticEnabled();
                // Remove outputs first, apply speed caps second, then enable ports.
                // A partial/uncertain ACK is never retried as ON.
                for(std::size_t s=0;s<readers.size()&&ok;++s){const auto reduced=static_cast<std::uint16_t>(confirmed.masks[s]&desired.masks[s]);
                    if(reduced!=confirmed.masks[s]){try{ok=writeAllowed(s)&&readers[s]->SetPortMask(reduced,automaticEnabled);}catch(...){ok=false;}if(ok)confirmed.masks[s]=reduced;}
                }
                for(std::size_t s=0;s<readers.size()&&ok;++s)if(desired.masks[s]&&(automatic.Preparing()||desired.hz[s]!=confirmed.hz[s])){
                    try{ok=automaticEnabled()&&automaticDirectionAllowed(s)&&readers[s]->SetAutomaticSpeed(desired.hz[s],automaticEnabled);}catch(...){ok=false;}
                    if(ok){confirmed.hz[s]=desired.hz[s];::DHLS_StrandJack::Services::AuditLog::Write("AUTO_D050","PLC="+std::to_string(s+1)+" Hz="+std::to_string(desired.hz[s]));}
                }
                for(std::size_t s=0;s<readers.size()&&ok;++s)if(confirmed.masks[s]!=desired.masks[s]){
                    try{ok=automaticEnabled()&&automaticDirectionAllowed(s)&&readers[s]->SetPortMask(desired.masks[s],automaticEnabled);}catch(...){ok=false;}
                    if(ok){confirmed.masks[s]=desired.masks[s];::DHLS_StrandJack::Services::AuditLog::Write("AUTO_D052","PLC="+std::to_string(s+1)+" mask="+std::to_string(desired.masks[s]));}
                }
                if(ok&&automaticEnabled())automatic.Commit(confirmed,MonotonicMs());
                else{automatic.Stop("Automatic output ACK/readback failed or stop requested",automaticCancel.load()?SequencePhase::Stopped:SequencePhase::Fault);clearAutomatic();}
            };
            ManualCleanup automaticCleanup{[&]{
                automaticCancel=true;automaticPending=false;automaticStart.reset();
                if(automatic.Busy()){automatic.Stop("Automatic shutdown: output OFF verification",SequencePhase::Stopped,true);clearAutomatic();}
                std::lock_guard<std::mutex> lock(mutex);
                if(automaticEngaged){published.automaticOperation=true;published.sequenceStopConfirmed=automatic.StopConfirmed();published.sequenceBusy=automatic.Busy();published.automaticOutputsOff=automatic.OutputsOff();published.monitoring=false;}
            }};
            unsigned mapGenerationSeen=0;
            while (!stopping || EmergencyQueued())
            {
                ApplyPendingOptions();
                Drain();
                if(automaticCancel.load()&&automatic.Busy()&&!automatic.Stopping())automatic.Stop("Automatic stop requested");
                clearAutomatic();
                if(manualClearRequested.exchange(false))manualNeedsClear=true;
                if(manualNeedsClear&&!manualHeld.load())clearManual();
                if(mapGenerationSeen!=mapGeneration.load()){mapGenerationSeen=mapGeneration.load();std::fill(extendedMapEnabled.begin(),extendedMapEnabled.end(),true);std::fill(mapAreaFailures.begin(),mapAreaFailures.end(),0);}
                // Never replay commands on reconnect; ACK/readback failure leaves result unknown.
                while (!controlCommands.empty()) {
                    auto command=std::move(controlCommands.front());controlCommands.pop_front();
                    if(command.action=="StartRequested"&&!command.request.automatic){
                        if(!manualHeld.load()||stopping||manualNeedsClear||faultLatched||emergencyInterlock||pcEmergencyLatched){manualHeld=false;controlMessage="Manual START rejected";continue;}
                        bool ready=true,any=false;std::array<std::uint16_t,4> masks{};
                        for(std::size_t s=0;s<options.plcs.size();++s){
                            for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(command.request.selected[s][p]){masks[s]|=static_cast<std::uint16_t>(1u<<options.plcs[s].channels[p].sensor);any=true;}
                            std::vector<std::uint16_t> direction;
                            ready&=writeAllowed(s)&&readers[s]->ReadWords(51,1,direction)&&direction.size()==1&&(direction[0]&1)==0&&(direction[0]&6)==(command.request.lowering?4:2);
                        }
                        EquipmentSnapshot loadStart;{std::lock_guard<std::mutex> lock(mutex);loadStart=published;}
                        for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(command.request.selected[s][p]){
                            if(s>=loadStart.plcs.size()||p>=loadStart.plcs[s].ports.size()){ready=false;continue;}
                            auto const& v=loadStart.plcs[s].ports[p];auto const& c=options.plcs[s].channels[p];
                            ready &= loadStart.plcs[s].readOk && v.loadValid && std::isfinite(v.loadTon) && c.maximumLoadTon>0 && v.loadTon<c.maximumLoadTon;
                        }
                        if(!ready||!any||!manualHeld.load()){manualHeld=false;manualNeedsClear=true;clearManual();controlMessage=manualNeedsClear?"Manual D052 OFF unconfirmed":"Manual START rejected";continue;}
                        if(options.manualSync){
                            EquipmentSnapshot start;{std::lock_guard<std::mutex> lock(mutex);start=published;}
                            const double work=command.request.lowering?options.fallWorkMm:options.riseWorkMm;
                            const double trip=command.request.lowering?options.fallStopMm:options.riseStopMm;
                            ready=std::isfinite(work)&&std::isfinite(trip)&&work>0&&trip>work;
                            for(std::size_t s=0;s<options.plcs.size();++s)for(std::size_t p=0;p<options.plcs[s].channels.size();++p)if(command.request.selected[s][p]){
                                if(s>=start.plcs.size()||p>=start.plcs[s].ports.size()){ready=false;continue;}
                                auto const& v=start.plcs[s].ports[p];ready&=v.strokeValid&&std::isfinite(v.absoluteMm);manualOrigins[s][p]=v.absoluteMm;
                            }
                            if(!ready){manualHeld=false;controlMessage="Manual sync requires valid stroke feedback and synchronization limits";continue;}
                        }
                        manualPaused.clear();manualMasks=masks;manualExpectedMasks=masks;manualNeedsClear=true;bool on=true;
                        for(std::size_t s=0;s<readers.size()&&on;++s){
                            if(!manualHeld.load()||stopping||EmergencyQueued()){on=false;break;}
                            try{on=readers[s]->SetPortMask(masks[s],[&]{return manualHeld.load()&&!stopping.load();});}catch(...){on=false;}
                        }
                        if(!on||!manualHeld.load()){manualHeld=false;clearManual();}
                        else{manualActive=true;controlMessage="Manual D052 ON confirmed";::DHLS_StrandJack::Services::AuditLog::Write("PLC_MANUAL_D052",controlMessage);}
                        continue;
                    }
                    const bool pump=command.action.rfind("PumpToggle:",0)==0;
                    const bool speedChange=command.action.rfind("LiftingSpeed:",0)==0||command.action.rfind("LoweringSpeed:",0)==0;
                    int target=-1;
                    if(speedChange){try{target=std::stoi(command.action.substr(command.action.find(':')+1))-1;}catch(...){target=-2;}}
                    if(pump) {try {target=std::stoi(command.action.substr(11))-1;}catch(...){target=-2;}}
                    bool speedWritten=false,writeDenied=false;
                    bool allOk=target!=-2 && !stopping && (!monitoring||speedChange) && !sendEmergency && !pcEmergencyLatched && !emergencyInterlock && !faultLatched;
                    for(std::size_t s=0;s<readers.size() && allOk;++s) {
                        if((pump||speedChange) && static_cast<int>(s)!=target)continue;
                        bool fresh=false;
                        {std::lock_guard<std::mutex> lock(mutex);auto const& plc=published.plcs[s];
                         fresh=plc.connected&&plc.readOk&&MonotonicMs()-plc.sampledAtMs<=static_cast<unsigned>(options.maxSampleAgeMs);}
                        if(!fresh || !connected[s] || EmergencyQueued()){allOk=false;break;}
                        if(!writeAllowed(s)){writeDenied=true;allOk=false;break;}
                        try {
                            std::uint16_t mask=6,value=command.action=="DownRequested"?4:2;
                            if(pump){std::vector<std::uint16_t> word;
                                if(!readers[s]->ReadWords(51,1,word)||word.size()!=1){allOk=false;break;}
                                mask=8;value=(word[0]&8)?0:8;
                            }
                            if(speedChange){
                                std::vector<std::uint16_t> word;
                                if(!readers[s]->ReadWords(51,1,word)||word.size()!=1){allOk=false;break;}
                                const bool lifting=command.action.rfind("LiftingSpeed:",0)==0;
                                const auto direction=word[0]&6;
                                if(direction==6){allOk=false;break;} // Conflicting PLC direction bits are never usable.
                                // The green UI arrow is feedback, never an alternative source of PLC direction.
                                if(direction==0)continue;
                                if((direction==2)!=lifting)continue;
                                const auto hz=lifting?command.request.liftingHz[s]:command.request.loweringHz[s];
                                allOk=std::isfinite(hz)&&hz>=0&&hz<=60&&std::floor(hz)==hz && readers[s]->SetSpeed(static_cast<std::uint16_t>(hz));
                                speedWritten=allOk;
                            }else if(!pump){
                                const auto hz=value==2?command.request.liftingHz[s]:command.request.loweringHz[s];
                                allOk=std::isfinite(hz)&&hz>=0&&hz<=60&&std::floor(hz)==hz;
                                // Clear the old direction before changing the shared speed register.
                                if(allOk)allOk=readers[s]->SetControlBits(6,0);
                                if(allOk&&!EmergencyQueued())allOk=readers[s]->SetSpeed(static_cast<std::uint16_t>(hz));else allOk=false;
                                if(allOk&&!EmergencyQueued())allOk=readers[s]->SetControlBits(mask,value);else allOk=false;
                            }else allOk=readers[s]->SetControlBits(mask,value);
                        }catch(...){allOk=false;}
                        // Failed/denied writes are never replayed. Read polling independently determines connection quality.
                    }
                    if((pump||speedChange) && (target<0||target>=static_cast<int>(readers.size())))allOk=false;
                    controlMessage=writeDenied?"D010.1 OFF or unconfirmed: monitoring only; write blocked":allOk?(speedChange?(speedWritten?"D050 speed confirmed":"Inactive direction speed stored locally; D050 unchanged"):"D051 command confirmed"):
                        "D050/D051 command failed or unavailable; check PLC state";
                    ::DHLS_StrandJack::Services::AuditLog::Write("PLC_COMMAND",command.action+": "+controlMessage);
                }
                if (sendEmergency)
                {
                    sendEmergency = false;
                    for (std::size_t s = 0; s < readers.size(); ++s)
                    {
                        bool confirmed = false;
                        try
                        {
                            if (readers[s])
                            {
                                if (!connected[s]) connected[s] = readers[s]->Connect();
                                confirmed = writeAllowed(s) && readers[s]->SetEmergencyOn();
                                if (!confirmed) { readers[s]->Disconnect(); connected[s] = false; }
                            }
                        }
                        catch (std::exception const&) { connected[s] = false; }
                        emergencyResults[s] = confirmed ? EmergencyWriteState::ConfirmedOn : EmergencyWriteState::FailedOrUnknown;
                        std::lock_guard<std::mutex> lock(mutex);
                        published.pcEmergencyRequested = published.emergencyActive = true;
                        published.monitoring = false;
                        published.plcs[s].emergencyWrite = emergencyResults[s];
                        ++published.sequence;
                    }
                }
                EquipmentSnapshot next;
                next.controlMessage=controlMessage;
                next.pcEmergencyRequested = pcEmergencyLatched;
                next.readOnly = !pcEmergencyLatched;
                next.plcs.resize(readers.size());
                for (std::size_t s = 0; s < readers.size(); ++s)
                {
                    next.plcs[s].emergencyWrite = emergencyResults[s];
                    next.plcs[s].ports.resize(options.plcs[s].channels.size());
                }
                next.monitoring = monitoring;
                next.message = message;
                const auto cycleBegin = MonotonicMs();
                std::vector<COGCalculator::Support> supports;
                bool cogInputsValid = true;
                const bool cycleFaultBefore=faultLatched;
                bool allEmergencyStatesRead = true, anyEmergency = false;
                std::vector<bool> commandKnown(readers.size(),false);
                std::vector<std::uint16_t> commandWords(readers.size(),0);
                for(auto& words:faultWords)words.known.fill(false);
                std::vector<SynchronizationAlgorithm::Cylinder> globalCylinders(readers.size() * 8);
                for (auto &c : globalCylinders)
                    c.selected = false;
                std::vector<double> globalPressures;
                bool pressureSourcesValid = true;
                for (std::size_t s = 0; s < readers.size() && !stopping; ++s)
                {
                    auto &output = next.plcs[s];
                    if(!connected[s])for(auto& filter:strokeFilters[s])filter.Clear();
                    output.emergencyWrite = emergencyResults[s];
                    auto const &plc = options.plcs[s];
                    output.ports.resize(plc.channels.size());
                    if (!readers[s])
                    {
                        output.diagnostic = "Missing transport";
                        cogInputsValid = false;
                        allEmergencyStatesRead = false;
                        continue;
                    }
                    if (!connected[s])
                        connected[s] = readers[s]->Connect();
                    output.connected = connected[s];
                    CachedDWords cache;
                    const auto before = MonotonicMs();
                    bool ok = connected[s];
                    auto read = [&](std::uint16_t address, std::size_t count,
                                    std::vector<std::uint16_t> &data) {
                        return !stopping && readers[s]->ReadWords(address, count, data) &&
                               data.size() == count;
                    };
                    const bool stateRead = ok && read(10, 1, cache.state);
                    allEmergencyStatesRead &= stateRead;
                    output.emergencyBit = stateRead && (cache.state[0] & 1) != 0;
                    anyEmergency |= output.emergencyBit;
                    if (output.emergencyBit)
                    {
                        emergencyInterlock = true;
                        monitoring = false;
                        captureOrigins = false;
                        message = "PLC emergency input active: diagnostics halted; PLC outputs NOT verified stopped";
                        // Publish the input immediately, even if a subsequent sensor read fails.
                        std::lock_guard<std::mutex> lock(mutex);
                        published.emergencyActive = true;
                        published.emergencyStateVerified = false;
                        published.monitoring = false;
                        published.message = message;
                        ++published.sequence;
                    }
                    std::vector<std::uint16_t> pcCommand;
                    const bool pcStateRead = stateRead && read(51, 1, pcCommand);
                    if (pcStateRead && (pcCommand[0] & 1) != 0)
                    {
                        pcEmergencyLatched = true;
                        monitoring = false;
                        captureOrigins = false;
                        next.pcEmergencyRequested = true;
                        emergencyResults[s] = output.emergencyWrite = EmergencyWriteState::ConfirmedOn;
                        std::lock_guard<std::mutex> lock(mutex);
                        published.pcEmergencyRequested = published.emergencyActive = true;
                        published.monitoring = false;
                        published.plcs[s].emergencyWrite = EmergencyWriteState::ConfirmedOn;
                        ++published.sequence;
                    }
                    std::vector<std::uint16_t> selectionWord,inverterFault;
                    const bool selectionRead=connected[s]&&read(52,1,selectionWord);
                    const bool inverterRead=connected[s]&&read(55,1,inverterFault);
                    const bool pressureFaultRead=connected[s]&&read(56,1,cache.pressureFaults);
                    const bool strokeFaultRead=connected[s]&&read(59,1,cache.strokeFaults);
                    auto updateFault=[&](int index,bool known,std::vector<std::uint16_t> const& value){
                        faultWords[s].known[index]=known;
                        if(known){
                            const auto changed=static_cast<std::uint16_t>(faultWords[s].bits[index]^value[0]);
                            const int addresses[]={52,55,56,59};
                            for(int bit=0;bit<16;++bit)if(changed&(1u<<bit))
                                ::DHLS_StrandJack::Services::AuditLog::Write("PLC_BIT_STATE","PLC="+std::to_string(s+1)+" D"+std::to_string(addresses[index])+" bit="+std::to_string(bit)+((value[0]&(1u<<bit))?" ON":" OFF"));
                            faultWords[s].bits[index]=value[0]; // Failed reads retain the last ON bits.
                        }
                    };
                    updateFault(0,selectionRead,selectionWord);updateFault(1,inverterRead,inverterFault);
                    updateFault(2,pressureFaultRead,cache.pressureFaults);updateFault(3,strokeFaultRead,cache.strokeFaults);
                    commandKnown[s]=pcStateRead;if(pcStateRead)commandWords[s]=pcCommand[0];
                    bool observedFault=false;
                    for(int index=1;index<4;++index)
                        observedFault|=faultWords[s].known[index]&&faultWords[s].bits[index]!=0;
                    if(observedFault&&!faultLatched){
                        faultLatched=true;monitoring=false;captureOrigins=false;controlCommands.clear();
                        // Stop already connected PLCs before waiting for any later PLC's sensor reads.
                        for(std::size_t k=0;k<readers.size();++k)if(connected[k]&&readers[k]){
                            bool off=commandKnown[k]&&(commandWords[k]&0x000E)==0;
                            if(!off&&writeAllowed(k)){try{off=readers[k]->StopOutputs();}catch(...){off=false;}}
                            if(off&&commandKnown[k])commandWords[k]&=static_cast<std::uint16_t>(~0x000E);
                            
                            ::DHLS_StrandJack::Services::AuditLog::Write("PLC_FAULT_STOP","PLC="+std::to_string(k+1)+(off?" immediate OFF confirmed":" stop failed or unknown"));
                        }
                    }

                    ok = pcStateRead && selectionRead && inverterRead && pressureFaultRead && strokeFaultRead && read(11,16,cache.values);
                    const auto after = MonotonicMs();
                    output.sampledAtMs = after;
                    output.readOk = ok;
                    auto cacheMap=[&](std::uint16_t first,std::vector<std::uint16_t> const& data){
                        for(std::size_t i=0;i<data.size();++i)output.mapWords.push_back({static_cast<std::uint16_t>(first+i),data[i],true,after});
                    };
                    cacheMap(52,selectionWord);cacheMap(55,inverterFault);cacheMap(10,cache.state);cacheMap(51,pcCommand);cacheMap(11,cache.values);cacheMap(56,cache.pressureFaults);cacheMap(59,cache.strokeFaults);
                    output.usable = ok && after - before <= static_cast<unsigned>(options.maxSampleAgeMs);
                    if (!ok)
                    {
                        for(auto& filter:strokeFilters[s])filter.Clear();
                        readers[s]->Disconnect();
                        connected[s] = false;
                        output.connected = false;
                        output.diagnostic = "PLC read failed";
                        cogInputsValid = false;
                        continue;
                    }
                    // Optional diagnostics never enable outputs or alter the core read/diagnostic validity.
                    if(mapMonitoring.load()&&extendedMapEnabled[s]){
                        for(std::uint16_t address:{std::uint16_t(50)}){
                            std::vector<std::uint16_t> value;bool received=false;
                            try{received=read(address,1,value);}catch(...){received=false;}
                            if(!received){extendedMapEnabled[s]=false;readers[s]->Disconnect();connected[s]=false;break;}
                            output.mapWords.push_back({address,value[0],true,MonotonicMs()});
                        }
                    }
                    // Only selected map areas are polled, on the existing single worker/socket.
                    if(mapMonitoring.load() && connected[s]) {
                        const auto selected=mapAreas.load();
                        for(unsigned flag:{1u,2u}) {
                            if(!(selected&flag)||(mapAreaFailures[s]&flag)) continue;
                            const char area=flag==1?'P':'M';
                            const std::vector<std::uint16_t> addresses=flag==1?std::vector<std::uint16_t>{0,2,8}:std::vector<std::uint16_t>{0,1,2,3,4,5,6,7,8};
                            std::vector<std::uint16_t> values;bool received=false;
                            try { received=!stopping && readers[s]->ReadDeviceWords(area,addresses,values) && values.size()==addresses.size(); }catch(...){}
                            if(!received){mapAreaFailures[s]|=flag;readers[s]->Disconnect();connected[s]=false;break;}
                            const auto stamp=MonotonicMs();
                            for(std::size_t i=0;i<addresses.size();++i)output.mapWords.push_back({addresses[i],values[i],true,stamp,area});
                        }
                    }
                    next.connected = true;
                    output.pcSelected = (cache.state[0] & 2) != 0;
                    if(!output.pcSelected){monitoring=false;captureOrigins=false;message="D010.1 OFF: status monitoring only; PC writes disabled";}
                    output.pumpOn = (cache.state[0] & 4) != 0;
                    output.emergencyBit = (cache.state[0] & 1) != 0;
                    unsigned sensorMask = 0;
                    for (auto const &channel : plc.channels)
                        sensorMask |= 1u << channel.sensor;
                    output.error = (cache.state[0] & 8) != 0 ||
                                   ((cache.pressureFaults[0] | cache.strokeFaults[0]) & sensorMask) != 0;
                    DRegionDiagnostics::Snapshot raw;
                    raw.readOk = output.usable;
                    raw.pcSelected = output.pcSelected;
                    raw.inverterError = (cache.state[0] & 8) != 0;
                    DRegionDiagnostics::Config diagnostics;
                    diagnostics.selected.fill(false);
                    diagnostics.maxAcquisitionDurationMs = options.maxSampleAgeMs;
                    auto &tolerances = diagnostics.settings;
                    tolerances.workToleranceMm = request.lowering ? options.fallWorkMm : options.riseWorkMm;
                    tolerances.stopToleranceMm = request.lowering ? options.fallStopMm : options.riseStopMm;
                    tolerances.maxSampleAgeMs = options.maxSampleAgeMs;
                    PressureBalancedLiftUp::Config balance;
                    balance.maxSpreadMpa = options.pressureSpreadToleranceBar > 0
                                               ? options.pressureSpreadToleranceBar * 0.1
                                               : NaN;
                    balance.targetToleranceMpa = 0.1;
                    balance.maxSampleAgeMs = options.maxSampleAgeMs;
                    PressureLiftUp::Config pressure;
                    pressure.maxSampleAgeMs = options.maxSampleAgeMs;
                    std::vector<SynchronizationAlgorithm::Cylinder> cylinders;
                    for (std::size_t p = 0; p < plc.channels.size(); ++p)
                    {
                        auto const &channel = plc.channels[p];
                        auto &value = output.ports[p];
                        const auto id = channel.sensor;
                        value.rawPressure = cache.values[2 * id];
                        value.rawStroke = cache.values[2 * id + 1];
                        value.pressureFault = (cache.pressureFaults[0] & (1u << id)) != 0;
                        value.strokeFault = (cache.strokeFaults[0] & (1u << id)) != 0;
                        raw.rawPressure[id] = static_cast<std::uint16_t>(value.rawPressure);
                        raw.rawStroke[id] = static_cast<std::uint16_t>(value.rawStroke);
                        raw.pressureDisconnected[id] = value.pressureFault;
                        raw.strokeDisconnected[id] = value.strokeFault;
                        value.pressureDisplayValid=output.usable&&!value.pressureFault&&
                            std::isfinite(channel.pressureBarPerCount)&&channel.pressureBarPerCount>0;
                        value.displayPressureBar=value.pressureDisplayValid?
                            Decode(value.rawPressure,channel.pressureSigned)*channel.pressureBarPerCount:NaN;
                        value.displayPressureBar=PressureZeroValue(value.displayPressureBar,pressureZeroEnabled[s][p]);
                        value.pressureDisplayValid=value.pressureDisplayValid&&std::isfinite(value.displayPressureBar)&&value.displayPressureBar>=0;
                        value.loadDisplayValid=value.pressureDisplayValid&&std::isfinite(channel.effectiveAreaMm2)&&channel.effectiveAreaMm2>0;
                        value.displayLoadTon=value.loadDisplayValid?
                            (value.displayPressureBar<=options.displayLoadZeroBar?0:value.displayPressureBar*channel.effectiveAreaMm2/98066.5):NaN;
                        if(!output.usable||value.strokeFault)strokeFilters[s][p].Clear();
                        const double filteredStroke=output.usable&&!value.strokeFault?strokeFilters[s][p].Push(Decode(value.rawStroke,channel.strokeSigned),options.strokeFilterSamples):NaN;
                        const double displayScale=channel.StrokeDisplayScale();
                        value.strokeDisplayValid=output.usable&&!value.strokeFault&&std::isfinite(displayScale)&&displayScale>0;
                        value.displayAbsoluteMm=value.strokeDisplayValid?filteredStroke*displayScale:NaN;
                        value.strokeDisplayValid=value.strokeDisplayValid&&std::isfinite(value.displayAbsoluteMm);
                        value.displayRelativeMm=value.strokeDisplayValid?value.displayAbsoluteMm-heightZeroOrigins[s][p]:NaN;
                        value.pressureValid =
                            output.usable && channel.scalingVerified && !value.pressureFault &&
                            std::isfinite(channel.pressureBarPerCount) && channel.pressureBarPerCount > 0;
                        value.strokeValid = output.usable && channel.scalingVerified && !value.strokeFault &&
                                            std::isfinite(channel.strokeMmPerCount) &&
                                            channel.strokeMmPerCount > 0;
                        value.pressureBar = value.pressureValid
                                                ? Decode(value.rawPressure, channel.pressureSigned) *
                                                      channel.pressureBarPerCount
                                                : NaN;
                        value.absoluteMm = value.strokeValid ? filteredStroke *
                                                                   channel.strokeMmPerCount
                                                             : NaN;
                        value.pressureBar=PressureZeroValue(value.pressureBar,pressureZeroEnabled[s][p]);
                        value.pressureValid =
                            value.pressureValid && std::isfinite(value.pressureBar) && value.pressureBar >= 0;
                        value.strokeValid = value.strokeValid && std::isfinite(value.absoluteMm);
                        if (captureOrigins)
                            origins[s][p] = value.absoluteMm;
                        value.relativeMm = value.strokeValid && std::isfinite(origins[s][p])
                                               ? value.absoluteMm - origins[s][p]
                                               : NaN;
                        value.loadValid = value.pressureValid && channel.areaVerified &&
                                          std::isfinite(channel.effectiveAreaMm2) &&
                                          channel.effectiveAreaMm2 > 0;
                        value.loadTon =
                            value.loadValid ? value.pressureBar * channel.effectiveAreaMm2 / 98066.5 : NaN;
                        diagnostics.selected[id] = request.selected[s][p];
                        diagnostics.startMm[id] = origins[s][p];
                        diagnostics.targetDeltaMm[id] = (request.lowering ? -1 : 1) * request.targetMm;
                        diagnostics.stroke[id] = {channel.strokeMmPerCount, 0, channel.strokeSigned,
                                                  channel.scalingVerified};
                        SynchronizationAlgorithm::Cylinder cylinder;
                        cylinder.selected = request.selected[s][p];
                        cylinder.startMm = origins[s][p];
                        cylinder.currentMm = value.absoluteMm;
                        cylinder.targetDeltaMm = diagnostics.targetDeltaMm[id];
                        cylinder.sampleTimeMs = after;
                        cylinder.quality = value.strokeValid ? SynchronizationAlgorithm::Quality::Valid
                                                             : SynchronizationAlgorithm::Quality::Fault;
                        cylinders.push_back(cylinder);
                        globalCylinders[s * 8 + p] = cylinder;
                        if (request.selected[s][p])
                        {
                            if (value.pressureValid)
                                globalPressures.push_back(value.pressureBar);
                            else
                                pressureSourcesValid = false;
                            PressureBalancedLiftUp::Cylinder selected;
                            selected.index = id;
                            selected.mpaPerCount = channel.pressureBarPerCount * 0.1;
                            selected.signed16 = channel.pressureSigned;
                            selected.calibrationVerified = channel.scalingVerified;
                            selected.targetMpa = request.targetBar * 0.1;
                            selected.advisoryMaxMpa = channel.maximumPressureBar * 0.1;
                            balance.selected.push_back(selected);
                            pressure.selected[id] = true;
                            pressure.pressure[id] = {channel.pressureBarPerCount * 0.1, 0,
                                                     channel.pressureSigned, channel.scalingVerified,
                                                     channel.effectiveAreaMm2 / 100};
                            pressure.targetMpa[id] = request.targetBar * 0.1;
                            pressure.maxAdvisoryMpa[id] = channel.maximumPressureBar * 0.1;
                            COGCalculator::Support support;
                            support.cylinderId = s * 8 + p;
                            support.xMm = channel.xMm;
                            support.yMm = channel.yMm;
                            support.geometryVerified = channel.geometryVerified;
                            support.loadKN = value.loadTon * 9.80665;
                            support.loadVerified = value.loadValid;
                            supports.push_back(support);
                            cogInputsValid &= value.loadValid && channel.geometryVerified;
                        }
                    }
                    if (monitoring)
                    {
                        try
                        {
                            SynchronizationAlgorithm algorithm(tolerances);
                            output.synchronization =
                                algorithm.Analyze(cylinders, SynchronizationAlgorithm::Mode::CommonDelta,
                                                  SynchronizationAlgorithm::PlcState::Unknown, after);
                            // Reuse the D adapter over the cached sample; never re-read a second snapshot.
                            auto diagnostic = DRegionDiagnostics(diagnostics).Poll(cache, before, after);
                            raw.usable = diagnostic.usable;
                            raw.diagnostic = diagnostic.diagnostic;
                            if (!request.pressureMode && !output.synchronization.dataValid)
                                output.diagnostic = "Invalid synchronization configuration or sensor data";
                        }
                        catch (std::invalid_argument const &)
                        {
                            output.diagnostic = "Synchronization tolerances require stop >= 3 * work > 0";
                        }
                        if (request.pressureMode)
                        {
                            auto checked = PressureBalancedLiftUp(balance).Analyze(raw, after, after);
                            auto lift = PressureLiftUp(pressure).Analyze(raw, after, after);
                            output.pressureDiagnosticValid =
                                checked.state != PressureBalancedLiftUp::State::Invalid &&
                                lift.state != PressureLiftUp::State::Invalid;
                            output.diagnostic = checked.reason;
                            output.diagnosticWarning = !output.pressureDiagnosticValid ||
                                                       checked.anyLimitExceeded ||
                                                       !checked.balanceWithinTolerance;
                        }
                        else if (request.lowering)
                        {
                            // Explicitly uncommissioned planner: diagnostics cannot authorize motion.
                            LoweringControl::Inputs inputs;
                            std::vector<double> start(8, 0);
                            std::uint16_t mask = 0;
                            for (std::size_t p = 0; p < cylinders.size(); ++p)
                            {
                                start[p] = cylinders[p].startMm;
                                if (cylinders[p].selected)
                                    mask |= static_cast<std::uint16_t>(1u << p);
                            }
                            auto plan =
                                LoweringControl::Prepare(inputs, mask, request.targetMm, start, after);
                            if (output.diagnostic.empty())
                                output.diagnostic = plan.reason;
                        }
                    }
                    if (monitoring && !request.pressureMode)
                        output.diagnosticWarning =
                            !output.synchronization.dataValid ||
                            output.synchronization.assessment != SynchronizationAlgorithm::Assessment::Normal;
                    if (!output.usable)
                        cogInputsValid = false;
                }
                bool anyFault=false,allFaultKnown=!readers.empty();
                const int faultAddresses[]={52,55,56,59};
                for(std::size_t s=0;s<faultWords.size();++s){
                    for(int index=1;index<4;++index){
                        auto const& words=faultWords[s];allFaultKnown&=words.known[index];
                        for(int bit=0;bit<16;++bit)if(words.bits[index]&(1u<<bit)){
                            anyFault=true;next.faultBits.push_back({static_cast<int>(s),faultAddresses[index],bit,words.known[index]});
                        }
                    }
                }
                const bool previouslyLatched=cycleFaultBefore;
                faultLatched|=anyFault;
                if(faultLatched){
                    monitoring=false;captureOrigins=false;controlCommands.clear();
                    next.faultStopOff.assign(readers.size(),false);
                    for(std::size_t s=0;s<readers.size();++s){
                        bool off=commandKnown[s]&&(commandWords[s]&0x000E)==0;
                        if(!off&&connected[s]&&readers[s]&&writeAllowed(s)){
                            try{off=readers[s]->StopOutputs();}catch(...){off=false;}
                            // A denied OFF command does not disable status monitoring. The transport closes itself on protocol failure.
                            ::DHLS_StrandJack::Services::AuditLog::Write("PLC_FAULT_STOP","PLC="+std::to_string(s+1)+(off?" D051.1/.2/.3 OFF confirmed":" stop failed or unknown"));
                        }
                        next.faultStopOff[s]=off;next.plcs[s].error=true;
                    }
                    next.faultStopConfirmed=std::all_of(next.faultStopOff.begin(),next.faultStopOff.end(),[](bool off){return off;});
                    allFaultKnown&=MonotonicMs()-cycleBegin<=static_cast<unsigned>(options.maxSampleAgeMs);
                    if(!allFaultKnown)for(auto& bit:next.faultBits)bit.confirmed=false;
                    if(!anyFault&&allFaultKnown&&next.faultStopConfirmed)faultLatched=false;
                    message=faultLatched?"PLC fault: stopped; restart blocked until OFF verified":"PLC fault cleared; explicit START required";
                }
                next.faultActive=faultLatched;next.faultStatesKnown=allFaultKnown;
                if(previouslyLatched!=faultLatched)::DHLS_StrandJack::Services::AuditLog::Write("PLC_FAULT",faultLatched?"Fault interlock ON":"Fault interlock cleared; no automatic restart");
                next.emergencyStateVerified = !stopping && allEmergencyStatesRead && !readers.empty() &&
                    MonotonicMs() - cycleBegin <= static_cast<unsigned>(options.maxSampleAgeMs);
                if (next.emergencyStateVerified && !anyEmergency && emergencyInterlock)
                {
                    emergencyInterlock = false;
                    next.message = message = "PLC emergency input cleared; restart diagnostics explicitly";
                }
                next.emergencyActive = emergencyInterlock || pcEmergencyLatched;
                next.readOnly = !pcEmergencyLatched;
                if (emergencyInterlock || pcEmergencyLatched)
                {
                    monitoring = false;
                    captureOrigins = false;
                    next.message = message = "PLC emergency input active: diagnostics halted; PLC outputs NOT verified stopped";
                }
                next.monitoring = monitoring;
                if (monitoring)
                {
                    bool usable = true;
                    for (std::size_t s = 0; s < next.plcs.size(); ++s)
                        for (std::size_t p = 0; p < next.plcs[s].ports.size(); ++p)
                            if (request.selected[s][p])
                                usable &= next.plcs[s].usable &&
                                          (request.pressureMode ? next.plcs[s].ports[p].pressureValid
                                                                : next.plcs[s].ports[p].strokeValid);
                    if (!usable)
                    {
                        monitoring = false;
                        next.monitoring = false;
                        message = "Diagnostics halted: invalid selected data";
                        next.message = message;
                    }
                }
                if (monitoring)
                {
                    if (request.pressureMode)
                    {
                        for (auto const &plc : next.plcs)
                        {
                            bool selected = false;
                            auto index = static_cast<std::size_t>(&plc - next.plcs.data());
                            for (std::size_t p = 0; p < plc.ports.size(); ++p)
                                selected |= request.selected[index][p];
                            if (selected)
                                pressureSourcesValid &= plc.pressureDiagnosticValid;
                        }
                        next.pressureBalanceValid =
                            pressureSourcesValid && !globalPressures.empty() &&
                            std::isfinite(options.pressureSpreadToleranceBar) &&
                            options.pressureSpreadToleranceBar > 0 &&
                            MonotonicMs() - cycleBegin <= static_cast<unsigned>(options.maxSampleAgeMs);
                        if (!globalPressures.empty())
                        {
                            auto range = std::minmax_element(globalPressures.begin(), globalPressures.end());
                            next.pressureSpreadBar = *range.second - *range.first;
                        }
                        next.pressureBalanced = next.pressureBalanceValid &&
                                                next.pressureSpreadBar <= options.pressureSpreadToleranceBar;
                        next.diagnosticWarning = !next.pressureBalanced;
                    }
                    else
                    {
                        SynchronizationAlgorithm::Settings settings;
                        settings.workToleranceMm = request.lowering ? options.fallWorkMm : options.riseWorkMm;
                        settings.stopToleranceMm = request.lowering ? options.fallStopMm : options.riseStopMm;
                        settings.maxSampleAgeMs = options.maxSampleAgeMs;
                        try
                        {
                            next.synchronization = SynchronizationAlgorithm(settings).Analyze(
                                globalCylinders, SynchronizationAlgorithm::Mode::CommonDelta,
                                SynchronizationAlgorithm::PlcState::Unknown, MonotonicMs());
                        }
                        catch (std::invalid_argument const &)
                        {
                        }
                        next.diagnosticWarning =
                            !next.synchronization.dataValid ||
                            next.synchronization.assessment != SynchronizationAlgorithm::Assessment::Normal;
                    }
                    if (next.diagnosticWarning)
                        for (std::size_t s = 0; s < next.plcs.size(); ++s)
                            for (std::size_t p = 0; p < next.plcs[s].ports.size(); ++p)
                                if (request.selected[s][p])
                                    next.plcs[s].diagnosticWarning = true;
                }
                captureOrigins = false;
                const auto finished = MonotonicMs();
                if (cogInputsValid && !supports.empty() &&
                    finished - cycleBegin <= static_cast<unsigned>(options.maxSampleAgeMs))
                    next.cog = COGCalculator{}.Calculate(supports, supports.front().cylinderId);
                else
                    next.cog.reason = "COG requires fresh calibrated loads and verified X/Y coordinates";
                if(options.loadToleranceEnabled && !loadReferenceTripped && (manualActive||automatic.Busy()||monitoring)){
                    for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].ports.size();++p)if(request.selected[s][p]){
                        auto const& v=next.plcs[s].ports[p];
                        if(loadReference.Exceeded(s*8+p,v.loadTon,next.plcs[s].usable&&v.loadValid,options.loadTolerancePercent)){
                            loadReferenceTripped=true;loadReferenceMessage="LOAD TOLERANCE exceeded / invalid load: SET "+std::to_string(s+1)+" PORT "+std::to_string(p+1);
                            ::DHLS_StrandJack::Services::AuditLog::Write("LOAD_TOLERANCE",loadReferenceMessage);break;
                        }
                    }
                }
                if(loadReferenceTripped){
                    manualHeld=false;monitoring=false;automaticStart.reset();automaticPending=false;
                    if(automatic.Busy())automatic.Stop(loadReferenceMessage,SequencePhase::Fault);
                }
                for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].ports.size();++p)next.plcs[s].ports[p].referenceLoadTon=loadReference.Value(s*8+p);
                next.loadReferenceTripped=loadReferenceTripped;next.loadReferenceMessage=loadReferenceMessage;
                if(manualActive){
                    bool valid=manualHeld.load()&&!faultLatched&&!emergencyInterlock&&!pcEmergencyLatched;
                    for(std::size_t s=0;s<next.plcs.size();++s){auto const& plc=next.plcs[s];unsigned mask=0,direction=0;bool known=false;
                        for(auto const& word:plc.mapWords)if(word.family=='D'&&word.valid){if(word.address==52){mask=word.value&255;known=true;}if(word.address==51)direction=word.value&6;}
                        valid&=plc.connected&&plc.readOk&&plc.usable&&plc.pcSelected&&known&&mask==manualExpectedMasks[s]&&direction==(request.lowering?4u:2u);
                    }
                    for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].ports.size();++p)
                        if(manualMasks[s]&(1u<<options.plcs[s].channels[p].sensor)){
                            auto const& v=next.plcs[s].ports[p];auto const& c=options.plcs[s].channels[p];
                            if(!v.loadValid || !std::isfinite(v.loadTon) || c.maximumLoadTon<=0 || v.loadTon>=c.maximumLoadTon){
                                valid=false;controlMessage="Manual stopped: maximum load or invalid load feedback";
                            }
                        }
                    if(valid&&options.manualSync){
                        std::vector<double> travel;std::vector<std::pair<std::size_t,std::size_t>> selected;
                        for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].ports.size();++p)
                            if(manualMasks[s]&(1u<<options.plcs[s].channels[p].sensor)){
                                auto const& port=next.plcs[s].ports[p];valid&=port.strokeValid;
                                travel.push_back((port.absoluteMm-manualOrigins[s][p])*(request.lowering?-1:1));selected.emplace_back(s,p);
                            }
                        valid=valid&&ManualSyncPlan(travel,request.lowering?options.fallWorkMm:options.riseWorkMm,request.lowering?options.fallStopMm:options.riseStopMm,manualPaused);
                        auto desired=manualMasks;
                        if(valid){for(std::size_t i=0;i<selected.size();++i)if(manualPaused[i])desired[selected[i].first]&=static_cast<std::uint16_t>(~(1u<<options.plcs[selected[i].first].channels[selected[i].second].sensor));
                            for(std::size_t s=0;s<readers.size()&&valid;++s)if(desired[s]!=manualExpectedMasks[s]){
                                bool ok=false;try{ok=manualHeld.load()&&!stopping&&writeAllowed(s)&&readers[s]->SetPortMask(desired[s],[&]{return manualHeld.load()&&!stopping.load()&&!EmergencyQueued();});}catch(...){}
                                if(ok){manualExpectedMasks[s]=desired[s];::DHLS_StrandJack::Services::AuditLog::Write("MANUAL_SYNC","PLC="+std::to_string(s+1)+" D052="+std::to_string(desired[s]));}else valid=false;
                            }
                        }
                        if(!valid)controlMessage="Manual synchronization stopped: deviation, feedback or output confirmation";
                    }
                    if(!valid)manualHeld=false;
                }
                if(manualClearRequested.exchange(false))manualNeedsClear=true;
                if(manualNeedsClear&&!manualHeld.load())clearManual();
                if(options.automaticMotion){
                    const auto now=MonotonicMs();
                    if(automaticStart){
                        if(!automaticCancel.load()&&!faultLatched&&!emergencyInterlock&&!pcEmergencyLatched&&!manualActive&&!manualNeedsClear){
                            if(!automatic.Begin(options,automaticStart->request,next,now))controlMessage=automatic.Reason();else{++automaticOperationId;::DHLS_StrandJack::Services::AuditLog::Write("AUTO_SEQUENCE","Automatic preparing: P.T / Stroke verified");}
                        }else controlMessage="Automatic START canceled before output";
                        automaticStart.reset();automaticPending=false;
                    }
                    if(automatic.Busy()&&(automaticCancel.load()||faultLatched||emergencyInterlock||pcEmergencyLatched)&&!automatic.Stopping())
                        automatic.Stop(emergencyInterlock||pcEmergencyLatched?"Automatic emergency input active":faultLatched?"Automatic PLC fault active":"Automatic stop requested",emergencyInterlock||pcEmergencyLatched?SequencePhase::Emergency:faultLatched?SequencePhase::Fault:SequencePhase::Stopped);
                    automatic.Build(next,now);
                    applyAutomatic();clearAutomatic();automatic.ObserveStop(next,MonotonicMs());
                    if(automaticEngaged){
                        next.sequenceNavigationLocked=automatic.HasOperation();next.automaticResetRequired=automatic.RequiresReset();next.automaticOperation=true;next.sequenceBusy=automatic.Busy()||automaticPending.load();next.sequencePhase=automatic.Phase();next.sequenceStopConfirmed=automatic.StopConfirmed();next.automaticOutputsOff=automatic.OutputsOff();
                        next.sequenceMessage=std::wstring(automatic.Reason().begin(),automatic.Reason().end());next.sequenceEventId=automatic.Event();
                        next.appliedSpeedHz=automatic.Applied().hz;next.appliedPortMasks=automatic.Applied().masks;
                        next.monitoring=automatic.Running();next.readOnly=!automatic.Busy();
                        for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].ports.size();++p){
                            auto& port=next.plcs[s].ports[p];port.sequenceTargetMm=automatic.Target(s,p);
                            port.sequenceOriginMm=automatic.Origin(s,p);port.sequencePositionValid=automatic.HasPositionTarget(s,p);
                        }
                        if(automaticEventSeen!=automatic.Event()){
                            automaticEventSeen=automatic.Event();::DHLS_StrandJack::Services::AuditLog::Write("AUTO_SEQUENCE",automatic.Reason());
                            automaticHistory.push_back({now,automaticOperationId,automatic.Phase(),0,next.sequenceMessage,{},automatic.StopConfirmed()});if(automaticHistory.size()>128)automaticHistory.erase(automaticHistory.begin());
                        }
                        next.sequenceHistory=automaticHistory;
                    }
                }
                if(request.pressureMode&&request.automatic&&automatic.PressureMode())next.pressureAuto=automatic.PressureResult();
                else if(request.pressureMode&&request.automatic){
                    auto in=PressureFeedback(options,request,next);
                    if(!pressureMonitorConfigured||pressureMonitorRequest.selected!=request.selected||pressureMonitorRequest.targetBar!=request.targetBar||pressureMonitorRevision!=optionsApplied){
                        pressureMonitor.Configure(options.pressureAuto,request.targetBar/10,options.riseWorkMm,options.riseStopMm);
                        pressureMonitorRequest=request;pressureMonitorRevision=optionsApplied;pressureMonitorConfigured=true;
                        pressureMonitor.StartRequest(in,MonotonicMs(),false,false);
                    }else pressureMonitor.Update(in,MonotonicMs());
                    next.pressureAuto=pressureMonitor.Result();
                }
                next.manualRunning=manualActive&&manualHeld.load();next.manualStopPending=manualNeedsClear&&!next.manualRunning;next.controlMessage=controlMessage;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(!manualHeld.load()&&(next.manualRunning||manualClearRequested.load())){next.manualRunning=false;next.manualStopPending=true;}
                    if(automaticPending.load()){next.automaticOperation=true;next.sequenceBusy=true;next.sequenceStopConfirmed=false;next.sequencePhase=SequencePhase::Checking;}
                    if(next.automaticOperation&&automaticCancel.load()&&next.sequencePhase==SequencePhase::Running){next.monitoring=false;next.sequenceBusy=true;next.sequencePhase=SequencePhase::Stopping;}
                    // A UI emergency accepted during this poll outranks this older sample.
                    if (published.pcEmergencyRequested && !next.pcEmergencyRequested)
                    {
                        next.pcEmergencyRequested = next.emergencyActive = true;
                        next.monitoring = false;
                        next.readOnly = false;
                        for (std::size_t s = 0; s < next.plcs.size(); ++s)
                            next.plcs[s].emergencyWrite = published.plcs[s].emergencyWrite;
                    }
                    for(std::size_t i=0;i<next.plcs.size();++i){
                        auto const& now=next.plcs[i];auto const& old=published.plcs[i];
                        if(now.connected!=old.connected||now.readOk!=old.readOk||now.usable!=old.usable||
                           now.pcSelected!=old.pcSelected||now.pumpOn!=old.pumpOn||now.error!=old.error||
                           now.emergencyBit!=old.emergencyBit||now.emergencyWrite!=old.emergencyWrite){
                            const auto emergencyCommand=now.emergencyWrite==EmergencyWriteState::ConfirmedOn?"confirmed ON":
                                now.emergencyWrite==EmergencyWriteState::Pending?"pending":
                                now.emergencyWrite==EmergencyWriteState::FailedOrUnknown?"failed or unknown":"not requested";
                            std::ostringstream event;event<<"PLC "<<i+1<<" ("<<options.plcs[i].ip<<")"
                                <<" | Link: "<<(now.connected?"connected":"disconnected")
                                <<" | Read: "<<(now.readOk?"OK":"unavailable")
                                <<" | Data: "<<(now.usable?"usable":"unavailable")
                                <<" | PC control selected: "<<(now.usable?(now.pcSelected?"yes":"no"):"unknown")
                                <<" | Pump: "<<(now.usable?(now.pumpOn?"ON":"OFF"):"unknown")
                                <<" | Fault: "<<(now.usable?(now.error?"active":"clear"):"unknown")
                                <<" | Emergency input: "<<(now.usable?(now.emergencyBit?"ON":"OFF"):"unknown")
                                <<" | Emergency command: "<<emergencyCommand;
                            ::DHLS_StrandJack::Services::AuditLog::Write("PLC_STATE",event.str());
                        }
                    }
                    next.sequence = published.sequence + 1;
                    next.configurationRevision=optionsApplied;
                    published = std::move(next);wake.notify_all();
                }
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(options.pollIntervalMs),
                              [&] { return stopping.load() || !commands.empty(); });
            }
            if(automatic.Busy()){automaticCancel=true;automatic.Stop("Automatic shutdown: output OFF verification",SequencePhase::Stopped,true);clearAutomatic();}
            manualHeld=false;if(manualClearRequested.exchange(false))manualNeedsClear=true;clearManual();
            {std::lock_guard<std::mutex> lock(mutex);published.manualRunning=false;published.manualStopPending=manualNeedsClear;}
            for (auto &reader : readers)
                if (reader)
                    reader->Disconnect();
        }
        catch (std::exception const &error)
        {
            std::lock_guard<std::mutex> lock(mutex);
            published.message = error.what();
            published.synchronization = {};
            published.pressureBalanceValid = published.pressureBalanced = false;
            published.monitoring = false;
            published.connected = false;
            published.emergencyStateVerified = false;
            published.cog.status = COGCalculator::Status::InvalidInput;
            published.cog.reason = "Disconnected or worker failure";
            for (auto &plc : published.plcs)
            {
                plc.connected = plc.readOk = plc.usable = false;
                plc.synchronization = {};
                plc.pressureDiagnosticValid = false;
                plc.diagnosticWarning = false;
                for (auto &port : plc.ports) {
                port.pressureValid = port.strokeValid = port.loadValid = false;
                port.pressureDisplayValid=port.loadDisplayValid=port.strokeDisplayValid=false;
            }
            }
            ++published.sequence;
        }
    }
};
EquipmentControlService::EquipmentControlService(ServiceOptions options, Factory factory)
    : impl_(nullptr)
{
    options.simulation = options.simulation || DeveloperSimulationEnabled.load();
    impl_ = std::make_unique<Impl>(std::move(options), std::move(factory));
}
EquipmentControlService::~EquipmentControlService()
{
    Stop();
}
void EquipmentControlService::Start()
{
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->started)
        return;
    impl_->stopping = false;
    impl_->worker = std::thread([this] { impl_->Run(); });
    impl_->started = true;
}
bool EquipmentControlService::SameTopology(ServiceOptions const& a,ServiceOptions const& b){
    if(a.simulation!=b.simulation||a.automaticMotion!=b.automaticMotion||a.timeoutMs!=b.timeoutMs||a.pollIntervalMs!=b.pollIntervalMs||a.maxSampleAgeMs!=b.maxSampleAgeMs||a.plcs.size()!=b.plcs.size())return false;
    for(std::size_t s=0;s<a.plcs.size();++s){if(a.plcs[s].ip!=b.plcs[s].ip||a.plcs[s].channels.size()!=b.plcs[s].channels.size())return false;
        for(std::size_t p=0;p<a.plcs[s].channels.size();++p)
            if(a.plcs[s].channels[p].sensor!=b.plcs[s].channels[p].sensor ||
               a.plcs[s].channels[p].pumpIp!=b.plcs[s].channels[p].pumpIp)return false;}
    return true;
}
bool EquipmentControlService::Reconfigure(ServiceOptions next){
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if(!impl_->started||impl_->stopping||!SameTopology(impl_->options,next)||impl_->pendingOptions||impl_->optionsRequested!=impl_->optionsApplied||impl_->published.sequenceBusy||impl_->published.manualRunning||impl_->published.manualStopPending)return false;
    for(std::size_t s=0;s<next.plcs.size();++s)for(std::size_t p=0;p<next.plcs[s].channels.size();++p){auto const& old=impl_->options.plcs[s].channels[p];auto const& now=next.plcs[s].channels[p];
        if(old.strokeMmPerCount>0&&(!std::isfinite(now.strokeMmPerCount)||now.strokeMmPerCount<=0))return false;}
    const auto revision=++impl_->optionsRequested;impl_->pendingOptions=std::move(next);impl_->wake.notify_all();
    if(!impl_->wake.wait_for(lock,std::chrono::seconds(10),[&]{return (impl_->optionsApplied>=revision&&!impl_->optionsAccepted)||impl_->published.configurationRevision>=revision||impl_->stopping.load();}))return false;
    return impl_->optionsApplied==revision&&impl_->optionsAccepted;
}
void EquipmentControlService::Stop()
{
    std::lock_guard<std::mutex> lifecycle(impl_->lifecycle);
    if(impl_->manualHeld.exchange(false))impl_->manualClearRequested=true;
    impl_->automaticCancel=true;
    impl_->stopping = true;
    impl_->wake.notify_all();
    if (impl_->worker.joinable())
        impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->published.synchronization = {};
    impl_->published.pressureBalanceValid = impl_->published.pressureBalanced = false;
    impl_->published.monitoring = false;
    impl_->published.connected = false;
    impl_->published.emergencyStateVerified = false;
    impl_->published.cog.status = COGCalculator::Status::InvalidInput;
    impl_->published.cog.reason = "Disconnected";
    for (auto &plc : impl_->published.plcs)
    {
        plc.connected = plc.readOk = plc.usable = false;
        plc.synchronization = {};
        plc.pressureDiagnosticValid = false;
        plc.diagnosticWarning = false;
        for (auto &port : plc.ports) {
                port.pressureValid = port.strokeValid = port.loadValid = false;
                port.pressureDisplayValid=port.loadDisplayValid=port.strokeDisplayValid=false;
            }
    }
}
void EquipmentControlService::SetPlcMapAreas(bool useP,bool useM){
    const unsigned mask=(useP?1u:0u)|(useM?2u:0u);
    if(impl_->mapAreas.exchange(mask)!=mask)++impl_->mapGeneration;impl_->wake.notify_one();
}
void EquipmentControlService::SetPlcMapMonitoring(bool enabled){
    impl_->mapMonitoring=enabled;if(enabled)++impl_->mapGeneration;impl_->wake.notify_one();
}
bool EquipmentControlService::IsLiveMonitorOnly() const noexcept
{
    return impl_->liveMonitorOnly;
}

bool EquipmentControlService::Submit(std::string action, DiagnosticRequest const &request)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->started || impl_->stopping)
        return false;
    if (IsLiveMonitorOnly())
    {
        // Allow local display operations only. Unknown actions fail closed before queue/state changes.
        const bool displayOnly = action == "HeightZero" || action == "PressureZero" ||
            action == "LoadSet" || action == "TargetChanged" || action == "Back" ||
            action == "ModeAuto" || action == "ModeManual" || action == "ModeDisplacement" ||
            action == "ModePressure" || action == "Locked" || action == "Unlocked";
        if (!displayOnly) return false;
    }
    if(action=="StopRequested"||action=="EmergencyRequested"||action=="Locked"){
        if(impl_->manualHeld.exchange(false)||impl_->published.manualRunning||impl_->published.manualStopPending){impl_->manualClearRequested=true;impl_->published.manualRunning=false;impl_->published.manualStopPending=true;}
    }
    if(!impl_->options.simulation&&impl_->options.automaticMotion){
        const bool cancel=action=="StopRequested"||action=="FullStopRequested"||action=="EmergencyRequested"||action=="Locked";
        if(cancel){impl_->automaticCancel=true;
            if(impl_->automaticPending.load()||(impl_->published.automaticOperation&&impl_->published.sequenceBusy)){impl_->published.monitoring=false;impl_->published.sequenceBusy=true;impl_->published.sequenceStopConfirmed=false;impl_->published.sequencePhase=SequencePhase::Stopping;}
        }
        const bool busy=impl_->automaticPending.load()||(impl_->published.automaticOperation&&impl_->published.sequenceBusy);
        const bool speedAction=action.rfind("LiftingSpeed:",0)==0||action.rfind("LoweringSpeed:",0)==0;
        if(busy&&!cancel&&!speedAction&&action!="ResetSequence")return false;
        if(action=="StartRequested"&&impl_->published.automaticResetRequired)return false;
        if(action=="StartRequested"&&request.automatic){
            if(request.locked||impl_->published.emergencyActive||impl_->published.faultActive)return false;
            if(impl_->published.automaticOperation&&(impl_->published.sequencePhase==SequencePhase::Fault||impl_->published.sequencePhase==SequencePhase::Emergency))return false;
            impl_->automaticCancel=false;
        }
    }
    const bool modeChange=action=="ModeAuto"||action=="ModeManual"||action=="ModePressure"||action=="ModeDisplacement";
    if(modeChange&&(request.locked||impl_->published.sequenceBusy||impl_->published.monitoring||impl_->published.manualRunning||impl_->published.manualStopPending||impl_->manualHeld.load()||impl_->manualClearRequested.load()))return false;
    const bool transition=action=="Back"||action.rfind("Arrangement",0)==0;
    if(transition&&(request.locked||impl_->published.sequenceBusy||impl_->published.monitoring||impl_->published.manualRunning||impl_->published.manualStopPending||impl_->manualHeld.load()||impl_->manualClearRequested.load()))return false;
    if(transition&&impl_->published.sequenceNavigationLocked&&!(action=="Back"&&!request.automatic))return false;
    if(action=="StartRequested"&&impl_->published.loadReferenceTripped)return false;
    if(action=="StartRequested"&&!impl_->options.simulation&&(impl_->manualHeld.load()||impl_->published.manualRunning||impl_->published.manualStopPending||impl_->manualClearRequested.load()))return false;
    if(action=="StartRequested"&&request.automatic&&!impl_->options.automaticScalesMatch)return false;
    const bool control=action=="UpRequested"||action=="DownRequested"||action.rfind("PumpToggle:",0)==0||action.rfind("LiftingSpeed:",0)==0||action.rfind("LoweringSpeed:",0)==0;
    const bool speed=action.rfind("LiftingSpeed:",0)==0||action.rfind("LoweringSpeed:",0)==0;
    if(control&&(impl_->manualHeld.load()||impl_->published.manualRunning||impl_->published.manualStopPending||impl_->manualClearRequested.load()))return false;
    if(control && (request.locked||(!speed&&impl_->published.monitoring)||(impl_->published.sequenceBusy&&!(speed&&impl_->published.automaticOperation&&impl_->published.sequencePhase==SequencePhase::Running))||impl_->published.emergencyActive)) return false;
    if((control||action=="StartRequested")&&impl_->published.faultActive)return false;
    if(control&&!impl_->options.simulation){
        int target=-1;
        if(action.rfind("PumpToggle:",0)==0||speed){try{target=std::stoi(action.substr(action.find(':')+1))-1;}catch(...){return false;}}
        if(target>=static_cast<int>(impl_->published.plcs.size()))return false;
        const auto now=MonotonicMs();
        for(std::size_t i=0;i<impl_->published.plcs.size();++i){
            if(target>=0&&static_cast<int>(i)!=target)continue;
            auto const& plc=impl_->published.plcs[i];
            if(!plc.connected||!plc.readOk||!plc.pcSelected||!plc.sampledAtMs||now-plc.sampledAtMs>static_cast<unsigned>(impl_->options.maxSampleAgeMs))return false;
        }
    }
    if(action=="StartRequested"&&!request.automatic&&!impl_->options.simulation){
        if(request.locked||impl_->manualHeld.load()||impl_->published.monitoring||impl_->published.manualStopPending||impl_->published.emergencyActive)return false;
        bool any=false;const auto now=MonotonicMs();
        for(std::size_t s=0;s<impl_->options.plcs.size();++s){auto const& plc=impl_->published.plcs[s];
            if(!plc.connected||!plc.readOk||!plc.pcSelected||!plc.sampledAtMs||now-plc.sampledAtMs>static_cast<unsigned>(impl_->options.maxSampleAgeMs))return false;
            for(std::size_t p=0;p<8;++p)if(request.selected[s][p]){if(p>=impl_->options.plcs[s].channels.size())return false;any=true;}
        }
        if(!any||impl_->commands.size()>=64)return false;
        impl_->manualHeld=true;
    }
    if(speed){
        // Coalesce only unsent updates for this PLC/direction; never retry a transmitted command.
        const auto separator=action.find(':',action.find(':')+1);
        if(separator==std::string::npos)return false;
        const auto prefix=action.substr(0,separator+1);
        impl_->commands.erase(std::remove_if(impl_->commands.begin(),impl_->commands.end(),[&](Impl::Command const& c){return c.action.rfind(prefix,0)==0;}),impl_->commands.end());
    }
    if (action == "EmergencyRequested")
        impl_->commands.clear();
    else if (action == "StopRequested" || ((impl_->options.simulation||impl_->options.automaticMotion)&&action=="FullStopRequested"))
        impl_->commands.erase(std::remove_if(impl_->commands.begin(), impl_->commands.end(), [this](Impl::Command const& command) {
            return command.action != "EmergencyRequested" && !(impl_->options.simulation&&(command.action=="StopRequested"||command.action=="FullStopRequested"));
        }), impl_->commands.end());
    else if (impl_->commands.size() >= 64)
        return false;
    if (action == "EmergencyRequested")
    {
        impl_->published.pcEmergencyRequested = impl_->published.emergencyActive = true;
        impl_->published.readOnly = impl_->options.simulation;
        impl_->published.monitoring = false;
        for (auto &plc : impl_->published.plcs) plc.emergencyWrite = EmergencyWriteState::Pending;
    }
    if(!impl_->options.simulation&&impl_->options.automaticMotion&&action=="StartRequested"&&request.automatic){
        impl_->automaticPending=true;impl_->published.automaticOperation=true;impl_->published.sequenceBusy=true;impl_->published.sequenceStopConfirmed=false;impl_->published.sequencePhase=SequencePhase::Checking;
    }
    if(impl_->options.simulation&&(action=="EmergencyRequested"||action=="StartRequested")){
        impl_->published.sequenceBusy=true;
        impl_->published.sequenceStopConfirmed=false;
        impl_->published.sequencePhase=action=="EmergencyRequested"?SequencePhase::Emergency:SequencePhase::Checking;
    }
    if(impl_->options.simulation&&(action=="StopRequested"||action=="FullStopRequested")&&
       std::any_of(impl_->commands.begin(),impl_->commands.end(),[&](Impl::Command const& c){return c.action==action;}))
        return true; // Coalesce duplicates without losing an explicit full stop.
    std::ostringstream audit;
    audit<<action<<" | Mode: "<<(request.automatic?"automatic":"manual")
         <<" | Control: "<<(request.pressureMode?"pressure":"distance")
         <<" | Direction: "<<(request.lowering?"lowering":"lifting")
         <<" | Target: ";
    if(request.pressureMode)audit<<request.targetBar<<" bar";
    else audit<<request.targetMm<<" mm";
    audit<<" | Selected jacks: ";
    bool selected=false;
    for(std::size_t s=0;s<4;++s)for(std::size_t p=0;p<8;++p)if(request.selected[s][p]){
        if(selected)audit<<", ";
        audit<<"SET "<<s+1<<"/PORT "<<p+1;
        selected=true;
    }
    if(!selected)audit<<"none";
    ::DHLS_StrandJack::Services::AuditLog::Write(impl_->options.simulation?"SIM_COMMAND":"COMMAND",audit.str());
    impl_->commands.push_back({std::move(action), request});
    impl_->wake.notify_one();
    return true;
}
EquipmentSnapshot EquipmentControlService::Latest() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto copy = impl_->published;
    const auto now = MonotonicMs();
    bool any = false;
    for (auto &plc : copy.plcs)
    {
        for(auto& word:plc.mapWords)if(!word.sampledAtMs||word.sampledAtMs>now||now-word.sampledAtMs>static_cast<unsigned>(impl_->options.maxSampleAgeMs))word.valid=false;
        if (!plc.sampledAtMs || now - plc.sampledAtMs > static_cast<unsigned>(impl_->options.maxSampleAgeMs))
        {
            plc.usable = false;
            plc.connected = false;
            copy.synchronization = {};
            copy.pressureBalanceValid = copy.pressureBalanced = false;
            copy.monitoring = false;
            if(copy.faultActive){copy.faultStatesKnown=false;for(auto& bit:copy.faultBits)if(bit.plc==static_cast<int>(&plc-copy.plcs.data()))bit.confirmed=false;}
            copy.emergencyStateVerified = false;
            plc.synchronization.dataValid = false;
            plc.pressureDiagnosticValid = false;
            for (auto &port : plc.ports) {
                port.pressureValid = port.strokeValid = port.loadValid = false;
                port.pressureDisplayValid=port.loadDisplayValid=port.strokeDisplayValid=false;
            }
            copy.cog.status = COGCalculator::Status::InvalidInput;
            copy.cog.reason = "Stale PLC data";
        }
        any |= plc.connected && plc.readOk;
    }
    copy.connected = any;
    if (!copy.simulation)
    {
        for (size_t i = 0; i < copy.plcs.size() && i < impl_->options.plcs.size(); ++i)
        {
            const auto& plc = copy.plcs[i];
            const auto& ip = impl_->options.plcs[i].ip;
            if (!ip.empty() && plc.connected && plc.readOk && plc.usable)
                copy.pumpByIp[ip] = plc.pumpOn;
        }
    }
    else if (!any) copy.pumpByIp.clear();
    return copy;
}

std::string EquipmentControlService::PumpIpForPort(Models::SystemConfig const& model, int plc, int port)
{
    const auto& settings = model.PLCSettings;
    if (plc < 0 || plc >= settings.PLC_Count || port < 0 || port >= settings.Sensor_HPU_Count) return {};
    const auto index = static_cast<size_t>(plc) * settings.Sensor_HPU_Count + port;
    std::wstring address;
    if (index < model.JackSetupDetails.size()) address = model.JackSetupDetails[index].IPAddress;
    if (address.empty() && static_cast<size_t>(plc) < settings.PLC_IPs.size()) address = settings.PLC_IPs[plc];
    if (address.empty() || address.find_first_not_of(L"0123456789.") != std::wstring::npos) return {};
    return {address.begin(), address.end()};
}
ServiceOptions EquipmentControlService::FromModel(Models::SystemConfig const &model)
{
    ServiceOptions options;
    options.automaticMotion=true;
    options.decelerationStartMm=model.ControlSettings.DistanceDecelerationStartMm;
    options.minimumSpeedPercent=model.ControlSettings.DistanceDecelerationMinimumPercent;
    options.positionToleranceMm=model.ControlSettings.DistanceDecelerationToleranceMm;
    options.automaticScalesMatch=Models::JackMeasurementRangesMatch(model);
    options.pressureSpreadToleranceBar = model.PLCSettings.PressureBalanceTolerance_bar;
    options.manualSync = model.ControlSettings.ManualSync != 0;
    options.strokeFilterSamples=std::isfinite(model.ControlSettings.Filtering)?static_cast<int>(std::clamp(std::round(model.ControlSettings.Filtering),1.0,1000.0)):1;
    options.pressureAuto=model.ControlSettings.PressureAuto;
    options.maximumStopPressureBar = model.ControlSettings.MaxStop;
    options.minimumLoweringPressureBar = model.ControlSettings.FallStop;
    options.displayLoadZeroBar = std::isfinite(model.SensorSettings.LoadMinZero_bar)&&model.SensorSettings.LoadMinZero_bar>0?model.SensorSettings.LoadMinZero_bar:0;
    options.loadTolerancePercent = model.ControlSettings.WeightDeviation;
    options.loadToleranceEnabled=model.ControlSettings.LoadToleranceEnabled;
    options.riseWorkMm = model.ControlSettings.RiseSync_Level1;
    options.riseStopMm = model.ControlSettings.RiseSync_Level2;
    options.fallWorkMm = model.ControlSettings.FallSync_Level1;
    options.fallStopMm = model.ControlSettings.FallSync_Level2;
    options.mockSyncMm = model.ControlSettings.SyncDeviationMm;
    options.mockTripMm = model.ControlSettings.AllowedDeviationMm;
    if (model.PLCSettings.PLC_Count < 1 || model.PLCSettings.PLC_Count > 4 ||
        model.PLCSettings.Sensor_HPU_Count < 1 || model.PLCSettings.Sensor_HPU_Count > 8)
        throw std::invalid_argument("Unsupported PLC/sensor count");
    const int count = model.PLCSettings.PLC_Count, ports = model.PLCSettings.Sensor_HPU_Count;
    for (int s = 0; s < count; ++s)
    {
        PlcConfig plc;
        if (s < static_cast<int>(model.PLCSettings.PLC_IPs.size()))
        {
            auto const &ip = model.PLCSettings.PLC_IPs[s];
            for (wchar_t c : ip)
            {
                if (c > 127 || c < 0)
                    throw std::invalid_argument("PLC IPv4 address must be ASCII");
                plc.ip.push_back(static_cast<char>(c));
            }
        }
        for (int p = 0; p < ports; ++p)
        {
            ChannelConfig channel;
            channel.pumpIp = PumpIpForPort(model, s, p);
            channel.sensor = p;
            auto index = static_cast<std::size_t>(s * ports + p);
            channel.scalingVerified = model.PLCSettings.SensorScalingVerified;
            channel.areaVerified = model.PLCSettings.EffectiveAreaVerified;
            channel.geometryVerified = model.PLCSettings.CoordinatesVerified;
            channel.pressureSigned = model.PLCSettings.PressureSigned16;
            channel.strokeSigned = model.PLCSettings.StrokeSigned16;
            if (index < model.JackSetupDetails.size())
            {
                auto const &detail = model.JackSetupDetails[index];
                channel.sensor = detail.Port - 1;
                channel.maximumLoadTon = detail.MaxWeight_ton;
                channel.workingLoadTon = detail.WorkingWeight_ton;
                channel.xMm = detail.X_mm;
                channel.yMm = detail.Y_mm;
                for (auto const &spec : model.JackSettings.Specs)
                    if (Models::JackTypeLabel(spec) == detail.JackSelection)
                    {
                        const double pressureAd=spec.PressureSensor_ADScale>0?spec.PressureSensor_ADScale:model.SensorSettings.PressureSensor_ADScale;
                        const double strokeAd=spec.DisplacementSensor_ADScale>0?spec.DisplacementSensor_ADScale:model.SensorSettings.DisplacementSensor_ADScale;
                        channel.pressureBarPerCount=pressureAd>0?(spec.PressureSensorScale_bar>0?spec.PressureSensorScale_bar:spec.WorkingPressure_bar)/pressureAd:0;
                        const double strokeRange=spec.JackStroke_mm>0?spec.JackStroke_mm:spec.DisplacementSensorScale_mm;
                        channel.strokeMmPerCount=strokeAd>0?strokeRange/strokeAd:0;
                        if(model.ControlSettings.PlcAdResolution>0)
                            channel.plcStrokeDisplayMmPerCount=strokeRange/model.ControlSettings.PlcAdResolution;
                        channel.effectiveAreaMm2 =
                            3.14159265358979323846 * spec.D3_Diameter_mm * spec.D3_Diameter_mm / 4;
                        channel.maximumPressureBar = spec.WorkingPressure_bar;
                        channel.strokeLimitMm = spec.JackStroke_mm;
                        break;
                    }
            }
            plc.channels.push_back(channel);
        }
        options.plcs.push_back(std::move(plc));
    }
    return options;
}
} // namespace DHLS_StrandJack::Control
