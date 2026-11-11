#pragma once
#include "UnitConversion.h"
#include "ConfigManager.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
namespace DHLS_StrandJack::DisplayUnits {
inline auto const& Settings(){return Services::ConfigManager::Instance().GetConfig().SensorSettings;}
inline double Weight(double ton){return Units::Weight(ton,Settings().WeightUnit);}
inline double Ton(double value){return Units::Ton(value,Settings().WeightUnit);}
inline double Pressure(double bar){return Units::Pressure(bar,Settings().PressureUnit);}
inline double Bar(double value){return Units::Bar(value,Settings().PressureUnit);}
inline std::wstring WeightLabel(){return Units::WeightLabel(Settings().WeightUnit);}
inline std::wstring PressureLabel(){return Units::PressureLabel(Settings().PressureUnit);}
inline int DisplacementDigits(){return (std::clamp)(Settings().DisplacementDisplayDigits,0,3);}
inline int PressureDigits(){return PressureLabel()==L"bar"?0:PressureLabel()==L"MPa"?3:1;}
inline std::wstring DisplacementText(double mm){if(!std::isfinite(mm))return L"--";std::wostringstream s;s<<std::fixed<<std::setprecision(DisplacementDigits())<<mm;return s.str();}
inline std::wstring PressureText(double bar){if(!std::isfinite(bar))return L"-- "+PressureLabel();std::wostringstream s;s<<std::fixed<<std::setprecision(PressureDigits())<<Pressure(bar)<<L" "<<PressureLabel();return s.str();}
inline std::wstring JackLabel(Models::JackSpec const& spec){if(!spec.TypeName.empty())return spec.TypeName;std::wostringstream s;s<<std::fixed<<std::setprecision(1)<<Weight(spec.Capacity_ton)<<L" "<<WeightLabel();return s.str();}
}
