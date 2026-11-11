#include "../EquipmentControlService.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace DHLS_StrandJack::Control;
struct Input {std::atomic<unsigned> command{0x900E},selection{1},inverter{0},pt{0},stroke{0},stops{0};std::atomic<bool> fail{false},stopFail{false};};
struct Reader:IPlcReadTransport{
 std::shared_ptr<Input> p;explicit Reader(std::shared_ptr<Input> value):p(value){}
 bool Connect()override{return !p->fail;}void Disconnect()override{}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{
  out.assign(n,0);if(p->fail)return false;
  if(a==10)out[0]=2;
  if(a==51)out[0]=static_cast<std::uint16_t>(p->command.load());
  if(a==52)out[0]=static_cast<std::uint16_t>(p->selection.load());
  if(a==55)out[0]=static_cast<std::uint16_t>(p->inverter.load());
  if(a==56)out[0]=static_cast<std::uint16_t>(p->pt.load());
  if(a==59)out[0]=static_cast<std::uint16_t>(p->stroke.load());
  return true;
 }
 bool StopOutputs()override{++p->stops;if(p->stopFail)return false;p->command=p->command.load()&~14u;return true;}
};
template<class F>void Wait(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!f()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
int main(){
 ServiceOptions options;options.pollIntervalMs=20;options.maxSampleAgeMs=1000;
 PlcConfig one;one.ip="one";ChannelConfig channel;channel.scalingVerified=true;channel.pressureBarPerCount=channel.strokeMmPerCount=1;one.channels.push_back(channel);options.plcs={one,one};options.plcs[1].ip="two";
 auto a=std::make_shared<Input>(),b=std::make_shared<Input>();
 EquipmentControlService svc(options,[&](auto const& c,int){return std::make_unique<Reader>(c.ip=="one"?a:b);});svc.Start();Wait([&]{return svc.Latest().faultStatesKnown;});
 assert(!svc.Latest().faultActive);assert(a->stops==0&&b->stops==0); // D52 selection ON is normal.
 DiagnosticRequest request;request.selected[0][0]=true;
 assert(svc.Submit("StartRequested",request));Wait([&]{return svc.Latest().monitoring;});
 for(int word:{55,56,59})for(int bit=0;bit<16;++bit){
  a->command=b->command=0x900E;
  auto& source=word==55?b->inverter:word==56?b->pt:b->stroke;source=1u<<bit;
  Wait([&]{auto s=svc.Latest();return s.faultActive&&s.faultStopConfirmed;});
  auto snapshot=svc.Latest();assert(!snapshot.monitoring);assert(snapshot.faultBits.size()==1);
  assert(snapshot.faultBits[0].plc==1&&snapshot.faultBits[0].word==word&&snapshot.faultBits[0].bit==bit);
  assert(a->command==0x9000&&b->command==0x9000);
  assert(!svc.Submit("StartRequested",request));assert(!svc.Submit("UpRequested",request));assert(!svc.Submit("PumpToggle:1",request));
  source=0;Wait([&]{return !svc.Latest().faultActive;});assert(!svc.Latest().monitoring);
 }
 b->pt=1;b->stroke=2;Wait([&]{return svc.Latest().faultBits.size()==2;});b->pt=0;Wait([&]{return svc.Latest().faultBits.size()==1;});assert(svc.Latest().faultActive);
 b->fail=true;Wait([&]{auto s=svc.Latest();return s.faultActive&&!s.faultStatesKnown;});assert(!svc.Latest().faultBits[0].confirmed);
 b->stroke=0;b->fail=false;Wait([&]{return !svc.Latest().faultActive;});
 b->stopFail=true;b->command=0x900E;b->inverter=1;Wait([&]{auto s=svc.Latest();return s.faultActive&&!s.faultStopConfirmed;});
 b->inverter=0;Wait([&]{return svc.Latest().faultBits.empty();});assert(svc.Latest().faultActive); // OFF bit alone cannot hide unknown stop.
 b->stopFail=false;Wait([&]{return !svc.Latest().faultActive;});assert(!svc.Latest().monitoring);
 svc.Stop();std::cout<<"PASS: all 48 fault bits, global OFF, other D51 bits preserved, persistent multi-fault/unknown warning, no restart, failed-stop hold, D52 selection exemption\n";
}
