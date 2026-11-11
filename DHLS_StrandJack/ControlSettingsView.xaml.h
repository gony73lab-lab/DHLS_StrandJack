#pragma once
#include "Localization.h"
#include "SettingsAudit.h"

#include "ControlSettingsView.g.h"

namespace winrt::DHLS_StrandJack::implementation
{
	struct ControlSettingsView : ControlSettingsViewT<ControlSettingsView>
	{
		ControlSettingsView();

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


		int32_t MyProperty();
		void MyProperty(int32_t value);

		void OnPressureAutoSettingsClick(winrt::Windows::Foundation::IInspectable const&,winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDistanceDecelerationClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        // --- Footer button handlers ---
		void OnSaveClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OnCaptureClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OnExitClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OnJackLayoutClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

		// --- ComboBox SelectionChanged handlers ---
		void OnPlcCountChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OnSensorCountChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

		// --- Loaded event handler ---
		void OnLoaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

		// 별도 Window로 열린 경우 Window 참조 설정 (ShowControlSettingsModalAsync에서 호출)
		void HostWindow(winrt::Microsoft::UI::Xaml::Window const& window) { m_hostWindow = window; }

	private:
        bool m_exitBusy=false;
        bool m_distanceDialogOpen=false;
        winrt::fire_and_forget ShowPressureAutoSettingsAsync();
        winrt::fire_and_forget ShowDistanceDecelerationAsync();
        bool m_soundPickerBusy=false;
        winrt::fire_and_forget PickSoundAsync(bool warning);
        std::shared_ptr<::DHLS_StrandJack::Services::SettingsAudit> m_audit;
        ::DHLS_StrandJack::Models::SystemConfig m_originalConfig;
        std::string m_originalJson;
        winrt::fire_and_forget ExitWithConfirmationAsync();
        void RefreshUnitInputs();
        void RefreshScaleWarning();
        bool m_updatingUnitInputs=false;
        bool m_isLoaded = false;
        bool m_captureInProgress = false;
		int m_dashboardPage = 0;
		int m_selectedDashboardJack = -1;
		static constexpr int DashboardPageSize = 8;
		winrt::Microsoft::UI::Xaml::Window m_hostWindow{ nullptr };

		// --- Dynamic UI population ---
		void UpdateDynamicIpFields(int plcCount);
		void UpdateJackTable(int plcCount, int sensorCount);
		void PopulateDashboardJackTable();
		void PullDashboardFields();
		bool ValidateDashboardIps();

		// Capture: 현재 View를 이미지로 저장
		winrt::Windows::Foundation::IAsyncAction CaptureViewAsync();
        winrt::fire_and_forget CaptureAndReportAsync();
	};
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
	struct ControlSettingsView :
		ControlSettingsViewT<ControlSettingsView, implementation::ControlSettingsView>
	{
	};
}
