#include "../Evo/XgbXgtTcpReadOnly.h"
#include "../EquipmentControlService.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace DHLS_StrandJack::Control;
struct Stats {std::atomic<int> connects{0},values{0},writes{0};std::atomic<bool> injected{false};bool delay=false;};
class ReadTransport final:public IPlcReadTransport {
 XgbXgtTcpReadOnly reader_;std::shared_ptr<Stats> stats_;
public:
 ReadTransport(PlcConfig const& p,int t,std::shared_ptr<Stats> s):reader_(p.ip,2004,t),stats_(std::move(s)){}
 bool Connect()override{++stats_->connects;return reader_.Connect();}
 void Disconnect()override{reader_.Disconnect();}
 bool SetEmergencyOn()override{++stats_->writes;return false;} // No PLC write capability in this test transport.
 bool ReadWords(std::uint16_t first,std::size_t n,std::vector<std::uint16_t>& out)override{
  if(first==11&&++stats_->values==3){stats_->injected=true;
   if(stats_->delay)std::this_thread::sleep_for(std::chrono::milliseconds(250));
   else {reader_.Disconnect();out.clear();return false;}
  }
  return reader_.ReadWords(first,n,out);
 }
};
int main(int argc,char** argv){
 if(argc!=2)return 2;
 for(int mode=0;mode<2;++mode){auto stats=std::make_shared<Stats>();stats->delay=mode==1;
  ServiceOptions options;options.pollIntervalMs=200;options.timeoutMs=1000;options.maxSampleAgeMs=150;
  PlcConfig p;p.ip=argv[1];for(int i=0;i<8;++i){ChannelConfig c;c.sensor=i;p.channels.push_back(c);}options.plcs.push_back(p);
  EquipmentControlService service(options,[stats](PlcConfig const& plc,int t){return std::make_unique<ReadTransport>(plc,t,stats);});
  service.Start();bool invalid=false,recovered=false;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
  while(std::chrono::steady_clock::now()<end){auto s=service.Latest();auto const& plc=s.plcs[0];
   if(stats->injected&&(!plc.readOk||!plc.usable))invalid=true;
   if(invalid&&plc.readOk&&plc.usable){recovered=true;break;}
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if(!invalid||!recovered||(!mode&&stats->connects<2)){service.Stop();std::cout<<"FAIL recovery mode="<<mode<<'\n';return 3;}
  DiagnosticRequest request;request.selected[0][0]=true;request.targetMm=50;
  service.Submit("StartRequested",request);
  bool refused=false;end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<end){auto s=service.Latest();if(!s.monitoring&&s.message=="Diagnostics require fresh calibrated measurements"){refused=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  service.Submit("PressureZero",request);std::this_thread::sleep_for(std::chrono::milliseconds(250));
  auto s=service.Latest();const bool setupBlocked=s.message=="Read-only: PLC command not transmitted";
  service.Stop();if(!refused||!setupBlocked||stats->writes){std::cout<<"FAIL readonly gate\n";return 4;}
  std::cout<<"PASS "<<(mode?"delayed acquisition rejected and recovered":"client disconnect detected and reconnected")<<" connectCalls="<<stats->connects<<" unverifiedStartRejected="<<refused<<" setupWriteBlocked="<<setupBlocked<<" writeAttempts="<<stats->writes<<'\n';
 }
 return 0;
}
