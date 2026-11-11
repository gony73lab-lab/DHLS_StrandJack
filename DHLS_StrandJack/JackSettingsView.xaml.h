#pragma once
#include "Localization.h"
#include "SettingsAudit.h"

#include "JackSettingsView.g.h"

namespace winrt::DHLS_StrandJack::implementation
{
    struct JackSettingsView : JackSettingsViewT<JackSettingsView>
    {
        JackSettingsView();

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


        // IDL 기본 생성 속성 (에러 해결용)
        int32_t MyProperty();
        void MyProperty(int32_t value);

        void ShowMotionArrowChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSaveClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSaveAndExitClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCaptureClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HostWindow(winrt::Microsoft::UI::Xaml::Window const& window){m_hostWindow=window;}
        void OnExitClick(winrt::Windows::Foundation::IInspectable const&,winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    private:
        winrt::fire_and_forget ExitWithConfirmationAsync();
        winrt::fire_and_forget CaptureAndReportAsync();
        winrt::Windows::Foundation::IAsyncAction CaptureViewAsync();
        bool m_captureInProgress = false;
        bool m_exitBusy=false;
        std::shared_ptr<::DHLS_StrandJack::Services::SettingsAudit> m_audit;
        winrt::Microsoft::UI::Xaml::Window m_hostWindow{nullptr};
        winrt::fire_and_forget EditSpecAsync(int operation);
        ::DHLS_StrandJack::Models::JackSpec ReadSpec();
        void ReloadSpecs(int selected);
        bool m_editBusy = false;
        bool m_rangeAlarmActive = false;
        bool m_loadingSpec = false;
        void UpdateSelectedSpec();
        void RefreshArrowPreview();
        void RefreshCalculatedValues();
        bool m_closeAfterSave = false;
        int m_selectedSpecIndex = -1;
        winrt::Microsoft::UI::Xaml::Controls::ComboBox::SelectionChanged_revoker m_specSelectionChanged;

    };
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
    struct JackSettingsView : JackSettingsViewT<JackSettingsView, implementation::JackSettingsView>
    {
    };
}
