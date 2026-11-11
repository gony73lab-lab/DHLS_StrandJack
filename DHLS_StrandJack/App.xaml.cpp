#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "ConfigManager.h"
#include "AuditLog.h"
#include "ControlLogic/DeveloperSimulation.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::DHLS_StrandJack::implementation
{
    /// <summary>
    /// Initializes the singleton application object.  This is the first line of authored code
    /// executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

#if defined _DEBUG
        // The application owns the publisher; these stateless handlers live for the application lifetime.
        DebugSettings().IsBindingTracingEnabled(true);
        DebugSettings().BindingFailed([](IInspectable const&, BindingFailedEventArgs const& e)
        {
            ::OutputDebugStringW((std::wstring{L"[XAML BindingFailed] "} + e.Message().c_str() + L"\n").c_str());
        });
#endif

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            wchar_t code[80]{};
            swprintf_s(code, L"[XAML UnhandledException] HRESULT=0x%08X\n", static_cast<unsigned>(e.Exception().value));
            ::OutputDebugStringW(code);
            ::OutputDebugStringW((std::wstring{L"[XAML UnhandledException] "} + e.Message().c_str() + L"\n").c_str());
            if (IsDebuggerPresent())
            {
                __debugbreak();
            }
        });
#endif
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="e">Details about the launch request and process.</param>
    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        // Register before creating a window or any PLC session. Secondary processes only redirect.
        using winrt::Microsoft::Windows::AppLifecycle::AppInstance;
        try{
            m_instance=AppInstance::FindOrRegisterForKey(L"DHLS_StrandJack.SingleInstance");
            if(!m_instance.IsCurrent()){
                RedirectToPrimaryAsync(m_instance,AppInstance::GetCurrent().GetActivatedEventArgs());return;
            }
            auto queue=winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            m_activationToken=m_instance.Activated([weak=get_weak(),queue](auto const&,auto const&){
                queue.TryEnqueue([weak]{if(auto self=weak.get();self&&self->window){
                    if(auto native=self->window.try_as<::IWindowNative>()){HWND hwnd{};if(SUCCEEDED(native->get_WindowHandle(&hwnd))){if(::IsIconic(hwnd))::ShowWindow(hwnd,SW_RESTORE);::SetForegroundWindow(hwnd);}}
                    try{self->window.Activate();}catch(winrt::hresult_error const&){}
                }});
            });
            m_activationSubscribed=true;
        }catch(winrt::hresult_error const& error){::OutputDebugStringW(error.message().c_str());Exit();return;}
        ::DHLS_StrandJack::Services::AuditLog::Instance().Configure(::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory().parent_path()/L"Log");
        ::DHLS_StrandJack::Services::AuditLog::Write("APP_START","DHLS_StrandJack");
        ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.store(true);
        window = make<MainWindow>();
        window.Activate();
    }
    App::~App(){if(m_activationSubscribed&&m_instance){try{m_instance.Activated(m_activationToken);}catch(...){}}}
    winrt::fire_and_forget App::RedirectToPrimaryAsync(winrt::Microsoft::Windows::AppLifecycle::AppInstance instance,winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments args){
        auto lifetime=get_strong();auto ui=winrt::apartment_context();
        try{co_await instance.RedirectActivationToAsync(args);}catch(winrt::hresult_error const& error){::OutputDebugStringW(error.message().c_str());}
        try{co_await ui;}catch(...){co_return;}
        Exit();
    }

}
