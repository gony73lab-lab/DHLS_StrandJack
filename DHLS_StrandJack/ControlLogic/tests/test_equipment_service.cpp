#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#include "../EquipmentControlService.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace DHLS_StrandJack::Control;
struct Shared
{
    std::atomic<int> calls{0}, inflight{0}, maximum{0};
    std::atomic<bool> fail{false}, emergency{false}, sensorReadFail{false};
    std::atomic<int> emergencyWrites{0};
    std::atomic<bool> secondaryEmergencyFail{false}, pcEmergencyInput{false};
    std::atomic<unsigned> pressure{20}, stroke{100}, secondPressureDelta{0}, secondStrokeDelta{0};
};
class Reader final : public IPlcReadTransport
{
    std::shared_ptr<Shared> shared;
    bool secondary = false;

  public:
    explicit Reader(std::shared_ptr<Shared> state, bool second = false)
        : shared(std::move(state)), secondary(second)
    {
    }
    bool SetEmergencyOn() override { ++shared->emergencyWrites; return !shared->fail && !(secondary && shared->secondaryEmergencyFail); }
    bool Connect() override
    {
        return !shared->fail;
    }
    void Disconnect() override
    {
    }
    bool ReadWords(std::uint16_t first, std::size_t count, std::vector<std::uint16_t> &out) override
    {
        auto n = ++shared->inflight;
        if (n > shared->maximum)
            shared->maximum = n;
        ++shared->calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        out.assign(count, 0);
        if (first == 10)
            out[0] = 6 | (shared->emergency ? 1 : 0);
        if (first == 51) out[0] = shared->pcEmergencyInput ? 1 : 0;
        if (first == 11)
            for (std::size_t i = 0; i < 8; ++i)
            {
                out[2 * i] = static_cast<std::uint16_t>(shared->pressure.load() +
                                                        (secondary ? shared->secondPressureDelta.load() : 0));
                out[2 * i + 1] = static_cast<std::uint16_t>(
                    shared->stroke.load() + (secondary ? shared->secondStrokeDelta.load() : 0));
            }
        --shared->inflight;
        return !shared->fail && !(first != 10 && shared->sensorReadFail);
    }
};
template <class F> void Until(int line,F predicate)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() > end)
            { std::cerr << "test timeout at line " << line << std::endl; throw std::runtime_error("test timeout"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
ServiceOptions Options(bool verified)
{
    ServiceOptions o;
    o.pollIntervalMs = 20;
    // Two PLCs perform several Sleep(2 ms) reads; Windows can round each sleep to a timer tick.
    // Dedicated stale/deadline tests validate rejection separately.
    o.maxSampleAgeMs = 500;
    o.riseWorkMm = o.fallWorkMm = 2;
    o.riseStopMm = o.fallStopMm = 6;
    o.pressureSpreadToleranceBar = 5;
    PlcConfig p;
    p.ip = "127.0.0.1";
    for (int i = 0; i < 4; ++i)
    {
        ChannelConfig c;
        c.sensor = i;
        c.pressureBarPerCount = c.strokeMmPerCount = 1;
        c.effectiveAreaMm2 = 1000;
        c.maximumPressureBar = 700;
        c.scalingVerified = c.areaVerified = c.geometryVerified = verified;
        c.xMm = (i == 1 || i == 2) ? 100 : 0;
        c.yMm = i >= 2 ? 100 : 0;
        p.channels.push_back(c);
    }
    o.plcs.push_back(p);
    return o;
}
int main()
{
#ifdef _MSC_VER
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
 _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif

    auto shared = std::make_shared<Shared>();
    auto factory = [shared](PlcConfig const &, int) { return std::make_unique<Reader>(shared); };
    {
        EquipmentControlService service(Options(false), factory);
        service.Start();
        Until(__LINE__,[&] { return service.Latest().plcs[0].readOk; });
        auto s = service.Latest();
        assert(s.plcs[0].ports[0].rawPressure == 20);
        assert(!s.plcs[0].ports[0].pressureValid);
        assert(!s.cog.valid());
        service.Stop();
    }
    {
        DHLS_StrandJack::Models::SystemConfig m;m.PLCSettings.PLC_Count=1;m.PLCSettings.Sensor_HPU_Count=1;
        m.PLCSettings.PLC_IPs={L"127.0.0.1"};m.SensorSettings.PressureSensor_ADScale=16000;
        m.SensorSettings.DisplacementSensor_ADScale=16000;m.ControlSettings.MaxWeightUsageRange_Percent=60;
        DHLS_StrandJack::Models::JackSpec spec;spec.Capacity_ton=50;spec.D3_Diameter_mm=100;
        spec.WorkingPressure_bar=700;spec.PressureSensorScale_bar=700;spec.DisplacementSensorScale_mm=500;spec.JackStroke_mm=500;
        m.JackSettings.Specs={spec};m.InitializeJackDetails();m.JackSetupDetails[0].JackSelection=L"50 ton";m.JackSetupDetails[0].MaxWeight_ton=214;m.JackSetupDetails[0].WorkingWeight_ton=171;
        auto o=EquipmentControlService::FromModel(m);o.automaticMotion=false;o.pollIntervalMs=20;
        const double area=3.14159265358979323846*100*100/4;
        assert(std::abs(o.plcs[0].channels[0].maximumLoadTon-214)<1e-9);
        assert(std::abs(o.plcs[0].channels[0].workingLoadTon-171)<1e-9);
        shared->pressure=8000;shared->stroke=16000;
        EquipmentControlService scaling(o,factory);scaling.Start();
        Until(__LINE__,[&]{return scaling.Latest().plcs[0].ports[0].loadDisplayValid;});
        auto port=scaling.Latest().plcs[0].ports[0];assert(std::abs(port.displayPressureBar-350)<1e-9);
        assert(std::abs(port.displayLoadTon-350*area/98066.5)<1e-9);
        assert(!port.pressureValid&&!port.loadValid); // Display does not bypass commissioning.
        assert(port.strokeDisplayValid&&!port.strokeValid);assert(port.displayAbsoluteMm==500&&port.displayRelativeMm==500);
        shared->stroke=8000;Until(__LINE__,[&]{return scaling.Latest().plcs[0].ports[0].displayAbsoluteMm==250;});
        DiagnosticRequest zero;zero.selected[0][0]=true;assert(scaling.Submit("HeightZero",zero));
        Until(__LINE__,[&]{auto p=scaling.Latest().plcs[0].ports[0];return p.displayRelativeMm==0&&p.displayAbsoluteMm==250;});
        shared->stroke=12000;Until(__LINE__,[&]{auto p=scaling.Latest().plcs[0].ports[0];return p.displayAbsoluteMm==375&&p.displayRelativeMm==125;});
        // START may change diagnostic origins, but cannot change the HEIGHT ZERO display reference.
        assert(scaling.Submit("StartRequested",zero));
        Until(__LINE__,[&]{return scaling.Latest().message.find("calibrated")!=std::string::npos;});
        assert(scaling.Latest().plcs[0].ports[0].displayRelativeMm==125);
        assert(scaling.Submit("HeightZero",zero));Until(__LINE__,[&]{return scaling.Latest().plcs[0].ports[0].displayRelativeMm==0;});
        shared->stroke=8000;Until(__LINE__,[&]{auto p=scaling.Latest().plcs[0].ports[0];return p.displayAbsoluteMm==250&&p.displayRelativeMm==-125;});
        assert(shared->emergencyWrites==0);

        auto rescaled=o;rescaled.plcs[0].channels[0].strokeMmPerCount=1000.0/16000;rescaled.plcs[0].channels[0].strokeLimitMm=1000;
        assert(scaling.Reconfigure(rescaled));
        {auto v=scaling.Latest().plcs[0].ports[0];assert(v.rawStroke==8000&&v.displayAbsoluteMm==500&&v.displayRelativeMm==-250);}
        shared->pressure=16000;Until(__LINE__,[&]{return scaling.Latest().plcs[0].ports[0].displayPressureBar==700;});
        shared->pressure=0;Until(__LINE__,[&]{return scaling.Latest().plcs[0].ports[0].displayPressureBar==0;});
        shared->sensorReadFail=true;Until(__LINE__,[&]{return !scaling.Latest().plcs[0].ports[0].pressureDisplayValid;});
        assert(!scaling.Latest().plcs[0].ports[0].strokeDisplayValid);
        scaling.Stop();shared->sensorReadFail=false;shared->pressure=8000;shared->stroke=100;
        m.SensorSettings.LoadMinZero_bar=400;
        m.PLCSettings.SensorScalingVerified=m.PLCSettings.EffectiveAreaVerified=true;
        auto deadband=EquipmentControlService::FromModel(m);deadband.automaticMotion=false;deadband.pollIntervalMs=20;
        assert(deadband.displayLoadZeroBar==400);
        EquipmentControlService loadZero(deadband,factory);loadZero.Start();
        Until(__LINE__,[&]{auto p=loadZero.Latest().plcs[0].ports[0];return p.loadDisplayValid&&p.loadValid;});
        auto low=loadZero.Latest().plcs[0].ports[0];
        assert(low.displayPressureBar==350&&low.displayLoadTon==0&&low.loadTon>0);
        shared->pressure=10000;
        Until(__LINE__,[&]{return loadZero.Latest().plcs[0].ports[0].displayLoadTon>0;});
        assert(loadZero.Latest().plcs[0].ports[0].displayPressureBar>400);
        loadZero.Stop();shared->pressure=20;
        m.JackSettings.Specs[0].PressureSensorScale_bar=0;
        assert(std::abs(EquipmentControlService::FromModel(m).plcs[0].channels[0].pressureBarPerCount-700.0/16000)<1e-12);
        m.JackSettings.Specs[0].DisplacementSensorScale_mm=1000;
        assert(EquipmentControlService::FromModel(m).plcs[0].channels[0].strokeMmPerCount==500.0/16000); // Jack stroke is full travel.
        m.JackSettings.Specs[0].JackStroke_mm=0;
        assert(EquipmentControlService::FromModel(m).plcs[0].channels[0].strokeMmPerCount==1000.0/16000); // Legacy range fallback.

    }
    shared->calls = 0;
    EquipmentControlService service(Options(true), factory);
    service.Start();
    Until(__LINE__,[&] { return service.Latest().plcs[0].ports[0].loadValid; });
    DiagnosticRequest request;
    for (int p = 0; p < 4; ++p)
        request.selected[0][p] = true;
    request.targetMm = 50;
    request.targetBar = 30;
    assert(service.Submit("SelectionUpdated", request));
    Until(__LINE__,[&] { return service.Latest().cog.valid(); });
    auto s = service.Latest();
    assert(std::abs(s.cog.cogXMm - 50) < 1e-9 && std::abs(s.cog.cogYMm - 50) < 1e-9);
    assert(s.plcs[0].pumpOn);
    assert(s.readOnly);
    request.locked = true;
    service.Submit("StartRequested", request);
    Until(__LINE__,[&] { return service.Latest().message.find("unlocked") != std::string::npos; });
    assert(!service.Latest().monitoring);
    request.locked = false;
    assert(service.Submit("HeightZero",request));
    Until(__LINE__,[&]{return service.Latest().plcs[0].ports[0].displayRelativeMm==0;});
    service.Submit("StartRequested", request);
    Until(__LINE__,[&] { return service.Latest().monitoring; });
    shared->stroke = 125;
    Until(__LINE__,[&] { return service.Latest().plcs[0].ports[0].relativeMm == 25; });
    service.Submit("StopRequested", request);
    Until(__LINE__,[&] { return !service.Latest().monitoring; });
    assert(service.Latest().plcs[0].ports[0].displayRelativeMm==25);
    assert(service.Submit("StartRequested",request));Until(__LINE__,[&]{return service.Latest().monitoring;});
    assert(service.Latest().plcs[0].ports[0].displayRelativeMm==25); // Successful restart cannot recapture the display zero.
    service.Submit("StopRequested",request);Until(__LINE__,[&]{return !service.Latest().monitoring;});
    service.Submit("EmergencyRequested", request);
    Until(__LINE__,[&] { return service.Latest().plcs[0].emergencyWrite == EmergencyWriteState::ConfirmedOn; });
    assert(shared->emergencyWrites == 1);
    service.Submit("StartRequested", request);
    auto emergencySequence = service.Latest().sequence;
    Until(__LINE__,[&] { return service.Latest().sequence > emergencySequence + 1; });
    assert(!service.Latest().monitoring && service.Latest().emergencyActive && !service.Latest().readOnly);
    assert(shared->emergencyWrites == 1);
    shared->fail = true;
    Until(__LINE__,[&] { return !service.Latest().plcs[0].connected; });
    assert(!service.Latest().cog.valid());
    auto before = std::chrono::steady_clock::now();
    service.Stop();
    assert(!service.Latest().monitoring && !service.Latest().connected && !service.Latest().cog.valid());
    assert(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
    assert(shared->maximum == 1);
    bool rejected = false;
    try
    {
        auto o = Options(true);
        o.plcs[0].channels[1].sensor = 0;
        EquipmentControlService bad(o, factory);
    }
    catch (std::invalid_argument const &)
    {
        rejected = true;
    }
    assert(rejected);
    {
        auto multi = std::make_shared<Shared>();
        auto o = Options(true);
        o.plcs.push_back(o.plcs.front());
        o.plcs.back().ip = "secondary";
        EquipmentControlService across(o, [multi](PlcConfig const &plc, int) {
            return std::make_unique<Reader>(multi, plc.ip == "secondary");
        });
        across.Start();
        Until(__LINE__,[&] { return across.Latest().plcs[1].ports[0].strokeValid; });
        DiagnosticRequest r;
        r.targetMm = 50;
        r.targetBar = 30;
        for (int s = 0; s < 2; ++s)
            for (int p = 0; p < 4; ++p)
                r.selected[s][p] = true;
        across.Submit("StartRequested", r);
        Until(__LINE__,[&] { return across.Latest().synchronization.dataValid; });
        multi->stroke = 125;
        multi->secondStrokeDelta = 20;
        Until(__LINE__,[&] {
            return across.Latest().synchronization.assessment ==
                   SynchronizationAlgorithm::Assessment::StopThresholdExceeded;
        });
        auto sample = across.Latest();
        assert(sample.plcs[0].synchronization.assessment == SynchronizationAlgorithm::Assessment::Normal);
        assert(sample.plcs[1].synchronization.assessment == SynchronizationAlgorithm::Assessment::Normal);
        assert(sample.diagnosticWarning && sample.plcs[0].diagnosticWarning &&
               sample.plcs[1].diagnosticWarning);
        r.pressureMode = true;
        across.Submit("ModePressure", r);
        across.Submit("StartRequested", r);
        Until(__LINE__,[&] { return across.Latest().pressureBalanceValid; });
        multi->secondPressureDelta = 10;
        Until(__LINE__,[&] { return across.Latest().pressureSpreadBar >= 10; });
        assert(!across.Latest().pressureBalanced && across.Latest().diagnosticWarning);
        across.Stop();
    }
    {
        auto input = std::make_shared<Shared>();
        EquipmentControlService emergencyService(Options(true), [input](PlcConfig const&, int) {
            return std::make_unique<Reader>(input);
        });
        emergencyService.Start();
        Until(__LINE__,[&] { return emergencyService.Latest().plcs[0].ports[0].strokeValid; });
        DiagnosticRequest r; r.selected[0][0] = true;
        emergencyService.Submit("StartRequested", r);
        Until(__LINE__,[&] { return emergencyService.Latest().monitoring; });
        input->emergency = true;
        input->sensorReadFail = true;
        Until(__LINE__,[&] { auto v = emergencyService.Latest(); return v.emergencyActive && !v.monitoring && !v.plcs[0].readOk; });
        emergencyService.Submit("StartRequested", r);
        auto sequence = emergencyService.Latest().sequence;
        Until(__LINE__,[&] { return emergencyService.Latest().sequence > sequence + 1; });
        assert(!emergencyService.Latest().monitoring);
        input->fail = true;
        input->emergency = false;
        sequence = emergencyService.Latest().sequence;
        Until(__LINE__,[&] { return emergencyService.Latest().sequence > sequence + 1; });
        assert(emergencyService.Latest().emergencyActive);
        assert(!emergencyService.Latest().emergencyStateVerified);
        input->fail = false; input->sensorReadFail = false;
        Until(__LINE__,[&] { auto v = emergencyService.Latest(); return v.emergencyStateVerified && !v.emergencyActive; });
        assert(!emergencyService.Latest().monitoring);
        emergencyService.Submit("StartRequested", r);
        Until(__LINE__,[&] { return emergencyService.Latest().monitoring; });
        emergencyService.Stop();
    }
    {
        auto input = std::make_shared<Shared>(); input->secondaryEmergencyFail = true;
        auto options = Options(false); options.plcs.push_back(options.plcs.front());
        options.plcs.back().ip = "secondary";
        EquipmentControlService pcEmergency(options, [input](PlcConfig const& plc, int) {
            return std::make_unique<Reader>(input, plc.ip == "secondary");
        });
        pcEmergency.Start();
        DiagnosticRequest r;
        assert(pcEmergency.Submit("EmergencyRequested", r));
        assert(pcEmergency.Latest().emergencyActive);
        Until(__LINE__,[&] { auto v = pcEmergency.Latest(); return v.plcs[0].emergencyWrite == EmergencyWriteState::ConfirmedOn &&
            v.plcs[1].emergencyWrite == EmergencyWriteState::FailedOrUnknown; });
        auto sequence = pcEmergency.Latest().sequence;
        Until(__LINE__,[&] { return pcEmergency.Latest().sequence > sequence + 2; });
        assert(input->emergencyWrites == 2); // Every configured PLC; no automatic retry.
        pcEmergency.Submit("StopRequested", r);
        pcEmergency.Submit("StartRequested", r);
        sequence = pcEmergency.Latest().sequence;
        Until(__LINE__,[&] { return pcEmergency.Latest().sequence > sequence + 1; });
        assert(pcEmergency.Latest().emergencyActive && !pcEmergency.Latest().monitoring);
        input->secondaryEmergencyFail = false;
        pcEmergency.Submit("EmergencyRequested", r); // Only another explicit click retries.
        Until(__LINE__,[&] { return pcEmergency.Latest().plcs[1].emergencyWrite == EmergencyWriteState::ConfirmedOn; });
        assert(input->emergencyWrites == 4);
        pcEmergency.Stop();
    }
    {
        auto input = std::make_shared<Shared>(); input->pcEmergencyInput = true;
        EquipmentControlService restored(Options(true), [input](PlcConfig const&, int) {
            return std::make_unique<Reader>(input);
        });
        restored.Start();
        Until(__LINE__,[&] { return restored.Latest().emergencyActive; });
        assert(restored.Latest().pcEmergencyRequested && !restored.Latest().monitoring);
        assert(input->emergencyWrites == 0); // Reading a previous ON never propagates new writes.
        DiagnosticRequest r; r.selected[0][0] = true;
        restored.Submit("StartRequested", r);
        auto sequence = restored.Latest().sequence;
        Until(__LINE__,[&] { return restored.Latest().sequence > sequence + 1; });
        assert(!restored.Latest().monitoring);
        restored.Stop();
    }
    for (int cycle = 0; cycle < 20; ++cycle)
    {
        auto input = std::make_shared<Shared>();
        auto options = Options(false); options.plcs.push_back(options.plcs.front());
        EquipmentControlService closing(options, [input](PlcConfig const&, int) { return std::make_unique<Reader>(input); });
        closing.Start();
        DiagnosticRequest r;
        closing.Submit("EmergencyRequested", r);
        closing.Submit("StopRequested", r);
        closing.Stop(); // An immediate page close/ordinary stop cannot discard the emergency request.
        assert(input->emergencyWrites == 2);
        assert(closing.Latest().plcs[0].emergencyWrite == EmergencyWriteState::ConfirmedOn);
        assert(closing.Latest().plcs[1].emergencyWrite == EmergencyWriteState::ConfirmedOn);
    }
    DHLS_StrandJack::Models::SystemConfig model;
    model.PLCSettings.PLC_Count = 1;
    model.PLCSettings.Sensor_HPU_Count = 1;
    model.InitializeJackDetails();
    model.PLCSettings.SensorScalingVerified = true;
    model.PLCSettings.EffectiveAreaVerified = true;
    model.PLCSettings.CoordinatesVerified = true;
    model.PLCSettings.InvalidateEquipmentVerification();
    assert(!model.PLCSettings.SensorScalingVerified && !model.PLCSettings.EffectiveAreaVerified &&
           !model.PLCSettings.CoordinatesVerified);
    auto options = EquipmentControlService::FromModel(model);
    options.automaticMotion=false; // This fixture tests read-only diagnostic compatibility.
    assert(!options.plcs[0].channels[0].scalingVerified);
    std::cout << "PASS: serialized reads, unverified data, conversion, COG, lock, diagnostics baseline, "
                 "stop/read-only and emergency ON, disconnect, shutdown, port-map rejection, cross-PLC diagnostics and emergency-input interlock\n";
}
