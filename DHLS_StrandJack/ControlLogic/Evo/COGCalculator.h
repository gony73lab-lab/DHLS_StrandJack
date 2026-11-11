#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <vector>

// Center-of-gravity estimate from verified, simultaneous vertical support reactions.
// This class is diagnostic ONLY: no PLC I/O, actuator control, or safety interlocks.
// X/Y coordinates are in millimeters in one common coordinate system.
// Result coordinates are relative to the selected reference support.
class COGCalculator final {
public:
    enum class LoadSource { VerifiedLoadKN, PressureAndArea };
    enum class Status {
        Valid,
        NotEnoughSupports,
        InvalidInput,
        MissingReference,
        InvalidGeometry,
        InvalidLoad,
        NoSupportedLoad
    };

    struct Support {
        std::size_t cylinderId = 0; // 0-based; independent of PLC D address mapping
        bool selected = true;
        double xMm = 0.0;
        double yMm = 0.0;
        bool geometryVerified = false;

        LoadSource loadSource = LoadSource::VerifiedLoadKN;
        double loadKN = 0.0;            // only for VerifiedLoadKN
        bool loadVerified = false;       // true only for a calibrated, validated load input

        double pressureMpa = 0.0;        // only for PressureAndArea
        double effectiveAreaMm2 = 0.0;   // effective pressure area, not always piston outer area
        double tareKN = 0.0;            // optional known preload / tare to subtract
        bool pressureVerified = false;  // valid, fresh, calibrated pressure measurement
        bool areaVerified = false;      // hydraulic geometry and direction verified
    };

    struct Contribution {
        std::size_t cylinderId = 0;
        double xRelativeMm = 0.0;
        double yRelativeMm = 0.0;
        double forceKN = 0.0;
        double sharePercent = 0.0;
    };

    struct Result {
        Status status = Status::InvalidInput;
        std::string reason;
        std::size_t referenceCylinderId = 0;
        std::size_t selectedCount = 0;
        double totalForceKN = 0.0;
        double cogXMm = 0.0;
        double cogYMm = 0.0;
        double distanceFromReferenceMm = 0.0;
        std::vector<Contribution> supports;
        bool valid() const { return status == Status::Valid; }
    };

