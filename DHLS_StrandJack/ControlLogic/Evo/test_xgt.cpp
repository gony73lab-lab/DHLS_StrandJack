#include "XgbXgtTcpReadOnly.h"
#include <cassert>
#include <iostream>
int main(){
 auto req=XgbXgtTcpReadOnly::BuildReadRequest({10,11},0x1234);
 assert(req.size()==20+8+2*(2+5));
 assert(req[20]==0x54 && req[21]==0 && req[22]==2);
 assert(req[14]==0x34 && req[15]==0x12);
 std::vector<std::uint8_t> reply={0x55,0,2,0,0,0,0,0,2,0,2,0,0x34,0x12,2,0,0x78,0x56};
 std::vector<std::uint16_t> result;
 assert(XgbXgtTcpReadOnly::ParseReadResponse(reply,2,result));
 assert(result.size()==2 && result[0]==0x1234 && result[1]==0x5678);
 reply[6]=1; assert(!XgbXgtTcpReadOnly::ParseReadResponse(reply,2,result));
 std::cout<<"XGT frame unit tests PASS\n";
}
