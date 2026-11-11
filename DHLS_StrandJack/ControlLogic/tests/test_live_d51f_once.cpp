// Manual hardware test only. Not part of app or automatic/offline tests.
// Explicit flag allows exactly one D51.F ON write, only from a zero D51 baseline.
// Never sends OFF, retries a write, or submits an equipment/motion command.
#include "../Evo/XgbXgtTcpReadWrite.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
int main(int argc,char** argv){
 if(argc!=4){std::cerr<<"Usage: IP --read-baseline|--set-d51f-once CSV\n";return 2;}
 const std::string ip=argv[1],mode=argv[2];if(ip!="192.168.10.124"||(mode!="--read-baseline"&&mode!="--set-d51f-once"))return 2;
 using Clock=std::chrono::steady_clock;auto start=Clock::now();auto ms=[&]{return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
 std::ofstream csv(argv[3]);csv<<"phase,elapsed_ms,address,word,bit_F\n";if(!csv)return 3;
 XgbXgtTcpReadWrite transport(ip,2004,1000);if(!transport.Connect()){std::cout<<"CONNECT_FAILED\n";return 4;}
 std::vector<std::uint16_t> state,word;if(!transport.ReadWords(10,1,state)){std::cout<<"D10_READ_FAILED\n";return 5;}
 std::cout<<"D10="<<state[0]<<'\n';csv<<"baseline,"<<ms()<<",10,"<<state[0]<<",\n";
 for(int i=0;i<3;++i){if(!transport.ReadWords(51,1,word))return 5;std::cout<<"BASELINE_D51="<<word[0]<<'\n';csv<<"baseline,"<<ms()<<",51,"<<word[0]<<','<<((word[0]>>15)&1)<<'\n';csv.flush();
  if(mode=="--set-d51f-once"&&word[0]!=0){std::cout<<"ABORT_D51_NOT_ZERO: no write sent\n";return 6;}
  std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 if(mode=="--read-baseline"){std::cout<<"READ_ONLY_BASELINE_DONE writes=0\n";return 0;}
 if(state[0]&1){std::cout<<"ABORT_EMERGENCY_INPUT: no write sent\n";return 6;}
 // Final fresh read immediately before preparing the only authorized bit.
 if(!transport.ReadWords(51,1,word)||word[0]!=0){std::cout<<"ABORT_FINAL_BASELINE_CHANGED: no write sent\n";return 6;}
 DRegionWritePolicy policy;policy.SetAuthorized(true);policy.AllowBit(51,15);auto prepared=policy.PrepareBit(51,15,true,word[0],true);
 if(prepared.decision!=DRegionWritePolicy::Decision::ReadyForExternalTransport||prepared.address!=51||prepared.value!=0x8000)return 7;
 const auto writeStart=Clock::now();const bool ack=transport.WritePrepared(prepared);const auto ackTime=Clock::now();
 csv<<"write_ack,"<<ms()<<",51,"<<prepared.value<<",1\n";csv.flush();std::cout<<"WRITE_ATTEMPTS=1 value=32768 (0x8000) ACK="<<ack<<" ack_ms="<<std::chrono::duration<double,std::milli>(ackTime-writeStart).count()<<'\n';
 if(!ack){std::cout<<"WRITE_OUTCOME_UNKNOWN: no retry, no OFF sent\n";return 8;}
 bool sawOn=false,cleared=false,otherBitsChanged=false;double firstOffMs=-1;unsigned samples=0,last=0;
 for(int i=0;i<100;++i){if(!transport.ReadWords(51,1,word)){std::cout<<"POST_READ_FAILED: no further writes\n";return 9;}
  const double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-writeStart).count();last=word[0];bool on=(word[0]&0x8000)!=0;sawOn|=on;otherBitsChanged|=(word[0]&0x7FFF)!=0;
  if(!on&&!cleared){cleared=true;firstOffMs=elapsed;}csv<<"observe,"<<elapsed<<",51,"<<word[0]<<','<<on<<'\n';csv.flush();++samples;
  if(i==0||i==99)std::cout<<"READBACK ms="<<elapsed<<" D51="<<word[0]<<" F="<<on<<'\n';
  std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 std::cout<<"RESULT samples="<<samples<<" saw_ON="<<sawOn<<" OFF_after_ACK="<<cleared<<" first_OFF_ms="<<firstOffMs<<" final_D51="<<last<<" other_bits_changed="<<otherBitsChanged<<" writes=1 OFF_writes=0\n";
 return cleared?0:10;
}
