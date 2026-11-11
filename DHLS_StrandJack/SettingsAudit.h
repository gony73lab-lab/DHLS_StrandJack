#pragma once
#include "AuditLog.h"
#include "ConfigManager.h"
#include <map>
#include <memory>
#include <functional>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace DHLS_StrandJack::Services {
// Owned by the View. Timer and input delegates hold weak state only.
class SettingsAudit:public std::enable_shared_from_this<SettingsAudit> {
    using Element=winrt::Microsoft::UI::Xaml::FrameworkElement;
    using Values=std::map<std::string,std::string>;
    winrt::weak_ref<Element> view_;
    std::string screen_;
    Values saved_,last_;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
    std::map<std::string,winrt::weak_ref<Element>> subscribed_;
    void Walk(Element const& element,std::string path,Values& values) {
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
        const auto name=winrt::to_string(element.Name());
        if(!name.empty())path+="/"+name;
        std::string value;bool input=true;
        if(auto n=element.try_as<NumberBox>())value=std::to_string(n.Value());
        else if(auto t=element.try_as<TextBox>())value=winrt::to_string(t.Text());
        else if(auto c=element.try_as<ComboBox>())value=std::to_string(c.SelectedIndex());
        else if(auto toggle=element.try_as<ToggleButton>()){auto v=toggle.IsChecked();value=v?(v.Value()?"true":"false"):"null";}
        else input=false;
        if(input) {
            if(name!="CmbJackType")values[path]=value;
            auto previous=subscribed_.find(path);
            if(previous==subscribed_.end()||previous->second.get()!=element) {
                subscribed_[path]=winrt::make_weak(element);auto weak=weak_from_this();
                auto changed=[weak](auto const&,auto const&){if(auto self=weak.lock())self->Capture();};
                if(auto n=element.try_as<NumberBox>())n.ValueChanged(changed);
                else if(auto t=element.try_as<TextBox>())t.TextChanged(changed);
                else if(auto c=element.try_as<ComboBox>())c.SelectionChanged(changed);
                else if(auto toggle=element.try_as<ToggleButton>()){toggle.Checked(changed);toggle.Unchecked(changed);toggle.Indeterminate(changed);}
            }
            return; // Skip editor template internals and passwords.
        }
        if(element.try_as<PasswordBox>())return;
        using winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper;
        for(int i=0;i<VisualTreeHelper::GetChildrenCount(element);++i)
            if(auto child=VisualTreeHelper::GetChild(element,i).try_as<Element>())Walk(child,path+"/"+std::to_string(i),values);
    }
    Values Read(){Values result;if(auto view=view_.get()){
        Walk(view,screen_,result);
        for(auto key:{L"CylinderColorBrush",L"PistonColorBrush"})
            if(auto brush=view.Resources().TryLookup(winrt::box_value(key)).try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>()){
                auto c=brush.Color();result[winrt::to_string(key)]=std::to_string(c.A)+","+std::to_string(c.R)+","+std::to_string(c.G)+","+std::to_string(c.B);}
    }return result;}
public:
    static std::shared_ptr<SettingsAudit> Attach(Element const& view,std::string screen){
        auto self=std::make_shared<SettingsAudit>();self->view_=winrt::make_weak(view);self->screen_=std::move(screen);
        self->MarkSaved();self->timer_=view.DispatcherQueue().CreateTimer();self->timer_.Interval(std::chrono::milliseconds(200));
        self->timer_.Tick([weak=std::weak_ptr<SettingsAudit>(self)](auto const&,auto const&){if(auto s=weak.lock())s->Capture();});self->timer_.Start();
        AuditLog::Write("VIEW_OPEN",self->screen_);return self;
    }
    ~SettingsAudit(){if(timer_)timer_.Stop();}
    void Capture(){auto current=Read();for(auto const& item:current){auto old=last_.find(item.first);
        if(old==last_.end()&&saved_.find(item.first)==saved_.end())saved_[item.first]=item.second;
        else if(old!=last_.end()&&old->second!=item.second)AuditLog::Write("DATA_CHANGE",item.first+" old="+old->second+" new="+item.second);}
        last_=std::move(current);}
    void MarkSaved(){saved_=last_=Read();}
    bool Dirty(){
        Capture();
        // Virtualized ListView rows can appear after Loaded without an edit.
        // Compare realized inputs; ControlSettings also compares the entire native config.
        for(auto const& item:last_){auto original=saved_.find(item.first);
            if(original!=saved_.end()&&original->second!=item.second)return true;}
        return false;
    }
};
inline winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmDiscardAsync(
    winrt::Microsoft::UI::Xaml::XamlRoot root,bool english) {
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    if(!root)co_return false;
    ContentDialog dialog;dialog.XamlRoot(root);
    dialog.Title(winrt::box_value(english?L"Unsaved changes":L"저장하지 않은 변경 사항"));
    StackPanel content;content.Spacing(12);
    FontIcon icon;icon.Glyph(L"\xE7BA");icon.FontSize(28);content.Children().Append(icon);
    TextBlock text;text.Text(english?L"Changes have not been saved. Discard changes and exit?":L"변경한 내용이 저장되지 않았습니다. 변경 사항을 취소하고 나가시겠습니까?");
    text.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);content.Children().Append(text);dialog.Content(content);
    dialog.PrimaryButtonText(english?L"Discard and exit":L"변경 취소 후 나가기");dialog.CloseButtonText(english?L"Continue editing":L"계속 편집");
    dialog.DefaultButton(ContentDialogButton::Close);
    co_return co_await dialog.ShowAsync()==ContentDialogResult::Primary;
}
}
