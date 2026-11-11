#include "COGIntegration.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

static bool near(double a,double b,double eps=1e-7) {return std::abs(a-b)<eps;}

int main() {
    COGCalculator calc;
    std::vector<COGCalculator::Support> s;
    const double x[4]={0,2000,2000,0};
    const double y[4]={0,0,1000,1000};
    const double f[4]={100,100,300,100};
    for (std::size_t i=0;i<4;++i) {
        COGCalculator::Support c;
        c.cylinderId=i; c.geometryVerified=true; c.xMm=x[i]; c.yMm=y[i];
        c.loadSource=COGCalculator::LoadSource::VerifiedLoadKN;
        c.loadKN=f[i]; c.loadVerified=true; s.push_back(c);
    }
    auto r=calc.Calculate(s,0);
    assert(r.valid() && r.selectedCount==4);
    assert(near(r.totalForceKN,600) && near(r.cogXMm,1333.3333333333) && near(r.cogYMm,666.6666666667));
    assert(near(r.supports[2].sharePercent,50.0));
    auto ref2=calc.Calculate(s,2);
    assert(ref2.valid() && near(ref2.cogXMm,-666.6666666667) && near(ref2.cogYMm,-333.3333333333));
    s[0].loadVerified=false;
    assert(calc.Calculate(s,0).status==COGCalculator::Status::InvalidLoad);
    s[0].loadVerified=true;
    s[2].selected=false; // three remaining points form a triangle
    assert(calc.Calculate(s,0).valid());
    s[3].selected=false;
    assert(calc.Calculate(s,0).status==COGCalculator::Status::NotEnoughSupports);
    s[2].selected=true; s[3].selected=true;
    s[1].yMm=0; s[2].yMm=0; s[3].yMm=0;
    assert(calc.Calculate(s,0).status==COGCalculator::Status::InvalidGeometry);
    s[2].yMm=1000; s[3].yMm=1000;
    assert(calc.Calculate(s,10).status==COGCalculator::Status::MissingReference);
    s[0].loadKN=std::numeric_limits<double>::quiet_NaN();
    assert(calc.Calculate(s,0).status==COGCalculator::Status::InvalidLoad);
    s[0].loadKN=100;

    PressureBalancedLiftUp::Config pcfg;
    pcfg.maxSpreadMpa=0.5;
    pcfg.targetToleranceMpa=0.1;
    for (std::size_t i=0;i<4;++i) {
        PressureBalancedLiftUp::Cylinder c;
        c.index=i; c.calibrationVerified=true; c.mpaPerCount=0.01;
        c.targetMpa=10; c.advisoryMaxMpa=15;
        pcfg.selected.push_back(c);
    }
    PressureBalancedLiftUp monitor(pcfg);
    DRegionDiagnostics::Snapshot snapshot;
    snapshot.readOk=true; snapshot.pcSelected=true;
    for (std::size_t i=0;i<4;++i) snapshot.rawPressure[i]=1000;
    auto pressure=monitor.Analyze(snapshot,1000,1100);
    assert(pressure.state==PressureBalancedLiftUp::State::AllTargetsObserved);

    std::vector<COGIntegration::Geometry> geometry;
    for (std::size_t i=0;i<4;++i) {
        COGIntegration::Geometry g;
        g.cylinderId=i; g.xMm=x[i]; g.yMm=y[i];
        g.geometryVerified=true; g.areaVerified=true;
        g.effectiveAreaMm2=(i==2?30000:10000); // synthetic example only
        geometry.push_back(g);
    }
    COGIntegration integration;
    auto ir=integration.Analyze(pressure,geometry,0);
    assert(ir.inputUsable && near(ir.cog.totalForceKN,600));
    assert(near(ir.cog.cogXMm,1333.3333333333));
    assert(near(ir.cog.cogYMm,666.6666666667));

    geometry[2].areaVerified=false;
    assert(!integration.Analyze(pressure,geometry,0).inputUsable);
    geometry[2].areaVerified=true;
    geometry.pop_back();
    assert(!integration.Analyze(pressure,geometry,0).inputUsable); // selection mismatch
    geometry.push_back(COGIntegration::Geometry{}); // duplicate id 0
    assert(!integration.Analyze(pressure,geometry,0).inputUsable);
    geometry.pop_back();
    snapshot.pressureDisconnected[0]=true;
    auto invalid=monitor.Analyze(snapshot,1000,1100);
    assert(invalid.state==PressureBalancedLiftUp::State::Invalid);
    assert(!integration.Analyze(invalid,geometry,0).inputUsable);
    snapshot.pressureDisconnected[0]=false;
    auto stale=monitor.Analyze(snapshot,1000,5000);
    assert(stale.state==PressureBalancedLiftUp::State::Invalid);
    assert(!integration.Analyze(stale,geometry,0).inputUsable);

    // A valid footprint with fewer than three positive reactions cannot
    // establish a trustworthy 2D support-reaction centroid.
    std::vector<COGCalculator::Support> underloaded = s;
    underloaded[1].loadKN = 0.0;
    underloaded[2].loadKN = 0.0;
    assert(calc.Calculate(underloaded,0).status == COGCalculator::Status::NotEnoughSupports);

    // Positive load-bearing points are collinear, but the selected footprint is not.
    std::vector<COGCalculator::Support> loadedLine = s;
    loadedLine[0].xMm = 0.0; loadedLine[0].yMm = 1000.0;
    loadedLine[0].loadKN = 0.0;
    loadedLine[1].xMm = 0.0; loadedLine[1].yMm = 0.0;
    loadedLine[2].xMm = 1000.0; loadedLine[2].yMm = 0.0;
    loadedLine[3].xMm = 2000.0; loadedLine[3].yMm = 0.0;
    assert(calc.Calculate(loadedLine,0).status == COGCalculator::Status::InvalidGeometry);

    // The pure calculator is variable-N, independent of the current 8-channel D-map.
    std::vector<COGCalculator::Support> nine = s;
    for (std::size_t i=4;i<9;++i) {
        auto c=s[i%4]; c.cylinderId=i;
        nine.push_back(c);
    }
    assert(calc.Calculate(nine,0).valid());

    std::cout << "COG calculator/integration offline tests PASS\n";
}
