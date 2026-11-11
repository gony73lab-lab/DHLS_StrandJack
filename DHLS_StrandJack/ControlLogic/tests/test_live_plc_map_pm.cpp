// Explicit read-only diagnostics test. Never invokes a write API or Submit.
#include "../EquipmentControlService.h"
#include "../PlcMapTracker.h"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Explicit PLC IP required\n";return 2;}
 using namespace DHLS_StrandJack::Control;using namespace DHLS_StrandJack::Diagnostics;
 ServiceOptions o;o.timeoutMs=1000;o.pollIntervalMs=200;o.maxSampleAgeMs=2000;PlcConfig p;p.ip=argv[1];p.channels.push_back(ChannelConfig{});o.plcs.push_back(p);
 EquipmentControlService service(o);service.SetPlcMapAreas(true,true);service.SetPlcMapMonitoring(true);service.Start();PlcMapTracker tracker(1);tracker.SetAreas(true,true);
 unsigned good=0;std::uint64_t previous=0;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(std::chrono::steady_clock::now()<end&&good<20){auto snapshot=service.Latest();
  if(snapshot.sequence!=previous&&!snapshot.plcs.empty()){previous=snapshot.sequence;auto const& plc=snapshot.plcs[0];tracker.Update(snapshot);
   if(plc.readOk&&plc.mapWords.size()==35){if(good++==0){for(auto const& w:plc.mapWords)if(w.family!='D')std::cout<<w.family<<"W"<<w.address<<"="<<w.value<<"\n";}}
  }std::this_thread::sleep_for(std::chrono::milliseconds(50));
 }
 service.SetPlcMapAreas(false,false);bool disabled=false;end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){auto s=service.Latest();if(!s.plcs.empty()&&s.plcs[0].mapWords.size()==23){disabled=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 service.Stop();std::cout<<"read_cycles="<<good<<" disabled="<<disabled<<" writes=0\n";return good==20&&disabled?0:1;
}
