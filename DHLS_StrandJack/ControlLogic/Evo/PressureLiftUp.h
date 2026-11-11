#pragma once
#include "DRegionDiagnostics.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

// Advisory pressure-assisted LIFT-UP monitor, not an actuator or safety function.
// D11,13,...25 = pressure raw; D56 bit 0..7 = P.T disconnect.
// D51.1 = PC UP request, but this class NEVER writes it.
class PressureLiftUp final {
public:
    enum class State { Invalid, Monitoring, PressureTargetObserved, LimitIndicated };
    struct ChannelCalibration {
        double mpaPerCount = 0.0;
        double offsetMpa = 0.0;
        bool signed16 = false;
        bool verified = false;
        double pistonAreaCm2 = 0.0; // optional, verified effective area for force estimate
    };
    struct Config {
        std::array<ChannelCalibration,8> pressure{};
        std::array<bool,8> selected{};
        std::array<double,8> targetMpa{};
        std::array<double,8> maxAdvisoryMpa{};
        double targetToleranceMpa = 0.1;
        std::uint64_t maxSampleAgeMs = 500;
        Config() { selected.fill(false); }
    };
    struct Channel {
        bool selected = false;
        bool valid = false;
        double pressureMpa = 0.0;
        double targetErrorMpa = 0.0;
        double estimatedForceKN = std::numeric_limits<double>::quiet_NaN();
        bool targetObserved = false;
        bool advisoryLimitExceeded = false;
    };
    struct Result {
        State state = State::Invalid;
        std::array<Channel,8> channels{};
        bool allTargetsObserved = false;
        bool anyLimitExceeded = false;
        std::string reason;
    };
    explicit PressureLiftUp(Config cfg) : cfg_(cfg) {}

    Result Analyze(const DRegionDiagnostics::Snapshot& s,
                   std::uint64_t sampleTimeMs,
                   std::uint64_t nowMs) const {
        Result r;
        if (!s.readOk || sampleTimeMs > nowMs ||
            nowMs - sampleTimeMs > cfg_.maxSampleAgeMs ||
            !std::isfinite(cfg_.targetToleranceMpa) || cfg_.targetToleranceMpa < 0) {
            r.reason = "PLC data missing/stale or invalid tolerance"; return r;
        }
        // Do not interpret unknown E-stop polarity as an operational permissive.
        if (!s.pcSelected || s.inverterError) {
            r.reason = "PC mode not selected or inverter fault indicated"; return r;
        }
        bool any = false;
        r.allTargetsObserved = true;
        for (std::size_t i=0; i<8; ++i) {
            auto& c = r.channels[i]; c.selected = cfg_.selected[i];
            if (!c.selected) continue;
            any = true;
            const auto& cal = cfg_.pressure[i];
            if (s.pressureDisconnected[i] || !cal.verified ||
                !std::isfinite(cal.mpaPerCount) || cal.mpaPerCount <= 0 ||
                !std::isfinite(cal.offsetMpa) ||
                !std::isfinite(cfg_.targetMpa[i]) || cfg_.targetMpa[i] <= 0 ||
                !std::isfinite(cfg_.maxAdvisoryMpa[i]) ||
                cfg_.maxAdvisoryMpa[i] < cfg_.targetMpa[i]) {
                r.reason = "P.T fault or unverified pressure configuration"; return r;
            }
            const double raw = cal.signed16
                ? static_cast<double>(static_cast<std::int16_t>(s.rawPressure[i]))
                : static_cast<double>(s.rawPressure[i]);
            c.pressureMpa = raw*cal.mpaPerCount + cal.offsetMpa;
            if (!std::isfinite(c.pressureMpa) || c.pressureMpa < 0) {
                r.reason = "Invalid converted pressure"; return r;
            }
            c.valid = true;
            c.targetErrorMpa = cfg_.targetMpa[i] - c.pressureMpa;
            c.targetObserved = c.pressureMpa >= cfg_.targetMpa[i] - cfg_.targetToleranceMpa;
            c.advisoryLimitExceeded = c.pressureMpa > cfg_.maxAdvisoryMpa[i];
            r.allTargetsObserved &= c.targetObserved;
            r.anyLimitExceeded |= c.advisoryLimitExceeded;
            if (std::isfinite(cal.pistonAreaCm2) && cal.pistonAreaCm2 > 0)
                c.estimatedForceKN = c.pressureMpa * cal.pistonAreaCm2 * 0.1;
        }
        if (!any) { r.reason = "No cylinders selected"; return r; }
        r.state = r.anyLimitExceeded ? State::LimitIndicated :
                  r.allTargetsObserved ? State::PressureTargetObserved : State::Monitoring;
        r.reason = r.anyLimitExceeded ? "PC advisory pressure limit exceeded; PLC response unknown" :
                   r.allTargetsObserved ? "Pressure target observed; NOT lift completion" :
                   "Pressure monitoring only; PLC owns movement";
        return r;
    }
private:
    Config cfg_;
};
