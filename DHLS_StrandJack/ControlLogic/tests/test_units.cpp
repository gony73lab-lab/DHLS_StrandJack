#include "../../UnitConversion.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DHLS_StrandJack::Units;
bool Near(double a,double b){return std::abs(a-b)<1e-9*(1+std::abs(b));}
int main(){
 assert(Near(Weight(1,L"KN"),9.80665));assert(Near(Ton(98.0665,L"kN"),10));
 assert(Near(Pressure(700,L"MPa"),70));assert(Near(Pressure(1,L"psi"),14.5037737730209));assert(Near(Pressure(1,L"kg/cm²"),1.01971621297793));
 for(auto u:{L"Ton",L"KN",L"kN",L""})for(double v:{0.,0.01,10.,1000.})assert(Near(Ton(Weight(v,u),u),v));
 for(auto u:{L"bar",L"Mpa",L"MPa",L"psi",L"kg/cm²",L"kgf/cm²",L""}){
  for(double v:{0.,0.01,50.,700.})assert(Near(Bar(Pressure(v,u),u),v));
  assert(Near(Pressure(25,u)/Pressure(50,u),0.5)); // progress invariant
  assert(Near(Bar(Pressure(50,u),u),50)); // target sent to control remains bar
 }
 assert(PressureLabel(L"Mpa")==L"MPa");assert(WeightLabel(L"KN")==L"kN");
 std::cout<<"PASS: Ton/kN and bar/MPa/psi/kgf round trips, legacy aliases, target/progress invariance\n";
}
