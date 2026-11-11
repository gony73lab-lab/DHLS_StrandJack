#include "COGIntegration.h"
#include <iomanip>
#include <iostream>

int main() {
    // OFFLINE ONLY: simulated D-region snapshot. No PLC network or writes.
    PressureBalancedLiftUp::Config cfg;
    cfg.maxSpreadMpa = 0.5;
    cfg.targetToleranceMpa = 0.1;
    for (std::size_t i=0;i<4;++i) {
        PressureBalancedLiftUp::Cylinder c;
        c.index=i; c.mpaPerCount=0.01; c.calibrationVerified=true;
        c.targetMpa=10; c.advisoryMaxMpa=15;
        cfg.selected.push_back(c);
    }
    PressureBalancedLiftUp pressureMonitor(cfg);
    DRegionDiagnostics::Snapshot sample;
    sample.readOk=true; sample.pcSelected=true;
    for (std::size_t i=0;i<4;++i) sample.rawPressure[i]=1000; // 10 MPa (example scale)
    const auto pressure=pressureMonitor.Analyze(sample,1000,1050);

    std::vector<COGIntegration::Geometry> layout(4);
    const double x[4]={0,2000,2000,0};
    const double y[4]={0,0,1000,1000};
    const double areas[4]={10000,10000,30000,10000};
    for (std::size_t i=0;i<4;++i) {
        layout[i].cylinderId=i;
        layout[i].xMm=x[i]; layout[i].yMm=y[i];
        layout[i].effectiveAreaMm2=areas[i];
        layout[i].geometryVerified=true;
        layout[i].areaVerified=true;
    }
    const auto cog=COGIntegration().Analyze(pressure,layout,0);
    if (!cog.inputUsable) {
        std::cerr << "COG unavailable: " << cog.reason << "\n";
        return 1;
    }
    std::cout << std::fixed << std::setprecision(2)
              << "COG relative to CYL1: X=" << cog.cog.cogXMm
              << " mm, Y=" << cog.cog.cogYMm
              << " mm, Total=" << cog.cog.totalForceKN << " kN\n";
    for (const auto& c : cog.cog.supports)
        std::cout << "CYL" << c.cylinderId+1 << " reaction=" << c.forceKN
                  << " kN, share=" << c.sharePercent << "%\n";
}
