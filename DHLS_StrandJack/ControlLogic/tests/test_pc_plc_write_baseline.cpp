// Read-only preparation for a separately authorized, isolated PLC write test.
#include "../Evo/XgbXgtTcpReadOnly.h"
#include "../Evo/XgbXgtTcpReadWrite.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <cassert>
int main(int argc,char**argv){
 if(argc!=3||std::string(argv[1])!="192.168.10.124")return 2;
 // Validate command packet addresses/masks without transmitting any write.
 for(unsigned bit:{0u,1u,2u,3u,15u}){DRegionWritePolicy p;p.SetAuthorized(true);p.AllowBit(51,bit);auto on=p.PrepareBit(51,bit,true,0,true);assert(on.value==(1u<<bit));auto frame=XgtWordWriteCodec::BuildWriteRequest(51,on.value,1);assert(frame[20]==0x58);}
 for(unsigned bit=0;bit<8;++bit){DRegionWritePolicy p;p.SetAuthorized(true);p.AllowBit(52,bit);assert(p.PrepareBit(52,bit,true,0,true).value==(1u<<bit));}
 XgbXgtTcpReadOnly plc(argv[1],2004,1000);if(!plc.Connect())return 3;std::ofstream csv(argv[2]);csv<<"sample,address,word,hex\n";
 for(int i=0;i<3;++i){for(unsigned a:{10u,50u,51u,52u,55u,56u,59u}){std::vector<std::uint16_t> v;if(!plc.ReadWords(static_cast<std::uint16_t>(a),1,v))return 4;std::cout<<"sample="<<i<<" D"<<a<<"="<<v[0]<<" (0x"<<std::hex<<std::setw(4)<<std::setfill('0')<<v[0]<<std::dec<<")\n";csv<<i<<','<<a<<','<<v[0]<<",0x"<<std::hex<<v[0]<<std::dec<<'\n';}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 std::cout<<"PASS: baseline read; command codec checks; hardware writes=0\n";
}
