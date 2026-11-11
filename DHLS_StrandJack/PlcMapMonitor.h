#pragma once
#include "ControlLogic/PlcMapTracker.h"
#include "AuditLog.h"
#include "ConfigManager.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <memory>
#include <cwctype>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
namespace DHLS_StrandJack::Diagnostics {
class PlcMapMonitor:public std::enable_shared_from_this<PlcMapMonitor> {
    using Grid=winrt::Microsoft::UI::Xaml::Controls::Grid;
    using TextBlock=winrt::Microsoft::UI::Xaml::Controls::TextBlock;
    using Border=winrt::Microsoft::UI::Xaml::Controls::Border;
    using ComboBox=winrt::Microsoft::UI::Xaml::Controls::ComboBox;
    using FrameworkElement=winrt::Microsoft::UI::Xaml::FrameworkElement;
    using SolidColorBrush=winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    bool english_=false,paused_=false,useP_=false,useM_=false;
    std::function<void(bool,bool)> areasChanged_;
    winrt::Microsoft::UI::Xaml::Controls::CheckBox usePBox_{nullptr},useMBox_{nullptr};
    TextBlock preferencesStatus_{nullptr};
    Grid root_{nullptr};ComboBox family_{nullptr},filter_{nullptr};
    std::vector<winrt::Microsoft::UI::Xaml::Controls::RadioButton> plcButtons_;
    std::size_t selectedPlc_=0;
    winrt::Microsoft::UI::Xaml::Controls::TextBox search_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::CheckBox pause_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView rows_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ScrollViewer history_{nullptr};
    TextBlock historyText_{nullptr};
    TextBlock status_{nullptr},summary_{nullptr};
    struct Row {std::size_t entry;Border frame;TextBlock value,count,time;unsigned foreground=0xFFFFFFFF,background=0xFFFFFFFF,outline=0xFFFFFFFF;};
    std::vector<Row> views_;std::vector<std::wstring> ips_;
    Control::EquipmentSnapshot last_;PlcMapTracker tracker_;std::size_t historySize_=0;std::wstring lastHistoryStamp_;
    std::wstring T(wchar_t const* ko,wchar_t const* en)const{return english_?en:ko;}
    static SolidColorBrush Brush(unsigned rgb){winrt::Windows::UI::Color c{255,static_cast<std::uint8_t>(rgb>>16),static_cast<std::uint8_t>(rgb>>8),static_cast<std::uint8_t>(rgb)};return SolidColorBrush(c);}
    static void Columns(Grid const& g,std::initializer_list<double> widths){using namespace winrt::Microsoft::UI::Xaml;for(double w:widths){Controls::ColumnDefinition c;c.Width(GridLength{w<0?1.0:w,w<0?GridUnitType::Star:GridUnitType::Pixel});g.ColumnDefinitions().Append(c);}}
    static void Add(Grid const& g,FrameworkElement const& e,int row,int column){Grid::SetRow(e,row);Grid::SetColumn(e,column);g.Children().Append(e);}
    static TextBlock Text(std::wstring const& s,unsigned color=0x1E293B,double size=13){TextBlock t;t.Text(s);t.FontSize(size);t.Foreground(Brush(color));t.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);return t;}
    static std::filesystem::path PreferencesPath(){return Services::ConfigManager::GetModelsDirectory().parent_path()/L"PlcMapMonitor.json";}
    void LoadPreferences(){
        try{std::ifstream input(PreferencesPath(),std::ios::binary);if(!input)return;
            std::string data((std::istreambuf_iterator<char>(input)),{});auto json=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(data));
            useP_=json.GetNamedBoolean(L"UseP",false);useM_=json.GetNamedBoolean(L"UseM",false);
        }catch(...){useP_=useM_=false;Services::AuditLog::Write("ERROR","PLC map preferences load failed; P/M disabled");}
        tracker_.SetAreas(useP_,useM_);
    }
    bool SavePreferences(){
        try{auto path=PreferencesPath(),temp=path;temp+=L".tmp";
            {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<"{\n  \"UseP\": "<<(useP_?"true":"false")<<",\n  \"UseM\": "<<(useM_?"true":"false")<<"\n}\n";out.flush();if(!out)throw std::runtime_error("preferences write");}
            if(!::MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("preferences replace");
            return true;
        }catch(...){Services::AuditLog::Write("ERROR","PLC map preferences save failed");return false;}
    }
    void ChangeAreas(){
        useP_=usePBox_.IsChecked().Value();useM_=useMBox_.IsChecked().Value();tracker_.SetAreas(useP_,useM_);
        // Remove cached optional words; a new sample must establish availability after each selection.
        for(auto& plc:last_.plcs)plc.mapWords.erase(std::remove_if(plc.mapWords.begin(),plc.mapWords.end(),[](auto const& w){return w.family!='D';}),plc.mapWords.end());
        tracker_.Update(last_);if(areasChanged_)areasChanged_(useP_,useM_);
        const bool saved=SavePreferences();preferencesStatus_.Text(saved?T(L"선택 자동 저장 · 모든 PLC에 적용",L"Selection saved · applies to all PLCs"):T(L"선택 저장 실패 · 이번 실행에만 적용",L"Save failed · applied for this session"));
        preferencesStatus_.Foreground(Brush(saved?0x64748B:0xDC2626));
        Services::AuditLog::Write("PLC_MAP_AREAS",std::string("P=")+(useP_?"enabled":"disabled")+" M="+(useM_?"enabled":"disabled"));Render();
    }
    bool AreaEnabled(MapEntry const& d)const{return d.family==L'D'||(d.family==L'P'&&useP_)||(d.family==L'M'&&useM_);}
    std::size_t Selected()const{return selectedPlc_;}
    std::wstring Label(MapEntry const& d)const{return english_?d.english:d.korean;}
    static std::wstring Lower(std::wstring s){for(auto& c:s)c=static_cast<wchar_t>(std::towlower(c));return s;}
    std::vector<std::size_t> Visible()const {
        std::vector<std::size_t> ids;auto query=Lower(std::wstring(search_.Text()));auto const& catalog=PlcMapCatalog();
        for(std::size_t i=0;i<catalog.size();++i){auto const& d=catalog[i];auto const& v=tracker_.Value(Selected(),i);
            const int f=family_.SelectedIndex();if((f==0&&d.family!=L'D')||(f==1&&d.family!=L'P')||(f==2&&d.family!=L'M'))continue;
            if(!query.empty()&&Lower(d.address+L" "+Label(d)).find(query)==std::wstring::npos)continue;
            const int filter=filter_.SelectedIndex();if(filter==1&&(!v.known||d.bit<0||v.value==0))continue;
            if(filter==2&&!v.changes)continue;if(filter==3&&v.known)continue;ids.push_back(i);
        }return ids;
    }
    static std::wstring Hex(unsigned v){std::wostringstream t;t<<L"0x"<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill(L'0')<<v;return t.str();}
    std::wstring ValueText(MapEntry const& d,MapValue const& v)const{if(!AreaEnabled(d))return T(L"--  미사용",L"--  disabled");if(!v.known)return T(L"--  미수집",L"--  unavailable");if(d.bit>=0)return std::wstring(v.value?L"ON":L"OFF")+L"   "+Hex(v.word);return std::to_wstring(v.value)+L"   "+Hex(v.word);}
    void Rebuild(){
        rows_.Items().Clear();views_.clear();auto const& catalog=PlcMapCatalog();
        for(auto id:Visible()){
            auto const& d=catalog[id];Grid grid;Columns(grid,{100,100,-1,170,60,90});grid.MinHeight(35);grid.Padding({8,3,8,3});
            auto address=Text(d.address);address.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));Add(grid,address,0,0);
            auto direction=Text(english_&&d.direction==L"PLC 내부"?L"Internal":d.direction,0x64748B,11);Add(grid,direction,0,1);
            auto label=Text(Label(d));label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);Add(grid,label,0,2);
            auto value=Text(L"--"),count=Text(L"0"),time=Text(L"--");Add(grid,value,0,3);Add(grid,count,0,4);Add(grid,time,0,5);
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(grid,winrt::box_value(d.source));
            Border frame;frame.Background(Brush(0xFFFFFF));frame.CornerRadius({4,4,4,4});frame.BorderThickness({1,1,1,1});frame.Child(grid);
            rows_.Items().Append(frame);views_.push_back({id,frame,value,count,time});
        }
        Render();
    }
    void Render(){
        if(paused_)return;
        auto ids=Visible();bool mismatch=ids.size()!=views_.size();if(!mismatch)for(std::size_t i=0;i<ids.size();++i)if(ids[i]!=views_[i].entry){mismatch=true;break;}
        if(mismatch){Rebuild();return;}
        auto const& catalog=PlcMapCatalog();unsigned known=0,changed=0;auto now=PlcMapTracker::NowMs();
        for(auto& row:views_){auto const& d=catalog[row.entry];auto const& v=tracker_.Value(Selected(),row.entry);known+=v.known;changed+=v.changes;
            auto text=ValueText(d,v);if(row.value.Text()!=text)row.value.Text(text);
            const bool available=AreaEnabled(d)&&v.known;
            const bool bitOn=available&&d.bit>=0&&v.value!=0;
            const auto foreground=!available?0x94A3B8u:d.bit>=0?(bitOn?0x15803Du:0x64748Bu):0x1D4ED8u;
            if(row.foreground!=foreground){row.value.Foreground(Brush(foreground));row.foreground=foreground;}auto count=std::to_wstring(v.changes);if(row.count.Text()!=count)row.count.Text(count);
            auto stamp=v.changedTime.empty()?L"--":v.changedTime;if(row.time.Text()!=stamp)row.time.Text(stamp);
            // Bit state stays visible until the next sampled state; a change flash must not mask ON green.
            const unsigned background=!available?0xF8FAFC:d.bit>=0?(bitOn?0xDCFCE7:0xF1F5F9):(v.changedAtMs&&now-v.changedAtMs<1500?0xFEF3C7:0xFFFFFF);
            const unsigned outline=bitOn?0x22C55E:0xE2E8F0;
            if(row.outline!=outline){row.frame.BorderBrush(Brush(outline));row.outline=outline;}
            if(row.background!=background){row.frame.Background(Brush(background));row.background=background;}
        }
        auto p=Selected();bool connected=p<last_.plcs.size()&&last_.plcs[p].connected&&last_.plcs[p].readOk;
        status_.Text(last_.simulation?T(L"시뮬레이션: 실제 PLC 원시 맵을 제공하지 않습니다.",L"SIMULATION: no real PLC raw map is supplied."):connected?T(L"수신 중 · PLC 메모리 관측값",L"Receiving · observed PLC memory"):T(L"수신 불가 · 마지막 값을 OFF로 간주하지 않습니다.",L"Unavailable · missing data is not OFF."));
        status_.Foreground(Brush(connected&&!last_.simulation?0x15803D:0xB45309));
        if((family_.SelectedIndex()==1&&!useP_)||(family_.SelectedIndex()==2&&!useM_)){status_.Text(T(L"선택 영역 미사용 · 전체 사용 체크 시 상태를 수집합니다.",L"Selected area disabled · enable collection above."));status_.Foreground(Brush(0x64748B));}
        if(last_.emergencyActive){status_.Text(T(L"비상정지 신호 감지 · 실제 출력 정지는 별도 확인이 필요합니다.",L"EMERGENCY signal active · physical output stop requires verification."));status_.Foreground(Brush(0xDC2626));}
        summary_.Text(T(L"표시 ",L"Rows ")+std::to_wstring(views_.size())+T(L" / 수신 ",L" / available ")+std::to_wstring(known)+T(L" / 관측 변경 ",L" / observed changes ")+std::to_wstring(changed));
        auto const& history=tracker_.History();std::wstring stamp=history.empty()?L"":history.front().time+std::to_wstring(history.front().plc)+std::to_wstring(history.front().entry)+std::to_wstring(history.front().after);
        if(historySize_!=history.size()||stamp!=lastHistoryStamp_){std::wstring lines;
            for(auto const& change:history)if(static_cast<std::size_t>(change.plc)==p){auto const& d=catalog[change.entry];
                std::wstring old=d.bit>=0?(change.before?L"ON":L"OFF"):std::to_wstring(change.before);
                std::wstring value=d.bit>=0?(change.after?L"ON":L"OFF"):std::to_wstring(change.after);
                lines+=change.time+L"  "+d.address+L"  "+old+L" → "+value+(change.reacquired?T(L" (재수신)",L" (reacquired)"):L"")+L"  "+Label(d)+L"\n";}
            if(historyText_.Text()!=lines)historyText_.Text(lines);
            historySize_=history.size();lastHistoryStamp_=stamp;}
    }
    void Build(){using namespace winrt::Microsoft::UI::Xaml;using namespace Controls;
        root_=Grid{};root_.Background(Brush(0xF8FAFC));root_.RequestedTheme(ElementTheme::Light);
        for(auto height:{44.0,40.0,34.0,30.0,-1.0,26.0,100.0,44.0}){RowDefinition row;row.Height(GridLength{height<0?1.0:height,height<0?GridUnitType::Star:GridUnitType::Pixel});root_.RowDefinitions().Append(row);}
        Grid toolbar;Columns(toolbar,{72.0*ips_.size(),85,150,-1,140});
        StackPanel plcChoices;plcChoices.Orientation(Orientation::Horizontal);
        auto weak=weak_from_this();
        for(std::size_t i=0;i<ips_.size();++i){
            const auto name=L"PLC "+std::to_wstring(i+1);
            if(ips_.size()==1){
                auto label=Text(name);
                ToolTipService::SetToolTip(label,winrt::box_value(ips_[i]));
                plcChoices.Children().Append(label);
                break;
            }
            RadioButton choice;
            choice.GroupName(L"PlcMapDevice");
            choice.Content(winrt::box_value(name));
            choice.MinWidth(0);
            choice.Width(72);
            choice.IsChecked(winrt::box_value(i==0).as<winrt::Windows::Foundation::IReference<bool>>());
            ToolTipService::SetToolTip(choice,winrt::box_value(ips_[i]));
            choice.Checked([weak,i](auto const&,auto const&){
                if(auto self=weak.lock()){
                    self->selectedPlc_=i;
                    self->historySize_=static_cast<std::size_t>(-1);
                    self->Rebuild();
                }
            });
            plcButtons_.push_back(choice);
            plcChoices.Children().Append(choice);
        }
        Add(toolbar,plcChoices,0,0);
        family_=ComboBox{};for(auto s:{L"D",L"P",L"M",english_?L"All":L"전체"})family_.Items().Append(winrt::box_value(s));family_.SelectedIndex(0);Add(toolbar,family_,0,1);
        filter_=ComboBox{};for(auto const& s:{T(L"전체 상태",L"All states"),T(L"ON 비트",L"ON bits"),T(L"변경된 항목",L"Changed"),T(L"미수집",L"Unavailable")})filter_.Items().Append(winrt::box_value(s));filter_.SelectedIndex(0);Add(toolbar,filter_,0,2);
        search_=TextBox{};search_.PlaceholderText(T(L"주소·설명 검색",L"Search address or description"));search_.Margin({6,0,8,0});Add(toolbar,search_,0,3);
        pause_=CheckBox{};pause_.Content(winrt::box_value(T(L"표시 일시정지",L"Pause display")));Add(toolbar,pause_,0,4);Add(root_,toolbar,0,0);
        Grid areaSettings;Columns(areaSettings,{210,210,-1});
        usePBox_=CheckBox{};usePBox_.Content(winrt::box_value(T(L"P 주소 전체 사용",L"Enable all P addresses")));usePBox_.IsChecked(winrt::box_value(useP_).as<winrt::Windows::Foundation::IReference<bool>>());Add(areaSettings,usePBox_,0,0);
        useMBox_=CheckBox{};useMBox_.Content(winrt::box_value(T(L"M 주소 전체 사용",L"Enable all M addresses")));useMBox_.IsChecked(winrt::box_value(useM_).as<winrt::Windows::Foundation::IReference<bool>>());Add(areaSettings,useMBox_,0,1);
        preferencesStatus_=Text(T(L"선택 자동 저장 · 모든 PLC에 적용",L"Selection saved · applies to all PLCs"),0x64748B,12);Add(areaSettings,preferencesStatus_,0,2);Add(root_,areaSettings,1,0);
        status_=Text(T(L"연결 대기",L"Waiting for connection"));Add(root_,status_,2,0);
        Grid header;Columns(header,{100,100,-1,170,60,90});header.Padding({8,0,8,0});int col=0;
        for(auto const& s:{T(L"주소",L"Address"),T(L"방향",L"Direction"),T(L"맵 설명",L"Map description"),T(L"상태 / WORD",L"State / WORD"),T(L"변경",L"Changes"),T(L"변경 시각",L"Changed at")})Add(header,Text(s,0x475569,12),0,col++);Add(root_,header,3,0);
        rows_=ListView{};rows_.SelectionMode(ListViewSelectionMode::None);rows_.IsItemClickEnabled(false);
        Style style(winrt::xaml_typename<ListViewItem>());style.Setters().Append(Setter(winrt::Microsoft::UI::Xaml::Controls::Control::HorizontalContentAlignmentProperty(),winrt::box_value(HorizontalAlignment::Stretch)));style.Setters().Append(Setter(winrt::Microsoft::UI::Xaml::Controls::Control::PaddingProperty(),winrt::box_value(Thickness{0,0,0,0})));rows_.ItemContainerStyle(style);Add(root_,rows_,4,0);
        summary_=Text(L"",0x475569,12);Add(root_,summary_,5,0);history_=ScrollViewer{};history_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);history_.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);history_.Background(Brush(0xE2E8F0));historyText_=Text(L"",0x334155,12);historyText_.Padding({8,6,8,6});historyText_.VerticalAlignment(VerticalAlignment::Top);history_.Content(historyText_);Add(root_,history_,6,0);
        auto note=Text(T(L"최근 변경 500건 · 약 200ms 관측: 짧은 신호는 누락될 수 있습니다. P/M은 전체 사용 선택 시 수집합니다. 명령 비트 ON은 실제 출력 확인이 아닙니다.",L"Latest 500 changes · ~200ms observation may miss short signals. P/M are collected only when enabled. A command bit is not physical output feedback."),0x64748B,11);note.TextWrapping(TextWrapping::Wrap);Add(root_,note,7,0);
        auto changed=[weak](auto const&,auto const&){if(auto self=weak.lock())self->ChangeAreas();};
        usePBox_.Checked(changed);usePBox_.Unchecked(changed);useMBox_.Checked(changed);useMBox_.Unchecked(changed);
        family_.SelectionChanged([weak](auto const&,auto const&){if(auto s=weak.lock())s->Rebuild();});filter_.SelectionChanged([weak](auto const&,auto const&){if(auto s=weak.lock())s->Rebuild();});search_.TextChanged([weak](auto const&,auto const&){if(auto s=weak.lock())s->Rebuild();});
        pause_.Checked([weak](auto const&,auto const&){if(auto s=weak.lock()){s->paused_=true;s->usePBox_.IsEnabled(false);s->useMBox_.IsEnabled(false);s->search_.IsEnabled(false);s->family_.IsEnabled(false);s->filter_.IsEnabled(false);for(auto const& choice:s->plcButtons_)choice.IsEnabled(false);}});pause_.Unchecked([weak](auto const&,auto const&){if(auto s=weak.lock()){s->paused_=false;s->usePBox_.IsEnabled(true);s->useMBox_.IsEnabled(true);s->search_.IsEnabled(true);s->family_.IsEnabled(true);s->filter_.IsEnabled(true);for(auto const& choice:s->plcButtons_)choice.IsEnabled(true);s->Render();}});Rebuild();
    }
public:
    PlcMapMonitor(std::vector<std::wstring> ips,bool english):english_(english),ips_(std::move(ips)),tracker_((std::max)(std::size_t(1),ips_.size())){if(ips_.empty())ips_.push_back(L"--");LoadPreferences();}
    static std::shared_ptr<PlcMapMonitor> Create(std::vector<std::wstring> ips,bool english){auto p=std::make_shared<PlcMapMonitor>(std::move(ips),english);p->Build();return p;}
    bool UseP()const{return useP_;}bool UseM()const{return useM_;}
    void SetAreaHandler(std::function<void(bool,bool)> handler){areasChanged_=std::move(handler);}
    Grid Root()const{return root_;}
    void Update(Control::EquipmentSnapshot const& snapshot){last_=snapshot;auto changes=tracker_.Update(snapshot);if(!changes.empty())historySize_=static_cast<std::size_t>(-1);
        for(auto const& c:changes)Services::AuditLog::Write("PLC_MAP_CHANGE","PLC="+std::to_string(c.plc+1)+" "+winrt::to_string(PlcMapCatalog()[c.entry].address)+" old="+std::to_string(c.before)+" new="+std::to_string(c.after)+(c.reacquired?" reacquired":""));
        Render();}
};
}
