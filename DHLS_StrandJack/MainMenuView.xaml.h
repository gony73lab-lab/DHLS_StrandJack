#pragma once
#include "Localization.h"
#include "ControlLogic/EquipmentControlService.h"

#include "MainMenuView.g.h"

namespace winrt::DHLS_StrandJack::implementation
{
	struct MainMenuView : MainMenuViewT<MainMenuView>
	{
		MainMenuView()
		{
			InitializeComponent();
		}

        ~MainMenuView();
        winrt::fire_and_forget DeveloperSimulation_Click(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ConnectionView_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ConnectionView_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void UpdatePlcConnections();
        winrt::Windows::Foundation::IAsyncOperation<bool> ConnectSharedPlcAsync(bool simulation);
        bool m_connectionTransition=false;
        void StopPlcConnections();
        static winrt::fire_and_forget ReleasePlcConnectionsAsync(std::shared_ptr<::DHLS_StrandJack::Control::EquipmentControlService> service);
        void HeaderLogoButton_Click(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
		int32_t MyProperty();
		void MyProperty(int32_t value);

        void CurrentModelName_Loaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            auto text = sender.as<winrt::Microsoft::UI::Xaml::Controls::TextBlock>();
            const auto& name = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
            text.Text(winrt::hstring{ name });
            text.Visibility(name.empty() ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

    // ========================================
    // Button Visual State
    // ========================================

    enum class MenuVisualState
    {
      Normal,
      Hover,
      GeneralClick,
      ExitClick
    };

    void ApplyMenuVisualState(
      winrt::Microsoft::UI::Xaml::Controls::Button const& button,
      MenuVisualState state);

    void ResetAllButtonsToNormal();

    // Mouse
    void MenuButton_PointerEntered(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

    void MenuButton_PointerExited(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

    // General Menu Click
    void MenuButton_Click(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    void StrandPreview_Click(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    // CONTROL/JACK SETTING - shared Password -> each SettingsView modal
    winrt::fire_and_forget ProtectedSetting_Click(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    winrt::Windows::Foundation::IAsyncOperation<bool> VerifySettingsPasswordAsync();

    winrt::Windows::Foundation::IAsyncAction ShowJackSettingsModalAsync(
      winrt::hstring const& title, int width = 0, int height = 0);

    winrt::Windows::Foundation::IAsyncAction ShowControlSettingsModalAsync(
      winrt::hstring const& title, int width = 0, int height = 0);


        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }

        void OnLanguageClick(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::SetEnglish(
                !::DHLS_StrandJack::Services::Localization::IsEnglish());
            UpdatePlcConnections();
        }
    // Version Load
    void VersionText_Loaded(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    // 프로그램 종료 버튼
    winrt::fire_and_forget BtnExit_Click(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    // MainMenuView 배경 더블클릭 -> 메인 Window 최대화
    void MainMenuView_Background_DoubleTapped(
      winrt::Windows::Foundation::IInspectable const& sender,
      winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);


  private:

    // EXIT 다이얼로그가 떠 있는 동안
    // PointerExited가 빨간색을 지우지 못하게 한다.
    std::shared_ptr<::DHLS_StrandJack::Control::EquipmentControlService> m_connectionService;
    winrt::Microsoft::UI::Xaml::DispatcherTimer m_clockTimer{nullptr};
    void RefreshCurrentTime();
    std::wstring m_connectionKey;
    bool m_connectionConfigured = false;
    bool m_connectionAlarmActive=false;
        bool m_connectionConfigurationError = false;
    bool m_connectionStarted = false;
    bool m_exitDialogOpen{ false };
    bool m_developerDialogOpen = false;

    // CONTROL/JACK는 같은 Password 창을 공유하고 각자 SettingsView를 표시함
    bool m_settingsDialogOpen{ false };
    winrt::hstring m_activeProtectedButtonName{};
  };
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
	struct MainMenuView : MainMenuViewT<MainMenuView, implementation::MainMenuView>
	{
	};
}
