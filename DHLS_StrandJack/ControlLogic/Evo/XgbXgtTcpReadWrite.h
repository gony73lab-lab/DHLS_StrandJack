#pragma once
#include "XgbXgtTcpReadOnly.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Protocol-only XGT word-write packet codec. Never writes to a socket by itself.
// Writes are deliberately separated from the read-only diagnostic transport.
class XgtWordWriteCodec final {
public:
    static std::vector<std::uint8_t> BuildWriteRequest(
        std::uint16_t address, std::uint16_t value, std::uint16_t invoke) {
        if (address < 50 || address > 52)
            throw std::invalid_argument("D write outside D50..D52");
        std::vector<std::uint8_t> body;
        Put16(body, 0x0058); // XGT write request
        Put16(body, 0x0002); // WORD
        Put16(body, 0x0000); // reserved
        Put16(body, 0x0001); // one individual device
        const std::string device = "%DW" + std::to_string(address);
        Put16(body, static_cast<std::uint16_t>(device.size()));
        body.insert(body.end(), device.begin(), device.end());
        Put16(body, 2); // word data byte count
        Put16(body, value);
        std::vector<std::uint8_t> frame(20, 0);
        const char company[8] = {'L','S','I','S','-','X','G','T'};
        for (int i=0;i<8;++i) frame[i]=static_cast<std::uint8_t>(company[i]);
        frame[12]=0xA0;
        frame[13]=0x33;
        frame[14]=static_cast<std::uint8_t>(invoke);
        frame[15]=static_cast<std::uint8_t>(invoke>>8);
        frame[16]=static_cast<std::uint8_t>(body.size());
        frame[17]=static_cast<std::uint8_t>(body.size()>>8);
        std::uint8_t sum=0;
        for (int i=0;i<19;++i) sum=static_cast<std::uint8_t>(sum+frame[i]);
        frame[19]=sum;
        frame.insert(frame.end(),body.begin(),body.end());
        return frame;
    }

    static bool ParseWriteResponse(const std::vector<std::uint8_t>& body) {
        // 0x0059 write response, WORD type, reserved, error status, block count.
        return body.size()==10 && Get16(body,0)==0x0059 &&
               Get16(body,2)==0x0002 && Get16(body,4)==0 &&
               Get16(body,6)==0 && Get16(body,8)==1;
    }
private:
    static void Put16(std::vector<std::uint8_t>& v,std::uint16_t x) {
        v.push_back(static_cast<std::uint8_t>(x));
        v.push_back(static_cast<std::uint8_t>(x>>8));
    }
    static std::uint16_t Get16(const std::vector<std::uint8_t>& v,std::size_t i) {
        return static_cast<std::uint16_t>(v[i] | (static_cast<unsigned>(v[i+1])<<8));
    }
};

// Explicit authorization for every writable bit. No command has a default allow.
// This is a policy and offline command-preparation layer, not a safety interlock.
class DRegionWritePolicy final {
public:
    enum class Decision { Denied, ReadyForExternalTransport };
    struct PreparedWrite {
        Decision decision = Decision::Denied;
        std::uint16_t address = 0;
        std::uint16_t value = 0;
        std::string reason;
    };

    // A deliberate separate approval must be supplied by the host application.
    // Never automatically arm from PLC connection or successful read.
    void SetAuthorized(bool authorized) { authorized_ = authorized; }
    void AllowBit(std::uint16_t address, unsigned bit) {
        if (address < 51 || address > 52 || bit >= 16)
            throw std::invalid_argument("unsupported bit address");
        bitMask_[address-51] |= static_cast<std::uint16_t>(1u<<bit);
    }
    void AllowSpeedWord(bool allowed) { allowSpeed_ = allowed; }

