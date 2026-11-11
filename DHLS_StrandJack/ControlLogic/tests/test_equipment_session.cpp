#include "../EquipmentSession.h"
#include <atomic>
#include <thread>
#include <iostream>
#include <cstdlib>
using namespace DHLS_StrandJack::Control;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<std::endl;std::exit(1);}}while(false)
struct Counts{std::atomic<int> active{0},maximum{0},connects{0},disconnects{0};};
struct Reader:IPlcReadTransport{std::shared_ptr<Counts> counts;bool on=false;Reader(std::shared_ptr<Counts> v):counts(v){}
 bool Connect()override{if(!on){on=true;int a=++counts->active;++counts->connects;if(a>counts->maximum)counts->maximum=a;}return true;}
 void Disconnect()override{if(on){on=false;--counts->active;++counts->disconnects;}}
 bool ReadWords(std::uint16_t a,std::size_t n,std::vector<std::uint16_t>& out)override{out.assign(n,0);if(a==10)out[0]=2;return true;}
};
template<class F>void Wait(F f){for(int i=0;i<500;++i){if(f())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}CHECK(false);}
int main(){
 auto counts=std::make_shared<Counts>();EquipmentControlService::Factory factory=[&](auto const&,int){return std::make_unique<Reader>(counts);};
 ServiceOptions options;options.pollIntervalMs=20;options.plcs.resize(1);options.plcs[0].ip="127.0.0.1";options.plcs[0].channels.resize(1);
 EquipmentSession app;auto menu=app.Connect(options,L"model",false,factory);Wait([&]{return menu->Latest().plcs[0].readOk;});
 auto dashboard=app.Connect(options,L"model",false,factory);CHECK(menu==dashboard&&counts->connects==1);menu.reset();dashboard.reset();CHECK(app.Current()&&counts->active==1);
 auto old=app.Current();options.plcs[0].channels[0].strokeMmPerCount=.5;auto changed=app.Connect(options,L"model",false,factory);
 Wait([&]{return changed->Latest().plcs[0].readOk;});CHECK(changed==old&&counts->maximum==1&&counts->disconnects==0);
 options.plcs[0].ip="other";changed=app.Connect(options,L"model",false,factory);
 Wait([&]{return changed->Latest().plcs[0].readOk;});CHECK(changed!=old&&counts->maximum==1&&counts->disconnects==1);
 EquipmentSession::DashboardMemory memory;memory.valid=memory.automatic=true;memory.selected[0][0]=true;memory.up[0]=17;memory.appliedReset=42;
 app.Dashboard(L"model",false,memory);auto restored=app.Dashboard(L"model",false);
 CHECK(restored.valid&&restored.automatic&&restored.selected[0][0]&&restored.up[0]==17&&restored.appliedReset==42);
 CHECK(!app.Dashboard(L"model",true).valid);
 CHECK(app.Disconnect(changed));CHECK(!app.Current()&&counts->active==0);CHECK(!app.Connect(options,L"model",false,factory));
 auto again=app.Connect(options,L"model",true,factory);Wait([&]{return again->Latest().plcs[0].readOk;});CHECK(again&&counts->active==1);
 auto stale=app.Request();auto latest=app.Request();CHECK(!app.Connect(options,L"stale-model",false,factory,stale));CHECK(app.Current()==again);
 CHECK(app.Connect(options,L"model",false,factory,latest)==again);
 auto mockOptions=options;mockOptions.simulation=true;
 auto mock=app.Connect(mockOptions,L"model",true,factory);
 Wait([&]{return mock->Latest().simulation&&mock->Latest().connected;});
 CHECK(mock!=again&&counts->active==0);
 auto live=app.Connect(options,L"model",true,factory);
 Wait([&]{return live->Latest().plcs[0].readOk;});
 CHECK(live!=mock&&!live->Latest().simulation&&counts->active==1&&counts->maximum==1);
 CHECK(app.CurrentFor(options,L"model")==live);
 app.Shutdown();CHECK(!app.Current()&&counts->active==0);CHECK(!app.Connect(options,L"model",true,factory));
 std::cout<<"PASS: app-owned PLC session reused across views, joined replacement, no concurrent sockets, authorized disconnect suppression, reconnect, stale coroutine rejection, shutdown OFF/join\n";
}
