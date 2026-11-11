#include "pch.h"
#include "ControlLogic/EquipmentSession.h"
#include "ControlLogic/ControlSettingsRuntime.h"
#include "WarningSound.h"
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include "ControlLogic/PlcDirectionFeedback.h"
#include "JackScaleConsistency.h"
#include "DisplayUnits.h"
#include "ControlLogic/PlcMapCatalog.h"
#include <winrt/Windows.Globalization.NumberFormatting.h>
#include "ControlLogic/DeveloperSimulation.h"
#include "MainDashboardView.xaml.h"
#if __has_include("MainDashboardView.g.cpp")
#include "MainDashboardView.g.cpp"
#endif

#include <algorithm>
#include <functional>
#include <cmath>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Input.h>
#include "MainWindow.xaml.h"
#include "MainMenuView.xaml.h"
#include "JackTypeLabel.h"
#include "ApplicationVersion.h"
#include "PlcMapMonitor.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <chrono>
#include <limits>
#include <utility>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Microsoft::UI::Text;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;

namespace
{
	std::wstring DashboardLogTime(std::chrono::system_clock::time_point at)
	{
		const auto wholeSecond = std::chrono::floor<std::chrono::seconds>(at);
		const auto time = std::chrono::system_clock::to_time_t(wholeSecond);
		std::tm local{};
		if (localtime_s(&local, &time) != 0) return L"--:--:--.---";
		std::wostringstream result;
		result << std::put_time(&local, L"%H:%M:%S") << L'.' << std::setfill(L'0') << std::setw(3)
			<< std::chrono::duration_cast<std::chrono::milliseconds>(at - wholeSecond).count();
		return result.str();
	}
	std::wstring DashboardRunTime(std::uint64_t elapsedMs, bool showMilliseconds)
	{
		const auto seconds = elapsedMs / 1000;
		std::wostringstream duration;
		duration << std::setfill(L'0') << std::setw(2) << seconds / 3600 << L':'
			<< std::setw(2) << (seconds / 60) % 60 << L':' << std::setw(2) << seconds % 60;
		if (showMilliseconds) duration << L'.' << std::setw(3) << elapsedMs % 1000;
		return duration.str();
	}
	// All dimensions are WinUI effective pixels; this view does not use HTML/WebView.
	Color Hex(uint32_t rgb)
	{
		return Color{ 255, static_cast<uint8_t>((rgb >> 16) & 0xFF),
									static_cast<uint8_t>((rgb >> 8) & 0xFF),
									static_cast<uint8_t>(rgb & 0xFF) };
	}
	SolidColorBrush Solid(uint32_t rgb) { return SolidColorBrush{ Hex(rgb) }; }

	Grid DashboardLogRow(hstring const& time, hstring const& message, bool primary = false)
	{
		Grid row;
		ColumnDefinition timeColumn;
		timeColumn.Width(GridLength{ 106, GridUnitType::Pixel });
		row.ColumnDefinitions().Append(timeColumn);
		row.ColumnDefinitions().Append(ColumnDefinition{});

		TextBlock timestamp;
		timestamp.Text(time);
		timestamp.FontFamily(FontFamily{ L"Consolas" });
		timestamp.FontSize(13);
		timestamp.Foreground(Solid(0x202020));
		timestamp.Margin(Thickness{ 6,2,6,2 });
		Border timeCell;
		timeCell.Background(Solid(0xEFF2F5));
		timeCell.BorderBrush(Solid(0xCBD5E1));
		timeCell.BorderThickness(Thickness{ 0,0,1,0 });
		timeCell.Child(timestamp);
		row.Children().Append(timeCell);

		TextBlock body;
		body.Text(message);
		body.FontSize(13);
		if (primary) body.FontWeight(FontWeights::Bold());
		body.Foreground(Solid(0x202020));
		body.TextWrapping(TextWrapping::Wrap);
		body.Margin(Thickness{ 8,2,4,2 });
		Grid::SetColumn(body, 1);
		row.Children().Append(body);
		return row;
	}

	LinearGradientBrush Vertical(uint32_t top, uint32_t bottom)
	{
		LinearGradientBrush brush;
		brush.StartPoint(Point{ 0.0f, 0.0f });
		brush.EndPoint(Point{ 0.0f, 1.0f });
		GradientStop a; a.Color(Hex(top)); a.Offset(0);
		GradientStop b; b.Color(Hex(bottom)); b.Offset(1);
		brush.GradientStops().Append(a);
		brush.GradientStops().Append(b);
		return brush;
	}
	LinearGradientBrush Horizontal(uint32_t left, uint32_t center, uint32_t right,
		uint32_t shoulder = 0, double shoulderOffset = 0.28)
	{
		LinearGradientBrush brush;
		brush.StartPoint(Point{ 0.0f, 0.0f });
		brush.EndPoint(Point{ 1.0f, 0.0f });
		GradientStop a; a.Color(Hex(left)); a.Offset(0);
		GradientStop b; b.Color(Hex(center)); b.Offset(0.5);
		GradientStop c; c.Color(Hex(right)); c.Offset(1);
		brush.GradientStops().Append(a);
		if (shoulder)
		{
			GradientStop start; start.Color(Hex(shoulder)); start.Offset(shoulderOffset);
			brush.GradientStops().Append(start);
		}
		brush.GradientStops().Append(b);
		if (shoulder)
		{
			GradientStop end; end.Color(Hex(shoulder)); end.Offset(1.0 - shoulderOffset);
			brush.GradientStops().Append(end);
		}
		brush.GradientStops().Append(c);
		return brush;
	}
	void RowPixels(Grid const& grid, double pixels)
	{
		RowDefinition row;
		row.Height(GridLengthHelper::FromPixels(pixels));
		grid.RowDefinitions().Append(row);
	}
	void RowStar(Grid const& grid, double weight = 1.0)
	{
		RowDefinition row;
		row.Height(GridLengthHelper::FromValueAndType(weight, GridUnitType::Star));
		grid.RowDefinitions().Append(row);
	}
	void ColumnPixels(Grid const& grid, double pixels)
	{
		ColumnDefinition column;
		column.Width(GridLengthHelper::FromPixels(pixels));
		grid.ColumnDefinitions().Append(column);
	}
	void ColumnStar(Grid const& grid, double weight = 1.0)
	{
		ColumnDefinition column;
		column.Width(GridLengthHelper::FromValueAndType(weight, GridUnitType::Star));
		grid.ColumnDefinitions().Append(column);
	}
	TextBlock CreateDashboardText(hstring const& text, double size = 12.0,
		uint32_t color = 0xFFFFFF, bool bold = false)
	{
		TextBlock label;
		label.Text(::DHLS_StrandJack::Services::Localization::Text(text.c_str()));
		label.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{ L"Malgun Gothic" });
		label.FontSize(size);
		label.Foreground(Solid(color));
		if (bold) label.FontWeight(FontWeights::Bold());
		label.VerticalAlignment(VerticalAlignment::Center);
		label.TextAlignment(TextAlignment::Center);
		label.TextTrimming(TextTrimming::CharacterEllipsis);
		return label;
	}
	Border CreateDashboardPanel(Brush const& background, double radius = 0.0)
	{
		Border box;
		box.Background(background);
		box.CornerRadius(CornerRadius{ radius, radius, radius, radius });
		return box;
	}
	Button BareButton(UIElement const& content)
	{
		Button button;
		button.Padding(Thickness{ 0,0,0,0 });
		button.Background(Solid(0x888888));
		button.BorderThickness(Thickness{ 0,0,0,0 });
		button.HorizontalAlignment(HorizontalAlignment::Stretch);
		button.VerticalAlignment(VerticalAlignment::Stretch);
		button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
		button.VerticalContentAlignment(VerticalAlignment::Stretch);
		button.Content(content);
		return button;
	}
	hstring OneDecimal(double value)
	{
		if (!std::isfinite(value)) return L"--";
		std::wostringstream ss;
		ss << std::fixed << std::setprecision(1) << value;
		return hstring(ss.str());
	}
	hstring Displacement(double value)
	{
		return hstring{ ::DHLS_StrandJack::DisplayUnits::DisplacementText(value) };
	}

}

namespace winrt::DHLS_StrandJack::implementation
{
	void MainDashboardView::HeaderLogoButton_Click(winrt::Windows::Foundation::IInspectable const&,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (m_locked || m_sequenceBusy) return;
		MainWindow::Minimize();
	}