    // currentWord must come from a fresh read of exactly the target D word.
    // RMW is NOT atomic with PLC execution or other writers.
    PreparedWrite PrepareBit(std::uint16_t address, unsigned bit,
                             bool set, std::uint16_t currentWord,
                             bool freshReadConfirmed) const {
        PreparedWrite out; out.address=address;
        if (!authorized_) { out.reason="host not authorized"; return out; }
        if (address < 51 || address > 52 || bit >= 16) {
            out.reason="not a mapped command bit"; return out;
        }
        if (!(bitMask_[address-51] & (1u<<bit))) {
            out.reason="bit not explicitly allowed"; return out;
        }
        if (!freshReadConfirmed) {
            out.reason="fresh target-word read required"; return out;
        }
        const auto mask=static_cast<std::uint16_t>(1u<<bit);
        out.value=set ? static_cast<std::uint16_t>(currentWord | mask)
                      : static_cast<std::uint16_t>(currentWord & ~mask);
        out.decision=Decision::ReadyForExternalTransport;
        return out;
    }
    PreparedWrite PrepareSpeed(std::uint16_t rawValue,
                               bool verifiedScalingAndLimits) const {
        PreparedWrite out; out.address=50;
        if (!authorized_ || !allowSpeed_ || !verifiedScalingAndLimits) {
            out.reason="speed write not approved or raw scale/range unverified";
            return out;
        }
        out.value=rawValue;
        out.decision=Decision::ReadyForExternalTransport;
        return out;
    }
private:
    bool authorized_=false;
    bool allowSpeed_=false;
    std::array<std::uint16_t,2> bitMask_{};
};


#include "DRegionDiagnostics.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <limits>
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

// XGT TCP driver; writes require an approved PreparedWrite. No automatic retry.
class XgbXgtTcpReadWrite final : public DRegionDiagnostics::IReadOnlyDWords {
public:
    explicit XgbXgtTcpReadWrite(std::string ip, std::uint16_t port=2004, int timeoutMs=1000)
        : ip_(std::move(ip)), port_(port), timeoutMs_(timeoutMs) {
        if(timeoutMs_<=0) throw std::invalid_argument("timeoutMs");
#ifdef _WIN32
        WSADATA w{}; if(WSAStartup(MAKEWORD(2,2),&w)!=0) throw std::runtime_error("WSAStartup");
#endif
    }
    ~XgbXgtTcpReadWrite() override { Disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    XgbXgtTcpReadWrite(const XgbXgtTcpReadWrite&)=delete;
    XgbXgtTcpReadWrite& operator=(const XgbXgtTcpReadWrite&)=delete;

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
        if(::connect(socket_,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0) { Disconnect(); return false; }
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
    // Sends exactly one approved write transaction; never retries automatically.
    // Successful protocol ACK is not evidence of hydraulic motion or safety.
    bool WritePrepared(const DRegionWritePolicy::PreparedWrite& prepared) {
        if (!Valid() || prepared.decision != DRegionWritePolicy::Decision::ReadyForExternalTransport ||
            prepared.address < 50 || prepared.address > 52) return false;
        std::vector<std::uint16_t> selection;
        if(!ReadWords(10,1,selection)||selection.size()!=1||(selection[0]&2)==0)return false;
        const auto req=XgtWordWriteCodec::BuildWriteRequest(prepared.address,prepared.value,++invokeId_);
        if (!SendAll(req.data(),req.size())) { Disconnect(); return false; }
        std::array<std::uint8_t,20> h{};
        if (!RecvAll(h.data(),h.size())) { Disconnect(); return false; }
        const auto len=Get16(h.data()+16);
        if (len != 10 || !CheckHeader(h,invokeId_)) { Disconnect(); return false; }
        std::vector<std::uint8_t> body(len);
        if (!RecvAll(body.data(),body.size()) || !XgtWordWriteCodec::ParseWriteResponse(body)) {
            Disconnect(); return false;
        }
        return true;
    }
    static std::vector<std::uint8_t> BuildReadRequest(const std::vector<std::uint16_t>& d,
                                                       std::uint16_t invoke) {
        if(d.empty() || d.size()>16) throw std::invalid_argument("XGT block count");
        std::vector<std::uint8_t> body;
        Put16(body,0x0054); Put16(body,0x0002); Put16(body,0); Put16(body,static_cast<std::uint16_t>(d.size()));
        for(auto a:d) {
            auto device=std::string("%DW")+std::to_string(a);
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
    bool SendAll(const std::uint8_t* data,std::size_t size) {
        while(size) {int n=::send(socket_,reinterpret_cast<const char*>(data),static_cast<int>(size),0);
            if(n<=0) return false;
            data+=n;
            size-=static_cast<std::size_t>(n);} return true;
    }
    bool RecvAll(std::uint8_t* data,std::size_t size) {
        while(size) {int n=::recv(socket_,reinterpret_cast<char*>(data),static_cast<int>(size),0);
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
