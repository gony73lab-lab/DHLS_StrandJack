#include "pch.h"
#include <cmath>
#include "WarningSound.h"
#include "DisplayUnits.h"
#include "Localization.h"
#include "JackSettingsView.xaml.h"
#include "JackTypeLabel.h"
#if __has_include("JackSettingsView.g.cpp")
#include "JackSettingsView.g.cpp"
#endif
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Globalization.NumberFormatting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
// WinUI's ConvertValue ABI uses the shared Windows.UI.Xaml.Interop.TypeName metadata type.
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::DHLS_StrandJack::implementation
{
  JackSettingsView::JackSettingsView()
  {
    InitializeComponent();
    PressureScaleUnit().Text(::DHLS_StrandJack::DisplayUnits::PressureLabel());
    // Round only the displayed text; keep the model's original precision.
    Windows::Globalization::NumberFormatting::IncrementNumberRounder rounder;
    rounder.Increment(1);
    rounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);
    Windows::Globalization::NumberFormatting::DecimalFormatter integerFormatter;
    integerFormatter.IntegerDigits(1);
    integerFormatter.FractionDigits(0);
    integerFormatter.IsGrouped(false);
    integerFormatter.NumberRounder(rounder);
    for (auto const& number : { TxtWedgeOpen(), TxtWedgeClose(), TxtLoadTransfer(), TxtSafeRange(),
        TxtCapacity(), TxtPressureRange(), TxtDisplacementRange(), TxtD1(), TxtD2(), TxtD3(), TxtJackStroke() })
    {
      number.NumberFormatter(integerFormatter);
    }
    auto const& specs = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSettings.Specs;
    Windows::Globalization::NumberFormatting::DecimalFormatter pressureFormat;
    pressureFormat.IntegerDigits(1);pressureFormat.FractionDigits(::DHLS_StrandJack::DisplayUnits::PressureDigits());pressureFormat.IsGrouped(false);
    Windows::Globalization::NumberFormatting::IncrementNumberRounder pressureRounder;pressureRounder.Increment(std::pow(10.0,-::DHLS_StrandJack::DisplayUnits::PressureDigits()));pressureRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);pressureFormat.NumberRounder(pressureRounder);
    Windows::Globalization::NumberFormatting::DecimalFormatter displacementFormat;displacementFormat.IntegerDigits(1);displacementFormat.FractionDigits(::DHLS_StrandJack::DisplayUnits::DisplacementDigits());displacementFormat.IsGrouped(false);
    Windows::Globalization::NumberFormatting::IncrementNumberRounder displacementRounder;displacementRounder.Increment(std::pow(10.0,-::DHLS_StrandJack::DisplayUnits::DisplacementDigits()));displacementRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);displacementFormat.NumberRounder(displacementRounder);
    for(auto box:{TxtDisplacementRange(),TxtD1(),TxtD2(),TxtD3(),TxtJackStroke(),TxtLoadTransfer(),TxtSafeRange(),TxtMaxLoadOperatingRange()})box.NumberFormatter(displacementFormat);
    TxtPressureRange().NumberFormatter(pressureFormat);TxtWorkingPressure().NumberFormatter(pressureFormat);
    CmbJackType().Items().Clear();
    for (auto const& spec : specs)
    {
      CmbJackType().Items().Append(box_value(spec.TypeName.empty()?::DHLS_StrandJack::DisplayUnits::JackLabel(spec):spec.TypeName));
    }
    if (!specs.empty()) CmbJackType().SelectedIndex(0);
    m_specSelectionChanged = CmbJackType().SelectionChanged(winrt::auto_revoke,
      [weak = get_weak()](auto const&, auto const&)
      {
        if (auto self = weak.get()) self->UpdateSelectedSpec();
      });
    UpdateSelectedSpec();
    auto refresh = [weak=get_weak()](auto const&, auto const&) { if(auto self=weak.get()) self->RefreshCalculatedValues(); };
    TxtD1().ValueChanged(refresh); TxtD2().ValueChanged(refresh); TxtD3().ValueChanged(refresh);
    TxtCapacity().ValueChanged(refresh);
    TxtJackStroke().ValueChanged(refresh); TxtSafeRange().ValueChanged(refresh); TxtLoadTransfer().ValueChanged(refresh);
    TxtMaxLoadOperatingRange().ValueChanged(refresh);
    Loaded([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get())
        self->m_audit=::DHLS_StrandJack::Services::SettingsAudit::Attach(*self,"JackSettings");});
  }

  void JackSettingsView::OnExitClick(Windows::Foundation::IInspectable const&,RoutedEventArgs const&){ExitWithConfirmationAsync();}
  void JackSettingsView::OnCaptureClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
  {
    CaptureAndReportAsync();
  }

  winrt::fire_and_forget JackSettingsView::CaptureAndReportAsync()
  {
    auto lifetime = get_strong();
    if (m_captureInProgress || !XamlRoot()) co_return;
    m_captureInProgress = true;
    winrt::hstring failure;
    try { co_await CaptureViewAsync(); }
    catch (winrt::hresult_error const& error) { failure = error.message(); }
    catch (std::exception const& error) { failure = winrt::to_hstring(error.what()); }
    catch (...) { failure = L"Capture failed."; }
    m_captureInProgress = false;
    if (!failure.empty())
    {
      ::OutputDebugStringW((std::wstring{L"JackSettings capture failed: "} + failure.c_str() + L"\n").c_str());
      if (auto root = XamlRoot())
      {
        ContentDialog dialog;
        dialog.XamlRoot(root);
        dialog.Title(box_value(::DHLS_StrandJack::Services::Localization::Text(L"오류")));
        dialog.Content(box_value(::DHLS_StrandJack::Services::Localization::Text(L"캡처 저장에 실패했습니다.") + L"\n" + failure));
        dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
        try { co_await dialog.ShowAsync(); }
        catch (...) { ::OutputDebugStringW(L"Unable to display JackSettings capture error.\n"); }
      }
    }
  }

  Windows::Foundation::IAsyncAction JackSettingsView::CaptureViewAsync()
  {
    auto lifetime = get_strong();
    if (!XamlRoot()) throw winrt::hresult_canceled();
    Media::Imaging::RenderTargetBitmap bitmap;
    co_await bitmap.RenderAsync(*this);
    if (!XamlRoot()) throw winrt::hresult_canceled();
    const auto width = static_cast<uint32_t>(bitmap.PixelWidth());
    const auto height = static_cast<uint32_t>(bitmap.PixelHeight());
    if (!width || !height) throw winrt::hresult_error(E_FAIL, L"Capture surface is empty.");
    auto buffer = co_await bitmap.GetPixelsAsync();
    if (!XamlRoot()) throw winrt::hresult_canceled();
    if (uint64_t{width} * height * 4 != buffer.Length())
      throw winrt::hresult_error(E_FAIL, L"Capture pixel buffer has an unexpected size.");
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
    name << L"JackSettings_" << std::put_time(&local, L"%H%M%S") << L".png";
    const auto directory = captureRoot / date.str();
    std::filesystem::create_directories(directory);
    auto folder = co_await Windows::Storage::StorageFolder::GetFolderFromPathAsync(directory.wstring());
    auto file = co_await folder.CreateFileAsync(name.str(), Windows::Storage::CreationCollisionOption::GenerateUniqueName);
    const std::filesystem::path filePath{file.Path().c_str()};
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
  winrt::fire_and_forget JackSettingsView::ExitWithConfirmationAsync(){
    auto lifetime=get_strong();if(m_exitBusy||m_editBusy)co_return;m_exitBusy=true;
    try {
      if(m_audit&&m_audit->Dirty()){
        if(!co_await ::DHLS_StrandJack::Services::ConfirmDiscardAsync(XamlRoot(),::DHLS_StrandJack::Services::Localization::IsEnglish())){m_exitBusy=false;co_return;}
        if(!IsLoaded()){m_exitBusy=false;co_return;}
        ::DHLS_StrandJack::Services::AuditLog::Write("DISCARD","JackSettings");
      }
      ::DHLS_StrandJack::Services::AuditLog::Write("VIEW_CLOSE","JackSettings");
      m_audit.reset();if(m_hostWindow){auto host=m_hostWindow;m_hostWindow=nullptr;host.Close();}
    }catch(...){::DHLS_StrandJack::Services::AuditLog::Write("ERROR","JackSettings exit confirmation failed");}
    m_exitBusy=false;
  }

  void JackSettingsView::ShowMotionArrowChanged(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
  {
    RefreshArrowPreview();
  }

  void JackSettingsView::RefreshArrowPreview()
  {
    // Checked can fire while InitializeComponent is still creating later named elements.
    if (!MotionArrowPreview() || !ShowMotionArrowCheck()) return;
    auto checked = ShowMotionArrowCheck().IsChecked();
    MotionArrowPreview().Visibility(checked && checked.Value() ? Visibility::Visible : Visibility::Collapsed);
  }

  void JackSettingsView::RefreshCalculatedValues()
  {
    if (m_loadingSpec) return;
    const double stroke = TxtJackStroke().Value();
    const double safeRange = TxtSafeRange().Value();
    const double transferRange = TxtLoadTransfer().Value();
    const double requestedRange = TxtMaxLoadOperatingRange().Value();
    const bool invalidRange = std::isfinite(stroke) && std::isfinite(safeRange) &&
      std::isfinite(transferRange) && std::isfinite(requestedRange) &&
      requestedRange > stroke - 2.0 * safeRange - transferRange + 0.000001;
    MaxOperatingRangeWarning().Visibility(invalidRange ? Visibility::Visible : Visibility::Collapsed);
    TxtMaxLoadOperatingRange().BorderBrush(Media::SolidColorBrush(invalidRange ? Microsoft::UI::Colors::Red() : Microsoft::UI::Colors::LightGray()));
    if (invalidRange && !m_rangeAlarmActive)
      ::DHLS_StrandJack::Services::PlayWarningSound(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().SoundSettings,false);
    m_rangeAlarmActive = invalidRange;
    if (!TxtRiseArea() || !TxtFallArea() || !TxtMaxPressure()) return;
    const double d1 = TxtD1().Value(), d2 = TxtD2().Value(), d3 = TxtD3().Value();
    const double capacity = TxtCapacity().Value();
    if (!std::isfinite(d1) || !std::isfinite(d2) || !std::isfinite(d3) ||
        d1 <= 0 || d2 <= 0 || d3 <= d1 || d3 <= d2 ||
        !std::isfinite(capacity) || capacity <= 0)
    {
      TxtRiseArea().Text(L""); TxtFallArea().Text(L""); TxtMaxPressure().Text(L"");
      return;
    }
    // The reference StrandJack settings screen uses six-decimal pi for displayed areas.
    constexpr double displayedPi = 3.14159;
    const double rise = displayedPi * (d3*d3-d2*d2)/4.0;
    const double fall = displayedPi * (d3*d3-d1*d1)/4.0;
    wchar_t value[40]{};
    swprintf_s(value, L"%.1f", rise); TxtRiseArea().Text(value);
    swprintf_s(value, L"%.1f", fall); TxtFallArea().Text(value);
    swprintf_s(value, L"%.1f", capacity * 98066.5 / rise); TxtMaxPressure().Text(value);
  }

  void JackSettingsView::UpdateSelectedSpec()
  {
    auto const& specs = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSettings.Specs;
    const int index = CmbJackType().SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= specs.size())
    {
      ShowMotionArrowCheck().IsChecked(false);
      RefreshArrowPreview();
      return;
    }
    m_selectedSpecIndex = index;
    auto const& spec = specs[index];
    auto const& sensor=::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().SensorSettings;
    auto scaleIndex=[](double value){return value==4000?0:value==16000?1:value==64000?2:-1;};
    TxtPressureScale().SelectedIndex(scaleIndex(spec.PressureSensor_ADScale>0?spec.PressureSensor_ADScale:sensor.PressureSensor_ADScale));
    TxtDisplacementScale().SelectedIndex(scaleIndex(spec.DisplacementSensor_ADScale>0?spec.DisplacementSensor_ADScale:sensor.DisplacementSensor_ADScale));
    m_loadingSpec = true;
    TxtPressureRange().Value(::DHLS_StrandJack::DisplayUnits::Pressure(spec.PressureSensorScale_bar));
    TxtDisplacementRange().Value(spec.DisplacementSensorScale_mm);
    TxtWedgeOpen().Value(spec.WedgeOpenConfirm_sec);
    TxtWedgeClose().Value(spec.WedgeCloseConfirm_sec);
    TxtLoadTransfer().Value(spec.LoadTransferRange_mm);
    TxtSafeRange().Value(spec.SafeRange_mm);
    TxtCapacity().Value(spec.Capacity_ton);
    TxtWorkingPressure().Value(::DHLS_StrandJack::DisplayUnits::Pressure(spec.WorkingPressure_bar));
    TxtD1().Value(spec.D1_Diameter_mm);
    TxtD2().Value(spec.D2_Diameter_mm);
    TxtD3().Value(spec.D3_Diameter_mm);
    TxtJackStroke().Value(spec.JackStroke_mm);
    TxtMaxLoadOperatingRange().Value(spec.MaxLoadOperatingRange_mm);
    CmbJackType().Text(spec.TypeName.empty()?::DHLS_StrandJack::DisplayUnits::JackLabel(spec):spec.TypeName);
    m_loadingSpec = false;
    RefreshCalculatedValues();
    ShowMotionArrowCheck().IsChecked(spec.ShowMotionArrow);
    RefreshArrowPreview();

    auto updateColor = [this](wchar_t const* resource, std::wstring const& value)
    {
      auto color = Microsoft::UI::Colors::Black();
      if (!value.empty())
      {
        try
        {
          color = unbox_value<winrt::Windows::UI::Color>(
            Microsoft::UI::Xaml::Markup::XamlBindingHelper::ConvertValue(
              winrt::xaml_typename<winrt::Windows::UI::Color>(), box_value(hstring{ value })));
        }
        catch (winrt::hresult_error const&) { /* Invalid stored color uses black. */ }
      }
      if (auto brush = Resources().TryLookup(box_value(hstring{ resource })).try_as<Media::SolidColorBrush>())
        brush.Color(color);
    };
    updateColor(L"CylinderColorBrush", spec.CylinderColor);
    updateColor(L"PistonColorBrush", spec.PistonColor);
    if(m_audit)m_audit->MarkSaved();
  }

  namespace
  {
    // Only copied native data crosses to the worker. The caller resumes on its UI apartment.
    Windows::Foundation::IAsyncOperation<bool> SaveJackSnapshotAsync(
        ::DHLS_StrandJack::Models::SystemConfig snapshot, std::wstring password)
    {
      co_await winrt::resume_background();
      std::filesystem::path temporary;
      try
      {
        auto directory = ::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory();
        std::filesystem::create_directories(directory);
        wchar_t path[MAX_PATH]{};
        if (!::GetTempFileNameW(directory.c_str(), L"jck", 0, path)) co_return false;
        temporary = path;
        bool saved = ::DHLS_StrandJack::Services::ConfigManager::SaveSnapshotToJson(temporary.wstring(), snapshot, password);
        auto destination = directory / (snapshot.ModelInfo.ModelName + L".json");
        if (saved) saved = !!::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        co_return saved;
      }
      catch (...)
      {
        std::error_code ec;
        if (!temporary.empty()) std::filesystem::remove(temporary, ec);
        co_return false;
      }
    }

    hstring JackText(wchar_t const* korean, wchar_t const* english)
    {
      return ::DHLS_StrandJack::Services::Localization::IsEnglish() ? hstring{english} : hstring{korean};
    }
  }

  void JackSettingsView::ReloadSpecs(int selected)
  {
    Windows::Globalization::NumberFormatting::DecimalFormatter pressureFormat;
    pressureFormat.IntegerDigits(1);pressureFormat.FractionDigits(::DHLS_StrandJack::DisplayUnits::PressureDigits());pressureFormat.IsGrouped(false);
    Windows::Globalization::NumberFormatting::IncrementNumberRounder pressureRounder;pressureRounder.Increment(std::pow(10.0,-::DHLS_StrandJack::DisplayUnits::PressureDigits()));pressureRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);pressureFormat.NumberRounder(pressureRounder);
    Windows::Globalization::NumberFormatting::DecimalFormatter displacementFormat;displacementFormat.IntegerDigits(1);displacementFormat.FractionDigits(::DHLS_StrandJack::DisplayUnits::DisplacementDigits());displacementFormat.IsGrouped(false);
    Windows::Globalization::NumberFormatting::IncrementNumberRounder displacementRounder;displacementRounder.Increment(std::pow(10.0,-::DHLS_StrandJack::DisplayUnits::DisplacementDigits()));displacementRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);displacementFormat.NumberRounder(displacementRounder);
    for(auto box:{TxtDisplacementRange(),TxtD1(),TxtD2(),TxtD3(),TxtJackStroke(),TxtMaxLoadOperatingRange()})box.NumberFormatter(displacementFormat);
    TxtPressureRange().NumberFormatter(pressureFormat);TxtWorkingPressure().NumberFormatter(pressureFormat);
    CmbJackType().Items().Clear();
    auto const& specs = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSettings.Specs;
    for (auto const& spec : specs) CmbJackType().Items().Append(box_value(spec.TypeName.empty()?::DHLS_StrandJack::DisplayUnits::JackLabel(spec):spec.TypeName));
    CmbJackType().SelectedIndex(specs.empty() ? -1 : (std::min)(selected, static_cast<int>(specs.size()) - 1));
    UpdateSelectedSpec();
  }

  ::DHLS_StrandJack::Models::JackSpec JackSettingsView::ReadSpec()
  {
    ::DHLS_StrandJack::Models::JackSpec spec;
    auto const& existing = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().JackSettings.Specs;
    if (m_selectedSpecIndex >= 0 && static_cast<size_t>(m_selectedSpecIndex) < existing.size())
      spec = existing[m_selectedSpecIndex];
    spec.Capacity_ton = TxtCapacity().Value();
    spec.TypeName = CmbJackType().Text().c_str();
    spec.WedgeOpenConfirm_sec = TxtWedgeOpen().Value();
    spec.WedgeCloseConfirm_sec = TxtWedgeClose().Value();
    spec.LoadTransferRange_mm = TxtLoadTransfer().Value();
    spec.SafeRange_mm = TxtSafeRange().Value();
    spec.PressureSensorScale_bar = ::DHLS_StrandJack::DisplayUnits::Bar(TxtPressureRange().Value());
    spec.DisplacementSensorScale_mm = TxtDisplacementRange().Value();
    spec.WorkingPressure_bar = ::DHLS_StrandJack::DisplayUnits::Bar(TxtWorkingPressure().Value());
    spec.D1_Diameter_mm = TxtD1().Value();
    spec.D2_Diameter_mm = TxtD2().Value();
    spec.D3_Diameter_mm = TxtD3().Value();
    spec.JackStroke_mm = TxtJackStroke().Value();
    spec.MaxLoadOperatingRange_mm = TxtMaxLoadOperatingRange().Value();
    auto showArrow = ShowMotionArrowCheck().IsChecked();
    spec.ShowMotionArrow = showArrow && showArrow.Value();
    for (double value : {spec.WedgeOpenConfirm_sec, spec.WedgeCloseConfirm_sec,
        spec.LoadTransferRange_mm, spec.SafeRange_mm, spec.Capacity_ton,
        spec.PressureSensorScale_bar, spec.DisplacementSensorScale_mm,
        spec.WorkingPressure_bar, spec.D1_Diameter_mm, spec.D2_Diameter_mm,
        spec.D3_Diameter_mm, spec.JackStroke_mm, spec.MaxLoadOperatingRange_mm})
      if (!std::isfinite(value) || value < 0) throw hresult_invalid_argument();
    if (spec.MaxLoadOperatingRange_mm >
        spec.JackStroke_mm - 2.0 * spec.SafeRange_mm - spec.LoadTransferRange_mm + 0.000001)
      throw std::range_error("Maximum load operating range exceeds available stroke");
    if (spec.TypeName.empty() || spec.Capacity_ton <= 0 ||
        (spec.D3_Diameter_mm > 0 &&
         ((spec.D1_Diameter_mm > 0 && spec.D3_Diameter_mm <= spec.D1_Diameter_mm) ||
          (spec.D2_Diameter_mm > 0 && spec.D3_Diameter_mm <= spec.D2_Diameter_mm))))
      throw hresult_invalid_argument();
    auto colorText = [this](wchar_t const* key)
    {
      auto color = Resources().Lookup(box_value(key)).as<Media::SolidColorBrush>().Color();
      wchar_t text[10]{};
      swprintf_s(text, L"#%02X%02X%02X%02X", color.A, color.R, color.G, color.B);
      return std::wstring{text};
    };
    spec.CylinderColor = colorText(L"CylinderColorBrush");
    return spec;
  }

  void JackSettingsView::OnAddClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { EditSpecAsync(0); }
  void JackSettingsView::OnDeleteClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { EditSpecAsync(1); }
  void JackSettingsView::OnSaveClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { EditSpecAsync(2); }
  void JackSettingsView::OnSaveAndExitClick(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
  {
    if (m_editBusy) return;
    m_closeAfterSave = true;
    EditSpecAsync(2);
  }

  winrt::fire_and_forget JackSettingsView::EditSpecAsync(int operation)
  {
    auto lifetime = get_strong();
    if (m_editBusy) co_return;
    m_editBusy = true;
    struct ResetBusy { bool& flag; ~ResetBusy() { flag = false; } } reset{m_editBusy};
    hstring error;
    bool completed = false;
    try
    {
      auto& manager = ::DHLS_StrandJack::Services::ConfigManager::Instance();
      auto const modelName = manager.GetConfig().ModelInfo.ModelName;
      auto const originalSpecs = manager.GetConfig().JackSettings.Specs;
      auto candidate = originalSpecs;
      int index = m_selectedSpecIndex;
      if (modelName.empty())
        error = JackText(L"모델을 먼저 선택해 주세요.", L"Select a model first.");
      else if (operation == 1 && (index < 0 || static_cast<size_t>(index) >= candidate.size()))
        error = JackText(L"잭 종류를 선택해 주세요.", L"Select a jack type.");
      else if (operation == 2 && (index < 0 || static_cast<size_t>(index) >= candidate.size()))
        error = JackText(L"잭 종류를 선택해 주세요.", L"Select a jack type.");
      else if (operation == 0)
      {
        // Capture inputs before the dialog suspends; no references into config survive the await.
        auto spec = ReadSpec();
        NumberBox capacity;
        capacity.Header(box_value(std::wstring(JackText(L"새 잭 용량", L"New jack capacity"))+L" ("+::DHLS_StrandJack::DisplayUnits::WeightLabel()+L")"));
        capacity.Minimum(0.001); capacity.Maximum(10000000); capacity.Value(::DHLS_StrandJack::DisplayUnits::Weight(1));
        Windows::Globalization::NumberFormatting::DecimalFormatter capacityFormat;capacityFormat.IntegerDigits(1);capacityFormat.FractionDigits(1);capacityFormat.IsGrouped(false);
        Windows::Globalization::NumberFormatting::IncrementNumberRounder capacityRounder;capacityRounder.Increment(0.1);capacityRounder.RoundingAlgorithm(Windows::Globalization::NumberFormatting::RoundingAlgorithm::RoundHalfAwayFromZero);capacityFormat.NumberRounder(capacityRounder);
        capacity.SmallChange(1); capacity.NumberFormatter(capacityFormat);
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(JackText(L"잭 종류 추가", L"Add jack type")));
        dialog.Content(capacity);
        dialog.PrimaryButtonText(JackText(L"추가", L"Add"));
        dialog.CloseButtonText(JackText(L"취소", L"Cancel"));
        dialog.PrimaryButtonStyle(Application::Current().Resources().Lookup(box_value(L"DialogPrimaryAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
        dialog.CloseButtonStyle(Application::Current().Resources().Lookup(box_value(L"DialogCancelIconStyle")).as<Microsoft::UI::Xaml::Style>());
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        spec.Capacity_ton = ::DHLS_StrandJack::DisplayUnits::Ton(capacity.Value());
        spec.TypeName = L"DSJ-" + std::to_wstring(static_cast<int>(std::round(spec.Capacity_ton)));
        if (!std::isfinite(spec.Capacity_ton) || spec.Capacity_ton <= 0)
          error = JackText(L"용량은 0보다 큰 값으로 입력해 주세요.", L"Enter a positive capacity.");
        else if (std::any_of(candidate.begin(), candidate.end(), [&spec](auto const& item) { return std::abs(item.Capacity_ton - spec.Capacity_ton) < 0.000001; }))
          error = JackText(L"같은 용량의 잭 종류가 이미 존재합니다.", L"A jack type with this capacity already exists.");
        else { candidate.push_back(spec); index = static_cast<int>(candidate.size()) - 1; }
      }
      else if (operation == 1)
      {
        auto const label = ::DHLS_StrandJack::Models::JackTypeLabel(candidate[index]);
        bool used = false;
        for (auto const& row : manager.GetConfig().JackSetupDetails)
        {
          if (row.JackSelection == label) used = true;
          try { if (std::abs(std::stod(row.JackSelection) - candidate[index].Capacity_ton) < 0.000001) used = true; } catch (...) {}
        }
        if (candidate.size() <= 1)
          error = JackText(L"마지막 잭 종류는 삭제할 수 없습니다.", L"The last jack type cannot be deleted.");
        else if (used)
          error = JackText(L"제어설정에서 사용 중인 잭 종류입니다. 잭선정을 변경한 후 삭제해 주세요.", L"This jack type is in use. Change the jack selections in Control Settings before deleting it.");
        else
        {
          ContentDialog dialog;
          dialog.XamlRoot(XamlRoot());
          dialog.Title(box_value(JackText(L"잭 종류 삭제", L"Delete jack type")));
          dialog.Content(box_value(hstring{label} + JackText(L" 잭 종류를 삭제하시겠습니까?", L": delete this jack type?")));
          dialog.PrimaryButtonText(JackText(L"삭제", L"Delete"));
          dialog.CloseButtonText(JackText(L"취소", L"Cancel"));
          dialog.PrimaryButtonStyle(Application::Current().Resources().Lookup(box_value(L"DialogPrimaryAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
          dialog.CloseButtonStyle(Application::Current().Resources().Lookup(box_value(L"DialogCancelIconStyle")).as<Microsoft::UI::Xaml::Style>());
          if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
          candidate.erase(candidate.begin() + index);
        }
      }
      else
      {
        auto edited = ReadSpec();
        if (std::any_of(candidate.begin(), candidate.end(), [&](auto const& item) {
              return &item != &candidate[index] && item.TypeName == edited.TypeName; }))
          error = JackText(L"같은 형식명이 이미 존재합니다.", L"A jack with this type name already exists.");
        else if (edited.TypeName != candidate[index].TypeName && !candidate[index].TypeName.empty())
        {
          if (std::any_of(candidate.begin(), candidate.end(), [&](auto const& item) {
                return std::abs(item.Capacity_ton - edited.Capacity_ton) < 0.000001; }))
            error = JackText(L"같은 최대하중의 잭이 이미 존재합니다.", L"A jack with this capacity already exists.");
          else { candidate.push_back(std::move(edited)); index = static_cast<int>(candidate.size()) - 1; }
        }
        else candidate[index] = std::move(edited);
      }

      if (error.empty())
      {
        // Recheck the model after the dialog suspension.
        if (manager.GetConfig().ModelInfo.ModelName != modelName)
          error = JackText(L"모델이 변경되었습니다. 다시 시도해 주세요.", L"The model changed. Please try again.");
        else
        {
          auto snapshot = manager.GetConfig();
          bool scalingChanged = snapshot.JackSettings.Specs.size() != candidate.size();
          bool areaChanged = scalingChanged;
          for (size_t i=0; i<candidate.size() && i<snapshot.JackSettings.Specs.size(); ++i)
          {
            auto const& before=snapshot.JackSettings.Specs[i];auto const& after=candidate[i];
            scalingChanged |= before.PressureSensor_ADScale!=after.PressureSensor_ADScale || before.DisplacementSensor_ADScale!=after.DisplacementSensor_ADScale || before.Capacity_ton!=after.Capacity_ton ||
              before.PressureSensorScale_bar!=after.PressureSensorScale_bar ||
              before.DisplacementSensorScale_mm!=after.DisplacementSensorScale_mm || before.JackStroke_mm!=after.JackStroke_mm;
            areaChanged |= before.Capacity_ton!=after.Capacity_ton || before.D1_Diameter_mm!=after.D1_Diameter_mm ||
              before.D2_Diameter_mm!=after.D2_Diameter_mm || before.D3_Diameter_mm!=after.D3_Diameter_mm;
          }
          if(scalingChanged) snapshot.PLCSettings.SensorScalingVerified=false;
          if(areaChanged) snapshot.PLCSettings.EffectiveAreaVerified=false;
          const auto previousSpecs=snapshot.JackSettings.Specs;
          snapshot.JackSettings.Specs = candidate;
          for (auto& row : snapshot.JackSetupDetails)
            for (auto const& spec : candidate)
              if (row.JackSelection == ::DHLS_StrandJack::Models::JackTypeLabel(spec))
              {
                auto previous=std::find_if(previousSpecs.begin(),previousSpecs.end(),[&](auto const& old){return ::DHLS_StrandJack::Models::JackTypeLabel(old)==row.JackSelection;});
                // A stroke/color/ADC edit must not discard a user-entered maximum load.
                if(previous!=previousSpecs.end()&&previous->D3_Diameter_mm==spec.D3_Diameter_mm&&previous->WorkingPressure_bar==spec.WorkingPressure_bar&&
                   std::isfinite(row.MaxWeight_ton)&&row.MaxWeight_ton>0)continue;
                row.MaxWeight_ton = ::DHLS_StrandJack::Models::Util::CalcAdvanceForce_ton(spec.D3_Diameter_mm, spec.WorkingPressure_bar);
                row.WorkingWeight_ton = row.MaxWeight_ton * snapshot.ControlSettings.MaxWeightUsageRange_Percent / 100.0;
              }
          auto saved = co_await SaveJackSnapshotAsync(snapshot, manager.GetSettingsPassword());
          if (!saved)
            error = JackText(L"모델 파일을 저장하지 못했습니다. 변경을 취소했습니다.", L"Could not save the model file. Changes were rolled back.");
          else if (manager.GetConfig().ModelInfo.ModelName == modelName)
          {
            manager.GetConfig().PLCSettings.SensorScalingVerified=snapshot.PLCSettings.SensorScalingVerified;
            manager.GetConfig().PLCSettings.EffectiveAreaVerified=snapshot.PLCSettings.EffectiveAreaVerified;
            manager.GetConfig().JackSettings.Specs = std::move(snapshot.JackSettings.Specs);
            manager.GetConfig().JackSetupDetails = std::move(snapshot.JackSetupDetails);
            if (XamlRoot()) ReloadSpecs(index);
            completed = true;
          }
        }
      }
    }
    catch (std::range_error const&) { error = JackText(L"최대 하중 가동 범위가 실린더 변위 - 안전 범위×2 - 하중전이 범위를 초과합니다.", L"Maximum load operating range exceeds stroke minus twice the safety range and load transfer range."); }
    catch (hresult_invalid_argument const&) { error = JackText(L"설정값은 0 이상의 유효한 숫자로 입력해 주세요.", L"Enter valid non-negative settings values."); }
    catch (...) { error = JackText(L"잭 설정을 변경하지 못했습니다.", L"Could not change the jack settings."); }
    if (!error.empty() && XamlRoot())
    {
      try
      {
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        StackPanel content; content.Orientation(Orientation::Horizontal); content.Spacing(12);
        FontIcon icon; icon.Glyph(L"\xE7BA"); icon.FontSize(24);
        TextBlock text; text.Text(error); text.TextWrapping(TextWrapping::Wrap); text.MaxWidth(360);
        content.Children().Append(icon); content.Children().Append(text);
        ::DHLS_StrandJack::Services::PlayWarningSound(::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().SoundSettings,false);
        dialog.Title(box_value(JackText(L"경고", L"Warning"))); dialog.Content(content);
        dialog.CloseButtonText(JackText(L"확인", L"OK"));
        dialog.CloseButtonStyle(Application::Current().Resources().Lookup(box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
        co_await dialog.ShowAsync();
      }
      catch (...) { /* Host may have closed while the operation was pending. */ }
    }
    if (m_closeAfterSave)
    {
      m_closeAfterSave = false;
      if (completed && m_hostWindow)
      {
        m_audit.reset();
        auto host = m_hostWindow;
        m_hostWindow = nullptr;
        host.Close();
      }
    }
  }

  // IDL 기본 생성 속성 구현부 추가
  int32_t JackSettingsView::MyProperty()
  {
    throw hresult_not_implemented();
  }

  void JackSettingsView::MyProperty(int32_t /* value */)
  {
    throw hresult_not_implemented();
  }
}
