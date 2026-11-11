#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace DHLS_StrandJack::Control {
// Pure preview calculation. No PLC I/O, state or assumption about commissioned gains.
inline bool ValidDistanceDeceleration(double startMm,double minimumPercent,double toleranceMm) noexcept {
    return std::isfinite(startMm)&&startMm>0&&std::isfinite(minimumPercent)&&minimumPercent>0&&minimumPercent<=100&&
        std::isfinite(toleranceMm)&&toleranceMm>=0&&toleranceMm<startMm;
}
inline double DistanceDecelerationPercent(double remainingMm,double startMm,double minimumPercent,double toleranceMm) noexcept {
    if(!ValidDistanceDeceleration(startMm,minimumPercent,toleranceMm)||!std::isfinite(remainingMm))return std::numeric_limits<double>::quiet_NaN();
    if(remainingMm<=toleranceMm)return 0;
    return minimumPercent+(100-minimumPercent)*std::clamp(remainingMm/startMm,0.0,1.0);
}
}
