#include "pch.h"
#include "JackScaleConsistency.h"
#include "Localization.h"
#include "ControlSettingsView.xaml.h"

#if __has_include("ControlSettingsView.g.cpp")
#include "ControlSettingsView.g.cpp"
#endif

#include "MainMenuView.xaml.h"
#include "JackArrangementView.xaml.h"
#include "MainWindow.xaml.h"
#include "ConfigManager.h"
#include "PlcDefaults.h"
#include "DisplayUnits.h"
#include "JackTypeLabel.h"
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <shobjidl.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Globalization.NumberFormatting.h>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Text.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <string_view>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DHLS_StrandJack::implementation
{
	static void TraceControlSettings(wchar_t const* stage)
	{
#ifdef _DEBUG
		::OutputDebugStringW((std::wstring{L"[ControlSettings] "} + stage + L"\n").c_str());
#else
		(void)stage;
#endif
	}

	static int SelectedDashboardJackCount(Microsoft::UI::Xaml::Controls::ComboBox const& box)
	{
		const int index = box.SelectedIndex();
		if (index < 0 || index >= static_cast<int>(box.Items().Size())) return 1;
		auto item = box.Items().GetAt(index).as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
		return std::stoi(std::wstring{unbox_value<hstring>(item.Content())});
	}

	// Runs synchronously on the UI thread for both initial display and selection changes.
	static void RefreshJackWeights(int rowIndex, int specIndex,
		Microsoft::UI::Xaml::Controls::TextBlock const& workingText,
		Microsoft::UI::Xaml::Controls::NumberBox const& maximumText)
	{
		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		if (rowIndex < 0 || rowIndex >= static_cast<int>(config.JackSetupDetails.size()) ||
			specIndex < 0 || specIndex >= static_cast<int>(config.JackSettings.Specs.size())) return;

		auto const& spec = config.JackSettings.Specs[specIndex];
		double maximum = 0;
		if (std::isfinite(spec.D3_Diameter_mm) && spec.D3_Diameter_mm > 0 &&
			std::isfinite(spec.WorkingPressure_bar) && spec.WorkingPressure_bar > 0)
			maximum = ::DHLS_StrandJack::Models::Util::CalcAdvanceForce_ton(
				spec.D3_Diameter_mm, spec.WorkingPressure_bar);

		auto& detail = config.JackSetupDetails[rowIndex];
        const bool typeChanged=detail.JackSelection != ::DHLS_StrandJack::Models::JackTypeLabel(spec);
        if(typeChanged)
        {
            config.PLCSettings.SensorScalingVerified=false;
            config.PLCSettings.EffectiveAreaVerified=false;
        }
		detail.JackSelection = ::DHLS_StrandJack::Models::JackTypeLabel(spec);
        // Preserve a saved or edited maximum on refresh/save. A new jack type restores its calculated rating.
        if(typeChanged || !std::isfinite(detail.MaxWeight_ton) || detail.MaxWeight_ton<=0)
            detail.MaxWeight_ton = maximum;
		detail.MaxWeightUsageRange_Percent = config.ControlSettings.MaxWeightUsageRange_Percent;
		detail.WorkingWeight_ton = detail.MaxWeight_ton * detail.MaxWeightUsageRange_Percent / 100.0;
		auto format = [](double value)
			{
				std::wostringstream text;
				text << std::fixed << std::setprecision(1) << ::DHLS_StrandJack::DisplayUnits::Weight(value) << L" " << ::DHLS_StrandJack::DisplayUnits::WeightLabel();
				return text.str();
			};
		if (maximumText) maximumText.Value(::DHLS_StrandJack::DisplayUnits::Weight(detail.MaxWeight_ton));
		if (workingText) workingText.Text(format(detail.WorkingWeight_ton));
	}

	// Resolve the spec by its persisted label, not by the current order of the specs vector.
	static int SelectedJackSpecIndex(Microsoft::UI::Xaml::Controls::ComboBox const& selection)
	{
		return selection.SelectedIndex();
	}

	static void RefreshJackRow(int rowIndex, Microsoft::UI::Xaml::Controls::Grid const& row,
		Microsoft::UI::Xaml::Controls::ComboBox const& source = nullptr)
	{
		using namespace Microsoft::UI::Xaml::Controls;
		TextBlock working{ nullptr };
        NumberBox maximum{ nullptr };
		ComboBox selection{ nullptr };
		// Reacquire projected controls from the owning row. A weak XAML peer can expire
		// even while the native visual is still owned by the row.
		for (auto const& child : row.Children())
		{
			auto element = child.try_as<FrameworkElement>();
			if (!element) continue;
			const int column = Grid::GetColumn(element);
			if (column == 5) working = child.try_as<TextBlock>();
			else if (column == 6) maximum = child.try_as<NumberBox>();
			else if (column == 7) selection = child.try_as<ComboBox>();
		}
		if (!selection || (source && source != selection)) return;
		RefreshJackWeights(rowIndex, SelectedJackSpecIndex(selection), working, maximum);
	}

	static void RefreshAllJackWeights(Microsoft::UI::Xaml::Controls::ListView const& table)
	{
		int rowIndex = 0;
		for (auto const& item : table.Items())
		{
			if (auto row = item.try_as<Microsoft::UI::Xaml::Controls::Grid>())
				RefreshJackRow(rowIndex, row);
			++rowIndex;
		}
	}

	// =========================================================
	//  MakeColor — ARGB -> Color 직접 생성 (ColorHelper 대체)
	// =========================================================
	static winrt::Windows::UI::Color MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
	{
		winrt::Windows::UI::Color color;
		color.A = a;
		color.R = r;
		color.G = g;
		color.B = b;
		return color;
	}

	static bool IsValidIpv4(std::wstring_view address)
	{
		int parts = 0;
		int digits = 0;
		int value = 0;
		for (const auto ch : address)
		{
			if (ch == L'.')
			{
				if (!digits || ++parts > 3) return false;
				digits = value = 0;
			}
			else
			{
				if (ch < L'0' || ch > L'9' || (digits != 0 && value == 0) || ++digits > 3) return false;
				value = value * 10 + (ch - L'0');
				if (value > 255) return false;
			}
		}
		return parts == 3 && digits != 0;
	}

	static void ConfigureIpv4Input(Microsoft::UI::Xaml::Controls::TextBox const& box)
	{
		Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(box,
			box_value(hstring{L"IPv4 주소: 점으로 구분한 4개 숫자, 각 0~255"}));
		auto indicate = [](auto const& input)
		{
			const bool valid = IsValidIpv4(input.Text());
			input.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush(
				valid ? MakeColor(255,203,213,225) : MakeColor(255,220,38,38)));
		};
		indicate(box);
		box.TextChanged([indicate](auto const& sender, auto const&)
		{
			indicate(sender.template as<Microsoft::UI::Xaml::Controls::TextBox>());
		});
	}

	ControlSettingsView::ControlSettingsView()
	{
		::OutputDebugStringW(L"ControlSettingsView: InitializeComponent begin.\n");
		try
		{
			InitializeComponent();
			::DHLS_StrandJack::Services::Localization::RegisterLanguageSelector(LanguageSelect());
		}
		catch (winrt::hresult_error const& error)
		{
			::OutputDebugStringW(L"ControlSettingsView: InitializeComponent failed: ");
			::OutputDebugStringW(error.message().c_str());
			::OutputDebugStringW(L"\n");
			throw;
		}
		::OutputDebugStringW(L"ControlSettingsView: InitializeComponent complete.\n");
		TraceControlSettings(L"Constructor: configure number formatters begin");
		Windows::Globalization::NumberFormatting::IncrementNumberRounder rounder;
		rounder.Increment(0.1);
		rounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
		Windows::Globalization::NumberFormatting::DecimalFormatter decimalFormatter;
		decimalFormatter.IntegerDigits(1);
		decimalFormatter.FractionDigits(1);
		decimalFormatter.IsGrouped(false);
		decimalFormatter.NumberRounder(rounder);
		using ControlConfig = ::DHLS_StrandJack::Models::ControlSettings;
		auto configureDecimal = [weak = get_weak(), decimalFormatter](
			Microsoft::UI::Xaml::Controls::NumberBox const& number, double ControlConfig::* field)
			{
				number.NumberFormatter(decimalFormatter);
				number.SmallChange(0.1);
				number.ValueChanged([weak, field](auto const& sender, auto const&)
					{
						if (auto self = weak.get(); self && self->m_isLoaded && std::isfinite(sender.Value()))
							::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.*field = sender.Value();
					});
			};
		configureDecimal(RiseDeviationInput(), &ControlConfig::RiseSync_Level1);
		configureDecimal(RiseRangeInput(), &ControlConfig::RiseSync_Level2);
		configureDecimal(FallDeviationInput(), &ControlConfig::FallSync_Level1);
		configureDecimal(FallRangeInput(), &ControlConfig::FallSync_Level2);
		configureDecimal(WeightDeviationInput(), &ControlConfig::WeightDeviation);
		TraceControlSettings(L"Constructor: configure number formatters complete");

		// Subscribe only after all named XAML controls have been created.
		// The view owns these publishers; weak delegates cannot retain or outlive the view.
		PlcCountSelect().SelectionChanged([weak = get_weak()](auto const& sender, auto const& args)
			{
				if (auto self = weak.get()) self->OnPlcCountChanged(sender, args);
			});
		SensorCountSelect().SelectionChanged([weak = get_weak()](auto const& sender, auto const& args)
			{
				if (auto self = weak.get()) self->OnSensorCountChanged(sender, args);
			});
		DashboardJackCount().SelectionChanged([weak = get_weak()](auto const&, auto const&)
			{
				if (auto self = weak.get(); self && self->m_isLoaded) {
					if (!self->ValidateDashboardIps())
					{
						const auto count = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.DashboardJackCount;
						self->m_isLoaded = false;
						self->DashboardJackCount().SelectedIndex((std::clamp)(count, 1, 16) - 1);
						self->m_isLoaded = true;
						return;
					}
					self->PullDashboardFields();
					auto& c = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
					const int count = c.ControlSettings.DashboardJackCount;
					const int plc = (count + 7) / 8;
					const int sensors = (std::min)(count, 8);
					self->m_isLoaded = false;
					self->PlcCountSelect().SelectedIndex(plc - 1);
					self->SensorCountSelect().SelectedIndex(sensors - 1);
					self->m_isLoaded = true;
					self->UpdateDynamicIpFields(plc);
					self->UpdateJackTable(plc, sensors);
					self->PopulateDashboardJackTable();
				}
			});
		BtnDashboardPrev().Click([weak = get_weak()](auto const&, auto const&)
		{
			if (auto self = weak.get(); self && self->m_isLoaded && self->m_dashboardPage > 0)
			{
				if (!self->ValidateDashboardIps()) return;
				self->PullDashboardFields();
				--self->m_dashboardPage;
				self->PopulateDashboardJackTable();
			}
		});
		BtnDashboardNext().Click([weak = get_weak()](auto const&, auto const&)
		{
			if (auto self = weak.get(); self && self->m_isLoaded)
			{
				if (!self->ValidateDashboardIps()) return;
				self->PullDashboardFields();
				++self->m_dashboardPage;
				self->PopulateDashboardJackTable();
			}
		});
		DisplacementDigitsSelect().SelectionChanged([weak = get_weak()](auto const& sender, auto const&)
			{
				if (auto self = weak.get(); self && self->m_isLoaded)
				{
					const int digits = sender.template as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex();
					if (digits < 0 || digits > 3) return;
					auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
					config.SensorSettings.DisplacementDisplayDigits = digits;
					self->UpdateJackTable(config.PLCSettings.PLC_Count, config.PLCSettings.Sensor_HPU_Count);
				}
			});
        auto unitChanged=[weak=get_weak()](auto const&,auto const&){
            if(auto self=weak.get();self&&self->m_isLoaded&&!self->m_updatingUnitInputs){
                auto& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
                const wchar_t* pressures[]={L"bar",L"MPa",L"psi",L"kgf/cm²"};
                int p=self->PressureUnitSelect().SelectedIndex();if(p<0||p>3)return;
                c.SensorSettings.PressureUnit=pressures[p];
                c.SensorSettings.WeightUnit=self->WeightUnitSelect().SelectedIndex()==1?L"kN":L"Ton";
                self->RefreshUnitInputs();self->UpdateJackTable(c.PLCSettings.PLC_Count,c.PLCSettings.Sensor_HPU_Count);
            }
        };
        PressureUnitSelect().SelectionChanged(unitChanged);WeightUnitSelect().SelectionChanged(unitChanged);
        auto bindAdc=[weak=get_weak()](auto const& box,bool pressure){
            
            box.SelectionChanged([weak,pressure](auto const& sender,auto const&){
                if(auto self=weak.get();self&&self->m_isLoaded){
                    const int index=sender.template as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex();if(index<0||index>2)return;
                    const double value=index==0?4000:index==1?16000:64000;
                    auto& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
                    auto& field=pressure?c.SensorSettings.PressureSensor_ADScale:c.SensorSettings.DisplacementSensor_ADScale;
                    if(field!=value)c.PLCSettings.SensorScalingVerified=false;field=value;
                }
            });
        };
        bindAdc(PressureAdScaleInput(),true);bindAdc(StrokeAdScaleInput(),false);

        auto pressureChanged=[weak=get_weak()](auto const&,auto const&){
            if(auto self=weak.get();self&&self->m_isLoaded&&!self->m_updatingUnitInputs){
                auto& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
                if(std::isfinite(self->MaxStopPressureInput().Value()))c.ControlSettings.MaxStop=::DHLS_StrandJack::DisplayUnits::Bar(self->MaxStopPressureInput().Value());
                if(std::isfinite(self->FallStopPressureInput().Value()))c.ControlSettings.FallStop=::DHLS_StrandJack::DisplayUnits::Bar(self->FallStopPressureInput().Value());
                if(std::isfinite(self->LoadMinZeroInput().Value()))c.SensorSettings.LoadMinZero_bar=::DHLS_StrandJack::DisplayUnits::Bar(self->LoadMinZeroInput().Value());
            }
        };
        MaxStopPressureInput().ValueChanged(pressureChanged);FallStopPressureInput().ValueChanged(pressureChanged);LoadMinZeroInput().ValueChanged(pressureChanged);
		LoadToleranceEnabledInput().Click([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get();self&&self->m_isLoaded)
            ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.LoadToleranceEnabled=self->LoadToleranceEnabledInput().IsChecked().Value();});
        MaxWeightRangeInput().Maximum(100);
		MaxWeightRangeInput().ValueChanged([weak = get_weak()](auto const& sender, auto const&)
			{
				if (auto self = weak.get(); self && self->m_isLoaded)
				{
					const double value = sender.Value();
					if (!std::isfinite(value)) return;
					auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
					config.ControlSettings.MaxWeightUsageRange_Percent = (std::clamp)(value, 0.0, 100.0);
					RefreshAllJackWeights(self->JackTableListView());
				}
			});

		Windows::Globalization::NumberFormatting::DecimalFormatter integerFormatter;
        integerFormatter.IntegerDigits(1);integerFormatter.FractionDigits(0);integerFormatter.IsGrouped(false);FilteringInput().NumberFormatter(integerFormatter);
        FilteringInput().ValueChanged([weak=get_weak()](auto const& box,auto const&){if(auto self=weak.get();self&&self->m_isLoaded&&std::isfinite(box.Value()))
            ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.Filtering=std::clamp(std::round(box.Value()),1.0,1000.0);});
        auto extraChanged=[weak=get_weak()](auto const&,auto const&){if(auto self=weak.get();self&&self->m_isLoaded){
            auto& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
            c.ControlSettings.ManualSync=self->ManualSyncInput().IsChecked().Value()?1:0;
            c.SoundSettings.bUseWarningSound=self->WarningSoundEnabled().IsChecked().Value();
            auto gauge=self->GaugePositionSelect().SelectedIndex()==1?L"Bottom":L"Right";
            if(c.SensorSettings.LoadGaugePosition!=gauge)::DHLS_StrandJack::Services::AuditLog::Write("LOAD_GAUGE_POSITION",winrt::to_string(c.SensorSettings.LoadGaugePosition)+" -> "+winrt::to_string(gauge));
            c.SensorSettings.LoadGaugePosition=gauge;c.PLCSettings.PLC_Model=L"LS";
            c.ModelInfo.CustomerName=self->CustomerInput().Text();c.ModelInfo.OperatorName=self->OperatorInput().Text();c.ModelInfo.DocumentNumber=self->DocumentInput().Text();
        }};
        ManualSyncInput().Checked(extraChanged);ManualSyncInput().Unchecked(extraChanged);
        WarningSoundEnabled().Checked(extraChanged);WarningSoundEnabled().Unchecked(extraChanged);
        GaugePositionSelect().SelectionChanged(extraChanged);
        CustomerInput().TextChanged(extraChanged);OperatorInput().TextChanged(extraChanged);DocumentInput().TextChanged(extraChanged);
        ArrowColorPicker().ColorChanged([weak=get_weak()](auto const& picker,auto const&){if(auto self=weak.get();self&&self->m_isLoaded){
            auto color=picker.Color();wchar_t hex[10]{};swprintf_s(hex,L"#%02X%02X%02X",color.R,color.G,color.B);
            auto& savedColor=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.ArrowColor;
            if(savedColor!=hex)::DHLS_StrandJack::Services::AuditLog::Write("ARROW_COLOR",winrt::to_string(savedColor)+" -> "+winrt::to_string(hex));
            savedColor=hex;
            self->ArrowColorPreview().Fill(Microsoft::UI::Xaml::Media::SolidColorBrush(color));
        }});
        CautionSoundBrowse().Click([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get())self->PickSoundAsync(false);});
        WarningSoundBrowse().Click([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get())self->PickSoundAsync(true);});
        // View-owned controls use weak delegates; no subscription survives the view.
        
		this->Loaded([weak = get_weak()](auto const& sender, auto const& args)
			{
				if (auto self = weak.get()) self->OnLoaded(sender, args);
			});
	}

    winrt::fire_and_forget ControlSettingsView::PickSoundAsync(bool warning){
        auto lifetime=get_strong();if(m_soundPickerBusy||!IsLoaded())co_return;
        m_soundPickerBusy=true;struct Reset{bool& b;~Reset(){b=false;}} reset{m_soundPickerBusy};
        auto model=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName;
        try{
            Windows::Storage::Pickers::FileOpenPicker picker;picker.FileTypeFilter().Append(L".wav");
            HWND hwnd=MainWindow::GetHwnd();
            if(m_hostWindow)check_hresult(m_hostWindow.as<IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
            auto file=co_await picker.PickSingleFileAsync();if(!file||!IsLoaded())co_return;
            auto& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();if(c.ModelInfo.ModelName!=model)co_return;
            (warning?c.SoundSettings.WarningSound:c.SoundSettings.CautionSound)=file.Path();
            (warning?WarningSoundPath():CautionSoundPath()).Text(file.Path());
            ::DHLS_StrandJack::Services::AuditLog::Write("SOUND_FILE",warning?"Warning WAV selected":"Caution WAV selected");
        }catch(hresult_error const& e){::OutputDebugStringW(e.message().c_str());}
    }
    void ControlSettingsView::RefreshScaleWarning(){
        auto const& config=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
        const bool valid=::DHLS_StrandJack::Models::JackMeasurementRangesMatch(config);
        ScaleConsistencyWarning().Text(::DHLS_StrandJack::Models::JackMeasurementRangeWarning(::DHLS_StrandJack::Services::Localization::IsEnglish()));
        ScaleConsistencyWarning().Visibility(valid?Microsoft::UI::Xaml::Visibility::Collapsed:Microsoft::UI::Xaml::Visibility::Visible);
    }
    void ControlSettingsView::RefreshUnitInputs(){
        m_updatingUnitInputs=true;
        auto const& c=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
        Windows::Globalization::NumberFormatting::DecimalFormatter format;format.IntegerDigits(1);format.FractionDigits(::DHLS_StrandJack::DisplayUnits::PressureDigits());format.IsGrouped(false);
        Windows::Globalization::NumberFormatting::IncrementNumberRounder pressureRounder;pressureRounder.Increment(std::pow(10.0,-::DHLS_StrandJack::DisplayUnits::PressureDigits()));pressureRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);format.NumberRounder(pressureRounder);
        for(auto box:{MaxStopPressureInput(),FallStopPressureInput(),LoadMinZeroInput()}){box.NumberFormatter(format);box.Minimum(0);box.SmallChange(0.1);box.Header(box_value(::DHLS_StrandJack::DisplayUnits::PressureLabel()));}
        MaxStopPressureInput().Value(::DHLS_StrandJack::DisplayUnits::Pressure(c.ControlSettings.MaxStop));
        FallStopPressureInput().Value(::DHLS_StrandJack::DisplayUnits::Pressure(c.ControlSettings.FallStop));
        LoadMinZeroInput().Value(::DHLS_StrandJack::DisplayUnits::Pressure(c.SensorSettings.LoadMinZero_bar));
        m_updatingUnitInputs=false;
    }

	int32_t ControlSettingsView::MyProperty()
	{
		throw hresult_not_implemented();
	}

	void ControlSettingsView::MyProperty(int32_t /*value*/)
	{
		throw hresult_not_implemented();
	}

	// ========================================
	// 저장 버튼 — 현재 Config를 .json 파일로 저장
	// ========================================
	void ControlSettingsView::OnSaveClick(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		auto modelName = config.ModelInfo.ModelName;
		if (modelName.empty())
			return;

		if (!ValidateDashboardIps()) return;
		auto& ips = config.PLCSettings.PLC_IPs;
		auto fields = DynamicIpContainer().Items();
		for (uint32_t i = 0; i < fields.Size(); ++i)
		{
			using namespace Microsoft::UI::Xaml::Controls;
			auto input = fields.GetAt(i).as<Grid>().Children().GetAt(1).as<Grid>().Children().GetAt(0).as<TextBox>();
			if (!IsValidIpv4(input.Text()))
			{
				input.Focus(FocusState::Programmatic);
				return;
			}
		}
		for (uint32_t i = 0; i < fields.Size() && i < ips.size(); ++i)
		{
			using namespace Microsoft::UI::Xaml::Controls;
			const auto address = fields.GetAt(i).as<Grid>().Children().GetAt(1).as<Grid>().Children().GetAt(0).as<TextBox>().Text();
			if (ips[i] == address.c_str()) continue;
			config.PLCSettings.InvalidateEquipmentVerification();
			ips[i] = address.c_str();
		}

		config.PLCSettings.PLC_Model=L"LS";
        // Persist the currently displayed selections and their derived weights together.
		RefreshAllJackWeights(JackTableListView());
		PullDashboardFields();
		std::error_code ec;
		const auto modelsDirectory = ::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory();
		std::filesystem::create_directories(modelsDirectory, ec);

		std::wstring path = (modelsDirectory / (modelName + L".json")).wstring();
		if(::DHLS_StrandJack::Services::ConfigManager::Instance().SaveToJson(path)) {
            m_originalConfig=config;
            m_originalJson=::DHLS_StrandJack::Services::ConfigManager::SerializeSnapshot(config,L"");
            if(m_audit)m_audit->MarkSaved();
        }
	}

	// Capture the view on its UI thread, then encode/store PNG on a worker thread.
	void ControlSettingsView::OnCaptureClick(
		Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
	{
		CaptureAndReportAsync();
	}

	winrt::fire_and_forget ControlSettingsView::CaptureAndReportAsync()
	{
		auto lifetime = get_strong();
		if (m_captureInProgress || !XamlRoot()) co_return;
		m_captureInProgress = true;
		winrt::hstring failure;
		try { co_await CaptureViewAsync(); }
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

	Windows::Foundation::IAsyncAction ControlSettingsView::CaptureViewAsync()
	{
		auto lifetime = get_strong();
		if (!XamlRoot()) throw winrt::hresult_canceled();
		Media::Imaging::RenderTargetBitmap bitmap;
		co_await bitmap.RenderAsync(*this);
		if (!XamlRoot()) throw winrt::hresult_canceled();
		const auto width = static_cast<uint32_t>(bitmap.PixelWidth());
		const auto height = static_cast<uint32_t>(bitmap.PixelHeight());
		if (!width || !height)
			throw winrt::hresult_error(E_FAIL, ::DHLS_StrandJack::Services::Localization::Text(L"캡처할 화면이 준비되지 않았습니다."));
		auto buffer = co_await bitmap.GetPixelsAsync();
		if (!XamlRoot()) throw winrt::hresult_canceled();
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
		name << L"ControlSettings_" << std::put_time(&local, L"%H%M%S") << L".png";
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
	// ========================================
	// 나가기 버튼 — 별도 Window 닫기 + MainMenuView로 이동
	// ========================================
	void ControlSettingsView::OnExitClick(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		PullDashboardFields();
        ExitWithConfirmationAsync();
    }

    winrt::fire_and_forget ControlSettingsView::ExitWithConfirmationAsync()
    {
        auto lifetime=get_strong();
        if(m_exitBusy)co_return;
        m_exitBusy=true;
        try {
            auto& manager=::DHLS_StrandJack::Services::ConfigManager::Instance();
            bool dirty=(m_audit&&m_audit->Dirty())||manager.SerializeSnapshot(manager.GetConfig(),L"")!=m_originalJson;
            if(dirty){
                const auto model=manager.GetConfig().ModelInfo.ModelName;
                if(!co_await ::DHLS_StrandJack::Services::ConfirmDiscardAsync(XamlRoot(),::DHLS_StrandJack::Services::Localization::IsEnglish())){
                    m_exitBusy=false;co_return;}
                if(!IsLoaded()||manager.GetConfig().ModelInfo.ModelName!=model){m_exitBusy=false;co_return;}
                manager.GetConfig()=m_originalConfig;
                ::DHLS_StrandJack::Services::AuditLog::Write("DISCARD","ControlSettings");
            }
            ::DHLS_StrandJack::Services::AuditLog::Write("VIEW_CLOSE","ControlSettings");
            m_audit.reset();
            if(m_hostWindow){auto host=m_hostWindow;m_hostWindow=nullptr;host.Close();}
            else MainWindow::NavigateToView(winrt::make<MainMenuView>(),1280,800);
        }catch(...){::DHLS_StrandJack::Services::AuditLog::Write("ERROR","ControlSettings exit confirmation failed");}
        m_exitBusy=false;
    }

	// ========================================
	// 잭 배치 버튼 — JackArrangementView로 화면 전환
	// ========================================
	void ControlSettingsView::OnJackLayoutClick(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		if (ArrangementOverlay().Visibility() == Visibility::Visible) return;
		if (!ValidateDashboardIps()) return;
		PullDashboardFields();
		auto arrangement = winrt::make_self<JackArrangementView>();
		arrangement->Dismiss = [weak = get_weak()](bool confirmed)
			{
				if (auto self = weak.get())
				{
					self->ArrangementOverlay().Visibility(Visibility::Collapsed);
					self->ArrangementOverlay().Children().Clear();
					self->SettingsContent().Visibility(Visibility::Visible);
					if (confirmed) self->PopulateDashboardJackTable();
					self->BtnJackLayout().Focus(FocusState::Programmatic);
				}
			};
		Microsoft::UI::Xaml::Controls::Viewbox box;
		box.Stretch(Microsoft::UI::Xaml::Media::Stretch::Uniform);
		box.Margin(ThicknessHelper::FromUniformLength(0));
		box.Child(arrangement.as<UIElement>());
		ArrangementOverlay().Children().Append(box);
		SettingsContent().Visibility(Visibility::Collapsed);
		ArrangementOverlay().Visibility(Visibility::Visible);
	}

	void ControlSettingsView::PopulateDashboardJackTable()
	{
		using namespace Microsoft::UI::Xaml::Controls;
		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		auto list = DashboardJackListView();
		list.Items().Clear();
		const int count = SelectedDashboardJackCount(DashboardJackCount());
		const int pages = (std::max)(1, (count + DashboardPageSize - 1) / DashboardPageSize);
		m_dashboardPage = (std::clamp)(m_dashboardPage, 0, pages - 1);
		DashboardPageText().Text(hstring{std::to_wstring(m_dashboardPage + 1) + L" / " + std::to_wstring(pages)});
		BtnDashboardPrev().IsEnabled(m_dashboardPage > 0);
		BtnDashboardNext().IsEnabled(m_dashboardPage + 1 < pages);
		for (int i = m_dashboardPage * DashboardPageSize; i < (std::min)(count, (m_dashboardPage + 1) * DashboardPageSize); ++i)
		{
			auto row = Grid();
			row.Height(30);
			row.ColumnSpacing(4);
			for (double width : {0.55, 1.32, 2.08, 0.8, 1.05, 1.05, 1.05, 1.15})
			{
				auto column = ColumnDefinition();
				column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
				row.ColumnDefinitions().Append(column);
			}
			const bool hasDetail = i < static_cast<int>(config.JackSetupDetails.size());
			const auto* detail = hasDetail ? &config.JackSetupDetails[i] : nullptr;
			const int sensors = (std::max)(1, config.PLCSettings.Sensor_HPU_Count);
			const int plc = i / sensors;
			auto hpuText = TextBlock();
			const int line = i / 8 + 1;
			const int port = i % 8 + 1;
			hpuText.Text(config.ControlSettings.JackArrangementTemplate != 1
				? L"HPU" + std::to_wstring(line) + L"-" + std::to_wstring(port)
				: L"HPU" + std::to_wstring(port));
			hpuText.TextAlignment(TextAlignment::Center);
			auto hpu = Button();
			hpu.Content(hpuText);
			hpu.MinWidth(0);
			hpu.MinHeight(0);
			hpu.Padding(Thickness{0,0,0,0});
			hpu.BorderThickness(Thickness{0,0,0,0});
			hpu.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(MakeColor(0,0,0,0)));
			hpu.HorizontalAlignment(HorizontalAlignment::Stretch);
			hpu.VerticalAlignment(VerticalAlignment::Stretch);
			hpu.Click([weak = get_weak(), i](auto const&, auto const&)
			{
				if (auto self = weak.get())
				{
					self->m_selectedDashboardJack = i;
					int index = self->m_dashboardPage * DashboardPageSize;
					for (auto const& item : self->DashboardJackListView().Items())
						item.template as<Grid>().Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
							index++ == i ? MakeColor(255,255,235,99) : MakeColor(255,255,255,255)));
				}
			});
			row.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
				i == m_selectedDashboardJack ? MakeColor(255,255,235,99) : MakeColor(255,255,255,255)));
			Grid::SetColumn(hpu, 0); row.Children().Append(hpu);
			auto ip = TextBox();
			ip.Text(detail && !detail->IPAddress.empty() ? detail->IPAddress :
				plc < static_cast<int>(config.PLCSettings.PLC_IPs.size()) ? config.PLCSettings.PLC_IPs[plc] : L"");
			ip.VerticalAlignment(VerticalAlignment::Center);
			ip.Height(29);
			ConfigureIpv4Input(ip);
			Grid::SetColumn(ip, 1); row.Children().Append(ip);
			auto type = ComboBox(); type.HorizontalAlignment(HorizontalAlignment::Stretch);
			int selected = 0;
			for (int j = 0; j < static_cast<int>(config.JackSettings.Specs.size()); ++j)
			{
				auto item = ComboBoxItem();
				item.Content(box_value(hstring{::DHLS_StrandJack::Models::JackTypeLabel(config.JackSettings.Specs[j])}));
				type.Items().Append(item);
				if (detail && detail->JackSelection == ::DHLS_StrandJack::Models::JackTypeLabel(config.JackSettings.Specs[j])) selected = j;
			}
			if (type.Items().Size()) type.SelectedIndex(selected);
			Grid::SetColumn(type, 2); row.Children().Append(type);
			auto load = TextBlock();
			load.Text(type.Items().Size() ? to_hstring(config.JackSettings.Specs[selected].Capacity_ton) : L"0");
			load.VerticalAlignment(VerticalAlignment::Center); load.HorizontalAlignment(HorizontalAlignment::Center);
			Grid::SetColumn(load, 3); row.Children().Append(load);
			type.SelectionChanged([weak = get_weak(), load](auto const& sender, auto const&)
			{
				if (!weak.get()) return;
				const int index = sender.template as<ComboBox>().SelectedIndex();
				const auto& specs = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSettings.Specs;
				if (index >= 0 && index < static_cast<int>(specs.size())) load.Text(to_hstring(specs[index].Capacity_ton));
			});
			auto stop = NumberBox(); stop.Value(detail ? detail->StopLoadPercent : 120); stop.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact); stop.HorizontalAlignment(HorizontalAlignment::Stretch);
			Grid::SetColumn(stop, 4); row.Children().Append(stop);
			auto title = TextBox();
			const auto defaultTitle = type.Items().Size() ? std::to_wstring(static_cast<int>(config.JackSettings.Specs[selected].Capacity_ton)) + L"T_" + std::to_wstring(i + 1) : L"";
			title.Text(detail && !detail->JackTitle.empty() ? hstring{detail->JackTitle} : hstring{defaultTitle});
			title.HorizontalAlignment(HorizontalAlignment::Stretch);
			Grid::SetColumn(title, 5); row.Children().Append(title);
			auto engine = NumberBox(); engine.Value(detail ? detail->EngineControlPercent : 100); engine.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact); engine.HorizontalAlignment(HorizontalAlignment::Stretch);
			Grid::SetColumn(engine, 6); row.Children().Append(engine);
			auto temp = NumberBox(); temp.Value(detail ? detail->TemperatureAdResolution : 0); temp.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact); temp.HorizontalAlignment(HorizontalAlignment::Stretch);
			Grid::SetColumn(temp, 7); row.Children().Append(temp);
			list.Items().Append(row);
		}
	}

	bool ControlSettingsView::ValidateDashboardIps()
	{
		using namespace Microsoft::UI::Xaml::Controls;
		for (auto const& item : DashboardJackListView().Items())
		{
			auto input = item.as<Grid>().Children().GetAt(1).as<TextBox>();
			if (IsValidIpv4(input.Text())) continue;
			input.Focus(FocusState::Programmatic);
			return false;
		}
		return true;
	}

	void ControlSettingsView::PullDashboardFields()
	{
		using namespace Microsoft::UI::Xaml::Controls;
		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		auto& c = config.ControlSettings;
		auto safe = [](double value, double fallback) { return std::isfinite(value) ? value : fallback; };
		c.PressureDisplayDigits = (std::clamp)(DashboardPressureDigits().SelectedIndex(), 0, 2);
		c.WeightDisplayDigits = (std::clamp)(DashboardWeightDigits().SelectedIndex()+1, 1, 2);
		c.SpeedControlResolution = static_cast<int>(safe(DashboardSpeedResolution().Value(), c.SpeedControlResolution));
		c.PlcAdResolution = static_cast<int>(safe(DashboardPlcAdResolution().Value(), c.PlcAdResolution));
		c.Filtering = safe(DashboardFilter().Value(), c.Filtering);
		c.DashboardJackCount = SelectedDashboardJackCount(DashboardJackCount());
		c.SyncDeviationMm = safe(DashboardSyncDeviation().Value(), c.SyncDeviationMm);
		c.AllowedDeviationMm = safe(DashboardAllowedDeviation().Value(), c.AllowedDeviationMm);
		c.AutoOverloadPercent = safe(DashboardAutoOverload().Value(), c.AutoOverloadPercent);
		c.ManualOverloadPercent = safe(DashboardManualOverload().Value(), c.ManualOverloadPercent);
		c.WedgeExtraPressureSec = safe(DashboardWedgeTime().Value(), c.WedgeExtraPressureSec);
		c.OverloadAllowedSec = safe(DashboardOverloadTime().Value(), c.OverloadAllowedSec);
		c.PretensionRangeMm = safe(DashboardPretensionRange().Value(), c.PretensionRangeMm);
		c.CloseUpperWedgeOnLowering = DashboardWedgeClosed().IsChecked().Value();
		c.SynchronizeTotalLift = DashboardSyncTotal().IsChecked().Value();
		c.AccumulateManualDistance = DashboardManualAccumulation().IsOn();
		c.DecelerationSpeedHz = safe(DashboardDecelerationSpeed().Value(), c.DecelerationSpeedHz);
		c.DecelerationRangeMm = safe(DashboardDecelerationRange().Value(), c.DecelerationRangeMm);
		c.RangeToleranceMm = safe(DashboardRangeTolerance().Value(), c.RangeToleranceMm);
		c.TemperatureMinC = safe(DashboardTemperatureMin().Value(), c.TemperatureMinC);
		c.TemperatureMaxC = safe(DashboardTemperatureMax().Value(), c.TemperatureMaxC);
		config.SensorSettings.PressureUnit = DashboardPressureUnit().SelectedIndex()==1 ? L"MPa" : DashboardPressureUnit().SelectedIndex()==2 ? L"psi" : L"bar";
		config.SensorSettings.WeightUnit = DashboardWeightUnit().SelectedIndex()==1 ? L"kN" : L"Ton";
		auto items = DashboardJackListView().Items();
		const int first = m_dashboardPage * DashboardPageSize;
		const int limit = (std::min)(static_cast<int>(items.Size()), static_cast<int>(config.JackSetupDetails.size()) - first);
		for (int j=0; j<limit; ++j)
		{
			const int i = first + j;
			auto row = items.GetAt(j).as<Grid>(); auto children = row.Children(); auto& d = config.JackSetupDetails[i];
			const auto address = children.GetAt(1).as<TextBox>().Text();
			if (IsValidIpv4(address)) d.IPAddress = address.c_str();
			const int type = children.GetAt(2).as<ComboBox>().SelectedIndex();
			if (type>=0 && type<static_cast<int>(config.JackSettings.Specs.size()))
				RefreshJackWeights(i, type, TextBlock{nullptr}, NumberBox{nullptr});
			d.StopLoadPercent = safe(children.GetAt(4).as<NumberBox>().Value(), d.StopLoadPercent);
			d.JackTitle = children.GetAt(5).as<TextBox>().Text();
			d.EngineControlPercent = safe(children.GetAt(6).as<NumberBox>().Value(), d.EngineControlPercent);
			d.TemperatureAdResolution = static_cast<int>(safe(children.GetAt(7).as<NumberBox>().Value(), d.TemperatureAdResolution));
		}
	}

	// ========================================
	// Loaded — XAML 트리 완전 로드 후 동적 UI 생성
	// ========================================
	void ControlSettingsView::OnLoaded(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		wchar_t const* stage = L"OnLoaded: apply config values";
		TraceControlSettings(stage);
		try
		{
			m_isLoaded = false;
			const auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
            const auto pressure=::DHLS_StrandJack::DisplayUnits::PressureLabel();
            PressureUnitSelect().SelectedIndex(pressure==L"MPa"?1:pressure==L"psi"?2:pressure==L"kgf/cm²"?3:0);
            WeightUnitSelect().SelectedIndex(::DHLS_StrandJack::DisplayUnits::WeightLabel()==L"kN"?1:0);
            RefreshUnitInputs();
            PressureAdScaleInput().SelectedIndex(config.SensorSettings.PressureSensor_ADScale==4000?0:config.SensorSettings.PressureSensor_ADScale==16000?1:config.SensorSettings.PressureSensor_ADScale==64000?2:-1);
            StrokeAdScaleInput().SelectedIndex(config.SensorSettings.DisplacementSensor_ADScale==4000?0:config.SensorSettings.DisplacementSensor_ADScale==16000?1:config.SensorSettings.DisplacementSensor_ADScale==64000?2:-1);
			ModelNameTextBox().Text(winrt::hstring{ config.ModelInfo.ModelName });
            CustomerInput().Text(config.ModelInfo.CustomerName);OperatorInput().Text(config.ModelInfo.OperatorName);DocumentInput().Text(config.ModelInfo.DocumentNumber);
            FilteringInput().Value(std::isfinite(config.ControlSettings.Filtering)?std::clamp(std::round(config.ControlSettings.Filtering),1.0,1000.0):1);
            ManualSyncInput().IsChecked(config.ControlSettings.ManualSync!=0);WarningSoundEnabled().IsChecked(config.SoundSettings.bUseWarningSound);
            CautionSoundPath().Text(config.SoundSettings.CautionSound);WarningSoundPath().Text(config.SoundSettings.WarningSound);
            GaugePositionSelect().SelectedIndex(config.SensorSettings.LoadGaugePosition==L"Bottom"?1:0);PlcModelSelect().SelectedIndex(0);
            auto color=Windows::UI::Color{255,96,255,32};
            try{if(!config.ControlSettings.ArrowColor.empty())color=unbox_value<Windows::UI::Color>(Microsoft::UI::Xaml::Markup::XamlBindingHelper::ConvertValue(winrt::xaml_typename<Windows::UI::Color>(),box_value(hstring{config.ControlSettings.ArrowColor})));}catch(...){}
            ArrowColorPicker().Color(color);ArrowColorPreview().Fill(Microsoft::UI::Xaml::Media::SolidColorBrush(color));
			RiseDeviationInput().Value(config.ControlSettings.RiseSync_Level1);
			RiseRangeInput().Value(config.ControlSettings.RiseSync_Level2);
			FallDeviationInput().Value(config.ControlSettings.FallSync_Level1);
			FallRangeInput().Value(config.ControlSettings.FallSync_Level2);
			WeightDeviationInput().Value(config.ControlSettings.WeightDeviation);
            LoadToleranceEnabledInput().IsChecked(config.ControlSettings.LoadToleranceEnabled);
			DisplacementDigitsSelect().SelectedIndex((std::clamp)(config.SensorSettings.DisplacementDisplayDigits, 0, 3));

			const int plcCount = (std::clamp)(config.PLCSettings.PLC_Count, 1, 4);
			const int sensorCount = (std::clamp)(config.PLCSettings.Sensor_HPU_Count, 1, 8);
			PlcCountSelect().SelectedIndex(plcCount - 1);
			SensorCountSelect().SelectedIndex(sensorCount - 1);
			MaxWeightRangeInput().Value((std::clamp)(config.ControlSettings.MaxWeightUsageRange_Percent, 0.0, 100.0));
			stage = L"OnLoaded: UpdateDynamicIpFields";
			TraceControlSettings(stage);
			UpdateDynamicIpFields(plcCount);
			stage = L"OnLoaded: UpdateJackTable";
			TraceControlSettings(stage);
			UpdateJackTable(plcCount, sensorCount);
			DashboardModelName().Text(config.ModelInfo.ModelName);
			DashboardPressureUnit().SelectedIndex(pressure==L"MPa"?1:pressure==L"psi"?2:0);
			DashboardWeightUnit().SelectedIndex(::DHLS_StrandJack::DisplayUnits::WeightLabel()==L"kN"?1:0);
			DashboardPressureDigits().SelectedIndex((std::clamp)(config.ControlSettings.PressureDisplayDigits,0,2));
			DashboardWeightDigits().SelectedIndex((std::clamp)(config.ControlSettings.WeightDisplayDigits-1,0,1));
			DashboardSpeedResolution().Value(config.ControlSettings.SpeedControlResolution);
			DashboardPlcAdResolution().Value(config.ControlSettings.PlcAdResolution);
			DashboardFilter().Value(config.ControlSettings.Filtering);
			const int configuredJacks = config.ControlSettings.DashboardJackCount;
			for (uint32_t n = 0; n < DashboardJackCount().Items().Size(); ++n)
			{
				DashboardJackCount().SelectedIndex(static_cast<int>(n));
				if (SelectedDashboardJackCount(DashboardJackCount()) == configuredJacks) break;
			}
			DashboardSyncDeviation().Value(config.ControlSettings.SyncDeviationMm);
			DashboardAllowedDeviation().Value(config.ControlSettings.AllowedDeviationMm);
			DashboardAutoOverload().Value(config.ControlSettings.AutoOverloadPercent);
			DashboardManualOverload().Value(config.ControlSettings.ManualOverloadPercent);
			DashboardWedgeTime().Value(config.ControlSettings.WedgeExtraPressureSec);
			DashboardOverloadTime().Value(config.ControlSettings.OverloadAllowedSec);
			DashboardPretensionRange().Value(config.ControlSettings.PretensionRangeMm);
			DashboardWedgeClosed().IsChecked(config.ControlSettings.CloseUpperWedgeOnLowering);
			DashboardWedgeOpen().IsChecked(!config.ControlSettings.CloseUpperWedgeOnLowering);
			DashboardSyncTotal().IsChecked(config.ControlSettings.SynchronizeTotalLift);
			DashboardSyncLift().IsChecked(!config.ControlSettings.SynchronizeTotalLift);
			DashboardManualAccumulation().IsOn(config.ControlSettings.AccumulateManualDistance);
			DashboardDecelerationSpeed().Value(config.ControlSettings.DecelerationSpeedHz);
			DashboardDecelerationRange().Value(config.ControlSettings.DecelerationRangeMm);
			DashboardRangeTolerance().Value(config.ControlSettings.RangeToleranceMm);
			DashboardTemperatureMin().Value(config.ControlSettings.TemperatureMinC);
			DashboardTemperatureMax().Value(config.ControlSettings.TemperatureMaxC);
			PopulateDashboardJackTable();
			m_isLoaded = true;
            RefreshScaleWarning();
            m_originalConfig=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
            m_originalJson=::DHLS_StrandJack::Services::ConfigManager::SerializeSnapshot(m_originalConfig,L"");
            m_audit=::DHLS_StrandJack::Services::SettingsAudit::Attach(*this,"ControlSettings");
			TraceControlSettings(L"OnLoaded: complete; awaiting XAML layout/render");
		}
		catch (winrt::hresult_error const& error)
		{
#ifdef _DEBUG
			wchar_t code[128]{};
			swprintf_s(code, L"[ControlSettings] FAILED HRESULT=0x%08X\n", static_cast<unsigned>(error.code().value));
			::OutputDebugStringW(code);
			TraceControlSettings(stage);
			::OutputDebugStringW((std::wstring{error.message().c_str()} + L"\n").c_str());
#endif
			throw;
		}
	}

	// ========================================
	// PLC # 콤보 변경 — IP 입력 필드 동적 생성/재구성
	// ========================================
	void ControlSettingsView::OnPlcCountChanged(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		// SelectionChanged can fire inside InitializeComponent, before later named controls exist.
		if (!m_isLoaded) return;
		int plcCount = PlcCountSelect().SelectedIndex() + 1;
		int sensorCount = SensorCountSelect().SelectedIndex() + 1;
		auto& ips = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings.PLC_IPs;
		ips.clear();
		for (int i = 0; i < plcCount; ++i) ips.push_back(::DHLS_StrandJack::Models::DefaultPlcIp(i));
		UpdateDynamicIpFields(plcCount);
		UpdateJackTable(plcCount, sensorCount);
	}

	// ========================================
	// Sensor # 콤보 변경 — 잭 테이블 행 수 재구성
	// ========================================
	void ControlSettingsView::OnSensorCountChanged(
		winrt::Windows::Foundation::IInspectable const& /*sender*/,
		winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		if (!m_isLoaded) return;
		int plcCount = PlcCountSelect().SelectedIndex() + 1;
		int sensorCount = SensorCountSelect().SelectedIndex() + 1;
		UpdateJackTable(plcCount, sensorCount);
	}

	// ========================================
	// PLC#N IP 입력 행을 DynamicIpContainer에 동적 생성
	// ========================================
	void ControlSettingsView::UpdateDynamicIpFields(int plcCount)
	{
		auto container = DynamicIpContainer();
		container.Items().Clear();

		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		auto& plcIps = config.PLCSettings.PLC_IPs;
		plcIps.resize(plcCount);

		for (int i = 0; i < plcCount; ++i)
		{
			// Config에 IP가 있으면 사용, 없으면 빈 문자열
			if (plcIps[i].empty()) plcIps[i] = ::DHLS_StrandJack::Models::DefaultPlcIp(i);
			std::wstring ip = plcIps[i];

			// 라벨: "PLC#1 IP" ~ "PLC#4 IP"
			std::wstring label = L"PLC#" + std::to_wstring(i + 1) + L" IP";

			// --- 행 Grid ---
			auto rowGrid = Microsoft::UI::Xaml::Controls::Grid();
			rowGrid.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0x64, 0x74, 0x8B)));
			rowGrid.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0xE2, 0xE8, 0xF0)));
			rowGrid.BorderThickness(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0, 0, 0, 1));
			rowGrid.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(8));

			auto col1 = Microsoft::UI::Xaml::Controls::ColumnDefinition();
			col1.Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(120));
			rowGrid.ColumnDefinitions().Append(col1);

			auto col2 = Microsoft::UI::Xaml::Controls::ColumnDefinition();
			col2.Width(Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(1, Microsoft::UI::Xaml::GridUnitType::Star));
			rowGrid.ColumnDefinitions().Append(col2);

			// 라벨 TextBlock
			auto labelTb = Microsoft::UI::Xaml::Controls::TextBlock();
			labelTb.Text(label);
			labelTb.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::Colors::White()));
			labelTb.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
			labelTb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Center);
			labelTb.FontWeight(Microsoft::UI::Text::FontWeights::Medium());
			labelTb.FontSize(14);
			rowGrid.SetColumn(labelTb, 0);
			rowGrid.Children().Append(labelTb);

			// IP 입력 TextBox (흰 배경 컨테이너)
			auto whiteGrid = Microsoft::UI::Xaml::Controls::Grid();
			whiteGrid.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::Colors::White()));
			whiteGrid.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(4));
			rowGrid.SetColumn(whiteGrid, 1);

			auto ipTextBox = Microsoft::UI::Xaml::Controls::TextBox();
			ipTextBox.Text(ip);
			ConfigureIpv4Input(ipTextBox);
			ipTextBox.LostFocus([weak = get_weak(), i](auto const& sender, auto const&)
			{
				if (!weak.get()) return;
				const auto address = sender.template as<Microsoft::UI::Xaml::Controls::TextBox>().Text();
				if (!IsValidIpv4(address)) return;
				auto& plc = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings;
				if (i >= static_cast<int>(plc.PLC_IPs.size()) || plc.PLC_IPs[i] == address.c_str()) return;
				plc.InvalidateEquipmentVerification();
				plc.PLC_IPs[i] = address.c_str();
			});
			ipTextBox.TextAlignment(Microsoft::UI::Xaml::TextAlignment::Center);
			ipTextBox.MinHeight(28);
			ipTextBox.CornerRadius(Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(4));
			// 입력 배경 색 (XAML의 InputBackgroundBrush = #fefce8)
			ipTextBox.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0xFE, 0xFC, 0xE8)));
			whiteGrid.Children().Append(ipTextBox);

			rowGrid.Children().Append(whiteGrid);

			container.Items().Append(rowGrid);
		}
	}

	// ========================================
	// 잭 테이블 행을 PLC# × Sensor# 만큼 동적 생성
	// ========================================
	void ControlSettingsView::UpdateJackTable(int plcCount, int sensorCount)
	{
		auto listView = JackTableListView();
		listView.Items().Clear();

		int totalRows = plcCount * sensorCount;

		auto& config = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
		auto& details = config.JackSetupDetails;
		// Retain data by PLC/sensor identity when the number of sensors changes.
		const auto previous = details;
		const int previousSensors = (std::max)(1, config.PLCSettings.Sensor_HPU_Count);
		const bool resetPorts = !m_isLoaded || previousSensors != sensorCount || config.PLCSettings.PLC_Count != plcCount;
		const int storedRows = (std::max)(totalRows, config.ControlSettings.DashboardJackCount);
		details.assign(storedRows, {});
		for (int i = 0; i < storedRows; ++i)
		{
			const int plc = i / sensorCount;
			const int sensor = i % sensorCount;
			const int oldIndex = plc * previousSensors + sensor;
			if (sensor < previousSensors && oldIndex < static_cast<int>(previous.size()))
				details[i] = previous[oldIndex];
			details[i].No = i + 1;
			details[i].JackNumber = sensor + 1;
			if (resetPorts || details[i].Port < 1 || details[i].Port > sensorCount)
				details[i].Port = sensor + 1;
		}
        if(config.PLCSettings.PLC_Count!=plcCount || config.PLCSettings.Sensor_HPU_Count!=sensorCount)
            config.PLCSettings.InvalidateEquipmentVerification();
		config.PLCSettings.PLC_Count = plcCount;
		config.PLCSettings.Sensor_HPU_Count = sensorCount;
		const int displacementDigits = (std::clamp)(config.SensorSettings.DisplacementDisplayDigits, 0, 3);
		const double displacementStep = std::pow(10.0, -displacementDigits);
		Windows::Globalization::NumberFormatting::IncrementNumberRounder displacementRounder;
		displacementRounder.Increment(displacementStep);
		displacementRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
		Windows::Globalization::NumberFormatting::DecimalFormatter displacementFormatter;
		displacementFormatter.IntegerDigits(1);
		displacementFormatter.FractionDigits(displacementDigits);
		displacementFormatter.IsGrouped(false);
		displacementFormatter.NumberRounder(displacementRounder);

		for (int i = 0; i < totalRows; ++i)
		{
			int plcIndex = i / sensorCount;       // 0-based PLC index
			int jackNo = i + 1;
			int port = (i % sensorCount) + 1;

			// Config에서 기존 값 로드 (있으면 사용, 없으면 기본값)
			double xMm = 0.0, yMm = 0.0;
			double target = 0.0;
			double workingWeight = 0.0;
			double maxWeight = 0.0;

			if (i < static_cast<int>(details.size()))
			{
				port = details[i].Port;
				xMm = details[i].X_mm;
				yMm = details[i].Y_mm;
				target = details[i].Target;
				workingWeight = details[i].WorkingWeight_ton;
				maxWeight = details[i].MaxWeight_ton;
			}

			// 행 Grid (HeaderTemplate의 ColumnDefinitions와 동일한 비율)
			auto rowGrid = Microsoft::UI::Xaml::Controls::Grid();
			rowGrid.Height(44);
			rowGrid.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(4));
			rowGrid.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::Colors::White()));
			rowGrid.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0xCB, 0xD5, 0xE1)));
			rowGrid.BorderThickness(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0, 0, 0, 1));

			// ColumnDefinitions: 1.75*, 0.9*, 0.9*, 0.9*, 0.95*, 1.0*, 1.05*, 1.5*, 0.9*
			double widths[] = { 1.75, 0.9, 0.9, 0.9, 0.95, 1.0, 1.05, 1.5, 0.9 };
			for (int c = 0; c < 9; ++c)
			{
				auto cd = Microsoft::UI::Xaml::Controls::ColumnDefinition();
				cd.Width(Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(widths[c], Microsoft::UI::Xaml::GridUnitType::Star));
				rowGrid.ColumnDefinitions().Append(cd);
			}

			// No (Jack 번호: "Jack N/PLC M")
			auto noTb = Microsoft::UI::Xaml::Controls::TextBlock();
			std::wstring noText = std::to_wstring(jackNo) + L" / PLC " + std::to_wstring(plcIndex + 1);
			::DHLS_StrandJack::Services::Localization::RegisterFormattedText(noTb, L"잭 ", noText);
			noTb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Center);
			noTb.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
			rowGrid.SetColumn(noTb, 0);
			rowGrid.Children().Append(noTb);

			// X(mm) — NumberBox
			auto xNb = Microsoft::UI::Xaml::Controls::NumberBox();
			xNb.NumberFormatter(displacementFormatter);
			xNb.SmallChange(displacementStep);
			xNb.Value(xMm);
			xNb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			xNb.MinHeight(28);
			xNb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(2));
			rowGrid.SetColumn(xNb, 1);
			rowGrid.Children().Append(xNb);

			// Y(mm) — NumberBox
			auto yNb = Microsoft::UI::Xaml::Controls::NumberBox();
			yNb.NumberFormatter(displacementFormatter);
			yNb.SmallChange(displacementStep);
			yNb.Value(yMm);
			yNb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			yNb.MinHeight(28);
			yNb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(2));
			rowGrid.SetColumn(yNb, 2);
			rowGrid.Children().Append(yNb);

			// Jack (Sensor 번호)
			auto jackTb = Microsoft::UI::Xaml::Controls::TextBlock();
			jackTb.Text(L"PH " + std::to_wstring(details[i].JackNumber));
			jackTb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Center);
			jackTb.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
			rowGrid.SetColumn(jackTb, 3);
			rowGrid.Children().Append(jackTb);

			// Target — NumberBox
			auto targetNb = Microsoft::UI::Xaml::Controls::NumberBox();
			targetNb.NumberFormatter(displacementFormatter);
			targetNb.SmallChange(displacementStep);
			targetNb.Value(target);
			targetNb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			targetNb.MinHeight(28);
			targetNb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(2));
			rowGrid.SetColumn(targetNb, 4);
			rowGrid.Children().Append(targetNb);

			// 작업중 (Working Weight)
			auto workTb = Microsoft::UI::Xaml::Controls::TextBlock();
			workTb.Text(std::to_wstring(static_cast<int>(workingWeight)));
			workTb.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0x1D, 0x4E, 0xD8)));
			workTb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Center);
			workTb.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
			rowGrid.SetColumn(workTb, 5);
			rowGrid.Children().Append(workTb);

            // Editable maximum in the selected display unit; canonical storage remains ton.
            auto maxTb = Microsoft::UI::Xaml::Controls::NumberBox();
            Windows::Globalization::NumberFormatting::DecimalFormatter loadFormatter;
            loadFormatter.IntegerDigits(1);loadFormatter.FractionDigits(1);loadFormatter.IsGrouped(false);
            Windows::Globalization::NumberFormatting::IncrementNumberRounder loadRounder;
            loadRounder.Increment(0.1);
            loadRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
            loadFormatter.NumberRounder(loadRounder);
            maxTb.NumberFormatter(loadFormatter);
            maxTb.Value(::DHLS_StrandJack::DisplayUnits::Weight(maxWeight));
            maxTb.Minimum(0);
            maxTb.SpinButtonPlacementMode(Microsoft::UI::Xaml::Controls::NumberBoxSpinButtonPlacementMode::Hidden);
            maxTb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            maxTb.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
            maxTb.MinWidth(0);maxTb.MinHeight(30);maxTb.FontSize(14);
            maxTb.Padding(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(6,2,6,2));
            maxTb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(2));
            maxTb.Name(L"MaximumLoadInput"+std::to_wstring(i+1));
            maxTb.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(MakeColor(0xFF,0xB9,0x1C,0x1C)));
            rowGrid.SetColumn(maxTb,6);
            rowGrid.Children().Append(maxTb);

			// 잭선정 (Jack Selection ComboBox)
			auto jackSelCb = Microsoft::UI::Xaml::Controls::ComboBox();
			jackSelCb.MinHeight(24);
			jackSelCb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			jackSelCb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 4, 0));
			jackSelCb.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0xFE, 0xFC, 0xE8)));
			// 기본 잭 스펙 목록
			const auto& defSpecs = config.JackSettings.Specs;
			int selectedSpec = -1;
			double savedCapacity = -1;
			try { savedCapacity = std::stod(details[i].JackSelection); }
			catch (std::exception const&) { /* An unset selection defaults to the first type. */ }
			for (const auto& sp : defSpecs)
			{
				const auto cap = ::DHLS_StrandJack::Models::JackTypeLabel(sp);
				if (cap == details[i].JackSelection || std::abs(savedCapacity - sp.Capacity_ton) < 0.000001)
					selectedSpec = static_cast<int>(jackSelCb.Items().Size());
				jackSelCb.Items().Append(box_value(::DHLS_StrandJack::DisplayUnits::JackLabel(sp)));
			}
			if (selectedSpec < 0 && !defSpecs.empty()) selectedSpec = 0;
			jackSelCb.SelectedIndex(selectedSpec);
			// Initialize and update the same row without navigation, saving, or rebuilding the table.
			RefreshJackWeights(i, selectedSpec, workTb, maxTb);
			jackSelCb.SelectionChanged([weak = get_weak(), i](
				Windows::Foundation::IInspectable const& sender,
				Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
				{
					auto self = weak.get();
					if (!self || !self->m_isLoaded) return;
					auto items = self->JackTableListView().Items();
					if (i < 0 || static_cast<uint32_t>(i) >= items.Size()) return;
					if (auto row = items.GetAt(i).try_as<Microsoft::UI::Xaml::Controls::Grid>())
						RefreshJackRow(i, row, sender.as<Microsoft::UI::Xaml::Controls::ComboBox>());
                    self->RefreshScaleWarning();
				});
            maxTb.ValueChanged([weak=get_weak(),i](auto const& sender,auto const&){
                auto self=weak.get();if(!self || !self->m_isLoaded)return;
                auto items=self->JackTableListView().Items();
                if(i<0 || static_cast<uint32_t>(i)>=items.Size())return;
                auto row=items.GetAt(i).template try_as<Microsoft::UI::Xaml::Controls::Grid>();
                if(!row || Microsoft::UI::Xaml::Controls::Grid::GetColumn(sender)!=6)return;
                bool current=false;for(auto const& child:row.Children())if(child==sender)current=true;
                if(!current)return;
                auto& config=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig();
                if(i>=static_cast<int>(config.JackSetupDetails.size()))return;
                auto& detail=config.JackSetupDetails[i];
                const double ton=::DHLS_StrandJack::DisplayUnits::Ton(sender.Value());
                if(!std::isfinite(ton)||ton<=0){
                    const double previous=::DHLS_StrandJack::DisplayUnits::Weight(detail.MaxWeight_ton);
                    if(std::isfinite(previous)&&previous>0)sender.Value(previous);
                    return;
                }
                detail.MaxWeight_ton=ton;
                RefreshJackRow(i,row);
            });
			rowGrid.SetColumn(jackSelCb, 7);
			rowGrid.Children().Append(jackSelCb);

			// Port ComboBox
			auto portCb = Microsoft::UI::Xaml::Controls::ComboBox();
			portCb.MinHeight(24);
			portCb.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			portCb.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(4, 0, 4, 0));
			portCb.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(
				MakeColor(0xFF, 0xFE, 0xFC, 0xE8)));
			for (int p = 1; p <= sensorCount; ++p)
			{
				auto item = Microsoft::UI::Xaml::Controls::ComboBoxItem();
				item.Content(box_value(std::to_wstring(p)));
				portCb.Items().Append(item);
			}
			portCb.SelectedIndex(port - 1);
			portCb.SelectionChanged([weak = get_weak(), i](auto const& sender, auto const&)
				{
					if (!weak.get()) return;
					auto& rows = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSetupDetails;
					int selected = sender.template as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex();
                    if(i<static_cast<int>(rows.size()) && selected>=0)
                    {
                        if(rows[i].Port!=selected+1) ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings.InvalidateEquipmentVerification();
                        rows[i].Port=selected+1;
                    }
				});
			auto saveNumber = [weak = get_weak(), i](double value, int column)
				{
					if (!weak.get() || !std::isfinite(value)) return;
					auto& rows = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSetupDetails;
					if (i >= static_cast<int>(rows.size())) return;
                    if((column==1 && rows[i].X_mm!=value) || (column==2 && rows[i].Y_mm!=value))
                        ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().PLCSettings.CoordinatesVerified=false;
					if (column == 1) rows[i].X_mm = value;
					else if (column == 2) rows[i].Y_mm = value;
					else rows[i].Target = value;
				};
			xNb.ValueChanged([saveNumber](auto const& sender, auto const&) { saveNumber(sender.Value(), 1); });
			yNb.ValueChanged([saveNumber](auto const& sender, auto const&) { saveNumber(sender.Value(), 2); });
			targetNb.ValueChanged([saveNumber](auto const& sender, auto const&) { saveNumber(sender.Value(), 4); });
			rowGrid.SetColumn(portCb, 8);
			rowGrid.Children().Append(portCb);

			listView.Items().Append(rowGrid);
		}
        RefreshScaleWarning();
	}
}
