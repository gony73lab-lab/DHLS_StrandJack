#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

// PC-side monitoring/diagnostics ONLY. Never use this class to actuate valves or implement safety stops.
class SynchronizationAlgorithm final {
public:
    enum class Mode { CommonDelta, ProportionalTilt };
    enum class Quality { Valid, Missing, Fault };
    enum class Assessment { Invalid, Normal, CorrectionIndicated, StopThresholdExceeded };
    enum class PlcState { Unknown, Idle, Running, Completed, Stopped, Alarm };

    struct Cylinder {
        bool selected = true;
        double startMm = 0.0;          // PLC-calibrated position at cycle start
        double currentMm = 0.0;        // PLC-calibrated position in same frame
        double targetDeltaMm = 0.0;    // signed commanded travel
        Quality quality = Quality::Valid;
        std::uint64_t sampleTimeMs = 0; // monotonic timestamp in caller's clock domain
    };
    struct Settings {
        double workToleranceMm = 2.0;
        double stopToleranceMm = 6.0;
        double targetToleranceMm = 0.5;
        std::uint64_t maxSampleAgeMs = 500;
        // Physical mm spread is not a valid tilt stop metric; only common-delta mode uses it.
    };
    struct CylinderResult {
        std::size_t index = 0; // zero-based
        double progressMm = 0.0; // direction-normalized distance traveled
        double targetErrorMm = 0.0; // signed target minus current in physical coordinates
        double normalizedProgress = 0.0; // relative fraction for tilt mode
        bool targetReached = false;
    };
    struct Result {
        Assessment assessment = Assessment::Invalid;
        PlcState plcState = PlcState::Unknown;
        bool dataValid = false;
        bool allTargetsReached = false;
        bool plcCompleted = false;
        double averageProgressMm = 0.0;
        double workErrorMm = 0.0;
        double stopErrorMm = 0.0;
        // Tilt-mode diagnostics are dimensionless and are NOT compared to mm tolerances.
        double tiltProgressSpread = 0.0;
        std::optional<std::size_t> slowest;
        std::optional<std::size_t> fastest;
        std::vector<CylinderResult> cylinders;
    };

    explicit SynchronizationAlgorithm(Settings settings) : settings_(settings) {
        if (!finite(settings.workToleranceMm) || !finite(settings.stopToleranceMm) ||
            !finite(settings.targetToleranceMm) || settings.workToleranceMm <= 0 ||
            settings.stopToleranceMm < 3.0 * settings.workToleranceMm ||
            settings.targetToleranceMm < 0 || settings.maxSampleAgeMs == 0)
            throw std::invalid_argument("Invalid tolerance or sampling settings");
    }

    Result Analyze(const std::vector<Cylinder>& cylinders, Mode mode,
                   PlcState plcState, std::uint64_t nowMs) const {
        Result out;
        out.plcState = plcState;
        out.plcCompleted = (plcState == PlcState::Completed);
        if (cylinders.empty()) return out;

        std::vector<double> progress;
        std::vector<double> normalized;
        std::vector<std::size_t> indices;
        int direction = 0;
        double maxAbsDelta = 0.0;
        for (std::size_t i = 0; i < cylinders.size(); ++i) {
            const auto& c = cylinders[i];
            if (!c.selected) continue;
            if (c.quality != Quality::Valid || !finite(c.startMm) ||
                !finite(c.currentMm) || !finite(c.targetDeltaMm) ||
                c.sampleTimeMs > nowMs || nowMs - c.sampleTimeMs > settings_.maxSampleAgeMs ||
                c.targetDeltaMm == 0.0) return out;
            int d = (c.targetDeltaMm > 0.0) ? 1 : -1;
            if (direction && direction != d) return out; // mixed directions are unsupported
            direction = d;
            maxAbsDelta = std::max(maxAbsDelta, std::abs(c.targetDeltaMm));
            indices.push_back(i);
        }
        if (indices.empty()) return out;
        if (mode == Mode::CommonDelta) {
            const double referenceDelta = cylinders[indices.front()].targetDeltaMm;
            for (auto i : indices)
                if (std::abs(cylinders[i].targetDeltaMm - referenceDelta) > 1e-9)
                    return out;
        }
        out.allTargetsReached = true;
        for (auto i : indices) {
            const auto& c = cylinders[i];
            const double travel = c.currentMm - c.startMm;
            const double p = static_cast<double>(direction) * travel;
            const double error = (c.startMm + c.targetDeltaMm) - c.currentMm;
            const double ratio = p / std::abs(c.targetDeltaMm);
            if (!finite(p) || !finite(error) || !finite(ratio)) return Result{};
            bool reached = std::abs(error) <= settings_.targetToleranceMm;
            out.allTargetsReached &= reached;
            out.cylinders.push_back({i, p, error, ratio, reached});
            progress.push_back(p);
            normalized.push_back(ratio);
        }
        out.dataValid = true;
        const auto [minIt, maxIt] = std::minmax_element(progress.begin(), progress.end());
        const auto [minRatio, maxRatio] = std::minmax_element(normalized.begin(), normalized.end());
        out.tiltProgressSpread = *maxRatio - *minRatio;
        // Stable mean for finite, bounded mechanical position values.
        double mean = 0.0;
        for (std::size_t i = 0; i < progress.size(); ++i)
            mean += (progress[i] - mean) / static_cast<double>(i + 1);
        out.averageProgressMm = mean;
        out.workErrorMm = std::max(0.0, mean - *minIt);
        out.stopErrorMm = *maxIt - *minIt;
        out.slowest = indices[static_cast<std::size_t>(minIt - progress.begin())];
        out.fastest = indices[static_cast<std::size_t>(maxIt - progress.begin())];
        if (mode == Mode::CommonDelta) {
            out.assessment = out.stopErrorMm > settings_.stopToleranceMm
                ? Assessment::StopThresholdExceeded
                : out.workErrorMm > settings_.workToleranceMm
                    ? Assessment::CorrectionIndicated : Assessment::Normal;
        } else {
            // No inferred tilt threshold: requires a verified PLC specification.
            out.assessment = Assessment::Normal;
        }
        return out;
    }
private:
    static bool finite(double x) { return std::isfinite(x); }
    Settings settings_;
};
