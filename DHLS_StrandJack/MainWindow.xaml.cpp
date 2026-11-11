#include "pch.h"
#include "Localization.h"
#include "ControlLogic/EquipmentSession.h"
#include "ConfigManager.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// MainMenuView 클래스를 인스턴스화하기 위해 헤더 포함
#include "MainMenuView.xaml.h"
#include "MainDashboardView.xaml.h"
#include "NewModelView.g.h"
#include "ModelManageView.g.h"
#include <algorithm>
#include <utility>
#include "RoundedWindow.h"

// 창 크기 조절을 위한 네이티브 헤더 추가
// 올바른 네이티브 윈도우 헤더로 교체
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DHLS_StrandJack::implementation
{
	// 현재 Window의 HWND를 저장하여 다른 화면에서 공유합니다.
	static HWND s_mainWindowHwnd = nullptr;
	static MainWindow* s_mainWindowImpl = nullptr;

	MainWindow::MainWindow()
	{
		InitializeComponent();
		Title(::DHLS_StrandJack::Services::Localization::Text(L"DHLS Strand Jack"));

		// 1. 현재 창의 HWND(네이티브 윈도우 핸들) 가져오기
		auto windowNative = this->try_as<::IWindowNative>();
		HWND hwnd{ nullptr };
		winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));

		s_mainWindowHwnd = hwnd;  // MainMenuView에서도 사용할 수 있도록 저장합니다.
		s_mainWindowImpl = this;  // ModelManageView 탐색에 사용합니다.

		// The publisher is this window and the delegate holds no view/Window reference.
		SizeChanged([hwnd](auto const&, auto const&)
			{
				::DHLS_StrandJack::Common::UpdateRoundedWindowRegion(hwnd);
			});

		// 2. HWND를 WinUI 3의 AppWindow 객체로 변환
		winrt::Microsoft::UI::WindowId windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
		winrt::Microsoft::UI::Windowing::AppWindow appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

		// 3. 첨부된 이미지 비율에 맞춘 초기 사이즈 설정 (가로 1280, 세로 460)
		appWindow.Resize(winrt::Windows::Graphics::SizeInt32{ 1280, 460 });
        // AppWindow does not support IWeakReferenceSource: do not use auto_revoke.
        m_sequenceClosingPublisher=appWindow;
        m_sequenceClosingToken=m_sequenceClosingPublisher.Closing(
            [weak=get_weak()](auto const&,winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args){
                if(auto self=weak.get()){
                    if(self->m_closeApproved)return;
                    args.Cancel(true);
                    if(!self->m_closePending)self->CloseAfterSequenceAsync();
                }
            });
        m_sequenceClosingSubscribed=true;
        Closed([weak=get_weak()](auto const&,auto const&){
            if(auto self=weak.get()){
                ::DHLS_StrandJack::Services::AuditLog::Write("APP_CLOSE","MainWindow closed");
                self->RevokeSequenceClosing();
                if(self->m_plcStatusTimer){self->m_plcStatusTimer.Stop();self->m_plcStatusTimer=nullptr;}
                if(s_mainWindowImpl==self.get()){
                    s_mainWindowImpl=nullptr;s_mainWindowHwnd=nullptr;
                }
            }
        });


        m_plcStatusTimer=winrt::Microsoft::UI::Xaml::DispatcherTimer{};
        m_plcStatusTimer.Interval(std::chrono::milliseconds(500));
        m_plcStatusTimer.Tick([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get())self->UpdateAppPlcStatus();});
        m_plcStatusTimer.Start();
		// 윈도우 콘텐츠를 타이틀바 영역까지 확장합니다.
		//ExtendsContentIntoTitleBar(true);
		//RemoveSysMenu(hwnd, true);
	}

    MainWindow::~MainWindow()
    {
        RevokeSequenceClosing();
        if(m_plcStatusTimer){m_plcStatusTimer.Stop();m_plcStatusTimer=nullptr;}
        if(s_mainWindowImpl==this){s_mainWindowImpl=nullptr;s_mainWindowHwnd=nullptr;}
    }

    void MainWindow::RevokeSequenceClosing() noexcept
    {
        if(!m_sequenceClosingSubscribed)return;
        m_sequenceClosingSubscribed=false;
        auto publisher=std::exchange(m_sequenceClosingPublisher,nullptr);
        const auto token=std::exchange(m_sequenceClosingToken,winrt::event_token{});
        if(publisher){
            try{publisher.Closing(token);}
            catch(winrt::hresult_error const& error){::OutputDebugStringW(error.message().c_str());}
        }
    }

    winrt::fire_and_forget MainWindow::CloseAfterSequenceAsync()
    {
        auto lifetime=get_strong();
        if(m_closePending)co_return;
        m_closePending=true;
        try{
            auto dashboard=MainContentHost().Content().try_as<winrt::DHLS_StrandJack::MainDashboardView>();
            if(dashboard && !co_await winrt::get_self<MainDashboardView>(dashboard)->PrepareToCloseAsync()){
                m_closePending=false;co_return;
            }
            if(m_plcStatusTimer)m_plcStatusTimer.Stop();
            auto ui=winrt::apartment_context();co_await winrt::resume_background();
            ::DHLS_StrandJack::Control::EquipmentSession::Shared().Shutdown();
            try{co_await ui;}catch(...){co_return;}
            m_closeApproved=true;
            Close();
        }catch(winrt::hresult_error const& error){
            m_closePending=false;::OutputDebugStringW(error.message().c_str());
        }
    }

    void MainWindow::UpdateAppPlcStatus(){
        using L=::DHLS_StrandJack::Services::Localization;
        auto service=::DHLS_StrandJack::Control::EquipmentSession::Shared().Current();
        const bool hasModel=!::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName.empty();
        AppPlcStatusBar().Visibility(service||hasModel?Visibility::Visible:Visibility::Collapsed);
        if(!service){AppPlcStatusText().Text(L::IsEnglish()?L"PLC disconnected / connection pending":L"PLC 연결 해제 / 연결 대기");return;}
        const auto state=service->Latest();std::wstring text=state.simulation?(L::IsEnglish()?L"SIMULATION | ":L"시뮬레이션 | "):L"";
        for(std::size_t i=0;i<state.plcs.size();++i){if(i)text+=L"  |  ";auto const& plc=state.plcs[i];text+=L"PLC "+std::to_wstring(i+1)+L": ";
            if(!plc.sampledAtMs)text+=L::IsEnglish()?L"Connecting":L"연결 중";
            else if(!plc.connected||!plc.readOk||!plc.usable)text+=L::IsEnglish()?L"Communication error":L"통신 오류";
            else text+=!state.simulation&&!plc.pcSelected?L"REMOTE":L::IsEnglish()?L"Connected":L"연결 정상";
        }
        AppPlcStatusText().Text(text);
    }

    bool MainWindow::CanNavigateFromCurrentView()
    {
        if (!s_mainWindowImpl) return true;
        auto dashboard = s_mainWindowImpl->MainContentHost().Content().try_as<winrt::DHLS_StrandJack::MainDashboardView>();
        return !dashboard || winrt::get_self<MainDashboardView>(dashboard)->CanLeaveDashboard(true);
    }

	void MainWindow::OnNewModelClicked()
	{
        if (!CanNavigateFromCurrentView()) return;
		// 1. NewModelView UserControl 인스턴스 생성
		winrt::DHLS_StrandJack::NewModelView indexControl;

		// 2. ContentControl의 내용으로 주입
		MainContentHost().Content(indexControl);

		// 3. 런처 버튼들 숨김 처리 (화면 전환 효과)
		LauncherPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

		// 향후 무거운 모델 생성 로직 추가 시 co_await winrt::resume_background(); 사용을 권장합니다.
		CallRestoreWindowSize(480, 420);// 348, 320);

	}

	void MainWindow::NavigateToView(winrt::Windows::Foundation::IInspectable const& content, int width, int height)
	{
        if (!CanNavigateFromCurrentView()) return;
		if (s_mainWindowImpl)
		{
			if (content)
			{
				::DHLS_StrandJack::Services::AuditLog::Write("NAVIGATE",winrt::to_string(winrt::get_class_name(content)));
                s_mainWindowImpl->MainContentHost().Content(content);
				s_mainWindowImpl->LauncherPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
			}
			else
			{
				// nullptr 
				s_mainWindowImpl->MainContentHost().Content(nullptr);
				s_mainWindowImpl->LauncherPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
			}
			s_mainWindowImpl->CallRestoreWindowSize(width, height);
		}
	}

	void MainWindow::NavigateToFullScreen(winrt::Windows::Foundation::IInspectable const& content)
	{
        if (!CanNavigateFromCurrentView()) return;
		if (!s_mainWindowImpl || !content) return;
		auto appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(winrt::Microsoft::UI::GetWindowIdFromWindow(GetHwnd()));
		::DHLS_StrandJack::Services::AuditLog::Write("NAVIGATE",winrt::to_string(winrt::get_class_name(content)));
                s_mainWindowImpl->MainContentHost().Content(content);
		s_mainWindowImpl->LauncherPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        if(::IsZoomed(GetHwnd()))s_mainWindowImpl->m_keepMaximized=true;
		appWindow.SetPresenter(winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
	}

	void MainWindow::OnExistingModelClicked()
	{
        if (!CanNavigateFromCurrentView()) return;
		// 1. ModelManageView UserControl 인스턴스 생성
		winrt::DHLS_StrandJack::ModelManageView indexControl;

		// 2. ContentControl의 내용으로 주입
		MainContentHost().Content(indexControl);

		// 3. 런처 버튼들 숨김 처리 (화면 전환 효과)
		LauncherPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

		// 향후 무거운 모델 생성 로직 추가 시 co_await winrt::resume_background(); 사용을 권장합니다.
		CallRestoreWindowSize(480, 420);// 336, 428);// 348, 436);

	}

	void MainWindow::CallRestoreWindowSize(int width, int height)
	{
		// 화면 전환 후 창을 원래 프로그램 크기로 다시 키우려면 아래 코드 활용
		auto windowNative = this->try_as<::IWindowNative>();
		HWND hwnd{ nullptr };
		windowNative->get_WindowHandle(&hwnd);
		winrt::Microsoft::UI::WindowId windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
		winrt::Microsoft::UI::Windowing::AppWindow appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        if(::IsZoomed(hwnd))m_keepMaximized=true;
		if (appWindow.Presenter().Kind() == winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen)
			appWindow.SetPresenter(winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
		if (auto presenter = appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
		{
            const bool modelScreen=static_cast<bool>(MainContentHost().Content().try_as<winrt::DHLS_StrandJack::ModelManageView>());
            if(m_keepMaximized&&!modelScreen){presenter.Maximize();return;}
            if(modelScreen&&presenter.State()==winrt::Microsoft::UI::Windowing::OverlappedPresenterState::Maximized)
                presenter.Restore();
		}
		// XAML dimensions are effective pixels; AppWindow bounds are physical pixels.
		// Remove the caption before sizing so the final non-client area is accounted for.
		RemoveSysMenu(hwnd);
		const double dpiScale = static_cast<double>(::GetDpiForWindow(hwnd)) / 96.0;
		int targetWidth = static_cast<int>(width * dpiScale);
		int targetHeight = static_cast<int>(height * dpiScale);
		MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
		if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
		{
			const auto& work = monitorInfo.rcWork;
			// Match the CONTROL SETTING modal: 8 physical pixels of monitor margin,
			// a uniform DPI-aware client size, then account for the window frame.
			const int availableWidth = (std::max)(1L, work.right - work.left - 16);
			const int availableHeight = (std::max)(1L, work.bottom - work.top - 16);
			double scale = dpiScale;
			const double widthLimit = static_cast<double>(availableWidth) / width;
			const double heightLimit = static_cast<double>(availableHeight) / height;
			if (scale > widthLimit) scale = widthLimit;
			if (scale > heightLimit) scale = heightLimit;
			RECT bounds{ 0, 0, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale) };
			winrt::check_bool(::AdjustWindowRectExForDpi(&bounds,
				static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
				static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), ::GetDpiForWindow(hwnd)));
			targetWidth = (std::min)(availableWidth, static_cast<int>(bounds.right - bounds.left));
			targetHeight = (std::min)(availableHeight, static_cast<int>(bounds.bottom - bounds.top));
			appWindow.MoveAndResize(winrt::Windows::Graphics::RectInt32{
					work.left + (work.right - work.left - targetWidth) / 2,
					work.top + (work.bottom - work.top - targetHeight) / 2,
					targetWidth, targetHeight });
		}
		else
		{
			appWindow.Resize(winrt::Windows::Graphics::SizeInt32{ targetWidth, targetHeight });
		}
		// appWindow.MoveAndResize({ 100, 100, 1280, 720 }); 

	 // 3. 배경색 원래 메인 색상으로 복구 (직접 Content 캐스팅)
		if (auto rootGrid = this->Content().try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>())
		{
			rootGrid.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::White()));
			// Application 테마 리소스에서 브러시를 런타임에 찾아와 적용
		 /* if (auto resource = winrt::Microsoft::UI::Xaml::Application::Current().Resources().TryLookup(winrt::box_value(L"ApplicationPageBackgroundThemeBrush")))
			{
				if (auto brush = resource.try_as<winrt::Microsoft::UI::Xaml::Media::Brush>()) {
					rootGrid.Background(brush);
				}
			}
			*/
		}

		//     if (appWindow) {
		//      // 타이틀 바의 제목 변경
		// 			appWindow.Title(::DHLS_StrandJack::Services::Localization::Text(L"신규 모델 생성"));
		// 		}
		RemoveSysMenu(hwnd);
	}

	// static 메서드 구현부 (this 포인터 없이 동작함)
	void MainWindow::RemoveSysMenu(HWND hwnd, bool caption)
	{
		if (!hwnd) return;

		LONG_PTR style =
			::GetWindowLongPtrW(
				hwnd,
				GWL_STYLE);

		if (caption) {
			style &=
				~static_cast<LONG_PTR>(
					WS_MINIMIZEBOX |
					WS_MAXIMIZEBOX);
		}
		else {
			style &=
				~static_cast<LONG_PTR>(
					WS_CAPTION |
					WS_SYSMENU |
					WS_MINIMIZEBOX |
					WS_MAXIMIZEBOX |
					WS_THICKFRAME);
		}

		::SetWindowLongPtrW(
			hwnd,
			GWL_STYLE,
			style);

		::SetWindowPos(
			hwnd,
			nullptr,
			0, 0, 0, 0,
			SWP_NOMOVE |
			SWP_NOSIZE |
			SWP_NOZORDER |
			SWP_NOACTIVATE |
			SWP_FRAMECHANGED);
		::DHLS_StrandJack::Common::UpdateRoundedWindowRegion(hwnd);
	}

    void MainWindow::Minimize()
    {
        // FullScreen has no presenter-level Minimize operation. Keep the
        // existing presenter and page state by minimizing its owned HWND.
        const HWND hwnd = GetHwnd();
        if (hwnd && ::IsWindow(hwnd)) ::ShowWindow(hwnd, SW_MINIMIZE);
    }

	HWND MainWindow::GetHwnd()
	{
		return s_mainWindowHwnd;
	}
}