	MainDashboardView::MainDashboardView()
	{
		InitializeComponent();
		// Keep the original DHLS_Sync controls, presented as a compact bottom strip.
		for (auto const& child : OperationStrip().Children())
			if (auto element = child.try_as<FrameworkElement>())
			{
				element.Width(220);
				element.Margin(Thickness{ 2,2,2,2 });
			}
		auto moveFromStrip = [this](UIElement const& element)
			{
				auto children = OperationStrip().Children();
				for (uint32_t index = 0; index < children.Size(); ++index)
					if (children.GetAt(index) == element) { children.RemoveAt(index); return; }
			};
		auto navigation = TopNavigationPanel();
		moveFromStrip(navigation);
		auto lock = LockButton();
		navigation.Child().as<Grid>().Children().RemoveAt(1);
		Grid::SetColumn(lock, 0);
		lock.Margin(Thickness{ 180,0,0,0 });
		lock.HorizontalAlignment(HorizontalAlignment::Left);
		MainDashboardHeader().Children().Append(lock);
		navigation.Width(64);
		navigation.HorizontalAlignment(HorizontalAlignment::Left);
		Grid::SetColumn(navigation, 0);
		MainDashboardHeader().Children().Append(navigation);
		auto actions = StartStopPanel();
		moveFromStrip(actions);
		actions.Width(157);
		actions.HorizontalAlignment(HorizontalAlignment::Left);
		BottomActionPanel().Children().Append(actions);
		if (!::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()) { LiftUpArrow().Fill(Solid(0xFFFFFF)); LiftDownArrow().Fill(Solid(0xFFFFFF)); }
		using PointerHandler = Microsoft::UI::Xaml::Input::PointerEventHandler;
		StartButton().AddHandler(UIElement::PointerPressedEvent(), box_value(PointerHandler{
				[weak = get_weak()](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e) {
						if (auto self = weak.get()) {
								if (self->m_autoMode ||
									 self->m_locked || self->m_sequenceBusy || self->m_manualHeld)return;
								auto properties = e.GetCurrentPoint(self->StartButton()).Properties();
								if (properties.IsRightButtonPressed() || properties.IsMiddleButtonPressed())return;
								self->m_manualHeld = true;
								if (!self->StartButton().CapturePointer(e.Pointer())) { self->m_manualHeld = false; return; }
								self->StartButton_Click(nullptr,nullptr);
								e.Handled(true);
						}
				} }), true);
		PointerHandler finish{ [weak = get_weak()](IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
				if (auto self = weak.get())self->EndManualHold();
		} };
		for (auto eventId : { UIElement::PointerReleasedEvent(),UIElement::PointerCanceledEvent(),
										 UIElement::PointerCaptureLostEvent(),UIElement::PointerExitedEvent() })
			StartButton().AddHandler(eventId, box_value(finish), true);
		StartButton().LostFocus([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
			if (auto self = weak.get())self->EndManualHold();
			});
		auto attachManualDirection = [this, &finish](Button const& button, bool lifting)
			{
				button.AddHandler(UIElement::PointerPressedEvent(), box_value(PointerHandler{
						[weak = get_weak(), lifting](IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
						{
								auto self = weak.get();
								if (!self || self->m_startupMenuMode != 2 || self->m_autoMode || self->m_calibrationMode ||
										self->m_locked || self->m_sequenceBusy || self->m_manualHeld || self->m_operationRunning ||
										self->m_diagnosticStartRequested || self->m_plcFaultActive || self->m_plcEmergencyActive) return;
								if (!self->m_equipmentService)
								{
										self->ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::Text(L"먼저 PLC를 연결하세요."));
										return;
								}
								const auto state = self->m_equipmentService->Latest();
								if (state.monitoring || state.sequenceBusy || state.manualRunning || state.manualStopPending) return;
								const auto source = sender.as<Button>();
								const auto properties = e.GetCurrentPoint(source).Properties();
								if (properties.IsRightButtonPressed() || properties.IsMiddleButtonPressed()) return;
								if (!source.CapturePointer(e.Pointer())) return;
								self->m_manualHeld = true;
								self->BeginMovementProgress(lifting);
								self->RaiseAction(lifting ? L"UpRequested" : L"DownRequested");
								self->StartButton_Click(nullptr, nullptr);
								e.Handled(true);
						} }), true);
				for (auto eventId : { UIElement::PointerReleasedEvent(), UIElement::PointerCanceledEvent(),
														 UIElement::PointerCaptureLostEvent(), UIElement::PointerExitedEvent() })
					button.AddHandler(eventId, box_value(finish), true);
				button.LostFocus([weak = get_weak()](IInspectable const&, RoutedEventArgs const&)
					{
						if (auto self = weak.get()) self->EndManualHold();
					});
			};
		attachManualDirection(FooterManualUp(), true);
		attachManualDirection(FooterManualDown(), false);
		{
			MenuFlyout resetMenu; MenuFlyoutItem reset;
			reset.Text(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Sequence RESET" : L"시퀀스 RESET");
			reset.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
				if (auto self = weak.get()) {
					if (self->m_locked || self->m_sequenceBusy)return;
					self->ConfirmSequenceResetAsync();
				}
				});
			resetMenu.Items().Append(reset); StopButton().ContextFlyout(resetMenu);
			using PointerHandler = Microsoft::UI::Xaml::Input::PointerEventHandler;
			auto stop = StopButton();
			stop.AddHandler(UIElement::PointerPressedEvent(), box_value(PointerHandler{
				[weak = get_weak()](IInspectable const& sender, auto const& args) {
					if (auto self = weak.get(); self && !self->m_fullStopPressedAt && self->CanHoldForFullStop() &&
						args.GetCurrentPoint(sender.as<UIElement>()).Properties().IsLeftButtonPressed()) {
						self->m_fullStopPressedAt = EquipmentService::MonotonicMs();
						self->m_fullStopPointerId = args.Pointer().PointerId();
					}
				}}), true);
			PointerHandler cancelHold{[weak = get_weak()](IInspectable const&, auto const& args) {
				if (auto self = weak.get(); self && args.Pointer().PointerId() == self->m_fullStopPointerId)
					self->m_fullStopPressedAt = 0;
			}};
			stop.AddHandler(UIElement::PointerReleasedEvent(), box_value(PointerHandler{
				[weak = get_weak()](IInspectable const&, auto const& args) {
					if (auto self = weak.get(); self && args.Pointer().PointerId() == self->m_fullStopPointerId) {
						self->UpdateFullStopHold();
						self->m_fullStopPressedAt = 0;
					}
				}}), true);
			for (auto event : {UIElement::PointerCanceledEvent(),
				UIElement::PointerCaptureLostEvent(), UIElement::PointerExitedEvent()})
				stop.AddHandler(event, box_value(cancelHold), true);
			stop.LostFocus([weak = get_weak()](auto const&, auto const&) {
				if (auto self = weak.get()) self->m_fullStopPressedAt = 0;
			});
		}
		auto const& targets = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
		m_displacementReferenceMm = std::isfinite(targets.MovementDistanceMm) && targets.MovementDistanceMm > 0 ? targets.MovementDistanceMm : 50.0;
		m_goalDistanceMm = std::isfinite(targets.GoalDistanceMm) && targets.GoalDistanceMm >= 0 ? targets.GoalDistanceMm : 10000.0;
		m_pressureTargetBar = std::isfinite(targets.PressureTargetBar) && targets.PressureTargetBar > 0 ? targets.PressureTargetBar : 50.0;
		if (::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load())m_autoMode = true;
		RestoreDashboardMemory();
		RefreshModeButtons();
		RefreshOperationPanel();
		auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		DashboardModelNameText().Text(config.ModelInfo.ModelName);
		DashboardModelNameText().Visibility(config.ModelInfo.ModelName.empty() ? Visibility::Collapsed : Visibility::Visible);
		SystemVersionText().Text(::DHLS_StrandJack::Services::ApplicationVersionText());
		ApplyJackArrangementTemplate(::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(config.ControlSettings.JackArrangementTemplate));
		for (int set = 1; set <= m_setCount; ++set)
			for (int port = 1; port <= m_portCount; ++port)
			{
				const size_t index = (set - 1) * config.PLCSettings.Sensor_HPU_Count + port - 1;
				if (set > config.PLCSettings.PLC_Count || port > config.PLCSettings.Sensor_HPU_Count || index >= config.JackSetupDetails.size()) continue;
				for (auto const& spec : config.JackSettings.Specs)
					if (::DHLS_StrandJack::Models::JackTypeLabel(spec) == config.JackSetupDetails[index].JackSelection)
					{
						m_setStates[set - 1].ports[port - 1].capacityTon = spec.Capacity_ton;
						RefreshPort(set, port);
					}
			}
	}

	MainDashboardView::~MainDashboardView() { StopEquipmentIntegration(); }

	winrt::fire_and_forget MainDashboardView::ReleaseEquipmentServiceAsync(std::shared_ptr<EquipmentService> service)
	{
		if (::DHLS_StrandJack::Control::EquipmentSession::Shared().IsCurrent(service))co_return;
		co_await winrt::resume_background(); service->Stop();
	}

	void MainDashboardView::RestoreDashboardMemory() {
		m_stateModel = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
		m_stateSimulation = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
		auto state = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Dashboard(m_stateModel, m_stateSimulation);
		m_hasDashboardMemory = state.valid; if (!state.valid)return;
		m_autoMode = state.automatic; m_locked = state.locked; m_movementLifting = state.lifting; m_stopSelected = state.stopSelected;
		m_pauseStopRequested = state.pauseRequested; m_fullStopChosen = state.fullStop; m_appliedResetEvent = state.appliedReset;
		for (int s = 0; s < 4; ++s) { m_setStates[s].liftingSpeed = (std::clamp)(state.up[s], 20, 60); m_setStates[s].loweringSpeed = (std::clamp)(state.down[s], 20, 60); for (int p = 0; p < 8; ++p)m_setStates[s].ports[p].selected = state.selected[s][p]; }
	}
	void MainDashboardView::SaveDashboardMemory() {
		if (m_stateModel.empty())return;
		::DHLS_StrandJack::Control::EquipmentSession::DashboardMemory state; state.valid = true; state.automatic = m_calibrationMode ? ::DHLS_StrandJack::Control::EquipmentSession::Shared().Dashboard(m_stateModel, m_stateSimulation).automatic : m_autoMode; state.locked = m_locked; state.lifting = m_movementLifting;
		state.stopSelected = m_stopSelected; state.pauseRequested = m_pauseStopRequested; state.fullStop = m_fullStopChosen; state.appliedReset = m_appliedResetEvent;
		for (int s = 0; s < 4; ++s) { state.up[s] = m_setStates[s].liftingSpeed; state.down[s] = m_setStates[s].loweringSpeed; for (int p = 0; p < 8; ++p)state.selected[s][p] = m_setStates[s].ports[p].selected; }
		::DHLS_StrandJack::Control::EquipmentSession::Shared().Dashboard(m_stateModel, m_stateSimulation, std::move(state));
	}
	void MainDashboardView::EquipmentView_Loaded(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_equipmentTimer) return;
		m_equipmentIntegrationActive = true;
		m_equipmentTimer = DispatcherTimer{};
		m_equipmentTimer.Interval(std::chrono::milliseconds(200));
		m_equipmentTimer.Tick([weak = get_weak()](IInspectable const&, IInspectable const&)
			{
				if (auto self = weak.get())
				{
					const bool simulation = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
					self->SimulationHeaderBadge().Visibility(simulation ? Visibility::Visible : Visibility::Collapsed);
					self->SimulationHeaderBadge().Opacity(
						(::DHLS_StrandJack::Control::EquipmentControlService::MonotonicMs() / 500) % 2 ? 0.15 : 1.0);
					self->SimulationHeaderText().Text(::DHLS_StrandJack::Services::Localization::IsEnglish()
						? L"SIMULATION" : L"시뮬레이션");
					if (self->m_manualHeld && ::GetForegroundWindow() != MainWindow::GetHwnd())self->EndManualHold();
					self->ApplyEquipmentSnapshot();
					self->UpdateFullStopHold();
					self->RefreshDashboardLog();
					self->RefreshDashboardClocks();
					self->RefreshMotionArrows();
				}
			});
		RefreshDashboardClocks();
		m_equipmentTimer.Start();
		try {
			auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig(); auto options = EquipmentService::FromModel(config); options.simulation = m_stateSimulation;
			m_equipmentService = ::DHLS_StrandJack::Control::EquipmentSession::Shared().CurrentFor(options, m_stateModel);
		}
		catch (...) {}
		if (m_equipmentService)ApplyEquipmentSnapshot();
		if (m_stateSimulation && !m_equipmentService) ConnectEquipmentAsync();
		RefreshLockState();
		if (!m_equipmentService)
		{
			const double missing = std::numeric_limits<double>::quiet_NaN();
			for (int set = 1; set <= m_setCount; ++set)
			{
				for (int port = 1; port <= m_portCount; ++port) SetPortMetrics(set, port, missing, missing, missing, missing);
				SetSetTotalWeight(set, missing);
			}
			SetTotalWeight(missing);
		}
	}
	winrt::Windows::Foundation::IAsyncOperation<bool> MainDashboardView::PrepareToCloseAsync()
	{
		auto lifetime = get_strong();
		EndManualHold();
		m_sequenceBusy = true;
		RefreshLockState();
		if (m_equipmentService && !::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()) {
			auto closeState = m_equipmentService->Latest(); if (closeState.automaticOperation && closeState.sequenceBusy)ForwardDiagnosticAction(L"FullStopRequested");
			auto ui = winrt::apartment_context(); const auto deadline = EquipmentService::MonotonicMs() + 5000;
			while (m_equipmentService) {
				auto state = m_equipmentService->Latest(); if (!state.manualRunning && !state.manualStopPending && !state.sequenceBusy)break;
				if (EquipmentService::MonotonicMs() >= deadline) { EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Output or P.T/Stroke stop feedback unconfirmed; window remains open." : L"출력 또는 P.T/Stroke 정지 확인 미완료: 상태 확인 후 다시 종료하세요."); co_return false; }
				co_await winrt::resume_after(std::chrono::milliseconds(20)); co_await ui;
			}
		}
		// A disconnect already owns the service in its coroutine; wait for that join too.
		auto closeUi = winrt::apartment_context();
		const auto disconnectDeadline = EquipmentService::MonotonicMs() + 5000;
		while (m_equipmentDisconnecting && !m_equipmentService) {
			if (EquipmentService::MonotonicMs() >= disconnectDeadline) {
				EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"Stop not confirmed; window remains open"));
				co_return false;
			}
			co_await winrt::resume_after(std::chrono::milliseconds(20));
			co_await closeUi;
		}
		m_equipmentDisconnecting = true;
		if (m_equipmentTimer)m_equipmentTimer.Stop();
		auto service = std::exchange(m_equipmentService, nullptr);
		if (service) {
			auto ui = winrt::apartment_context();
			co_await winrt::resume_background();
			service->Stop();
			const auto finalSnapshot = service->Latest();
			co_await ui;
			if ((finalSnapshot.simulation || finalSnapshot.automaticOperation) && !finalSnapshot.sequenceStopConfirmed) {
				m_sequenceBusy = false; m_equipmentDisconnecting = false;
				RefreshLockState();
				EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"Stop not confirmed; window remains open"));
				co_return false;
			}
		}
		co_return true;
	}
	void MainDashboardView::EquipmentView_Unloaded(IInspectable const&, RoutedEventArgs const&) { StopEquipmentIntegration(); }
	void MainDashboardView::StopEquipmentIntegration()
	{
		m_fullStopPressedAt = 0;
		if (m_equipmentIntegrationActive)SaveDashboardMemory();
		ClosePressureDiagnostics();
		ClosePlcMapWindow();
		EndManualHold();
		m_sequenceBusy = false;
		if (m_equipmentTimer) { m_equipmentTimer.Stop(); m_equipmentTimer = nullptr; }
		m_equipmentIntegrationActive = false; m_diagnosticStartRequested = false;
		m_equipmentService.reset();
	}
	winrt::fire_and_forget MainDashboardView::DisconnectEquipmentAsync(std::shared_ptr<EquipmentService> service)
	{
		auto ui = winrt::apartment_context(); auto weak = get_weak();
		co_await winrt::resume_background();
		const bool disconnected = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Disconnect(service);
		try { co_await ui; }
		catch (hresult_error const&) { co_return; }
		if (auto self = weak.get())
		{
			self->m_equipmentDisconnecting = false;
			if (!disconnected)self->m_equipmentService = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Current();
			if (self->IsLoaded()) self->EquipmentConnectButton().IsEnabled(!self->m_locked && !self->m_sequenceBusy);
		}
	}
	void MainDashboardView::ClosePressureDiagnostics(bool closeWindow)
	{
		auto window = std::exchange(m_pressureDiagnosticsWindow, nullptr);
		m_pressureDiagnosticsText = nullptr;
		if (!window)return;
		try { window.Closed(m_pressureDiagnosticsClosed); }
		catch (...) {}
		m_pressureDiagnosticsClosed = {};
		try { window.Content(nullptr); if (closeWindow)window.Close(); }
		catch (...) {}
	}
	void MainDashboardView::PressureDiagnostics_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!IsLoaded())return;
		if (m_pressureDiagnosticsWindow) { m_pressureDiagnosticsWindow.Activate(); return; }
		auto lifetime = get_strong();
		try {
			using L = ::DHLS_StrandJack::Services::Localization;
			Window window; m_pressureDiagnosticsWindow = window;
			window.Title(L::IsEnglish() ? L"Automatic pressure diagnostics" : L"압력 자동 진단");
			ScrollViewer scroll; scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
			scroll.Padding(Thickness{ 20 }); scroll.Background(Solid(0x172033));
			TextBlock text; text.Foreground(Solid(0xFFF0A0)); text.FontSize(16); text.TextWrapping(TextWrapping::Wrap);
			text.Text(L::IsEnglish() ? L"Select pressure / automatic mode to view diagnostics." : L"압력제어 / 자동모드를 선택하면 진단값이 표시됩니다.");
			m_pressureDiagnosticsText = text; scroll.Content(text); window.Content(scroll);
			m_pressureDiagnosticsClosed = window.Closed([weak = get_weak()](auto const&, auto const&) {if (auto self = weak.get())self->ClosePressureDiagnostics(false); });
			HWND hwnd = nullptr; check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&hwnd));
			auto owner = MainWindow::GetHwnd();
			if (owner && ::IsWindow(owner))::SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
			MONITORINFO monitor{ sizeof(MONITORINFO) };
			if (::GetMonitorInfoW(::MonitorFromWindow(owner ? owner : hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
				auto dpi = ::GetDpiForWindow(hwnd);
				int width = (std::min)(monitor.rcWork.right - monitor.rcWork.left, static_cast<LONG>(::MulDiv(900, dpi ? dpi : 96, 96)));
				int height = (std::min)(monitor.rcWork.bottom - monitor.rcWork.top, static_cast<LONG>(::MulDiv(600, dpi ? dpi : 96, 96)));
				::SetWindowPos(hwnd, nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2, monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			ApplyEquipmentSnapshot(); window.Activate();
		}
		catch (...) { ClosePressureDiagnostics(); ::DHLS_StrandJack::Services::AuditLog::Write("ERROR", "Pressure diagnostics window failed"); }
	}
	void MainDashboardView::PlcMap_Click(IInspectable const&, RoutedEventArgs const&) { ShowPlcMapWindow(); }
	void MainDashboardView::ClosePlcMapWindow(bool closeWindow) {
		auto window = std::exchange(m_plcMapWindow, nullptr);
		if (!window)return;
		try { window.Closed(m_plcMapClosedToken); }
		catch (...) {}
		m_plcMapClosedToken = {};
		if (m_plcMapMonitor)m_plcMapMonitor->SetAreaHandler({});
		if (m_equipmentService)m_equipmentService->SetPlcMapMonitoring(false);
		try { if (auto host = window.Content().try_as<Grid>())host.Children().Clear(); window.Content(nullptr); if (closeWindow)window.Close(); }
		catch (...) {}
		::DHLS_StrandJack::Services::AuditLog::Write("PLC_MAP_CLOSE", "Modeless read-only PLC map");
	}
	void MainDashboardView::ShowPlcMapWindow() {
		// One modeless window per dashboard; subsequent clicks activate the existing window.
		if (!IsLoaded())return;
		if (m_plcMapWindow) { m_plcMapWindow.Activate(); return; }
		auto lifetime = get_strong();
		try {
			using L = ::DHLS_StrandJack::Services::Localization;
			if (!m_equipmentService && !m_locked && !m_sequenceBusy && !m_operationRunning && !m_equipmentDisconnecting)
				EquipmentConnect_Click(nullptr, nullptr);
			if (!m_plcMapMonitor) {
				auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings;
				std::vector<std::wstring> ips; for (int i = 0; i < config.PLC_Count && i < static_cast<int>(config.PLC_IPs.size()); ++i)ips.push_back(config.PLC_IPs[i]);
				m_plcMapMonitor = ::DHLS_StrandJack::Diagnostics::PlcMapMonitor::Create(std::move(ips), L::IsEnglish());
			}
			Window window;
			m_plcMapWindow = window;
			window.Title(L::IsEnglish() ? L"PLC map status monitor" : L"PLC 맵 상태 모니터");
			Grid host; host.Background(Solid(0xF8FAFC));
			RowDefinition body; body.Height(GridLength{ 1,GridUnitType::Star }); host.RowDefinitions().Append(body);
			auto monitorRoot = m_plcMapMonitor->Root();
			monitorRoot.Width(std::numeric_limits<double>::quiet_NaN()); monitorRoot.Height(std::numeric_limits<double>::quiet_NaN());
			host.Children().Append(monitorRoot);
			window.Content(host);
			m_plcMapClosedToken = window.Closed([weak = get_weak()](auto const&, auto const&) {if (auto self = weak.get())self->ClosePlcMapWindow(false); });
			HWND hwnd = nullptr; winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&hwnd));
			const HWND owner = MainWindow::GetHwnd();
			if (owner && ::IsWindow(owner))::SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
			MONITORINFO monitor{ sizeof(MONITORINFO) };
			if (::GetMonitorInfoW(::MonitorFromWindow(owner ? owner : hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
				const auto dpi = ::GetDpiForWindow(hwnd); const int width = (std::min)(static_cast<int>(monitor.rcWork.right - monitor.rcWork.left), ::MulDiv(1200, dpi ? dpi : 96, 96));
				const int height = (std::min)(static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top), ::MulDiv(760, dpi ? dpi : 96, 96));
				::SetWindowPos(hwnd, nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2, monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (m_equipmentService) {
				std::weak_ptr<EquipmentService> weakService = m_equipmentService;
				m_plcMapMonitor->SetAreaHandler([weakService](bool p, bool m) {if (auto service = weakService.lock())service->SetPlcMapAreas(p, m); });
				m_equipmentService->SetPlcMapAreas(m_plcMapMonitor->UseP(), m_plcMapMonitor->UseM());
				m_equipmentService->SetPlcMapMonitoring(true); m_plcMapMonitor->Update(m_equipmentService->Latest());
			}
			window.Activate();
			::DHLS_StrandJack::Services::AuditLog::Write("PLC_MAP_OPEN", "Modeless read-only PLC map");
		}
		catch (hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); ClosePlcMapWindow(); ::DHLS_StrandJack::Services::AuditLog::Write("ERROR", "PLC map window failed"); }
		catch (...) { ClosePlcMapWindow(); ::DHLS_StrandJack::Services::AuditLog::Write("ERROR", "PLC map window failed"); }
	}
	void MainDashboardView::EquipmentConnect_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_locked) return;
		if (m_sequenceBusy || m_operationRunning) { ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Cannot disconnect PLC during operation. Stop first." : L"운전 중에는 PLC 연결을 해제할 수 없습니다. 먼저 정지하세요."); return; }
		using L = ::DHLS_StrandJack::Services::Localization;
		if (m_plcFaultActive) { ShowControlWarningAsync(L::IsEnglish() ? L"Keep PLC connected until fault OFF and stop confirmation are verified." : L"고장 비트 OFF 및 정지 확인이 완료될 때까지 PLC 연결을 유지하세요."); return; }
		if (m_equipmentDisconnecting) return;
		if (m_equipmentService)
		{
			ConfirmPlcDisconnectAsync();
			return;
		}
		ConnectEquipmentAsync(true);
	}
	winrt::fire_and_forget MainDashboardView::ConnectEquipmentAsync(bool explicitReconnect) {
		if (m_equipmentDisconnecting)co_return;
		auto lifetime = get_strong(); auto ui = winrt::apartment_context();
		m_equipmentDisconnecting = true; EquipmentConnectButton().IsEnabled(false);
		std::shared_ptr<EquipmentService> service; bool failed = false;
		try {
			auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
			auto options = EquipmentService::FromModel(config); options.simulation = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
			auto model = config.ModelInfo.ModelName;
			const auto ticket = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Request();
			co_await winrt::resume_background();
			try { service = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Connect(std::move(options), model, explicitReconnect, {}, ticket); }
			catch (...) { failed = true; }
			try { co_await ui; }
			catch (...) { co_return; }
		}
		catch (...) { failed = true; }
		m_equipmentDisconnecting = false; if (!IsLoaded())co_return;
		m_equipmentService = std::move(service); EquipmentConnectButton().IsEnabled(!m_locked && !m_sequenceBusy);
		using L = ::DHLS_StrandJack::Services::Localization;
		EquipmentConnectButton().Content(box_value(L::Text(m_equipmentService ? L"PLC 연결 해제" : L"PLC 연결")));
		if (!m_equipmentService) { EquipmentStatusText().Text(L::Text(failed ? L"PLC 설정을 확인하세요." : L"연결 대기")); co_return; }
		if (m_plcMapWindow && m_plcMapMonitor) {
			std::weak_ptr<EquipmentService> weakService = m_equipmentService;
			m_plcMapMonitor->SetAreaHandler([weakService](bool p, bool m) {if (auto value = weakService.lock())value->SetPlcMapAreas(p, m); });
			m_equipmentService->SetPlcMapAreas(m_plcMapMonitor->UseP(), m_plcMapMonitor->UseM()); m_equipmentService->SetPlcMapMonitoring(true);
		}
		ApplyEquipmentSnapshot();
	}
	winrt::fire_and_forget MainDashboardView::ConfirmPlcDisconnectAsync() {
		auto lifetime = get_strong();
		if (m_plcDisconnectAuthBusy || !IsLoaded() || !XamlRoot() || !m_equipmentService)co_return;
		m_plcDisconnectAuthBusy = true;
		struct AuthGuard { bool& busy; ~AuthGuard() { busy = false; } }guard{ m_plcDisconnectAuthBusy };
		std::weak_ptr<EquipmentService> expected = m_equipmentService;
		using L = ::DHLS_StrandJack::Services::Localization;
		const bool english = L::IsEnglish();
		try {
			ContentDialog dialog;
			auto root = m_plcMapWindow && m_plcMapMonitor ? m_plcMapMonitor->Root().XamlRoot() : XamlRoot();
			dialog.XamlRoot(root ? root : XamlRoot());
			dialog.Title(box_value(english ? L"Developer verification · disconnect PLC" : L"개발자 확인 · PLC 연결 해제"));
			StackPanel content; content.Spacing(12);
			StackPanel description; description.Orientation(Orientation::Horizontal); description.Spacing(10);
			FontIcon icon; icon.Glyph(L"\xE946"); icon.FontSize(24); icon.Foreground(Solid(0x2563EB));
			TextBlock text; text.Text(english ? L"Enter the developer password to disconnect PLC." : L"PLC 연결을 해제하려면 개발자 비밀번호를 입력하세요."); text.TextWrapping(TextWrapping::Wrap); text.MaxWidth(380);
			description.Children().Append(icon); description.Children().Append(text); content.Children().Append(description);
			PasswordBox password; password.PlaceholderText(english ? L"Developer password" : L"개발자 비밀번호"); content.Children().Append(password);
			TextBlock error; error.Foreground(Solid(0xDC2626)); error.TextWrapping(TextWrapping::Wrap); content.Children().Append(error);
			dialog.Content(content); dialog.PrimaryButtonText(english ? L"Disconnect" : L"연결 해제"); dialog.CloseButtonText(english ? L"Cancel" : L"취소"); dialog.DefaultButton(ContentDialogButton::Close);
			dialog.PrimaryButtonClick([password, error, english](auto const&, auto const& args) {
				const bool valid = password.Password() == L"32990966"; password.Password(L"");
				if (!valid) { args.Cancel(true); error.Text(english ? L"Incorrect developer password." : L"개발자 비밀번호가 올바르지 않습니다."); ::DHLS_StrandJack::Services::AuditLog::Write("PLC_DISCONNECT_DENIED", "Invalid developer password"); }
				});
			const auto result = co_await dialog.ShowAsync(); password.Password(L"");
			if (result != ContentDialogResult::Primary || !IsLoaded())co_return;
			auto authorizedService = expected.lock();
			if (!authorizedService || authorizedService != m_equipmentService || m_equipmentDisconnecting)co_return;
			const auto state = authorizedService->Latest();
			// The modeless map may own the password dialog: recheck dashboard/service state after suspension.
			if (m_locked || m_sequenceBusy || m_operationRunning || m_diagnosticStartRequested || m_plcFaultActive || state.monitoring || state.sequenceBusy || state.manualRunning || state.manualStopPending || state.faultActive) {
				EquipmentStatusText().Text(english ? L"PLC disconnect rejected: operation, lock or fault state changed." : L"PLC 연결 해제 거부: 운전·잠금·고장 상태를 확인하세요."); co_return;
			}
			::DHLS_StrandJack::Services::AuditLog::Write("PLC_DISCONNECT_AUTHORIZED", "Developer verified");
			auto service = std::exchange(m_equipmentService, nullptr);
			m_equipmentDisconnecting = true; m_diagnosticStartRequested = false;
			LiftUpArrow().Fill(Solid(0xFFFFFF)); LiftDownArrow().Fill(Solid(0xFFFFFF));
			EquipmentConnectButton().IsEnabled(false);
			DisconnectEquipmentAsync(std::move(service));
			EquipmentConnectButton().Content(box_value(L::Text(L"PLC 연결")));
			EquipmentStatusText().Text(L::Text(L"연결 대기"));
			if (m_plcMapMonitor)m_plcMapMonitor->Update({});
			SetOperationRunning(false);
			m_pumpByIp.clear();
			const double missing = std::numeric_limits<double>::quiet_NaN();
			for (int s = 1; s <= m_setCount; ++s) { SetPlcConnected(s, false); m_setStates[s - 1].hpuRunning = false; for (int p = 1; p <= m_portCount; ++p)SetPortMetrics(s, p, missing, missing, missing, missing); SetSetTotalWeight(s, missing); }
			RefreshHpuButtons(); SetTotalWeight(missing);

		}
		catch (hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); ::DHLS_StrandJack::Services::AuditLog::Write("ERROR", "PLC disconnect verification unavailable; connection retained"); }
		catch (...) { ::DHLS_StrandJack::Services::AuditLog::Write("ERROR", "PLC disconnect verification failed; connection retained"); }
	}
	void MainDashboardView::ForwardDiagnosticAction(std::wstring const& action)
	{
		if (!m_equipmentIntegrationActive) return;
		if (action == L"StartRequested") m_diagnosticStartRequested = true;
		else if (action == L"StopRequested" || action == L"FullStopRequested" || action == L"EmergencyRequested" || action == L"UpRequested" || action == L"DownRequested" || action == L"ModePressure" || action == L"ModeDisplacement" || action == L"TargetChanged") m_diagnosticStartRequested = false;
		if (action == L"StopRequested") EquipmentStatusText().Text(!m_autoMode && !::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()
			? (::DHLS_StrandJack::Services::Localization::IsEnglish() ? hstring{ L"Manual stop: waiting for D052 OFF confirmation" } : hstring{ L"수동 정지: D052 OFF 확인 중" })
			: hstring{ ::DHLS_StrandJack::Services::Localization::Text(L"읽기 전용: PLC 운전 명령을 전송하지 않았습니다.") });
		if (action == L"Report:CenterOfGravity") return;
		if (action == L"EmergencyRequested") EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"비상정지 전송 대기"));
		if (!m_equipmentService)
		{
			::DHLS_StrandJack::Services::AuditLog::Write(m_stateSimulation ? "SIM_COMMAND_REJECTED" : "COMMAND_REJECTED", to_string(action) + " | No equipment session");
			if (action == L"StartRequested") EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"먼저 PLC를 연결하세요."));
			return;
		}
		::DHLS_StrandJack::Control::DiagnosticRequest request;
		for (int s = 0; s < m_setCount && s < 4; ++s) {
			request.liftingHz[s] = m_setStates[s].liftingSpeed;
			request.loweringHz[s] = m_setStates[s].loweringSpeed;
		}
		request.automatic = m_autoMode;
		request.pressureMode = m_pressureMode; request.lowering = !m_movementLifting; request.locked = m_locked;
		request.targetMm = m_displacementReferenceMm; request.targetBar = m_pressureTargetBar;
		request.mockStrandJob = m_stateSimulation && m_autoMode && !m_pressureMode && !m_calibrationMode;
		request.mockJobGoalMm = m_goalDistanceMm;
		request.mockPretension = m_stateSimulation && m_startupMenuMode == 3;
		if (request.mockPretension) {
			auto selected = TensionTravelChoice().IsChecked();
			request.targetMm = selected && selected.Value() ? TensionTravelInput().Value()
				: ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.PretensionRangeMm;
			request.pretensionLoadPercent = TensionPercentInput().Value();
			request.lowering = false;
		}
		const bool motionRequest = action == L"StartRequested" || action == L"PrepareStrandJob" || action == L"PreparePretension";
		const auto snapshot = motionRequest ? m_equipmentService->Latest() : ::DHLS_StrandJack::Control::EquipmentSnapshot{};
		bool hasEligiblePort = false;
		for (int s = 0; s < m_setCount && s < 4; ++s)
			for (int p = 0; p < m_portCount; ++p) {
				request.selected[s][p] = motionRequest ? PortCanOperate(s, p, snapshot) : m_setStates[s].ports[p].selected;
				hasEligiblePort |= request.selected[s][p];
			}
		if (motionRequest && !hasEligiblePort) {
			m_diagnosticStartRequested = false;
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Select a jack and turn on its pump."
				: L"잭 카드를 활성화하고 해당 펌프를 켜세요.");
			return;
		}
		const bool accepted = m_equipmentService->Submit(to_string(action), request);
		if (!accepted)::DHLS_StrandJack::Services::AuditLog::Write(m_stateSimulation ? "SIM_COMMAND_REJECTED" : "COMMAND_REJECTED", to_string(action));
		if (!accepted && m_equipmentService->IsLiveMonitorOnly())
		{
			m_diagnosticStartRequested = false;
			EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(
				L"읽기 전용: PLC 운전 명령을 전송하지 않았습니다."));
			return;
		}
		if (!accepted && (action == L"UpRequested" || action == L"DownRequested" || action.rfind(L"PumpToggle:", 0) == 0 || action == L"StartRequested"))
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Command rejected. Check lock, operation and PLC state." : L"명령이 거부되었습니다. 잠금·운전·PLC 상태를 확인하세요.");
		if (action == L"StartRequested" && (::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load() || m_autoMode)) {
			if (accepted && !snapshot.mockJobPaused) {
				m_pauseStopRequested = m_stopSelected = false;
				RefreshOperationButtons();
			}
			m_sequenceBusy = accepted; RefreshLockState();
		}
	}
	void MainDashboardView::ApplyEquipmentSnapshot()
	{
		if (!m_equipmentService || !IsLoaded()) return;
		using L = ::DHLS_StrandJack::Services::Localization;
		auto snapshot = m_equipmentService->Latest();
		m_pumpByIp = snapshot.pumpByIp;
		if (m_pressureMode && m_autoMode && !m_calibrationMode) {
			const auto& pa = snapshot.pressureAuto; const bool en = ::DHLS_StrandJack::Services::Localization::IsEnglish();
			std::wostringstream text; text << std::fixed << std::setprecision(3);
			const std::wstring detail = pa.reason.empty() ? (en ? L"Sensor/settings confirmation required" : L"센서/설정 확인 필요") : std::wstring(L::Text(to_hstring(pa.reason).c_str()).c_str());
			text << (snapshot.simulation ? (en ? L"SIMULATION" : L"시뮬레이션") : (snapshot.pressureAuto.commandOutputAllowed ? (en ? L"PLC CONTROL" : L"PLC 제어") : (en ? L"PLC STATUS" : L"PLC 상태"))) << L" | " << L::Text(to_hstring(::DHLS_StrandJack::Control::PressureAutoStateName(pa.state)).c_str()).c_str() << L" | " << detail << L"\n";
			std::size_t selected = 0; for (int set = 0; set < m_setCount; ++set)for (int port = 0; port < m_portCount; ++port)selected += m_setStates[set].ports[port].selected;
			text << (en ? L"Selected: " : L"선택: ") << selected << L" | ";
			if (pa.valid)text << (en ? L"Average: " : L"평균압력: ") << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(pa.averageMPa * 10) << L" " << ::DHLS_StrandJack::DisplayUnits::PressureLabel() << L" | " << (en ? L"Spread: " : L"압력편차: ") << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(pa.spreadMPa * 10) << L" | Work/Stop: " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(pa.workErrorMm) << L" / " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(pa.stopErrorMm) << L" mm\n";
			else text << (en ? L"Diagnostics unconfirmed (not zero)" : L"진단값 미확인") << L"\n";
			text << (en ? L"Recommended speed: " : L"추천 속도: ") << pa.recommendedPercent << L"% | " << (en ? L"PLC applied: " : L"PLC 적용 확인: ");
			if (snapshot.simulation)text << (en ? L"none (simulation)" : L"없음 (시뮬레이션)"); else { bool known = false; for (auto const& plc : snapshot.plcs)for (auto const& word : plc.mapWords)if (word.family == 'D' && word.address == 50 && word.valid) { text << word.value << L" Hz "; known = true; }if (!known)text << (en ? L"unconfirmed" : L"미확인"); }
			for (auto const& port : pa.ports)text << L"\nSET " << (port.id - 1) / 8 + 1 << L" / PORT " << (port.id - 1) % 8 + 1 << L" : " << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(port.pressureMPa * 10) << L" / " << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(port.targetMPa * 10) << L" " << ::DHLS_StrandJack::DisplayUnits::PressureLabel() << L" | " << (en ? L"remaining " : L"잔여 ") << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(port.remainingMPa * 10) << L" | " << (en ? L"estimated force " : L"추정 힘 ") << port.forceKN << L" kN";
			if (pa.ports.empty())for (int set = 0; set < m_setCount; ++set)for (int port = 0; port < m_portCount; ++port)if (m_setStates[set].ports[port].selected)text << L"\nSET " << set + 1 << L" / PORT " << port + 1 << L" : -- / " << std::setprecision(::DHLS_StrandJack::DisplayUnits::PressureDigits()) << ::DHLS_StrandJack::DisplayUnits::Pressure(m_pressureTargetBar) << L" " << ::DHLS_StrandJack::DisplayUnits::PressureLabel() << L" | " << (en ? L"remaining unconfirmed" : L"잔여압력 미확인");
			if (m_pressureDiagnosticsText)m_pressureDiagnosticsText.Text(hstring{ text.str() });
		}

		using ::DHLS_StrandJack::Control::SequencePhase;
		const bool automaticMotion = snapshot.simulation || snapshot.automaticOperation;
		if (snapshot.mockJobRunning && !snapshot.mockJobPaused && m_mockJobPaused) {
			m_pauseStopRequested = m_stopSelected = false;
			RefreshOperationButtons();
		}
		const bool motionRunning = automaticMotion && snapshot.sequencePhase == SequencePhase::Running;
		const bool completed = automaticMotion && snapshot.sequencePhase == SequencePhase::Completed && snapshot.sequenceStopConfirmed;
		const auto motionNow = EquipmentService::MonotonicMs();
		if (motionRunning && !m_motionRunning) m_motionStartedMs = motionNow;
		if (completed && !m_cycleCompleted)
		{
			m_motionCompletedMs = m_motionRunning || m_motionStopping ? motionNow : 0;
			if (snapshot.mockJobFinished)
				m_pauseStopRequested = m_stopSelected = true;
		}
		m_motionRunning = motionRunning;
		m_motionStopping = automaticMotion && snapshot.sequencePhase == SequencePhase::Stopping;
		m_cycleCompleted = completed;
		m_mockJobValid = snapshot.mockJobValid;
		if (m_mockJobChangesLocked != snapshot.mockJobChangesLocked || m_mockJobPaused != snapshot.mockJobPaused)
		{
			m_mockJobChangesLocked = snapshot.mockJobChangesLocked;
			m_mockJobPaused = snapshot.mockJobPaused;
			RefreshLockState();
		}
		m_mockJobTravelMm = snapshot.mockJobTravelMm;
		m_mockJobGoalMm = snapshot.mockJobGoalMm;
		m_mockJobStrokeBaseMm = snapshot.mockJobStrokeBaseMm;
		m_mockJobStrokeSpanMm = snapshot.mockJobStrokeSpanMm;
		const bool wasShowingRemaining = m_showJobRemaining;
		if (snapshot.mockJobChangesLocked && snapshot.mockJobValid) m_showJobRemaining = true;
		else if (!snapshot.mockJobFinished) m_showJobRemaining = false;
		if (m_showJobRemaining || wasShowingRemaining)
		{
			const double value = m_showJobRemaining ? (std::max)(0.0, m_mockJobGoalMm - m_mockJobTravelMm) : m_goalDistanceMm;
			if (GoalDistanceInput().Value() != value)
			{
				m_updatingGoalDistanceInput = true;
				GoalDistanceInput().Value(value);
				m_updatingGoalDistanceInput = false;
			}
		}
		m_cyclePending = (snapshot.simulation || snapshot.automaticOperation) && snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Checking;
		if (!snapshot.simulation) {
			const auto direction = ::DHLS_StrandJack::Control::ConfirmedPlcDirection(snapshot);
			LiftUpArrow().Fill(Solid(direction == 2 ? 0x60FF20 : 0xFFFFFF));
			LiftDownArrow().Fill(Solid(direction == 4 ? 0x60FF20 : 0xFFFFFF));
			if (direction == 2 || direction == 4)m_movementLifting = direction == 2;
		}
		if (m_plcMapMonitor)m_plcMapMonitor->Update(snapshot);
		m_plcFaultActive = snapshot.loadReferenceTripped || snapshot.faultActive || (snapshot.automaticOperation && snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Fault);
		PlcFaultOverlay().Visibility(snapshot.faultActive ? Visibility::Visible : Visibility::Collapsed);
		if (snapshot.faultActive) {
			m_diagnosticStartRequested = false; m_stopSelected = true;
			std::wstring text = L::IsEnglish() ? L"Restart blocked until all fault bits are OFF.\n" : L"고장 비트가 모두 OFF로 확인될 때까지 재시작할 수 없습니다.\n";
			for (auto const& bit : snapshot.faultBits) {
				auto found = std::find_if(::DHLS_StrandJack::Diagnostics::PlcMapCatalog().begin(), ::DHLS_StrandJack::Diagnostics::PlcMapCatalog().end(), [&](auto const& e) {return e.family == L'D' && e.word == bit.word && e.bit == bit.bit; });
				const wchar_t hex[] = L"0123456789ABCDEF";
				text += L"PLC " + std::to_wstring(bit.plc + 1) + L" D0" + std::to_wstring(bit.word) + L"." + hex[bit.bit] + L": ";
				text += found != ::DHLS_StrandJack::Diagnostics::PlcMapCatalog().end() ? (L::IsEnglish() ? found->english : found->korean) : L"Unknown bit";
				if (!bit.confirmed)text += L::IsEnglish() ? L" (last ON; communication unavailable)" : L" (마지막 ON 상태, 통신 확인 불가)";
				text += L"\n";
			}
			text += snapshot.faultStopConfirmed ? (L::IsEnglish() ? L"D051.1/.2/.3 OFF confirmed." : L"D051.1/.2/.3 OFF 확인 완료.") : (L::IsEnglish() ? L"Output stop not confirmed. Check PLC connection." : L"출력 정지 명령 OFF가 확인되지 않았습니다. PLC 연결을 확인하세요.");
			PlcFaultTitle().Text(L::IsEnglish() ? L"PLC fault — stopped" : L"PLC 고장 — 정지"); PlcFaultText().Text(text);
		}

		if (!snapshot.simulation) {
			const bool autoRunning = snapshot.automaticOperation && snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Running;
			if (m_automaticRunning != autoRunning) { m_automaticRunning = autoRunning; RefreshLockState(); }
			const bool busy = snapshot.manualRunning || snapshot.manualStopPending || snapshot.sequenceBusy;
			if (m_sequenceBusy != busy) { m_sequenceBusy = busy; RefreshLockState(); }
			if (snapshot.manualStopPending || snapshot.controlMessage == "Manual D052 OFF confirmed" || snapshot.controlMessage == "Manual START rejected") { m_stopSelected = true; m_diagnosticStartRequested = false; }
		}
		if (snapshot.simulation || snapshot.automaticOperation) {
			const bool busy = snapshot.sequenceBusy;
			if (m_sequenceBusy != busy) { m_sequenceBusy = busy; RefreshLockState(); }
			if (snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Idle && !snapshot.pcEmergencyRequested) {
				m_pcEmergencyRequested = m_plcEmergencyActive = false;
				if (snapshot.sequenceMessage == L"Reset accepted; new START required" && m_appliedResetEvent != snapshot.sequenceEventId) {
					m_appliedResetEvent = snapshot.sequenceEventId;
					m_pauseStopRequested = m_fullStopChosen = m_stopSelected = m_diagnosticStartRequested = false;
					ResetPortSelections();
					for (int set = 1; set <= m_setCount; ++set) {
						RefreshAllButton(set);
						for (int port = 1; port <= m_portCount; ++port)RefreshPort(set, port);
					}
					SetOperationRunning(false); SetMovementProgress(0); RefreshMotionArrows(true); SaveDashboardMemory();
					RefreshLockState();
				}
			}
		}
		if (snapshot.emergencyActive) m_plcEmergencyActive = true;
		else if (snapshot.emergencyStateVerified && !m_pcEmergencyRequested) m_plcEmergencyActive = false;
		EmergencySignalOverlay().Visibility(m_plcEmergencyActive ? Visibility::Visible : Visibility::Collapsed);
		if (m_plcEmergencyActive)
		{
			m_diagnosticStartRequested = false;
			m_stopSelected = true;
		}
		const double missing = std::numeric_limits<double>::quiet_NaN();
		bool anyValid = false, diagnosticWarning = false, anyVisibleConnected = false; double total = 0;
		for (int s = 1; s <= m_setCount; ++s)
		{
			double setTotal = 0; bool complete = true;
			auto const* plc = s <= static_cast<int>(snapshot.plcs.size()) ? &snapshot.plcs[s - 1] : nullptr;
			// OFF is REMOTE only with a fresh usable PLC sample; a disconnect is unknown.
			m_setStates[s - 1].remoteMonitoring = !snapshot.simulation && plc && plc->connected && plc->readOk && plc->usable && !plc->pcSelected;
			SetPlcConnected(s, plc && plc->connected && plc->readOk);
			anyVisibleConnected |= plc && plc->connected && plc->readOk;
			SetErrorActive(s, plc && (plc->error || plc->diagnosticWarning || plc->emergencyBit));
			diagnosticWarning |= plc && plc->diagnosticWarning;
			m_setStates[s - 1].hpuRunning = plc && plc->usable && plc->pumpOn;
			for (int p = 1; p <= m_portCount; ++p)
			{
				auto const* port = plc && p <= static_cast<int>(plc->ports.size()) ? &plc->ports[p - 1] : nullptr;
				auto& portState = m_setStates[s - 1].ports[p - 1];
				portState.mockStrokeZeroMm = port && snapshot.simulation ? port->mockStrokeZeroMm : 0.0;
				portState.sequencePositionValid = port && port->sequencePositionValid;
				if (port && port->mockClampsValid)
					portState.previewClampOpen = { port->mockUpperOpen,port->mockLowerOpen };
				portState.sequenceMm = port && port->strokeValid ? port->absoluteMm : missing;
				if (port) { portState.sequenceTargetMm = port->sequenceTargetMm; portState.sequenceOriginMm = port->sequenceOriginMm; }
				SetPortMetrics(s, p, port && port->pressureDisplayValid ? port->displayPressureBar : port && port->pressureValid ? port->pressureBar : missing,
					port && port->loadDisplayValid ? port->displayLoadTon : port && port->loadValid ? port->loadTon : missing, port && port->strokeDisplayValid ? port->displayRelativeMm : port && port->strokeValid ? port->relativeMm : missing,
					port && port->strokeDisplayValid ? port->displayAbsoluteMm : port && port->strokeValid ? port->absoluteMm : missing);
				if (port && port->loadDisplayValid) setTotal += port->displayLoadTon;
				else if (port && port->loadValid) setTotal += port->loadTon; else complete = false;
				anyValid |= port && port->pressureValid && port->strokeValid;
			}
			SetSetTotalWeight(s, complete ? setTotal : missing);
			total += complete ? setTotal : missing;
		}
		bool loadAlarmEdge = false;
		std::wstring loadStatus;
		auto const& loadConfig = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		for (int s = 0; s < m_setCount && s < 4; ++s)for (int p = 0; p < m_portCount && p < 8; ++p) {
			int level = 0; const auto index = s * loadConfig.PLCSettings.Sensor_HPU_Count + p;
			if (s < static_cast<int>(snapshot.plcs.size()) && p < static_cast<int>(snapshot.plcs[s].ports.size()) &&
				index < static_cast<int>(loadConfig.JackSetupDetails.size()) && m_setStates[s].ports[p].selected) {
				auto const& port = snapshot.plcs[s].ports[p]; auto const& limits = loadConfig.JackSetupDetails[index];
				if (snapshot.plcs[s].readOk && port.loadValid && std::isfinite(port.loadTon))
					level = limits.MaxWeight_ton > 0 && port.loadTon >= limits.MaxWeight_ton ? 2 :
					limits.WorkingWeight_ton > 0 && port.loadTon >= limits.WorkingWeight_ton ? 1 : 0;
			}
			if (level > m_loadAlarmLevels[s][p]) {
				loadAlarmEdge = true;
				::DHLS_StrandJack::Services::AuditLog::Write("LOAD_ALARM", "SET=" + std::to_string(s + 1) + " PORT=" + std::to_string(p + 1) + " level=" + std::to_string(level));
			}
			m_loadAlarmLevels[s][p] = level;
			if (level) {
				if (!loadStatus.empty())loadStatus += L" | ";
				loadStatus += L"SET " + std::to_wstring(s + 1) + L" PORT " + std::to_wstring(p + 1) +
					(level == 2 ? (L::IsEnglish() ? L": maximum load reached - STOP" : L": 최대하중 도달 · 정지") :
						(L::IsEnglish() ? L": working load reached - WARNING" : L": 작업하중 도달 · 경고"));
			}
		}
		const bool alarm = snapshot.loadReferenceTripped || snapshot.faultActive || snapshot.emergencyActive || snapshot.diagnosticWarning || snapshot.manualStopPending || (snapshot.automaticOperation && (snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Fault || snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Stopping));
		if ((snapshot.loadReferenceTripped && !m_loadReferencePreviouslyTripped) || loadAlarmEdge || (alarm && !m_alarmPreviouslyActive))::DHLS_StrandJack::Services::PlayWarningSound(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().SoundSettings, true);
		m_alarmPreviouslyActive = alarm;
		m_loadReferencePreviouslyTripped = snapshot.loadReferenceTripped;
		RefreshHpuButtons(); SetTotalWeight(total); SetOperationRunning(!m_plcFaultActive && !m_plcEmergencyActive &&
			(snapshot.mockJobRunning || snapshot.manualRunning ||
				(snapshot.automaticOperation && snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Running) ||
				(snapshot.monitoring && m_diagnosticStartRequested)));
		StrandReadyButton().IsEnabled(snapshot.simulation && m_autoMode && !m_pressureMode && !m_calibrationMode &&
			!m_locked && !m_operationRunning && !snapshot.sequenceBusy && !m_mockJobChangesLocked &&
			!snapshot.automaticResetRequired && !snapshot.emergencyActive);
		EquipmentStatusText().Text(L::Text(!anyVisibleConnected ? L"통신 대기/오류" : snapshot.monitoring ? (diagnosticWarning ? L"동기화/압력 진단 확인 필요" : L"읽기 전용 진단 중") : anyValid ? L"연결됨" : L"센서 환산 설정 확인 필요"));
		if (snapshot.message.find("halted") != std::string::npos) EquipmentStatusText().Text(L::Text(L"진단 중지: 센서/통신 상태를 확인하세요."));
		else if (snapshot.message.find("fresh calibrated") != std::string::npos) EquipmentStatusText().Text(L::Text(L"선택 포트의 환산·수집 상태를 확인하세요."));
		else if (snapshot.message.find("Invalid diagnostic target") != std::string::npos) EquipmentStatusText().Text(L::Text(L"진단 목표와 선택 포트를 확인하세요."));
		if (snapshot.message.find("emergency") != std::string::npos) EquipmentStatusText().Text(L::Text(L"읽기 전용: 비상정지 명령은 PLC로 전송되지 않습니다."));
		else if (snapshot.message.find("not transmitted") != std::string::npos || snapshot.message.find("no PLC stop") != std::string::npos)
			EquipmentStatusText().Text(L::Text(L"읽기 전용: PLC 운전 명령을 전송하지 않았습니다."));
		if (m_pcEmergencyRequested || snapshot.pcEmergencyRequested)
		{
			std::wstring results;
			for (std::size_t i = 0; i < snapshot.plcs.size(); ++i)
			{
				if (i) results += L" | ";
				results += L"PLC " + std::to_wstring(i + 1) + L" D051.0: ";
				auto state = snapshot.plcs[i].emergencyWrite;
				results += L::Text(state == ::DHLS_StrandJack::Control::EmergencyWriteState::ConfirmedOn ? L"ON 확인"
					: state == ::DHLS_StrandJack::Control::EmergencyWriteState::FailedOrUnknown ? L"전송 실패/상태 불명"
					: state == ::DHLS_StrandJack::Control::EmergencyWriteState::NotRequested ? L"미요청" : L"전송 대기").c_str();
			}
			EquipmentStatusText().Text(hstring{ results });
		}
		else if (!m_plcEmergencyActive && !snapshot.controlMessage.empty()) EquipmentStatusText().Text(L::IsEnglish() ? to_hstring(snapshot.controlMessage) : hstring{ snapshot.controlMessage == "Manual D052 ON confirmed" ? L"수동 운전: 선택 포트 D052 ON 확인" : snapshot.controlMessage == "Manual D052 OFF confirmed" ? L"수동 정지: D052 OFF 확인" : snapshot.controlMessage == "Manual D052 OFF unconfirmed" ? L"수동 정지 미확인: D052 OFF 및 PLC 상태를 확인하세요." : snapshot.controlMessage == "Manual START rejected" ? L"수동 시작 거부: 포트 선택·PLC 방향·연결 상태를 확인하세요." : snapshot.controlMessage == "Inactive direction speed stored locally; D050 unchanged" ? L"선택하지 않은 방향의 속도 저장 완료 (D050 유지)" : snapshot.controlMessage == "D050 speed confirmed" ? L"D050 속도 전송 및 상태 확인 완료" : snapshot.controlMessage == "D051 command confirmed" ? L"D051 명령 전송 및 상태 확인 완료" : L"D050/D051 명령 실패 또는 조작 불가: PLC·운전·잠금 상태를 확인하세요." });
		else if (m_plcEmergencyActive) EquipmentStatusText().Text(L::Text(L"PLC 비상정지 신호 감지: 출력 정지 확인 필요"));
		else if (snapshot.message.find("input cleared") != std::string::npos) EquipmentStatusText().Text(L::Text(L"비상정지 신호 해제: 시작 버튼으로 다시 시작하세요."));
		if (!snapshot.simulation) {
			std::wstring readOnly;
			for (std::size_t i = 0; i < snapshot.plcs.size(); ++i) {
				auto const& plc = snapshot.plcs[i];
				if (!plc.connected || !plc.readOk || !plc.pcSelected) {
					if (!readOnly.empty())readOnly += L", "; readOnly += L"PLC " + std::to_wstring(i + 1);
				}
			}
			if (!readOnly.empty())EquipmentStatusText().Text(readOnly + (L::IsEnglish() ? L": D010.1 OFF/unknown · monitoring only, writes blocked" : L": D010.1 OFF/미확인 · 상태 모니터링 전용, 쓰기 차단") + (snapshot.manualStopPending ? (L::IsEnglish() ? L" · D052 OFF UNCONFIRMED" : L" · D052 OFF 미확인") : L""));
		}
		if (snapshot.simulation) {
			std::wstring status = ::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"SIMULATION | " : L"시뮬레이션 | ";
			auto reason = snapshot.sequenceMessage;
			const std::wstring stopSuffix = L" / stop not confirmed";
			const auto suffix = reason.find(stopSuffix);
			if (suffix != std::wstring::npos)reason.resize(suffix);
			status += L::Text(reason).c_str();
			if (suffix != std::wstring::npos)status += ::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L" / stop not confirmed" : L" / 정지 확인 실패";
			EquipmentStatusText().Text(hstring{ status });
		}
		if (snapshot.automaticOperation && !snapshot.simulation) {
			auto raw = snapshot.sequenceMessage; std::wstring detail;
			if (L::IsEnglish())detail = raw;
			else if (raw.find(L"complete:") != std::wstring::npos)detail = L"자동운전 완료: D052 OFF · P.T/Stroke 정체 확인";
			else if (raw.find(L"fault stopped:") != std::wstring::npos) {
				detail = L"자동운전 고장 정지 확인";
				if (raw.find(L"synchronization") != std::wstring::npos)detail += L" · 동조 편차 초과";
				else if (raw.find(L"opposite direction") != std::wstring::npos)detail += L" · Stroke가 지령 반대 방향으로 이동";
				else if (raw.find(L"overshoot") != std::wstring::npos)detail += L" · 목표 허용오차 초과";
				else if (raw.find(L"pressure or maximum") != std::wstring::npos)detail += L" · 압력·작업 중량 한계";
				else if (raw.find(L"did not move") != std::wstring::npos)detail += L" · Stroke 이동 피드백 없음";
				else if (raw.find(L"HPU") != std::wstring::npos)detail += L" · HPU·방향 상태 확인";
				else if (raw.find(L"PC mode") != std::wstring::npos)detail += L" · 통신·PC 선택 확인";
				else if (raw.find(L"ACK/readback") != std::wstring::npos)detail += L" · 출력 응답 확인 실패";
				else if (raw.find(L"timeout") != std::wstring::npos)detail += L" · 운전 제한시간 초과";
				else detail += L" · 센서·PLC·한계 상태 확인";
				detail += L" · RESET 필요";
			}
			else if (raw.find(L"stopped: D052") != std::wstring::npos)detail = L"일시정지: D052 OFF · P.T/Stroke 정체 확인";
			else if (raw.find(L"feedback unconfirmed") != std::wstring::npos)detail = L"D052 OFF 확인 · P.T/Stroke 정지 피드백 대기";
			else if (raw.find(L"targets reached") != std::wstring::npos)detail = L"목표 도달 · D052 OFF와 P.T/Stroke 정지 확인 중";
			else if (raw.find(L"running:") != std::wstring::npos)detail = L"자동 운전 중: D050 속도 · D052 포트 출력 확인";
			else if (raw.find(L"preparing:") != std::wstring::npos)detail = L"자동 운전 준비: P.T/Stroke 확인";
			else if (raw.find(L"synchronization") != std::wstring::npos)detail = L"동조 편차 초과 · 선택 포트 정지";
			else if (raw.find(L"opposite direction") != std::wstring::npos)detail = L"Stroke가 지령 반대 방향으로 이동 · 선택 포트 정지";
			else if (raw.find(L"overshoot") != std::wstring::npos)detail = L"목표 허용오차 초과 · 선택 포트 정지";
			else if (raw.find(L"pressure or maximum") != std::wstring::npos)detail = L"압력 또는 최대하중 한계 · 선택 포트 정지";
			else if (raw.find(L"did not move") != std::wstring::npos)detail = L"Stroke 이동 피드백 없음 · 선택 포트 정지";
			else if (raw.find(L"HPU feedback") != std::wstring::npos)detail = L"HPU 상태 또는 PLC 방향 비트 확인 필요";
			else if (raw.find(L"stale or PC mode") != std::wstring::npos)detail = L"PLC 통신 또는 D010.1 쓰기 허가 확인 필요";
			else if (raw.find(L"ACK/readback") != std::wstring::npos)detail = L"출력 응답·읽기 확인 실패 · D052 OFF 확인 중";
			else if (raw.find(L"target exceeds") != std::wstring::npos)detail = L"목표가 잭 스트로크 범위를 벗어났습니다.";
			else if (raw.find(L"calibration") != std::wstring::npos || raw.find(L"rated limits") != std::wstring::npos)detail = L"P.T/Stroke 환산·단면적·잭 한계 설정 확인 필요";
			else if (raw.find(L"START:") != std::wstring::npos)detail = L"자동 시작 거부: 포트·속도·동조·감속·PLC 상태 확인 필요";
			else if (raw.find(L"Reset accepted") != std::wstring::npos)detail = L"시퀀스 초기화 완료 · 새 시작 필요";
			else if (raw.find(L"emergency") != std::wstring::npos)detail = L"비상정지 · D052 OFF 및 센서 정지 확인 중";
			else if (raw.find(L"stop requested") != std::wstring::npos)detail = L"정지 요청 · D052 OFF 및 센서 정지 확인 중";
			else detail = L"자동운전 상태 확인 중";
			for (std::size_t set = 0; set < snapshot.plcs.size(); ++set)detail += L" | PLC" + std::to_wstring(set + 1) + L" " + std::to_wstring(snapshot.appliedSpeedHz[set]) + L"Hz";
			EquipmentStatusText().Text(detail);
			if (!snapshot.sequenceBusy && snapshot.sequencePhase != ::DHLS_StrandJack::Control::SequencePhase::Running) { m_stopSelected = true; RefreshOperationButtons(); }
			if (snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Fault && !snapshot.faultActive) {
				PlcFaultOverlay().Visibility(Visibility::Visible); PlcFaultTitle().Text(L::IsEnglish() ? L"Automatic operation stopped" : L"자동운전 정지"); PlcFaultText().Text(detail);
			}
		}

		const auto setupResult = snapshot.simulation ? to_string(hstring{ snapshot.sequenceMessage }) : snapshot.message;
		if (setupResult.find("PRESSURE ZERO:") == 0 || setupResult.find("LOAD SET:") == 0 || setupResult.find("Setup rejected:") == 0)
			EquipmentStatusText().Text(L::IsEnglish() ? to_hstring(setupResult) : hstring{ setupResult.find("PRESSURE ZERO:") == 0 ? L"압력 영점: 음수 P.T는 0, 양수 압력은 유지" : setupResult.find("LOAD SET:") == 0 ? L"하중 기준 설정 완료: 선택 포트의 현재 하중 저장" : L"영점/하중 설정 거부: 정지·잠금·선택 포트·유효 센서 및 양수 하중을 확인하세요." });
		if (snapshot.loadReferenceTripped) {
			EquipmentStatusText().Text(L::IsEnglish() ? to_hstring(snapshot.loadReferenceMessage) : hstring{ L"하중 편차 초과 또는 피드백 이상 · 선택 포트 정지 · RESET 필요" });
			if (!snapshot.emergencyActive && !snapshot.faultActive) { PlcFaultOverlay().Visibility(Visibility::Visible); PlcFaultTitle().Text(L::IsEnglish() ? L"Load tolerance STOP" : L"하중 편차 정지"); PlcFaultText().Text(L::IsEnglish() ? to_hstring(snapshot.loadReferenceMessage) : hstring{ L"기준하중 대비 허용 편차 초과 또는 하중 피드백 이상입니다. 정지 상태를 확인하고 RESET 후 하중 기준을 다시 설정하세요.\n" } + to_hstring(snapshot.loadReferenceMessage.substr(snapshot.loadReferenceMessage.find("SET ") == std::string::npos ? 0 : snapshot.loadReferenceMessage.find("SET ")))); }
		}
		if (!loadStatus.empty() && !snapshot.emergencyActive && !snapshot.faultActive && !snapshot.loadReferenceTripped)
			EquipmentStatusText().Text(EquipmentStatusText().Text() + L" | " + loadStatus);
		if (m_fullStopChosen && snapshot.sequenceStopConfirmed && snapshot.automaticResetRequired &&
			!m_plcEmergencyActive && !m_plcFaultActive)
			EquipmentStatusText().Text(L::Text(L"완전 정지 · 재시작하려면 시퀀스 RESET"));
		else if (snapshot.mockJobFinished && completed && !snapshot.sequenceBusy && !snapshot.automaticResetRequired &&
			snapshot.sequenceMessage.rfind(L"Mock job completed", 0) == 0)
			EquipmentStatusText().Text(L::Text(L"목표 도달 · 일시정지"));
	}
	winrt::fire_and_forget MainDashboardView::ShowCogAsync()
	{
		auto lifetime = get_strong();
		using L = ::DHLS_StrandJack::Services::Localization;
		if (!XamlRoot()) co_return;
		ContentDialog dialog; dialog.XamlRoot(XamlRoot()); dialog.Title(box_value(L"COG"));
		std::wstring text;
		if (m_equipmentService)
		{
			auto snapshot = m_equipmentService->Latest();
			if (snapshot.cog.valid()) text = L"X: " + std::wstring(Displacement(snapshot.cog.cogXMm)) + L" mm\nY: " + std::wstring(Displacement(snapshot.cog.cogYMm)) + L" mm\n" + std::wstring(L::Text(L"총 중량")) + L": " + std::wstring(OneDecimal(::DHLS_StrandJack::DisplayUnits::Weight(snapshot.cog.totalForceKN / 9.80665))) + L" " + ::DHLS_StrandJack::DisplayUnits::WeightLabel();
			else text = L::Text(L"COG 계산에는 교정된 하중, 확인된 X/Y 좌표와 3개 이상의 비직선 지지점이 필요합니다.").c_str();
		}
		else text = L::Text(L"먼저 PLC를 연결하세요.").c_str();
		TextBlock body; body.Text(text); body.TextWrapping(TextWrapping::Wrap); dialog.Content(body);
		dialog.CloseButtonText(L::Text(L"확인"));
		try { co_await dialog.ShowAsync(); }
		catch (hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); }
	}

	hstring MainDashboardView::ModelTitle() const
	{
		auto const& name = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
		return hstring{ name } + L" " + ::DHLS_StrandJack::Services::Localization::Text(L"SYNCHRONOUS LIFTING SYSTEM");
	}

	event_token MainDashboardView::ActionRequested(
		Windows::Foundation::TypedEventHandler<DHLS_StrandJack::MainDashboardView, hstring> const& handler)
	{
		return m_actionRequested.add(handler);
	}
	void MainDashboardView::ActionRequested(event_token const& token)
	{
		m_actionRequested.remove(token);
	}
	void MainDashboardView::RaiseAction(std::wstring const& action)
	{
		// Emits an intention only: the host MUST validate interlocks/permissions
		// and implement PLC/device operations separately.
		ForwardDiagnosticAction(action);
		m_actionRequested(*this, hstring(action));
	}
	bool MainDashboardView::ValidSet(int32_t n) { return n >= 1 && n <= 8; }
	bool MainDashboardView::ValidPort(int32_t n) const { return n >= 1 && n <= m_portCount; }

	void MainDashboardView::DashboardRoot_SizeChanged(IInspectable const&, SizeChangedEventArgs const& args)
	{
		if (!DashboardGrid() || !LeftPanelColumn() || !SystemTitleText()) return;
		DashboardGrid().Width((std::max)(1200.0, static_cast<double>(args.NewSize().Width)));
		DashboardGrid().Height((std::max)(570.0, static_cast<double>(args.NewSize().Height) - 102.0));
		// Matches the HTML @media(max-width:1280px) breakpoint.
		LeftPanelColumn().Width(GridLengthHelper::FromPixels(args.NewSize().Width <= 1280 ? 280 : 328));

	}
	void MainDashboardView::RefreshModeButtons()
	{
		auto light = Vertical(0xF8F8F8, 0xC8C8C8);
		auto dark = Vertical(0x585858, 0x282828);
		DisplacementModeButton().Background(m_pressureMode ? light : dark);
		DisplacementModeButton().Foreground(Solid(m_pressureMode ? 0x808080 : 0x60FF20));
		PressureModeButton().Background(m_pressureMode ? dark : light);
		PressureModeButton().Foreground(Solid(m_pressureMode ? 0x60FF20 : 0x808080));
		AutoModeButton().Background(m_autoMode ? dark : light);
		AutoModeButton().Foreground(Solid(m_autoMode ? 0x60FF20 : 0x808080));
		ManualModeButton().Background(m_autoMode ? light : dark);
		ManualModeButton().Foreground(Solid(m_autoMode ? 0x808080 : 0x60FF20));
		PressureDiagnosticsLabel().Text(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Pressure diagnostics" : L"압력 자동 진단");
		if (m_pressureDiagnosticsText && !(m_pressureMode && m_autoMode && !m_calibrationMode))m_pressureDiagnosticsText.Text(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Select pressure / automatic mode to view diagnostics." : L"압력제어 / 자동모드를 선택하면 진단값이 표시됩니다。");
	}

	// Main-panel callbacks: do not drive any actual motor/valve/pump.
#define DASHBOARD_ACTION_HANDLER(Method, action) \
    void MainDashboardView::Method(IInspectable const&, RoutedEventArgs const&) { if (!m_locked) RaiseAction(L##action); }
	void MainDashboardView::CaptureButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_locked || m_sequenceBusy) return;
		CaptureAndReportAsync();
	}
	winrt::fire_and_forget MainDashboardView::CaptureAndReportAsync()
	{
		auto lifetime = get_strong();
		if (m_captureInProgress || (!IsLoaded() || !XamlRoot())) co_return;
		m_captureInProgress = true;
		winrt::hstring failure;
		try { co_await CaptureViewAsync(); }
		catch (winrt::hresult_canceled const&) { /* Navigated away before pixels were captured. */ }
		catch (winrt::hresult_error const& error) { failure = error.message(); }
		catch (std::exception const& error) { failure = winrt::to_hstring(error.what()); }
		catch (...) { failure = ::DHLS_StrandJack::Services::Localization::Text(L"캡처 저장에 실패했습니다."); }
		m_captureInProgress = false;
		if (!failure.empty())
		{
			::OutputDebugStringW(failure.c_str());
			if (auto root = XamlRoot())
			{
				Microsoft::UI::Xaml::Controls::ContentDialog dialog;
				dialog.XamlRoot(root);
				dialog.Title(box_value(::DHLS_StrandJack::Services::Localization::Text(L"오류")));
				dialog.Content(box_value(::DHLS_StrandJack::Services::Localization::Text(L"캡처 저장에 실패했습니다.") + L"\n" + failure));
				dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
				dialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
					winrt::box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
				try { co_await dialog.ShowAsync(); }
				catch (...) { ::OutputDebugStringW(L"Unable to display capture error.\n"); }
			}
		}
	}

	Windows::Foundation::IAsyncAction MainDashboardView::CaptureViewAsync()
	{
		auto lifetime = get_strong();
		if ((!IsLoaded() || !XamlRoot())) throw winrt::hresult_canceled();
		Media::Imaging::RenderTargetBitmap bitmap;
		co_await bitmap.RenderAsync(*this);
		if ((!IsLoaded() || !XamlRoot())) throw winrt::hresult_canceled();
		const auto width = static_cast<uint32_t>(bitmap.PixelWidth());
		const auto height = static_cast<uint32_t>(bitmap.PixelHeight());
		if (!width || !height)
			throw winrt::hresult_error(E_FAIL, ::DHLS_StrandJack::Services::Localization::Text(L"캡처할 화면이 준비되지 않았습니다."));
		auto buffer = co_await bitmap.GetPixelsAsync();
		if ((!IsLoaded() || !XamlRoot())) throw winrt::hresult_canceled();
		const uint64_t expected = uint64_t{ width } *height * 4;
		if (expected != buffer.Length())
			throw winrt::hresult_error(E_FAIL, ::DHLS_StrandJack::Services::Localization::Text(L"캡처 이미지 데이터가 올바르지 않습니다."));
		std::vector<uint8_t> pixels(buffer.Length());
		Windows::Storage::Streams::DataReader::FromBuffer(buffer).ReadBytes(pixels);
		const double dpi = 96.0 * XamlRoot().RasterizationScale();
		const auto captureRoot = ::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory().parent_path() / L"Captures";

		co_await winrt::resume_background();
		const auto now = std::chrono::system_clock::now();
		const auto timestamp = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_s(&local, &timestamp);
		std::wostringstream date, name;
		date << std::put_time(&local, L"%Y%m%d");
		name << L"MainDashboard_" << std::put_time(&local, L"%H%M%S") << L".png";
		const auto directory = captureRoot / date.str();
		std::filesystem::create_directories(directory);
		auto folder = co_await Windows::Storage::StorageFolder::GetFolderFromPathAsync(directory.wstring());
		auto file = co_await folder.CreateFileAsync(name.str(), Windows::Storage::CreationCollisionOption::GenerateUniqueName);
		const std::filesystem::path filePath{ file.Path().c_str() };
		try
		{
			auto stream = co_await file.OpenAsync(Windows::Storage::FileAccessMode::ReadWrite);
			auto encoder = co_await Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(
				Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId(), stream);
			encoder.SetPixelData(Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
				Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied,
				width, height, dpi, dpi, pixels);
			co_await encoder.FlushAsync();
			if (!(co_await stream.FlushAsync())) throw winrt::hresult_error(E_FAIL);
		}
		catch (...)
		{
			std::error_code ignored;
			std::filesystem::remove(filePath, ignored);
			throw;
		}
		::OutputDebugStringW((L"Capture saved: " + filePath.wstring() + L"\n").c_str());
	}
	bool MainDashboardView::CanChangeControlMode()
	{
		if (m_locked)return false;
		bool busy = m_sequenceBusy || m_manualHeld || m_operationRunning;
		if (m_equipmentService) { auto state = m_equipmentService->Latest(); busy |= state.sequenceBusy || state.manualRunning || state.manualStopPending || state.monitoring; }
		if (busy) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Cannot change control mode during operation or while stopping. Stop first."
				: L"동작 중이거나 정지 처리 중에는 모드를 전환할 수 없습니다. 정지 완료 후 다시 선택하세요."); return false;
		}
		return true;
	}

	bool MainDashboardView::CanLeaveDashboard(bool allowStoppedManual)
	{
		if (m_locked) return false;
		bool busy = m_sequenceBusy || m_manualHeld;
		if (m_equipmentService) { auto state = m_equipmentService->Latest(); busy |= state.manualRunning || state.manualStopPending || state.sequenceBusy || (state.sequenceNavigationLocked && !(allowStoppedManual && !m_autoMode)); }
		if (busy) { ShowNavigationBlockedAsync(); return false; }
		if (!m_operationRunning) return true;
		ShowNavigationBlockedAsync();
		return false;
	}

	winrt::fire_and_forget MainDashboardView::ShowNavigationBlockedAsync()
	{
		auto lifetime = get_strong();
		if (m_navigationWarningOpen || !IsLoaded() || !XamlRoot()) co_return;
		m_navigationWarningOpen = true;
		struct ResetWarning { bool& open; ~ResetWarning() { open = false; } } reset{ m_navigationWarningOpen };
		using L = ::DHLS_StrandJack::Services::Localization;
		try
		{
			ContentDialog dialog;
			dialog.XamlRoot(XamlRoot());
			dialog.Title(box_value(L::Text(L"경고")));
			StackPanel content; content.Orientation(Orientation::Horizontal); content.Spacing(12);
			FontIcon icon; icon.Glyph(L"\xE7BA"); icon.FontSize(28); icon.Foreground(Solid(0xD97706));
			TextBlock text;
			const bool latched = m_equipmentService && m_equipmentService->Latest().sequenceNavigationLocked;
			text.Text(latched ? (L::IsEnglish() ? L"Automatic sequence is retained. Stop and confirm standstill, then use Sequence RESET before changing screen or mode." : L"자동운전 시퀀스가 유지 중입니다. 정지 상태를 확인하고 시퀀스 RESET 후 화면 또는 모드를 전환하세요.") : L::Text(L"동작 중에는 화면을 전환할 수 없습니다. 정지 후 다시 시도하세요."));
			text.TextWrapping(TextWrapping::Wrap); text.MaxWidth(380);
			content.Children().Append(icon); content.Children().Append(text);
			dialog.Content(content);
			dialog.CloseButtonText(L::Text(L"확인"));
			co_await dialog.ShowAsync();
		}
		catch (hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); }
	}

	void MainDashboardView::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!CanLeaveDashboard(true)) return;
		if (m_locked || m_sequenceBusy) return;
		auto lifetime = get_strong();
		RaiseAction(L"Back");
		MainWindow::NavigateToView(winrt::make<MainMenuView>(), 1920, 1080);
	}
	void MainDashboardView::LockButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!m_locked)EndManualHold();
		m_locked = !m_locked;
		RefreshLockState();
		RaiseAction(m_locked ? L"Locked" : L"Unlocked");
	}
	void MainDashboardView::HeightZeroButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!m_locked && !m_sequenceBusy && !m_mockJobChangesLocked && !m_operationRunning)
			RaiseAction(L"HeightZero");
	}
		DASHBOARD_ACTION_HANDLER(PressureZeroButton_Click, "PressureZero")
		DASHBOARD_ACTION_HANDLER(LoadSetButton_Click, "LoadSet")
		winrt::fire_and_forget MainDashboardView::ShowControlWarningAsync(hstring message)
	{
		auto lifetime = get_strong();
		if (m_navigationWarningOpen || !IsLoaded() || !XamlRoot())co_return;
		m_navigationWarningOpen = true;
		::DHLS_StrandJack::Services::PlayWarningSound(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().SoundSettings, false);
		struct Reset { bool& flag; ~Reset() { flag = false; } } reset{ m_navigationWarningOpen };
		using L = ::DHLS_StrandJack::Services::Localization;
		try {
			ContentDialog dialog; dialog.XamlRoot(XamlRoot()); dialog.Title(box_value(L::Text(L"경고")));
			StackPanel content; content.Orientation(Orientation::Horizontal); content.Spacing(12);
			FontIcon icon; icon.Glyph(L"\xE7BA"); icon.FontSize(28); icon.Foreground(Solid(0xD97706));
			TextBlock text; text.Text(message); text.TextWrapping(TextWrapping::Wrap); text.MaxWidth(400);
			content.Children().Append(icon); content.Children().Append(text); dialog.Content(content);
			dialog.CloseButtonText(L::Text(L"확인")); co_await dialog.ShowAsync();
		}
		catch (hresult_error const& error) { ::OutputDebugStringW(error.message().c_str()); }
	}
	void MainDashboardView::UpButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_locked) return;
		bool busy = m_plcFaultActive || m_sequenceBusy || m_operationRunning || m_diagnosticStartRequested;
		if (m_equipmentService) { auto state = m_equipmentService->Latest(); busy |= state.monitoring || state.sequenceBusy || state.manualRunning || state.manualStopPending; }
		if (busy) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Cannot change direction during operation or while stopping. Stop first."
				: L"동작 중이거나 정지 처리 중에는 방향을 전환할 수 없습니다. 정지 완료 후 다시 선택하세요.");
			return;
		}
		BeginMovementProgress(true);
		RaiseAction(L"UpRequested");
	}
	void MainDashboardView::DownButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_locked) return;
		bool busy = m_plcFaultActive || m_sequenceBusy || m_operationRunning || m_diagnosticStartRequested;
		if (m_equipmentService) { auto state = m_equipmentService->Latest(); busy |= state.monitoring || state.sequenceBusy || state.manualRunning || state.manualStopPending; }
		if (busy) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Cannot change direction during operation or while stopping. Stop first."
				: L"동작 중이거나 정지 처리 중에는 방향을 전환할 수 없습니다. 정지 완료 후 다시 선택하세요.");
			return;
		}
		BeginMovementProgress(false);
		RaiseAction(L"DownRequested");
	}
	void MainDashboardView::CalibrationZeroButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!m_locked)ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
			? L"Calibration ZERO is unavailable until its PLC procedure is defined."
			: L"교정 ZERO의 PLC 절차가 아직 정의되지 않아 사용할 수 없습니다.");
	}
	void MainDashboardView::CalibrationSpanButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!m_locked)ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
			? L"Calibration SPAN is unavailable until its PLC procedure is defined."
			: L"교정 SPAN의 PLC 절차가 아직 정의되지 않아 사용할 수 없습니다.");
	}
	winrt::fire_and_forget MainDashboardView::ConfirmSequenceResetAsync() {
		auto lifetime = get_strong();
		if (m_sequenceResetAuthBusy || m_plcDisconnectAuthBusy || m_locked || !IsLoaded() || !XamlRoot())co_return;
		if (m_sequenceBusy || m_operationRunning || m_manualHeld) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Stop and confirm standstill before Sequence RESET." : L"운전을 정지하고 정지 확인 후 시퀀스 RESET을 실행하세요."); co_return;
		}
		m_sequenceResetAuthBusy = true;
		struct AuthGuard { bool& busy; ~AuthGuard() { busy = false; } }guard{ m_sequenceResetAuthBusy };
		auto expectedService = m_equipmentService;
		const bool english = ::DHLS_StrandJack::Services::Localization::IsEnglish();
		const auto expectedPassword = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetSettingsPassword();
		try {
			ContentDialog dialog; dialog.XamlRoot(XamlRoot());
			StackPanel title; title.Orientation(Orientation::Horizontal); title.Spacing(8);
			FontIcon icon; icon.Glyph(L"\xE946"); icon.FontSize(24); icon.Foreground(Solid(0x2563EB));
			TextBlock label; label.Text(english ? L"Operator verification · Sequence RESET" : L"작업자 확인 · 시퀀스 RESET");
			title.Children().Append(icon); title.Children().Append(label); dialog.Title(title);
			StackPanel content; content.Spacing(12);
			TextBlock guide; guide.Text(english ? L"Enter the operator password to reset the sequence." : L"시퀀스를 초기화하려면 작업자 비밀번호를 입력하세요."); guide.TextWrapping(TextWrapping::Wrap); content.Children().Append(guide);
			PasswordBox password; password.PlaceholderText(english ? L"Operator password" : L"작업자 비밀번호"); content.Children().Append(password);
			TextBlock error; error.Foreground(Solid(0xDC2626)); content.Children().Append(error);
			dialog.Content(content); dialog.PrimaryButtonText(english ? L"RESET" : L"초기화"); dialog.CloseButtonText(english ? L"Cancel" : L"취소"); dialog.DefaultButton(ContentDialogButton::Close);
			auto token = dialog.PrimaryButtonClick([password, error, english, expectedPassword](auto const&, auto const& args) {
				const bool valid = !expectedPassword.empty() && password.Password() == winrt::hstring(expectedPassword); password.Password(L"");
				if (!valid) { args.Cancel(true); error.Text(english ? L"Incorrect operator password." : L"작업자 비밀번호가 올바르지 않습니다."); ::DHLS_StrandJack::Services::AuditLog::Write("SEQUENCE_RESET_DENIED", "Invalid operator password"); }
				});
			const auto result = co_await dialog.ShowAsync(); dialog.PrimaryButtonClick(token); password.Password(L"");
			if (result != ContentDialogResult::Primary || !IsLoaded() || m_locked || expectedService != m_equipmentService)co_return;
			if (m_sequenceBusy || m_operationRunning || m_manualHeld)co_return;
			if (expectedService) { auto state = expectedService->Latest(); if (state.sequenceBusy || state.manualRunning || state.manualStopPending)co_return; }
			::DHLS_StrandJack::Services::AuditLog::Write("SEQUENCE_RESET_AUTHORIZED", "Operator verified");
			ForwardDiagnosticAction(L"ResetSequence");
		}
		catch (winrt::hresult_error const&) { ::DHLS_StrandJack::Services::AuditLog::Write("SEQUENCE_RESET_FAILED", "Operator verification dialog unavailable"); }
	}
	bool MainDashboardView::PortCanOperate(int set, int port, ::DHLS_StrandJack::Control::EquipmentSnapshot const& snapshot) const
	{
		const auto& state = m_setStates[set];
		if (!state.ports[port].selected || state.remoteMonitoring) return false;
		const auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		const auto pump = snapshot.pumpByIp.find(EquipmentService::PumpIpForPort(config, set, port));
		return pump != snapshot.pumpByIp.end() && pump->second;
	}

	bool MainDashboardView::CheckMovementRange() {
		if (!m_autoMode || m_pressureMode || !m_equipmentService)return true;
		auto snapshot = m_equipmentService->Latest();
		auto options = EquipmentService::FromModel(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig());
		bool resume = snapshot.sequencePhase == ::DHLS_StrandJack::Control::SequencePhase::Stopped && !snapshot.automaticResetRequired;
		if (resume)for (std::size_t s = 0; s < options.plcs.size(); ++s)for (std::size_t p = 0; p < options.plcs[s].channels.size(); ++p) {
			const bool selected = s < static_cast<std::size_t>(m_setCount) && p < static_cast<std::size_t>(m_portCount) && PortCanOperate(static_cast<int>(s), static_cast<int>(p), snapshot);
			if (s >= snapshot.plcs.size() || p >= snapshot.plcs[s].ports.size()) { resume = false; continue; }
			auto const& port = snapshot.plcs[s].ports[p]; if (selected != port.sequencePositionValid)resume = false;
			if (selected) {
				const double distance = port.sequenceTargetMm - port.sequenceOriginMm;
				if (std::abs(std::abs(distance) - m_displacementReferenceMm) > 1e-6 || (distance > 0) != m_movementLifting)resume = false;
			}
		}
		if (resume)return true; // An unchanged paused request keeps its original target, rather than adding the full distance again.
		double available = std::numeric_limits<double>::infinity(); int limitingSet = 0, limitingPort = 0;
		const auto now = EquipmentService::MonotonicMs();
		for (int s = 0; s < m_setCount; ++s)for (int p = 0; p < m_portCount; ++p)if (PortCanOperate(s, p, snapshot)) {
			if (s >= static_cast<int>(snapshot.plcs.size()) || p >= static_cast<int>(snapshot.plcs[s].ports.size()) || s >= static_cast<int>(options.plcs.size()) || p >= static_cast<int>(options.plcs[s].channels.size()))continue;
			auto const& plc = snapshot.plcs[s]; auto const& port = plc.ports[p]; auto const& channel = options.plcs[s].channels[p];
			if (!plc.usable || !plc.sampledAtMs || now - plc.sampledAtMs > static_cast<unsigned>(options.maxSampleAgeMs) || !(port.strokeDisplayValid || port.strokeValid))continue;
			const double position = snapshot.simulation ? port.absoluteMm : (channel.strokeSigned ? static_cast<std::int16_t>(port.rawStroke) : static_cast<double>(port.rawStroke)) * channel.strokeMmPerCount;
			const double remaining = ::DHLS_StrandJack::Control::AvailableTravelMm(position, channel.strokeLimitMm, !m_movementLifting);
			if (std::isfinite(remaining) && remaining < available) { available = remaining; limitingSet = s + 1; limitingPort = p + 1; }
		}
		if (!std::isfinite(available) || m_displacementReferenceMm <= available + 1e-9)return true;
		std::wostringstream text; text << std::fixed << std::setprecision(2);
		if (::DHLS_StrandJack::Services::Localization::IsEnglish())text << L"Requested travel: " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(m_displacementReferenceMm) << L" mm. Maximum available travel: " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(available) << L" mm.\nSET " << limitingSet << L" / PORT " << limitingPort;
		else text << L"요청 이동변위 " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(m_displacementReferenceMm) << L"mm는 잭 스트로크 범위를 초과합니다. 최대 " << ::DHLS_StrandJack::DisplayUnits::DisplacementText(available) << L"mm 이동 가능합니다.\nSET " << limitingSet << L" / PORT " << limitingPort;
		ShowControlWarningAsync(hstring{ text.str() }); return false;
	}
	void MainDashboardView::StartButton_Click(IInspectable const& sender, RoutedEventArgs const&)
	{
		if (!m_autoMode && (sender || !m_manualHeld))return; // Manual START is owned only by PointerPressed, never the synthesized Click.
		if (!::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load() ||
			(m_equipmentService && m_equipmentService->IsLiveMonitorOnly()))
		{
			EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(
				L"읽기 전용: PLC 운전 명령을 전송하지 않았습니다."));
			return;
		}

		if (::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()) {
			if (m_calibrationMode) { EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"Calibration sequence is not implemented")); return; }
			if (!m_autoMode && !m_manualHeld) { EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"Manual: hold START to run")); return; }
		}
		if (m_plcEmergencyActive)
		{
			EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"PLC 비상정지 신호 감지: 출력 정지 확인 필요"));
			return;
		}
		if (m_plcFaultActive) { ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Clear the PLC fault before restarting." : L"PLC 고장 비트가 OFF로 확인된 후 다시 시작하세요."); return; }
		if (m_locked || m_sequenceBusy || m_fullStopChosen) return;
		if (m_autoMode && !::DHLS_StrandJack::Models::JackMeasurementRangesMatch(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig())) {
			ShowControlWarningAsync(winrt::hstring{ ::DHLS_StrandJack::Models::JackMeasurementRangeWarning(::DHLS_StrandJack::Services::Localization::IsEnglish()) }); return;
		}
		if (m_equipmentService && m_equipmentService->Latest().automaticResetRequired) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Confirm the stopped state and use sequence RESET before restarting." : L"정지 상태를 확인하고 시퀀스 RESET 후 다시 시작하세요."); return;
		}
		if (m_stateSimulation && m_autoMode && !m_pressureMode && m_equipmentService &&
			m_equipmentService->Latest().mockJobFinished) {
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::Text(L"목표 도달 · 일시정지"));
			return;
		}
		if (m_startupMenuMode != 3 && !(m_stateSimulation && m_autoMode && !m_pressureMode && !m_calibrationMode))
			try { if (!CheckMovementRange())return; }
		catch (std::exception const&) { ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish() ? L"Check the jack stroke and sensor scale settings." : L"잭 스트로크와 변위 스케일 설정을 확인하세요."); return; }
		if (!m_equipmentIntegrationActive) {
			m_pauseStopRequested = m_fullStopChosen = false;
			SetOperationRunning(true);
		}
		RaiseAction(L"StartRequested");
	}
	void MainDashboardView::StrandReadyButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!m_equipmentService || m_locked || m_sequenceBusy || m_operationRunning) return;
		if (m_startupMenuMode == 3) { RaiseAction(L"PreparePretension"); return; }
		if (m_stateSimulation && m_autoMode && !m_pressureMode && !m_calibrationMode)
			RaiseAction(L"PrepareStrandJob");
	}
	void MainDashboardView::CalibrationPressureZero_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_calibrationMode && !m_locked && !m_sequenceBusy && !m_operationRunning)
			RaiseAction(L"CalibrationPressureZero");
	}
	void MainDashboardView::CalibrationStrokeZero_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_calibrationMode && !m_locked && !m_sequenceBusy && !m_operationRunning)
			RaiseAction(L"CalibrationStrokeZero");
	}
	void MainDashboardView::EndManualHold()
	{
		if (!m_manualHeld)return;
		m_manualHeld = false;
		StartButton().ReleasePointerCaptures();
		FooterManualUp().ReleasePointerCaptures();
		FooterManualDown().ReleasePointerCaptures();
		StopButton_Click(nullptr, nullptr);
	}
	void MainDashboardView::StopButton_Click(IInspectable const& sender, RoutedEventArgs const&)
	{
		if (m_fullStopChosen || (sender && m_pauseStopRequested)) return;
		if (m_operationRunning || m_sequenceBusy)m_pauseStopRequested = true;
		m_stopSelected = true;
		SetOperationRunning(false);
		RaiseAction(L"StopRequested");
	}
	bool MainDashboardView::CanHoldForFullStop() const
	{
		if (!m_pauseStopRequested || m_fullStopChosen || m_locked || m_plcEmergencyActive ||
			!m_equipmentService || m_equipmentService->IsLiveMonitorOnly()) return false;
		const auto snapshot = m_equipmentService->Latest();
		using ::DHLS_StrandJack::Control::SequencePhase;
		return !snapshot.sequenceBusy && snapshot.sequenceStopConfirmed &&
			(snapshot.sequencePhase == SequencePhase::Stopped ||
			 (snapshot.mockJobFinished && snapshot.sequencePhase == SequencePhase::Completed));
	}

	void MainDashboardView::UpdateFullStopHold()
	{
		if (!m_fullStopPressedAt) return;
		if (::GetForegroundWindow() != MainWindow::GetHwnd() || !CanHoldForFullStop())
		{
			m_fullStopPressedAt = 0;
			return;
		}
		if (EquipmentService::MonotonicMs() - m_fullStopPressedAt < 3000) return;
		m_fullStopPressedAt = 0;
		m_fullStopChosen = m_stopSelected = true;
		SetOperationRunning(false);
		RaiseAction(L"FullStopRequested");
		EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"완전 정지 · 재시작하려면 시퀀스 RESET"));
		SaveDashboardMemory();
		RefreshLockState();
	}

	void MainDashboardView::EmergencyButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load() ||
			(m_equipmentService && m_equipmentService->IsLiveMonitorOnly()))
		{
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"PLC monitoring only. No emergency command sent. Use the equipment emergency stop."
				: L"PLC 감시 전용입니다. 비상정지 명령은 전송되지 않았습니다. 장비의 비상정지 버튼을 사용하세요.");
			return;
		}
		m_pcEmergencyRequested = m_plcEmergencyActive = true;
		m_diagnosticStartRequested = false;
		m_stopSelected = true;
		SetOperationRunning(false);
		EmergencySignalOverlay().Visibility(Visibility::Visible);
		// The app session is already connected when this screen is loaded.
		RaiseAction(L"EmergencyRequested");
		if (!m_equipmentService)
			EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"비상정지 전송 실패: PLC 연결을 확인하세요."));
	}

	void MainDashboardView::RefreshOperationButtons()
	{
		StartButton().Background(m_operationRunning
			? Resources().Lookup(box_value(L"ActiveGreen")).as<Brush>()
			: Solid(0xF0F0F0).as<Brush>());
		StopButton().Background(Solid(m_stopSelected ? 0xFF2020 : 0xF0F0F0));
		auto const label = ::DHLS_StrandJack::Services::Localization::Text(m_fullStopChosen ? L"정지" : L"일시정지");
		if (unbox_value_or<hstring>(StopButton().Content(), L"") != label)
			StopButton().Content(box_value(label));
	}

	void MainDashboardView::RefreshDashboardLog()
	{
		using L = ::DHLS_StrandJack::Services::Localization;
		using Audit = ::DHLS_StrandJack::Services::AuditLog;
		if (m_simulationLogEnglish != L::IsEnglish())
		{
			m_simulationLogEnglish = L::IsEnglish();
			m_simulationLogRevision = 0;
		}
		std::vector<Audit::Event> events;
		if (!Audit::ReadDashboardEvents(m_simulationLogRevision, events)) return;
		auto rows = DashboardEventRows().Children();
		rows.Clear();
		for (auto const& event : events)
		{
			if (event.at < m_dashboardLogOpenedAt ||
				(event.category.rfind("SIM_", 0) == 0) != m_stateSimulation) continue;
			const auto split = event.message.find(" | ");
			auto message = L::Text(to_hstring(event.message.substr(0, split)).c_str());
			constexpr std::string_view cycleStartPrefix = "Both clamps closed | Cycle=";
			if (event.category == "SIM_STEP" && event.message.rfind(cycleStartPrefix, 0) == 0)
			{
				const auto cycle = to_hstring(event.message.substr(cycleStartPrefix.size()));
				message = L::IsEnglish() ? L"Cycle " + cycle + L" start · " + message
					: cycle + L"사이클 시작 · " + message;
			}
			if ((event.category=="RUN_RESULT"||event.category=="SIM_RUN_RESULT")&&split!=std::string::npos)
				message = message + to_hstring(event.message.substr(split,event.message.find(" | ",split+3)-split));
			const bool warning=event.category=="SIM_WARNING"||event.category=="PLC_FAULT_STOP"||
				event.category=="LOAD_TOLERANCE"||event.message.find(" | State: Fault |")!=std::string::npos||
				event.message.find(" | State: Emergency |")!=std::string::npos||
				(event.category=="SIM_PRESSURE_STATE"&&event.message.rfind("Completed:",0)!=0);
			if(warning)message=L::Text(L"경고")+L" · "+message;
			const bool primary = warning || event.category == "SIM_STEP" || event.category=="SIM_JOB_START" ||
				event.category == "SIM_RUN_RESULT" || event.category == "RUN_RESULT";
			rows.Append(DashboardLogRow(hstring{ DashboardLogTime(event.at) }, message, primary));
		}
		auto scroll = DashboardLogScroll();
		scroll.UpdateLayout();
		scroll.ChangeView(nullptr, box_value(scroll.ScrollableHeight()).as<IReference<double>>(), nullptr, true);
	}

	void MainDashboardView::RefreshDashboardClocks()
	{
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm local{};
		if (localtime_s(&local, &now) == 0)
		{
			std::wostringstream clock;
			clock << std::put_time(&local, L"%H:%M:%S");
			const hstring value{ clock.str() };
			if (DashboardCurrentTime().Text() != value) DashboardCurrentTime().Text(value);
		}
		const auto elapsedMs = m_operationRunning ? EquipmentService::MonotonicMs() - m_runStartedMs : m_runElapsedMs;
		const hstring value{ DashboardRunTime(elapsedMs, false) };
		if (DashboardElapsedTime().Text() != value) DashboardElapsedTime().Text(value);
	}

	void MainDashboardView::SetOperationRunning(bool running)
	{
		// UI-thread update hook for a host's confirmed operation state.
		const bool starting = running && !m_operationRunning;
		const bool operationChanged = running != m_operationRunning;
		if (starting)
		{
			m_runStartedMs = EquipmentService::MonotonicMs();
			for (auto& set : m_setStates)
				for (auto& port : set.ports)
				{
					port.hasTravelOrigin = port.selected && std::isfinite(port.absoluteMm);
					port.travelOriginMm = port.absoluteMm;
					port.travelLifting = m_movementLifting;
				}
		}
		else if (!running && m_operationRunning)
		{
			m_runElapsedMs = EquipmentService::MonotonicMs() - m_runStartedMs;
			::DHLS_StrandJack::Services::AuditLog::Write(m_stateSimulation ? "SIM_RUN_RESULT" : "RUN_RESULT",
				"Run time result | " + to_string(hstring{ DashboardRunTime(m_runElapsedMs, true) }) +
				" | Dashboard: " + to_string(OperationModeTitle()) +
				" | State: " + (m_plcEmergencyActive ? "emergency" : m_plcFaultActive ? "fault" :
					m_cycleCompleted ? "sequence completed" : "running state ended"));
		}
		m_operationRunning = running;
		if (operationChanged) RefreshLockState();
		if (running) m_stopSelected = false;
		RefreshOperationButtons();
		RefreshMotionArrows(starting);
	}
