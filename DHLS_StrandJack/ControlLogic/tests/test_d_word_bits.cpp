// Offline audit only: fake transport, no network and no physical PLC writes.
#include "../Evo/XgbXgtTcpReadWrite.h"
#include "../Evo/LoweringControl.h"
#include "../PlcMapTracker.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <crtdbg.h>
#include <iostream>
#include <memory>
#include <thread>
using namespace DHLS_StrandJack::Control;
using namespace DHLS_StrandJack::Diagnostics;
struct Words {std::atomic<unsigned> d10{0},d51{0},d56{0},d59{0},writes{0};};
struct Reader: IPlcReadTransport {
 std::shared_ptr<Words> words;explicit Reader(std::shared_ptr<Words> w):words(w){}
 bool Connect()override{return true;}void Disconnect()override{}
 bool SetEmergencyOn()override{++words->writes;return false;}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{
  out.assign(n,0);if(a==10)out[0]=static_cast<std::uint16_t>(words->d10.load());if(a==51)out[0]=static_cast<std::uint16_t>(words->d51.load());
  if(a==56)out[0]=static_cast<std::uint16_t>(words->d56.load());if(a==59)out[0]=static_cast<std::uint16_t>(words->d59.load());
  if(a==11)for(unsigned i=0;i<8;++i){out[2*i]=100+i;out[2*i+1]=200+i;}return true;
 }
};
ServiceOptions Options(){ServiceOptions o;o.pollIntervalMs=20;o.maxSampleAgeMs=2000;PlcConfig p;p.ip="127.0.0.1";for(int id:{7,0,3}){ChannelConfig c;c.sensor=id;c.scalingVerified=true;c.pressureBarPerCount=1;c.strokeMmPerCount=1;p.channels.push_back(c);}o.plcs.push_back(p);return o;}
unsigned Raw(EquipmentSnapshot const& s,unsigned address){for(auto const& w:s.plcs[0].mapWords)if(w.family=='D'&&w.address==address)return w.value;return 0xffffffffu;}
template<class F>EquipmentSnapshot Wait(EquipmentControlService& service,F condition){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);for(;;){auto s=service.Latest();if(s.sequence&&s.plcs[0].readOk&&condition(s))return s;if(std::chrono::steady_clock::now()>end){std::cerr<<"Snapshot timeout\n";std::abort();}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
int main(){_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 // Independently enumerate every WORD value through the actual byte parser and write mask policy.
 DRegionWritePolicy policy;policy.SetAuthorized(true);policy.AllowBit(51,15);
 for(unsigned value=0;value<=0xffff;++value){std::vector<std::uint8_t> response={0x55,0,2,0,0,0,0,0,1,0,2,0,static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8)};std::vector<std::uint16_t> decoded;
  assert(XgbXgtTcpReadOnly::ParseReadResponse(response,1,decoded)&&decoded[0]==value);
  auto on=policy.PrepareBit(51,15,true,static_cast<std::uint16_t>(value),true);auto off=policy.PrepareBit(51,15,false,static_cast<std::uint16_t>(value),true);
  assert(on.value==(value|0x8000)&&off.value==(value&0x7fff));assert((on.value&0x7fff)==(value&0x7fff));
  assert(LoweringControl::ValidateDirectionWord(static_cast<std::uint16_t>(value))==((value&6)!=6));
 }
 std::cout<<"PASS: all 65536 WORD values, little endian, F mask preservation and direction mask\n";
 // Every catalog D bit, including A..F: one-hot and all-ones must map to only its own bit.
 for(unsigned address:{10,51,52,55,56,59})for(unsigned bit=0;bit<16;++bit){EquipmentSnapshot s;s.plcs.resize(1);s.plcs[0].connected=s.plcs[0].readOk=true;s.plcs[0].mapWords={{static_cast<std::uint16_t>(address),static_cast<std::uint16_t>(1u<<bit),true,1}};PlcMapTracker t(1);t.Update(s);
  for(std::size_t i=0;i<PlcMapCatalog().size();++i){auto const& d=PlcMapCatalog()[i];if(d.family=='D'&&d.word==static_cast<int>(address)&&d.bit>=0){auto const& v=t.Value(0,i);assert(v.known&&v.value==(d.bit==static_cast<int>(bit)));}}
 }
 std::cout<<"PASS: map D10/51/52/55/56/59 one-hot bits 0..F (96 cases)\n";
 auto words=std::make_shared<Words>();EquipmentControlService service(Options(),[words](auto const&,int){return std::make_unique<Reader>(words);});service.Start();Wait(service,[](auto const&){return true;});
 for(unsigned bit=0;bit<16;++bit){unsigned value=1u<<bit;words->d10=value;auto s=Wait(service,[&](auto const& v){return Raw(v,10)==value;});auto const& p=s.plcs[0];assert(p.emergencyBit==(bit==0));assert(p.pcSelected==(bit==1));assert(p.pumpOn==(bit==2));assert(p.error==(bit==3));}
 words->d10=0;Wait(service,[](auto const& s){return Raw(s,10)==0&&!s.emergencyActive;});
 const int ids[]={7,0,3};for(unsigned addr:{56,59})for(unsigned bit=0;bit<16;++bit){words->d56=addr==56?1u<<bit:0;words->d59=addr==59?1u<<bit:0;
  auto s=Wait(service,[&](auto const& v){return Raw(v,56)==words->d56&&Raw(v,59)==words->d59;});for(int p=0;p<3;++p){auto const& v=s.plcs[0].ports[p];assert(v.rawPressure==100+ids[p]&&v.rawStroke==200+ids[p]);assert(v.pressureFault==(addr==56&&bit==ids[p]));assert(v.strokeFault==(addr==59&&bit==ids[p]));}assert(s.plcs[0].error&&s.faultActive);}
 service.Stop();assert(words->writes==0);std::cout<<"PASS: service D10 0..F, sensor WORD stride, reordered PT/HT IDs and all-bit fault interlock\n";
 for(unsigned value:{1u,2u,4u,8u,16u,32u,64u,128u,256u,512u,1024u,2048u,4096u,8192u,16384u,32768u,32769u}){auto w=std::make_shared<Words>();w->d51=value;EquipmentControlService one(Options(),[w](auto const&,int){return std::make_unique<Reader>(w);});one.Start();auto s=Wait(one,[&](auto const& v){return Raw(v,51)==value;});assert(s.emergencyActive==((value&1)!=0));assert(s.pcEmergencyRequested==((value&1)!=0));assert(s.plcs[0].emergencyWrite==((value&1)?EmergencyWriteState::ConfirmedOn:EmergencyWriteState::NotRequested));one.Stop();assert(w->writes==0);}
 std::cout<<"PASS: service D51 every bit and 0x8001; F is never mistaken for E-stop; zero writes\n";
}
