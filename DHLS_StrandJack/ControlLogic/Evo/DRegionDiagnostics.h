#pragma once
#include "SynchronizationAlgorithm.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// D-region diagnostic integration. Transport intentionally read-only.
// PLC word endianness, signedness and engineering scaling MUST be verified on site.
class DRegionDiagnostics final {
public:
    struct IReadOnlyDWords {
        virtual ~IReadOnlyDWords() = default;
        virtual bool ReadWords(std::uint16_t firstD, std::size_t count,
                               std::vector<std::uint16_t>& out) = 0;
        // Address uses D-word index, NOT a byte address. No write method exists.
    };
    struct Calibration {
        double mmPerCount = 0.0;  // must be explicitly configured from verified PLC spec
        double offsetMm = 0.0;
        bool signed16 = false;    // confirm PLC encoding before use
        bool verified = false;
    };
    struct Config {
        std::array<Calibration,8> stroke{};
        std::array<bool,8> selected{};
        std::array<double,8> startMm{};
        std::array<double,8> targetDeltaMm{};
        SynchronizationAlgorithm::Mode mode = SynchronizationAlgorithm::Mode::CommonDelta;
        SynchronizationAlgorithm::Settings settings{};
        std::uint64_t maxAcquisitionDurationMs = 500;
        Config() { selected.fill(true); }
    };
    struct Snapshot {
        bool readOk = false;
        bool usable = false;
        bool pcSelected = false;
        bool pumpOn = false;
        bool emergencyStop = false;
        bool inverterError = false;
        std::array<bool,8> strokeDisconnected{};
        std::array<bool,8> pressureDisconnected{};
        std::array<std::uint16_t,8> rawStroke{};
        std::array<std::uint16_t,8> rawPressure{};
        std::string reason;
        SynchronizationAlgorithm::Result diagnostic{};
    };

    // All times must use the same monotonic clock, supplied by caller.
    explicit DRegionDiagnostics(Config cfg) : cfg_(cfg), algo_(cfg.settings) {}

    Snapshot Poll(IReadOnlyDWords& io, std::uint64_t beforeMs,
                  std::uint64_t afterMs) const {
        Snapshot s;
        if (afterMs < beforeMs || afterMs-beforeMs > cfg_.maxAcquisitionDurationMs) {
            s.reason = "Acquisition time invalid/stale"; return s;
        }
        // Read minimal, documented D areas. No access to M/P regions.
        std::vector<std::uint16_t> state, measurements, pressureFaults, strokeFaults;
        if (!ReadExact(io,10,1,state) || !ReadExact(io,11,16,measurements) ||
            !ReadExact(io,56,1,pressureFaults) || !ReadExact(io,59,1,strokeFaults)) {
            s.reason = "PLC read failure or incomplete block"; return s;
        }
        s.readOk = true;
        s.emergencyStop = Bit(state[0],0);
        s.pcSelected = Bit(state[0],1);
        s.pumpOn = Bit(state[0],2);
        s.inverterError = Bit(state[0],3);
        std::vector<SynchronizationAlgorithm::Cylinder> cylinders;
        cylinders.reserve(8);
        for (std::size_t i=0;i<8;++i) {
            s.rawPressure[i] = measurements[2*i];      // D11,D13,...D25
            s.rawStroke[i] = measurements[2*i+1];      // D12,D14,...D26
            s.pressureDisconnected[i] = Bit(pressureFaults[0],static_cast<unsigned>(i));
            s.strokeDisconnected[i] = Bit(strokeFaults[0],static_cast<unsigned>(i));
            const auto& cal = cfg_.stroke[i];
            SynchronizationAlgorithm::Cylinder c;
            c.selected = cfg_.selected[i];
            c.startMm = cfg_.startMm[i];
            c.targetDeltaMm = cfg_.targetDeltaMm[i];
            c.sampleTimeMs = afterMs;
            c.quality = SynchronizationAlgorithm::Quality::Valid;
            if (c.selected && (!cal.verified || !(cal.mmPerCount > 0.0) ||
                !std::isfinite(cal.mmPerCount) || !std::isfinite(cal.offsetMm))) {
                s.reason = "Stroke encoding/scaling unverified";
                return s;
            }
            if (s.strokeDisconnected[i]) c.quality = SynchronizationAlgorithm::Quality::Fault;
            const double count = cal.signed16
                ? static_cast<double>(static_cast<std::int16_t>(s.rawStroke[i]))
                : static_cast<double>(s.rawStroke[i]);
            c.currentMm = count * cal.mmPerCount + cal.offsetMm;
            cylinders.push_back(c);
        }
        s.diagnostic = algo_.Analyze(cylinders,cfg_.mode,
            SynchronizationAlgorithm::PlcState::Unknown,afterMs);
        s.usable = s.diagnostic.dataValid;
        if (!s.usable) s.reason = "Sensor fault or invalid diagnostic inputs";
        // Status bits are advisory. D-map contains no confirmed PLC cycle-complete bit.
        return s;
    }
private:
    static bool Bit(std::uint16_t w, unsigned n) { return ((w>>n)&1u)!=0; }
    static bool ReadExact(IReadOnlyDWords& io, std::uint16_t a, std::size_t n,
                          std::vector<std::uint16_t>& v) {
        v.clear(); return io.ReadWords(a,n,v) && v.size()==n;
    }
    Config cfg_;
    SynchronizationAlgorithm algo_;
};
