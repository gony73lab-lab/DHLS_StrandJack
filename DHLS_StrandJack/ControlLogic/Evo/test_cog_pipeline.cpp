#include "COGDiagnosticPipeline.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

struct FakeDReader : DRegionDiagnostics::IReadOnlyDWords {
    std::vector<std::uint16_t> measurements=std::vector<std::uint16_t>(16,0);
    std::uint16_t faults=0;
    bool fail=false;
    bool ReadWords(std::uint16_t firstD,std::size_t count,
                   std::vector<std::uint16_t>& out) override {
        if (fail) return false;
        if (firstD==11 && count==16) {out=measurements;return true;}
        if (firstD==56 && count==1) {out={faults};return true;}
        return false;
    }
};
int main() {
    COGDiagnosticPipeline::Config cfg;
    cfg.referenceCylinderId=0;
    const double x[4]={0,2000,2000,0};
    const double y[4]={0,0,1000,1000};
    const double a[4]={10000,10000,30000,10000};
    for (std::size_t i=0;i<4;++i) {
        COGDiagnosticPipeline::Channel ch;
        ch.cylinderId=i;ch.xMm=x[i];ch.yMm=y[i];
        ch.geometryVerified=true;
        ch.mpaPerCount=0.01;ch.pressureCalibrationVerified=true;
        ch.effectiveAreaMm2=a[i];ch.areaVerified=true;
        cfg.channels.push_back(ch);
    }
    COGDiagnosticPipeline pipe(cfg);
    FakeDReader fake;
    for (std::size_t i=0;i<4;++i) fake.measurements[2*i]=1000;
    auto r=pipe.Poll(fake,1000,1100,1200);
    assert(r.valid());
    assert(std::abs(r.cog.totalForceKN-600)<1e-7);
    assert(std::abs(r.cog.cogXMm-1333.333333333)<1e-6);
    assert(std::abs(r.cog.cogYMm-666.666666667)<1e-6);
    fake.faults=1u<<2;
    r=pipe.Poll(fake,1000,1100,1200);
    assert(!r.valid() && r.pressureSensorFault);
    fake.faults=0;
    assert(!pipe.Poll(fake,1000,1100,1700).valid());
    fake.fail=true;
    assert(!pipe.Poll(fake,1000,1100,1200).valid());
    std::cout<<"COG D-region pipeline offline tests PASS\n";
}