#undef DASHBOARD_ACTION_HANDLER

	void MainDashboardView::DisplacementModeButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!CanChangeControlMode()) return;
		if (m_locked || m_sequenceBusy) return;
		m_pressureMode = false; RefreshModeButtons(); RefreshOperationPanel(); RaiseAction(L"ModeDisplacement");
	}
	void MainDashboardView::PressureModeButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_calibrationMode) return;
		if (!CanChangeControlMode()) return;
		if (m_locked || m_sequenceBusy) return;
		m_pressureMode = true; RefreshModeButtons(); RefreshOperationPanel(); RaiseAction(L"ModePressure");
	}
	void MainDashboardView::AutoModeButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_calibrationMode) return;
		if (!CanChangeControlMode()) return;
		if (m_locked || m_sequenceBusy) return;
		m_autoMode = true; RefreshModeButtons(); RaiseAction(L"ModeAuto");
	}
	void MainDashboardView::ManualModeButton_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (!CanChangeControlMode()) return;
		if (m_locked || m_sequenceBusy) return;
		m_autoMode = false; RefreshModeButtons(); RaiseAction(L"ModeManual");
	}

	uint32_t MainDashboardView::GroupColor(int32_t group) noexcept
	{
		// Same values as JackArrangementView.html getColorHex().
		switch (group)
		{
		case 1: return 0xFF2020;  // red
		case 2: return 0x00E534;  // green
		case 3: return 0xF5EB00;  // yellow
		case 4: return 0x00DDE6;  // cyan
		default: return 0x888888;
		}
	}

	void MainDashboardView::ResetPortSelections()
	{
		// A changed arrangement reassigns physical jack coordinates. Previously
		// selected ports must NEVER remain selected by accident after remapping.
		for (auto& set : m_setStates)
			for (auto& port : set.ports)
				port.selected = false;
	}

	int32_t MainDashboardView::ActiveArrangementTemplate() const noexcept
	{
		return m_arrangementTemplate;
	}

	void MainDashboardView::UpdatePortLayout(int32_t setCount, int32_t columns)
	{
		// Backward-compatible free SET layout. Does not impersonate an
		// arrangement template. Prefer ApplyJackArrangementTemplate for DHLS.
		const int32_t nextColumns = columns == 2 ? 2 : 1;
		const int32_t nextCount = std::clamp(setCount, 1, nextColumns * 4);
		const int32_t nextPorts = (std::clamp)(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings.Sensor_HPU_Count, 1, 8);
		bool changed = m_arrangementTemplate != 0 || m_portCount != nextPorts ||
			m_setCount != nextCount || m_columns != nextColumns;
		m_arrangementTemplate = 0;
		m_columns = nextColumns;
		m_setCount = nextCount;
		m_rows = (m_setCount + m_columns - 1) / m_columns;
		m_portCount = nextPorts;
		m_portColumns = m_portCount;
		m_portRows = 1;
		RenderPortSets();
		if (changed) RaiseAction(L"ArrangementLayoutChanged:Legacy");
	}

	void MainDashboardView::ApplyJackArrangementTemplate(int32_t templateId)
	{
		if (templateId != ::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(templateId))
			throw hresult_invalid_argument(L"Invalid arrangement templateId.");

		// General settings and JackArrangementView share SET/PORT indices.
		// T1: one line, ports 1..8. T2: left/right SETs, ports 1..4 above 5..8.
		// MainDashboard's SET = group, PORT = number.
		auto const& plc = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings;
		const int templateSets = templateId == 1 ? 1 : 2;
		const int configuredJacks = (std::clamp)(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.DashboardJackCount, 1, 16);
		const int portsPerPlc = (std::clamp)(plc.Sensor_HPU_Count, 1, 8);
		const int nextSets = (std::min)((std::min)((std::clamp)(plc.PLC_Count, 1, 4), templateSets),
			(configuredJacks + portsPerPlc - 1) / portsPerPlc);
		const int nextPorts = (std::min)(portsPerPlc, configuredJacks);
		if (m_arrangementTemplate == templateId && m_setCount == nextSets && m_portCount == nextPorts) return;
		m_arrangementTemplate = templateId;
		m_setCount = nextSets;
		m_portCount = nextPorts;
		m_columns = (std::min)(templateId == 2 ? 2 : 1, m_setCount);
		m_rows = (m_setCount + m_columns - 1) / m_columns;
		m_portColumns = (std::min)(templateId == 2 ? 4 : 8, m_portCount);
		m_portRows = (m_portCount + m_portColumns - 1) / m_portColumns;

		RenderPortSets();
		RaiseAction(L"ArrangementApplied:" + std::to_wstring(templateId));
	}

	void MainDashboardView::ApplyConfirmedArrangementJson(hstring const& arrangementJson)
	{
		// Contract comes from JackArrangementView.ConfirmedArrangementJson.
		// Validate the ENTIRE deterministic cell mapping before changing UI.
		// Rejects stale/partial/custom JSON rather than silently miswiring ports.
		using namespace Windows::Data::Json;
		JsonObject snapshot = JsonObject::Parse(arrangementJson);
		if (!snapshot.HasKey(L"templateId") || !snapshot.HasKey(L"cylinders"))
			throw hresult_invalid_argument(L"Arrangement JSON needs templateId and cylinders.");

		const double numericId = snapshot.GetNamedNumber(L"templateId");
		if (numericId != ::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(numericId))
			throw hresult_invalid_argument(L"Invalid arrangement templateId.");
		const auto templateId = static_cast<int32_t>(numericId);

		JsonArray cylinders = snapshot.GetNamedArray(L"cylinders");
		const int32_t rows = templateId == 1 ? 1 : 2;
		const bool stacked = templateId == ::DHLS_StrandJack::Models::StackedTwoLineTemplate;
		if (cylinders.Size() != static_cast<uint32_t>(rows * 8))
			throw hresult_invalid_argument(L"Arrangement cylinder count does not match the template.");

		constexpr wchar_t const* colors[] = { L"red", L"green", L"yellow", L"cyan" };
		for (int32_t row = 0; row < rows; ++row)
		{
			for (int32_t col = 0; col < 8; ++col)
			{
				const auto cell = cylinders.GetAt(row * 8 + col).GetObject();
				const int32_t group = stacked ? row + 1 : templateId == 2 ? (col < 4 ? 1 : 2) : 1;
				const int32_t number = templateId == 2 ? row * 4 + col % 4 + 1 : col + 1;
				const hstring expectedId = to_hstring(row) + L"-" + to_hstring(col);
				const hstring expectedLabel = to_hstring(group) + L"-" + to_hstring(number);
				if (cell.GetNamedString(L"id") != expectedId ||
					cell.GetNamedString(L"label") != expectedLabel ||
					cell.GetNamedString(L"color") != colors[group - 1] ||
					!cell.GetNamedBoolean(L"active"))
				{
					throw hresult_invalid_argument(L"Arrangement JSON cell mapping differs from JackArrangementView.");
				}
			}
		}
		ApplyJackArrangementTemplate(templateId);
	}

	void MainDashboardView::RenderPortSets()
	{
		auto host = PortSetHost();
		auto clock = DashboardClockPanel();
		auto clockHost = DashboardClockHost();
		if (auto parent = clock.Parent().try_as<Panel>(); parent && parent != clockHost.as<Panel>())
		{
			uint32_t index{};
			if (parent.Children().IndexOf(clock, index)) parent.Children().RemoveAt(index);
			clockHost.Children().Append(clock);
		}
		host.Children().Clear();
		host.ColumnDefinitions().Clear();
		host.RowDefinitions().Clear();
		m_lockablePortControls.clear(); m_lockableSpeedControls.clear();
		m_setVisuals.clear();
		m_setVisuals.resize(m_setCount);
		RefreshFooterSpeedControls();

		for (int col = 0; col < m_columns; ++col) ColumnStar(host);
		for (int row = 0; row < m_rows; ++row)
		{
			if (m_rows == 1 && m_arrangementTemplate == 0) RowPixels(host, 400); // original one-row fixed height
			else RowStar(host);
		}
		for (int i = 0; i < m_setCount; ++i)
		{
			auto box = CreatePortSet(i + 1, m_setVisuals[i]);
			box.Margin(Thickness{ 4,4,4,4 });
			Grid::SetColumn(box, i % m_columns);
			Grid::SetRow(box, i / m_columns);
			host.Children().Append(box);
			RefreshAllButton(i + 1);
			RefreshSetLamps(i + 1);
			RefreshSetTotal(i + 1);
		}
		RefreshHpuButtons();
	}

	Border MainDashboardView::CreatePortSet(int32_t setNumber, SetVisual& view)
	{
		auto body = CreateDashboardPanel(Solid(0x888888), 8);
		body.BorderBrush(Solid(GroupColor(setNumber)));
		body.BorderThickness(Thickness{ 2,2,2,2 });
		Grid grid;
		RowPixels(grid, m_rows < 3 ? 40 : 32);
		RowStar(grid);
		grid.Children().Append(CreateToolbar(setNumber, view));

		Grid ports;
		ports.Background(Solid(0x888888));
		ports.Margin(Thickness{ 2,2,2,2 });
		for (int c = 0; c < m_portColumns; ++c) ColumnStar(ports);
		for (int r = 0; r < m_portRows; ++r) RowStar(ports);
		for (int p = 1; p <= m_portCount; ++p)
		{
			// Template 4: each SET has ports 1..4 above ports 5..8.
			auto card = CreatePortCard(setNumber, p, view);
			if ((setNumber - 1) * m_portCount + p > (std::clamp)(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.DashboardJackCount, 1, 16))
				card.Visibility(Visibility::Collapsed);
			card.Margin(Thickness{ 2,2,2,2 });
			Grid::SetColumn(card, (p - 1) % m_portColumns);
			Grid::SetRow(card, (p - 1) / m_portColumns);
			ports.Children().Append(card);
		}
		Grid::SetRow(ports, 1);
		grid.Children().Append(ports);
		body.Child(grid);
		return body;
	}

	Grid MainDashboardView::CreateToolbar(int32_t setNumber, SetVisual& view)
	{
		Grid outer;
		outer.Background(Vertical(0x686868, 0x383838));
		outer.Padding(Thickness{ 4,1,4,1 });

		// One native toolbar, scaled uniformly when two sets share a narrow width.
		StackPanel strip;
		strip.Orientation(Orientation::Horizontal);
		strip.Spacing(6);
		strip.Height(m_rows < 3 ? 36 : 28);

		// Compact status group: caption above a small shaded round lamp.
		auto status = [&](hstring const& title, Border& lamp, double width)
			{
				StackPanel column;
				column.Spacing(1);
				column.Width(width);
				auto caption = CreateDashboardText(title, 9, 0xFFFFFF);
				column.Children().Append(caption);
				lamp = CreateDashboardPanel(Vertical(0xAAAAAA, 0x252525), 7);
				lamp.Width(14); lamp.Height(14);
				lamp.BorderBrush(Solid(0x111111));
				lamp.BorderThickness(Thickness{ 1,1,1,1 });
				lamp.HorizontalAlignment(HorizontalAlignment::Center);
				column.Children().Append(lamp);
				return column;
			};
		StackPanel indicators;
		indicators.Orientation(Orientation::Horizontal);
		indicators.Spacing(3);
		indicators.VerticalAlignment(VerticalAlignment::Center);
		status(L"PLC" + to_hstring(setNumber), view.plcLamp, 27);

		// Retain feedback objects for state updates; these indicators are not displayed here.
		status(L"HPU", view.hpuLamp, 27);
		if (m_arrangementTemplate == 2 && setNumber == 2)
		{
			auto clock = DashboardClockPanel();
			auto children = DashboardClockHost().Children();
			uint32_t index{};
			if (children.IndexOf(clock, index)) children.RemoveAt(index);
			indicators.Children().Append(clock);
		}
		indicators.Children().Append(status(L"ERROR", view.errorLamp, 38));

		view.allCheckBox = CheckBox{};
		view.allCheckBox.Content(box_value(hstring{ L"All" }));
		view.allCheckBox.IsThreeState(true);
		view.allCheckBox.FontSize(11);
		view.allCheckBox.Foreground(Solid(0xFFFFFF));
		view.allCheckBox.Background(Solid(0x000000));
		view.allCheckBox.BorderBrush(Solid(0x555555));
		view.allCheckBox.MinWidth(0); view.allCheckBox.MinHeight(0);
		view.allCheckBox.Padding(Thickness{ 0,0,0,0 });
		view.allCheckBox.Click([weak = get_weak(), setNumber](IInspectable const&, RoutedEventArgs const&)
			{
				auto self = weak.get(); if (!self) return;
				self->ToggleAll(setNumber);
			});
		Viewbox allBox;
		allBox.Height(24); allBox.Width(46);
		allBox.Stretch(Microsoft::UI::Xaml::Media::Stretch::Uniform);
		RegisterLockablePortControl(view.allCheckBox);
		allBox.Child(view.allCheckBox);
		indicators.Children().Append(allBox);
		strip.Children().Append(indicators);

		auto addSpeed = [&](hstring const& name, bool lifting, TextBlock& output)
			{
				Grid speed;
				speed.Width(m_columns == 2 ? 146 : 184);
				speed.Height(32);
				speed.Visibility(Visibility::Collapsed); // Speed editing is hosted in BottomOperationPanel.
				speed.Background(Solid(0x444444));
				ColumnPixels(speed, lifting ? 50 : 55);
				ColumnPixels(speed, 32);
				ColumnStar(speed);
				auto label = CreateDashboardText(name, 9, 0xFFFFFF);
				speed.Children().Append(label);
				Border num = CreateDashboardPanel(Solid(0xFFFFFF), 3);
				output = CreateDashboardText(to_hstring(lifting ? m_setStates[setNumber - 1].liftingSpeed
					: m_setStates[setNumber - 1].loweringSpeed),
					12, 0x111111, true);
				num.Child(output);
				num.Margin(Thickness{ 1,4,1,4 });
				Grid::SetColumn(num, 1);
				speed.Children().Append(num);
				Slider slider;
				slider.Minimum(20); slider.Maximum(60); slider.StepFrequency(1);
				slider.Value(lifting ? m_setStates[setNumber - 1].liftingSpeed
					: m_setStates[setNumber - 1].loweringSpeed);
				slider.Margin(Thickness{ 1,0,1,0 });
				slider.VerticalAlignment(VerticalAlignment::Center);
				slider.ValueChanged([weak = get_weak(), setNumber, lifting, output](IInspectable const& sender, auto const& args)
					{
						auto self = weak.get(); if (!self) return;
						if (self->m_locked) return;
						int value = static_cast<int>(std::lround(args.NewValue()));
						auto& set = self->m_setStates[setNumber - 1];
						if (self->m_sequenceBusy && !self->m_automaticRunning)
						{
							const int configured = lifting ? set.liftingSpeed : set.loweringSpeed;
							output.Text(to_hstring(configured));
							auto source = sender.as<Slider>(); if (source.Value() != configured)source.Value(configured);
							self->EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"운전 중에는 속도를 변경할 수 없습니다."));
							return;
						}
						if (value < 20 || value>60) { auto source = sender.as<Slider>(); source.Value(lifting ? set.liftingSpeed : set.loweringSpeed); return; }
						if (value == (lifting ? set.liftingSpeed : set.loweringSpeed))return;
						if (lifting) set.liftingSpeed = value;
						else set.loweringSpeed = value;
						output.Text(to_hstring(value));
						if (setNumber == 1)
							(lifting ? self->FooterLiftSlider() : self->FooterLowerSlider()).Value(value);
						self->RaiseAction(std::wstring(lifting ? L"LiftingSpeed:" : L"LoweringSpeed:") +
							std::to_wstring(setNumber) + L":" + std::to_wstring(value));
					});
				slider.IsEnabled(!m_locked && (!m_sequenceBusy || m_automaticRunning)); m_lockableSpeedControls.push_back(slider);
				Grid::SetColumn(slider, 2);
				speed.Children().Append(slider);
				strip.Children().Append(speed);
			};
		addSpeed(L"LIFTING\nSPEED (Hz)", true, view.liftingSpeed);
		addSpeed(L"LOWERING\nSPEED (Hz)", false, view.loweringSpeed);

		view.total = CreateDashboardText(OneDecimal(::DHLS_StrandJack::DisplayUnits::Weight(m_setStates[setNumber - 1].totalTon)), 12, 0x111111, true);
		if (m_arrangementTemplate != 0)
		{
			// Direct correspondence with JackArrangementView group colors.
			Border marker = CreateDashboardPanel(Solid(GroupColor(setNumber)), 2);
			marker.Width(9); marker.Height(22);
			marker.VerticalAlignment(VerticalAlignment::Center);
			strip.Children().Append(marker);
		}
		strip.Children().Append(CreateDashboardText(hstring(L"SET ") + to_hstring(setNumber), 12, 0xFFFFFF, true));

		Viewbox fitter;
		fitter.Stretch(Stretch::Uniform);
		fitter.StretchDirection(StretchDirection::DownOnly);
		fitter.HorizontalAlignment(m_columns == 2 && setNumber == 2
			? HorizontalAlignment::Right : HorizontalAlignment::Left);
		fitter.VerticalAlignment(VerticalAlignment::Center);
		fitter.Child(strip);
		outer.Children().Append(fitter);
		return outer;
	}

	Button MainDashboardView::CreatePortCard(int32_t setNumber, int32_t portNumber, SetVisual& setView)
	{
		using namespace Microsoft::UI::Xaml::Shapes;
		auto& view = setView.ports[portNumber - 1];
		Canvas module;
		module.Width(236); module.Height(540);
		const auto place = [](Canvas const& canvas, UIElement const& item, double x, double y)
			{
				Canvas::SetLeft(item, x); Canvas::SetTop(item, y);
				canvas.Children().Append(item);
			};
		const auto box = [&](Canvas const& canvas, double x, double y, double w, double h, Brush const& brush, bool outlined = true)
			{
				auto border = CreateDashboardPanel(brush);
				border.Width(w); border.Height(h);
				if (outlined) { border.BorderBrush(Solid(0x111111)); border.BorderThickness(Thickness{ 1,1,1,1 }); }
				place(canvas, border, x, y);
				return border;
			};
		const auto label = [&](Canvas const& canvas, hstring const& text, double x, double y, double w, double size, uint32_t color, bool bold = false)
			{
				auto item = CreateDashboardText(text, size, color, bold);
				item.Width(w);
				place(canvas, item, x, y);
				return item;
			};
		auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		const size_t index = (setNumber - 1) * config.PLCSettings.Sensor_HPU_Count + portNumber - 1;
		auto title = index < config.JackSetupDetails.size() ? config.JackSetupDetails[index].JackTitle : std::wstring{};
		if (title.empty()) title = L"PORT " + std::to_wstring(portNumber);
		label(module, hstring{ title }, 8, 5, 220, 25, 0x001532, true);
		view.oilTemperature = label(module, L"Oil T: -- °C", 11, 41, 115, 11, 0x111111, true);
		view.oilTemperature.TextAlignment(TextAlignment::Left);
		view.pumpLabel = label(module, L"PUMP --", 130, 42, 57, 11, 0x111111, true);
		view.pumpPill = box(module, 186, 39, 38, 18, Vertical(0xF2F2F2, 0xA8A8A8));
		view.pumpPill.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ 9,9,9,9 });
		view.pumpLed = CreateDashboardPanel(Solid(0x444444), 7);
		view.pumpLed.Width(14); view.pumpLed.Height(14); view.pumpLed.Margin(Thickness{ 1,0,1,0 });
		view.pumpLed.VerticalAlignment(VerticalAlignment::Center);
		view.pumpLed.BorderBrush(Solid(0x111111));
		view.pumpLed.BorderThickness(Thickness{ 1,1,1,1 });
		view.pumpPill.Child(view.pumpLed);
		view.pumpPill.PointerEntered([](IInspectable const& sender, auto const&)
			{
				sender.as<Border>().Background(Solid(0x808080));
			});
		view.pumpPill.PointerExited([](IInspectable const& sender, auto const&)
			{
				sender.as<Border>().Background(Vertical(0xF2F2F2, 0xA8A8A8));
			});
		view.pumpPill.PointerPressed([weak = get_weak(), setNumber, portNumber](IInspectable const& sender, auto const& args)
			{
				if (!args.GetCurrentPoint(sender.as<Border>()).Properties().IsLeftButtonPressed()) return;
				args.Handled(true); // The switch must not also toggle selection on the enclosing jack button.
				if (auto self = weak.get()) self->TogglePumpForPort(setNumber, portNumber);
			});

		view.visual = box(module, 8, 63, 220, 318, Solid(0xD1D5DB));
		Canvas picture;
		view.visual.Child(picture);
		view.strandBrush = Solid(0xA0A0A0);
		for (int wire = 0; wire < 5; ++wire)
		{
			Line strand;
			strand.X1(75 + 16.5 * wire);
			strand.X2(strand.X1());
			strand.Y1(7); strand.Y2(114);
			strand.Stroke(view.strandBrush);
			strand.StrokeThickness(2);
			DoubleCollection dashes;
			dashes.Append(3); dashes.Append(2);
			strand.StrokeDashArray(dashes);
			picture.Children().Append(strand);
			view.strands[wire] = strand;
		}

		view.upperAssembly = Canvas{};
		view.upperAssembly.Width(218); view.upperAssembly.Height(137);
		place(picture, view.upperAssembly, 0, 56);
		auto const& upper = view.upperAssembly;
		const auto bodyPart = [&](Canvas const& target, double x, double y, double w, double h)
			{
				auto part = box(target, x, y, w, h, Solid(0x52525B));
				view.cylinderParts.push_back(part);
				return part;
			};
		const auto clamp = [&](Canvas const& target, int index, double x, double y, double width, double height)
			{
				view.clampBodies[index] = box(target, x, y, width, height, Vertical(0x4F565E, 0x3A4047));
				view.clampLabels[index] = label(target, L"--", x, y + 4, width, 12, 0xFFFF99);
				const float left = static_cast<float>(x), right = static_cast<float>(x + width);
				const float mid = static_cast<float>(y + height / 2);
				auto makeArrow = [&](float tip, float tail, Brush const& fill)
					{
						const float shoulder = tip + (tail - tip) * 0.43f;
						PointCollection points;
						points.Append(Point{ tip, mid });
						points.Append(Point{ shoulder, mid - 6 });
						points.Append(Point{ tail, mid - 6 });
						points.Append(Point{ tail, mid + 6 });
						points.Append(Point{ shoulder, mid + 6 });
						Polygon arrow;
						arrow.Points(points);
						arrow.Fill(fill);
						arrow.Stroke(Solid(0x111111));
						arrow.StrokeThickness(1);
						target.Children().Append(arrow);
						return arrow;
					};
				const auto closedFill = Vertical(0x008B00, 0x70FF00);
				const auto openFill = Vertical(0xC45A00, 0xFFD053);
				constexpr float arrowLength = 14;
				view.clampArrows[index] = {
						makeArrow(left, left - arrowLength, closedFill), makeArrow(right, right + arrowLength, closedFill),
						makeArrow(left - arrowLength, left, openFill), makeArrow(right + arrowLength, right, openFill)
				};
				for (auto const& arrow : view.clampArrows[index]) arrow.Visibility(Visibility::Collapsed);
			};
		bodyPart(upper, 64, 0, 90, 36);
		bodyPart(upper, 43, 36, 132, 25);
		view.cutouts = { box(upper, 43, 39, 15, 19, Solid(0xFFFFFF), false),
										box(upper, 160, 39, 15, 19, Solid(0xFFFFFF), false) };
		clamp(upper, 0, 58, 36, 102, 25);
		bodyPart(upper, 64, 61, 90, 8);
		bodyPart(upper, 56, 69, 106, 8);
		view.pistonRod = box(upper, 58, 77, 102, 4, Horizontal(0x5A5A5A, 0xFFFFFF, 0x4A4A4A, 0xADADAD, 0.25));

		Canvas lower;
		lower.Width(218); lower.Height(318);
		place(picture, lower, 0, 0);
		bodyPart(lower, 48, 137, 122, 118);
		clamp(lower, 1, 60, 255, 98, 27);
		bodyPart(lower, 48, 282, 122, 32);
		size_t outlineIndex = 0;
		for (double y : {287.0, 289.0})
			for (double x : {47.0, 49.0})
				view.engineOutline[outlineIndex++] = label(lower, L"--", x, y, 122, 14, 0x000000, true);
		view.enginePercent = label(lower, L"--", 48, 288, 122, 14, 0xFFFFFF, true);
		view.cornerLed = box(picture, 5, 5, 11, 11, Solid(0x555555));
		view.cornerLed.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ 5.5,5.5,5.5,5.5 });

		view.motionIcon = Canvas{};
		view.motionIcon.Width(64); view.motionIcon.Height(62);
		view.motionDirection = RotateTransform{};
		view.motionIcon.RenderTransformOrigin(Point{ 0.5f, 0.5f });
		view.motionIcon.RenderTransform(view.motionDirection);
		view.motionGreen = Vertical(0x13E600, 0x008A00);
		view.motionOrange = Vertical(0xFFB32C, 0xDE6800);
		const auto motionPolygon = [&](Canvas const& canvas, std::initializer_list<Point> vertices)
			{
				Polygon shape;
				PointCollection points;
				for (auto const& point : vertices) points.Append(point);
				shape.Points(points);
				shape.Fill(view.motionGreen);
				shape.Stroke(Solid(0x123300));
				shape.StrokeThickness(1.5);
				canvas.Children().Append(shape);
				return shape;
			};
		view.slowArrow = motionPolygon(view.motionIcon, { {32,12}, {48,49}, {32,38}, {16,49} });
		view.fastArrow = Canvas{};
		view.motionIcon.Children().Append(view.fastArrow);
		motionPolygon(view.fastArrow, { {32,2}, {62,22}, {47,22}, {47,41}, {32,34}, {17,41}, {17,22}, {2,22} });
		motionPolygon(view.fastArrow, { {17,44}, {32,37}, {47,44}, {47,50}, {32,43}, {17,50} });
		motionPolygon(view.fastArrow, { {17,53}, {32,46}, {47,53}, {47,59}, {32,52}, {17,59} });
		place(lower, view.motionIcon, 77, 151);
		view.motionIcon.Visibility(Visibility::Collapsed);
		view.remoteBadge = box(lower, 56, 225, 106, 20, Solid(0x0646B5));
		view.remoteBadge.Child(CreateDashboardText(L"REMOTE", 13, 0xFFFF00, true));
		view.remoteBadge.Visibility(Visibility::Collapsed);

		Canvas telemetry;
		telemetry.Width(236); telemetry.Height(540);
		place(module, telemetry, 0, 0);
		const std::array<hstring, 4> names{
				L"현재 변위", L"현재 압력", L"현재 하중", L"누적 거리" };
		const std::array<std::wstring, 4> units{
				L"mm", ::DHLS_StrandJack::DisplayUnits::PressureLabel(),
				::DHLS_StrandJack::DisplayUnits::WeightLabel(), L"mm" };
		for (size_t row = 0; row < names.size(); ++row)
		{
			const double y = 388.0 + 37.0 * static_cast<double>(row);
			auto caption = box(telemetry, 8, y, 74, 34, Solid(0x525252));
			auto text = CreateDashboardText(names[row], 12, 0xFFFFFF);
			::DHLS_StrandJack::Services::Localization::RegisterFormattedText(text, names[row].c_str(), L"\n(" + units[row] + L")");
			caption.Child(text);
			auto reading = box(telemetry, 81, y, 105, 34, Solid(0xFFFFFF));
			view.metricValues[row] = CreateDashboardText(L"--", 20, 0x000000);
			view.metricValues[row].TextAlignment(TextAlignment::Right);
			view.metricValues[row].Margin(Thickness{ 0,0,8,0 });
			reading.Child(view.metricValues[row]);
		}
		box(telemetry, 190, 388, 38, 145, Solid(0xE5FAFF));
		view.loadPercent = label(telemetry, L"하중\n--", 191, 390, 36, 10, 0x001532, true);
		box(telemetry, 193, 419, 32, 104, Solid(0x262626));
		for (int y : {445, 471, 497})
			for (int x = 195; x < 223; x += 5)
				box(telemetry, x, y, 3, 1, Solid(0x777777), false);
		view.gaugeFill = box(telemetry, 194, 522, 30, 0, Solid(0x18E618), false);
		view.loadStrip = box(telemetry, 193, 526, 32, 4, Solid(0x18C918), false);

		view.outer = CreateDashboardPanel(Solid(0x9CA3AF), 8);
		view.outer.BorderBrush(Solid(0x181818));
		view.outer.BorderThickness(Thickness{ 1,1,1,1 });
		view.outer.Child(module);
		Viewbox fitted;
		fitted.Stretch(Stretch::Uniform);
		fitted.Child(view.outer);
		Button button = BareButton(fitted);
		button.Background(Solid(0x303030));
		button.SizeChanged([weak = get_weak(), weakOuter = make_weak(view.outer), module, lower, telemetry,
			setNumber, portNumber](auto const&, SizeChangedEventArgs const& args)
			{
				auto self = weak.get();
				auto outer = weakOuter.get();
				if (!self || !outer || setNumber > static_cast<int>(self->m_setVisuals.size())) return;
				auto& card = self->m_setVisuals[setNumber - 1].ports[portNumber - 1];
				if (card.outer != outer) return;
				const auto size = args.NewSize();
				if (size.Width <= 0 || size.Height <= 0) return;
				const double extra = (std::max)(0.0, size.Height * 238.0 / size.Width - 542.0);
				if (std::abs(extra - card.extraTravel) < 0.5) return;
				card.extraTravel = extra;
				module.Height(540 + extra);
				card.visual.Height(318 + extra);
				Canvas::SetTop(lower, extra);
				Canvas::SetTop(telemetry, extra);
				for (auto const& strand : card.strands) strand.Y2(114 + extra);
				self->RefreshPortMotion(setNumber, portNumber);
			});
		RegisterLockablePortControl(button);
		button.Click([weak = get_weak(), setNumber, portNumber](IInspectable const&, RoutedEventArgs const&)
			{
				if (auto self = weak.get()) self->TogglePort(setNumber, portNumber);
			});
		if (::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load())
		{
			MenuFlyout menu;
			MenuFlyoutItem pump;
			pump.Text(L"시뮬레이션 펌프 전환");
			pump.Click([weak = get_weak(), setNumber, portNumber](IInspectable const&, RoutedEventArgs const&)
				{
					if (auto self = weak.get()) self->TogglePumpForPort(setNumber, portNumber);
				});
			menu.Items().Append(pump);
			button.ContextFlyout(menu);
		}
		RefreshPort(setNumber, portNumber);
		return button;
	}
	void MainDashboardView::TogglePumpForPort(int32_t setNumber, int32_t portNumber)
	{
		if (m_locked || m_sequenceBusy || m_operationRunning || m_plcEmergencyActive ||
			!m_equipmentService || !::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()) return;
		const auto snapshot = m_equipmentService->Latest();
		if (!snapshot.simulation || snapshot.sequenceBusy ||
			(m_mockJobChangesLocked && !(snapshot.mockJobPaused && snapshot.sequenceStopConfirmed))) return;
		auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		const auto ip = EquipmentService::PumpIpForPort(config, setNumber - 1, portNumber - 1);
		if (!ip.empty()) RaiseAction(L"PumpIpToggle:" + std::wstring(ip.begin(), ip.end()));
	}

	void MainDashboardView::RefreshPort(int32_t setNumber, int32_t portNumber)
	{
		if (!ValidSet(setNumber) || !ValidPort(portNumber) ||
			setNumber > static_cast<int32_t>(m_setVisuals.size())) return;
		auto const& set = m_setStates[setNumber - 1];
		auto const& data = set.ports[portNumber - 1];
		auto& view = m_setVisuals[setNumber - 1].ports[portNumber - 1];
		if (!view.outer) return;

		const bool active = data.selected && !set.remoteMonitoring;
		auto const& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		const auto pumpIp = EquipmentService::PumpIpForPort(config, setNumber - 1, portNumber - 1);
		const auto pump = m_pumpByIp.find(pumpIp);
		const bool pumpKnown = pump != m_pumpByIp.end();
		const bool pumping = pumpKnown && pump->second;
		const bool activeBackground = active && pumping;
		const bool simulation = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
		const uint32_t imageColor = activeBackground ? 0xFFFFFF : 0xD1D5DB;
		view.outer.Background(Solid(activeBackground ? 0xB8F4FF : 0x9CA3AF));
		view.strandBrush.Color(Hex(activeBackground ? 0x111111 : 0xA0A0A0));
		view.visual.Background(Solid(imageColor));
		for (auto const& cutout : view.cutouts) cutout.Background(Solid(imageColor));
		view.remoteBadge.Visibility(set.remoteMonitoring ? Visibility::Visible : Visibility::Collapsed);
		using Language = ::DHLS_StrandJack::Services::Localization;
		view.oilTemperature.Text(Language::Text(L"유압유") + (simulation && set.plcConnected ? L": 0°C" : L": -- °C"));
		view.pumpLabel.Text(Language::Text(!pumpKnown ? L"펌프 --" : pumping ? L"펌프 켜짐" : L"펌프 꺼짐"));
		view.pumpLabel.Foreground(Solid(pumping ? 0x111111 : 0xFFFFFF));
		view.pumpLed.Background(Solid(pumping ? 0x00EE00 : 0x000000));
		view.pumpLed.HorizontalAlignment(pumping ? HorizontalAlignment::Right : HorizontalAlignment::Left);
		view.cornerLed.Background(activeBackground ? Vertical(0x7FE07F, 0x3FB53F).as<Brush>() : Solid(0x555555).as<Brush>());

		const size_t index = (setNumber - 1) * config.PLCSettings.Sensor_HPU_Count + portNumber - 1;
		auto const* detail = index < config.JackSetupDetails.size() ? &config.JackSetupDetails[index] : nullptr;
		Color color = Hex(0x52525B);
		bool showArrow = false;
		if (detail)
		{
			for (auto const& spec : config.JackSettings.Specs)
			{
				if (::DHLS_StrandJack::Models::JackTypeLabel(spec) != detail->JackSelection) continue;
				showArrow = spec.ShowMotionArrow;
				try
				{
					color = unbox_value<Color>(Markup::XamlBindingHelper::ConvertValue(
						xaml_typename<Color>(), box_value(hstring{ spec.CylinderColor })));
				}
				catch (hresult_error const&) {}
				break;
			}
		}
		const auto rgb = [](Color c) { return (uint32_t(c.R) << 16) | (uint32_t(c.G) << 8) | c.B; };
		const auto tint = [&](double amount)
			{
				const auto mix = [amount](uint8_t c) { return static_cast<uint8_t>(c + (255 - c) * amount); };
				return rgb(Color{ 255, mix(color.R), mix(color.G), mix(color.B) });
			};
		const bool yellow = color.R > 170 && color.G > 150 && color.B < 120;
		const bool blue = color.B > color.R + 35 && color.B > color.G + 35;
		LinearGradientBrush cylinder = !active ? Horizontal(0x52525B, 0xA1A1AA, 0x3F3F46, 0x71717A) :
			yellow ? Horizontal(0xD9B300, 0xFFFF99, 0xB39200, 0xFFEA4D) :
			blue ? Horizontal(0x3B3BD6, 0x9494FF, 0x2E2EB8, 0x6B6BFF) :
			Horizontal(rgb(color), tint(0.45), rgb(color));
		for (auto const& part : view.cylinderParts) part.Background(cylinder);
		const double engine = detail ? detail->EngineControlPercent : -1.0;
		view.enginePercent.Text(std::isfinite(engine) && engine >= 0 ? to_hstring(std::lround(engine)) + L"%" : hstring{ L"--" });
		view.enginePercent.Foreground(Solid(active && yellow ? 0x111111 : 0xFFFFFF));
		for (auto const& outline : view.engineOutline)
		{
			outline.Text(view.enginePercent.Text());
			outline.Visibility(active && !yellow ? Visibility::Visible : Visibility::Collapsed);
		}
		for (size_t clamp = 0; clamp < view.clampLabels.size(); ++clamp)
		{
			const bool known = simulation && set.plcConnected && active;
			const bool open = data.previewClampOpen[clamp];
			view.clampBodies[clamp].Background(active ? Vertical(0x4F565E, 0x3A4047) : Vertical(0x525252, 0x3F3F3F));
			view.clampLabels[clamp].Text(known ? ::DHLS_StrandJack::Services::Localization::Text(open ? L"열림" : L"닫힘") : hstring{ L"--" });
			view.clampLabels[clamp].Foreground(Solid(known ? (open ? 0xFFB27A : 0xFFFF99) : 0xD1D5DB));
			for (size_t i = 0; i < view.clampArrows[clamp].size(); ++i)
				view.clampArrows[clamp][i].Visibility(known && ((i >= 2) == open) ? Visibility::Visible : Visibility::Collapsed);
		}

		view.metricValues[0].Text(Displacement(data.absoluteMm - data.mockStrokeZeroMm));
		const auto pressureReading = ::DHLS_StrandJack::DisplayUnits::PressureText(data.pressureBar);
		view.metricValues[1].Text(hstring{ pressureReading.substr(0, pressureReading.rfind(L' ')) });
		view.metricValues[2].Text(OneDecimal(::DHLS_StrandJack::DisplayUnits::Weight(data.loadTon)));
		view.metricValues[3].Text(Displacement(data.relativeMm));
		const bool validLoad = std::isfinite(data.loadTon) && std::isfinite(data.capacityTon) && data.capacityTon > 0;
		const double loadRatio = validLoad ? (std::max)(0.0, data.loadTon / data.capacityTon) : 0.0;
		const double loadHeight = active && loadRatio > 0 ? std::clamp(loadRatio, 0.04, 1.0) * 102 : 0;
		view.loadPercent.Text(validLoad ? hstring{ L"하중\n" } + to_hstring(std::lround((std::min)(loadRatio, 9.99) * 100)) + L"%" : L"하중\n--");
		view.gaugeFill.Height(loadHeight);
		Canvas::SetTop(view.gaugeFill, 522 - loadHeight);
		view.gaugeFill.Background(loadRatio >= 1.0 ? Vertical(0xFB923C, 0xB91C1C) :
			loadRatio >= 0.8 ? Vertical(0xFEF08A, 0xD97706) : Vertical(0x7AFF7A, 0x009900));

		view.showArrow = showArrow;
		RefreshPortMotion(setNumber, portNumber);
	}


	void MainDashboardView::RefreshAllButton(int32_t setNumber)
	{
		if (!ValidSet(setNumber) || setNumber > static_cast<int32_t>(m_setVisuals.size())) return;
		bool all = std::all_of(m_setStates[setNumber - 1].ports.begin(),
			m_setStates[setNumber - 1].ports.begin() + m_portCount,
			[](PortState const& port) { return port.selected; });
		auto& view = m_setVisuals[setNumber - 1];
		const bool any = std::any_of(m_setStates[setNumber - 1].ports.begin(),
			m_setStates[setNumber - 1].ports.begin() + m_portCount,
			[](PortState const& port) { return port.selected; });
		if (view.allCheckBox)
		{
			if (any && !all) view.allCheckBox.IsChecked(nullptr);
			else view.allCheckBox.IsChecked(box_value(all).as<Windows::Foundation::IReference<bool>>());
		}
	}
	void MainDashboardView::TogglePort(int32_t setNumber, int32_t portNumber)
	{
		if (m_locked || m_sequenceBusy || m_mockJobChangesLocked) return;
		if (!ValidSet(setNumber) || !ValidPort(portNumber) || setNumber > m_setCount) return;
		auto& data = m_setStates[setNumber - 1].ports[portNumber - 1];
		data.selected = !data.selected;
		RefreshPort(setNumber, portNumber);
		RefreshAllButton(setNumber);
		RefreshMovementProgress();
		RefreshMotionArrows(true);
		RaiseAction(L"PortSelected:" + std::to_wstring(setNumber) + L":" +
			std::to_wstring(portNumber) + L":" + (data.selected ? L"1" : L"0"));
	}
	void MainDashboardView::ToggleAll(int32_t setNumber)
	{
		if (m_locked || m_sequenceBusy || m_mockJobChangesLocked) return;
		if (!ValidSet(setNumber) || setNumber > m_setCount) return;
		auto& ports = m_setStates[setNumber - 1].ports;
		bool all = std::all_of(ports.begin(), ports.begin() + m_portCount,
			[](PortState const& p) { return p.selected; });
		for (int i = 0; i < m_portCount; ++i)
		{
			ports[i].selected = !all;
			RefreshPort(setNumber, i + 1);
		}
		RefreshAllButton(setNumber);
		RefreshMovementProgress();
		RefreshMotionArrows(true);
		RaiseAction(L"AllSelected:" + std::to_wstring(setNumber) + L":" + (all ? L"0" : L"1"));
	}
	void MainDashboardView::RefreshHpuButtons()
	{
		for (std::size_t index = 0; index < m_setVisuals.size(); ++index)
		{
			auto& view = m_setVisuals[index];
			if (view.hpuLamp) view.hpuLamp.Background(m_setStates[index].hpuRunning ?
				Vertical(0xB0FF80, 0x208010) : Vertical(0xAAAAAA, 0x252525));
		}
	}
	void MainDashboardView::RefreshSetLamps(int32_t setNumber)
	{
		if (!ValidSet(setNumber) || setNumber > static_cast<int32_t>(m_setVisuals.size())) return;
		auto const& state = m_setStates[setNumber - 1];
		auto& view = m_setVisuals[setNumber - 1];
		if (view.plcLamp) view.plcLamp.Background(state.plcConnected ? Vertical(0xB0FF80, 0x208010) : Vertical(0xFFAAAA, 0xB80000));
		if (view.errorLamp) view.errorLamp.Background(state.errorActive ? Vertical(0xFFAAAA, 0xB80000) : Vertical(0xAAAAAA, 0x252525));
	}
	void MainDashboardView::RefreshSetTotal(int32_t setNumber)
	{
		if (!ValidSet(setNumber) || setNumber > static_cast<int32_t>(m_setVisuals.size())) return;
		if (setNumber == 1)
			RefreshFooterSpeedControls();
		if (m_setVisuals[setNumber - 1].total)
			m_setVisuals[setNumber - 1].total.Text(OneDecimal(::DHLS_StrandJack::DisplayUnits::Weight(m_setStates[setNumber - 1].totalTon)));
	}

	// Host-facing data APIs. They only set displayed data; no real PLC is accessed.
	void MainDashboardView::SetPortCapacity(int32_t s, int32_t p, int32_t ton)
	{
		if (!ValidSet(s) || !ValidPort(p)) return;
		m_setStates[s - 1].ports[p - 1].capacityTon = std::max(0, ton);
		RefreshPort(s, p);
	}
	void MainDashboardView::SetPortMetrics(int32_t s, int32_t p, double pressure,
		double load, double relative, double absolute)
	{
		if (!ValidSet(s) || !ValidPort(p)) return;
		auto& port = m_setStates[s - 1].ports[p - 1];
		port.pressureBar = pressure; port.loadTon = load;
		port.relativeMm = relative; port.absoluteMm = absolute;
		RefreshPort(s, p);
		RefreshMovementProgress();
	}
	void MainDashboardView::TargetInput_ValueChanged(NumberBox const&, NumberBoxValueChangedEventArgs const&)
	{
		if (m_updatingTargetInput || m_locked || m_sequenceBusy || (m_mockJobChangesLocked&&!m_mockJobPaused) || m_operationRunning)return;
		const double entered = MoveDisplacementInput().Value();
		if (!std::isfinite(entered) || entered <= 0)return;
		auto& settings = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
		if (m_pressureMode)settings.PressureTargetBar = ::DHLS_StrandJack::DisplayUnits::Bar(entered);
		else settings.MovementDistanceMm = entered;
		// The same input shows bar in pressure mode and mm in displacement mode.
		// Keep the displacement reference independently when changing modes.
		if (m_pressureMode) m_pressureTargetBar = MoveDisplacement();
		else m_displacementReferenceMm = MoveDisplacement();
		for (int set = 1; set <= static_cast<int>(m_setVisuals.size()); ++set)
			for (int port = 1; port <= m_portCount; ++port) RefreshPort(set, port);
		RefreshMovementProgress();
		RefreshMotionArrows(true);
		if (m_mockJobPaused)
			::DHLS_StrandJack::Services::AuditLog::Write("DATA_CHANGE","Next cycle stroke: "+std::to_string(entered)+" mm");
		else if (m_equipmentIntegrationActive) ForwardDiagnosticAction(L"TargetChanged");
	}

	void MainDashboardView::GoalDistanceInput_ValueChanged(NumberBox const&, NumberBoxValueChangedEventArgs const&)
	{
		if (m_updatingGoalDistanceInput || m_locked || m_sequenceBusy || m_mockJobChangesLocked || !GoalDistanceInput()) return;
		const double value = GoalDistanceInput().Value();
		if (!std::isfinite(value) || value < 0) return;
		m_showJobRemaining = false;
		m_goalDistanceMm = value;
		::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.GoalDistanceMm = value;
	}

	void MainDashboardView::DistanceInput_PointerWheelChanged(IInspectable const& sender,
		Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		auto box = sender.try_as<NumberBox>();
		if (!box || box.FocusState() == FocusState::Unfocused || !box.IsEnabled() || m_locked || m_sequenceBusy) return;
		const int delta = args.GetCurrentPoint(box).Properties().MouseWheelDelta();
		if (delta == 0) return;
		const double current = box.Value();
		if (!std::isfinite(current)) return;
		box.Value((std::max)(box.Minimum(), current + (delta > 0 ? box.SmallChange() : -box.SmallChange())));
		args.Handled(true);
	}

	void MainDashboardView::RefreshFooterSpeedControls()
	{
		if (m_setCount < 1 || !FooterLiftSlider() || !FooterLowerSlider()
			|| !FooterLiftSpeedText() || !FooterLowerSpeedText()) return;
		auto const& set = m_setStates.front();
		m_updatingFooterSpeed = true;
		FooterLiftSlider().Value(set.liftingSpeed);
		FooterLowerSlider().Value(set.loweringSpeed);
		FooterLiftSpeedText().Text(to_hstring(set.liftingSpeed));
		FooterLowerSpeedText().Text(to_hstring(set.loweringSpeed));
		m_updatingFooterSpeed = false;
	}

	void MainDashboardView::ChangeFooterSpeed(bool lifting, double requestedHz)
	{
		if (m_updatingFooterSpeed || m_locked || m_setCount < 1
			|| !FooterLiftSpeedText() || !FooterLowerSpeedText()) return;
		auto& set = m_setStates.front();
		const int configured = lifting ? set.liftingSpeed : set.loweringSpeed;
		const int value = static_cast<int>(std::lround(requestedHz));
		if (m_sequenceBusy && !m_automaticRunning)
		{
			RefreshFooterSpeedControls();
			EquipmentStatusText().Text(::DHLS_StrandJack::Services::Localization::Text(L"운전 중에는 속도를 변경할 수 없습니다."));
			return;
		}
		if (value < 20 || value > 60)
		{
			RefreshFooterSpeedControls();
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Operating speed must be between 20 and 60 Hz."
				: L"운전 속도는 20~60Hz 범위여야 합니다.");
			return;
		}
		if (value == configured) return;
		if (lifting) set.liftingSpeed = value;
		else set.loweringSpeed = value;
		RefreshFooterSpeedControls();
		RaiseAction(std::wstring(lifting ? L"LiftingSpeed:1:" : L"LoweringSpeed:1:") + std::to_wstring(value));
	}

	void MainDashboardView::FooterLiftSlider_ValueChanged(IInspectable const&,
		Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
	{
		ChangeFooterSpeed(true, args.NewValue());
	}

	void MainDashboardView::FooterLowerSlider_ValueChanged(IInspectable const&,
		Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
	{
		ChangeFooterSpeed(false, args.NewValue());
	}

	hstring MainDashboardView::OperationModeTitle() const
	{
		using L = ::DHLS_StrandJack::Services::Localization;
		if (m_calibrationMode) return L::Text(L"교정");
		switch (m_startupMenuMode)
		{
		case 1: return L::Text(L"하강 운전");
		case 2: return L::Text(L"수동 운전");
		case 3: return L::Text(L"텐셔닝 작업");
		default: return L::Text(L"상승 운전");
		}
	}

	void MainDashboardView::SetStartupMenuMode(int32_t mode)
	{
		m_startupMenuMode = mode;
		m_calibrationMode = mode == 4;
		m_pressureMode = mode == 3;
		m_autoMode = mode != 2 && mode != 4;
		BeginMovementProgress(mode != 1);
		RefreshModeButtons();
		RefreshOperationPanel();
	}

	void MainDashboardView::PreviewWedge_Click(IInspectable const& sender, RoutedEventArgs const&)
	{
		if (!::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load() ||
			m_locked || m_sequenceBusy || m_mockJobChangesLocked || m_operationRunning || !m_equipmentService) return;
		const auto snapshot = m_equipmentService->Latest();
		if (!snapshot.simulation) return;
		const auto tag = unbox_value<hstring>(sender.as<Button>().Tag());
		const size_t clamp = tag == L"LowerOpen" || tag == L"LowerClose" ? 1 : 0;
		const bool open = tag == L"LowerOpen" || tag == L"UpperOpen";
		const std::string event = clamp == 0 ? (open ? "Upper wedge opened" : "Upper wedge closed")
			: (open ? "Lower wedge opened" : "Lower wedge closed");
		for (int set = 1; set <= m_setCount; ++set)
			for (int port = 1; port <= m_portCount; ++port)
			{
				auto& state = m_setStates[set - 1].ports[port - 1];
				if (!PortCanOperate(set - 1, port - 1, snapshot)) continue;
				state.previewClampOpen[clamp] = open;
				::DHLS_StrandJack::Services::AuditLog::Write("SIM_WEDGE", event + " | SET=" + std::to_string(set) + " PORT=" + std::to_string(port));
				RefreshPort(set, port);
			}
	}

	void MainDashboardView::SetOperatingMode(int32_t mode)
	{
		if (!CanLeaveDashboard()) return;
		m_calibrationMode = mode == 2;
		m_pressureMode = mode == 1;
		if (m_calibrationMode)m_autoMode = false;
		else if (m_hasDashboardMemory)m_autoMode = ::DHLS_StrandJack::Control::EquipmentSession::Shared().Dashboard(m_stateModel, m_stateSimulation).automatic;
		else m_autoMode = ::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load();
		RefreshModeButtons();
		RefreshOperationPanel();
	}

	void MainDashboardView::RefreshOperationPanel()
	{
		const auto modeTitle = OperationModeTitle();
		OperationLogMessageHeader().Text(::DHLS_StrandJack::Services::Localization::Text(L"메시지") + L"(" + modeTitle + L")");
		BodyOperationModeTitle().Text(modeTitle);
		const auto show = Visibility::Visible;
		const auto hide = Visibility::Collapsed;
		const bool wedgeLayout = m_startupMenuMode == 2 || m_calibrationMode;
		const bool tensionLayout = m_startupMenuMode == 3 && !m_calibrationMode;
		const auto modeVisibility = wedgeLayout || tensionLayout ? hide : show;
		BottomDistancePanel().Visibility(modeVisibility);
		BottomActionPanel().Visibility(modeVisibility);
		BottomWedgePanel().Visibility(wedgeLayout ? show : hide);
		BottomManualDirections().Visibility(wedgeLayout ? show : hide);
		BottomTensionInputs().Visibility(tensionLayout ? show : hide);
		BottomTensionActions().Visibility(tensionLayout ? show : hide);
		BottomCalibrationActions().Visibility(m_calibrationMode ? show : hide);
		BottomActionColumn().Width(GridLength{ tensionLayout ? 137.0 : 157.0, GridUnitType::Pixel });
		BottomLoadColumn().Width(GridLength{ tensionLayout ? 290.0 : 314.0, GridUnitType::Pixel });
		BottomCalibrationColumn().Width(GridLength{ m_calibrationMode ? 157.0 : 0.0, GridUnitType::Pixel });
		if (tensionLayout)
		{
			const auto& settings = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
			TensionRangeInput().Text(Displacement(settings.PretensionRangeMm));
			TensionTravelInput().Value(m_displacementReferenceMm);
		}
		MovementInputPanel().Visibility(m_calibrationMode ? hide : show);
		MovementProgressPanel().Visibility(m_calibrationMode ? hide : show);
		CalibrationPanel().Visibility(m_calibrationMode ? show : hide);
		ReportToolsPanel().Visibility(!m_calibrationMode && m_pressureMode ? show : hide);
		MovementInputLabel().Text(::DHLS_StrandJack::Services::Localization::Text(m_pressureMode ? L"설정압력" : L"동작거리"));
		MovementInputUnit().Text(m_pressureMode ? hstring{ ::DHLS_StrandJack::DisplayUnits::PressureLabel() } : hstring{ L"mm" });
		m_updatingTargetInput = true;
		Windows::Globalization::NumberFormatting::DecimalFormatter targetFormat;
		targetFormat.IntegerDigits(1); targetFormat.FractionDigits(m_pressureMode ? ::DHLS_StrandJack::DisplayUnits::PressureDigits() : ::DHLS_StrandJack::DisplayUnits::DisplacementDigits()); targetFormat.IsGrouped(false);
		Windows::Globalization::NumberFormatting::IncrementNumberRounder targetRounder;
		targetRounder.Increment(std::pow(10.0, -static_cast<int>(targetFormat.FractionDigits())));
		targetRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
		targetFormat.NumberRounder(targetRounder);
		MoveDisplacementInput().NumberFormatter(targetFormat);
		if (!m_pressureMode) GoalDistanceInput().NumberFormatter(targetFormat);
		m_updatingGoalDistanceInput = true;
		GoalDistanceInput().Value(m_goalDistanceMm);
		m_updatingGoalDistanceInput = false;
		MoveDisplacementInput().Value(m_pressureMode ? ::DHLS_StrandJack::DisplayUnits::Pressure(m_pressureTargetBar) : m_displacementReferenceMm);
		m_updatingTargetInput = false;
		Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(MoveDisplacementInput(),
			::DHLS_StrandJack::Services::Localization::Text(m_pressureMode ? L"설정압력" : L"동작거리"));
		RefreshLockState();
		RefreshMovementProgress();
		RefreshMotionArrows(true);
	}

	void MainDashboardView::RegisterLockablePortControl(Control const& control)
	{
		control.IsEnabled(!m_locked && !m_sequenceBusy);
		m_lockablePortControls.push_back(control);
	}

	void MainDashboardView::RefreshLockState()
	{
		// Cover header, back, connection and dynamically built action buttons.
		// Stop and Emergency remain accessible independently of the input lock.
		std::function<void(DependencyObject const&)> updateButtons;
		updateButtons = [&](DependencyObject const& node)
			{
				if (auto button = node.try_as<Button>())
				{
					const auto tag = unbox_value_or<hstring>(button.Tag(), L"");
					if (tag == L"UnavailableFooterCommand" || tag == L"CenterOfGravity")
					{
						button.IsEnabled(false);
						return;
					}
					const bool safetyOrUnlock = button == LockButton() || button == StopButton() ||
						button == FooterCalibrationStop() || button == EmergencyButton();
					const bool readOnlyMonitor = button == PlcMapButton() || button == PressureDiagnosticsButton();
					const bool holdButton = button == StartButton() || button == FooterManualUp() || button == FooterManualDown();
					button.IsEnabled(safetyOrUnlock || readOnlyMonitor || (!m_locked && (!m_sequenceBusy || (holdButton && m_manualHeld)) &&
						(button != EquipmentConnectButton() || !m_equipmentDisconnecting)));
					return; // Do not override implementation controls inside a Button template.
				}
				if (node.try_as<NumberBox>() || node.try_as<Slider>()) return;
				for (int i = 0; i < VisualTreeHelper::GetChildrenCount(node); ++i)
					updateButtons(VisualTreeHelper::GetChild(node, i));
			};
		updateButtons(*this);

		auto light = Resources().Lookup(box_value(L"LightButtonGradient")).as<Brush>();
		auto dark = Resources().Lookup(box_value(L"DarkButtonGradient")).as<Brush>();
		LockButton().Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
		LockButton().Foreground(Solid(0xFFFFFF));
		if (auto icon = LockButton().Content().try_as<FontIcon>())
			icon.Glyph(m_locked ? L"\xE72E" : L"\xE785");
		for (auto button : { DisplacementModeButton(), PressureModeButton(), AutoModeButton(), ManualModeButton(), LiftUpButton(), LiftDownButton(), StartButton() })
			button.IsEnabled(!m_locked && (!m_sequenceBusy || (button == StartButton() && m_manualHeld)));
		// Keep arrows clickable while busy so they can explain the direction interlock.
		// Their handlers return before changing UI direction or enqueueing any PLC command.
		LiftUpButton().IsEnabled(!m_locked);
		LiftDownButton().IsEnabled(!m_locked);
		if (m_fullStopChosen)
			for (auto button : {StartButton(), FooterManualUp(), FooterManualDown()}) button.IsEnabled(false);
		const bool inputsEnabled = !m_locked && !m_sequenceBusy && !m_mockJobChangesLocked && !m_operationRunning;
		MoveDisplacementInput().IsEnabled(inputsEnabled || (m_mockJobPaused&&!m_locked&&!m_sequenceBusy&&!m_operationRunning&&!m_fullStopChosen));
		GoalDistanceInput().IsEnabled(inputsEnabled);
		FooterHeightZeroButton().IsEnabled(inputsEnabled);
		ManualHeightZeroButton().IsEnabled(inputsEnabled);
		TensionTravelInput().IsEnabled(inputsEnabled);
		TensionPercentInput().IsEnabled(inputsEnabled);
		FooterLiftSlider().IsEnabled(inputsEnabled);
		FooterLowerSlider().IsEnabled(inputsEnabled);
		MoveDisplacementInput().Background(Solid(m_locked ? 0xC2C2C2 : 0xFFFFFF));
		for (auto button : { BackNavigationButton(),DisplacementModeButton(),PressureModeButton(),AutoModeButton(),ManualModeButton() })button.IsEnabled(!m_locked);
		PressureModeButton().IsEnabled(!m_locked && !m_calibrationMode);
		AutoModeButton().IsEnabled(!m_locked && !m_calibrationMode);
		MoveDisplacementInput().Foreground(Solid(m_locked ? 0x808080 : 0x000000));
		for (auto const& control : m_lockablePortControls)
		{
			// During a confirmed pause, the card hosts the pump menu. TogglePort still locks selection.
			const bool pausedPumpMenu = m_stateSimulation && m_mockJobPaused && !m_operationRunning &&
				!m_plcEmergencyActive && static_cast<bool>(control.ContextFlyout());
			control.IsEnabled(!m_locked && !m_sequenceBusy && (!m_mockJobChangesLocked || pausedPumpMenu));
		}
		for (auto const& control : m_lockableSpeedControls)control.IsEnabled(inputsEnabled);
		for (auto panel : { ZeroControlsPanel(), CalibrationPanel(), ReportToolsPanel() })
			for (auto child : panel.Children())
				if (auto control = child.try_as<Control>())
					control.IsEnabled(unbox_value_or<hstring>(control.Tag(), L"") != L"CenterOfGravity" &&
						!m_locked && !m_sequenceBusy);
		for (auto child : ZeroControlsPanel().Children())
		{
			auto button = child.as<Button>();
			const bool muted = m_locked || m_calibrationMode;
			button.Background(muted ? light : dark);
			button.Foreground(Solid(muted ? 0x808080 : 0xFFFFFF));
		}
		DirectionPanel().Background(m_locked ? light : Resources().Lookup(box_value(L"LeftTopGradient")).as<Brush>());
		MovementInputPanel().Background(m_locked ? light : dark);
		StartStopPanel().Background(m_locked ? light : dark);
		TotalWeightPanel().Background(m_locked ? light : dark);
		MovementInputLabel().Foreground(Solid(m_locked ? 0x808080 : 0xFFFFFF));
		MovementInputUnit().Foreground(Solid(m_locked ? 0x808080 : 0xFFFFFF));
		TotalWeightLabel().Foreground(Solid(m_locked ? 0x808080 : 0xFFFFFF));
		TotalWeightUnit().Foreground(Solid(m_locked ? 0x808080 : 0xFFFFFF));
		TotalWeightText().Foreground(Solid(m_locked ? 0xBFBFBF : 0x90EE90));
		// Stop and emergency remain available; locking never changes motion state.
		StopButton().IsEnabled(true);
		EmergencyButton().IsEnabled(true);
	}

	void MainDashboardView::ReportToolButton_Click(IInspectable const& sender, RoutedEventArgs const&)
	{
		if (m_locked || m_sequenceBusy)return;
		if (auto button = sender.try_as<Button>())
		{
			const auto tag = winrt::unbox_value<hstring>(button.Tag());
			if (tag == L"CenterOfGravity") return;
			ShowControlWarningAsync(::DHLS_StrandJack::Services::Localization::IsEnglish()
				? L"Record 1–5, folder and XLS export are not defined yet."
				: L"기록 1~5, 폴더 및 XLS 저장 형식이 아직 정의되지 않아 사용할 수 없습니다.");
		}
	}

	void MainDashboardView::RefreshPortMotion(int32_t setNumber, int32_t portNumber)
	{
		const auto& set = m_setStates[setNumber - 1];
		const auto& port = set.ports[portNumber - 1];
		auto& view = m_setVisuals[setNumber - 1].ports[portNumber - 1];
		if (!view.upperAssembly) return;
		const bool active = port.selected && !set.remoteMonitoring;
		double ratio = 0.0;
		if (active && std::isfinite(port.absoluteMm))
		{
			if (m_stateSimulation && m_autoMode && !m_pressureMode && m_mockJobValid && m_mockJobStrokeSpanMm > 0)
			{
				ratio = std::clamp((port.absoluteMm - m_mockJobStrokeBaseMm) / m_mockJobStrokeSpanMm, 0.0, 1.0);
			}
			else if (!m_pressureMode && port.sequencePositionValid && std::isfinite(port.sequenceMm) &&
				std::isfinite(port.sequenceOriginMm) && std::isfinite(port.sequenceTargetMm) && m_displacementReferenceMm > 0)
			{
				const double lower = (std::min)(port.sequenceOriginMm, port.sequenceTargetMm);
				ratio = std::clamp((port.sequenceMm - lower) / m_displacementReferenceMm, 0.0, 1.0);
			}
			else if (std::isfinite(m_displacementReferenceMm) && m_displacementReferenceMm > 0.0)
			{
				if (port.hasTravelOrigin)
				{
					const double direction = port.travelLifting ? 1.0 : -1.0;
					const double traveled = (port.absoluteMm - port.travelOriginMm) * direction;
					const double progress = std::clamp(traveled / m_displacementReferenceMm, 0.0, 1.0);
					ratio = port.travelLifting ? progress : 1.0 - progress;
				}
				else ratio = std::clamp(port.absoluteMm / m_displacementReferenceMm, 0.0, 1.0);
			}
		}
		const double pistonHeight = 4.0 + ratio * (52.0 + view.extraTravel);
		Canvas::SetTop(view.upperAssembly, 60.0 + view.extraTravel - pistonHeight);
		view.pistonRod.Height(pistonHeight);
		const auto now = EquipmentService::MonotonicMs();
		const auto startupElapsed = now - m_motionStartedMs;
		const auto completionElapsed = now - m_motionCompletedMs;
		const bool starting = m_motionRunning && startupElapsed < 1200;
		const bool completionFlash = m_cycleCompleted && m_motionCompletedMs != 0 && completionElapsed < 1600;
		const bool hasTarget = port.sequencePositionValid && std::isfinite(port.sequenceMm)
			&& std::isfinite(port.sequenceOriginMm) && std::isfinite(port.sequenceTargetMm)
			&& port.sequenceTargetMm != port.sequenceOriginMm;
		const bool indicators = active && set.plcConnected && view.showArrow && !m_calibrationMode
			&& !m_pressureMode && !m_plcFaultActive && !m_plcEmergencyActive && hasTarget
			&& (m_motionRunning || m_motionStopping || completionFlash);
		view.motionIcon.Visibility(indicators ? Visibility::Visible : Visibility::Collapsed);
		if (indicators)
		{
			const auto& settings = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings;
			const bool lifting = port.sequenceTargetMm > port.sequenceOriginMm;
			const double direction = lifting ? 1.0 : -1.0;
			const double traveled = (port.sequenceMm - port.sequenceOriginMm) * direction;
			const double remaining = (port.sequenceTargetMm - port.sequenceMm) * direction;
			const double lowSpeedRange = settings.DistanceDecelerationStartMm;
			const bool fast = m_motionRunning && traveled > lowSpeedRange && remaining > lowSpeedRange;
			view.motionDirection.Angle(lifting ? 0 : 180);
			view.fastArrow.Visibility(fast ? Visibility::Visible : Visibility::Collapsed);
			view.slowArrow.Visibility(fast ? Visibility::Collapsed : Visibility::Visible);
			view.slowArrow.Fill(starting ? view.motionOrange : view.motionGreen);
			const auto flashElapsed = completionFlash ? completionElapsed : startupElapsed;
			const bool dimmed = (fast || (starting && traveled <= lowSpeedRange) || completionFlash) && (flashElapsed / 400) % 2 != 0;
			view.motionIcon.Opacity(dimmed ? 0.2 : 1.0);
		}
		view.loadStrip.Opacity(ratio > 0.10 ? 1.0 : 0.25);
	}

	void MainDashboardView::RefreshMotionArrows(bool)
	{
		for (int set = 1; set <= static_cast<int>(m_setVisuals.size()); ++set)
			for (int port = 1; port <= m_portCount; ++port)
				RefreshPortMotion(set, port);
	}


	void MainDashboardView::BeginMovementProgress(bool lifting)
	{
		m_movementLifting = lifting;
		if (::DHLS_StrandJack::Control::DeveloperSimulationEnabled.load()) {
			LiftUpArrow().Fill(Solid(lifting ? 0x60FF20 : 0xFFFFFF));
			LiftDownArrow().Fill(Solid(lifting ? 0xFFFFFF : 0x60FF20));
		} // Real PLC direction colors are updated only from confirmed D051 feedback.
		RefreshMovementProgress();
		RefreshMotionArrows(true);
	}

	void MainDashboardView::RefreshMovementProgress()
	{
		// NumberBox ValueChanged can run before later XAML names are initialized.
		if (!MovementProgressText() || !MovementProgressFillColumn() || !MovementProgressRemainingColumn()) return;
		// Automatic displacement progress belongs to the active relative-move cycle.
		if (!m_pressureMode && m_autoMode) {
			if (m_mockJobValid && m_mockJobGoalMm > 0) {
				SetMovementProgress(100.0 * m_mockJobTravelMm / m_mockJobGoalMm); return;
			}
			if (m_cyclePending) { SetMovementProgress(0); return; }
			double average = 0; int count = 0;
			for (int s = 0; s < m_setCount; ++s)for (int p = 0; p < m_portCount; ++p) {
				auto const& port = m_setStates[s].ports[p];
				if (!port.selected)continue;
				if (!port.sequencePositionValid) { SetMovementProgress(0); return; }
				const double ratio = ::DHLS_StrandJack::Control::MovementCycleRatio(port.absoluteMm, port.sequenceOriginMm, port.sequenceTargetMm);
				if (!std::isfinite(ratio)) { SetMovementProgress(0); MovementProgressText().Text(L"--%"); return; }
				average += (ratio - average) / ++count;
			}
			SetMovementProgress(count ? (m_cycleCompleted ? 100.0 : average * 100.0) : 0.0); return;
		}
		const double target = MoveDisplacement();
		double average = 0.0;
		int count = 0;
		for (int s = 0; s < m_setCount; ++s)
			for (int p = 0; p < m_portCount; ++p)
			{
				auto const& port = m_setStates[s].ports[p];
				if (!port.selected) continue;
				const double value = m_pressureMode ? port.pressureBar : port.absoluteMm;
				if (!std::isfinite(value))
				{
					SetMovementProgress(0.0);
					MovementProgressText().Text(L"--%");
					return; // Never silently omit an active port with invalid data.
				}
				++count;
				average += (value - average) / count;
			}
		if (count == 0)
		{
			SetMovementProgress(0.0);
			return;
		}
		if (target <= 0.0 || !std::isfinite(average))
		{
			SetMovementProgress(0.0);
			MovementProgressText().Text(L"--%");
			return;
		}
		SetMovementProgress(std::clamp(average / target, 0.0, 1.0) * 100.0);
	}

	double MainDashboardView::MoveDisplacement()
	{
		const double value = MoveDisplacementInput().Value();
		return std::isfinite(value) && value >= 0.0 ? (m_pressureMode ? ::DHLS_StrandJack::DisplayUnits::Bar(value) : value) : 0.0;
	}

	void MainDashboardView::SetMovementProgress(double percent)
	{
		// Call on the owning UI thread, like the other dashboard update APIs.
		const double value = std::isfinite(percent) ? std::clamp(percent, 0.0, 100.0) : 0.0;
		MovementProgressFillColumn().Width(GridLength{ value, GridUnitType::Star });
		MovementProgressRemainingColumn().Width(GridLength{ 100.0 - value, GridUnitType::Star });
		MovementProgressText().Text(winrt::to_hstring(static_cast<int>(value)) + L"%");
	}

	void MainDashboardView::SetTotalWeight(double ton)
	{
		TotalWeightUnit().Text(::DHLS_StrandJack::DisplayUnits::WeightLabel());
		TotalWeightText().Text(OneDecimal(::DHLS_StrandJack::DisplayUnits::Weight(ton)));
	}
	void MainDashboardView::SetSetTotalWeight(int32_t s, double ton)
	{
		if (!ValidSet(s)) return;
		m_setStates[s - 1].totalTon = ton;
		RefreshSetTotal(s);
	}
	void MainDashboardView::SetPlcConnected(int32_t s, bool connected)
	{
		if (!ValidSet(s)) return;
		m_setStates[s - 1].plcConnected = connected;
		RefreshSetLamps(s);
	}
	void MainDashboardView::SetErrorActive(int32_t s, bool active)
	{
		if (!ValidSet(s)) return;
		m_setStates[s - 1].errorActive = active;
		RefreshSetLamps(s);
	}
	void MainDashboardView::SetHpuRunning(int32_t plcIndex, bool running)
	{
		if (!ValidSet(plcIndex) || plcIndex > m_setCount) return;
		m_setStates[plcIndex - 1].hpuRunning = running;
		const auto& ips = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings.PLC_IPs;
		if (static_cast<size_t>(plcIndex) <= ips.size() && !ips[plcIndex - 1].empty())
		{
			const auto& ip = ips[plcIndex - 1];
			m_pumpByIp[std::string(ip.begin(), ip.end())] = running;
			for (int32_t set = 1; set <= m_setCount; ++set)
				for (int32_t port = 1; port <= m_portCount; ++port)
					RefreshPort(set, port);
		}
		RefreshHpuButtons();
	}
	bool MainDashboardView::IsPortSelected(int32_t s, int32_t p)
	{
		return ValidSet(s) && ValidPort(p) && m_setStates[s - 1].ports[p - 1].selected;
	}
}
