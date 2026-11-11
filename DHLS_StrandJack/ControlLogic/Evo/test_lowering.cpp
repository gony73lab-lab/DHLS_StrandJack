#include "LoweringControl.h"
#include <cassert>
#include <iostream>
int main() {
    using L=LoweringControl;
    L::Inputs in;
    auto blocked=L::Prepare(in,0x03,10.0,std::vector<double>(8,100.0),1000);
    assert(!blocked.eligibleForManualReview);
    in.commissioned=in.commandOwnership=in.plcReadyVerified=in.plcAlarmFree=true;
    in.communicationHealthy=in.sensorCalibrationVerified=in.coordinatePositiveOnExtend=true;
    in.commandSemanticsVerified=in.allSelectedStrokeHealthy=in.snapshotCoherent=true;
    auto p=L::Prepare(in,0x03,10.0,std::vector<double>(8,100.0),1000);
    assert(p.eligibleForManualReview && p.deltaMm==-10.0);
    assert(!L::ValidateDirectionWord(0x0006));
    assert(L::ValidateDirectionWord(0x0004));
    SynchronizationAlgorithm a({2.0,6.0,0.5,500});
    auto current=std::vector<double>(8,100.0);
    current[0]=95.0;current[1]=94.0;
    auto o=L::Observe(p,in,current,1100,1150,a);
    assert(o.phase==L::Phase::Monitoring && o.diagnostics.dataValid);
    assert(o.diagnostics.stopErrorMm==1.0);
    current[0]=90.0;current[1]=90.0;
    o=L::Observe(p,in,current,1200,1250,a);
    assert(o.phase==L::Phase::TargetObserved);
    in.communicationHealthy=false;
    o=L::Observe(p,in,current,1200,1250,a);
    assert(o.phase==L::Phase::Fault);
    std::cout<<"lowering offline tests PASS\n";
}
