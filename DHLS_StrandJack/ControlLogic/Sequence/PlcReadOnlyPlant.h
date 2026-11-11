#pragma once
#include "SequenceEngine.h"
#include "../EquipmentControlService.h"
namespace DHLS_StrandJack::Control {
class PlcReadOnlyPlant final : public DHLS::IPlant {
    std::function<EquipmentSnapshot()> latest_;
public:
    explicit PlcReadOnlyPlant(std::function<EquipmentSnapshot()> latest):latest_(std::move(latest)){}
    DHLS::PlantStatus Read() override {
        auto s=latest_();DHLS::PlantStatus p;
        p.sampleId=s.sequence;p.emergency=s.emergencyActive;p.communicationOk=!s.plcs.empty();
        p.sampledAtMs=DHLS::SequenceNowMs();
        for(std::size_t i=0;i<s.plcs.size();++i){auto const& plc=s.plcs[i];
            p.communicationOk &= plc.connected&&plc.readOk&&plc.usable;
            p.sampledAtMs=(std::min)(p.sampledAtMs,plc.sampledAtMs);
            for(std::size_t j=0;j<plc.ports.size();++j){auto const& port=plc.ports[j];
                p.jacks.push_back({static_cast<int>(i*8+j+1),port.absoluteMm,port.pressureBar,0,0,
                    port.pressureValid&&port.strokeValid,false,false,false});
            }
        }
        p.operationAllowed=false;return p; // D10/PLC feedback is insufficient to prove safe motion readiness.
    }
    bool Apply(std::vector<DHLS::JackCommand> const&) override{return false;}
    bool StopAll() override{return false;} // No commissioned physical stop feedback/neutral outputs.
};
}
