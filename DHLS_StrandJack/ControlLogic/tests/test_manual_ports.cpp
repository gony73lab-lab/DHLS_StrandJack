#include <crtdbg.h>
#include <cstdlib>
#include "../EquipmentControlService.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace DHLS_StrandJack::Control;
#undef assert
#define assert(condition) do {if(!(condition)){std::cerr<<"FAILED line "<<__LINE__<<": "<<#condition<<std::endl;std::exit(1);}} while(false)
static int waitStage=0;
struct State{std::atomic<unsigned> pressure{0},mask{0xA500},direction{2},onWrites{0},offWrites{0};std::atomic<bool> pc{true},failOn{false},blockOn{false},entered{false},release{false};};
struct Reader:IPlcReadTransport{
 std::shared_ptr<State> p;explicit Reader(std::shared_ptr<State> s):p(s){}
 bool Connect()override{return true;}void Disconnect()override{}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{out.assign(n,0);if(a==11)out[0]=static_cast<std::uint16_t>(p->pressure.load());if(a==10)out[0]=p->pc?2:0;if(a==51)out[0]=static_cast<std::uint16_t>(p->direction);if(a==52)out[0]=static_cast<std::uint16_t>(p->mask);return true;}
 bool SetPortMask(std::uint16_t mask,std::function<bool()>const& held={})override{
  if(mask&&p->blockOn){p->entered=true;while(!p->release)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(!p->pc||(mask&&held&&!held()))return false;
  p->mask=(p->mask.load()&0xFF00)|mask;if(mask)++p->onWrites;else++p->offWrites;
  return !(mask&&p->failOn);
 }
};
template<class F>void Wait(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);++waitStage;while(!f()){if(std::chrono::steady_clock::now()>=end){std::cerr<<"Wait stage timeout "<<waitStage<<"\n";assert(false);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
int main(){
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 ServiceOptions o;o.pollIntervalMs=20;o.maxSampleAgeMs=1000;o.plcs.resize(2);o.plcs[0].ip="one";o.plcs[1].ip="two";
 ChannelConfig c;c.sensor=0;c.scalingVerified=c.areaVerified=true;c.pressureBarPerCount=1;c.strokeMmPerCount=1;c.effectiveAreaMm2=98066.5;c.maximumLoadTon=10;c.workingLoadTon=8;c.maximumPressureBar=700;c.strokeLimitMm=500;o.plcs[0].channels.push_back(c);c.sensor=2;o.plcs[0].channels.push_back(c);c.sensor=1;o.plcs[1].channels.push_back(c);
 auto a=std::make_shared<State>(),b=std::make_shared<State>();
 EquipmentControlService service(o,[&](PlcConfig const&p,int){return std::make_unique<Reader>(p.ip=="one"?a:b);});
 service.Start();Wait([&]{return service.Latest().plcs[1].readOk;});
 DiagnosticRequest r;r.automatic=false;r.selected[0][0]=r.selected[0][1]=r.selected[1][0]=true;
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().manualRunning;});assert(a->mask==0xA505&&b->mask==0xA502);
 assert(!service.Submit("UpRequested",r));assert(!service.Submit("StartRequested",r));auto automatic=r;automatic.automatic=true;assert(!service.Submit("StartRequested",automatic));
 assert(service.Submit("StopRequested",r));Wait([&]{auto s=service.Latest();return !s.manualRunning&&!s.manualStopPending&&a->mask==0xA500&&b->mask==0xA500;});
 // Release while ON transmission is waiting: callback must prevent any later enable.
 const auto applied=a->onWrites.load();a->blockOn=true;a->entered=false;a->release=false;
 assert(service.Submit("StartRequested",r));Wait([&]{return a->entered.load();});assert(service.Submit("StopRequested",r));a->release=true;
 Wait([&]{return !service.Latest().manualStopPending;});assert(a->onWrites==applied&&a->mask==0xA500&&b->mask==0xA500);a->blockOn=false;
 // Partial/uncertain ACK: clear every PLC; never replay an ON request.
 b->failOn=true;assert(service.Submit("StartRequested",r));Wait([&]{return b->onWrites>=2;});
 Wait([&]{return !service.Latest().manualStopPending&&a->mask==0xA500&&b->mask==0xA500;});assert(!service.Latest().manualRunning);b->failOn=false;
 // PC permission lost while held: no OFF writes to that PLC, stop remains unconfirmed.
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().manualRunning;});a->pc=false;
 Wait([&]{return service.Latest().manualStopPending&&!service.Latest().manualRunning;});assert(a->mask==0xA505);assert(!service.Submit("StartRequested",r));
 const auto onCount=a->onWrites.load();a->pc=true;Wait([&]{return !service.Latest().manualStopPending&&a->mask==0xA500&&b->mask==0xA500;});assert(a->onWrites==onCount);
 // Working threshold continues; maximum equality clears every selected PLC.
 a->pressure=8;Wait([&]{return service.Latest().plcs[0].ports[0].loadTon==8;});
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().manualRunning;});
 a->pressure=10;Wait([&]{auto v=service.Latest();return !v.manualRunning&&!v.manualStopPending&&a->mask==0xA500&&b->mask==0xA500;});
 const auto overloadOns=a->onWrites.load();assert(service.Submit("StartRequested",r));
 std::this_thread::sleep_for(std::chrono::milliseconds(80));assert(a->onWrites==overloadOns);
 a->pressure=0;Wait([&]{return service.Latest().plcs[0].ports[0].loadTon==0;});
 // Native shutdown must clear an active manual selection before closing transports.
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().manualRunning;});service.Stop();assert(a->mask==0xA500&&b->mask==0xA500&&!service.Latest().manualStopPending);
 // Measured load reference also protects the real manual hold path.
 auto loadOptions=o;loadOptions.automaticMotion=true;loadOptions.loadToleranceEnabled=true;loadOptions.loadTolerancePercent=10;
 a->pressure=4;b->pressure=4;EquipmentControlService guarded(loadOptions,[&](PlcConfig const&p,int){return std::make_unique<Reader>(p.ip=="one"?a:b);});
 DiagnosticRequest selected;selected.automatic=false;selected.selected[0][0]=true;
 guarded.Start();Wait([&]{return guarded.Latest().plcs[0].ports[0].loadValid;});
 assert(guarded.Submit("LoadSet",selected));Wait([&]{return guarded.Latest().plcs[0].ports[0].referenceLoadTon==4;});
 assert(guarded.Submit("StartRequested",selected));Wait([&]{return guarded.Latest().manualRunning;});a->pressure=5;
 Wait([&]{auto v=guarded.Latest();return v.loadReferenceTripped&&!v.manualRunning&&!v.manualStopPending&&a->mask==0xA500&&b->mask==0xA500;});
 assert(!guarded.Submit("StartRequested",selected));assert(guarded.Submit("ResetSequence",selected));
 Wait([&]{return !guarded.Latest().loadReferenceTripped;});guarded.Stop();
 std::cout<<"PASS: mapped D052 hold/release, cancellation before send, partial ACK cleanup, PC gate stop pending/recovery, no ON replay, shutdown OFF\n";
}
