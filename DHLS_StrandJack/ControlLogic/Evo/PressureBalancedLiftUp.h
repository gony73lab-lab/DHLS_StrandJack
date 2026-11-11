#pragma once
#include "DRegionDiagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// PC-side advisory pressure balance/target monitor ONLY. No actuator writes.
// Current 8POINT D-map supports up to 8 mapped channels; vector size is variable.
class PressureBalancedLiftUp final {
public:
    enum class State { Invalid, Monitoring, Imbalance, AllTargetsObserved, PressureLimitIndicated };
    struct Cylinder {
        std::size_t index = 0; // 0..7 maps to D(11 + 2*index)
        double mpaPerCount = 0;
        double offsetMpa = 0;
        bool signed16 = false;
        bool calibrationVerified = false;
        double targetMpa = 0;
        double advisoryMaxMpa = 0;
    };
    struct Config {
        std::vector<Cylinder> selected;
        double maxSpreadMpa = 0; // advisory max-min among selected channels
        double targetToleranceMpa = 0;
        std::uint64_t maxSampleAgeMs = 500;
    };
    struct Channel {
        std::size_t index = 0;
        double pressureMpa = 0;
        double targetErrorMpa = 0;
        bool targetObserved = false;
        bool advisoryLimitExceeded = false;
        double deviationFromMeanMpa = 0;
    };
    struct Result {
        State state = State::Invalid;
        std::vector<Channel> channels;
        double meanMpa = 0;
        double minMpa = 0;
        double maxMpa = 0;
        double spreadMpa = 0;
        bool balanceWithinTolerance = false;
        bool allTargetsObserved = false;
        bool anyLimitExceeded = false;
        bool strokeDiagnosticUsable = false;
        std::string reason;
    };
    explicit PressureBalancedLiftUp(Config config) : config_(std::move(config)) {}

    Result Analyze(const DRegionDiagnostics::Snapshot& snapshot,
                   std::uint64_t sampledAtMs, std::uint64_t nowMs) const {
        Result out;
        if (!snapshot.readOk || sampledAtMs > nowMs ||
            nowMs - sampledAtMs > config_.maxSampleAgeMs ||
            !std::isfinite(config_.maxSpreadMpa) || config_.maxSpreadMpa < 0 ||
            !std::isfinite(config_.targetToleranceMpa) || config_.targetToleranceMpa < 0 ||
            config_.selected.empty() || config_.selected.size() > 8) {
            out.reason = "Missing/stale data or invalid configuration";
            return out;
        }
        // PLC mode/fault bits are indicators, not independent motion permissives.
        if (!snapshot.pcSelected || snapshot.inverterError) {
            out.reason = "PC mode not selected or inverter fault indicated";
            return out;
        }
        bool used[8] = {};
        double sum = 0;
        out.minMpa = std::numeric_limits<double>::infinity();
        out.maxMpa = -std::numeric_limits<double>::infinity();
        out.allTargetsObserved = true;
        for (const auto& cfg : config_.selected) {
            if (cfg.index >= 8 || used[cfg.index] ||
                snapshot.pressureDisconnected[cfg.index] ||
                !cfg.calibrationVerified || !std::isfinite(cfg.mpaPerCount) ||
                cfg.mpaPerCount <= 0 || !std::isfinite(cfg.offsetMpa) ||
                !std::isfinite(cfg.targetMpa) || cfg.targetMpa <= 0 ||
                !std::isfinite(cfg.advisoryMaxMpa) ||
                cfg.advisoryMaxMpa < cfg.targetMpa) {
                out.reason = "Invalid channel mapping, calibration, or pressure sensor";
                return out;
            }
            used[cfg.index] = true;
            const double raw = cfg.signed16
                ? static_cast<double>(static_cast<std::int16_t>(snapshot.rawPressure[cfg.index]))
                : static_cast<double>(snapshot.rawPressure[cfg.index]);
            const double pressure = raw * cfg.mpaPerCount + cfg.offsetMpa;
            if (!std::isfinite(pressure) || pressure < 0) {
                out.reason = "Invalid converted pressure";
                return out;
            }
            Channel ch;
            ch.index = cfg.index;
            ch.pressureMpa = pressure;
            ch.targetErrorMpa = cfg.targetMpa - pressure;
            ch.targetObserved = pressure >= cfg.targetMpa - config_.targetToleranceMpa;
            ch.advisoryLimitExceeded = pressure > cfg.advisoryMaxMpa;
            out.channels.push_back(ch);
            sum += pressure;
            out.minMpa = std::min(out.minMpa, pressure);
            out.maxMpa = std::max(out.maxMpa, pressure);
            out.allTargetsObserved &= ch.targetObserved;
            out.anyLimitExceeded |= ch.advisoryLimitExceeded;
        }
        out.meanMpa = sum / static_cast<double>(out.channels.size());
        out.spreadMpa = out.maxMpa - out.minMpa;
        out.balanceWithinTolerance = out.spreadMpa <= config_.maxSpreadMpa;
        for (auto& ch : out.channels)
            ch.deviationFromMeanMpa = ch.pressureMpa - out.meanMpa;
        out.strokeDiagnosticUsable = snapshot.usable && snapshot.diagnostic.dataValid;
        out.state = out.anyLimitExceeded ? State::PressureLimitIndicated :
                    !out.balanceWithinTolerance ? State::Imbalance :
                    out.allTargetsObserved ? State::AllTargetsObserved : State::Monitoring;
        out.reason = out.anyLimitExceeded ? "Advisory pressure ceiling exceeded" :
                     !out.balanceWithinTolerance ? "Pressure imbalance observed" :
                     out.allTargetsObserved ? "Targets observed; not PLC lift completion" :
                     "Pressure targets not all observed";
        return out;
    }
private:
    Config config_;
};
