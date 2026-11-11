#pragma once
#include "Evo/DRegionDiagnostics.h"
#include "Evo/XgbXgtTcpReadWrite.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <limits>
#include <functional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// XGB FEnet XGT dedicated TCP server (default 2004), individual WORD reads only.
// Narrow D050/D051 write APIs; every transmitted write requires a fresh D010.1 ON read.
class XgtEmergencyTransport final : public DRegionDiagnostics::IReadOnlyDWords {
public:
    explicit XgtEmergencyTransport(std::string ip, std::uint16_t port=2004, int timeoutMs=1000)
        : ip_(std::move(ip)), port_(port), timeoutMs_(timeoutMs) {
        if(timeoutMs_<=0) throw std::invalid_argument("timeoutMs");
#ifdef _WIN32
        WSADATA w{}; if(WSAStartup(MAKEWORD(2,2),&w)!=0) throw std::runtime_error("WSAStartup");
#endif
    }
    ~XgtEmergencyTransport() override { Disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    XgtEmergencyTransport(const XgtEmergencyTransport&)=delete;
    XgtEmergencyTransport& operator=(const XgtEmergencyTransport&)=delete;

    bool Connect() {
        Disconnect();
        socket_=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(!Valid()) return false;
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port_);
        if(inet_pton(AF_INET,ip_.c_str(),&a.sin_addr)!=1) { Disconnect(); return false; }
#ifdef _WIN32
        DWORD t=static_cast<DWORD>(timeoutMs_);
        setsockopt(socket_,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&t),sizeof(t));
        setsockopt(socket_,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&t),sizeof(t));
