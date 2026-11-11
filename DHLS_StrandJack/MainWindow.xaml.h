#pragma once
#include "Localization.h"

#include "MainWindow.g.h"
#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::DHLS_StrandJack::implementation
{
  struct MainWindow : MainWindowT<MainWindow>
  {
    MainWindow();
    ~MainWindow();
    winrt::Microsoft::UI::Windowing::AppWindow m_sequenceClosingPublisher{nullptr};
    winrt::event_token m_sequenceClosingToken{};
    bool m_sequenceClosingSubscribed=false;
    void RevokeSequenceClosing() noexcept;
    bool m_closeApproved=false,m_closePending=false;
    bool m_keepMaximized=false;
    winrt::Microsoft::UI::Xaml::DispatcherTimer m_plcStatusTimer{nullptr};
    void UpdateAppPlcStatus();
    winrt::fire_and_forget CloseAfterSequenceAsync();

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


    // x:Bind 타겟 메서드 (UI 컨트롤 파라미터 종속성 제거)
    void OnNewModelClicked();
    void OnExistingModelClicked();
    void CallRestoreWindowSize(int width = 1920, int height = 1080);
    //void RemoveSysMenu(HWND hwnd = nullptr);
    // ----------------------------------------------------
    // [추가] MainMenuView에서 접근할 수 있도록 static으로 선언
    // ----------------------------------------------------
    static void RemoveSysMenu(HWND hwnd = nullptr, bool caption = false);
    static HWND GetHwnd();
    static void Minimize();
    static bool CanNavigateFromCurrentView();
    static void NavigateToFullScreen(winrt::Windows::Foundation::IInspectable const& content);
    static void NavigateToView(winrt::Windows::Foundation::IInspectable const& content, int width = 1920, int height = 1080);
  };
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
  struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
  {
  };
}