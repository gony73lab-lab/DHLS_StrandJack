#include "../XgtEmergencyTransport.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
static bool Transfer(SOCKET peer, unsigned char* data, int size, bool sending) {
    for(int done=0;done<size;) { int n=sending?send(peer,reinterpret_cast<char*>(data+done),size-done,0):recv(peer,reinterpret_cast<char*>(data+done),size-done,0); if(n<=0)return false;done+=n; } return true;
}
static unsigned Get(std::vector<unsigned char> const& v,int i) {return v[i]|(v[i+1]<<8);}
static void Put(std::vector<unsigned char>&v,unsigned n) {v.push_back(n&255);v.push_back(n>>8);}
static void Check(bool existingOn,bool badAck,bool badReadback) {
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); assert(listener!=INVALID_SOCKET);
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);assert(listen(listener,1)==0);
    int length=sizeof(a);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&a),&length)==0);
    std::atomic<int>writes{0};
    std::thread server([&] {
        SOCKET peer=accept(listener,nullptr,nullptr);assert(peer!=INVALID_SOCKET);
        DWORD timeout=2000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
        unsigned word=existingOn?0xA5A5:0xA5A4;
        for (;;) {
            std::vector<unsigned char> h(20);if(!Transfer(peer,h.data(),20,false))break;
            std::vector<unsigned char>b(Get(h,16));assert(Transfer(peer,b.data(),static_cast<int>(b.size()),false));
            assert(Get(b,2)==2 && Get(b,6)==1);
            const auto nameSize=Get(b,8);const std::string name(b.begin()+10,b.begin()+10+nameSize);assert(name=="%DW51"||name=="%DW10");
            std::vector<unsigned char> response;
            const bool writing=Get(b,0)==0x58;
            if(writing) {
                ++writes;assert(!existingOn);assert(Get(b,10+nameSize)==2);
                assert(Get(b,12+nameSize)==0xA5A5); // Every non-emergency bit preserved.
                word=badReadback?0xA5A4:0xA5A5;
                Put(response,0x59);Put(response,2);Put(response,0);Put(response,badAck?1:0);Put(response,1);
            } else {
                assert(Get(b,0)==0x54);Put(response,0x55);Put(response,2);Put(response,0);Put(response,0);Put(response,1);Put(response,2);Put(response,name=="%DW10"?2:word);
            }
            h[13]=0x11;h[16]=static_cast<unsigned char>(response.size());h[17]=0;h[19]=0;
            for(int i=0;i<19;++i)h[19]=static_cast<unsigned char>(h[19]+h[i]);
            assert(Transfer(peer,h.data(),20,true));assert(Transfer(peer,response.data(),static_cast<int>(response.size()),true));
            if(writing && badAck)break;
        }
        closesocket(peer);
    });
    { XgtEmergencyTransport transport("127.0.0.1",ntohs(a.sin_port),300);assert(transport.Connect());
      assert(transport.SetEmergencyOn()==(!badAck&&!badReadback));transport.Disconnect(); }
    server.join();closesocket(listener);assert(writes==(existingOn?0:1));
}

static void ControlCheck(bool badAck=false,bool badReadback=false) {
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);assert(listen(listener,1)==0);
    int length=sizeof(a);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&a),&length)==0);
    std::atomic<unsigned> word{0x8000},emergency{2},writes{0},speed{0},speedWrites{0};
    std::thread server([&]{
        SOCKET peer=accept(listener,nullptr,nullptr);assert(peer!=INVALID_SOCKET);
        for(;;){
            std::vector<unsigned char> h(20);if(!Transfer(peer,h.data(),20,false))break;
            std::vector<unsigned char>b(Get(h,16));assert(Transfer(peer,b.data(),static_cast<int>(b.size()),false));
            auto n=Get(b,8);std::string name(b.begin()+10,b.begin()+10+n);
            assert(name=="%DW10"||name=="%DW51"||name=="%DW50"||name=="%DW55"||name=="%DW56"||name=="%DW59");
            std::vector<unsigned char> response;
            if(Get(b,0)==0x58){
                auto desired=Get(b,12+n);
                if(name=="%DW50"){assert(desired<=60);++speedWrites;if(!badReadback)speed=desired;}
                else {assert(name=="%DW51");assert((desired&6)!=6 && (desired&~14u)==(word.load()&~14u));
                ++writes;if(!badReadback)word=desired;}
                Put(response,0x59);Put(response,2);Put(response,0);Put(response,badAck?1:0);Put(response,1);
            }else{
                Put(response,0x55);Put(response,2);Put(response,0);Put(response,0);Put(response,1);Put(response,2);
                Put(response,name=="%DW10"?emergency.load():name=="%DW50"?speed.load():name=="%DW51"?word.load():0);
            }
            h[13]=0x11;h[16]=static_cast<unsigned char>(response.size());h[17]=h[19]=0;
            for(int i=0;i<19;++i)h[19]=static_cast<unsigned char>(h[19]+h[i]);
            if(!Transfer(peer,h.data(),20,true)||!Transfer(peer,response.data(),static_cast<int>(response.size()),true))break;
        }closesocket(peer);
    });
    {XgtEmergencyTransport t("127.0.0.1",ntohs(a.sin_port),300);assert(t.Connect());
     assert(!t.SetSpeed(61));
     if(!badAck&&!badReadback){assert(t.SetSpeed(37));assert(speed==37&&speedWrites==1);}
     assert(!t.SetControlBits(0x8000,0x8000));assert(!t.SetControlBits(6,6));
     assert(t.SetControlBits(6,2)==(!badAck&&!badReadback));
     if(!badAck&&!badReadback){
        assert(word==0x8002);assert(t.SetControlBits(6,4));assert(word==0x8004);
        assert(t.SetControlBits(8,8));assert(word==0x800C);
        assert(t.SetControlBits(8,0));assert(word==0x8004);
        emergency=3;assert(!t.SetControlBits(6,2));assert(writes==4);
        word=0x800F;assert(t.SetControlBits(14,0));assert(word==0x8001&&writes==5);
     }else assert(writes==1); // no automatic retry
     t.Disconnect();}
    server.join();closesocket(listener);
}

int main() {WSADATA w{};assert(WSAStartup(MAKEWORD(2,2),&w)==0);
 ControlCheck();ControlCheck(true,false);ControlCheck(false,true);
 Check(false,false,false);Check(true,false,false);Check(false,true,false);Check(false,false,true);
 WSACleanup();std::cout<<"PASS: loopback D51.0 ON only, other bits preserved, ACK/readback validation, idempotency, no automatic retry\n";
}
