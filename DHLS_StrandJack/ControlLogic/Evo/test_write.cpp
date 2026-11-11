#include "XgbXgtTcpReadWrite.h"
#include <cassert>
#include <iostream>
int main() {
    using Policy=DRegionWritePolicy;
    Policy p;
    assert(p.PrepareBit(51,1,true,0,false).decision==Policy::Decision::Denied);
    p.SetAuthorized(true);
    assert(p.PrepareBit(51,1,true,0,true).decision==Policy::Decision::Denied);
    p.AllowBit(51,1);
    assert(p.PrepareBit(51,1,true,0,false).decision==Policy::Decision::Denied);
    auto w=p.PrepareBit(51,1,true,0x8000,true);
    assert(w.decision==Policy::Decision::ReadyForExternalTransport && w.value==0x8002);
    assert(p.PrepareBit(51,1,false,0x8002,true).value==0x8000);
    assert(p.PrepareBit(52,0,true,0,true).decision==Policy::Decision::Denied);
    assert(p.PrepareSpeed(100,true).decision==Policy::Decision::Denied);
    p.AllowSpeedWord(true);
    assert(p.PrepareSpeed(100,false).decision==Policy::Decision::Denied);
    assert(p.PrepareSpeed(100,true).decision==Policy::Decision::ReadyForExternalTransport);
    auto f=XgtWordWriteCodec::BuildWriteRequest(51,0x8002,0x1234);
    assert(f[20]==0x58 && f[21]==0 && f[22]==2 && f[23]==0);
    assert(f[14]==0x34 && f[15]==0x12);
    assert(f[f.size()-2]==0x02 && f[f.size()-1]==0x80);
    assert(XgtWordWriteCodec::ParseWriteResponse({0x59,0,2,0,0,0,0,0,1,0}));
    assert(!XgtWordWriteCodec::ParseWriteResponse({0x59,0,2,0,0,0,1,0,1,0}));
    bool threw=false;
    try { XgtWordWriteCodec::BuildWriteRequest(59,0,1); }
    catch(const std::invalid_argument&) { threw=true; }
    assert(threw);
    std::cout<<"Write codec/policy offline tests PASS\n";
}
