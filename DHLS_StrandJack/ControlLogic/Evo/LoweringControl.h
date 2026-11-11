#pragma once
#include "SynchronizationAlgorithm.h"
#include "XgbXgtTcpReadWrite.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// PC-side lowering command planner and monitor. NOT a safety controller.
// Deliberately does not send commands, generate pulses, or retry writes.
class LoweringControl final {
public:
    enum class Phase { Disabled, Ready, Monitoring, TargetObserved, Fault, Unknown };
    struct Inputs {
        bool commissioned = false;       // signed commissioning approval
        bool commandOwnership = false;   // verified exclusive ownership of D51/D52
        bool plcReadyVerified = false;   // externally verified PLC permissive
        bool plcAlarmFree = false;       // from verified PLC status/alarm signals
        bool plcRunning = false;         // verified, NOT inferred from pump bit
        bool communicationHealthy = false;
        bool sensorCalibrationVerified = false;
        bool coordinatePositiveOnExtend = false;
        bool commandSemanticsVerified = false; // level/pulse/reset/handshake proven
        bool allSelectedStrokeHealthy = false;
        bool snapshotCoherent = false;
    };
    struct Plan {
        Phase phase = Phase::Disabled;
        bool eligibleForManualReview = false;
        std::string reason;
        std::uint16_t selectedCylinderMask = 0;
        double deltaMm = 0.0; // negative means retract / lower for validated coordinate system
        std::vector<SynchronizationAlgorithm::Cylinder> cylinders;
    };
    struct Observation {
        Phase phase = Phase::Unknown;
        SynchronizationAlgorithm::Result diagnostics;
        std::string reason;
    };

    static Plan Prepare(const Inputs& in, std::uint16_t selectionMask,
                        double loweringDistanceMm,
                        const std::vector<double>& startPositionsMm,
                        std::uint64_t sampleTimeMs) {
        Plan p;
        p.selectedCylinderMask = selectionMask;
        if (!std::isfinite(loweringDistanceMm) || loweringDistanceMm <= 0.0) {
            p.reason = "lowering distance must be finite and positive"; return p;
        }
        if ((selectionMask & 0xFFu) == 0 || (selectionMask & 0xFF00u) != 0 ||
            startPositionsMm.size() != 8) {
            p.reason = "select 1..8 cylinders and provide eight starting positions"; return p;
        }
        if (!in.commissioned || !in.commandOwnership || !in.plcReadyVerified ||
            !in.plcAlarmFree || !in.communicationHealthy ||
            !in.sensorCalibrationVerified || !in.coordinatePositiveOnExtend ||
            !in.commandSemanticsVerified || !in.allSelectedStrokeHealthy ||
            !in.snapshotCoherent) {
            p.reason = "commissioning, PLC permissives, sensor or command checks incomplete";
            return p;
        }
        p.deltaMm = -loweringDistanceMm;
        for (std::size_t i = 0; i < 8; ++i) {
            if (!std::isfinite(startPositionsMm[i])) {
                p.reason = "invalid starting stroke";
                p.cylinders.clear(); return p;
            }
            SynchronizationAlgorithm::Cylinder c;
            c.selected = (selectionMask & (1u << i)) != 0;
            c.startMm = startPositionsMm[i];
            c.currentMm = startPositionsMm[i];
            c.targetDeltaMm = p.deltaMm;
            c.sampleTimeMs = sampleTimeMs;
            p.cylinders.push_back(c);
        }
        p.phase = Phase::Ready;
        p.eligibleForManualReview = true;
        p.reason = "planning only; no PLC writes issued";
        return p;
    }

    static Observation Observe(const Plan& plan, const Inputs& in,
                               const std::vector<double>& positionsMm,
                               std::uint64_t sampleTimeMs,
                               std::uint64_t nowMs,
                               const SynchronizationAlgorithm& algorithm) {
        Observation out;
        if (plan.phase != Phase::Ready || positionsMm.size() != 8 ||
            !in.communicationHealthy || !in.plcAlarmFree ||
            !in.allSelectedStrokeHealthy || !in.snapshotCoherent) {
            out.phase = Phase::Fault;
            out.reason = "invalid plan or telemetry: do not infer motion state";
            return out;
        }
        auto cylinders = plan.cylinders;
        for (std::size_t i = 0; i < 8; ++i) {
            cylinders[i].currentMm = positionsMm[i];
            cylinders[i].sampleTimeMs = sampleTimeMs;
        }
        out.diagnostics = algorithm.Analyze(
            cylinders, SynchronizationAlgorithm::Mode::CommonDelta,
            in.plcRunning ? SynchronizationAlgorithm::PlcState::Running
                          : SynchronizationAlgorithm::PlcState::Unknown, nowMs);
        if (!out.diagnostics.dataValid ||
            out.diagnostics.assessment == SynchronizationAlgorithm::Assessment::Invalid) {
            out.phase = Phase::Fault;
            out.reason = "stale, invalid or uncalibrated position";
        } else if (out.diagnostics.assessment ==
                   SynchronizationAlgorithm::Assessment::StopThresholdExceeded) {
            out.phase = Phase::Fault;
            out.reason = "PC diagnostic stop tolerance exceeded; PLC safety response not inferred";
        } else if (out.diagnostics.allTargetsReached) {
            out.phase = Phase::TargetObserved;
            out.reason = "target observed by PC; not a PLC completion acknowledgement";
        } else {
            out.phase = Phase::Monitoring;
            out.reason = "monitoring only; PLC owns motion";
        }
        return out;
    }

    // Validate that a separately approved D51 bit write cannot command UP and DOWN together.
    // It does not establish whether command assertion is safe or permitted.
    static bool ValidateDirectionWord(std::uint16_t proposedD51) {
        constexpr std::uint16_t up = (1u << 1);
        constexpr std::uint16_t down = (1u << 2);
        return (proposedD51 & (up | down)) != (up | down);
    }
};
