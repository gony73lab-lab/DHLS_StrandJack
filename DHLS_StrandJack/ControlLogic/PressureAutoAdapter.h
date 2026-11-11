#pragma once
#include "EquipmentControlService.h"
namespace DHLS_StrandJack::Control {
inline PressureInput PressureFeedback(ServiceOptions const& o,DiagnosticRequest const& r,EquipmentSnapshot const& s){
    PressureInput in;in.sampleId=s.sequence+1;in.atMs=EquipmentControlService::MonotonicMs();in.communication=true;in.permitted=true;in.outputsOff=true;in.emergency=s.emergencyActive||s.faultActive;
    for(std::size_t set=0;set<o.plcs.size();++set){if(set>=s.plcs.size()){in.communication=false;continue;}auto const& plc=s.plcs[set];
        bool selected=false;for(bool v:r.selected[set])selected|=v;if(!selected)continue;
        in.communication &= plc.usable&&plc.connected&&plc.readOk;in.permitted &= s.simulation||plc.pcSelected;in.atMs=std::min(in.atMs,plc.sampledAtMs);
        for(std::size_t p=0;p<o.plcs[set].channels.size();++p){auto const& c=o.plcs[set].channels[p];if(p>=plc.ports.size()){in.communication=false;continue;}auto const& v=plc.ports[p];
            const double maxP=o.maximumStopPressureBar>0?std::min(c.maximumPressureBar,o.maximumStopPressureBar):c.maximumPressureBar;
            in.cylinders.push_back({static_cast<int>(set*8+p+1),r.selected[set][p],v.pressureValid&&v.strokeValid,s.simulation||(c.scalingVerified&&c.areaVerified),s.sequenceStopConfirmed,
                v.pressureBar/10,v.absoluteMm,c.strokeLimitMm,maxP/10,c.effectiveAreaMm2,c.maximumLoadTon});
        }
    }in.sampleId=s.sequence?s.sequence:in.atMs;return in;
}
}
