#include "../EquipmentControlService.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <iostream>
using namespace DHLS_StrandJack::Control;
static int stage=0; template<class P>void Wait(P pred){++stage;for(int i=0;i<1500;++i){if(pred())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}std::cerr<<"TIMEOUT stage="<<stage<<std::endl;assert(false);}
struct Reader: IPlcReadTransport {
 inline static std::atomic<unsigned> stroke{0},mask{0},pressure{0},direction{10},hz{0};std::atomic<int>& writes;
 Reader(std::atomic<int>& w):writes(w){}bool Connect()override{return true;}void Disconnect()override{}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{
  out.assign(n,0);if(a==10)out[0]=6;if(a==51)out[0]=direction;if(a==52)out[0]=mask;if(a==50)out[0]=hz;
  if(a==11&&n>1){if(mask.load()){if((direction.load()&6)==2)pressure=std::min(100u,pressure.load()+10);else if((direction.load()&6)==4)pressure=pressure.load()>60?pressure.load()-10:50;}for(size_t i=0;i+1<n;i+=2){out[i]=static_cast<std::uint16_t>(pressure.load());out[i+1]=static_cast<std::uint16_t>(stroke.load());}}return true;
 }
 bool SetSpeed(std::uint16_t v)override{++writes;hz=v;return true;}
 bool SetPortMask(std::uint16_t v,std::function<bool()>const& allowed={})override{if(allowed&&!allowed())return false;++writes;mask=v;return true;}
 bool SetControlBits(std::uint16_t m,std::uint16_t v)override{++writes;direction=(direction.load()&~m)|v;return true;}
};
ServiceOptions Options(bool sim){ServiceOptions o;o.simulation=sim;o.automaticMotion=true;o.pollIntervalMs=20;o.riseWorkMm=2;o.riseStopMm=6;o.fallWorkMm=2;o.fallStopMm=6;o.pressureAuto.settleMs=60;
 PlcConfig p;p.ip="127.0.0.1";ChannelConfig c;c.sensor=0;c.strokeLimitMm=500;c.maximumPressureBar=700;c.effectiveAreaMm2=1000;c.maximumLoadTon=100;c.workingLoadTon=80;c.scalingVerified=c.areaVerified=true;c.pressureBarPerCount=.1;c.strokeMmPerCount=.1;p.channels.push_back(c);o.plcs.push_back(p);return o;}
int main(){DiagnosticRequest r;r.pressureMode=r.automatic=true;r.selected[0][0]=true;r.targetBar=10;
 {std::atomic<int>writes{0};auto o=Options(false);Reader::mask=Reader::pressure=Reader::stroke=0;Reader::direction=14;
  EquipmentControlService s(o,[&](auto const&,int){return std::make_unique<Reader>(writes);});s.Start();Wait([&]{return s.Latest().plcs[0].usable;});
  assert(s.Submit("StartRequested",r));Wait([&]{return !s.Latest().sequenceBusy;});assert(writes==0&&Reader::mask==0);
  Reader::direction=10;Wait([&]{auto sample=s.Latest();for(auto const& w:sample.plcs[0].mapWords)if(w.address==51&&w.value==10)return true;return false;});
  assert(s.Submit("StartRequested",r));Wait([&]{return Reader::mask.load()!=0;});Wait([&]{return s.Latest().sequencePhase==SequencePhase::Completed;});
  assert(s.Latest().sequenceStopConfirmed&&Reader::mask==0&&writes>0&&s.Latest().pressureAuto.state==PressureAutoState::Completed);
  Reader::stroke=100;Reader::direction=12;r.lowering=true;r.targetBar=5;
  Wait([&]{auto sample=s.Latest();for(auto const& w:sample.plcs[0].mapWords)if(w.address==51&&w.value==12)return true;return false;});
  assert(s.Submit("StartRequested",r));Wait([&]{return Reader::mask.load()!=0;});Wait([&]{return s.Latest().sequencePhase==SequencePhase::Completed;});
  assert(s.Latest().sequenceStopConfirmed&&Reader::mask==0&&Reader::pressure==50);
  r.lowering=false;r.targetBar=10;s.Stop();
 }
 {std::atomic<int>writes{0};auto o=Options(false);Reader::pressure=Reader::mask=Reader::stroke=0;Reader::direction=10;r.targetBar=100;
  EquipmentControlService s(o,[&](auto const&,int){return std::make_unique<Reader>(writes);});s.Start();Wait([&]{return s.Latest().plcs[0].usable;});assert(s.Submit("StartRequested",r));Wait([&]{return Reader::mask!=0;});
  Reader::direction=14;Wait([&]{auto v=s.Latest();return v.sequencePhase==SequencePhase::Fault&&v.sequenceStopConfirmed;});assert(Reader::mask==0);std::this_thread::sleep_for(std::chrono::milliseconds(50));assert(Reader::mask==0);s.Stop();r.targetBar=10;
 }
 {auto o=Options(true);EquipmentControlService s(o);s.Start();Wait([&]{return s.Latest().connected;});assert(s.Submit("StartRequested",r));Wait([&]{return s.Latest().pressureAuto.busy;});assert(!s.Submit("ModeManual",r));Wait([&]{return s.Latest().pressureAuto.state==PressureAutoState::Completed;});auto v=s.Latest();assert(v.pressureAuto.stopConfirmed&&v.sequenceStopConfirmed&&!v.sequenceBusy&&v.plcs[0].ports[0].pressureBar>=9.5);s.Stop();}
 {auto o=Options(true);EquipmentControlService s(o);s.Start();Wait([&]{return s.Latest().connected;});r.targetBar=100;s.Submit("StartRequested",r);Wait([&]{return s.Latest().pressureAuto.busy;});s.Submit("StopRequested",r);Wait([&]{return s.Latest().pressureAuto.stopConfirmed;});assert(s.Latest().pressureAuto.state!=PressureAutoState::Completed);s.Stop();}
 {auto o=Options(true);EquipmentControlService s(o);s.Start();Wait([&]{return s.Latest().connected;});r.automatic=false;r.targetBar=.01;s.Submit("StartRequested",r);Wait([&]{return s.Latest().plcs[0].ports[0].absoluteMm>0;});assert(s.Latest().pressureAuto.state==PressureAutoState::Idle);s.Submit("StopRequested",r);Wait([&]{return s.Latest().sequenceStopConfirmed&&!s.Latest().sequenceBusy;});s.Stop();}
 std::cout<<"PASS: pressure simulation completion/stop/mode interlock, fake PLC D052 completion / direction interlock\n";
}
