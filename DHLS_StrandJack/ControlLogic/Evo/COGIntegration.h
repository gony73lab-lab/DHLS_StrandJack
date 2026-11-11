#pragma once

#include "COGCalculator.h"
#include "PressureBalancedLiftUp.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Maps verified, fresh D-region pressure monitor output into COGCalculator.
// Does not add any D-area write capability. Only mapped pressure channels
// (0..7 for the current 8POINT ADDRESS sheet) may be used.
class COGIntegration final {
public:
    struct Geometry {
        std::size_t cylinderId = 0; // D11 + 2*cylinderId
        bool selected = true;
        double xMm = 0.0;
        double yMm = 0.0;
        bool geometryVerified = false;
        double effectiveAreaMm2 = 0.0;
        bool areaVerified = false;
        double tareKN = 0.0;
    };
    struct Result {
        bool inputUsable = false;
        std::string reason;
        COGCalculator::Result cog;
    };

    Result Analyze(const PressureBalancedLiftUp::Result& pressure,
                   const std::vector<Geometry>& geometry,
                   std::size_t referenceCylinderId) const {
        Result out;
        if (pressure.state == PressureBalancedLiftUp::State::Invalid ||
            pressure.channels.empty()) {
            out.reason = "Pressure snapshot invalid; COG calculation inhibited";
            return out;
        }
        std::set<std::size_t> pressureIds;
        for (const auto& c : pressure.channels) {
            if (c.index >= 8 || !pressureIds.insert(c.index).second ||
                !std::isfinite(c.pressureMpa) || c.pressureMpa < 0.0) {
                out.reason = "Invalid or duplicate D-region pressure channel";
                return out;
            }
        }
        std::vector<COGCalculator::Support> supports;
        std::set<std::size_t> selectedIds;
        for (const auto& g : geometry) {
            if (!g.selected) continue;
            if (g.cylinderId >= 8 || !selectedIds.insert(g.cylinderId).second) {
                out.reason = "Unmapped or duplicate selected geometry channel";
                return out;
            }
            auto it = std::find_if(pressure.channels.begin(),pressure.channels.end(),
                [&g](const PressureBalancedLiftUp::Channel& ch){return ch.index==g.cylinderId;});
            if (it==pressure.channels.end()) {
                out.reason = "Missing selected channel in pressure snapshot";
                return out;
            }
            COGCalculator::Support s;
            s.cylinderId = g.cylinderId;
            s.selected = true;
            s.xMm = g.xMm;
            s.yMm = g.yMm;
            s.geometryVerified = g.geometryVerified;
            s.loadSource = COGCalculator::LoadSource::PressureAndArea;
            s.pressureMpa = it->pressureMpa;
            s.pressureVerified = true; // pressure monitor validated calibration and sample age
            s.effectiveAreaMm2 = g.effectiveAreaMm2;
            s.areaVerified = g.areaVerified;
            s.tareKN = g.tareKN;
            supports.push_back(s);
        }
        if (selectedIds != pressureIds) {
            out.reason = "Pressure monitor and COG selected sets differ";
            return out;
        }
        out.cog = calculator_.Calculate(supports, referenceCylinderId);
        out.inputUsable = out.cog.valid();
        out.reason = out.cog.reason;
        return out;
    }
private:
    COGCalculator calculator_;
};
