#include "DRegionDiagnostics.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
struct FakeReadOnlyPLC : DRegionDiagnostics::IReadOnlyDWords {
    std::map<unsigned,std::uint16_t> words;
    bool ReadWords(std::uint16_t start, std::size_t count,
                   std::vector<std::uint16_t>& out) override {
        out.clear();
        for (std::size_t i=0;i<count;++i) {
            auto it=words.find(start+static_cast<unsigned>(i));
            if(it==words.end()) return false;
            out.push_back(it->second);
        }
        return true;
    }
};
int main(){
    FakeReadOnlyPLC plc;
    plc.words[10]=0b110;
    for(unsigned i=0;i<8;i++){
        plc.words[11+2*i]=100;
        plc.words[12+2*i]=static_cast<std::uint16_t>(250+i);
    }
    plc.words[56]=0;plc.words[59]=0;
    DRegionDiagnostics::Config cfg;
    for(auto& c:cfg.stroke){c.mmPerCount=0.1;c.verified=true;}
    cfg.startMm.fill(20.0);cfg.targetDeltaMm.fill(50.0);
    DRegionDiagnostics diag(cfg);
    auto s=diag.Poll(plc,1000,1010);
    assert(s.readOk && s.usable && s.pcSelected && s.pumpOn);
    assert(std::abs(s.diagnostic.stopErrorMm-0.7)<1e-9);
    plc.words[59]=1u<<3;
    s=diag.Poll(plc,1020,1030);
    assert(s.readOk && !s.usable && s.strokeDisconnected[3]);
    cfg.stroke[0].verified=false;
    DRegionDiagnostics unsafe(cfg);
    s=unsafe.Poll(plc,1040,1050);
    assert(!s.usable);
    std::cout << "PASS: normal, sensor fault, unverified scaling\n";
}
