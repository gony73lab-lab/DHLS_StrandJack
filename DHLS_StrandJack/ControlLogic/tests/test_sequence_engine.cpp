#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#include "../Sequence/SequenceEngine.h"
#include "../Sequence/MockPlant.h"
#include "../Sequence/PlcReadOnlyPlant.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace DHLS;
struct Plant final:IPlant {
    std::mutex m;PlantStatus p;std::vector<double> step{0.2,0.05};
    bool stale=false,reject=false,stopFail=false,badAck=false,feedbackMissing=false,freezeAfterApply=false,disconnectAfterApply=false;
    int paused=0,applies=0;
    Plant(){p.communicationOk=p.operationAllowed=true;
        p.jacks={{1,10,1,0,100,true,false,false,true},{2,20,1,0,100,true,false,false,true}};}
    PlantStatus Read()override{std::lock_guard<std::mutex> l(m);
        if(!stale){++p.sampleId;p.sampledAtMs=SequenceNowMs();}return p;}
    bool Apply(std::vector<JackCommand> const& commands)override{
        std::lock_guard<std::mutex> l(m);if(reject)return false;++applies;
        for(auto& j:p.jacks){auto it=std::find_if(commands.begin(),commands.end(),[&](auto const& c){return c.id==j.id;});
            auto d=it==commands.end()?Direction::Hold:it->direction;j.stopped=d==Direction::Hold;
            if(d==Direction::Hold)++paused;
            else {const auto delta=step[j.id-1];j.positionMm+=(d==Direction::Up?delta:-delta);j.pressureBar+=(d==Direction::Up?delta:-delta);}
        }
        p.acceptedCommandId=badAck?0:commands.front().commandId;
        if(freezeAfterApply)stale=true;
        if(disconnectAfterApply)p.communicationOk=false;
        return true;
    }
    bool StopAll()override{std::lock_guard<std::mutex> l(m);if(stopFail)return false;if(!feedbackMissing)for(auto& j:p.jacks)j.stopped=true;return true;}
    PlantStatus Copy(){std::lock_guard<std::mutex> l(m);return p;}
};
void Wait(SequenceEngine& e,State state){for(int i=0;i<600;++i){if(e.GetState()==state&&!e.Busy())return;
    if(e.GetState()==State::Fault&&state!=State::Fault){std::cerr<<"unexpected fault\n";assert(false);}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));}assert(false);}
Request Req(){Request r;r.jackIds={1,2};r.travelMm=2;r.syncPauseMm=.3;r.syncResumeMm=.1;r.syncTripMm=1.5;
    r.positionToleranceMm=.03;r.stopTimeoutMs=80;r.timeoutMs=4000;return r;}
void Invalid(std::function<void(Plant&,Request&)> edit){Plant p;auto r=Req();edit(p,r);SequenceEngine e(p);
    assert(e.Start(r));Wait(e,State::Fault);assert(!e.Start(r));}
