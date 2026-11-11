#pragma once

#include "MainDashboardView.g.h"
#include "Localization.h"
#include "ControlLogic/EquipmentControlService.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace DHLS_StrandJack::Diagnostics { class PlcMapMonitor; }

namespace winrt::DHLS_StrandJack::implementation
{
    struct MainDashboardView : MainDashboardViewT<MainDashboardView>
    {
        MainDashboardView();
        ~MainDashboardView();
        void PressureDiagnostics_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClosePressureDiagnostics(bool closeWindow=true);
        Microsoft::UI::Xaml::Window m_pressureDiagnosticsWindow{nullptr};
        Microsoft::UI::Xaml::Controls::TextBlock m_pressureDiagnosticsText{nullptr};
        winrt::event_token m_pressureDiagnosticsClosed{};
        void PlcMap_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ShowPlcMapWindow();
        void ClosePlcMapWindow(bool closeWindow=true);
        winrt::fire_and_forget ConfirmPlcDisconnectAsync();
        void EquipmentConnect_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget ConnectEquipmentAsync(bool explicitReconnect=false);
        void RestoreDashboardMemory();
        void SaveDashboardMemory();
        winrt::fire_and_forget ConfirmSequenceResetAsync();
        bool CheckMovementRange();
        bool PortCanOperate(int set, int port, ::DHLS_StrandJack::Control::EquipmentSnapshot const& snapshot) const;
        bool m_hasDashboardMemory=false;
        std::wstring m_stateModel;
        bool m_stateSimulation=false;
        bool m_mockJobValid=false;
        bool m_mockJobChangesLocked=false;
        bool m_mockJobPaused=false;
        double m_mockJobTravelMm=0, m_mockJobGoalMm=0;
        double m_mockJobStrokeBaseMm=0, m_mockJobStrokeSpanMm=0;
        bool m_showJobRemaining=false;
        void StrandReadyButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CalibrationPressureZero_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CalibrationStrokeZero_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        std::uint64_t m_simulationLogRevision=0;
        std::chrono::system_clock::time_point m_dashboardLogOpenedAt=std::chrono::system_clock::now();
        bool m_simulationLogEnglish=false;
        void RefreshDashboardLog();
        void EquipmentView_Loaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void EquipmentView_Unloaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HeaderLogoButton_Click(winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::hstring ModelTitle() const;
        bool CanLeaveDashboard(bool allowStoppedManual=false);
        bool CanChangeControlMode();
        winrt::Windows::Foundation::IAsyncOperation<bool> PrepareToCloseAsync();
        winrt::fire_and_forget ShowNavigationBlockedAsync();
        winrt::fire_and_forget ShowControlWarningAsync(winrt::hstring message);
        void SetOperatingMode(int32_t mode);
        void SetStartupMenuMode(int32_t mode);
        void RefreshOperationPanel();
        void TargetInput_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox const&,
            Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void GoalDistanceInput_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox const&,
            Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void DistanceInput_PointerWheelChanged(Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void FooterLiftSlider_ValueChanged(Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void FooterLowerSlider_ValueChanged(Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void ReportToolButton_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        int32_t MyProperty() { return 0; }
        void MyProperty(int32_t) {}
        void OnLocalizedLoaded(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
        { ::DHLS_StrandJack::Services::Localization::RegisterElement(sender.as<Microsoft::UI::Xaml::FrameworkElement>()); }

        // WinRT public methods declared in MainDashboardView.xaml.idl.
        void UpdatePortLayout(int32_t setCount, int32_t columns);
        void ApplyJackArrangementTemplate(int32_t templateId);
        void ApplyConfirmedArrangementJson(winrt::hstring const& arrangementJson);
        int32_t ActiveArrangementTemplate() const noexcept;
        void SetPortCapacity(int32_t setNumber, int32_t portNumber, int32_t capacityTon);
        void SetPortMetrics(int32_t setNumber, int32_t portNumber,
                            double pressureBar, double loadTon,
                            double relativeMm, double absoluteMm);
        double MoveDisplacement();
        void SetMovementProgress(double percent);
        void SetTotalWeight(double ton);
        void SetSetTotalWeight(int32_t setNumber, double ton);
        void SetPlcConnected(int32_t setNumber, bool connected);
        void SetErrorActive(int32_t setNumber, bool active);
        void SetHpuRunning(int32_t plcIndex, bool running);
        void SetOperationRunning(bool running);
        bool IsPortSelected(int32_t setNumber, int32_t portNumber);

        winrt::event_token ActionRequested(
            Windows::Foundation::TypedEventHandler<DHLS_StrandJack::MainDashboardView, winrt::hstring> const& handler);
        void ActionRequested(winrt::event_token const& token);

        // Code-behind for named XAML elements. Never sends PLC output directly.
        void DashboardRoot_SizeChanged(Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void CaptureButton_Click(Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void BackButton_Click(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
        void LockButton_Click(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DisplacementModeButton_Click(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PressureModeButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AutoModeButton_Click(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ManualModeButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HeightZeroButton_Click(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PressureZeroButton_Click(Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
        void LoadSetButton_Click(Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void UpButton_Click(Windows::Foundation::IInspectable const&,
                            Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DownButton_Click(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CalibrationZeroButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CalibrationSpanButton_Click(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StartButton_Click(Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StopButton_Click(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
        void EmergencyButton_Click(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);

        void PreviewWedge_Click(Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        std::shared_ptr<::DHLS_StrandJack::Diagnostics::PlcMapMonitor> m_plcMapMonitor;
        winrt::Microsoft::UI::Xaml::Window m_plcMapWindow{nullptr};
        winrt::event_token m_plcMapClosedToken{};
        bool m_plcDisconnectAuthBusy=false;
        bool m_sequenceResetAuthBusy=false;
        using TextBlock = Microsoft::UI::Xaml::Controls::TextBlock;
        using Border = Microsoft::UI::Xaml::Controls::Border;
        using Button = Microsoft::UI::Xaml::Controls::Button;

        struct PortState
        {
            double capacityTon{ 0 };
            double pressureBar{ 0.0 };
            double loadTon{ 0.0 };
            double relativeMm{ 0.0 };
            double absoluteMm{ 0.0 };
            double mockStrokeZeroMm{ 0.0 };
            bool selected{ false };
            double sequenceTargetMm{ 0.0 },sequenceOriginMm{0.0};
            double sequenceMm{ 0.0 };
            double travelOriginMm{ 0.0 };
            bool sequencePositionValid=false;
            bool hasTravelOrigin{ false };
            bool travelLifting{ true };
            std::array<bool, 2> previewClampOpen{};
        };
        struct SetState
        {
            std::array<PortState, 8> ports{};
            int liftingSpeed{ 20 };
            int loweringSpeed{ 20 };
            double totalTon{ 0.0 };
            bool plcConnected{ false };
            bool remoteMonitoring{ false };
            bool hpuRunning{ false };
            bool errorActive{ false };
        };
        struct PortVisual
        {
            Border outer{ nullptr };
            Border visual{ nullptr };
            Border remoteBadge{ nullptr };
            Border pistonRod{ nullptr };
            Border pumpPill{ nullptr };
            Border pumpLed{ nullptr };
            Microsoft::UI::Xaml::Media::SolidColorBrush strandBrush{ nullptr };
            Border cornerLed{ nullptr };
            Border gaugeFill{ nullptr };
            Border loadStrip{ nullptr };
            Microsoft::UI::Xaml::Controls::Canvas upperAssembly{ nullptr }, motionIcon{ nullptr }, fastArrow{ nullptr };
            std::array<Microsoft::UI::Xaml::Shapes::Line, 5> strands{ nullptr, nullptr, nullptr, nullptr, nullptr };
            double extraTravel{ 0 };
            Microsoft::UI::Xaml::Shapes::Polygon slowArrow{ nullptr };
            Microsoft::UI::Xaml::Media::RotateTransform motionDirection{ nullptr };
            Microsoft::UI::Xaml::Media::Brush motionGreen{ nullptr }, motionOrange{ nullptr };
            std::vector<Border> cylinderParts;
            std::array<Border, 2> cutouts{ nullptr, nullptr }, clampBodies{ nullptr, nullptr };
            std::array<TextBlock, 2> clampLabels{ nullptr, nullptr };
            std::array<std::array<Microsoft::UI::Xaml::Shapes::Polygon, 4>, 2> clampArrows{
                {{nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr}}};
            TextBlock oilTemperature{ nullptr }, pumpLabel{ nullptr }, enginePercent{ nullptr }, loadPercent{ nullptr };
            std::array<TextBlock, 4> engineOutline{ nullptr, nullptr, nullptr, nullptr };
            std::array<TextBlock, 4> metricValues{ nullptr, nullptr, nullptr, nullptr };
            bool showArrow{ false };
        };
        struct SetVisual
        {
            Border plcLamp{ nullptr };
            Border errorLamp{ nullptr };
            Border hpuLamp{ nullptr };
            Microsoft::UI::Xaml::Controls::CheckBox allCheckBox{ nullptr };
            TextBlock total{ nullptr };
            TextBlock liftingSpeed{ nullptr };
            TextBlock loweringSpeed{ nullptr };
            std::array<PortVisual, 8> ports{};
        };

        int32_t m_setCount{ 1 };
        int32_t m_columns{ 1 };
        int32_t m_rows{ 1 };
        int32_t m_arrangementTemplate{ 0 }; // 0 = legacy UpdatePortLayout mode
        int32_t m_portCount{ 8 };
        int32_t m_portColumns{ 8 };
        int32_t m_portRows{ 1 };
        bool m_captureInProgress{ false };
        winrt::fire_and_forget CaptureAndReportAsync();
        Windows::Foundation::IAsyncAction CaptureViewAsync();
        using EquipmentService = ::DHLS_StrandJack::Control::EquipmentControlService;
        std::shared_ptr<EquipmentService> m_equipmentService;
        Microsoft::UI::Xaml::DispatcherTimer m_equipmentTimer{ nullptr };
        bool m_equipmentIntegrationActive{ false };
        bool m_equipmentDisconnecting{ false };
        bool m_diagnosticStartRequested{ false };
        winrt::fire_and_forget DisconnectEquipmentAsync(std::shared_ptr<EquipmentService> service);
        void StopEquipmentIntegration();
        void ApplyEquipmentSnapshot();
        void ForwardDiagnosticAction(std::wstring const& action);
        static winrt::fire_and_forget ReleaseEquipmentServiceAsync(std::shared_ptr<EquipmentService> service);
        winrt::fire_and_forget ShowCogAsync();
        bool m_operationRunning{ false };
        std::uint64_t m_runStartedMs=0, m_runElapsedMs=0;
        void RefreshDashboardClocks();
        bool m_alarmPreviouslyActive=false;
        bool m_loadReferencePreviouslyTripped=false;
        bool m_cycleCompleted=false,m_cyclePending=false;
        bool m_motionRunning=false, m_motionStopping=false;
        std::uint64_t m_motionStartedMs=0, m_motionCompletedMs=0;
        std::array<std::array<int,8>,4> m_loadAlarmLevels{};
        bool m_automaticRunning=false;
        bool m_navigationWarningOpen{ false };
        bool m_plcEmergencyActive{ false };
        bool m_plcFaultActive{ false };
        bool m_pcEmergencyRequested{ false };
        bool m_stopSelected{ false };
        bool m_locked{ false };
        std::vector<Microsoft::UI::Xaml::Controls::Control> m_lockablePortControls;
        std::vector<Microsoft::UI::Xaml::Controls::Control> m_lockableSpeedControls;
        void RefreshLockState();
        void RegisterLockablePortControl(Microsoft::UI::Xaml::Controls::Control const& control);
        void RefreshOperationButtons();
        bool m_updatingTargetInput{ true };
        bool m_pressureMode{ false };
        int32_t m_startupMenuMode{ 0 };
        winrt::hstring OperationModeTitle() const;
        bool m_autoMode{ false };
        bool m_sequenceBusy{ false }, m_manualHeld{ false };
        void EndManualHold();
        bool m_pauseStopRequested=false,m_fullStopChosen=false;
        std::uint64_t m_fullStopPressedAt=0;
        std::uint32_t m_fullStopPointerId=0;
        std::uint64_t m_appliedResetEvent=0;
        bool CanHoldForFullStop() const;
        void UpdateFullStopHold();
        std::array<SetState, 8> m_setStates{};
        std::map<std::string, bool> m_pumpByIp;
        std::vector<SetVisual> m_setVisuals{};

        winrt::event<Windows::Foundation::TypedEventHandler<DHLS_StrandJack::MainDashboardView, winrt::hstring>>
            m_actionRequested;

        void BeginMovementProgress(bool lifting);
        void RefreshMovementProgress();
        double m_displacementReferenceMm{ 50.0 };
        double m_goalDistanceMm{ 10000.0 };
        bool m_updatingGoalDistanceInput{ false };
        bool m_updatingFooterSpeed{ false };
        void RefreshFooterSpeedControls();
        void ChangeFooterSpeed(bool lifting, double requestedHz);
        double m_pressureTargetBar{ 50.0 };
        bool m_calibrationMode{ false };
        bool m_movementLifting{ true };
        void RaiseAction(std::wstring const& action);
        void RefreshModeButtons();
        void RenderPortSets();
        Microsoft::UI::Xaml::Controls::Border CreatePortSet(int32_t setNumber, SetVisual& view);
        Microsoft::UI::Xaml::Controls::Grid CreateToolbar(int32_t setNumber, SetVisual& view);
        Microsoft::UI::Xaml::Controls::Button CreatePortCard(
            int32_t setNumber, int32_t portNumber, SetVisual& view);
        void RefreshPort(int32_t setNumber, int32_t portNumber);
        void TogglePumpForPort(int32_t setNumber, int32_t portNumber);
        void RefreshPortMotion(int32_t setNumber, int32_t portNumber);
        void RefreshMotionArrows(bool resetCompletion = false);
        void RefreshAllButton(int32_t setNumber);
        void RefreshHpuButtons();
        void RefreshSetLamps(int32_t setNumber);
        void RefreshSetTotal(int32_t setNumber);
        void TogglePort(int32_t setNumber, int32_t portNumber);
        void ToggleAll(int32_t setNumber);
        void ResetPortSelections();
        static uint32_t GroupColor(int32_t group) noexcept;
        static bool ValidSet(int32_t setNumber);
        bool ValidPort(int32_t portNumber) const;
    };
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
    struct MainDashboardView : MainDashboardViewT<MainDashboardView, implementation::MainDashboardView>
    {
    };
}
