// Loopback-only validation of D50, defined D51 bits and D52.0..7; no PLC connection.
#include "../Evo/XgbXgtTcpReadWrite.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
#include <array>
static bool Transfer(SOCKET peer,unsigned char* p,int n,bool sendIt){for(int done=0;done<n;){int count=sendIt?send(peer,reinterpret_cast<char*>(p+done),n-done,0):recv(peer,reinterpret_cast<char*>(p+done),n-done,0);if(count<=0)return false;done+=count;}return true;}
static unsigned Get(std::vector<unsigned char>const&v,int i){return v[i]|(v[i+1]<<8);}static void Put(std::vector<unsigned char>&v,unsigned n){v.push_back(n&255);v.push_back(n>>8);}
int main(){WSADATA w{};assert(WSAStartup(MAKEWORD(2,2),&w)==0);SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);assert(listen(listener,1)==0);int length=sizeof(a);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&a),&length)==0);
 std::array<unsigned,3> memory{{0,0x8000,0}};std::atomic<unsigned>writes{0};
 std::thread server([&]{SOCKET peer=accept(listener,nullptr,nullptr);assert(peer!=INVALID_SOCKET);DWORD timeout=3000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
  for(;;){std::vector<unsigned char>h(20);if(!Transfer(peer,h.data(),20,false))break;std::vector<unsigned char>b(Get(h,16));assert(Transfer(peer,b.data(),static_cast<int>(b.size()),false));assert(Get(b,2)==2&&Get(b,6)==1);unsigned size=Get(b,8);std::string name(b.begin()+10,b.begin()+10+size);assert(name=="%DW50"||name=="%DW51"||name=="%DW52");int index=std::stoi(name.substr(3))-50;std::vector<unsigned char>response;
   if(Get(b,0)==0x58){assert(Get(b,10+size)==2);auto value=Get(b,12+size);if(index==0)assert(value<=1);if(index==1){assert((value&0x7ff0)==0);assert((value&6)!=6);}if(index==2)assert(value==0||(value<=128&&(value&(value-1))==0));memory[index]=value;++writes;Put(response,0x59);Put(response,2);Put(response,0);Put(response,0);Put(response,1);}
   else{assert(Get(b,0)==0x54);Put(response,0x55);Put(response,2);Put(response,0);Put(response,0);Put(response,1);Put(response,2);Put(response,memory[index]);}
   h[13]=0x11;h[16]=static_cast<unsigned char>(response.size());h[17]=0;h[19]=0;for(int i=0;i<19;++i)h[19]=static_cast<unsigned char>(h[19]+h[i]);assert(Transfer(peer,h.data(),20,true));assert(Transfer(peer,response.data(),static_cast<int>(response.size()),true));
  }closesocket(peer);
 });
 {XgbXgtTcpReadWrite io("127.0.0.1",ntohs(a.sin_port),1000);assert(io.Connect());DRegionWritePolicy policy;policy.SetAuthorized(true);policy.AllowSpeedWord(true);for(unsigned bit:{0u,1u,2u,3u,15u})policy.AllowBit(51,bit);for(unsigned bit=0;bit<8;++bit)policy.AllowBit(52,bit);
  auto read=[&](unsigned address){std::vector<std::uint16_t>v;assert(io.ReadWords(static_cast<std::uint16_t>(address),1,v));return v[0];};
  auto send=[&](DRegionWritePolicy::PreparedWrite const& prepared){assert(io.WritePrepared(prepared));assert(read(prepared.address)==prepared.value);};
  send(policy.PrepareSpeed(1,true));send(policy.PrepareSpeed(0,true));
  for(unsigned bit:{1u,2u,3u,0u}){auto before=read(51);send(policy.PrepareBit(51,bit,true,before,true));assert((read(51)&~(1u<<bit))==(before&~(1u<<bit)));send(policy.PrepareBit(51,bit,false,read(51),true));assert(read(51)==before);}
  for(unsigned bit=0;bit<8;++bit){auto before=read(52);send(policy.PrepareBit(52,bit,true,before,true));send(policy.PrepareBit(52,bit,false,read(52),true));assert(read(52)==before);}
  send(policy.PrepareBit(51,15,false,read(51),true));send(policy.PrepareBit(51,15,true,read(51),true));
  assert(read(50)==0&&read(51)==0x8000&&read(52)==0);assert(policy.PrepareBit(51,4,true,0,true).decision==DRegionWritePolicy::Decision::Denied);assert(policy.PrepareBit(52,8,true,0,true).decision==DRegionWritePolicy::Decision::Denied);io.Disconnect();
 }
 server.join();closesocket(listener);WSACleanup();assert(writes==28);std::cout<<"PASS: 28 loopback writes with ACK/readback; D50 min test, defined D51 bits, D52.0..7, F preservation, original states restored; undefined bits denied; no physical PLC writes.\n";
}
