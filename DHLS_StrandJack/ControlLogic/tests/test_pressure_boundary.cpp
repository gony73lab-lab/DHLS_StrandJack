
#include "../AutomaticDistanceSequence.h"
#include <cassert>
#include <iostream>
namespace DHLS_StrandJack::Control { std::uint64_t EquipmentControlService::MonotonicMs() { return 1000; } }
using namespace DHLS_StrandJack::Control;
int main() {
 ServiceOptions o; o.automaticMotion=true; o.riseWorkMm=o.fallWorkMm=2; o.riseStopMm=o.fallStopMm=6; o.maxSampleAgeMs=1000; o.plcs.resize(1);
 ChannelConfig c; c.sensor=0; c.scalingVerified=c.areaVerified=true; c.strokeMmPerCount=500.0/16000; c.pressureBarPerCount=.1; c.effectiveAreaMm2=1000; c.strokeLimitMm=500; c.maximumPressureBar=700; c.maximumLoadTon=100; c.workingLoadTon=80; o.plcs[0].channels.push_back(c);
 EquipmentSnapshot s; s.plcs.resize(1); auto& plc=s.plcs[0]; plc.connected=plc.readOk=plc.usable=plc.pcSelected=plc.pumpOn=true; plc.sampledAtMs=1000; plc.mapWords={{51,2,true,1000},{52,0,true,1000}}; plc.ports.resize(1);
 auto& v=plc.ports[0]; v.strokeValid=v.pressureValid=v.loadValid=true; v.pressureBar=100; v.rawPressure=1000; v.loadTon=1;
 DiagnosticRequest r; r.automatic=r.pressureMode=true; r.selected[0][0]=true; r.targetBar=200;
 v.rawStroke=16000; v.absoluteMm=490; AutomaticDistanceSequence upStart; assert(!upStart.Begin(o,r,s,1000)); assert(upStart.Desired().masks[0]==0);
 v.rawStroke=15999; AutomaticDistanceSequence up; assert(up.Begin(o,r,s,1000)); assert(up.Desired().masks[0]==1); auto on=up.Desired(); up.Commit(on,1001); plc.mapWords[1].value=on.masks[0]; v.rawStroke=16000; up.Build(s,1002);
 assert(up.Stopping()&&up.Desired().masks[0]==0&&up.Reason().find("Raw Stroke limit")!=std::string::npos); auto d=up.PressureResult(); assert(d.state==PressureAutoState::StopRequested&&d.recommendedPercent==0&&!d.commandOutputAllowed);
 plc.mapWords[1].value=0; v.pressureBar=200; AutomaticDistanceSequence reached; assert(reached.Begin(o,r,s,1000)); assert(reached.Desired().masks[0]==0);
 v.pressureBar=100; plc.mapWords[0].value=4; v.rawStroke=0; v.absoluteMm=10; r.lowering=true; r.targetBar=50;
 AutomaticDistanceSequence downStart; assert(!downStart.Begin(o,r,s,1000)); assert(downStart.Desired().masks[0]==0);
 v.rawStroke=1; AutomaticDistanceSequence down; assert(down.Begin(o,r,s,1000)); assert(down.Desired().masks[0]==1); on=down.Desired(); down.Commit(on,1001); plc.mapWords[1].value=on.masks[0]; v.rawStroke=0; down.Build(s,1002);
 assert(down.Stopping()&&down.Desired().masks[0]==0&&down.Reason().find("Raw Stroke limit")!=std::string::npos);
 std::cout<<"PASS: pressure raw upper/lower limits, reached target, stop diagnostics\n";
}
