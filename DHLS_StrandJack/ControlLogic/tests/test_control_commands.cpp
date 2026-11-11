#include "../EquipmentControlService.h"
#include "../PlcDirectionFeedback.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace DHLS_StrandJack::Control;
struct State {std::atomic<unsigned> word{0x8000},writes{0},speed{0};std::atomic<bool> fail{false},speedFail{false},pcSelected{true};};
struct Reader:IPlcReadTransport {
 std::shared_ptr<State> s;explicit Reader(std::shared_ptr<State> v):s(v){}
 bool Connect()override{return !s->fail;}void Disconnect()override{}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{
  out.assign(n,0);if(a==10)out[0]=s->pcSelected?2:0;if(a==51)out[0]=static_cast<std::uint16_t>(s->word.load());
  if(a==11)for(auto& v:out)v=10;
  return !s->fail;
 }
 bool SetSpeed(std::uint16_t v)override{if(s->speedFail)return false;s->speed=v;return true;}
 bool SetControlBits(std::uint16_t mask,std::uint16_t value)override{
  assert(!s->fail);assert(mask==6||mask==8);if(mask==6&&value)assert(s->speed==(value==2?25:15));s->word=(s->word.load()&~mask)|value;++s->writes;return true;
 }
};
template<class F>void Wait(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!f()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
int main(){
 EquipmentSnapshot feedback;feedback.plcs.resize(2);
 for(auto& plc:feedback.plcs){plc.connected=true;plc.mapWords.push_back({51,2,true,1});}
 assert(ConfirmedPlcDirection(feedback)==2);feedback.plcs[1].mapWords[0].value=4;assert(ConfirmedPlcDirection(feedback)==0);
 feedback.plcs[0].mapWords[0].value=4;assert(ConfirmedPlcDirection(feedback)==4);
 feedback.plcs[1].mapWords[0].valid=false;assert(ConfirmedPlcDirection(feedback)==0);
 feedback.plcs[1].mapWords[0].valid=true;feedback.plcs[0].mapWords[0].value=6;assert(ConfirmedPlcDirection(feedback)==0);
 ServiceOptions o;o.pollIntervalMs=20;PlcConfig p;p.ip="fake";ChannelConfig c;c.sensor=0;c.scalingVerified=true;c.pressureBarPerCount=c.strokeMmPerCount=1;p.channels.push_back(c);o.plcs.push_back(p);
 auto state=std::make_shared<State>();EquipmentControlService service(o,[state](auto const&,int){return std::make_unique<Reader>(state);});
 service.Start();Wait([&]{return service.Latest().plcs[0].readOk;});DiagnosticRequest r;r.selected[0][0]=true;r.liftingHz[0]=25;r.loweringHz[0]=15;
 state->pcSelected=false;Wait([&]{return !service.Latest().plcs[0].pcSelected;});
 auto monitorOnlySequence=service.Latest().sequence;
 assert(!service.Submit("UpRequested",r));assert(!service.Submit("LiftingSpeed:1:25",r));assert(!service.Submit("PumpToggle:1",r));
 Wait([&]{return service.Latest().sequence>monitorOnlySequence+1;});assert(state->writes==0&&service.Latest().plcs[0].readOk);
 state->pcSelected=true;Wait([&]{return service.Latest().plcs[0].pcSelected;});assert(state->writes==0);
 assert(!service.Submit("LiftingSpeed:",r));assert(!service.Submit("LoweringSpeed:1",r));
 // UI selection cannot replace PLC direction feedback when D051 bits are OFF.
 auto stoppedSequence=service.Latest().sequence;r.liftingHz[0]=27;assert(service.Submit("LiftingSpeed:1:27",r));Wait([&]{return service.Latest().sequence>stoppedSequence+1;});assert(state->speed==0&&state->word==0x8000&&state->writes==0);
 r.liftingHz[0]=25;
 assert(service.Submit("UpRequested",r));Wait([&]{return state->word==0x8002;});
 r.liftingHz[0]=28;assert(service.Submit("LiftingSpeed:1:28",r));Wait([&]{return state->speed==28;});assert(!service.Latest().monitoring&&state->word==0x8002);
 r.liftingHz[0]=25;assert(service.Submit("LiftingSpeed:1:25",r));Wait([&]{return state->speed==25;});
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().monitoring;});
 r.liftingHz[0]=33;assert(service.Submit("LiftingSpeed:1:33",r));Wait([&]{return state->speed==33;});
 assert(state->word==0x8002&&state->writes==2&&service.Latest().monitoring);
 auto before=service.Latest().sequence;r.loweringHz[0]=12;
 assert(service.Submit("LoweringSpeed:1:12",r));Wait([&]{return service.Latest().sequence>before+1;});assert(state->speed==33);
 r.locked=true;assert(!service.Submit("LiftingSpeed:1:40",r));r.locked=false;
 service.Submit("StopRequested",r);Wait([&]{return !service.Latest().monitoring;});r.loweringHz[0]=15;r.liftingHz[0]=25;
 assert(service.Submit("DownRequested",r));Wait([&]{return state->word==0x8004;});
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().monitoring;});
 r.loweringHz[0]=18;assert(service.Submit("LoweringSpeed:1:18",r));Wait([&]{return state->speed==18;});
 assert(state->word==0x8004&&state->writes==4&&service.Latest().monitoring);
 before=service.Latest().sequence;r.liftingHz[0]=40;
 assert(service.Submit("LiftingSpeed:1:40",r));Wait([&]{return service.Latest().sequence>before+1;});assert(state->speed==18);
 service.Submit("StopRequested",r);Wait([&]{return !service.Latest().monitoring;});r.liftingHz[0]=25;r.loweringHz[0]=15;

 assert(service.Submit("PumpToggle:1",r));Wait([&]{return state->word==0x800C;});
 assert(service.Submit("PumpToggle:1",r));Wait([&]{return state->word==0x8004;});
 // A D50 failure may clear the old direction but must never turn a new direction ON.
 state->speedFail=true;assert(service.Submit("UpRequested",r));
 Wait([&]{return service.Latest().controlMessage.find("failed")!=std::string::npos;});
 assert((state->word&6)==0);assert(state->writes==7);state->speedFail=false;
 r.locked=true;assert(!service.Submit("PumpToggle:1",r));r.locked=false;
 assert(service.Submit("StartRequested",r));Wait([&]{return service.Latest().monitoring;});
 assert(!service.Submit("PumpToggle:1",r));assert(!service.Submit("UpRequested",r));assert(!service.Submit("DownRequested",r));assert(state->writes==7);
 service.Submit("StopRequested",r);Wait([&]{return !service.Latest().monitoring;});
 state->fail=true;Wait([&]{return !service.Latest().plcs[0].readOk;});
 assert(!service.Submit("PumpToggle:1",r));assert(state->writes==7); // Unknown permission is rejected before queuing.
 service.Stop();std::cout<<"PASS: serialized direction exclusion, per-PLC HPU toggle, lock/running/disconnect interlocks, no reconnect replay\n";
}