#else
        timeval t{timeoutMs_/1000,(timeoutMs_%1000)*1000};
        setsockopt(socket_,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));
        setsockopt(socket_,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
#endif
#ifdef _WIN32
        u_long nonblocking=1;
        if(ioctlsocket(socket_,FIONBIO,&nonblocking)!=0) { Disconnect(); return false; }
        deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
        if(::connect(socket_,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0) {
            const auto error=WSAGetLastError();
            if((error!=WSAEWOULDBLOCK && error!=WSAEINPROGRESS) || !WaitSocket(true)) { Disconnect(); return false; }
            int socketError=0, size=sizeof(socketError);
            if(getsockopt(socket_,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&socketError),&size)!=0 || socketError) { Disconnect(); return false; }
        }
        nonblocking=0;
        if(ioctlsocket(socket_,FIONBIO,&nonblocking)!=0) { Disconnect(); return false; }
#else
        if(::connect(socket_,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0) { Disconnect(); return false; }
#endif
        return true;
    }
    void Disconnect() {
        if(Valid()) {
#ifdef _WIN32
            closesocket(socket_);
#else
            ::close(socket_);
#endif
            socket_=InvalidSocket();
        }
    }
    bool ReadWords(std::uint16_t firstD,std::size_t count,std::vector<std::uint16_t>& out) override {
        out.clear();
        if(!Valid() || count==0 || count>256 || static_cast<unsigned>(firstD)+count>65536u) return false;
        std::vector<std::uint16_t> temp; temp.reserve(count);
        // Individual WORD read; bounded chunks of <=16 addresses, XGT protocol limit.
        for(std::size_t base=0;base<count;base+=16) {
            std::size_t n=std::min<std::size_t>(16,count-base);
            std::vector<std::uint16_t> addresses;
            for(std::size_t i=0;i<n;++i) addresses.push_back(static_cast<std::uint16_t>(firstD+base+i));
            deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
            auto req=BuildReadRequest(addresses,++invokeId_);
            if(!SendAll(req.data(),req.size())) { Disconnect(); return false; }
            std::array<std::uint8_t,20> h{};
            if(!RecvAll(h.data(),h.size())) { Disconnect(); return false; }
            auto len=Get16(h.data()+16);
            if(len<10 || len>512 || !CheckHeader(h,invokeId_)) { Disconnect(); return false; }
            std::vector<std::uint8_t> body(len);
            if(!RecvAll(body.data(),body.size()) || !ParseReadResponse(body,n,temp)) { Disconnect(); return false; }
        }
        out=std::move(temp); return true;
    }
    bool ReadDeviceWords(char area,std::vector<std::uint16_t> const& addresses,std::vector<std::uint16_t>& out) {
        out.clear();
        if(!Valid() || (area!='P' && area!='M') || addresses.empty() || addresses.size()>16) return false;
        const auto count=addresses.size();
        std::vector<std::uint16_t> temp; temp.reserve(count);
        // Individual WORD read; bounded chunks of <=16 addresses, XGT protocol limit.
        for(std::size_t base=0;base<count;base+=16) {
            std::size_t n=std::min<std::size_t>(16,count-base);

            deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
            auto req=BuildReadRequest(addresses,++invokeId_,area);
            if(!SendAll(req.data(),req.size())) { Disconnect(); return false; }
            std::array<std::uint8_t,20> h{};
            if(!RecvAll(h.data(),h.size())) { Disconnect(); return false; }
            auto len=Get16(h.data()+16);
            if(len<10 || len>512 || !CheckHeader(h,invokeId_)) { Disconnect(); return false; }
            std::vector<std::uint8_t> body(len);
            if(!RecvAll(body.data(),body.size()) || !ParseReadResponse(body,n,temp)) { Disconnect(); return false; }
        }
        out=std::move(temp); return true;
    }
    // Single worker only. Narrow D51 command mask; other D words remain read-only here.
    bool FaultsClear() {
        std::vector<std::uint16_t> value;
        for(std::uint16_t word:{std::uint16_t(55),std::uint16_t(56),std::uint16_t(59)})
            if(!ReadWords(word,1,value)||value.size()!=1||value[0]!=0)return false;
        return true;
    }
    bool PcWriteEnabled(){
        std::vector<std::uint16_t> state;
        return ReadWords(10,1,state)&&state.size()==1&&(state[0]&2)!=0;
    }
    bool SetSpeed(std::uint16_t speed,std::function<bool()> const& enabled={}) {
        if(enabled&&!enabled())return false;
        if(speed>60||!FaultsClear())return false; // UI Hz value is written directly, with no invented scaling.
        std::vector<std::uint16_t> state,command;
        if(!ReadWords(10,1,state)||state.size()!=1||(state[0]&1)||
           !ReadWords(51,1,command)||command.size()!=1||(command[0]&1))return false;
        if(!PcWriteEnabled())return false; // Includes emergency ON and output OFF: no write exception.
        deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
        if(enabled&&!enabled())return false;
        auto packet=XgtWordWriteCodec::BuildWriteRequest(50,speed,++invokeId_);
        if(!SendAll(packet.data(),packet.size())){Disconnect();return false;}
        std::array<std::uint8_t,20> h{};
        if(!RecvAll(h.data(),h.size())||!CheckHeader(h,invokeId_)||Get16(h.data()+16)!=10){Disconnect();return false;}
        std::vector<std::uint8_t> body(10);
        if(!RecvAll(body.data(),body.size())||!XgtWordWriteCodec::ParseWriteResponse(body)){Disconnect();return false;}
        std::vector<std::uint16_t> verify;
        return ReadWords(50,1,verify)&&verify.size()==1&&verify[0]==speed;
    }
    bool SetControlBits(std::uint16_t mask, std::uint16_t value) {
        if (!mask || (mask & ~std::uint16_t(0x000E)) || (value & ~mask)) return false;
        std::vector<std::uint16_t> state, current;
        if(value!=0&&(!FaultsClear()||!ReadWords(10,1,state)||state.size()!=1||(state[0]&1)))return false;
        if(!ReadWords(51,1,current)||current.size()!=1||(value!=0&&(current[0]&1)))return false;
        const auto desired=static_cast<std::uint16_t>((current[0]&~mask)|value);
        if ((desired&6)==6) return false;
        if(value==0&&(current[0]&mask)==0)return true;
        // Fresh WORD RMW preserves unrelated snapshot bits; not atomic against PLC writes (including F).
        if(!PcWriteEnabled())return false; // Includes emergency ON and output OFF: no write exception.
        deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
        auto request=XgtWordWriteCodec::BuildWriteRequest(51,desired,++invokeId_);
        if (!SendAll(request.data(),request.size())) { Disconnect(); return false; }
        std::array<std::uint8_t,20> header{};
        if (!RecvAll(header.data(),header.size()) || !CheckHeader(header,invokeId_) || Get16(header.data()+16)!=10)
        { Disconnect(); return false; }
        std::vector<std::uint8_t> body(10);
        if (!RecvAll(body.data(),body.size()) || !XgtWordWriteCodec::ParseWriteResponse(body))
        { Disconnect(); return false; }
        std::vector<std::uint16_t> verify;
        return ReadWords(51,1,verify) && verify.size()==1 && (verify[0]&mask)==value;
    }
    bool SetPortMask(std::uint16_t mask,std::function<bool()> const& held={}){
        if(mask&0xFF00)return false;
        if(mask&&((held&&!held())||!FaultsClear()))return false;
        std::vector<std::uint16_t> current,state,command;
        if(mask&&(!ReadWords(10,1,state)||state.size()!=1||(state[0]&1)||!ReadWords(51,1,command)||command.size()!=1||(command[0]&1)||((command[0]&6)!=2&&(command[0]&6)!=4)))return false;
        if(!ReadWords(52,1,current)||current.size()!=1)return false;
        if(mask&&held&&!held())return false;
        if((current[0]&255)==mask)return true;
        if(!PcWriteEnabled())return false;
        if(mask&&held&&!held())return false; // Never enable after a queued pointer release.
        const auto value=static_cast<std::uint16_t>((current[0]&0xFF00)|mask);
        deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
        auto packet=XgtWordWriteCodec::BuildWriteRequest(52,value,++invokeId_);
        if(!SendAll(packet.data(),packet.size())){Disconnect();return false;}
        std::array<std::uint8_t,20> h{};
        if(!RecvAll(h.data(),h.size())||!CheckHeader(h,invokeId_)||Get16(h.data()+16)!=10){Disconnect();return false;}
        std::vector<std::uint8_t> body(10);
        if(!RecvAll(body.data(),body.size())||!XgtWordWriteCodec::ParseWriteResponse(body)){Disconnect();return false;}
        std::vector<std::uint16_t> verify;return ReadWords(52,1,verify)&&verify.size()==1&&(verify[0]&255)==mask;
    }
    bool SetEmergencyOn() {
        std::vector<std::uint16_t> current;
        if (!ReadWords(51, 1, current) || current.size()!=1) return false;
        if (current[0] & 1) return true;
        // This is deliberately a fresh-read RMW; PLC/other-writer races remain possible.
        // Every other command stays denied by policy.
        DRegionWritePolicy policy;
        policy.SetAuthorized(true); policy.AllowBit(51,0);
        auto prepared=policy.PrepareBit(51,0,true,current[0],true);
        if (prepared.decision!=DRegionWritePolicy::Decision::ReadyForExternalTransport) return false;
        if(!PcWriteEnabled())return false; // Includes emergency ON and output OFF: no write exception.
        deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs_);
        auto request=XgtWordWriteCodec::BuildWriteRequest(51,prepared.value,++invokeId_);
        if (!SendAll(request.data(),request.size())) { Disconnect(); return false; }
        std::array<std::uint8_t,20> header{};
        if (!RecvAll(header.data(),header.size()) || !CheckHeader(header,invokeId_) || Get16(header.data()+16)!=10)
        { Disconnect(); return false; }
        std::vector<std::uint8_t> body(10);
        if (!RecvAll(body.data(),body.size()) || !XgtWordWriteCodec::ParseWriteResponse(body))
        { Disconnect(); return false; }
        std::vector<std::uint16_t> verify;
        return ReadWords(51,1,verify) && verify.size()==1 && (verify[0]&1)!=0;
    }
    static std::vector<std::uint8_t> BuildReadRequest(const std::vector<std::uint16_t>& d,
                                                       std::uint16_t invoke,char area='D') {
        if((area!='D' && area!='P' && area!='M') || d.empty() || d.size()>16) throw std::invalid_argument("XGT block count");
        std::vector<std::uint8_t> body;
        Put16(body,0x0054); Put16(body,0x0002); Put16(body,0); Put16(body,static_cast<std::uint16_t>(d.size()));
        for(auto a:d) {
            auto device=std::string("%")+area+"W"+std::to_string(a);
            Put16(body,static_cast<std::uint16_t>(device.size()));
            body.insert(body.end(),device.begin(),device.end());
        }
        std::vector<std::uint8_t> frame(20,0);
        const char company[8]={'L','S','I','S','-','X','G','T'};
        std::memcpy(frame.data(),company,8);
        frame[12]=0xA0; // XGB FEnet manual example CPU information; confirm device compatibility
        frame[13]=0x33;
        frame[14]=static_cast<std::uint8_t>(invoke); frame[15]=static_cast<std::uint8_t>(invoke>>8);
        frame[16]=static_cast<std::uint8_t>(body.size()); frame[17]=static_cast<std::uint8_t>(body.size()>>8);
        std::uint8_t checksum=0;
        for(int i=0;i<19;++i) checksum=static_cast<std::uint8_t>(checksum+frame[i]);
        frame[19]=checksum;
        frame.insert(frame.end(),body.begin(),body.end()); return frame;
    }
    static bool ParseReadResponse(const std::vector<std::uint8_t>& b,std::size_t n,
                                  std::vector<std::uint16_t>& append) {
        if(b.size()<10 || Get16(b.data())!=0x0055 || Get16(b.data()+2)!=0x0002 ||
           Get16(b.data()+6)!=0 || Get16(b.data()+8)!=n) return false;
        std::size_t pos=10;
        std::vector<std::uint16_t> temp;
        for(std::size_t i=0;i<n;++i) {
            if(pos+4>b.size() || Get16(b.data()+pos)!=2) return false;
            temp.push_back(Get16(b.data()+pos+2)); pos+=4;
        }
        if(pos!=b.size()) return false;
        append.insert(append.end(),temp.begin(),temp.end()); return true;
    }
