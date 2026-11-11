#pragma once

#include "App.xaml.g.h"
#include <winrt/Microsoft.Windows.AppLifecycle.h>

namespace winrt::DHLS_StrandJack::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();
        winrt::fire_and_forget RedirectToPrimaryAsync(winrt::Microsoft::Windows::AppLifecycle::AppInstance instance,winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments args);

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
        winrt::Microsoft::Windows::AppLifecycle::AppInstance m_instance{nullptr};
        winrt::event_token m_activationToken{};
        bool m_activationSubscribed=false;
    };
}
