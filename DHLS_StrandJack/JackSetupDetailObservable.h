// JackSetupDetailObservable.h: ListView 바인딩용 Observable 모델
//
// ----------------------------------------------------------------
// MFC → WinUI 3 Migration: Observable Jack Setup Detail
// ----------------------------------------------------------------
// [MFC Original]
//   JackSetupDetail struct in ConfigManager.h: plain data struct
//   Updated via m_listJackDetails.SetItemText() in ControlSettingsDlg
//
// [WinUI 3 Migration]
//   Implements INotifyPropertyChanged so ListView can bind TwoWay
//   to individual cell values via {x:Bind} in a DataTemplate
// ----------------------------------------------------------------
#pragma once
#include "DisplayUnits.h"

#include "ViewModelBase.h"
#include <string>

namespace DHLS_StrandJack::Models
{
    // Observable wrapper around JackSetupDetail for UI binding
    class JackSetupDetailObservable : public DHLS_StrandJack::Common::ViewModelBase
    {
    private:
        int m_no = 0;
        double m_x = 0;
        double m_y = 0;
        std::wstring m_jackSelection;
        double m_target = 0;
        double m_workingWeight = 0;
        double m_maxWeight = 0;
        int m_port = 1;

    public:
        JackSetupDetailObservable() = default;
        JackSetupDetailObservable(int no, double x, double y,
            const std::wstring& jackSel, double target,
            double workW, double maxW, int port)
            : m_no(no), m_x(x), m_y(y), m_jackSelection(jackSel),
              m_target(target), m_workingWeight(workW), m_maxWeight(maxW), m_port(port) {}

        // Properties for {x:Bind}
        PROP_GET_SET(int, No, m_no)
        PROP_GET_SET(double, X, m_x)
        PROP_GET_SET(double, Y, m_y)
        PROP_GET_SET(std::wstring, JackSelection, m_jackSelection)
        PROP_GET_SET(double, Target, m_target)
        PROP_GET_SET(double, WorkingWeight, m_workingWeight)
        PROP_GET_SET(double, MaxWeight, m_maxWeight)
        PROP_GET_SET(int, Port, m_port)

        // Formatted display strings for XAML TextBlock binding
        std::wstring XDisplay() const
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(::DHLS_StrandJack::DisplayUnits::DisplacementDigits()) << m_x;
            return ss.str();
        }
        std::wstring YDisplay() const
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(::DHLS_StrandJack::DisplayUnits::DisplacementDigits()) << m_y;
            return ss.str();
        }
        std::wstring WorkingWeightDisplay() const
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(1) << ::DHLS_StrandJack::DisplayUnits::Weight(m_workingWeight) << L" " << ::DHLS_StrandJack::DisplayUnits::WeightLabel();
            return ss.str();
        }
        std::wstring MaxWeightDisplay() const
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(1) << ::DHLS_StrandJack::DisplayUnits::Weight(m_maxWeight) << L" " << ::DHLS_StrandJack::DisplayUnits::WeightLabel();
            return ss.str();
        }
        std::wstring TargetDisplay() const
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(::DHLS_StrandJack::DisplayUnits::DisplacementDigits()) << m_target;
            return ss.str();
        }
    };
}