int main(){
#ifdef _MSC_VER
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
 _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif

 {Plant p;SequenceEngine e(p);auto r=Req();assert(e.Start(r));Wait(e,State::Completed);
  auto s=p.Copy();assert(std::abs(s.jacks[0].positionMm-12)<.25);assert(std::abs(s.jacks[1].positionMm-22)<.1);assert(p.paused>0);
  assert(e.StopConfirmed());r.mode=Mode::AutoDown;assert(e.Start(r));Wait(e,State::Completed);
  s=p.Copy();assert(std::abs(s.jacks[0].positionMm-10)<.3);assert(std::abs(s.jacks[1].positionMm-20)<.15);}
 {Plant p;SequenceEngine e(p);auto r=Req();assert(e.Start(r));
  std::this_thread::sleep_for(std::chrono::milliseconds(90));e.Stop();Wait(e,State::Stopped);
  assert(e.StopConfirmed());assert(!e.Reset());assert(e.Start(r));Wait(e,State::Completed);auto v=p.Copy();
  assert(std::abs(v.jacks[0].positionMm-12)<.25&&std::abs(v.jacks[1].positionMm-22)<.1);
  r.mode=Mode::AutoDown;assert(e.Start(r));std::this_thread::sleep_for(std::chrono::milliseconds(90));
  e.Stop();Wait(e,State::Stopped);assert(e.Start(r));Wait(e,State::Completed);v=p.Copy();
  assert(std::abs(v.jacks[0].positionMm-10)<.3&&std::abs(v.jacks[1].positionMm-20)<.15);
 }
 {Plant p;SequenceEngine e(p);auto r=Req();r.jackIds={1};assert(e.Start(r));
  std::this_thread::sleep_for(std::chrono::milliseconds(90));e.Stop();Wait(e,State::Stopped);
  const double stopped=p.Copy().jacks[0].positionMm;e.Stop(true);
  assert(e.Start(r));Wait(e,State::Completed);
  assert(std::abs(p.Copy().jacks[0].positionMm-(stopped+2))<.25);
 }
 {Plant p;SequenceEngine e(p);auto r=Req();r.jackIds={1};assert(e.Start(r));Wait(e,State::Completed);assert(p.Copy().jacks[1].positionMm==20);}
 {Plant p;SequenceEngine e(p);auto r=Req();r.mode=Mode::PressureUp;r.targetPressureBar=2;
  assert(e.Start(r));Wait(e,State::Completed);assert(p.Copy().jacks[0].pressureBar>=2);}
 for(auto mode:{Mode::ManualUp,Mode::ManualDown}){Plant p;SequenceEngine e(p);auto r=Req();r.mode=mode;
  assert(e.Start(r));std::this_thread::sleep_for(std::chrono::milliseconds(90));e.Stop();Wait(e,State::Stopped);assert(e.StopConfirmed());}
 for(auto state:{State::Checking,State::Preparing,State::Running}){Plant p;SequenceEngine e(p);auto r=Req();
  e.SetCallback([&](Update const& u){if(u.state==state)e.Stop();});assert(e.Start(r));Wait(e,State::Stopped);assert(p.applies==0);}
 for(auto phase:{State::Checking,State::Preparing,State::Running}){Plant p;SequenceEngine e(p);auto r=Req();
  e.SetCallback([&](Update const& u){if(u.state==phase)e.EmergencyStop();});
  assert(e.Start(r));Wait(e,State::Emergency);assert(e.StopConfirmed());assert(!e.Start(r));assert(e.Reset());assert(e.GetState()==State::Idle);}
 Invalid([](auto&,auto& r){r.jackIds.clear();});
 Invalid([](auto&,auto& r){r.jackIds={1,1};});
 Invalid([](auto&,auto& r){r.jackIds={99};});
 Invalid([](auto&,auto& r){r.travelMm=std::numeric_limits<double>::quiet_NaN();});
 Invalid([](auto&,auto& r){r.travelMm=1000;});
 Invalid([](auto& p,auto&){p.p.jacks[0].positionMm=std::numeric_limits<double>::infinity();});
 Invalid([](auto& p,auto&){p.p.jacks[0].sensorValid=false;});
 Invalid([](auto& p,auto&){p.p.jacks[0].overload=true;});
 Invalid([](auto& p,auto&){p.p.communicationOk=false;});
 Invalid([](auto& p,auto&){p.reject=true;});
 Invalid([](auto& p,auto&){p.badAck=true;});
 Invalid([](auto& p,auto&){p.stale=true;});
 Invalid([](auto& p,auto&){p.freezeAfterApply=true;});
 Invalid([](auto& p,auto&){p.disconnectAfterApply=true;});
 Invalid([](auto& p,auto& r){p.step={0,0};r.mode=Mode::PressureUp;r.targetPressureBar=10;r.noMotionTimeoutMs=30;});
 Invalid([](auto&,auto& r){r.timeoutMs=30;});
 Invalid([](auto& p,auto& r){p.step={2,0};r.syncTripMm=1;r.syncPauseMm=.3;});
 Invalid([](auto& p,auto& r){p.step={0,0};r.noMotionTimeoutMs=30;});
 Invalid([](auto&,auto& r){r.mode=Mode::PressureUp;r.targetPressureBar=1;r.maximumPressureBar=1;});
 Invalid([](auto&,auto& r){r.mode=Mode::AutoDown;r.minimumLoweringPressureBar=1;});
 Invalid([](auto&,auto& r){r.mode=Mode::TargetHeight;});
 {Plant p;p.stopFail=true;SequenceEngine e(p);auto r=Req();e.SetCallback([&](Update const& u){if(u.state==State::Running)e.Stop();});
  assert(e.Start(r));Wait(e,State::Fault);assert(!e.StopConfirmed());assert(!e.Reset());
  {std::lock_guard<std::mutex> l(p.m);p.stopFail=false;}e.EmergencyStop();Wait(e,State::Emergency);assert(e.Reset());}
 {Plant p;p.feedbackMissing=true;p.p.jacks[0].stopped=false;SequenceEngine e(p);auto r=Req();
  e.SetCallback([&](Update const& u){if(u.state==State::Running)e.Stop();});
  assert(e.Start(r));Wait(e,State::Fault);assert(!e.StopConfirmed());assert(!e.Reset());}
 {struct Throwing:IPlant {int stops=0;PlantStatus Read()override{throw std::runtime_error("read failed");}
   bool Apply(std::vector<JackCommand>const&)override{return false;}bool StopAll()override{++stops;return true;}} p;
  SequenceEngine e(p);auto r=Req();assert(e.Start(r));Wait(e,State::Fault);assert(p.stops>0&&!e.StopConfirmed());}
 {Plant p;SequenceEngine e(p);auto r=Req();r.mode=Mode::ManualUp;r.manualSync=false;r.syncPauseMm=0;r.syncTripMm=0;r.syncResumeMm=0;
  assert(e.Start(r));std::this_thread::sleep_for(std::chrono::milliseconds(90));e.Stop();Wait(e,State::Stopped);}
 {std::vector<JackStatus> j{{1,0,0,0,100,true,false,false,true},{2,0,0,0,100,true,false,false,true}};MockPlant p(j);
  assert(p.Apply({{1,Direction::Up,1}}));assert(!p.Apply({{1,Direction::Down,1}}));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));auto s=p.Read();assert(s.jacks[0].positionMm>0&&s.jacks[1].positionMm==0);
  assert(p.Apply({{2,Direction::Up,2}}));std::this_thread::sleep_for(std::chrono::milliseconds(20));s=p.Read();assert(s.jacks[0].stopped);}
 {std::vector<JackStatus> j{{1,0,0,0,100,true,false,false,true}};MockPlant p(j);
  p.Jam(0,true);assert(p.Apply({{1,Direction::Up,1}}));
  std::this_thread::sleep_for(std::chrono::milliseconds(25));auto a=p.Read();
  assert(a.jacks[0].positionMm==0&&a.jacks[0].pressureBar>0);
  assert(p.StopAll());std::this_thread::sleep_for(std::chrono::milliseconds(25));auto b=p.Read();
  assert(b.jacks[0].stopped&&b.jacks[0].pressureBar==a.jacks[0].pressureBar);
  p.Jam(0,false);p.DirectionSpeeds(0,2,1);assert(p.Apply({{1,Direction::Up,2}}));
  std::this_thread::sleep_for(std::chrono::milliseconds(25));a=p.Read();assert(a.jacks[0].positionMm>0);
  p.Fault(false,false);std::this_thread::sleep_for(std::chrono::milliseconds(25));b=p.Read();
  assert(b.jacks[0].stopped&&b.jacks[0].positionMm==a.jacks[0].positionMm);
 }
 {std::vector<JackStatus> j{{1,0,0,0,100,true,false,false,true}};MockPlant p(j);p.DirectionSpeeds(0,2,1);
  SequenceEngine e(p);auto r=Req();r.jackIds={1};r.travelMm=.5;
  assert(e.Start(r));Wait(e,State::Completed);auto a=p.Read();
  assert(e.StopConfirmed()&&a.jacks[0].stopped&&a.jacks[0].positionMm>=.4&&a.jacks[0].positionMm<.65);
 }
 {MockPlant p({{1,10,3,0,100,true,false,false,true}});SequenceEngine e(p);
  p.Fault(true,true);e.EmergencyStop();Wait(e,State::Emergency);
  assert(!e.Reset(false,[&]{return p.ResetMotion();}));
  p.Fault(true,false);assert(e.Reset(false,[&]{return p.ResetMotion();}));
  auto v=p.Read();assert(v.jacks[0].positionMm==0&&v.jacks[0].pressureBar==0);
 }
 {using namespace DHLS_StrandJack::Control;PlcReadOnlyPlant adapter([]{return EquipmentSnapshot{};});
  assert(!adapter.Read().operationAllowed);assert(!adapter.Apply({{1,Direction::Up,1}}));assert(!adapter.StopAll());}
 std::cout<<"PASS: relative goals, selection ownership, pause/resume/trip, pressure, manual stop, cancellation, stale/ACK, faults/reset, read-only PLC gate\n";
}