private:
#ifdef _WIN32
    using Socket=SOCKET;
    static Socket InvalidSocket(){return INVALID_SOCKET;}
#else
    using Socket=int;
    static Socket InvalidSocket(){return -1;}
#endif
    bool Valid() const {return socket_!=InvalidSocket();}
    static std::uint16_t Get16(const std::uint8_t* p) {return static_cast<std::uint16_t>(p[0]|(p[1]<<8));}
    static void Put16(std::vector<std::uint8_t>& v,std::uint16_t x) {v.push_back(static_cast<std::uint8_t>(x));v.push_back(static_cast<std::uint8_t>(x>>8));}
    static bool CheckHeader(const std::array<std::uint8_t,20>& h,std::uint16_t invoke) {
        const char id[8]={'L','S','I','S','-','X','G','T'};
        if(std::memcmp(h.data(),id,8)!=0 || h[13]!=0x11 || Get16(h.data()+14)!=invoke) return false;
        // BCC = sum of header bytes 0..18 (including zeros), stored at 19.
        std::uint8_t checksum=0;
        for(int i=0;i<19;++i) checksum=static_cast<std::uint8_t>(checksum+h[i]);
        return checksum==h[19];
    }
    bool WaitSocket(bool writing) {
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline_-std::chrono::steady_clock::now()).count();
        if(remaining<=0) return false;
        fd_set descriptors; FD_ZERO(&descriptors); FD_SET(socket_,&descriptors);
        timeval timeout{static_cast<long>(remaining/1000000),static_cast<long>(remaining%1000000)};
#ifdef _WIN32
        return ::select(0,writing ? nullptr : &descriptors,writing ? &descriptors : nullptr,nullptr,&timeout)>0;
#else
        return ::select(socket_+1,writing ? nullptr : &descriptors,writing ? &descriptors : nullptr,nullptr,&timeout)>0;
#endif
    }
    std::chrono::steady_clock::time_point deadline_{};
    bool SendAll(const std::uint8_t* data,std::size_t size) {
        while(size) {if(!WaitSocket(true)) return false; int n=::send(socket_,reinterpret_cast<const char*>(data),static_cast<int>(size),0);
            if(n<=0) return false;
            data+=n;
            size-=static_cast<std::size_t>(n);} return true;
    }
    bool RecvAll(std::uint8_t* data,std::size_t size) {
        while(size) {if(!WaitSocket(false)) return false; int n=::recv(socket_,reinterpret_cast<char*>(data),static_cast<int>(size),0);
            if(n<=0) return false;
            data+=n;
            size-=static_cast<std::size_t>(n);} return true;
    }
    std::string ip_;
    std::uint16_t port_;
    int timeoutMs_;
    std::uint16_t invokeId_=0;
    Socket socket_=InvalidSocket();
};
