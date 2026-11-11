#include "PressureLiftUp.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
 PressureLiftUp::Config c; c.selected[0]=true; c.pressure[0].verified=true;
 c.pressure[0].mpaPerCount=.01; c.pressure[0].pistonAreaCm2=100;
 c.targetMpa[0]=5; c.maxAdvisoryMpa[0]=8;
 PressureLiftUp monitor(c); DRegionDiagnostics::Snapshot s;
 s.readOk=true; s.pcSelected=true; s.rawPressure[0]=600;
 auto r=monitor.Analyze(s,100,101);
 assert(r.state==PressureLiftUp::State::PressureTargetObserved);
 assert(std::abs(r.channels[0].estimatedForceKN-60)<1e-9);
 s.rawPressure[0]=900; r=monitor.Analyze(s,100,101);
 assert(r.state==PressureLiftUp::State::LimitIndicated);
 s.pressureDisconnected[0]=true; r=monitor.Analyze(s,100,101);
 assert(r.state==PressureLiftUp::State::Invalid);
 s.pressureDisconnected[0]=false; r=monitor.Analyze(s,100,1000);
 assert(r.state==PressureLiftUp::State::Invalid);
 std::cout<<"Pressure lift-up offline tests PASS\n";
}
