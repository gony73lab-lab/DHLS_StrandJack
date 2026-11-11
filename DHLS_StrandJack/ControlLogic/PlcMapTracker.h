#pragma once
#include "EquipmentControlService.h"
#include "PlcMapCatalog.h"
#include <deque>
#include <optional>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
namespace DHLS_StrandJack::Diagnostics {
struct MapValue {bool known=false;std::uint16_t value=0,word=0;unsigned changes=0;std::uint64_t changedAtMs=0;std::wstring changedTime;};
struct MapChange {int plc=0;std::size_t entry=0;std::uint16_t before=0,after=0;bool reacquired=false;std::wstring time;};
class PlcMapTracker {
    std::vector<std::vector<MapValue>> values_;
    std::vector<std::vector<bool>> seen_;
    std::deque<MapChange> history_;
    bool useP_=false,useM_=false;
public:
    static std::uint64_t NowMs(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    static std::wstring Time(){auto now=std::chrono::system_clock::now();auto t=std::chrono::system_clock::to_time_t(now);std::tm local{};localtime_s(&local,&t);std::wostringstream s;s<<std::put_time(&local,L"%H:%M:%S");return s.str();}
    explicit PlcMapTracker(std::size_t count):values_(count,std::vector<MapValue>(PlcMapCatalog().size())),seen_(count,std::vector<bool>(PlcMapCatalog().size(),false)){}
    void SetAreas(bool p,bool m){useP_=p;useM_=m;}
    std::vector<MapChange> Update(Control::EquipmentSnapshot const& snapshot){
        std::vector<MapChange> changes;auto const& catalog=PlcMapCatalog();
        for(std::size_t p=0;p<values_.size();++p)for(std::size_t e=0;e<catalog.size();++e){
            auto& v=values_[p][e];auto const& def=catalog[e];bool available=false;std::uint16_t word=0;
            if(!snapshot.simulation&&p<snapshot.plcs.size()&&snapshot.plcs[p].connected&&snapshot.plcs[p].readOk&&(def.family==L'D'||(def.family==L'P'&&useP_)||(def.family==L'M'&&useM_))){
                for(auto const& sample:snapshot.plcs[p].mapWords)if(sample.family==def.family&&sample.address==def.word&&sample.valid){available=true;word=sample.value;break;}
            }
            if(!available){v.known=false;continue;}
            auto value=def.bit<0?word:static_cast<std::uint16_t>((word>>def.bit)&1);
            if(seen_[p][e]&&v.value!=value){MapChange c{static_cast<int>(p),e,v.value,value,!v.known,Time()};
                ++v.changes;v.changedAtMs=NowMs();v.changedTime=c.time;history_.push_front(c);changes.push_back(c);
                if(history_.size()>500)history_.pop_back();}
            v.known=true;v.value=value;v.word=word;seen_[p][e]=true;
        }
        return changes;
    }
    MapValue const& Value(std::size_t p,std::size_t e)const{return values_.at(p).at(e);}
    std::deque<MapChange> const& History()const{return history_;}
};
}
