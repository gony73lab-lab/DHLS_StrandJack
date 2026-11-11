#include "PressureBalancedLiftUp.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    PressureBalancedLiftUp::Config cfg;
    cfg.maxSpreadMpa = 0.5;
    cfg.targetToleranceMpa = 0.1;
    for (std::size_t i = 0; i < 3; ++i) {
        PressureBalancedLiftUp::Cylinder c;
        c.index = i; c.calibrationVerified = true; c.mpaPerCount = 0.01;
        c.targetMpa = 10; c.advisoryMaxMpa = 12;
        cfg.selected.push_back(c);
    }
    PressureBalancedLiftUp monitor(cfg);
    DRegionDiagnostics::Snapshot s;
    s.readOk = true; s.pcSelected = true;
    s.rawPressure[0] = 950; s.rawPressure[1] = 960; s.rawPressure[2] = 970;
    auto r = monitor.Analyze(s, 100, 110);
    assert(r.state == PressureBalancedLiftUp::State::Monitoring);
    assert(r.channels.size() == 3 && std::abs(r.spreadMpa - 0.2) < 1e-8);
    s.rawPressure[2] = 1100;
    r = monitor.Analyze(s, 100, 110);
    assert(r.state == PressureBalancedLiftUp::State::Imbalance);
    s.rawPressure[0] = 1000; s.rawPressure[1] = 1010; s.rawPressure[2] = 1000;
    r = monitor.Analyze(s, 100, 110);
    assert(r.state == PressureBalancedLiftUp::State::AllTargetsObserved);
    s.rawPressure[1] = 1300;
    r = monitor.Analyze(s, 100, 110);
    assert(r.state == PressureBalancedLiftUp::State::PressureLimitIndicated);
    s.pressureDisconnected[1] = true;
    assert(monitor.Analyze(s, 100, 110).state == PressureBalancedLiftUp::State::Invalid);
    assert(monitor.Analyze(s, 100, 900).state == PressureBalancedLiftUp::State::Invalid);
    cfg.selected.push_back(cfg.selected[0]);
    assert(PressureBalancedLiftUp(cfg).Analyze(s, 100, 110).state == PressureBalancedLiftUp::State::Invalid);
    std::cout << "Pressure balanced lift offline tests PASS\n";
}
