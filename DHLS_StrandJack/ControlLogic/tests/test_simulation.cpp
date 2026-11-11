#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#include "../EquipmentControlService.h"
#include "../DeveloperSimulation.h"
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
using namespace DHLS_StrandJack::Control;
int main(int argc, char** argv) {
#ifdef _MSC_VER
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
 _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif

 {
     ServiceOptions monitorOptions;
     monitorOptions.automaticMotion = true;
     EquipmentControlService monitor(monitorOptions); // No PLC addresses: no network I/O.
     assert(monitor.IsLiveMonitorOnly());
     monitor.Start();
     DiagnosticRequest request;
     for (auto action : {"StartRequested", "UpRequested", "DownRequested", "PumpToggle:1",
                         "LiftingSpeed:1:20", "StopRequested", "FullStopRequested",
                         "EmergencyRequested", "ResetSequence", "UnrecognizedOutput"})
         assert(!monitor.Submit(action, request));
     const auto snapshot = monitor.Latest();
     assert(!snapshot.pcEmergencyRequested && !snapshot.manualRunning && !snapshot.sequenceBusy);
     monitor.Stop();
 }

 ServiceOptions o; o.riseWorkMm=o.fallWorkMm=2;o.riseStopMm=o.fallStopMm=5; o.plcs.resize(2);
 for(auto& plc:o.plcs) { plc.channels.resize(2); plc.channels[1].sensor=1;for(auto& c:plc.channels)c.strokeLimitMm=100; }
 auto pumpOptions=o;
 pumpOptions.plcs[0].channels[0].pumpIp = pumpOptions.plcs[1].channels[0].pumpIp = "192.168.10.124";
 pumpOptions.plcs[0].channels[1].pumpIp = pumpOptions.plcs[1].channels[1].pumpIp = "192.168.10.125";
 int transports=0;
 DeveloperSimulationEnabled.store(true);
 EquipmentControlService service(pumpOptions,[&](PlcConfig const&,int)->std::unique_ptr<IPlcReadTransport>{++transports; return {};});
 assert(!service.IsLiveMonitorOnly());
 service.Start();
 int stage=0;
 auto wait=[&](auto predicate) { ++stage; for(int i=0;i<100;++i){if(predicate(service.Latest()))return;std::this_thread::sleep_for(std::chrono::milliseconds(20));} auto failed=service.Latest();std::cerr<<"stage="<<stage<<" state="<<static_cast<int>(failed.sequencePhase)<<" busy="<<failed.sequenceBusy<<" confirmed="<<failed.sequenceStopConfirmed<<"\n";std::wcerr<<failed.sequenceMessage<<L"\n";assert(false); };
 wait([](auto const& s){return s.connected;});
 DiagnosticRequest r; r.selected[0][0]=true; r.targetMm=10; r.targetBar=20;
 assert(service.Submit("StartRequested",r));
 wait([](auto const& s){return s.sequenceMessage == L"START rejected: selected jack pump is OFF";});
 assert(!service.Latest().pumpByIp.at("192.168.10.124"));
 assert(service.Latest().plcs[0].ports[0].absoluteMm==0);
 DiagnosticRequest pumpRequest;
 assert(service.Submit("PumpToggle:1", pumpRequest));
 wait([](auto const& s){return s.plcs.size() == 2 && s.plcs[0].pumpOn && !s.plcs[1].pumpOn;});
 assert(service.Submit("PumpToggle:2", pumpRequest));
 wait([](auto const& s){return s.plcs.size() == 2 && s.plcs[0].pumpOn && s.plcs[1].pumpOn;});
 assert(service.Submit("PumpToggle:1", pumpRequest));
 wait([](auto const& s){return s.plcs.size() == 2 && !s.plcs[0].pumpOn && s.plcs[1].pumpOn;});
 assert(service.Submit("PumpToggle:2", pumpRequest));
 wait([](auto const& s){return s.plcs.size() == 2 && !s.plcs[0].pumpOn && !s.plcs[1].pumpOn;});
 assert(service.Submit("PumpIpToggle:192.168.10.124", pumpRequest));
 wait([](auto const& s){return s.pumpByIp.at("192.168.10.124") && !s.pumpByIp.at("192.168.10.125") &&
     s.plcs[0].pumpOn && s.plcs[1].pumpOn;});
 assert(service.Submit("PumpIpToggle:192.168.10.125", pumpRequest));
 wait([](auto const& s){return s.pumpByIp.at("192.168.10.124") && s.pumpByIp.at("192.168.10.125");});
 assert(service.Submit("PumpIpToggle:192.168.10.124", pumpRequest));
 wait([](auto const& s){return !s.pumpByIp.at("192.168.10.124") && s.pumpByIp.at("192.168.10.125");});
 assert(service.Submit("PumpIpToggle:192.168.10.125", pumpRequest));
 wait([](auto const& s){return !s.pumpByIp.at("192.168.10.124") && !s.pumpByIp.at("192.168.10.125");});
 assert(service.Submit("PumpIpToggle:192.168.10.124", pumpRequest));
 wait([](auto const& s){return s.pumpByIp.at("192.168.10.124");});
 assert(service.Submit("PumpIpToggle:192.168.10.125", pumpRequest));
 wait([](auto const& s){return s.pumpByIp.at("192.168.10.125");});
 service.Submit("StartRequested",r);
 wait([](auto const& s){return s.monitoring && s.plcs[0].ports[0].absoluteMm>=2.0;});
 auto first=service.Latest();
 assert(first.plcs[0].ports[1].absoluteMm==0 && first.plcs[1].ports[0].absoluteMm==0);
 service.Submit("StopRequested",r);
 wait([](auto const& s){return !s.monitoring&&!s.sequenceBusy;});
 first=service.Latest();
 r.lowering=true;r.automatic=false; service.Submit("DownRequested",r); service.Submit("StartRequested",r);
 wait([&](auto const& s){return s.plcs[0].ports[0].absoluteMm<first.plcs[0].ports[0].absoluteMm;});
 service.Submit("StopRequested",r);
 wait([](auto const& s){return !s.monitoring&&!s.sequenceBusy;});
 auto snapshot=service.Latest();
 assert(snapshot.sequenceStopConfirmed&&!snapshot.sequenceHistory.empty());
 assert(snapshot.sequenceHistory.back().atMs>0&&snapshot.sequenceHistory.back().operationId>0);
 assert(snapshot.sequenceHistory.back().selectedJackIds==std::vector<int>{1});
 auto stopped=snapshot.plcs[0].ports[0].absoluteMm;
 std::this_thread::sleep_for(std::chrono::milliseconds(200));
 assert(service.Latest().plcs[0].ports[0].absoluteMm==stopped);
 service.Submit("EmergencyRequested",r);
 wait([](auto const& s){return s.emergencyActive;});
 wait([](auto const& s){return s.emergencyActive&&!s.sequenceBusy;});
 service.Submit("ResetSequence",r);
 wait([](auto const& s){return s.sequencePhase==SequencePhase::Idle&&!s.emergencyActive;});
 assert(!service.Latest().monitoring); // RESET cannot restart a motion.
 for(auto const& plc:service.Latest().plcs)for(auto const& port:plc.ports)assert(port.absoluteMm==0&&port.pressureBar==0);
 service.Stop(); assert(service.Latest().sequenceStopConfirmed);assert(transports==0);
 if(argc==2&&std::string(argv[1])=="--pump-only"){
   std::cout<<"PASS: OFF blocks START; only selected jacks move after pump ON; no real PLC transport\n";
   return 0;
 }
 // Closing while motion or an accepted emergency is pending must finish on the owner worker.
 for(int cycle=0;cycle<5;++cycle){
   EquipmentControlService closing(o,[&](PlcConfig const&,int)->std::unique_ptr<IPlcReadTransport>{++transports;return {};});
   closing.Start();
   DiagnosticRequest run;run.selected[0][0]=true;run.targetMm=10;
   closing.Submit("StartRequested",run);
   if(cycle%2)closing.Submit("EmergencyRequested",run);
   else std::this_thread::sleep_for(std::chrono::milliseconds(80));
   closing.Stop();auto final=closing.Latest();
   assert(final.sequenceStopConfirmed&&!final.sequenceBusy&&!final.monitoring);
   if(cycle%2)assert(final.emergencyActive);
 }
 // Full stop preserves position and requires RESET before another START.
 {
   auto settings=o;EquipmentControlService twoStop(settings);
   twoStop.Start();DiagnosticRequest go;go.selected[0][0]=true;go.targetMm=10;
   twoStop.Submit("StartRequested",go);
   auto until=[&](auto fn){for(int i=0;i<200;++i){auto v=twoStop.Latest();assert(v.sequencePhase!=SequencePhase::Fault);if(fn(v))return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}assert(false);};
   until([](auto const& v){return v.monitoring&&v.plcs[0].ports[0].absoluteMm>1;});
   twoStop.Submit("StopRequested",go);
   until([](auto const& v){return !v.sequenceBusy&&v.sequencePhase==SequencePhase::Stopped;});
   const double position=twoStop.Latest().plcs[0].ports[0].absoluteMm;
   twoStop.Submit("FullStopRequested",go);
   until([](auto const& v){return v.sequenceMessage==L"Full stop confirmed";});
   const auto fullStopEvent=twoStop.Latest().sequenceEventId;
   twoStop.Submit("StartRequested",go);
   until([&](auto const& v){return v.sequenceEventId>fullStopEvent&&v.automaticResetRequired&&!v.sequenceBusy;});
   assert(twoStop.Latest().plcs[0].ports[0].absoluteMm==position);
   twoStop.Submit("ResetSequence",go);
   until([](auto const& v){return v.sequencePhase==SequencePhase::Idle;});
   twoStop.Submit("StartRequested",go);
   until([](auto const& v){return v.monitoring;});
   twoStop.Submit("FullStopRequested",go);
   until([](auto const& v){return !v.sequenceBusy&&v.sequenceMessage==L"Full stop confirmed";});
   twoStop.Submit("ResetSequence",go);
   until([](auto const& v){return v.sequencePhase==SequencePhase::Idle;});
   auto cleared=twoStop.Latest();assert(!cleared.emergencyActive&&!cleared.pcEmergencyRequested);
   for(auto const& plc:cleared.plcs)for(auto const& port:plc.ports){assert(port.absoluteMm==0&&port.relativeMm==0&&port.pressureBar==0&&port.sequenceTargetMm==0);}
   twoStop.Stop();
 }
 // Cross-PLC synchronization must own every selected active port, regardless of different SET speeds.
 {
   auto multi=o;multi.mockSyncMm=.5;multi.mockTripMm=6;
   EquipmentControlService sync(multi,[&](PlcConfig const&,int)->std::unique_ptr<IPlcReadTransport>{++transports;return {};});
   sync.Start();DiagnosticRequest go;go.selected[0][0]=go.selected[0][1]=go.selected[1][0]=true;
   go.targetMm=20;go.liftingHz[0]=20;go.liftingHz[1]=10;
   assert(sync.Submit("StartRequested",go));
   const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   bool complete=false,paused=false;double largestSpread=0,previousFast=-1,previousSlow=-1;
   while(std::chrono::steady_clock::now()<end){auto v=sync.Latest();
     assert(v.sequencePhase!=SequencePhase::Fault&&v.sequencePhase!=SequencePhase::Emergency);
     double fast=v.plcs[0].ports[0].absoluteMm,other=v.plcs[0].ports[1].absoluteMm,slow=v.plcs[1].ports[0].absoluteMm;
     largestSpread=(std::max)(largestSpread,(std::max)({fast,other,slow})-(std::min)({fast,other,slow}));
     assert(v.plcs[1].ports[1].absoluteMm==0);
     if(fast>1&&fast<19&&std::abs(fast-previousFast)<.001&&slow>previousSlow+.01)paused=true;
     previousFast=fast;previousSlow=slow;
     if(v.sequencePhase==SequencePhase::Completed&&!v.sequenceBusy){
       const auto reached=[&](double position){return std::abs(position-go.targetMm)<=multi.positionToleranceMm;};
       std::cout<<"Cross-PLC completion (mm): "<<fast<<", "<<other<<", "<<slow
                <<"; peak spread "<<largestSpread<<" mm\n"<<std::flush;
       assert(v.sequenceStopConfirmed&&reached(fast)&&reached(other)&&reached(slow));
       assert(v.sequenceHistory.back().selectedJackIds==std::vector<int>({1,2,9}));
       complete=true;break;
     }
     std::this_thread::sleep_for(std::chrono::milliseconds(25));
   }
   assert(complete&&paused&&largestSpread>.5&&largestSpread<2);sync.Stop();
 }
 // Regression: at the UI default 20 Hz, a 500 mm move must survive the entire pressure/load check.
 {
   ServiceOptions full;full.riseWorkMm=full.fallWorkMm=2;full.riseStopMm=full.fallStopMm=6;
   full.maximumStopPressureBar=700;full.plcs.resize(1);full.plcs[0].channels.resize(2);
   auto& channel=full.plcs[0].channels[0];channel.strokeLimitMm=500;channel.effectiveAreaMm2=1000;
   channel.maximumPressureBar=700;channel.maximumLoadTon=700*1000/98066.5;
   channel.workingLoadTon=channel.maximumLoadTon*.6;
   full.plcs[0].channels[1]=channel;full.plcs[0].channels[1].sensor=1;
   EquipmentControlService motion(full,[&](PlcConfig const&,int)->std::unique_ptr<IPlcReadTransport>{++transports;return {};});
   motion.Start();DiagnosticRequest go;go.selected[0][0]=true;go.targetMm=500;go.automatic=true;
   assert(motion.Submit("StartRequested",go));
   const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(70);
   bool running=false,completed=false,stoppedOnce=false,restarted=false;
   while(std::chrono::steady_clock::now()<end){auto v=motion.Latest();
      running|=v.monitoring;
      if(!stoppedOnce&&v.monitoring&&v.plcs[0].ports[0].absoluteMm>=20){
          assert(motion.Submit("StopRequested",go));stoppedOnce=true;
      }
      if(stoppedOnce&&!restarted&&v.sequencePhase==SequencePhase::Stopped&&!v.sequenceBusy){
          assert(v.sequenceStopConfirmed);assert(motion.Submit("StartRequested",go));restarted=true;
      }

      assert(v.sequencePhase!=SequencePhase::Fault&&v.sequencePhase!=SequencePhase::Emergency);
      assert(v.plcs[0].ports[1].absoluteMm==0);
      if(v.sequencePhase==SequencePhase::Completed&&!v.sequenceBusy){
          assert(v.sequenceStopConfirmed&&std::abs(v.plcs[0].ports[0].absoluteMm-go.targetMm)<=full.positionToleranceMm);
          assert(v.plcs[0].ports[0].pressureBar<700);completed=true;break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
   }
   assert(running&&completed&&stoppedOnce&&restarted);motion.Stop();
 }
 assert(transports==0);
 DeveloperSimulationEnabled.store(false);
 EquipmentControlService real(o,[&](PlcConfig const&,int)->std::unique_ptr<IPlcReadTransport>{++transports;return {};});
 real.Start();std::this_thread::sleep_for(std::chrono::milliseconds(100));real.Stop();assert(transports==2);
}
