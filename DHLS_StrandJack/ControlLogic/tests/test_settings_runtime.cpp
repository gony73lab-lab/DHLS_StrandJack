#include "../ControlSettingsRuntime.h"
#include "../EquipmentControlService.h"
#include "../Sequence/MockPlant.h"
#include "../Sequence/SimulationSequence.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <cstdlib>
using namespace DHLS_StrandJack::Control;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<std::endl;std::exit(1);}}while(false)
template<class F>void Wait(F f){for(int i=0;i<200;++i){if(f())return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}CHECK(false);}
struct State{std::atomic<unsigned> mask{0},a{100},b{100};};
struct Reader:IPlcReadTransport{std::shared_ptr<State> s;Reader(std::shared_ptr<State> v):s(v){}bool Connect()override{return true;}void Disconnect()override{}
 bool ReadWords(std::uint16_t address,std::size_t count,std::vector<std::uint16_t>& out)override{out.assign(count,0);if(address==10)out[0]=2;if(address==51)out[0]=2;if(address==52)out[0]=s->mask;if(address==11&&count==16){out[1]=s->a;out[3]=s->b;}return true;}
 bool SetPortMask(std::uint16_t mask,std::function<bool()> const& held)override{if(mask&&held&&!held())return false;s->mask=mask;return true;}
};
int main(){
 CHECK(PressureZeroValue(-5,true)==0&&PressureZeroValue(20,true)==20&&PressureZeroValue(-5,false)==-5);
 CHECK(std::isnan(PressureZeroValue(std::numeric_limits<double>::quiet_NaN(),true)));
 LoadReference reference;CHECK(!reference.Set(0,0)&&!reference.Set(0,-1));CHECK(reference.Set(0,100));
 CHECK(!reference.Exceeded(0,110,true,10)&&!reference.Exceeded(0,90,true,10));
 CHECK(reference.Exceeded(0,110.1,true,10)&&reference.Exceeded(0,89.9,true,10)&&reference.Exceeded(0,100,false,10));
 CHECK(!reference.Exceeded(1,999,true,10));reference.Clear();CHECK(!reference.Has(0));
 DHLS::JackStatus negative;negative.id=1;negative.maxPositionMm=500;negative.sensorValid=negative.stopped=true;negative.pressureBar=-5;
 DHLS::MockPlant pressurePlant({negative});pressurePlant.PressureZero({1});CHECK(pressurePlant.Read().jacks[0].pressureBar==0);
 negative.pressureBar=20;pressurePlant.SetJack(0,negative);CHECK(pressurePlant.Read().jacks[0].pressureBar==20);
 ServiceOptions loadSim;loadSim.simulation=true;loadSim.loadToleranceEnabled=true;loadSim.loadTolerancePercent=10;
 loadSim.riseWorkMm=2;loadSim.riseStopMm=6;loadSim.plcs.resize(1);ChannelConfig hydraulic;
 hydraulic.strokeMmPerCount=500.0/16000;hydraulic.strokeLimitMm=500;hydraulic.effectiveAreaMm2=98066.5;hydraulic.maximumPressureBar=700;hydraulic.maximumLoadTon=700;hydraulic.workingLoadTon=500;
 loadSim.plcs[0].channels.push_back(hydraulic);SimulationSequence loadSimulation(loadSim);DiagnosticRequest lift;
 lift.selected[0][0]=true;lift.targetMm=2;loadSimulation.Submit("StartRequested",lift);
 Wait([&]{return loadSimulation.Latest().sequencePhase==SequencePhase::Completed;});
 {auto before=loadSimulation.Latest();auto next=loadSim;next.plcs[0].channels[0].strokeLimitMm=1000;next.plcs[0].channels[0].strokeMmPerCount=1000.0/16000;
    CHECK(loadSimulation.Reconfigure(next));auto after=loadSimulation.Latest();
    CHECK(after.sequencePhase==before.sequencePhase&&after.plcs[0].ports[0].absoluteMm==2*before.plcs[0].ports[0].absoluteMm);
    CHECK(loadSimulation.Reconfigure(loadSim));}
 CHECK(loadSimulation.Latest().sequenceNavigationLocked);loadSimulation.Submit("ModeManual",lift);CHECK(loadSimulation.Latest().sequenceNavigationLocked);
 loadSimulation.Submit("LoadSet",lift);CHECK(loadSimulation.Latest().plcs[0].ports[0].referenceLoadTon>0);
 lift.targetMm=100;loadSimulation.Submit("StartRequested",lift);
 Wait([&]{auto v=loadSimulation.Latest();return v.loadReferenceTripped&&!v.sequenceBusy&&v.sequenceStopConfirmed;});
 loadSimulation.Submit("StartRequested",lift);CHECK(loadSimulation.Latest().loadReferenceTripped);
 loadSimulation.Submit("ResetSequence",lift);CHECK(!loadSimulation.Latest().loadReferenceTripped&&!loadSimulation.Latest().sequenceNavigationLocked);loadSimulation.Shutdown();
 CHECK(StrokeExtensionRatio(4000.0/16000*500,500)==.25);
 CHECK(StrokeExtensionRatio(8000.0/16000*500,500)==.5);
 CHECK(MovementCycleRatio(125,0,125)==1&&MovementCycleRatio(125,125,250)==0&&MovementCycleRatio(250,125,250)==1);
 CHECK(MovementCycleRatio(187.5,250,125)==.5&&MovementCycleRatio(125,250,125)==1);
 CHECK(std::isnan(StrokeExtensionRatio(125,0))&&std::isnan(MovementCycleRatio(NAN,0,125)));
 CHECK(AvailableTravelMm(450,500,false)==50&&AvailableTravelMm(450,500,true)==450);
 CHECK(AvailableTravelMm(500,500,false)==0&&AvailableTravelMm(0,500,true)==0&&std::isnan(AvailableTravelMm(501,500,false)));
 StrokeAverage average;CHECK(average.Push(10,5)==10);average.Push(20,5);average.Push(30,5);average.Push(40,5);CHECK(average.Push(50,5)==30);CHECK(average.Push(60,5)==40);average.Clear();CHECK(average.Push(100,5)==100);CHECK(average.Push(25,1)==25);
 std::vector<bool> paused;CHECK(ManualSyncPlan({0,3},2,6,paused)&&!paused[0]&&paused[1]);CHECK(ManualSyncPlan({0,1.5},2,6,paused)&&paused[1]);CHECK(ManualSyncPlan({0,1},2,6,paused)&&!paused[1]);CHECK(!ManualSyncPlan({0,7},2,6,paused));CHECK(!ManualSyncPlan({0,1},0,0,paused));
 DHLS::JackStatus jack;jack.id=1;jack.maxPositionMm=1000;jack.sensorValid=true;jack.stopped=true;
 DHLS::MockPlant filtered({jack},{},5);
 for(int i=1;i<=5;++i){jack.positionMm=i*10;filtered.SetJack(0,jack);auto v=filtered.Read();CHECK(v.jacks[0].positionMm==(i+1)*5);}
 jack.positionMm=60;filtered.SetJack(0,jack);CHECK(filtered.Read().jacks[0].positionMm==40);
 ServiceOptions simOptions;simOptions.simulation=true;simOptions.strokeFilterSamples=5;simOptions.riseWorkMm=2;simOptions.riseStopMm=6;simOptions.plcs.resize(1);
 ChannelConfig simChannel;simChannel.strokeLimitMm=500;simOptions.plcs[0].channels.push_back(simChannel);
 SimulationSequence simulation(simOptions);DiagnosticRequest simRequest;simRequest.selected[0][0]=true;simRequest.targetMm=5;
 simOptions.manualSync=false;simulation.Submit("StartRequested",simRequest);
 Wait([&]{return simulation.Latest().sequencePhase==SequencePhase::Completed;});simulation.Shutdown();
 auto state=std::make_shared<State>();ServiceOptions options;options.pollIntervalMs=20;options.manualSync=true;options.riseWorkMm=2;options.riseStopMm=6;options.strokeFilterSamples=5;options.plcs.resize(1);
 ChannelConfig c;c.areaVerified=true;c.effectiveAreaMm2=100;c.maximumLoadTon=10;c.workingLoadTon=8;c.scalingVerified=true;c.strokeMmPerCount=1;c.pressureBarPerCount=1;c.strokeLimitMm=1000;options.plcs[0].channels.push_back(c);c.sensor=1;options.plcs[0].channels.push_back(c);
 EquipmentControlService service(options,[&](auto const&,int){return std::make_unique<Reader>(state);});service.Start();Wait([&]{auto v=service.Latest();return !v.plcs.empty()&&v.plcs[0].ports[0].strokeValid;});
 DiagnosticRequest request;request.automatic=false;request.selected[0][0]=request.selected[0][1]=true;CHECK(service.Submit("StartRequested",request));Wait([&]{return service.Latest().manualRunning;});CHECK(state->mask==3);
 state->a=104;Wait([&]{return state->mask==2;});CHECK(service.Latest().manualRunning);Wait([&]{return service.Latest().plcs[0].ports[0].absoluteMm==104;});CHECK(service.Latest().plcs[0].ports[0].rawStroke==104);
 state->b=104;Wait([&]{return state->mask==3;});state->a=115;Wait([&]{auto v=service.Latest();return !v.manualRunning&&!v.manualStopPending&&state->mask==0;});service.Stop();
 std::cout<<"PASS: rolling 5-sample stroke mean, warmup/reset, sync hysteresis/trip, real D052 pause/resume and stop cleanup, simulated filtered feedback and target completion\n";
}
