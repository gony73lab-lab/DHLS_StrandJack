#pragma once

#include "COGCalculator.h"
#include "DRegionDiagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Live D-region -> COG diagnostic adapter. Uses existing XGT TCP reader via
// DRegionDiagnostics::IReadOnlyDWords; deliberately contains NO PLC write calls.
// This reader does NOT require stroke calibration or PC mode selection: COG
// may be displayed for a stationary load, provided all measurements are valid.
class COGDiagnosticPipeline final {
public:
    struct Channel {
        std::size_t cylinderId = 0;  // 0..7 -> D11, D13, ... D25
        bool selected = true;
        double xMm = 0.0;
        double yMm = 0.0;
        bool geometryVerified = false;
        double mpaPerCount = 0.0;
        double pressureOffsetMpa = 0.0;
        bool pressureSigned16 = false;
        bool pressureCalibrationVerified = false;
        double effectiveAreaMm2 = 0.0;
        bool areaVerified = false;
        double tareKN = 0.0;
    };
    struct Config {
        std::vector<Channel> channels;
        std::size_t referenceCylinderId = 0;
        std::uint64_t maxSampleAgeMs = 500;
        std::uint64_t maxAcquisitionDurationMs = 500;
    };
    struct Result {
        bool acquisitionOk = false;
        bool pressureSensorFault = false;
        std::uint16_t pressureFaultWord = 0;
        std::string reason;
        COGCalculator::Result cog;
        bool valid() const { return acquisitionOk && cog.valid(); }
    };

    explicit COGDiagnosticPipeline(Config cfg) : cfg_(std::move(cfg)) {}

    Result Poll(DRegionDiagnostics::IReadOnlyDWords& reader,
                std::uint64_t beforeMs,
                std::uint64_t afterMs,
                std::uint64_t nowMs) const {
        Result result;
        if (afterMs < beforeMs || nowMs < afterMs ||
            afterMs-beforeMs > cfg_.maxAcquisitionDurationMs ||
            nowMs-afterMs > cfg_.maxSampleAgeMs) {
            result.reason = "Acquisition interval invalid or sample stale";
            return result;
        }
        std::vector<std::uint16_t> data, fault;
        // Read only documented D-region: D11..D26 and D56.
        // Sequential reads are NOT a PLC-atomic snapshot.
        if (!reader.ReadWords(11,16,data) || data.size()!=16 ||
            !reader.ReadWords(56,1,fault) || fault.size()!=1) {
            result.reason = "Incomplete PLC D-region read";
            return result;
        }
        result.acquisitionOk = true;
        result.pressureFaultWord = fault[0];
        std::vector<COGCalculator::Support> supports;
        std::set<std::size_t> used;
        for (const auto& ch : cfg_.channels) {
            if (!ch.selected) continue;
            if (ch.cylinderId>=8 || !used.insert(ch.cylinderId).second) {
                result.reason = "Unmapped or duplicate selected cylinder";
                return result;
            }
            if ((fault[0] & (std::uint16_t(1u)<<ch.cylinderId)) != 0u) {
                result.pressureSensorFault = true;
                result.reason = "PLC pressure sensor disconnection flag asserted";
                return result;
            }
            if (!ch.pressureCalibrationVerified || !std::isfinite(ch.mpaPerCount) ||
                ch.mpaPerCount <= 0.0 || !std::isfinite(ch.pressureOffsetMpa)) {
                result.reason = "Unverified pressure scaling";
                return result;
            }
            const std::uint16_t raw=data[2*ch.cylinderId];
            const double count=ch.pressureSigned16
                ? static_cast<double>(static_cast<std::int16_t>(raw))
                : static_cast<double>(raw);
            const double pressure=count*ch.mpaPerCount+ch.pressureOffsetMpa;
            COGCalculator::Support s;
            s.cylinderId=ch.cylinderId;
            s.xMm=ch.xMm; s.yMm=ch.yMm;
            s.geometryVerified=ch.geometryVerified;
            s.loadSource=COGCalculator::LoadSource::PressureAndArea;
            s.pressureMpa=pressure;
            s.pressureVerified=true;
            s.effectiveAreaMm2=ch.effectiveAreaMm2;
            s.areaVerified=ch.areaVerified;
            s.tareKN=ch.tareKN;
            supports.push_back(s);
        }
        result.cog=calculator_.Calculate(supports,cfg_.referenceCylinderId);
        result.reason=result.cog.reason;
        return result;
    }
private:
    Config cfg_;
    COGCalculator calculator_;
};
