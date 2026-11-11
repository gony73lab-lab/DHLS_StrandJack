#pragma once
#include "EquipmentControlService.h"
namespace DHLS_StrandJack::Control {
// A shared arrow is green only when every configured PLC confirms the same direction.
inline unsigned ConfirmedPlcDirection(EquipmentSnapshot const& state){
    unsigned common=0;if(state.plcs.empty())return 0;
    for(auto const& plc:state.plcs){
        if(!plc.connected)return 0;
        unsigned direction=0;bool known=false;
        for(auto const& word:plc.mapWords)if(word.family=='D'&&word.address==51&&word.valid){direction=word.value&6;known=true;break;}
        if(!known||(direction!=2&&direction!=4))return 0;
        if(common&&direction!=common)return 0;common=direction;
    }
    return common;
}
}
