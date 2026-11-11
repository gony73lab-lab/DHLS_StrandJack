#include "../DistanceDeceleration.h"
#include <iostream>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<std::endl;std::exit(1);}}while(false)
using namespace DHLS_StrandJack::Control;
int main(){
 const double remaining[]={50,20,10,5,1,.5,0,-1};const double expected[]={100,100,62.5,43.75,28.75,0,0,0};
 for(int i=0;i<8;++i)CHECK(std::abs(DistanceDecelerationPercent(remaining[i],20,25,.5)-expected[i])<1e-9);
 CHECK(std::abs(DistanceDecelerationPercent(14.5,20,25,.5)-79.375)<1e-9);
 CHECK(!ValidDistanceDeceleration(0,25,.5));CHECK(!ValidDistanceDeceleration(20,0,.5));CHECK(!ValidDistanceDeceleration(20,101,.5));CHECK(!ValidDistanceDeceleration(20,25,20));CHECK(!ValidDistanceDeceleration(20,25,-1));
 CHECK(std::isnan(DistanceDecelerationPercent(NAN,20,25,.5)));CHECK(std::isnan(DistanceDecelerationPercent(5,0,25,.5)));
 double previous=100;for(int i=200;i>=0;--i){auto percent=DistanceDecelerationPercent(i*.1,20,25,.5);CHECK(percent>=0&&percent<=100&&percent<=previous+1e-9);previous=percent;}
 CHECK(DistanceDecelerationPercent(5,20,100,.5)==100);CHECK(DistanceDecelerationPercent(0,20,25,0)==0);
 std::cout<<"PASS: reference speed table, 85.5mm example, tolerance stop, monotonic speed, bounds and invalid inputs\n";
}
