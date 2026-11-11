#include "../AutomaticDistanceSequence.h"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace DHLS_StrandJack::Control {
static std::uint64_t clockMs = 1000;
std::uint64_t EquipmentControlService::MonotonicMs() { return clockMs; }
}
using namespace DHLS_StrandJack::Control;
int main() {
 ServiceOptions options; options.automaticMotion=true; options.maxSampleAgeMs=1000; options.riseWorkMm=options.fallWorkMm=2; options.riseStopMm=options.fallStopMm=6; options.plcs.resize(1);
 ChannelConfig channel; channel.sensor=0; channel.scalingVerified=channel.areaVerified=true; channel.strokeMmPerCount=.1; channel.pressureBarPerCount=.1; channel.effectiveAreaMm2=1000; channel.strokeLimitMm=500; channel.maximumPressureBar=700; channel.maximumLoadTon=100; channel.workingLoadTon=80; options.plcs[0].channels.push_back(channel);
 DiagnosticRequest request; request.automatic=request.pressureMode=true; request.targetBar=200; request.selected[0][0]=true;
 auto sample=[]() {
  EquipmentSnapshot s; s.plcs.resize(1); auto& plc=s.plcs[0]; plc.connected=plc.readOk=plc.usable=plc.pcSelected=plc.pumpOn=true; plc.sampledAtMs=1000; plc.mapWords={{51,2,true,1000},{52,0,true,1000}}; plc.ports.resize(1);
  auto& p=plc.ports[0]; p.strokeValid=p.pressureValid=p.loadValid=true; p.rawStroke=1000; p.absoluteMm=100; p.rawPressure=1000; p.pressureBar=100; p.loadTon=1; return s;
 };
 auto checkStop=[&](auto mutate,const char* reason) {
  clockMs=1000; auto s=sample(); AutomaticDistanceSequence seq; assert(seq.Begin(options,request,s,1000)); auto on=seq.Desired(); assert(on.masks[0]==1); seq.Commit(on,1001);
  s.plcs[0].mapWords[1].value=on.masks[0]; s.plcs[0].sampledAtMs=1002; clockMs=1002; mutate(s); seq.Build(s,1002);
  assert(seq.Stopping()&&seq.Desired().masks[0]==0);
  assert(seq.Reason().find(reason)!=std::string::npos);
  auto display=seq.PressureResult(); assert(display.state==PressureAutoState::StopRequested&&display.recommendedPercent==0);
 };
 checkStop([](auto& s){s.plcs[0].connected=false;},"feedback stale");
 checkStop([](auto& s){s.plcs[0].pcSelected=false;},"PC mode OFF");
 checkStop([](auto& s){s.plcs[0].mapWords[1].valid=false;},"feedback stale");
 checkStop([](auto& s){s.plcs[0].ports[0].pressureValid=false;},"calibration or feedback invalid");
 checkStop([](auto& s){s.plcs[0].ports[0].strokeValid=false;},"calibration or feedback invalid");
 checkStop([](auto& s){s.faultActive=true;},"fault or emergency");
 checkStop([](auto& s){s.emergencyActive=true;},"fault or emergency");
 checkStop([](auto& s){s.plcs[0].ports[0].rawStroke=990;},"opposite direction");
 clockMs=1000; auto moving=sample(); AutomaticDistanceSequence recede; assert(recede.Begin(options,request,moving,1000)); auto movingOn=recede.Desired(); recede.Commit(movingOn,1001);
 moving.plcs[0].mapWords[1].value=movingOn.masks[0]; moving.plcs[0].sampledAtMs=1002; moving.plcs[0].ports[0].rawStroke=1020; moving.plcs[0].ports[0].absoluteMm=102; clockMs=1002; recede.Build(moving,1002); assert(!recede.Stopping()&&recede.Desired().masks[0]==1);
 moving.plcs[0].sampledAtMs=1003; moving.plcs[0].ports[0].rawStroke=1014; moving.plcs[0].ports[0].absoluteMm=101.4; clockMs=1003; recede.Build(moving,1003);
 assert(recede.Stopping()&&recede.Desired().masks[0]==0&&recede.Reason().find("opposite direction")!=std::string::npos);
 clockMs=1000; auto s=sample(); AutomaticDistanceSequence still; assert(still.Begin(options,request,s,1000)); auto on=still.Desired(); still.Commit(on,1001); s.plcs[0].mapWords[1].value=on.masks[0]; s.plcs[0].sampledAtMs=6101; clockMs=6101; still.Build(s,6101);
 assert(still.Stopping()&&still.Desired().masks[0]==0&&still.Reason().find("No pressure response")!=std::string::npos);
 std::cout<<"PASS: pressure fake feedback disconnect, PC gate, D052 read loss, sensors, fault, emergency, reverse, no response\n";
}
