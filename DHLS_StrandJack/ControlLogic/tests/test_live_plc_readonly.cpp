// Live hardware test: explicit IP required; never submits a service command or calls a write API.
#include "../Evo/XgbXgtTcpReadOnly.h"
#include "../EquipmentControlService.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <iomanip>
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: test_live_plc_readonly IP CSV_PATH\n";return 2;}
 using Clock=std::chrono::steady_clock;
 std::ofstream csv(argv[2]);csv<<"phase,cycle,address,count,ok,elapsed_ms,values\n";
 XgbXgtTcpReadOnly reader(argv[1],2004,1000);
 if(!reader.Connect()){std::cout<<"CONNECT FAIL\n";return 3;}
 std::cout<<"CONNECT PASS "<<argv[1]<<":2004\n";
 const std::pair<unsigned,unsigned> groups[]={{10,1},{51,1},{11,16},{56,1},{59,1}};
 unsigned reads=0;double sum=0,maximum=0;
 for(unsigned cycle=0;cycle<60;++cycle){
  for(auto group:groups){std::vector<std::uint16_t> values;auto t=Clock::now();bool ok=reader.ReadWords(static_cast<std::uint16_t>(group.first),group.second,values);
   double ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
   csv<<"driver,"<<cycle<<','<<group.first<<','<<group.second<<','<<ok<<','<<ms<<",\"";
   for(auto value:values)csv<<value<<' ';csv<<"\"\n";csv.flush();
   if(!ok){std::cout<<"READ FAIL cycle="<<cycle<<" D="<<group.first<<" count="<<group.second<<" ms="<<ms<<"\n";return 4;}
   if(cycle==0){std::cout<<"D"<<group.first<<":";for(auto v:values)std::cout<<' '<<v;std::cout<<'\n';}
   ++reads;sum+=ms;maximum=(std::max)(maximum,ms);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
 }
 reader.Disconnect();
 for(int i=0;i<3;++i){std::vector<std::uint16_t> value;bool ok=reader.Connect()&&reader.ReadWords(10,1,value);csv<<"reconnect,"<<i<<",10,1,"<<ok<<",0,\"\"\n";reader.Disconnect();if(!ok){std::cout<<"RECONNECT FAIL\n";return 5;}}
 std::cout<<"DRIVER PASS reads="<<reads<<" mean_ms="<<sum/reads<<" max_ms="<<maximum<<" reconnects=3\n";
 using namespace DHLS_StrandJack::Control;
 ServiceOptions options;options.simulation=false;options.pollIntervalMs=200;options.timeoutMs=1000;options.maxSampleAgeMs=1500;
 PlcConfig plc;plc.ip=argv[1];for(int i=0;i<8;++i){ChannelConfig c;c.sensor=i;c.scalingVerified=false;plc.channels.push_back(c);}options.plcs.push_back(plc);
 for(int session=0;session<3;++session){
  EquipmentControlService service(options);service.Start();auto until=Clock::now()+std::chrono::seconds(8);unsigned good=0;std::uint64_t last=0;
  while(Clock::now()<until&&good<10){auto s=service.Latest();
   if(s.sequence!=last&&!s.plcs.empty()&&s.plcs[0].readOk){last=s.sequence;auto const& p=s.plcs[0];
    csv<<"service,"<<session<<",10,1,"<<p.usable<<",0,\"connected="<<p.connected<<" ports="<<p.ports.size()<<" readOnly="<<s.readOnly<<"\"\n";
    if(!s.readOnly||s.simulation||p.ports.size()!=8){std::cerr<<"SERVICE MODE FAIL\n";service.Stop();return 6;}
    for(auto const& v:p.ports)if(v.pressureValid||v.strokeValid||v.loadValid){std::cerr<<"UNVERIFIED SCALE ACCEPTED\n";service.Stop();return 7;}
    if(p.usable)++good;
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  auto t=Clock::now();service.Stop();auto stopMs=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
  csv<<"service_stop,"<<session<<",0,0,"<<(good==10)<<','<<stopMs<<",\"\"\n";
  std::cout<<"SERVICE session="<<session<<" samples="<<good<<" stop_ms="<<stopMs<<'\n';if(good<10)return 8;
 }
 std::cout<<"PASS: real driver polling, reconnect, service lifecycle and unverified-sensor gate. ZERO WRITE COMMANDS.\n";return 0;
}
