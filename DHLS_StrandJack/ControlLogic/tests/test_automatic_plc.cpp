#include "../AutomaticDistanceSequence.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstdlib>
using namespace DHLS_StrandJack::Control;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<std::endl;std::exit(1);}}while(false)
static int stage=0;
template<class F>void Wait(F f){++stage;for(int i=0;i<400;++i){if(f())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}std::cerr<<"Timeout stage "<<stage<<std::endl;CHECK(false);}
struct World{
 std::mutex mutex;std::array<unsigned,2> mask{{0xA500,0xA500}},speed{},direction{{2,2}};std::array<bool,2> pc{{true,true}},pump{{true,true}};
 std::array<std::array<unsigned,8>,2> stroke{},pending{};bool frame=false,pressureNoise=false,failOn=false;unsigned pressure=100,onWrites=0,offWrites=0,speedWrites=0;std::array<unsigned,2> fault{};
 std::atomic<bool> blockOn{false},entered{false},release{false};
 void Position(double mm){std::lock_guard<std::mutex> l(mutex);for(auto& row:pending)row.fill(static_cast<unsigned>(std::lround(mm*10)));frame=true;}
 void Directions(unsigned v){std::lock_guard<std::mutex> l(mutex);direction.fill(v);}
 unsigned Mask(int s){std::lock_guard<std::mutex> l(mutex);return mask[s]&255;}
 unsigned Hz(int s){std::lock_guard<std::mutex> l(mutex);return speed[s];}
 unsigned Ons(){std::lock_guard<std::mutex> l(mutex);return onWrites;}
};
struct Reader:IPlcReadTransport{
 std::shared_ptr<World> w;int set;Reader(std::shared_ptr<World> v,int s):w(v),set(s){}
 bool Connect()override{return true;}void Disconnect()override{}
 bool ReadWords(std::uint16_t address,std::size_t count,std::vector<std::uint16_t>& out)override{
  std::lock_guard<std::mutex> l(w->mutex);out.assign(count,0);
  if(address==10)out[0]=(w->pc[set]?2:0)|(w->pump[set]?4:0);
  if(address==50)out[0]=w->speed[set];if(address==51)out[0]=w->direction[set];if(address==52)out[0]=w->mask[set];if(address==55)out[0]=w->fault[set];
  if(address==11&&count==16){if(set==0){if(w->frame){w->stroke=w->pending;w->frame=false;}if(w->pressureNoise)w->pressure=w->pressure==100?101:100;}
   for(int i=0;i<8;++i){out[i*2]=w->pressure;out[i*2+1]=w->stroke[set][i];}}
  return true;
 }
 bool SetEmergencyOn()override{std::lock_guard<std::mutex> l(w->mutex);if(!w->pc[set])return false;w->direction[set]|=1;return true;}
 bool SetSpeed(std::uint16_t value)override{std::lock_guard<std::mutex> l(w->mutex);if(!w->pc[set])return false;CHECK(value<=60);w->speed[set]=value;++w->speedWrites;return true;}
 bool SetControlBits(std::uint16_t mask,std::uint16_t value)override{std::lock_guard<std::mutex> l(w->mutex);if(!w->pc[set])return false;w->direction[set]=(w->direction[set]&~mask)|value;return true;}
 bool SetPortMask(std::uint16_t value,std::function<bool()> const& enabled={})override{
  if(value&&w->blockOn){w->entered=true;while(!w->release)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(value&&enabled&&!enabled())return false;
  std::lock_guard<std::mutex> l(w->mutex);if(!w->pc[set])return false;CHECK(!(value&0xFF00));
  if(value){CHECK(w->speed[set]>0);++w->onWrites;}else ++w->offWrites;
  w->mask[set]=(w->mask[set]&0xFF00)|value;if(value&&w->failOn){w->failOn=false;return false;}return true;
 }
};
int main(){
 ServiceOptions o;o.automaticMotion=true;o.pollIntervalMs=20;o.maxSampleAgeMs=500;o.riseWorkMm=o.fallWorkMm=2;o.riseStopMm=o.fallStopMm=6;
 o.plcs.resize(2);o.plcs[0].ip="one";o.plcs[1].ip="two";
 ChannelConfig c;c.scalingVerified=c.areaVerified=true;c.strokeMmPerCount=.1;c.pressureBarPerCount=1;c.effectiveAreaMm2=100;c.strokeLimitMm=500;c.maximumPressureBar=700;c.maximumLoadTon=10;c.workingLoadTon=5;
 c.sensor=0;o.plcs[0].channels.push_back(c);c.sensor=2;o.plcs[0].channels.push_back(c);c.sensor=1;o.plcs[1].channels.push_back(c);
 // Exercise independent feedback cases without sleeps or a physical PLC.
 EquipmentSnapshot sample;sample.plcs.resize(2);
 auto populate=[&](std::uint64_t at){for(std::size_t set=0;set<2;++set){auto& plc=sample.plcs[set];plc.connected=plc.readOk=plc.usable=plc.pcSelected=plc.pumpOn=true;plc.sampledAtMs=at;plc.ports.resize(o.plcs[set].channels.size());plc.mapWords={{51,2,true,at},{52,0,true,at}};for(auto& port:plc.ports){port.strokeValid=port.pressureValid=port.loadValid=true;port.pressureBar=100;port.rawPressure=100;port.loadTon=.1;port.absoluteMm=0;port.rawStroke=0;}}};
 populate(100);DiagnosticRequest pure;pure.targetMm=50;pure.selected[0][0]=pure.selected[0][1]=pure.selected[1][0]=true;
 AutomaticDistanceSequence a;CHECK(a.Begin(o,pure,sample,100));auto applied=a.Desired();a.Commit(applied,101);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=120;}
 sample.plcs[0].ports[0].absoluteMm=3;sample.plcs[0].ports[0].rawStroke=30;a.Build(sample,120);CHECK(a.Desired().masks[0]==4&&a.Desired().masks[1]==2);applied=a.Desired();a.Commit(applied,121);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=140;for(auto& port:sample.plcs[set].ports){port.absoluteMm=2;port.rawStroke=20;}}
 sample.plcs[0].ports[0].absoluteMm=3;sample.plcs[0].ports[0].rawStroke=30;a.Build(sample,140);CHECK(a.Desired().masks[0]==5);applied=a.Desired();a.Commit(applied,141);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=160;}
 sample.plcs[0].ports[0].absoluteMm=10;sample.plcs[0].ports[0].rawStroke=100;a.Build(sample,160);CHECK(a.Stopping()&&a.Desired().masks[0]==0);
 a.Stop("Operator STOP",SequencePhase::Stopped);AutomaticDistanceSequence::Output zero;a.Commit(zero,180);
 for(auto& plc:sample.plcs){plc.mapWords[1].value=0;plc.sampledAtMs=200;}a.ObserveStop(sample,200);for(auto& plc:sample.plcs)plc.sampledAtMs=220;a.ObserveStop(sample,220);CHECK(a.Phase()==SequencePhase::Fault&&a.RequiresReset()&&a.Reason().find("synchronization")!=std::string::npos);a.Stop("Operator STOP",SequencePhase::Stopped);CHECK(a.Phase()==SequencePhase::Fault&&a.RequiresReset());
 populate(200);AutomaticDistanceSequence noMotion;CHECK(noMotion.Begin(o,pure,sample,200));applied=noMotion.Desired();noMotion.Commit(applied,201);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=5300;}noMotion.Build(sample,5300);CHECK(noMotion.Stopping()&&noMotion.Reason().find("did not move")!=std::string::npos);
 populate(6000);AutomaticDistanceSequence rawLimit;CHECK(rawLimit.Begin(o,pure,sample,6000));applied=rawLimit.Desired();rawLimit.Commit(applied,6001);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=6020;}sample.plcs[0].ports[0].absoluteMm=40;sample.plcs[0].ports[0].rawStroke=510;rawLimit.Build(sample,6020);CHECK(rawLimit.Stopping()&&rawLimit.Reason().find("overshoot")!=std::string::npos);
 populate(6500);for(auto& plc:sample.plcs)for(auto& port:plc.ports){port.absoluteMm=100;port.rawStroke=1000;}AutomaticDistanceSequence wrongWay;CHECK(wrongWay.Begin(o,pure,sample,6500));applied=wrongWay.Desired();wrongWay.Commit(applied,6501);for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=applied.masks[set];sample.plcs[set].sampledAtMs=6520;}sample.plcs[0].ports[0].rawStroke=990;wrongWay.Build(sample,6520);CHECK(wrongWay.Stopping()&&wrongWay.Reason().find("opposite direction")!=std::string::npos);
 populate(7000);sample.plcs[0].pumpOn=false;AutomaticDistanceSequence rejected;CHECK(!rejected.Begin(o,pure,sample,7000));sample.plcs[0].pumpOn=true;sample.plcs[0].ports[0].pressureBar=700;CHECK(!rejected.Begin(o,pure,sample,7000));
 // A port's working limit warns only; its maximum limit stops at equality.
 populate(8000);sample.plcs[0].ports[0].loadTon=5;
 AutomaticDistanceSequence workingOnly;CHECK(workingOnly.Begin(o,pure,sample,8000));
 populate(8100);sample.plcs[0].ports[0].loadTon=9.99;
 AutomaticDistanceSequence belowMax;CHECK(belowMax.Begin(o,pure,sample,8100));
 auto loadApplied=belowMax.Desired();belowMax.Commit(loadApplied,8101);
 for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=loadApplied.masks[set];sample.plcs[set].sampledAtMs=8120;}
 sample.plcs[0].ports[0].loadTon=10;belowMax.Build(sample,8120);
 CHECK(belowMax.Stopping()&&belowMax.Desired().masks[0]==0&&belowMax.Desired().masks[1]==0);
 populate(8200);sample.plcs[0].ports[0].loadTon=10;
 AutomaticDistanceSequence atMax;CHECK(!atMax.Begin(o,pure,sample,8200));
 // 16000 counts / 500 mm: each completed START adds another 125 mm.
 auto scaled=o;for(auto& plc:scaled.plcs)for(auto& c:plc.channels)c.strokeMmPerCount=500.0/16000;
 DiagnosticRequest step;step.targetMm=125;step.selected[0][0]=true;AutomaticDistanceSequence repeated;
 populate(9000);CHECK(repeated.Begin(scaled,step,sample,9000));CHECK(repeated.Origin(0,0)==0&&repeated.Target(0,0)==125);
 auto moveTo=[&](unsigned raw,double mm,std::uint64_t at){auto out=repeated.Desired();repeated.Commit(out,at);
    for(int set=0;set<2;++set){sample.plcs[set].mapWords[1].value=out.masks[set];sample.plcs[set].sampledAtMs=at+20;}
    sample.plcs[0].ports[0].rawStroke=raw;sample.plcs[0].ports[0].absoluteMm=mm;repeated.Build(sample,at+20);
    CHECK(repeated.Stopping());AutomaticDistanceSequence::Output off;repeated.Commit(off,at+21);
    for(auto& plc:sample.plcs){plc.mapWords[1].value=0;plc.sampledAtMs=at+40;}repeated.ObserveStop(sample,at+40);
    for(auto& plc:sample.plcs)plc.sampledAtMs=at+60;repeated.ObserveStop(sample,at+60);
    CHECK(repeated.Phase()==SequencePhase::Completed&&repeated.StopConfirmed());
 };
 moveTo(4000,125,9001);CHECK(repeated.HasPositionTarget(0,0));
 CHECK(repeated.Begin(scaled,step,sample,9080));CHECK(repeated.Origin(0,0)==125&&repeated.Target(0,0)==250);
 moveTo(8000,250,9081);
 auto doubled=scaled;for(auto& plc:doubled.plcs)for(auto& c:plc.channels){c.strokeMmPerCount=1000.0/16000;c.strokeLimitMm=1000;}
 CHECK(repeated.Reconfigure(doubled));CHECK(repeated.Phase()==SequencePhase::Completed&&repeated.Origin(0,0)==250&&repeated.Target(0,0)==500);
 CHECK(repeated.Reconfigure(scaled));step.targetMm=300;CHECK(!repeated.Begin(scaled,step,sample,9160));
 populate(9400);sample.plcs[0].ports[0].rawStroke=14400;sample.plcs[0].ports[0].absoluteMm=450;
 DiagnosticRequest nearLimit;nearLimit.selected[0][0]=true;nearLimit.targetMm=100;AutomaticDistanceSequence limited;
 CHECK(!limited.Begin(scaled,nearLimit,sample,9400));nearLimit.targetMm=50;CHECK(limited.Begin(scaled,nearLimit,sample,9400));CHECK(limited.Target(0,0)==500);
 auto w=std::make_shared<World>();EquipmentControlService service(o,[&](auto const& p,int){return std::make_unique<Reader>(w,p.ip=="one"?0:1);});
 service.Start();Wait([&]{return service.Latest().plcs[1].ports[0].loadValid;});
 DiagnosticRequest r;r.automatic=true;r.targetMm=100;r.selected[0][0]=r.selected[0][1]=r.selected[1][0]=true;
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});CHECK(w->Mask(0)==5&&w->Mask(1)==2&&w->Hz(0)==20&&w->Hz(1)==20);
 CHECK(!service.Submit("DownRequested",r));CHECK(!service.Submit("PumpToggle:1",r));CHECK(!service.Submit("StartRequested",r));
 w->Position(85);Wait([&]{return w->Hz(0)==16&&w->Hz(1)==16;});
 r.liftingHz[0]=12;CHECK(service.Submit("LiftingSpeed:1:12",r));Wait([&]{return w->Hz(0)==9;});
 w->Position(97);Wait([&]{return w->Hz(0)==4&&w->Hz(1)==7;});
 {std::lock_guard<std::mutex> l(w->mutex);w->pressureNoise=true;}w->Position(99.6);
 Wait([&]{return w->Mask(0)==0&&w->Mask(1)==0&&service.Latest().sequencePhase==SequencePhase::Stopping;});
 std::this_thread::sleep_for(std::chrono::milliseconds(100));CHECK(!service.Latest().sequenceStopConfirmed&&service.Latest().sequenceBusy);
 {std::lock_guard<std::mutex> l(w->mutex);w->pressureNoise=false;}
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Completed;});CHECK(service.Latest().sequenceStopConfirmed);
 CHECK(service.Latest().sequenceNavigationLocked);CHECK(service.Submit("ModeManual",r)&&service.Submit("ModePressure",r)&&service.Submit("ModeDisplacement",r)&&service.Submit("ModeAuto",r));CHECK(!service.Submit("Back",r));
 r.locked=true;CHECK(!service.Submit("ModeManual",r));r.locked=false;
 r.automatic=false;CHECK(service.Submit("Back",r));r.locked=true;CHECK(!service.Submit("Back",r));r.locked=false;r.automatic=true;
 CHECK(service.Submit("ResetSequence",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Idle&&!service.Latest().sequenceNavigationLocked;});
 CHECK(service.Submit("ModeAuto",r));
 // Lowering uses a decreasing Stroke and the lowering Hz cap.
 w->Directions(4);r.lowering=true;r.targetMm=50;Wait([&]{return (service.Latest().plcs[0].mapWords[1].valid);});
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});w->Position(64.6);Wait([&]{return w->Hz(0)==16;});w->Position(50);
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Completed;});CHECK(service.Latest().sequenceStopConfirmed&&w->Mask(0)==0&&w->Mask(1)==0);
 // Pause retains absolute targets; resume cannot turn the same travel into a new goal.
 w->Directions(2);r.lowering=false;r.targetMm=20;r.liftingHz[0]=20;CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});
 w->Position(55);Wait([&]{return service.Latest().plcs[0].ports[0].absoluteMm==55;});CHECK(service.Submit("StopRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Stopped&&service.Latest().sequenceStopConfirmed;});
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});CHECK(std::abs(service.Latest().plcs[0].ports[0].sequenceTargetMm-70)<1e-9);
 w->Position(69.6);Wait([&]{return service.Latest().sequencePhase==SequencePhase::Completed;});
 // PC-mode loss keeps an OFF-only cleanup pending, with no reconnect ON replay.
 r.targetMm=20;CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});const auto on=w->Ons();
 {std::lock_guard<std::mutex> l(w->mutex);w->pc[0]=false;}
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Stopping&&w->Mask(1)==0;});CHECK(w->Mask(0)==5&&!service.Latest().sequenceStopConfirmed);
 {std::lock_guard<std::mutex> l(w->mutex);w->pc[0]=true;}
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Fault&&service.Latest().sequenceStopConfirmed;});CHECK(w->Ons()==on&&w->Mask(0)==0&&service.Latest().automaticResetRequired);CHECK(!service.Submit("StartRequested",r));
 CHECK(service.Submit("ResetSequence",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Idle;});
 // Uncertain ON ACK clears all PLCs and must never retry ON.
 {std::lock_guard<std::mutex> l(w->mutex);w->failOn=true;}
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Fault&&service.Latest().sequenceStopConfirmed;});CHECK(w->Mask(0)==0&&w->Mask(1)==0);
 CHECK(service.Submit("ResetSequence",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Idle;});
 // STOP accepted while an ON operation is blocked suppresses the late write.
 const auto before=w->Ons();w->blockOn=true;w->release=false;w->entered=false;CHECK(service.Submit("StartRequested",r));Wait([&]{return w->entered.load();});CHECK(service.Submit("StopRequested",r));w->release=true;
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Stopped&&service.Latest().sequenceStopConfirmed;});CHECK(w->Ons()==before);w->blockOn=false;
 CHECK(service.Submit("FullStopRequested",r));Wait([&]{return service.Latest().automaticResetRequired&&service.Latest().sequenceStopConfirmed;});CHECK(!service.Submit("StartRequested",r));CHECK(service.Submit("ResetSequence",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Idle;});
 // PC emergency cannot be reset while its PLC bit is still ON. No OFF bit is invented.
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});CHECK(service.Submit("EmergencyRequested",r));
 Wait([&]{return service.Latest().sequencePhase==SequencePhase::Emergency&&service.Latest().sequenceStopConfirmed;});CHECK(w->Mask(0)==0&&w->Mask(1)==0);CHECK(service.Submit("ResetSequence",r));std::this_thread::sleep_for(std::chrono::milliseconds(60));CHECK(service.Latest().automaticResetRequired);
 w->Directions(2);Wait([&]{auto s=service.Latest();for(auto const& plc:s.plcs)for(auto const& word:plc.mapWords)if(word.address==51&&(word.value&1))return false;return true;});
 CHECK(service.Submit("ResetSequence",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Idle&&!service.Latest().pcEmergencyRequested;});
 // Direct service shutdown must clear outputs without claiming unobserved physical stop.
 CHECK(service.Submit("StartRequested",r));Wait([&]{return service.Latest().sequencePhase==SequencePhase::Running;});service.Stop();CHECK(w->Mask(0)==0&&w->Mask(1)==0);CHECK(!service.Latest().sequenceStopConfirmed);
 EquipmentControlService idleReset(o,[&](auto const& p,int){return std::make_unique<Reader>(w,p.ip=="one"?0:1);});idleReset.Start();Wait([&]{return idleReset.Latest().plcs[1].readOk;});CHECK(idleReset.Submit("EmergencyRequested",r));
 Wait([&]{auto s=idleReset.Latest();if(!s.pcEmergencyRequested)return false;for(auto const& plc:s.plcs){bool on=false;for(auto const& word:plc.mapWords)if(word.address==51&&word.valid&&(word.value&1))on=true;if(!on)return false;}return true;});w->Directions(2);
 Wait([&]{auto s=idleReset.Latest();for(auto const& plc:s.plcs)for(auto const& word:plc.mapWords)if(word.address==51&&(word.value&1))return false;return true;});CHECK(idleReset.Submit("ResetSequence",r));Wait([&]{auto snapshot=idleReset.Latest();static int checks=0;if(++checks==120)std::cerr<<"Idle reset state="<<static_cast<int>(snapshot.sequencePhase)<<" emergency="<<snapshot.pcEmergencyRequested<<" busy="<<snapshot.sequenceBusy<<" message="<<snapshot.controlMessage<<"\n";return snapshot.sequencePhase==SequencePhase::Idle&&!snapshot.pcEmergencyRequested;});idleReset.Stop();
 // LOAD SET uses per-port measured loads, including at START and during motion.
 auto guarded=o;guarded.loadToleranceEnabled=true;guarded.loadTolerancePercent=10;
 {std::lock_guard<std::mutex> l(w->mutex);w->pressure=100;}
 EquipmentControlService loadService(guarded,[&](auto const& p,int){return std::make_unique<Reader>(w,p.ip=="one"?0:1);});
 loadService.Start();Wait([&]{return loadService.Latest().plcs[1].ports[0].loadValid;});
 CHECK(loadService.Submit("LoadSet",r));Wait([&]{return loadService.Latest().plcs[0].ports[0].referenceLoadTon>0;});
 CHECK(loadService.Submit("StartRequested",r));Wait([&]{return loadService.Latest().sequencePhase==SequencePhase::Running;});
 {std::lock_guard<std::mutex> l(w->mutex);w->pressure=110;}
 Wait([&]{return loadService.Latest().plcs[1].ports[0].rawPressure==110;});CHECK(!loadService.Latest().loadReferenceTripped);
 {std::lock_guard<std::mutex> l(w->mutex);w->pressure=111;}
 Wait([&]{auto v=loadService.Latest();return v.loadReferenceTripped&&v.sequencePhase==SequencePhase::Fault&&v.sequenceStopConfirmed;});
 CHECK(w->Mask(0)==0&&w->Mask(1)==0&&!loadService.Submit("StartRequested",r));
 CHECK(loadService.Submit("ResetSequence",r));Wait([&]{auto v=loadService.Latest();return !v.loadReferenceTripped&&v.sequencePhase==SequencePhase::Idle;});loadService.Stop();
 // Signed negative P.T is clamped only after PRESSURE ZERO; raw WORD is retained.
 for(auto& plc:guarded.plcs)for(auto& c:plc.channels)c.pressureSigned=true;
 {std::lock_guard<std::mutex> l(w->mutex);w->pressure=65526;}
 EquipmentControlService zeroService(guarded,[&](auto const& p,int){return std::make_unique<Reader>(w,p.ip=="one"?0:1);});
 zeroService.Start();Wait([&]{return zeroService.Latest().plcs[1].readOk;});CHECK(!zeroService.Latest().plcs[0].ports[0].pressureValid);
 CHECK(zeroService.Submit("PressureZero",r));Wait([&]{auto v=zeroService.Latest().plcs[0].ports[0];return v.pressureValid&&v.pressureBar==0&&v.displayPressureBar==0;});
 CHECK(zeroService.Latest().plcs[0].ports[0].rawPressure==65526);
 {std::lock_guard<std::mutex> l(w->mutex);w->pressure=50;}
 Wait([&]{return zeroService.Latest().plcs[0].ports[0].pressureBar==50;});
 CHECK(zeroService.Submit("HeightZero",r));Wait([&]{return zeroService.Latest().plcs[0].ports[0].displayRelativeMm==0;});
 CHECK(zeroService.Submit("ResetSequence",r));Wait([&]{auto v=zeroService.Latest().plcs[0].ports[0];return v.displayRelativeMm==v.displayAbsoluteMm;});zeroService.Stop();
 std::cout<<"PASS: real automatic worker D050 deceleration / D052 mapped ports, UP/DOWN, P.T+Stroke stop confirmation, pause/resume, speed cap, PC gate, uncertain ACK, stop-before-enable, full-stop reset, shutdown OFF\n";
}
