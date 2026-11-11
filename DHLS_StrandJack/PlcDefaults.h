#pragma once
#include <string>
namespace DHLS_StrandJack::Models {
// Only PLC1 was commissioned at this address. Other PLC addresses remain unchanged.
inline std::wstring DefaultPlcIp(int zeroBasedIndex) {
    return zeroBasedIndex==0 ? std::wstring{L"192.168.10.124"}
                            : std::wstring{L"192.168.0."}+std::to_wstring(zeroBasedIndex+1);
}
}
