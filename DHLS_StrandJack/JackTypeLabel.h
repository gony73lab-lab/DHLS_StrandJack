#pragma once

#include "SystemConfigModels.h"
#include <sstream>

namespace DHLS_StrandJack::Models
{
    // Both settings screens display the same model-defined jack types.
    inline std::wstring JackTypeLabel(JackSpec const& spec)
    {
        if (!spec.TypeName.empty()) return spec.TypeName;
        std::wostringstream text;
        text << spec.Capacity_ton << L" ton";
        return text.str();
    }
}
