#include "pch.h"
#include "ControlSettingsView.xaml.h"
#include "ConfigManager.h"
#include "ControlLogic/DistanceDeceleration.h"
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Globalization.NumberFormatting.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;
namespace {
SolidColorBrush Paint(unsigned rgb){return SolidColorBrush{Windows::UI::Color{255,
    static_cast<uint8_t>(rgb>>16),static_cast<uint8_t>(rgb>>8),static_cast<uint8_t>(rgb)}};}
hstring Number(double value,int digits=1){std::wostringstream text;text<<std::fixed<<std::setprecision(digits)<<value;return hstring{text.str()};}
TextBlock Label(hstring const& value,double size=14,unsigned color=0x334155){TextBlock t;t.Text(value);t.FontSize(size);t.Foreground(Paint(color));t.TextWrapping(TextWrapping::Wrap);return t;}
struct DistanceEditor {
    bool english=false;
    double target=100;
    NumberBox start,minimum,tolerance;
    Slider position;
    Microsoft::UI::Xaml::Shapes::Rectangle zone,stopZone,fill;
    Microsoft::UI::Xaml::Shapes::Ellipse marker,speedMarker;
    TextBlock startLabel,metrics,error;
    StackPanel preview;
    Microsoft::UI::Xaml::Shapes::Polyline curve;
    bool Valid() const {return ::DHLS_StrandJack::Control::ValidDistanceDeceleration(start.Value(),minimum.Value(),tolerance.Value());}
    void Refresh(){
        if(!Valid()){
            preview.Visibility(Visibility::Collapsed);
            error.Visibility(Visibility::Visible);
            error.Text(english?L"Enter a positive deceleration distance, a minimum speed of 1–100%, and a tolerance smaller than the deceleration distance.":L"감속 시작 거리는 0보다 크게, 최저 속도는 1~100%, 허용오차는 감속 거리보다 작게 입력하세요.");
            return;
        }
        preview.Visibility(Visibility::Visible);
        error.Text(L"");error.Visibility(Visibility::Collapsed);
        const double begin=std::max(0.0,target-start.Value()),current=position.Value(),remaining=std::max(0.0,target-current);
        const double percent=::DHLS_StrandJack::Control::DistanceDecelerationPercent(remaining,start.Value(),minimum.Value(),tolerance.Value());
        const double edge=std::max(0.0,target-tolerance.Value());
        const bool stopped=remaining<=tolerance.Value();
        const bool slowing=remaining<=start.Value();
        const auto stageColor=stopped?0xB91C1C:slowing?0x92400E:0x1D4ED8;
        Canvas::SetLeft(zone,40+660*begin/target);zone.Width(660*(edge-begin)/target);
        Canvas::SetLeft(stopZone,40+660*edge/target);stopZone.Width(660*(target-edge)/target);
        fill.Width(660*current/target);Canvas::SetLeft(marker,30+660*current/target);
        startLabel.Text((english?L"Travel at slowdown: ":L"감속 시작 이동거리 ")+Number(begin,2)+L" mm    ·    "+
            (english?L"Travel at stop request: ":L"정지 요청 이동거리 ")+Number(edge,2)+L" mm");
        auto stage=stopped?(english?L"Stop request":L"정지 요청"):
            slowing?(english?L"Deceleration zone":L"감속 구간"):(english?L"Base speed":L"기준 속도");
        metrics.Foreground(Paint(stageColor));
        metrics.Text(hstring{stage}+L"    |    "+(english?L"Distance left: ":L"목표까지 ")+Number(remaining,2)+L" mm    |    "+
            (english?L"Speed ratio: ":L"속도 비율 ")+Number(percent)+L"%");
        speedMarker.Fill(Paint(stageColor));
        Canvas::SetLeft(speedMarker,34+660*current/target);
        Canvas::SetTop(speedMarker,104-percent*0.85);
        auto points=curve.Points();points.Clear();
        auto point=[&](double x,double y){points.Append(Windows::Foundation::Point{static_cast<float>(40+660*x/target),static_cast<float>(110-y*0.85)});};
        point(0,::DHLS_StrandJack::Control::DistanceDecelerationPercent(target,start.Value(),minimum.Value(),tolerance.Value()));
        if(begin>0)point(begin,100);
        // The vertical drop is a stop request at tolerance, rather than an invented ramp below minimum speed.
        const double before=minimum.Value()+(100-minimum.Value())*std::min(1.0,tolerance.Value()/start.Value());
        if(edge>0)point(edge,before);
        point(edge,0);point(target,0);
    }
};
struct PressureEditor {
    bool english=false;double target=10;
    ::DHLS_StrandJack::Control::PressureAutoSettings initial;
    std::array<NumberBox,7> boxes;
    Slider position;
    Microsoft::UI::Xaml::Shapes::Rectangle zone,fill;Microsoft::UI::Xaml::Shapes::Ellipse marker;Microsoft::UI::Xaml::Shapes::Polyline curve;
    TextBlock metrics,error,startLabel;
    ::DHLS_StrandJack::Control::PressureAutoSettings Read()const{
        auto v=initial;v.toleranceMPa=boxes[0].Value()/10;v.slowdownMPa=boxes[1].Value()/10;v.minimumPercent=boxes[2].Value();
        v.gainPercentPerMPa=boxes[3].Value();v.balanceToleranceMPa=boxes[4].Value()/10;v.ratePercentPerSecond=boxes[5].Value();
        double ms=boxes[6].Value();v.settleMs=std::isfinite(ms)&&ms>=0&&ms<=60000?static_cast<int>(ms):-1;return v;
    }
    double Speed(double current)const{auto v=Read();const double gap=std::max(0.0,target-current);
        return gap<=v.toleranceMPa?0:gap>v.slowdownMPa?100:std::clamp(v.gainPercentPerMPa*gap,v.minimumPercent,100.0);
    }
    void Refresh(){
        auto v=Read();if(!v.Valid()){error.Text(english?L"Check tolerance < slowdown gap, minimum 1–100%, and positive gain/rate.":L"허용오차는 감속 압력차보다 작게, 최저 속도는 1~100%, 게인·변화율은 양수로 입력하세요.");return;}
        error.Text(L"");const double begin=std::max(0.0,target-v.slowdownMPa),current=position.Value();
        Canvas::SetLeft(zone,40+660*begin/target);zone.Width(660*(target-begin)/target);
        fill.Width(660*current/target);Canvas::SetLeft(marker,30+660*current/target);
        startLabel.Text((english?L"Slowdown starts: ":L"감속 시작: ")+Number(begin*10,0)+L" bar");
        Canvas::SetLeft(startLabel,std::clamp(40+660*begin/target-70,40.0,510.0));
        metrics.Text((english?L"Current: ":L"현재 압력: ")+Number(current*10,0)+L" bar  |  "+(english?L"Remaining: ":L"목표까지: ")+Number(std::max(0.0,target-current)*10,0)+L" bar  |  "+(english?L"Speed cap: ":L"속도 상한: ")+Number(Speed(current))+L"%"+(Speed(current)==0?(english?L" · Hold / settle / stop":L" · 유지 → 안정화 → 정지"):L""));
        auto points=curve.Points();points.Clear();for(int i=0;i<=200;++i){const double samplePressure=target*i/200.0;points.Append(Windows::Foundation::Point{static_cast<float>(40+660*i/200.0),static_cast<float>(110-Speed(samplePressure)*.85)});}
    }
};

}
namespace winrt::DHLS_StrandJack::implementation {
void ControlSettingsView::OnDistanceDecelerationClick(Windows::Foundation::IInspectable const&,RoutedEventArgs const&){ShowDistanceDecelerationAsync();}
winrt::fire_and_forget ControlSettingsView::ShowDistanceDecelerationAsync(){
    auto lifetime=get_strong();if(m_distanceDialogOpen||!IsLoaded()||!XamlRoot())co_return;
    m_distanceDialogOpen=true;struct Reset{bool& flag;~Reset(){flag=false;}} reset{m_distanceDialogOpen};
    try{
        const auto model=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
        auto const initial=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
        auto state=std::make_shared<DistanceEditor>();state->english=::DHLS_StrandJack::Services::Localization::IsEnglish();const bool en=state->english;
        state->target=std::isfinite(initial.MovementDistanceMm)&&initial.MovementDistanceMm>0?initial.MovementDistanceMm:100;
        ContentDialog dialog;dialog.RequestedTheme(ElementTheme::Light);dialog.XamlRoot(XamlRoot());dialog.Title(box_value(en?L"Distance-proportional deceleration":L"거리 비례 감속 설정"));
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"),box_value(900.0));
        dialog.PrimaryButtonText(en?L"Apply":L"적용");dialog.CloseButtonText(en?L"Cancel":L"취소");dialog.DefaultButton(ContentDialogButton::Primary);
        StackPanel content;content.Spacing(8);content.Width(std::min(800.0,std::max(260.0,XamlRoot().Size().Width-120.0)));
        content.Children().Append(Label(en?L"Set how the jack slows down as it approaches the target. Use the preview below to check when slowdown and the stop request occur.":L"목표에 가까워질 때의 감속을 설정합니다. 아래 미리보기에서 감속 시작과 정지 요청 시점을 확인하세요."));
        Grid fields;fields.ColumnSpacing(12);
        for(int i=0;i<3;++i){ColumnDefinition col;col.Width(GridLength{1,GridUnitType::Star});fields.ColumnDefinitions().Append(col);}
        Windows::Globalization::NumberFormatting::DecimalFormatter format;format.IntegerDigits(1);format.FractionDigits(2);format.IsGrouped(false);
        auto field=[&](NumberBox const& box,hstring const& title,double value,double min,double max,int column){
            box.Header(Label(title,13));box.Minimum(min);box.Maximum(max);box.Value(value);box.SmallChange(column==1?1:0.1);box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);box.NumberFormatter(format);box.HorizontalAlignment(HorizontalAlignment::Stretch);box.VerticalAlignment(VerticalAlignment::Bottom);Grid::SetColumn(box,column);fields.Children().Append(box);
        };
        field(state->start,en?L"Slow down this far before target (mm)":L"목표 몇 mm 전에 감속할까요?",initial.DistanceDecelerationStartMm,0.01,999999,0);
        field(state->minimum,en?L"Minimum % of base speed":L"최저 속도 (기준 속도 대비 %)",initial.DistanceDecelerationMinimumPercent,1,100,1);
        field(state->tolerance,en?L"Request stop within this distance (mm)":L"목표 몇 mm 이내에서 정지 요청?",initial.DistanceDecelerationToleranceMm,0,999999,2);
        content.Children().Append(fields);
        state->error=Label(L"",13,0xB91C1C);content.Children().Append(state->error);
        Border picture;picture.Background(Paint(0xF1F5F9));picture.BorderBrush(Paint(0xCBD5E1));picture.BorderThickness(Thickness{1});picture.CornerRadius(Microsoft::UI::Xaml::CornerRadius{12});picture.Padding(Thickness{12});
        StackPanel pictureContent;pictureContent.Spacing(4);pictureContent.Children().Append(Label(en?L"Cylinder travel preview":L"실린더 이동 미리보기",18,0x0F172A));
        pictureContent.Children().Append(Label((en?L"Movement distance: ":L"기존 이동변위: ")+Number(state->target,::DHLS_StrandJack::DisplayUnits::DisplacementDigits())+L" mm  ·  "+(en?L"100% = configured operating speed":L"100% = 선택 방향의 설정 속도"),13));
        state->startLabel=Label(L"",13);pictureContent.Children().Append(state->startLabel);
        Canvas drawing;drawing.Width(740);drawing.Height(80);
        Microsoft::UI::Xaml::Shapes::Rectangle track;track.Width(660);track.Height(28);track.RadiusX(14);track.RadiusY(14);track.Fill(Paint(0xCBD5E1));Canvas::SetLeft(track,40);Canvas::SetTop(track,10);drawing.Children().Append(track);
        state->fill.Height(28);state->fill.RadiusX(14);state->fill.RadiusY(14);state->fill.Fill(Paint(0x3B82F6));state->fill.Opacity(0.5);Canvas::SetLeft(state->fill,40);Canvas::SetTop(state->fill,10);drawing.Children().Append(state->fill);
        state->zone.Height(38);state->zone.Fill(Paint(0xF59E0B));state->zone.Opacity(0.45);Canvas::SetTop(state->zone,5);drawing.Children().Append(state->zone);
        state->stopZone.Height(38);state->stopZone.Fill(Paint(0xEF4444));state->stopZone.Opacity(0.55);Canvas::SetTop(state->stopZone,5);drawing.Children().Append(state->stopZone);
        Line goal;goal.X1(700);goal.X2(700);goal.Y1(2);goal.Y2(46);goal.Stroke(Paint(0xEF4444));goal.StrokeThickness(3);DoubleCollection dash;dash.Append(4);dash.Append(3);goal.StrokeDashArray(dash);drawing.Children().Append(goal);
        state->marker.Width(20);state->marker.Height(20);state->marker.Fill(Paint(0x1D4ED8));state->marker.Stroke(Paint(0xFFFFFF));state->marker.StrokeThickness(3);Canvas::SetTop(state->marker,14);drawing.Children().Append(state->marker);
        auto zero=Label(L"0 mm",17);Canvas::SetLeft(zero,40);Canvas::SetTop(zero,49);drawing.Children().Append(zero);
        auto target=Label(Number(state->target,::DHLS_StrandJack::DisplayUnits::DisplacementDigits())+L" mm",17);Canvas::SetLeft(target,580);Canvas::SetTop(target,49);drawing.Children().Append(target);
        Viewbox drawingBox;drawingBox.Child(drawing);drawingBox.Stretch(Stretch::Uniform);drawingBox.MaxHeight(80);pictureContent.Children().Append(drawingBox);
        pictureContent.Children().Append(Label(en?L"Drag to preview travel  ·  Amber: slowdown  ·  Red: stop request":L"슬라이더로 이동거리 확인  ·  주황: 감속 구간  ·  빨강: 정지 요청 구간",13));
        state->position.Minimum(0);state->position.Maximum(state->target);state->position.StepFrequency(std::max(0.001,state->target/1000));state->position.Value(0);pictureContent.Children().Append(state->position);
        state->metrics=Label(L"",15,0x1D4ED8);pictureContent.Children().Append(state->metrics);picture.Child(pictureContent);
        content.Children().Append(state->preview);
        state->preview.Children().Append(picture);
        state->preview.Children().Append(Label(en?L"Speed ratio over travel":L"이동거리에 따른 속도 비율",15));
        Canvas chart;chart.Width(740);chart.Height(142);
        Line axis;axis.X1(40);axis.X2(700);axis.Y1(110);axis.Y2(110);axis.Stroke(Paint(0x94A3B8));axis.StrokeThickness(1);chart.Children().Append(axis);
        auto top=Label(L"100%",12);Canvas::SetTop(top,4);chart.Children().Append(top);auto bottom=Label(L"0%",12);Canvas::SetTop(bottom,101);chart.Children().Append(bottom);
        state->curve.Stroke(Paint(0x2563EB));state->curve.StrokeThickness(3);chart.Children().Append(state->curve);
        state->speedMarker.Width(12);state->speedMarker.Height(12);state->speedMarker.Stroke(Paint(0xFFFFFF));state->speedMarker.StrokeThickness(1);chart.Children().Append(state->speedMarker);
        auto endLabel=Label(en?L"Target":L"목표",12);Canvas::SetLeft(endLabel,675);Canvas::SetTop(endLabel,117);chart.Children().Append(endLabel);
        Viewbox chartBox;chartBox.Child(chart);chartBox.Stretch(Stretch::Uniform);chartBox.MaxHeight(105);state->preview.Children().Append(chartBox);
        content.Children().Append(Label(en?L"Sequence sheet example: for a 20 mm slowdown, slow down from 75 to 55 mm when position decreases or from 335 to 355 mm when it increases.":L"시퀀스 시트 예시 · 감속 거리 20 mm: 변위가 줄면 75 → 55 mm, 늘면 335 → 355 mm 구간에서 감속합니다.",13));
        content.Children().Append(Label(en?L"This preview does not change PLC outputs. Apply, then save the control settings to retain these values.":L"미리보기는 PLC 출력을 변경하지 않습니다. 적용 후 제어 설정에서 저장하면 값이 유지됩니다.",12,0x64748B));
        ScrollViewer scroll;scroll.Content(content);scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);scroll.MaxHeight(std::max(200.0,XamlRoot().Size().Height-220.0));dialog.Content(scroll);
        // The coroutine owns the editor until the modal closes. View-owned UI is
        // never retained by its own delegates; event closures only retain weak state.
        auto changed=[weak=std::weak_ptr<DistanceEditor>(state)](auto const&,auto const&){if(auto editor=weak.lock())editor->Refresh();};
        state->start.ValueChanged(changed);state->minimum.ValueChanged(changed);state->tolerance.ValueChanged(changed);state->position.ValueChanged(changed);
        dialog.PrimaryButtonClick([weak=std::weak_ptr<DistanceEditor>(state)](auto const&,ContentDialogButtonClickEventArgs const& args){if(auto editor=weak.lock();!editor||!editor->Valid()){args.Cancel(true);if(editor)editor->Refresh();}});
        state->Refresh();
        if(co_await dialog.ShowAsync()!=ContentDialogResult::Primary||!IsLoaded())co_return;
        auto& config=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();if(config.ModelInfo.ModelName!=model)co_return;
        auto& settings=config.ControlSettings;
        ::DHLS_StrandJack::Services::AuditLog::Write("DISTANCE_DECELERATION","startMm="+std::to_string(settings.DistanceDecelerationStartMm)+" -> "+std::to_string(state->start.Value())+" minimumPercent="+std::to_string(settings.DistanceDecelerationMinimumPercent)+" -> "+std::to_string(state->minimum.Value())+" toleranceMm="+std::to_string(settings.DistanceDecelerationToleranceMm)+" -> "+std::to_string(state->tolerance.Value()));
        settings.DistanceDecelerationStartMm=state->start.Value();settings.DistanceDecelerationMinimumPercent=state->minimum.Value();settings.DistanceDecelerationToleranceMm=state->tolerance.Value();
    }catch(hresult_error const& e){::OutputDebugStringW(e.message().c_str());::DHLS_StrandJack::Services::AuditLog::Write("ERROR","Distance deceleration editor could not be displayed");}
}
}

