#include "../XgtEmergencyTransport.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
static bool Transfer(SOCKET p,unsigned char* d,int n,bool sendData){for(int i=0;i<n;){int k=sendData?send(p,reinterpret_cast<char*>(d+i),n-i,0):recv(p,reinterpret_cast<char*>(d+i),n-i,0);if(k<=0)return false;i+=k;}return true;}
static unsigned U(std::vector<unsigned char>const&v,int i){return v[i]|(v[i+1]<<8);}
static void Put(std::vector<unsigned char>&v,unsigned n){v.push_back(n&255);v.push_back(n>>8);}
// mode 0: OFF, 1: ON, 2: D010 read failure, 3: drops OFF between precheck and transmission.
static void Check(bool generic,int mode){
 SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
 sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);assert(listen(listener,1)==0);int size=sizeof(a);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&a),&size)==0);
 std::atomic<unsigned>writes{0},reads{0};unsigned memory[64]{};memory[51]=0x8002;memory[52]=0xA505;
 std::thread server([&]{SOCKET peer=accept(listener,nullptr,nullptr);assert(peer!=INVALID_SOCKET);
  for(;;){std::vector<unsigned char> h(20);if(!Transfer(peer,h.data(),20,false))break;std::vector<unsigned char>b(U(h,16));assert(Transfer(peer,b.data(),static_cast<int>(b.size()),false));assert(U(b,6)==1);auto n=U(b,8);std::string name(b.begin()+10,b.begin()+10+n);unsigned address=std::stoul(name.substr(3));assert(address<64);bool write=U(b,0)==0x58;std::vector<unsigned char> reply;
   if(write){assert(mode==1);assert(address>=50&&address<=52);++writes;memory[address]=U(b,12+n);Put(reply,0x59);Put(reply,2);Put(reply,0);Put(reply,0);Put(reply,1);}
   else{assert(U(b,0)==0x54);bool bad=address==10&&mode==2;Put(reply,0x55);Put(reply,2);Put(reply,0);Put(reply,bad?1:0);Put(reply,1);
    if(!bad){unsigned value=memory[address];if(address==10){unsigned seen=++reads;value=mode==1||(mode==3&&seen==1)?2:0;}Put(reply,2);Put(reply,value);}}
   h[13]=0x11;h[16]=static_cast<unsigned char>(reply.size());h[17]=h[19]=0;for(int i=0;i<19;++i)h[19]=static_cast<unsigned char>(h[19]+h[i]);if(!Transfer(peer,h.data(),20,true)||!Transfer(peer,reply.data(),static_cast<int>(reply.size()),true))break;
  }closesocket(peer);
 });
 if(generic){XgbXgtTcpReadWrite io("127.0.0.1",ntohs(a.sin_port),300);assert(io.Connect());
  for(unsigned address=50;address<=52;++address){DRegionWritePolicy::PreparedWrite p{DRegionWritePolicy::Decision::ReadyForExternalTransport,static_cast<std::uint16_t>(address),7,"test only"};assert(io.WritePrepared(p)==(mode==1));if(mode==2)break;}
  if(mode!=2){std::vector<std::uint16_t> data;assert(io.ReadWords(51,1,data));}io.Disconnect();
 }else{XgtEmergencyTransport io("127.0.0.1",ntohs(a.sin_port),300);assert(io.Connect());assert(io.SetSpeed(25)==(mode==1));
  if(mode!=2&&mode!=3){assert(io.SetControlBits(6,2)==(mode==1));assert(io.SetPortMask(3,[]{return true;})==(mode==1));assert(io.SetPortMask(0)==(mode==1));
   if(mode==1){int checks=0;assert(!io.SetPortMask(3,[&]{return ++checks<2;}));}
   assert(io.SetControlBits(14,0)==(mode==1));assert(io.SetEmergencyOn()==(mode==1));std::vector<std::uint16_t> data;assert(io.ReadWords(51,1,data));}
  io.Disconnect();
 }
 server.join();closesocket(listener);assert(writes==(mode==1?(generic?3u:6u):0u));
}
int main(){WSADATA w{};assert(WSAStartup(MAKEWORD(2,2),&w)==0);for(int m:{0,1,2}){Check(false,m);Check(true,m);}Check(false,3);WSACleanup();std::cout<<"PASS: D010.1 OFF/unknown blocks D050-D052, emergency and OFF writes; ON allows writes; fresh-read transition denies; read connection retained\n";}
