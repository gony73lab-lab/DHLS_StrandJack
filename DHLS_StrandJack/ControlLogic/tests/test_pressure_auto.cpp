#include "../PressureAutoControl.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace DHLS_StrandJack::Control;
PressureInput Sample(int n=2){PressureInput in;in.communication=in.permitted=true;in.sampleId=1;in.atMs=1000;for(int i=0;i<n;++i)in.cylinders.push_back({i+1,true,true,true,true,0,10,500,70,1000,100});return in;}
void Tick(PressureAutoController& c,PressureInput& in,std::uint64_t t){in.sampleId++;in.atMs=t;c.Update(in,t);}
int main(){
 for(int n:{1,2,4,8}){auto in=Sample(n);PressureAutoController c;assert(c.Configure({},10,2,6));assert(c.StartRequest(in,1000,true,true));Tick(c,in,1100);assert(c.Result().ports.size()==n&&c.Result().averageMPa==0&&c.Result().recommendedPercent>0);}
 {auto in=Sample(8);for(int i=3;i<8;++i){in.cylinders[i].selected=false;in.cylinders[i].pressureMPa=NAN;}PressureAutoController c;assert(c.Configure({},10,2,6));assert(c.StartRequest(in,1000,true,true));assert(c.Result().ports.size()==3);}
 {auto in=Sample();in.cylinders[0].pressureMPa=10;in.cylinders[1].pressureMPa=0;PressureAutoController c;c.Configure({},10,2,6);assert(c.StartRequest(in,1000,true,false));Tick(c,in,1100);assert(c.Result().state==PressureAutoState::Hold&&c.Result().stopRequested&&!c.Result().allReached);}
 {auto in=Sample();in.cylinders[1].areaMm2=2000;PressureAutoController c;c.Configure({},10,2,6);assert(c.StartRequest(in,1000,true,true));in.cylinders[0].pressureMPa=in.cylinders[1].pressureMPa=5;Tick(c,in,1100);assert(c.Result().ports[1].forceKN==2*c.Result().ports[0].forceKN);}
 {auto in=Sample();PressureAutoController c;PressureAutoSettings s;s.settleMs=100;c.Configure(s,10,2,6);assert(c.StartRequest(in,1000,true,true));in.cylinders[0].pressureMPa=10;Tick(c,in,1100);assert(c.Result().partial&&c.Result().ports[0].hold&&!c.Result().allReached);
  in.cylinders[1].pressureMPa=10;Tick(c,in,1200);assert(c.Result().state==PressureAutoState::TargetPending);in.cylinders[1].pressureMPa=9;Tick(c,in,1250);assert(!c.Result().allReached);
  in.cylinders[1].pressureMPa=10;Tick(c,in,1300);Tick(c,in,1401);assert(c.Result().stopRequested&&!c.Result().stopConfirmed);
  in.outputsOff=false;Tick(c,in,1500);Tick(c,in,1600);assert(!c.Result().stopConfirmed);
  in.outputsOff=true;for(auto& v:in.cylinders)v.stopped=true;Tick(c,in,1700);Tick(c,in,1800);Tick(c,in,1900);assert(c.Result().state==PressureAutoState::Completed&&c.Result().stopConfirmed&&!c.Busy());
  in.cylinders[0].pressureMPa=8;Tick(c,in,2000);assert(c.Result().state==PressureAutoState::Hold&&!c.Busy());}
 for(int fault=0;fault<8;++fault){auto in=Sample();PressureAutoController c;c.Configure({},10,2,6);assert(c.StartRequest(in,1000,true,true));
  if(fault==0)in.communication=false;if(fault==1)in.cylinders[0].valid=false;if(fault==2)in.cylinders[0].calibrated=false;if(fault==3)in.cylinders[0].pressureMPa=NAN;if(fault==4)in.cylinders[0].strokeMm=INFINITY;if(fault==5)in.cylinders[0].pressureMPa=71;if(fault==6)in.cylinders[0].strokeMm=17;if(fault==7)in.atMs=1;
  if(fault==7){in.sampleId++;c.Update(in,2000);}else Tick(c,in,1100);assert(c.Result().stopRequested&&!c.Result().allReached&&c.Result().recommendedPercent==0);}
 {auto in=Sample();PressureAutoSettings s;s.balanceToleranceMPa=.1;PressureAutoController c;c.Configure(s,10,2,6);c.StartRequest(in,1000,true,true);in.cylinders[0].pressureMPa=1;Tick(c,in,1100);assert(c.Result().state==PressureAutoState::Hold);}
 {auto in=Sample();PressureAutoController c;c.Configure({},10,2,6);assert(!c.StartRequest(in,1000,false,true));assert(c.Result().state==PressureAutoState::OutputInhibited&&!c.Busy());}
 {auto in=Sample();PressureAutoSettings s;s.timeoutMs=100;s.stopTimeoutMs=100;PressureAutoController c;c.Configure(s,10,2,6);c.StartRequest(in,1000,true,true);Tick(c,in,1101);assert(c.Result().stopRequested);Tick(c,in,1300);assert(!c.Result().stopConfirmed);}
 {auto in=Sample();PressureAutoController c;c.Configure({},10,2,6);c.StartRequest(in,1000,true,true);in.cylinders[0].strokeMm=13;Tick(c,in,1100);assert(c.Result().ports[0].hold);in.cylinders[1].strokeMm=12.5;Tick(c,in,1200);assert(!c.Result().ports[0].hold);}
 {PressureAutoSettings s;s.toleranceMPa=2;PressureAutoController c;assert(!c.Configure(s,10,2,6));auto in=Sample();c.Configure({},80,2,6);assert(!c.StartRequest(in,1000,true,true));}
 {auto in=Sample();PressureAutoSettings settings;settings.ratePercentPerSecond=20;PressureAutoController c;c.Configure(settings,10,2,6);assert(c.StartRequest(in,1000,true,true));Tick(c,in,1100);assert(c.Result().recommendedPercent>=25&&c.Result().recommendedPercent<=27.000001);Tick(c,in,1200);assert(c.Result().recommendedPercent>=25&&c.Result().recommendedPercent<=29.000001);}
 {auto in=Sample();PressureAutoController c;c.Configure({},10,2,6,{8,12});assert(c.StartRequest(in,1000,true,true));assert(c.Result().ports[0].targetMPa==8&&c.Result().ports[1].targetMPa==12);}
 {auto in=Sample();PressureAutoController c;c.Configure({},10,2,6);c.StartRequest(in,1000,true,true);Tick(c,in,6100);assert(c.Result().state==PressureAutoState::Fault&&c.Result().stopRequested);}
 {auto in=Sample();PressureAutoController c;c.Configure({},10,2,6);c.StartRequest(in,1000,true,true);c.RequestStop(1100);in.outputsOff=true;in.cylinders[0].stopped=false;Tick(c,in,1200);Tick(c,in,1300);Tick(c,in,1400);assert(!c.Result().stopConfirmed);}
 {auto in=Sample();for(auto& v:in.cylinders){v.pressureMPa=15;v.strokeMm=100;}PressureAutoSettings settings;settings.settleMs=100;PressureAutoController c;assert(c.Configure(settings,10,2,6,{},true));assert(c.StartRequest(in,1000,true,true));Tick(c,in,1100);assert(!c.Result().allReached);in.cylinders[0].pressureMPa=10;Tick(c,in,1200);assert(c.Result().partial&&c.Result().ports[0].hold);in.cylinders[1].pressureMPa=10;Tick(c,in,1300);Tick(c,in,1401);assert(c.Result().stopRequested);}
 std::cout<<"PASS: pressure MPa targets, variable selection, partial/shared drive, independent areas, settle/noise, OFF+stable stop, protections, inhibition, timeout and sync\n";
}
