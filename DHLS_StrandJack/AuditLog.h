#pragma once
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <utility>

namespace DHLS_StrandJack::Services {
// Native-only asynchronous writer; never retains a View or touches XAML.
class AuditLog {
public:
    struct Event { std::chrono::system_clock::time_point at; std::string category, message; };
private:
    struct Entry { Event event; std::string context; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Entry> queue_;
    std::deque<Event> workEvents_;
    std::uint64_t workRevision_=0;
    std::filesystem::path directory_;
    std::string context_;
    bool closing_=false;
    std::thread worker_;
    AuditLog():worker_([this]{Run();}){}
    static std::string Clean(std::string s) {
        for(auto& c:s) if(c=='\r'||c=='\n'||c=='\t')c=' ';
        return s;
    }
    static bool IsWorkEvent(std::string const& code) {
        return code=="SIM_STEP" || code=="SIM_JOB_START" || code=="SIM_WARNING" || code=="SIM_FEEDBACK" || code=="SIM_SEQUENCE" ||
            code=="SIM_PRESSURE_STATE" || code=="SIM_RUN_RESULT" || code=="SIM_PUMP" ||
            code=="SIM_WEDGE" || code=="RUN_RESULT" || code=="AUTO_SEQUENCE" ||
            code=="PLC_MANUAL_D052" || code=="PLC_FAULT_STOP" || code=="LOAD_TOLERANCE";
    }
    static bool IsDashboardEvent(std::string const& code, std::string const& message) {
        if(code=="SIM_SEQUENCE")
            return message.find(" | State: Fault |")!=std::string::npos ||
                message.find(" | State: Emergency |")!=std::string::npos ||
                message.find(" | State: Stopped |")!=std::string::npos;
        if(code=="SIM_PRESSURE_STATE")
            return message.rfind("Fault:",0)==0 || message.rfind("SensorInvalid:",0)==0 ||
                message.rfind("CommunicationLost:",0)==0 || message.rfind("PressureLimitExceeded:",0)==0 ||
                message.rfind("SyncLimitExceeded:",0)==0 || message.rfind("Completed:",0)==0;
        return IsWorkEvent(code)&&code!="SIM_FEEDBACK";
    }
    static const char* EventTitle(std::string const& code) {
        if(code=="APP_START") return "Application opened";
        if(code=="APP_CLOSE") return "Application closed";
        if(code=="DASHBOARD_MODE") return "Dashboard mode selected";
        if(code=="UI_CLICK") return "Operator action";
        if(code=="COMMAND" || code=="SIM_COMMAND") return "Command queued";
        if(code=="COMMAND_REJECTED" || code=="SIM_COMMAND_REJECTED") return "Command rejected";
        if(code=="RUN_RESULT" || code=="SIM_RUN_RESULT") return "Run segment ended";
        if(code=="PLC_STATE") return "PLC status changed";
        if(code=="PLC_FAULT" || code=="PLC_FAULT_STOP") return "PLC fault protection";
        if(code=="PLC_BIT_STATE") return "PLC input/output changed";
        if(code=="SIM_SEQUENCE" || code=="AUTO_SEQUENCE") return "Sequence state changed";
        if(code=="SIM_STEP") return "Operation step";
        if(code=="SIM_FEEDBACK") return "Operation feedback";
        if(code=="SIM_PRESSURE_STATE") return "Pressure operation state changed";
        if(code=="SIM_MODE" || code=="SIM_SESSION") return "Simulation connection/mode";
        if(code=="SIM_PUMP") return "Simulated pump state changed";
        if(code=="SIM_WEDGE") return "Simulated wedge state changed";
        if(code=="MODEL_LOADED") return "Model loaded";
        if(code=="SAVE") return "Settings saved";
        if(code=="SAVE_FAILED") return "Settings save failed";
        return "Event";
    }
    void Run() noexcept {
        for(;;) {
            Entry entry; std::filesystem::path directory;
            { std::unique_lock<std::mutex> lock(mutex_);
              wake_.wait(lock,[this]{return closing_||(!directory_.empty()&&!queue_.empty());});
              if(queue_.empty()||(closing_&&directory_.empty()))return;
              entry=std::move(queue_.front());queue_.pop_front();directory=directory_; }
            try {
                auto const& event=entry.event;
                const auto second=std::chrono::floor<std::chrono::seconds>(event.at);
                auto t=std::chrono::system_clock::to_time_t(second);std::tm local{};localtime_s(&local,&t);
                std::ostringstream date,time;
                date<<(IsWorkEvent(event.category)?"Work_":"History_")<<std::put_time(&local,"%Y%m%d")<<".log";
                time<<std::put_time(&local,"%Y-%m-%d %H:%M:%S")<<'.'<<std::setfill('0')<<std::setw(3)
                    <<std::chrono::duration_cast<std::chrono::milliseconds>(event.at-second).count();
                std::filesystem::create_directories(directory);
                std::ofstream file(directory/date.str(),std::ios::binary|std::ios::app);
                file<<time.str()<<" | "<<EventTitle(event.category)<<" ["<<event.category<<"] | "
                    <<event.message<<entry.context<<"\r\n";
                file.flush();
                if(!file)throw std::runtime_error("Audit log write failed");
            } catch(...) {
                // Diagnostic fallback; disk failure must not interrupt emergency/STOP processing.
                std::fputs("[AuditLog] Unable to write log file.\n",stderr);
            }
        }
    }
public:
    static AuditLog& Instance(){static AuditLog value;return value;}
    ~AuditLog(){ {std::lock_guard<std::mutex> lock(mutex_);closing_=true;}wake_.notify_one();if(worker_.joinable())worker_.join(); }
    void Configure(std::filesystem::path directory){ {std::lock_guard<std::mutex> lock(mutex_);directory_=std::move(directory);}wake_.notify_one(); }
    static void Context(std::string model,std::string customer,std::string operatorName,std::string document) {
        auto& log=Instance();std::lock_guard<std::mutex> lock(log.mutex_);
        std::ostringstream context;
        context<<" | Model: "<<std::quoted(Clean(model))<<" | Customer: "<<std::quoted(Clean(customer))
            <<" | Operator: "<<std::quoted(Clean(operatorName))<<" | Document: "<<std::quoted(Clean(document));
        log.context_=context.str();
    }
    static void Write(std::string category,std::string message) noexcept {
        try {auto& log=Instance(); {std::lock_guard<std::mutex> lock(log.mutex_);
            if(log.closing_)return;
            const auto at=std::chrono::system_clock::now();
            category=Clean(std::move(category));message=Clean(std::move(message));
            log.queue_.push_back({{at,category,message},log.context_});
            log.wake_.notify_one();
            if(IsDashboardEvent(category,message)){
                log.workEvents_.push_back({at,std::move(category),std::move(message)});
                if(log.workEvents_.size()>80)log.workEvents_.pop_front();
                ++log.workRevision_;
            }}}catch(...){}
    }
    static bool ReadDashboardEvents(std::uint64_t& revision,std::vector<Event>& events) {
        auto& log=Instance();
        std::lock_guard<std::mutex> lock(log.mutex_);
        if(revision==log.workRevision_)return false;
        events.assign(log.workEvents_.begin(),log.workEvents_.end());
        revision=log.workRevision_;
        return true;
    }
};
}
