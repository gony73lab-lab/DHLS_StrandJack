#include "pch.h"
#include "ControlLogic/EquipmentSession.h"
#include "WarningSound.h"
#include "ControlLogic/DeveloperSimulation.h"
#include "Localization.h"
#include "ApplicationVersion.h"
#include "MainMenuView.xaml.h"
#include "JackSettingsView.xaml.h"
#include "ControlSettingsView.xaml.h"
#include "ModelManageView.xaml.h"
#include "MainDashboardView.xaml.h"
#if __has_include("MainMenuView.g.cpp")
#include "MainMenuView.g.cpp"
#endif

#include <windows.h>
#include <winver.h>
#pragma comment(lib, "version.lib")
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.ApplicationModel.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <chrono>
#include <ctime>

// 1. MainWindow의 C++ 헤더를 인클루드합니다.
#include "MainWindow.xaml.h"
#include "ConfigManager.h"
#include "RoundedWindow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;

// [Hard Constraint] Windows::UI (UWP) 네임스페이스 배제 및 Microsoft::UI::Colors 네임스페이스 선언 제거됨

namespace winrt::DHLS_StrandJack::implementation
{
    winrt::fire_and_forget MainMenuView::DeveloperSimulation_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_developerDialogOpen || m_settingsDialogOpen || m_exitDialogOpen || m_connectionTransition) co_return;
        auto lifetime = get_strong();
        m_developerDialogOpen = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{m_developerDialogOpen};
        using namespace ::DHLS_StrandJack::Control;
        const bool en = ::DHLS_StrandJack::Services::Localization::IsEnglish();
        try
        {
            if (!XamlRoot()) co_return;
            if (!DeveloperSimulationEnabled.load())
            {
                PasswordBox password;
                password.PlaceholderText(en ? L"Developer password" : L"개발자 비밀번호");
                ContentDialog verify;
                verify.XamlRoot(XamlRoot());
                verify.Title(box_value(en ? L"Developer simulation" : L"개발자 시뮬레이션"));
                verify.Content(password);
                verify.PrimaryButtonText(en ? L"Enable" : L"활성화");
                verify.CloseButtonText(en ? L"Cancel" : L"취소");
                if (co_await verify.ShowAsync() != ContentDialogResult::Primary || !IsLoaded()) co_return;
                const bool valid = password.Password() == L"32990966";
                password.Password(L"");
                if (!valid)
                {
                    ::DHLS_StrandJack::Services::AuditLog::Write("SIM_MODE_DENIED","Developer authentication failed");
                    ContentDialog error;
                    error.XamlRoot(XamlRoot()); error.Title(box_value(en ? L"Warning" : L"경고"));
                    error.Content(box_value(en ? L"Incorrect password." : L"비밀번호가 올바르지 않습니다."));
                    error.CloseButtonText(en ? L"OK" : L"확인");
                    co_await error.ShowAsync(); co_return;
                }
            }
            if (!IsLoaded()) co_return;
            const bool enabled = !DeveloperSimulationEnabled.load();
            const bool switched = co_await ConnectSharedPlcAsync(enabled);
            if (!IsLoaded() || !XamlRoot()) co_return;
            ::DHLS_StrandJack::Services::AuditLog::Write(switched?"SIM_MODE":"SIM_MODE_DENIED",
                switched?(enabled?"Simulation enabled":"Simulation disabled; PLC connection started"):"Equipment session switch failed");
            ContentDialog notice; notice.XamlRoot(XamlRoot());
            notice.Title(box_value(en ? L"Developer simulation" : L"개발자 시뮬레이션"));
            notice.Content(box_value(!switched
                ? (en ? L"Could not change the equipment connection. Check the PLC settings and stop or reset any active sequence."
                      : L"장비 연결을 전환하지 못했습니다. PLC 설정과 운전 정지·RESET 상태를 확인하세요.")
                : enabled
                ? (en ? L"Simulation enabled. The dashboard uses virtual equipment." : L"시뮬레이션 모드입니다. 대시보드는 모의 장비를 사용합니다.")
                : (en ? L"Simulation disabled. Connecting to the PLC using this model's settings." : L"시뮬레이션을 해제했습니다. 현재 모델 설정으로 PLC 연결을 시작했습니다.")));
            notice.CloseButtonText(en ? L"OK" : L"확인");
            co_await notice.ShowAsync();
        }
        catch (winrt::hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); }
    }

    MainMenuView::~MainMenuView() { StopPlcConnections(); }

    winrt::fire_and_forget MainMenuView::ReleasePlcConnectionsAsync(std::shared_ptr<::DHLS_StrandJack::Control::EquipmentControlService> service)
    { service.reset();co_return; }
    winrt::Windows::Foundation::IAsyncOperation<bool> MainMenuView::ConnectSharedPlcAsync(bool simulation)
    {
        using namespace ::DHLS_StrandJack::Control;
        auto lifetime = get_strong();
        ServiceOptions options;
        std::wstring model;
        try {
            const auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
            options = EquipmentControlService::FromModel(config);
            options.simulation = simulation;
            model = config.ModelInfo.ModelName;
            if (!simulation && std::none_of(options.plcs.begin(), options.plcs.end(),
                [](auto const& plc) { return !plc.ip.empty(); })) co_return false;
        } catch (...) { co_return false; }

        auto ui = winrt::apartment_context();
        m_connectionTransition = true;
        UpdatePlcConnections();
        auto& session = EquipmentSession::Shared();
        const auto ticket = session.Request();
        co_await winrt::resume_background();
        std::shared_ptr<EquipmentControlService> service;
        try {
            const auto current = session.Current();
            if (!current || !current->Latest().sequenceNavigationLocked) {
                const bool previous = DeveloperSimulationEnabled.exchange(simulation);
                try { service = session.Connect(std::move(options), model, true, {}, ticket); }
                catch (...) {}
                if (!service) DeveloperSimulationEnabled.store(previous);
            }
        } catch (...) {}
        co_await ui;
        m_connectionTransition = false;
        if (IsLoaded()) {
            if (service) m_connectionService = service;
            UpdatePlcConnections();
        }
        co_return !!service;
    }

    void MainMenuView::StopPlcConnections()
    {
        if (m_clockTimer) { m_clockTimer.Stop(); m_clockTimer = nullptr; }
        if (auto service = std::exchange(m_connectionService, nullptr))
            ReleasePlcConnectionsAsync(std::move(service));
        m_connectionStarted = false;
    }

    void MainMenuView::ConnectionView_Loaded(IInspectable const&, RoutedEventArgs const&)
    {
        m_connectionStarted = false;
        try {
            using namespace ::DHLS_StrandJack::Control;
            const auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
            auto options = EquipmentControlService::FromModel(config);
            options.simulation = DeveloperSimulationEnabled.load();
            m_connectionService = EquipmentSession::Shared().CurrentFor(options, config.ModelInfo.ModelName);
        } catch (...) {}
        PlcConnectionMessage().Opacity(1.0);
        UpdatePlcConnections();
        RefreshCurrentTime();
        if (!m_clockTimer)
        {
            m_clockTimer = DispatcherTimer{};
            m_clockTimer.Interval(std::chrono::seconds(1));
            m_clockTimer.Tick([weak = get_weak()](auto const&, auto const&)
            {
                if (auto self = weak.get()) {
                    self->RefreshCurrentTime();
                    if (self->m_connectionService) self->UpdatePlcConnections();
                }
            });
            m_clockTimer.Start();
        }
    }

    void MainMenuView::RefreshCurrentTime()
    {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        if (localtime_s(&local, &now) != 0) return;
        std::wostringstream text;
        text << std::put_time(&local, L"%H:%M:%S");
        MainMenuCurrentTime().Text(text.str());
    }

    void MainMenuView::ConnectionView_Unloaded(IInspectable const&, RoutedEventArgs const&)
    { StopPlcConnections(); }

    void MainMenuView::UpdatePlcConnections()
    {
        PlcConnectionBanner().Visibility(Visibility::Visible);
        PlcConnectionBanner().Background(SolidColorBrush{winrt::Windows::UI::Color{255,71,85,105}});
        const bool simulation = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
        const bool english = ::DHLS_StrandJack::Services::Localization::IsEnglish();
        if (m_connectionTransition) {
            PlcConnectionMessage().Text(english ? L"Switching equipment connection…" : L"장비 연결 전환 중…");
        } else if (simulation) {
            PlcConnectionMessage().Text(english ? L"Simulation · virtual equipment" : L"시뮬레이션 · 모의 장비");
        } else if (m_connectionService) {
            const auto snapshot = m_connectionService->Latest();
            const bool ready = snapshot.connected && !snapshot.plcs.empty() &&
                std::all_of(snapshot.plcs.begin(), snapshot.plcs.end(), [](auto const& plc){ return plc.connected && plc.readOk; });
            PlcConnectionMessage().Text(ready
                ? (english ? L"PLC connected" : L"PLC 연결 완료")
                : (english ? L"Waiting for PLC response" : L"PLC 응답 대기"));
        } else {
            PlcConnectionMessage().Text(english ? L"PLC disconnected · connect from the dashboard" : L"PLC 미연결 · 대시보드에서 연결");
        }
    }

    void MainMenuView::HeaderLogoButton_Click(winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        MainWindow::Minimize();
    }

	int32_t MainMenuView::MyProperty()
	{
		throw hresult_not_implemented();
	}

	void MainMenuView::MyProperty(int32_t /* value */)
	{
		throw hresult_not_implemented();
	}


	// ========================================
	// MainMenuView Background Double Click
	// 배경 더블클릭 시 메인 Window 최대화
	// ========================================
	void MainMenuView::MainMenuView_Background_DoubleTapped(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e)
	{
		// 이 이벤트는 MainMenuView 배경 전용 Border에서만 발생한다.
		// Button / NumberBox 등 실제 Control의 더블클릭은 처리하지 않는다.
		e.Handled(true);

		// MainMenuView가 속한 메인 Window의 HWND를 안전하게 획득한다.
		HWND hwnd = MainWindow::GetHwnd();

		if (hwnd == nullptr ||
			!::IsWindow(hwnd))
		{
			return;
		}

		// 이미 최대화 상태이면 그대로 둔다.
		if (!::IsZoomed(hwnd))
		{
			::ShowWindow(
				hwnd,
				SW_MAXIMIZE);
		}
	}

	void MainMenuView::ApplyMenuVisualState(
		Button const& button,
		MenuVisualState state)
	{
		switch (state)
		{
		case MenuVisualState::Normal:

			// 흰 배경 + 검정 아이콘
			button.Background(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::White()));

			button.Foreground(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Black()));

			break;


		case MenuVisualState::Hover:

			// 검정 배경 + 흰 아이콘
			button.Background(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Black()));

			button.Foreground(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::White()));

			break;


		case MenuVisualState::GeneralClick:

			// 일반 버튼
			// 노랑 배경 + 검정 아이콘
			button.Background(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Yellow()));

			button.Foreground(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Black()));

			break;


		case MenuVisualState::ExitClick:

			// EXIT
			// 빨강 배경 + 검정 아이콘
			button.Background(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Red()));

			button.Foreground(
				SolidColorBrush(
					winrt::Microsoft::UI::Colors::Black()));

			break;
		}
	}

	void MainMenuView::ResetAllButtonsToNormal()
	{
		ApplyMenuVisualState(
			BtnStroke(),
			MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnAngle(),
			MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnWeighing(),
			MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnCalib(),
			MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnGeneral(),
			MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnJack(),
			MenuVisualState::Normal);
		ApplyMenuVisualState(BtnChangeModel(), MenuVisualState::Normal);

		ApplyMenuVisualState(
			BtnExit(),
			MenuVisualState::Normal);
	}

	// ========================================
	// Mouse Hover
	// ========================================
	void MainMenuView::MenuButton_PointerEntered(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& /*e*/)
	{
		if (auto btn = sender.try_as<Button>())
		{
			// EXIT 다이얼로그가 이미 떠 있다면
			// 빨간 클릭 상태를 유지한다.
			if (btn.Name() == L"BtnExit" &&
				m_exitDialogOpen)
			{
				ApplyMenuVisualState(
					btn,
					MenuVisualState::ExitClick);

				return;
			}

			// CONTROL/JACK password 또는 settings modal이 열려 있으면 클릭 상태 유지
			if (m_settingsDialogOpen &&
				btn.Name() == m_activeProtectedButtonName)
			{
				ApplyMenuVisualState(
					btn,
					MenuVisualState::GeneralClick);

				return;
			}

			// 일반 Mouse Enter
			ApplyMenuVisualState(
				btn,
				MenuVisualState::Hover);
		}
	}


	// ========================================
	// Mouse Exit
	// ========================================
	void MainMenuView::MenuButton_PointerExited(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& /*e*/)
	{
		if (auto btn = sender.try_as<Button>())
		{
			// EXIT 종료 확인창이 열려 있는 동안에는
			// Pointer Exit가 발생해도 빨간 상태 유지
			if (btn.Name() == L"BtnExit" &&
				m_exitDialogOpen)
			{
				ApplyMenuVisualState(
					btn,
					MenuVisualState::ExitClick);

				return;
			}

			// CONTROL/JACK password 또는 settings modal이 열려 있으면 클릭 상태 유지
			if (m_settingsDialogOpen &&
				btn.Name() == m_activeProtectedButtonName)
			{
				ApplyMenuVisualState(
					btn,
					MenuVisualState::GeneralClick);

				return;
			}

			// 일반 Mouse Exit
			// 흰 배경 + 검정 아이콘
			ApplyMenuVisualState(
				btn,
				MenuVisualState::Normal);
		}
	}


	// ========================================
	// General Menu Click
	// ========================================
    void MainMenuView::StrandPreview_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_connectionTransition || m_developerDialogOpen) return;
        auto button = sender.try_as<Button>();
        if (!button) return;
        const auto mode = unbox_value<winrt::hstring>(button.Tag());
        auto dashboardView = winrt::make_self<MainDashboardView>();
        using Language = ::DHLS_StrandJack::Services::Localization;
        const auto isMode = [&](std::wstring_view korean) { return std::wstring_view{mode} == korean || mode == Language::Text(korean); };
        const int menuMode = isMode(L"하강 운전") ? 1 : isMode(L"수동 운전") ? 2
            : isMode(L"텐셔닝 작업") ? 3 : isMode(L"교정") || mode == L"Calibration" ? 4 : 0;
        constexpr const char* modes[]{"Lifting","Lowering","Manual","Pretensioning","Calibration"};
        ::DHLS_StrandJack::Services::AuditLog::Write("DASHBOARD_MODE",modes[menuMode]);
        dashboardView->SetStartupMenuMode(menuMode);
        MainWindow::NavigateToFullScreen(dashboardView.as<winrt::DHLS_StrandJack::MainDashboardView>());
    }

    void MainMenuView::MenuButton_Click(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		if (m_connectionTransition || m_developerDialogOpen) return;
		auto lifetime = get_strong();
		if (auto btn = sender.try_as<Button>())
		{
			// 노랑 배경 + 검정 아이콘
			ApplyMenuVisualState(
				btn,
				MenuVisualState::GeneralClick);
			if (btn.Name() == L"BtnStroke" || btn.Name() == L"BtnWeighing" || btn.Name() == L"BtnCalib")
            {
                auto dashboard = winrt::make_self<MainDashboardView>();
                dashboard->SetOperatingMode(btn.Name() == L"BtnWeighing" ? 1 : (btn.Name() == L"BtnCalib" ? 2 : 0));
                MainWindow::NavigateToFullScreen(dashboard.as<winrt::DHLS_StrandJack::MainDashboardView>());
            }
			else if (btn.Name() == L"BtnChangeModel")
				MainWindow::NavigateToView(winrt::DHLS_StrandJack::ModelManageView{}, 480, 420);
		}
	}




	// ========================================
	// CONTROL / JACK shared password dialog
	// ========================================
	winrt::Windows::Foundation::IAsyncOperation<bool>
		MainMenuView::VerifySettingsPasswordAsync()
	{
		// 설정 비밀번호는 ConfigManager에서 로드 (설정 파일 기반)
		auto& configMgr = ::DHLS_StrandJack::Services::ConfigManager::Instance();
		const std::wstring kSettingsPassword = configMgr.GetSettingsPassword();

		TextBlock guideText;
		guideText.Text(::DHLS_StrandJack::Services::Localization::Text(L"설정 화면 접근 비밀번호를 입력하세요."));

		PasswordBox passwordBox;
		passwordBox.PlaceholderText(::DHLS_StrandJack::Services::Localization::Text(L"Password"));
		passwordBox.PasswordRevealMode(PasswordRevealMode::Peek);

		TextBlock errorText;
		errorText.Text(L"");
		errorText.Foreground(
			SolidColorBrush(
				winrt::Microsoft::UI::Colors::Red()));
		errorText.Visibility(Visibility::Collapsed);

		StackPanel passwordPanel;
		passwordPanel.Spacing(8);
		passwordPanel.Children().Append(guideText);
		passwordPanel.Children().Append(passwordBox);
		passwordPanel.Children().Append(errorText);

		ContentDialog passwordDialog;
		passwordDialog.XamlRoot(this->XamlRoot());
		StackPanel passwordTitle;
		passwordTitle.Orientation(Orientation::Horizontal);
		passwordTitle.Spacing(8);
		FontIcon infoIcon;
		infoIcon.Glyph(L"\uE946");
		infoIcon.FontSize(24);
		infoIcon.Foreground(SolidColorBrush(Microsoft::UI::Colors::DodgerBlue()));
		infoIcon.VerticalAlignment(VerticalAlignment::Center);
		TextBlock passwordLabel;
		passwordLabel.Text(::DHLS_StrandJack::Services::Localization::Text(L"PASSWORD"));
		passwordLabel.VerticalAlignment(VerticalAlignment::Center);
		passwordTitle.Children().Append(infoIcon);
		passwordTitle.Children().Append(passwordLabel);
		passwordDialog.Title(passwordTitle);
		passwordDialog.Content(passwordPanel);
		passwordDialog.PrimaryButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
                passwordDialog.PrimaryButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogPrimaryAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
		passwordDialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"취소"));
                passwordDialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogCancelIconStyle")).as<Microsoft::UI::Xaml::Style>());
		passwordDialog.DefaultButton(ContentDialogButton::Primary);

		bool passwordAccepted = false;

		// 비밀번호가 틀려도 같은 ContentDialog를 닫지 않고 재입력하게 함.
		auto passwordClickRevoker =
			passwordDialog.PrimaryButtonClick(
				winrt::auto_revoke,
				[&](ContentDialog const&,
					ContentDialogButtonClickEventArgs const& args)
				{
					if (passwordBox.Password() == winrt::hstring(kSettingsPassword))
					{
						passwordAccepted = true;
						return;
					}

					args.Cancel(true);
					errorText.Text(::DHLS_StrandJack::Services::Localization::Text(L"비밀번호가 올바르지 않습니다."));
					errorText.Visibility(Visibility::Visible);
					passwordBox.Password(L"");
					passwordBox.Focus(FocusState::Programmatic);
				});

		auto result = co_await passwordDialog.ShowAsync();

		co_return
			result == ContentDialogResult::Primary &&
			passwordAccepted;
	}


	// Shared settings-window implementation. All XAML setup and cleanup run on the UI thread.
	namespace
	{
		using CloseBindings = std::vector<Button::Click_revoker>;
		using ViewFactory = std::function<FrameworkElement()>;
		using BindView = std::function<void(FrameworkElement const&, Window const&, CloseBindings&)>;
		using ReleaseView = std::function<void(FrameworkElement const&)>;

		struct ModalCleanup
		{
			std::function<void()> action;
			~ModalCleanup() noexcept
			{
				try { action(); }
				catch (...) { ::OutputDebugStringW(L"Settings modal cleanup failed.\n"); }
			}
		};

		// No UI objects are touched by the thread-pool continuation.
		// Awaiting this IAsyncAction restores the caller's UI apartment.
		winrt::Windows::Foundation::IAsyncAction WaitForModalSignalAsync(
			std::shared_ptr<winrt::handle> signal)
		{
			co_await winrt::resume_on_signal(signal->get());
		}

		void BindModalCloseButtons(FrameworkElement const& view, Window const& window,
			CloseBindings& bindings)
		{
			for (auto name : { L"BtnClose", L"BtnExit" })
			{
				if (auto button = view.FindName(name).try_as<Button>())
				{
					bindings.push_back(button.Click(winrt::auto_revoke,
						[weakWindow = winrt::make_weak(window)](auto const&, auto const&)
						{
							if (auto target = weakWindow.get()) target.Close();
						}));
				}
			}
		}

		void PositionSettingsWindow(HWND window, HWND owner, int width, int height)
		{
			MONITORINFO monitor{ sizeof(MONITORINFO) };
			winrt::check_bool(::GetMonitorInfoW(
				::MonitorFromWindow(owner ? owner : window, MONITOR_DEFAULTTONEAREST), &monitor));

			const auto& work = monitor.rcWork;
			const int availableWidth = (std::max)(1L, work.right - work.left - 16);
			const int availableHeight = (std::max)(1L, work.bottom - work.top - 16);
			const UINT dpi = ::GetDpiForWindow(owner ? owner : window);
			double scale = static_cast<double>(dpi ? dpi : 96) / 96.0;
			scale = (std::min)(scale, static_cast<double>(availableWidth) / width);
			scale = (std::min)(scale, static_cast<double>(availableHeight) / height);

			RECT bounds{ 0, 0, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale) };
			winrt::check_bool(::AdjustWindowRectExForDpi(&bounds,
				static_cast<DWORD>(::GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
				static_cast<DWORD>(::GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi ? dpi : 96));

			const int outerWidth = (std::min)(availableWidth, static_cast<int>(bounds.right - bounds.left));
			const int outerHeight = (std::min)(availableHeight, static_cast<int>(bounds.bottom - bounds.top));
			winrt::check_bool(::SetWindowPos(window, nullptr,
				work.left + (work.right - work.left - outerWidth) / 2,
				work.top + (work.bottom - work.top - outerHeight) / 2,
				outerWidth, outerHeight, SWP_NOZORDER | SWP_NOACTIVATE));
		}

		winrt::Windows::Foundation::IAsyncAction ShowSettingsModalAsync(
			winrt::hstring title, int width, int height,
			ViewFactory createView, BindView bindView, ReleaseView releaseView)
		{
			auto cancellation = co_await winrt::get_cancellation_token();
			if (cancellation()) co_return;

			auto view = createView();
			Window window;
			window.Title(::DHLS_StrandJack::Services::Localization::Text(title));
			const HWND owner = MainWindow::GetHwnd();
			HWND hwnd = nullptr;
			winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&hwnd));

			auto signal = std::make_shared<winrt::handle>(
				::CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!*signal) winrt::throw_last_error();

			auto closed = std::make_shared<bool>(false); // Read/write only on the UI thread.
			bool ownerDisabled = false;
			CloseBindings closeBindings;
			Window::Closed_revoker closedRevoker;
			Window::SizeChanged_revoker roundingRevoker;

			auto restoreOwner = [&]() noexcept
			{
				if (ownerDisabled && owner && ::IsWindow(owner))
				{
					::EnableWindow(owner, TRUE);
					::SetForegroundWindow(owner);
				}
				ownerDisabled = false;
			};

			ModalCleanup cleanup{ [&]()
			{
				// Revoke delegates before releasing the content or host window.
				closeBindings.clear();
				roundingRevoker.revoke();
				try { releaseView(view); }
				catch (...) { ::OutputDebugStringW(L"Settings host release failed.\n"); }
				if (!*closed)
				{
					try { window.Content(nullptr); }
					catch (...) { ::OutputDebugStringW(L"Settings content release failed.\n"); }
					try { window.Close(); }
					catch (...) { ::OutputDebugStringW(L"Settings window close failed.\n"); }
				}
				closedRevoker.revoke();
				restoreOwner();
			} };

			closedRevoker = window.Closed(winrt::auto_revoke,
				[closed, &restoreOwner, signal](auto const&, auto const&)
				{
					*closed = true;
					restoreOwner();
					::SetEvent(signal->get());
				});

			Grid root;
			root.Background(SolidColorBrush(Microsoft::UI::Colors::White()));
			Viewbox scaleHost;
			scaleHost.Stretch(Stretch::Uniform);
			scaleHost.StretchDirection(StretchDirection::Both);
			scaleHost.HorizontalAlignment(HorizontalAlignment::Stretch);
			scaleHost.VerticalAlignment(VerticalAlignment::Stretch);
			scaleHost.Child(view);
			root.Children().Append(scaleHost);
			window.Content(root);

			bindView(view, window, closeBindings);
			if (owner && ::IsWindow(owner))
				::SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
			MainWindow::RemoveSysMenu(hwnd);
			roundingRevoker = window.SizeChanged(winrt::auto_revoke,
				[hwnd](auto const&, auto const&)
				{
					::DHLS_StrandJack::Common::UpdateRoundedWindowRegion(hwnd);
				});
			PositionSettingsWindow(hwnd, owner, width, height);
			::DHLS_StrandJack::Common::UpdateRoundedWindowRegion(hwnd);

			// Cancellation may arrive on any thread. Only enqueue UI work here.
			auto dispatcher = window.DispatcherQueue();
			cancellation.callback([dispatcher, weakWindow = winrt::make_weak(window), signal, closed]()
			{
				if (!dispatcher.TryEnqueue([weakWindow, closed]()
				{
					if (!*closed)
					{
						if (auto target = weakWindow.get()) target.Close();
					}
				}))
				{
					::SetEvent(signal->get());
				}
			});

			if (cancellation()) co_return;
			window.Activate();
			if (!*closed && owner && ::IsWindow(owner) && ::IsWindowEnabled(owner))
			{
				ownerDisabled = true;
				::EnableWindow(owner, FALSE);
				::SetForegroundWindow(hwnd);
			}

			// Do not propagate cancellation into this wait: the callback closes the window,
			// Closed signals the event, and cleanup must run after returning to the UI.
			co_await WaitForModalSignalAsync(signal);
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainMenuView::ShowJackSettingsModalAsync(
		winrt::hstring const& title, int width, int height)
	{
		auto lifetime = get_strong();
		auto modal = ShowSettingsModalAsync(title, width > 0 ? width : 730, height > 0 ? height : 560,
			[]() -> FrameworkElement { return winrt::make<JackSettingsView>(); },
            [](FrameworkElement const& view,Window const& window,CloseBindings&){
                winrt::get_self<JackSettingsView>(view.as<winrt::DHLS_StrandJack::JackSettingsView>())->HostWindow(window);
            },
            [](FrameworkElement const& view){
                winrt::get_self<JackSettingsView>(view.as<winrt::DHLS_StrandJack::JackSettingsView>())->HostWindow(nullptr);
            });
		auto cancellation = co_await winrt::get_cancellation_token();
		cancellation.callback([modal]() { modal.Cancel(); });
		co_await modal;
	}

	winrt::Windows::Foundation::IAsyncAction MainMenuView::ShowControlSettingsModalAsync(
		winrt::hstring const& title, int width, int height)
	{
		auto lifetime = get_strong();
		auto modal = ShowSettingsModalAsync(title, width > 0 ? width : 1280, height > 0 ? height : 780,
			[]() -> FrameworkElement { return winrt::make<ControlSettingsView>(); },
			[](FrameworkElement const& view, Window const& window, CloseBindings&)
			{
				// Existing XAML close/exit handlers call OnExitClick.
				view.as<winrt::DHLS_StrandJack::ControlSettingsView>().HostWindow(window);
			},
			[](FrameworkElement const& view)
			{
				view.as<winrt::DHLS_StrandJack::ControlSettingsView>().HostWindow(nullptr);
			});
		auto cancellation = co_await winrt::get_cancellation_token();
		cancellation.callback([modal]() { modal.Cancel(); });
		co_await modal;
	}

	// ========================================
	// CONTROL/JACK SETTING Click
	// 두 버튼은 같은 Password 창을 사용하고,
	// 인증 후 각자의 SettingsView를 모달로 표시한다.
	// ========================================
	winrt::fire_and_forget MainMenuView::ProtectedSetting_Click(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		if (m_settingsDialogOpen)
		{
			co_return;
		}

		auto lifetime = get_strong();
		auto btn = sender.try_as<Button>();

		if (!btn)
		{
			co_return;
		}

		m_settingsDialogOpen = true;
		m_activeProtectedButtonName = btn.Name();

		// Click 상태: 노랑 배경 + 검정 아이콘
		ApplyMenuVisualState(
			btn,
			MenuVisualState::GeneralClick);

		try
		{
			// 두 버튼 모두 동일한 Password dialog 함수 사용
			// Development builds currently bypass settings authentication.
			// Restore the verification call before production deployment.
			constexpr bool developmentSkipPassword = true;
			bool passwordAccepted = developmentSkipPassword || co_await VerifySettingsPasswordAsync();

			if (!passwordAccepted)
			{
				m_settingsDialogOpen = false;
				m_activeProtectedButtonName = L"";

				ApplyMenuVisualState(
					btn,
					MenuVisualState::Normal);

				co_return;
			}

			// Password 확인 완료 후 클릭한 버튼에 맞는 View 표시
			if (btn.Name() == L"BtnJack")
			{
				co_await ShowJackSettingsModalAsync(L"Strandjack type properties", 980, 588);
			}
			else if (btn.Name() == L"BtnGeneral")
			{
				co_await ShowControlSettingsModalAsync(L"General Setting", 1024, 800);
			}
		}
		catch (...)
		{
			// 아래 공통 복구 로직에서 버튼 상태를 원상 복구함.
		}

		m_settingsDialogOpen = false;
		m_activeProtectedButtonName = L"";

		ApplyMenuVisualState(
			btn,
			MenuVisualState::Normal);
	}

	// ========================================
	// Version
	// ========================================
	void MainMenuView::VersionText_Loaded(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (auto text = sender.try_as<TextBlock>())
			text.Text(::DHLS_StrandJack::Services::ApplicationVersionText());
	}

	// ========================================
	// EXIT
	// ========================================
	winrt::fire_and_forget MainMenuView::BtnExit_Click(
		winrt::Windows::Foundation::IInspectable const& sender,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		// EXIT 다이얼로그 중복 실행 방지
		if (m_exitDialogOpen)
		{
			co_return;
		}

		auto lifetime = get_strong();

		auto btn = sender.try_as<Button>();

		// ========================================
		// EXIT Click
		// 빨강 배경 + 검정 아이콘
		// ========================================

		m_exitDialogOpen = true;

		if (btn)
		{
			ApplyMenuVisualState(
				btn,
				MenuVisualState::ExitClick);
		}

		try
		{
			ContentDialog exitDialog;

			exitDialog.XamlRoot(
				this->XamlRoot());

			exitDialog.Title(
				winrt::box_value(
					L"프로그램 종료"));

			exitDialog.Content(
				winrt::box_value(
					L"정말 프로그램을 종료하시겠습니까?"));

			exitDialog.PrimaryButtonText(
				L"확인");
                exitDialog.PrimaryButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogPrimaryAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());

			exitDialog.CloseButtonText(
				L"취소");
                exitDialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogCancelIconStyle")).as<Microsoft::UI::Xaml::Style>());

			exitDialog.DefaultButton(
				ContentDialogButton::Close);


			auto result =
				co_await exitDialog.ShowAsync();


			// ========================================
			// 확인
			// ========================================

			if (result ==
				ContentDialogResult::Primary)
			{
				Application::Current().Exit();

				co_return;
			}


			// ========================================
			// 취소
			// ========================================

			m_exitDialogOpen = false;

			// 모든 버튼
			// 흰 배경 + 검정 아이콘
			ResetAllButtonsToNormal();
		}
		catch (...)
		{
			m_exitDialogOpen = false;

			// 오류가 발생해도 UI 복구
			ResetAllButtonsToNormal();
		}
	}
}