    Result Calculate(const std::vector<Support>& supports,
                     std::size_t referenceCylinderId) const {
        Result result;
        result.referenceCylinderId = referenceCylinderId;
        std::set<std::size_t> allIds;
        std::vector<const Support*> selected;
        selected.reserve(supports.size());

        // Reject duplicate IDs even if one copy is not selected: mapping ambiguity.
        for (const auto& s : supports) {
            if (!allIds.insert(s.cylinderId).second)
                return Fail(result, Status::InvalidInput, "Duplicate cylinder ID");
            if (s.selected) selected.push_back(&s);
        }
        result.selectedCount = selected.size();
        if (selected.size() < 3)
            return Fail(result, Status::NotEnoughSupports,
                        "At least 3 non-collinear loaded supports required for a 2D estimate");

        const Support* ref = nullptr;
        for (const auto* s : selected) {
            if (s->cylinderId == referenceCylinderId) ref = s;
            if (!s->geometryVerified || !std::isfinite(s->xMm) || !std::isfinite(s->yMm))
                return Fail(result, Status::InvalidGeometry,
                            "Unverified or non-finite support coordinates");
        }
        if (!ref)
            return Fail(result, Status::MissingReference, "Reference cylinder is not selected");

        double maxDistance = 0.0;
        for (const auto* s : selected)
            maxDistance = std::max(maxDistance, std::hypot(s->xMm-ref->xMm,
                                                         s->yMm-ref->yMm));
        if (!std::isfinite(maxDistance) || maxDistance <= 0.0)
            return Fail(result, Status::InvalidGeometry, "Zero or invalid support footprint");

        // Check non-collinearity in a scale-independent way.
        double maxCross = 0.0;
        for (std::size_t i=0;i<selected.size();++i) {
            const double xi=selected[i]->xMm-ref->xMm;
            const double yi=selected[i]->yMm-ref->yMm;
            for (std::size_t j=i+1;j<selected.size();++j) {
                const double xj=selected[j]->xMm-ref->xMm;
                const double yj=selected[j]->yMm-ref->yMm;
                maxCross = std::max(maxCross, std::abs(xi*yj-yi*xj));
            }
        }
        if (!std::isfinite(maxCross) || maxCross <= maxDistance*maxDistance*1e-10)
            return Fail(result, Status::InvalidGeometry,
                        "Selected supports are collinear or nearly collinear");

        // Use a force-weighted incremental centroid rather than summing F*x directly.
        // This reduces avoidable overflow/cancellation for large coordinate offsets.
        double total = 0.0, centroidX = 0.0, centroidY = 0.0;
        std::vector<const Support*> loadBearing;
        for (const auto* s : selected) {
            double force = 0.0;
            if (s->loadSource == LoadSource::VerifiedLoadKN) {
                if (!s->loadVerified || !std::isfinite(s->loadKN) || s->loadKN < 0.0)
                    return Fail(result, Status::InvalidLoad, "Load is not verified or is invalid");
                force = s->loadKN;
            } else if (s->loadSource == LoadSource::PressureAndArea) {
                if (!s->pressureVerified || !s->areaVerified ||
                    !std::isfinite(s->pressureMpa) || s->pressureMpa < 0.0 ||
                    !std::isfinite(s->effectiveAreaMm2) || s->effectiveAreaMm2 <= 0.0 ||
                    !std::isfinite(s->tareKN) || s->tareKN < 0.0)
                    return Fail(result, Status::InvalidLoad,
                                "Pressure/area/tare not verified or invalid");
                // 1 MPa = 1 N/mm^2; 1000 N = 1 kN.
                force = s->pressureMpa * s->effectiveAreaMm2 / 1000.0 - s->tareKN;
                if (!std::isfinite(force) || force < 0.0)
                    return Fail(result, Status::InvalidLoad,
                                "Computed reaction is negative or non-finite");
            } else {
                return Fail(result, Status::InvalidLoad, "Unknown load source");
            }

            if (force > 0.0) loadBearing.push_back(s);
            Contribution c;
            c.cylinderId = s->cylinderId;
            c.xRelativeMm = s->xMm - ref->xMm;
            c.yRelativeMm = s->yMm - ref->yMm;
            c.forceKN = force;
            if (!std::isfinite(c.xRelativeMm) || !std::isfinite(c.yRelativeMm))
                return Fail(result, Status::InvalidGeometry, "Relative coordinate overflow");
            result.supports.push_back(c);

            const double nextTotal = total + force;
            if (!std::isfinite(nextTotal))
                return Fail(result, Status::InvalidLoad, "Total force overflow");
            if (force > 0.0) {
                centroidX += (c.xRelativeMm-centroidX)*(force/nextTotal);
                centroidY += (c.yRelativeMm-centroidY)*(force/nextTotal);
            }
            total = nextTotal;
        }
        if (!(total > 0.0))
            return Fail(result, Status::NoSupportedLoad, "Total supported force is zero");
        if (loadBearing.size() < 3)
            return Fail(result, Status::NotEnoughSupports,
                        "Fewer than 3 load-bearing supports for a 2D estimate");
        double loadedCross = 0.0;
        const Support* loadedOrigin = loadBearing.front();
        for (std::size_t i=1;i<loadBearing.size();++i) {
            const double xi=loadBearing[i]->xMm-loadedOrigin->xMm;
            const double yi=loadBearing[i]->yMm-loadedOrigin->yMm;
            for (std::size_t j=i+1;j<loadBearing.size();++j) {
                const double xj=loadBearing[j]->xMm-loadedOrigin->xMm;
                const double yj=loadBearing[j]->yMm-loadedOrigin->yMm;
                loadedCross=std::max(loadedCross,std::abs(xi*yj-yi*xj));
            }
        }
        if (loadedCross <= maxDistance*maxDistance*1e-10)
            return Fail(result, Status::InvalidGeometry,
                        "Load-bearing supports do not span a 2D area");
        if (!std::isfinite(centroidX) || !std::isfinite(centroidY))
            return Fail(result, Status::InvalidLoad, "COG arithmetic invalid");
        result.totalForceKN = total;
        result.cogXMm = centroidX;
        result.cogYMm = centroidY;
        result.distanceFromReferenceMm = std::hypot(centroidX, centroidY);
        for (auto& c : result.supports)
            c.sharePercent = (c.forceKN / total) * 100.0;
        result.status = Status::Valid;
        result.reason = "2D projected COG estimate from vertical support reactions; advisory only";
        return result;
    }

private:
    static Result Fail(Result& r, Status status, const char* why) {
        // Never expose a partial set of contributions as a usable COG result.
        r.status=status;
        r.reason=why;
        r.supports.clear();
        r.totalForceKN=0.0;
        r.cogXMm=r.cogYMm=r.distanceFromReferenceMm=0.0;
        return r;
    }
};
