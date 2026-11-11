#pragma once
#include <string>
#include <cwctype>
namespace DHLS_StrandJack::Units {
inline std::wstring Lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
inline double WeightFactor(std::wstring unit){return Lower(unit)==L"kn"?9.80665:1.0;}
inline double PressureFactor(std::wstring unit){unit=Lower(unit);if(unit==L"mpa")return 0.1;if(unit==L"psi")return 14.5037737730209;if(unit==L"kg/cm²"||unit==L"kgf/cm²"||unit==L"kg/cm2"||unit==L"kgf/cm2")return 1.01971621297793;return 1.0;}
inline double Weight(double ton,std::wstring const& unit){return ton*WeightFactor(unit);}
inline double Ton(double value,std::wstring const& unit){return value/WeightFactor(unit);}
inline double Pressure(double bar,std::wstring const& unit){return bar*PressureFactor(unit);}
inline double Bar(double value,std::wstring const& unit){return value/PressureFactor(unit);}
inline std::wstring WeightLabel(std::wstring const& unit){return WeightFactor(unit)==1?L"Ton":L"kN";}
inline std::wstring PressureLabel(std::wstring unit){auto lower=Lower(unit);if(lower==L"mpa")return L"MPa";if(lower==L"psi")return L"psi";if(PressureFactor(unit)>1.019&&PressureFactor(unit)<1.020)return L"kgf/cm²";return L"bar";}
}
