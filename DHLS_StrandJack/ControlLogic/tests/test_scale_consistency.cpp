#include "../../JackScaleConsistency.h"
#include "../EquipmentControlService.h"
#include "../DeveloperSimulation.h"
#include "../Sequence/SimulationSequence.h"
#include <cassert>
using namespace DHLS_StrandJack;
int main(){
 Models::SystemConfig m;m.PLCSettings.PLC_Count=1;m.PLCSettings.Sensor_HPU_Count=2;
 Models::JackSpec a;a.Capacity_ton=10;a.PressureSensorScale_bar=700;a.DisplacementSensorScale_mm=500;
 auto b=a;b.Capacity_ton=20;m.JackSettings.Specs={a,b};m.JackSetupDetails.resize(2);m.JackSetupDetails[0].Port=1;m.JackSetupDetails[1].Port=2;
 m.JackSetupDetails[0].JackSelection=Models::JackTypeLabel(a);m.JackSetupDetails[1].JackSelection=Models::JackTypeLabel(b);
 assert(Models::JackMeasurementRangesMatch(m));
 m.JackSettings.Specs[1].PressureSensorScale_bar=350;assert(!Models::JackMeasurementRangesMatch(m));
 auto o=Control::EquipmentControlService::FromModel(m);assert(!o.automaticScalesMatch);
 Control::DiagnosticRequest request;request.selected[0][0]=true;
 Control::SimulationSequence sequence(o);sequence.Submit("StartRequested",request);assert(sequence.Latest().sequenceMessage.find(L"ranges differ")!=std::wstring::npos);
 Control::DeveloperSimulationEnabled=true;
 Control::EquipmentControlService service(o);service.Start();assert(!service.Submit("StartRequested",request));service.Stop();Control::DeveloperSimulationEnabled=false;
 m.JackSettings.Specs[1].PressureSensorScale_bar=700;m.JackSettings.Specs[1].DisplacementSensorScale_mm=600;assert(!Models::JackMeasurementRangesMatch(m));
 m.JackSettings.Specs[1].DisplacementSensorScale_mm=500;m.SensorSettings.PressureUnit=L"MPa";assert(Models::JackMeasurementRangesMatch(m));
 m.PLCSettings.Sensor_HPU_Count=1;m.JackSettings.Specs[1].PressureSensorScale_bar=350;assert(Models::JackMeasurementRangesMatch(m));
 for(double scale:{4000.,16000.,64000.}){m.SensorSettings.PressureSensor_ADScale=scale;m.SensorSettings.DisplacementSensor_ADScale=scale;auto mapped=Control::EquipmentControlService::FromModel(m);assert(mapped.plcs[0].channels[0].pressureBarPerCount==700/scale);assert(mapped.plcs[0].channels[0].strokeMmPerCount==500/scale);}
 m.SensorSettings.PressureSensor_ADScale=16000;m.SensorSettings.DisplacementSensor_ADScale=16000;
 m.JackSettings.Specs[0].PressureSensor_ADScale=16000;m.JackSettings.Specs[0].DisplacementSensor_ADScale=16000;assert(Models::JackMeasurementRangesMatch(m));
 m.JackSettings.Specs[0].PressureSensor_ADScale=4000;assert(!Models::JackMeasurementRangesMatch(m));assert(Control::EquipmentControlService::FromModel(m).plcs[0].channels[0].pressureBarPerCount==700./4000);
 m.JackSettings.Specs[0].PressureSensor_ADScale=16000;m.JackSettings.Specs[0].DisplacementSensor_ADScale=64000;assert(!Models::JackMeasurementRangesMatch(m));
}

