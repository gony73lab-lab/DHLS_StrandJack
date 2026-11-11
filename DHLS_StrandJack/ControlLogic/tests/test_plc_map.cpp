#include "../XgtEmergencyTransport.h"
#include "../PlcMapTracker.h"
#include <cassert>
#include <crtdbg.h>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <iostream>
#include <set>
#include <algorithm>
using namespace DHLS_StrandJack;
using namespace Diagnostics;
using namespace Control;
std::size_t Id(std::wstring const& name){auto const& c=PlcMapCatalog();for(std::size_t i=0;i<c.size();++i)if(c[i].address==name)return i;throw std::runtime_error("Missing map address");}
EquipmentSnapshot Sample(unsigned word){EquipmentSnapshot s;s.plcs.resize(2);for(auto& p:s.plcs){p.connected=p.readOk=true;p.mapWords={{10,static_cast<std::uint16_t>(word),true,1},{11,129,true,1}};}return s;}
struct Counts {std::atomic<int> optional{0},writes{0},pReads{0},mReads{0};std::atomic<bool> failP{false};};
class Reader:public IPlcReadTransport {
 std::shared_ptr<Counts> c_;
public:
 explicit Reader(std::shared_ptr<Counts> c):c_(std::move(c)){}
 bool ReadDeviceWords(char area,std::vector<std::uint16_t> const& addresses,std::vector<std::uint16_t>& out)override{
  assert(area=='P'||area=='M');if(area=='P'){assert((addresses==std::vector<std::uint16_t>{0,2,8}));++c_->pReads;if(c_->failP){out.clear();return false;}}else{assert(addresses.size()==9);++c_->mReads;}
  out.assign(addresses.size(),0x8001);return true;
 }
 bool Connect()override{return true;}void Disconnect()override{}
 bool SetEmergencyOn()override{++c_->writes;return false;}
 bool ReadWords(std::uint16_t first,std::size_t n,std::vector<std::uint16_t>& out)override{
  out.assign(n,0);if(first==10)out[0]=6;if(first==11)out[0]=129;
  if(first==50){++c_->optional;out[0]=first;}
  return true;
 }
};
template<class F>void Until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!f()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
int main(){
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 auto const& catalog=PlcMapCatalog();std::set<std::wstring> unique;for(auto const& e:catalog)assert(unique.insert(e.address).second);
 assert(catalog.size()==305);assert(catalog[Id(L"D051.F")].bit==15);Id(L"P000A");Id(L"M003F");Id(L"D050");
 PlcMapTracker tracker(2);auto s=Sample(0);assert(tracker.Update(s).empty());assert(tracker.Value(0,Id(L"D010.0")).known);assert(!tracker.Value(0,Id(L"P0000")).known);
 s.plcs[0].mapWords[0].value=1;auto changes=tracker.Update(s);assert(changes.size()==1);assert(tracker.Value(0,Id(L"D010.0")).changes==1);assert(tracker.Value(1,Id(L"D010.0")).changes==0);
 assert(tracker.Update(s).empty());s.plcs[0].connected=false;assert(tracker.Update(s).empty());assert(!tracker.Value(0,Id(L"D010.0")).known);assert(tracker.Value(0,Id(L"D010.0")).value==1);
 s.plcs[0].connected=true;s.plcs[0].mapWords[0].value=0;changes=tracker.Update(s);assert(changes.size()==1&&changes[0].reacquired);assert(tracker.Value(0,Id(L"D010.0")).changes==2);
 s.plcs[0].mapWords[1].value=130;changes=tracker.Update(s);assert(changes.size()==1&&catalog[changes[0].entry].address==L"D11");
 s.simulation=true;tracker.Update(s);assert(!tracker.Value(0,Id(L"D11")).known);
 s.simulation=false;s.plcs[0].mapWords[1].valid=false;tracker.Update(s);assert(!tracker.Value(0,Id(L"D11")).known);
 PlcMapTracker bounded(1);s=Sample(0);bounded.Update(s);for(int i=1;i<=600;++i){s.plcs[0].mapWords[0].value=static_cast<std::uint16_t>(i%2);bounded.Update(s);}assert(bounded.History().size()==500);assert(bounded.Value(0,Id(L"D010.0")).changes==600);
 PlcMapTracker pm(2);auto raw=Sample(0);raw.plcs[0].mapWords.push_back({0,0x8001,true,1,'P'});raw.plcs[0].mapWords.push_back({3,0x8000,true,1,'M'});
 pm.Update(raw);assert(!pm.Value(0,Id(L"P0000")).known);pm.SetAreas(true,false);assert(pm.Update(raw).empty());assert(pm.Value(0,Id(L"P0000")).value==1&&pm.Value(0,Id(L"P000F")).value==1);assert(!pm.Value(0,Id(L"M003F")).known);
 pm.SetAreas(true,true);pm.Update(raw);assert(pm.Value(0,Id(L"M003F")).value==1);raw.plcs[0].mapWords.back().value=0;assert(pm.Update(raw).size()==1);pm.SetAreas(false,true);pm.Update(raw);assert(!pm.Value(0,Id(L"P0000")).known);assert(pm.Value(0,Id(L"M003F")).known);
 for(char area:{'D','P','M'}){auto frame=XgtEmergencyTransport::BuildReadRequest({0,2,8},17,area);std::string bytes(frame.begin(),frame.end());assert(bytes.find(std::string("%")+area+"W8")!=std::string::npos);assert(frame[20]==0x54);}
 bool rejected=false;try{XgtEmergencyTransport::BuildReadRequest({0},1,'X');}catch(std::invalid_argument const&){rejected=true;}assert(rejected);
 auto counts=std::make_shared<Counts>();ServiceOptions o;o.pollIntervalMs=20;o.maxSampleAgeMs=500;PlcConfig p;p.ip="127.0.0.1";p.channels.push_back(ChannelConfig{});o.plcs.push_back(p);
 EquipmentControlService service(o,[counts](PlcConfig const&,int){return std::make_unique<Reader>(counts);});service.Start();Until([&]{return service.Latest().plcs[0].readOk;});assert(counts->optional==0);assert(service.Latest().plcs[0].mapWords.size()==22);
 service.SetPlcMapMonitoring(true);Until([&]{return service.Latest().plcs[0].mapWords.size()==23;});assert(counts->optional>=1);
 service.SetPlcMapAreas(true,false);Until([&]{return service.Latest().plcs[0].mapWords.size()==26;});assert(counts->pReads>0&&counts->mReads==0);
 service.SetPlcMapAreas(false,true);Until([&]{auto v=service.Latest().plcs[0].mapWords;return v.size()==32&&std::none_of(v.begin(),v.end(),[](auto const& w){return w.family=='P';});});assert(counts->mReads>0);
 service.SetPlcMapAreas(true,true);Until([&]{return service.Latest().plcs[0].mapWords.size()==35;});
 service.SetPlcMapAreas(false,false);Until([&]{return service.Latest().plcs[0].mapWords.size()==23;});
 counts->failP=true;service.SetPlcMapAreas(true,true);Until([&]{auto v=service.Latest().plcs[0].mapWords;return v.size()==32&&std::none_of(v.begin(),v.end(),[](auto const& w){return w.family=='P';});});assert(service.Latest().plcs[0].readOk);
 service.SetPlcMapMonitoring(false);Until([&]{return service.Latest().plcs[0].mapWords.size()==22;});service.Stop();assert(counts->writes==0);
 std::cout<<"PASS: 305 map entries, hexadecimal P/M and bit F, ON/OFF edges, WORD changes, reconnect quality, simulation/stale unknown, PLC isolation, 500-history bound, opt-in D/P/M polling, independent enable/disable, bit F/family isolation, read-only codec and zero writes.\n";
}
