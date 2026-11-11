#pragma once
#include "../SystemConfigModels.h"
#include "Evo/COGCalculator.h"
#include "Evo/DRegionDiagnostics.h"
#include <array>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace DHLS_StrandJack::Control
{
struct ChannelConfig
{
    int sensor = 0;
    double pressureBarPerCount = 0, strokeMmPerCount = 0;
    double effectiveAreaMm2 = 0, maximumPressureBar = 0;
    double xMm = 0, yMm = 0;
    double strokeLimitMm = 0, maximumLoadTon = 0, workingLoadTon = 0;
    bool scalingVerified = false, areaVerified = false, geometryVerified = false;
    bool pressureSigned = false, strokeSigned = false;
    std::string pumpIp;
    double plcStrokeDisplayMmPerCount=0;
    double StrokeDisplayScale() const noexcept {
        return strokeMmPerCount>0?strokeMmPerCount:plcStrokeDisplayMmPerCount;
    }
};
struct PlcConfig
{
    std::string ip;
    std::vector<ChannelConfig> channels;
};
struct ServiceOptions
{
    PressureAutoSettings pressureAuto;
    bool simulation = false;
    bool automaticScalesMatch = true;
    bool automaticMotion = false; // FromModel enables the commissioned D050/D052 position path.
    std::vector<PlcConfig> plcs;
    int pollIntervalMs = 200, timeoutMs = 400, maxSampleAgeMs = 1000;
    double riseWorkMm = 0, riseStopMm = 0, fallWorkMm = 0, fallStopMm = 0;
    double mockSyncMm=10, mockTripMm=20;
    double pressureSpreadToleranceBar = 0;
    bool manualSync = false;
    bool loadToleranceEnabled=false;
    int strokeFilterSamples = 1;
    double decelerationStartMm=20, minimumSpeedPercent=25, positionToleranceMm=0.5;
    double maximumStopPressureBar = 0, minimumLoweringPressureBar = 0, loadTolerancePercent = 0, displayLoadZeroBar = 0;
};
struct DiagnosticRequest
{
    std::array<double,4> liftingHz{{20,20,20,20}}, loweringHz{{20,20,20,20}};
    std::array<std::array<bool, 8>, 4> selected{};
    bool pressureMode = false, lowering = false, locked = false, automatic = true;
    double targetMm = 50, targetBar = 50;
    double mockJobGoalMm=0;
    bool mockStrandJob=false;
    bool mockPretension=false;
    double pretensionLoadPercent=0;
};
struct PortSample
{
    unsigned rawPressure = 0, rawStroke = 0;
    // Configured engineering display; does not certify a sensor for control/COG.
    bool pressureDisplayValid=false, loadDisplayValid=false, strokeDisplayValid=false;
    double displayPressureBar=0, displayLoadTon=0;
    double displayAbsoluteMm=0, displayRelativeMm=0; // HEIGHT ZERO display reference, independent of diagnostic origins.
    bool pressureFault = false, strokeFault = false;
    bool pressureValid = false, strokeValid = false, loadValid = false;
    double pressureBar = 0, absoluteMm = 0, relativeMm = 0, loadTon = 0;
    double mockStrokeZeroMm=0;
    double sequenceTargetMm = 0, sequenceOriginMm=0;
    bool sequencePositionValid=false;
    bool mockClampsValid=false, mockUpperOpen=false, mockLowerOpen=false;
    double referenceLoadTon=0;
};
enum class EmergencyWriteState { NotRequested, Pending, ConfirmedOn, FailedOrUnknown };
struct PlcMapWord { std::uint16_t address=0,value=0; bool valid=false; std::uint64_t sampledAtMs=0; char family='D'; };
struct PlcSample
{
    EmergencyWriteState emergencyWrite = EmergencyWriteState::NotRequested;
    bool connected = false, readOk = false, usable = false, pumpOn = false, error = false;
    bool pcSelected = false, emergencyBit = false;
    std::uint64_t sampledAtMs = 0;
    std::vector<PortSample> ports;
    std::vector<PlcMapWord> mapWords;
    SynchronizationAlgorithm::Result synchronization;
    bool pressureDiagnosticValid = false, diagnosticWarning = false;
    std::string diagnostic;
};
enum class SequencePhase { Unavailable=-1, Idle, Checking, Preparing, Running, Stopping, Completed, Stopped, Fault, Emergency };
struct SequenceRecord {
    std::uint64_t atMs=0,operationId=0;
    SequencePhase phase=SequencePhase::Unavailable;
    int mode=0;
    std::wstring message;
    std::vector<int> selectedJackIds;
    bool stopConfirmed=false;
};
struct PlcFaultBit { int plc=0,word=0,bit=0; bool confirmed=false; };
struct EquipmentSnapshot
{
    std::map<std::string, bool> pumpByIp;
    PressureAutoResult pressureAuto;
    bool faultActive=false,faultStatesKnown=false,faultStopConfirmed=false;
    std::vector<PlcFaultBit> faultBits;
    std::vector<bool> faultStopOff;
    std::uint64_t sequence = 0,configurationRevision=0;
    bool connected = false, monitoring = false, readOnly = true;
    bool simulation = false, sequenceBusy = false, sequenceStopConfirmed = false;
    bool mockJobReady=false, mockJobValid=false, mockJobRunning=false, mockJobFinished=false;
    bool mockJobChangesLocked=false;
    bool mockJobPaused=false;
    double mockJobTravelMm=0, mockJobGoalMm=0;
    double mockJobStrokeBaseMm=0, mockJobStrokeSpanMm=0;
    bool manualRunning=false, manualStopPending=false;
    bool loadReferenceTripped=false;
    std::string loadReferenceMessage;
    bool automaticOperation=false, automaticOutputsOff=true, automaticResetRequired=false;
    bool sequenceNavigationLocked=false;
    std::array<std::uint16_t,4> appliedSpeedHz{}, appliedPortMasks{};
    SequencePhase sequencePhase = SequencePhase::Unavailable;
    std::wstring sequenceMessage;
    std::uint64_t sequenceEventId = 0;
    std::vector<SequenceRecord> sequenceHistory;
    bool emergencyActive = false, emergencyStateVerified = false;
    bool pcEmergencyRequested = false;
    std::string controlMessage; // Result of the latest explicit operator D51 command.
    std::vector<PlcSample> plcs;
    COGCalculator::Result cog;
    SynchronizationAlgorithm::Result synchronization;
    double pressureSpreadBar = 0;
    bool pressureBalanceValid = false;
    bool pressureBalanced = false;
    bool diagnosticWarning = false;
    std::string message;
};
class IPlcReadTransport : public DRegionDiagnostics::IReadOnlyDWords
{
  public:
    virtual bool ReadDeviceWords(char, std::vector<std::uint16_t> const&, std::vector<std::uint16_t>& out) { out.clear(); return false; }
    virtual bool Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool StopOutputs() { return SetControlBits(0x000E,0); } // OFF only, preserves emergency/heartbeat bits.
    virtual bool SetPortMask(std::uint16_t, std::function<bool()> const& = {}) { return false; }
    virtual bool SetSpeed(std::uint16_t) { return false; }
    virtual bool SetAutomaticSpeed(std::uint16_t value,std::function<bool()> const& enabled) { return (!enabled||enabled())&&SetSpeed(value); }
    virtual bool SetControlBits(std::uint16_t, std::uint16_t) { return false; }
    virtual bool SetEmergencyOn() { return false; } // Narrow opt-in: D51.0 ON only.
};
// Explicit operator commands are serialized on the worker: D51.0 emergency and D51.1/.2/.3.
class EquipmentControlService final
{
  public:
    using Factory = std::function<std::unique_ptr<IPlcReadTransport>(PlcConfig const &, int)>;
    explicit EquipmentControlService(ServiceOptions options, Factory factory = {});
    ~EquipmentControlService();
    EquipmentControlService(EquipmentControlService const &) = delete;
    EquipmentControlService &operator=(EquipmentControlService const &) = delete;
    void SetPlcMapAreas(bool useP, bool useM); // Read-only P/M diagnostics, never PLC output enable.
    void SetPlcMapMonitoring(bool enabled); // Additional read-only words while the diagnostic window is open.
    void Start(); // thread creation only, no caller-thread I/O
    void Stop();  // blocking: use background thread from WinUI
    bool Reconfigure(ServiceOptions options); // same PLC/port topology; worker applies scales without clearing runtime state
    static bool SameTopology(ServiceOptions const& a,ServiceOptions const& b);
    bool Submit(std::string action, DiagnosticRequest const &request);
    bool IsLiveMonitorOnly() const noexcept;
    EquipmentSnapshot Latest() const; // immutable native value copy
    static ServiceOptions FromModel(Models::SystemConfig const &model);
    static std::string PumpIpForPort(Models::SystemConfig const& model, int plc, int port);
    static std::uint64_t MonotonicMs();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace DHLS_StrandJack::Control
