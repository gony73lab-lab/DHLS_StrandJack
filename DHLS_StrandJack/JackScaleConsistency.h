#pragma once
#include "SystemConfigModels.h"
#include "JackTypeLabel.h"
#include <cmath>
namespace DHLS_StrandJack::Models {
// Compare configured jack measurement ranges, never ADC counts or unused catalog entries.
inline bool JackMeasurementRangesMatch(SystemConfig const& model) {
    JackSpec const* first=nullptr;
    const int count=model.PLCSettings.PLC_Count*model.PLCSettings.Sensor_HPU_Count;
    auto equal=[](double a,double b){return std::isfinite(a)&&std::isfinite(b)&&a>=0&&b>=0&&std::abs(a-b)<=1e-9*std::max(1.0,std::max(std::abs(a),std::abs(b)));};
    for(int i=0;i<count;++i){
        if(static_cast<std::size_t>(i)>=model.JackSetupDetails.size())return false;
        JackSpec const* selected=nullptr;
        for(auto const& spec:model.JackSettings.Specs)
            if(JackTypeLabel(spec)==model.JackSetupDetails[i].JackSelection){selected=&spec;break;}
        if(!selected)return false;
        if(selected->PressureSensor_ADScale>0&&!equal(selected->PressureSensor_ADScale,model.SensorSettings.PressureSensor_ADScale))return false;
        if(selected->DisplacementSensor_ADScale>0&&!equal(selected->DisplacementSensor_ADScale,model.SensorSettings.DisplacementSensor_ADScale))return false;
        if(!equal(selected->PressureSensorScale_bar,selected->PressureSensorScale_bar)||!equal(selected->DisplacementSensorScale_mm,selected->DisplacementSensorScale_mm))return false;
        if(first&&(!equal(first->PressureSensorScale_bar,selected->PressureSensorScale_bar)||!equal(first->DisplacementSensorScale_mm,selected->DisplacementSensorScale_mm)||!equal(first->JackStroke_mm>0?first->JackStroke_mm:first->DisplacementSensorScale_mm,selected->JackStroke_mm>0?selected->JackStroke_mm:selected->DisplacementSensorScale_mm)))return false;
        first=selected;
    }
    return first!=nullptr;
}
inline std::wstring JackMeasurementRangeWarning(bool english) {
    return english?L"Warning: selected jack AD scales or pressure/displacement measurement ranges differ or a jack type is missing. Automatic operation is blocked. Match AD scales with Control Settings and the bar/mm ranges between jack types.":L"경고: 사용 잭의 AD 스케일 또는 압력·변위 측정범위가 다르거나 잭 종류가 누락되었습니다. 자동운전이 차단됩니다. 잭 AD 스케일은 제어 설정과, bar/mm 측정범위는 잭 종류 간 일치시키세요.";
}
}
