// =========================================================
//  JackArrangementView.xaml.h — 잭 배치 UserControl 헤더
//  HTML: 잭 배치.html
//  역할: 4x8 그리드(32셀) + 4개 템플릿 선택 + OK/Cancel
//  구조: UserControl 기반 (Page 아님)
//
//  DHLS_StrandJack 프로젝트: Microsoft.UI.Xaml (WinUI 3)
//  자체 완결형 — JackCellData, JackColorToBrushConverter 구현체 포함.
// =========================================================
#pragma once
#include <functional>
#include "Localization.h"

#include "JackArrangementView.g.h"
#include "JackCellData.g.h"

namespace winrt::DHLS_StrandJack::implementation
{
    // =====================================================
    //  JackCellData — 잭 그리드 셀 데이터 구현체
    //  HTML: cells.push({ id, active, color, label })
    // =====================================================
    struct JackCellData : JackCellDataT<JackCellData>
    {
        JackCellData() = default;
        JackCellData(hstring id, bool active, DHLS_StrandJack::JackColor color, hstring label)
            : m_id(id), m_active(active), m_color(color), m_label(label) {}

        // ---- INotifyPropertyChanged ----
        event_token PropertyChanged(
            Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        { return m_propertyChanged.add(handler); }
        void PropertyChanged(event_token const& token)
        { m_propertyChanged.remove(token); }

        // ---- 속성 ----
        hstring Id() const { return m_id; }
        void Id(hstring const& v) { if (m_id != v) { m_id = v; RaisePropertyChanged(L"Id"); } }

        bool Active() const { return m_active; }
        void Active(bool v) { if (m_active != v) { m_active = v; RaisePropertyChanged(L"Active"); } }

        DHLS_StrandJack::JackColor Color() const { return m_color; }
        void Color(DHLS_StrandJack::JackColor v) { if (m_color != v) { m_color = v; RaisePropertyChanged(L"Color"); } }

        hstring Label() const { return m_label; }
        void Label(hstring const& v) { if (m_label != v) { m_label = v; RaisePropertyChanged(L"Label"); } }

    private:
        hstring           m_id;
        bool              m_active = false;
        DHLS_StrandJack::JackColor m_color = DHLS_StrandJack::JackColor::Inactive;
        hstring           m_label;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

    protected:
        void RaisePropertyChanged(hstring const& propertyName)
        {
            m_propertyChanged(*this,
                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
        }
    };

    // =====================================================
    //  JackArrangementView — UserControl 본체
    //  HTML: <div x-data="jackArrangement()">
    // =====================================================
    struct JackArrangementView : JackArrangementViewT<JackArrangementView>
    {
        // ---- 생성자 ----
        JackArrangementView();
        // The hosting view can dismiss this screen without navigating the main window.
        std::function<void(bool)> Dismiss;

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


        // ---- Loaded event ----
        void OnLoaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        // ---- INotifyPropertyChanged ----
        event_token PropertyChanged(
            Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        { return m_propertyChanged.add(handler); }
        void PropertyChanged(event_token const& token)
        { m_propertyChanged.remove(token); }

        // ---- State 속성 (IDL 정의) ----

        // HTML: activeTemplate (초기값 2)
        int32_t ActiveTemplate() const { return m_activeTemplate; }
        void ActiveTemplate(int32_t value)
        {
            if (m_activeTemplate != value)
        {
                m_activeTemplate = value;
                RaisePropertyChanged(L"ActiveTemplate");
                UpdateTemplateBorders();
        }
        }

        // ---- Commands (IDL 정의) ----
        // HTML: selectTemplate(templateId)
        void SelectTemplate(int32_t templateId);

        // x:Bind용 래퍼 (Click 이벤트 시그니처 호환)
        void SelectTemplate1();
        void SelectTemplate2();
        void SelectTemplate3();
        void SelectTemplate4();
        void SelectStackedTwoLine();

        // HTML: OK 버튼
        void Confirm();

        // HTML: Cancel 버튼
        void Cancel();

        // HTML: Close (X) 버튼
        void Close();

    private:
        // ---- 필드 ----
        int32_t m_activeTemplate = 2;   // HTML 초기값: 2
        Windows::Foundation::Collections::IObservableVector<DHLS_StrandJack::JackCellData> m_gridData{ nullptr };

        // ---- static 헬퍼 (Converter에서도 사용) ----

        // 셀 색상 → hex 문자열 (HTML: getColorHex)
        static hstring GetColorHex(DHLS_StrandJack::JackColor color);

        // 텍스트 색상 (HTML: getTextColor — yellow=검정, 나머지=흰색)
        static hstring GetTextColor(DHLS_StrandJack::JackColor color);

        // "#RRGGBB" → Windows::UI::Color 파싱
        static winrt::Windows::UI::Color ParseColor(hstring const& hex);

        // ARGB → Color 직접 생성 (ColorHelper 대체)
        static winrt::Windows::UI::Color MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b);

        // ---- 내부 메서드 ----

        // HTML: generateGrid(templateId) — 32셀 데이터 생성
        void GenerateGrid(int32_t templateId);

        // XAML의 CylinderGrid에 자식 요소 동적 추가 (32개 셀)
        void InitializeGrid();

        // 템플릿 선택 테두리 업데이트 (HTML: :class 바인딩)
        void UpdateTemplateBorders();

        // 그리드 셀 UIElement 생성 (실린더 SVG → XAML Border/StackPanel)
        Microsoft::UI::Xaml::UIElement CreateCellElement(int32_t row, int32_t col);

        // INPC 이벤트
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

    protected:
        void RaisePropertyChanged(hstring const& propertyName)
        {
            m_propertyChanged(*this,
                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
        }
    };


}

// =====================================================
//  factory_implementation — WinUI 3 표준 패턴
//  winrt::make<T>() 및 XAML 인스턴스화에 필요
// =====================================================
namespace winrt::DHLS_StrandJack::factory_implementation
{
    struct JackArrangementView : JackArrangementViewT<JackArrangementView, implementation::JackArrangementView>
    {
    };

    struct JackCellData : JackCellDataT<JackCellData, implementation::JackCellData>
    {
    };


}