namespace winrt::DHLS_StrandJack::implementation {
void ControlSettingsView::OnPressureAutoSettingsClick(Windows::Foundation::IInspectable const&,RoutedEventArgs const&){ShowPressureAutoSettingsAsync();}
winrt::fire_and_forget ControlSettingsView::ShowPressureAutoSettingsAsync(){
    auto lifetime=get_strong();if(m_distanceDialogOpen||!IsLoaded()||!XamlRoot())co_return;
    m_distanceDialogOpen=true;struct Guard{bool& b;~Guard(){b=false;}}guard{m_distanceDialogOpen};
    try{
        const auto model=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
        const auto control=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
        const bool en=::DHLS_StrandJack::Services::Localization::IsEnglish();
        auto state=std::make_shared<PressureEditor>();state->english=en;state->initial=control.PressureAuto;
        state->target=std::isfinite(control.PressureTargetBar)&&control.PressureTargetBar>0?control.PressureTargetBar/10:10;
        ContentDialog dialog;dialog.RequestedTheme(ElementTheme::Light);dialog.XamlRoot(XamlRoot());dialog.Title(box_value(en?L"Pressure-proportional deceleration":L"압력 자동운전 · 압력 비례 감속"));
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"),box_value(900.0));
        StackPanel panel;panel.Spacing(10);panel.Width(std::min(800.0,std::max(260.0,static_cast<double>(XamlRoot().Size().Width)-120)));
        panel.Children().Append(Label(en?L"Set the slowdown gap, minimum speed and target tolerance. Target pressure and direction are selected on the dashboard.":L"감속 압력차·최저 속도·허용오차를 설정하세요. 목표 압력과 상승/하강 방향은 대시보드에서 선택합니다."));
        Grid fields;fields.ColumnSpacing(12);for(int i=0;i<3;++i){ColumnDefinition col;col.Width(GridLength{1,GridUnitType::Star});fields.ColumnDefinitions().Append(col);}
        const wchar_t* ko[]={L"목표 전 감속 압력차 (bar)",L"최저 속도 (%)",L"목표 허용오차 (bar)"};
        const wchar_t* eng[]={L"Slowdown gap (bar)",L"Minimum speed (%)",L"Target tolerance (bar)"};
        int order[]={1,2,0};for(int column=0;column<3;++column){int i=order[column];auto box=state->boxes[i];box.Header(box_value(en?eng[column]:ko[column]));box.Minimum(i==2?1:0);box.Maximum(i==2?100:100000);box.Value(i==0?control.PressureAuto.toleranceMPa*10:i==1?control.PressureAuto.slowdownMPa*10:control.PressureAuto.minimumPercent);box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);Grid::SetColumn(box,column);fields.Children().Append(box);}panel.Children().Append(fields);
        state->error=Label(L"",13,0xB91C1C);panel.Children().Append(state->error);
        Border picture;picture.Background(Paint(0xF1F5F9));picture.CornerRadius(Microsoft::UI::Xaml::CornerRadius{12});picture.Padding(Thickness{12});
        StackPanel visual;visual.Spacing(6);visual.Children().Append(Label(en?L"Pressure tracking preview — UP example":L"압력 추종 미리보기 — 상승 예시",18,0x0F172A));
        visual.Children().Append(Label((en?L"Target pressure: ":L"목표 압력: ")+Number(state->target*10,0)+L" bar  ·  "+(en?L"DOWN uses the remaining pressure difference in reverse.":L"하강은 현재 압력 − 목표 압력의 차이로 반대로 적용합니다."),13));
        Canvas drawing;drawing.Width(740);drawing.Height(170);
        Microsoft::UI::Xaml::Shapes::Rectangle track;track.Width(660);track.Height(28);track.RadiusX(14);track.RadiusY(14);track.Fill(Paint(0xCBD5E1));Canvas::SetLeft(track,40);Canvas::SetTop(track,74);drawing.Children().Append(track);
        state->zone.Height(64);state->zone.Fill(Paint(0xF59E0B));state->zone.Opacity(.3);Canvas::SetTop(state->zone,56);drawing.Children().Append(state->zone);
        state->fill.Height(28);state->fill.Fill(Paint(0x3B82F6));Canvas::SetLeft(state->fill,40);Canvas::SetTop(state->fill,74);drawing.Children().Append(state->fill);
        Line goal;goal.X1(700);goal.X2(700);goal.Y1(42);goal.Y2(127);goal.Stroke(Paint(0xEF4444));goal.StrokeThickness(3);drawing.Children().Append(goal);
        state->marker.Width(20);state->marker.Height(20);state->marker.Fill(Paint(0x1D4ED8));state->marker.Stroke(Paint(0xFFFFFF));state->marker.StrokeThickness(3);Canvas::SetTop(state->marker,78);drawing.Children().Append(state->marker);
        state->startLabel=Label(L"",16);Canvas::SetTop(state->startLabel,16);drawing.Children().Append(state->startLabel);
        auto zero=Label(L"0 bar",16);Canvas::SetLeft(zero,40);Canvas::SetTop(zero,132);drawing.Children().Append(zero);
        auto target=Label(Number(state->target*10,0)+L" bar",16);Canvas::SetLeft(target,605);Canvas::SetTop(target,132);drawing.Children().Append(target);
        Viewbox drawingBox;drawingBox.Child(drawing);drawingBox.Stretch(Stretch::Uniform);drawingBox.MaxHeight(145);visual.Children().Append(drawingBox);
        visual.Children().Append(Label(en?L"Drag to preview pressure; no PLC outputs are sent.":L"슬라이더로 압력을 바꿔 보세요. 미리보기는 PLC 출력을 보내지 않습니다.",13));
        state->position.Minimum(0);state->position.Maximum(state->target);state->position.StepFrequency(state->target/1000);visual.Children().Append(state->position);
        state->metrics=Label(L"",15,0x1D4ED8);visual.Children().Append(state->metrics);picture.Child(visual);panel.Children().Append(picture);
        panel.Children().Append(Label(en?L"Speed cap vs pressure":L"압력에 따른 속도 상한",15));Canvas chart;chart.Width(740);chart.Height(142);
        Line axis;axis.X1(40);axis.X2(700);axis.Y1(110);axis.Y2(110);axis.Stroke(Paint(0x94A3B8));axis.StrokeThickness(1);chart.Children().Append(axis);
        state->curve.Stroke(Paint(0x2563EB));state->curve.StrokeThickness(3);chart.Children().Append(state->curve);
        auto full=Label(L"100%",12);chart.Children().Append(full);auto stop=Label(L"0%",12);Canvas::SetTop(stop,100);chart.Children().Append(stop);
        Viewbox chartBox;chartBox.Child(chart);chartBox.Stretch(Stretch::Uniform);chartBox.MaxHeight(105);panel.Children().Append(chartBox);
        Expander advanced;advanced.Header(box_value(en?L"Advanced: gain / spread / rate / settle":L"고급 설정: 게인·편차·변화율·안정화"));advanced.HorizontalAlignment(HorizontalAlignment::Stretch);
        StackPanel advancedFields;advancedFields.Spacing(8);
        const wchar_t* advancedKo[]={L"압력 비례 게인 (%/MPa)",L"압력 편차 정지 기준 (bar, 0=진단만)",L"속도 변화율 (%/초)",L"목표 안정화 시간 (ms)"};
        const wchar_t* advancedEn[]={L"Gain (%/MPa)",L"Pressure spread stop (bar, 0=diagnostic)",L"Speed rate (%/second)",L"Target settle time (ms)"};
        double advancedValues[]={control.PressureAuto.gainPercentPerMPa,control.PressureAuto.balanceToleranceMPa*10,control.PressureAuto.ratePercentPerSecond,static_cast<double>(control.PressureAuto.settleMs)};
        for(int i=3;i<7;++i){auto box=state->boxes[i];box.Header(box_value(en?advancedEn[i-3]:advancedKo[i-3]));box.Minimum(0);box.Maximum(i==6?60000:100000);box.Value(advancedValues[i-3]);box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);advancedFields.Children().Append(box);}advanced.Content(advancedFields);panel.Children().Append(advanced);
        panel.Children().Append(Label(en?L"The graph shows the instantaneous speed cap; the configured rate limit also applies. All selected ports must reach target. Apply, then Save. Maximum pressure/load and lowering stop pressure remain independent protections.":L"그림은 즉시 계산한 속도 상한이며 실제 지령에는 변화율 제한도 적용됩니다. 선택 포트가 모두 목표에 도달해야 완료됩니다. 적용 후 저장하세요. 최대 정지 압력·최대 하중·하강 정지 압력은 별도 보호 조건입니다.",12,0x64748B));
        ScrollViewer scroll;scroll.Content(panel);scroll.MaxHeight(std::max(220.0,static_cast<double>(XamlRoot().Size().Height)-220.0));dialog.Content(scroll);
        auto changed=[weak=std::weak_ptr<PressureEditor>(state)](auto const&,auto const&){if(auto editor=weak.lock())editor->Refresh();};
        for(auto box:state->boxes)box.ValueChanged(changed);state->position.ValueChanged(changed);
        dialog.PrimaryButtonText(en?L"Apply":L"적용");dialog.CloseButtonText(en?L"Cancel":L"취소");
        dialog.PrimaryButtonClick([weak=std::weak_ptr<PressureEditor>(state)](auto const&,ContentDialogButtonClickEventArgs const& args){if(auto editor=weak.lock();!editor||!editor->Read().Valid()){args.Cancel(true);if(editor)editor->Refresh();}});
        state->Refresh();if(co_await dialog.ShowAsync()!=ContentDialogResult::Primary||!IsLoaded())co_return;
        auto& config=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();if(config.ModelInfo.ModelName!=model)co_return;
        config.ControlSettings.PressureAuto=state->Read();::DHLS_StrandJack::Services::AuditLog::Write("PRESSURE_AUTO_SETTINGS","Pressure deceleration settings updated; save control settings to persist");
    }catch(hresult_error const&){::DHLS_StrandJack::Services::AuditLog::Write("ERROR","Pressure auto settings dialog failed");}
}
}
