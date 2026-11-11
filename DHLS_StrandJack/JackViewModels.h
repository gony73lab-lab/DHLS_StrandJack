// JackViewModels.h — Observable Jack models for XAML binding
// Extracted from ControlSettingsViewModel.h for View integration
#pragma once

#include "ViewModelBase.h"
#include "RelayCommand.h"
#include "ConfigManager.h"
#include "DisplayUnits.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <cmath>
#include "ViewModels.JackSetupDetailViewModel.g.h"
#include "ViewModels.JackSpecViewModel.g.h"

namespace winrt::DHLS_StrandJack::ViewModels::implementation
{
    using namespace ::DHLS_StrandJack::Common;
    using namespace ::DHLS_StrandJack::Models;

    // ── Observable Jack Setup Detail (for ListView row binding) ──
    struct JackSetupDetailViewModel : JackSetupDetailViewModelT<JackSetupDetailViewModel>
    {
        JackSetupDetailViewModel() = default;

        // INPC
        winrt::event_token PropertyChanged(winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        { return m_propertyChanged.add(handler); }
        void PropertyChanged(winrt::event_token const& token) noexcept
        { m_propertyChanged.remove(token); }

        // Properties
        int No() const { return m_no; }
        void No(int value) { m_no = value; RaisePropertyChanged(L"No"); }

        double X_mm() const { return m_x; }
        void X_mm(double value) { m_x = value; RaisePropertyChanged(L"X_mm"); }

        double Y_mm() const { return m_y; }
        void Y_mm(double value) { m_y = value; RaisePropertyChanged(L"Y_mm"); }

        int JackNumber() const { return m_jackNumber; }
        void JackNumber(int value) { m_jackNumber = value; RaisePropertyChanged(L"JackNumber"); }

        double Target() const { return m_target; }
        void Target(double value) { m_target = value; RaisePropertyChanged(L"Target"); }

        double WorkingWeight_ton() const { return m_workingWeight; }
        void WorkingWeight_ton(double value) { m_workingWeight = value; RaisePropertyChanged(L"WorkingWeight_ton"); }

        double MaxWeight_ton() const { return m_maxWeight; }
        void MaxWeight_ton(double value) { m_maxWeight = value; RaisePropertyChanged(L"MaxWeight_ton"); }

        winrt::hstring JackSelection() const { return m_jackSelection; }
        void JackSelection(winrt::hstring const& value) { m_jackSelection = value; RaisePropertyChanged(L"JackSelection"); }

        int Port() const { return m_port; }
        void Port(int value) { m_port = value; RaisePropertyChanged(L"Port"); }

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> JackSpecs() const { return m_jackSpecs; }
        void JackSpecs(winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& specs)
        {
            m_jackSpecs = specs;
            RaisePropertyChanged(L"JackSpecs");
        }

    private:
        int m_no = 0, m_jackNumber = 0, m_port = 0;
        double m_x = 0, m_y = 0, m_target = 0, m_workingWeight = 0, m_maxWeight = 0;
        winrt::hstring m_jackSelection{ L"DSJ-70" };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_jackSpecs{ nullptr };
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

        void RaisePropertyChanged(const wchar_t* name)
        {
            m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ name });
        }
    };

    // ── Observable Jack Spec (for ComboBox binding) ──
    struct JackSpecViewModel : JackSpecViewModelT<JackSpecViewModel>
    {
        JackSpecViewModel() = default;

        winrt::event_token PropertyChanged(winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        { return m_propertyChanged.add(handler); }
        void PropertyChanged(winrt::event_token const& token) noexcept
        { m_propertyChanged.remove(token); }

        double Capacity_ton() const { return m_capacity; }
        void Capacity_ton(double value) { m_capacity = value; RaisePropertyChanged(L"Capacity_ton"); RaisePropertyChanged(L"DisplayName"); }

        double WorkingPressure_bar() const { return m_workingPressure; }
        void WorkingPressure_bar(double value) { m_workingPressure = value; RaisePropertyChanged(L"WorkingPressure_bar"); }

        double D3_Diameter_mm() const { return m_d3; }
        void D3_Diameter_mm(double value) { m_d3 = value; RaisePropertyChanged(L"D3_Diameter_mm"); }

        double PressureSensorScale_bar() const { return m_pScale; }
        void PressureSensorScale_bar(double value) { m_pScale = value; RaisePropertyChanged(L"PressureSensorScale_bar"); }

        double DisplacementSensorScale_mm() const { return m_dScale; }
        void DisplacementSensorScale_mm(double value) { m_dScale = value; RaisePropertyChanged(L"DisplacementSensorScale_mm"); }

        double D1_Diameter_mm() const { return m_d1; }
        void D1_Diameter_mm(double value) { m_d1 = value; RaisePropertyChanged(L"D1_Diameter_mm"); }

        double D2_Diameter_mm() const { return m_d2; }
        void D2_Diameter_mm(double value) { m_d2 = value; RaisePropertyChanged(L"D2_Diameter_mm"); }

        double JackStroke_mm() const { return m_stroke; }
        void JackStroke_mm(double value) { m_stroke = value; RaisePropertyChanged(L"JackStroke_mm"); }

        winrt::hstring CylinderColor() const { return m_cylColor; }
        void CylinderColor(winrt::hstring const& value) { m_cylColor = value; RaisePropertyChanged(L"CylinderColor"); }

        winrt::hstring PistonColor() const { return m_pisColor; }
        void PistonColor(winrt::hstring const& value) { m_pisColor = value; RaisePropertyChanged(L"PistonColor"); }

        winrt::hstring DisplayName() const { return winrt::hstring{ ::DHLS_StrandJack::Common::FormatDouble(::DHLS_StrandJack::DisplayUnits::Weight(m_capacity), 1) + L" " + ::DHLS_StrandJack::DisplayUnits::WeightLabel() }; }

    private:
        double m_capacity = 0, m_workingPressure = 700, m_d3 = 0;
        double m_pScale = 0, m_dScale = 0, m_d1 = 0, m_d2 = 0, m_stroke = 0;
        winrt::hstring m_cylColor, m_pisColor;
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

        void RaisePropertyChanged(const wchar_t* name)
        {
            m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ name });
        }
    };
} // namespace winrt::DHLS_StrandJack::ViewModels::implementation

namespace winrt::DHLS_StrandJack::ViewModels::factory_implementation
{
    struct JackSetupDetailViewModel : JackSetupDetailViewModelT<JackSetupDetailViewModel, implementation::JackSetupDetailViewModel> {};
    struct JackSpecViewModel : JackSpecViewModelT<JackSpecViewModel, implementation::JackSpecViewModel> {};
}
