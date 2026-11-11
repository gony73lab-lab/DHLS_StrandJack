﻿// =========================================================
//  JackArrangementView.xaml.cpp — 잭 배치 UserControl 구현
//  HTML: 잭 배치.html
//  로직: generateGrid, selectTemplate, getColorHex, getTextColor
//  DHLS_StrandJack 프로젝트: Microsoft.UI.Xaml (WinUI 3)
// =========================================================
#include "pch.h"
#include "Localization.h"
#include "JackArrangementView.xaml.h"
#if __has_include("JackArrangementView.g.cpp")
#include "JackArrangementView.g.cpp"
#endif
#if __has_include("JackCellData.g.cpp")
#include "JackCellData.g.cpp"
#endif

#include "MainWindow.xaml.h"
#include "ControlSettingsView.xaml.h"
#include "MainMenuView.xaml.h"

#include <winrt/Microsoft.UI.Text.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::DHLS_StrandJack::implementation
{
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI;

    // =========================================================
    //  JackArrangementView — 생성자
    //  HTML: init() { this.selectTemplate(this.activeTemplate) }
    // =========================================================
    JackArrangementView::JackArrangementView()
    {
        InitializeComponent();
        m_activeTemplate = ::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(
            ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.JackArrangementTemplate);
        const Border frames[] = { Template1Border(), Template2Border(), Template3Border(), Template4Border(), StackedTwoLineBorder() };
        for (int id = 1; id <= ::DHLS_StrandJack::Models::StackedTwoLineTemplate; ++id)
        {
            auto frame = frames[id - 1];
            auto button = frame.Child().as<Button>();
            button.PointerEntered([weak = get_weak(), weakFrame = make_weak(frame), id](auto const&, auto const&)
            {
                if (auto self = weak.get(); self && self->m_activeTemplate != id)
                    if (auto border = weakFrame.get())
                        border.BorderBrush(SolidColorBrush(MakeColor(255, 147, 197, 253)));
            });
            button.PointerExited([weak = get_weak()](auto const&, auto const&)
            {
                if (auto self = weak.get()) self->UpdateTemplateBorders();
            });
        }

        // HTML: gridData = [] (빈 배열 초기화)
        m_gridData = winrt::single_threaded_observable_vector<DHLS_StrandJack::JackCellData>();

        // 동적 UI 생성은 Loaded 이벤트에서 수행 (XAML 트리 완전 로드 후)
        this->Loaded({ this, &JackArrangementView::OnLoaded });
    }

    // =========================================================
    //  OnLoaded — XAML 트리 완전 로드 후 동적 UI 초기화
    // =========================================================
    void JackArrangementView::OnLoaded(
        winrt::Windows::Foundation::IInspectable const& /*sender*/,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
    {
        // HTML: init() -> selectTemplate(2) (초기값)
        GenerateGrid(m_activeTemplate);
        InitializeGrid();
        UpdateTemplateBorders();
    }

    // =========================================================
    //  SelectTemplate — 템플릿 선택
    //  HTML: selectTemplate(templateId) {
    //          this.activeTemplate = templateId;
    //          this.gridData = this.generateGrid(templateId);
    //        }
    // =========================================================
    void JackArrangementView::SelectTemplate(int32_t templateId)
    {
        if (templateId != ::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(templateId))
            throw winrt::hresult_invalid_argument();
        ActiveTemplate(templateId);       // setter -> RaisePropertyChanged + UpdateTemplateBorders
        GenerateGrid(templateId);         // gridData 재생성
        InitializeGrid();                 // UI 갱신
    }

    void JackArrangementView::SelectTemplate1() { SelectTemplate(1); }
    void JackArrangementView::SelectTemplate2() { SelectTemplate(2); }
    void JackArrangementView::SelectTemplate3() {}
    void JackArrangementView::SelectTemplate4() {}
    void JackArrangementView::SelectStackedTwoLine()
    {
        SelectTemplate(::DHLS_StrandJack::Models::StackedTwoLineTemplate);
    }

    // =========================================================
    //  Confirm / Cancel / Close — 버튼 핸들러
    //  HTML: OK / Cancel / Close(X) 버튼
    // =========================================================
    void JackArrangementView::Confirm()
    {
        auto lifetime = get_strong();
        ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ControlSettings.JackArrangementTemplate = m_activeTemplate;
        if (Dismiss) { auto dismiss = Dismiss; dismiss(true); return; }
        // Confirm -> ControlSettingsView
        MainWindow::NavigateToView(winrt::make<ControlSettingsView>(), 1024, 800);
    }

    void JackArrangementView::Cancel()
    {
        auto lifetime = get_strong();
        if (Dismiss) { auto dismiss = Dismiss; dismiss(false); return; }
        // Cancel -> MainMenuView
        MainWindow::NavigateToView(winrt::make<MainMenuView>(), 1920, 1080);
    }

    void JackArrangementView::Close()
    {
        auto lifetime = get_strong();
        if (Dismiss) { auto dismiss = Dismiss; dismiss(false); return; }
        // Close -> MainMenuView
        MainWindow::NavigateToView(winrt::make<MainMenuView>(), 1920, 1080);
    }
    // Logical indices 0..7 are line 1 and 8..15 are line 2.
    // The visual grid shows ports 1..4 in the first row and 5..8 in the second.
    void JackArrangementView::GenerateGrid(int32_t templateId)
    {
        if (templateId != ::DHLS_StrandJack::Models::NormalizeJackArrangementTemplate(templateId))
            throw winrt::hresult_invalid_argument();
        m_gridData.Clear();
        const bool stacked = templateId == ::DHLS_StrandJack::Models::StackedTwoLineTemplate;
        const int rows = templateId == 1 ? 1 : 2;
        const int columns = 8;
        const DHLS_StrandJack::JackColor colors[] = { DHLS_StrandJack::JackColor::Red,
            DHLS_StrandJack::JackColor::Green, DHLS_StrandJack::JackColor::Yellow, DHLS_StrandJack::JackColor::Cyan };
        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < columns; ++col)
            {
                const int group = stacked ? row + 1 : templateId == 2 ? (col < 4 ? 1 : 2) : 1;
                const int number = templateId == 2 ? row * 4 + (col % 4) + 1 : col + 1;
                auto label = winrt::to_hstring(group) + L"-" + winrt::to_hstring(number);
                m_gridData.Append(winrt::make<JackCellData>(
                    winrt::to_hstring(row) + L"-" + winrt::to_hstring(col), true, colors[group - 1], label));
            }
        }
    }

    void JackArrangementView::InitializeGrid()
    {
        auto grid = CylinderGrid();
        grid.Children().Clear();
        grid.RowDefinitions().Clear();
        grid.RowSpacing(4);
        grid.ColumnSpacing(4);
        const int columns = 8;
        const int rows = static_cast<int>(m_gridData.Size()) / columns;
        grid.ColumnDefinitions().Clear();
        for (int col = 0; col < columns; ++col)
        {
            ColumnDefinition definition;
            definition.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            grid.ColumnDefinitions().Append(definition);
        }
        for (int row = 0; row < rows; ++row)
        {
            RowDefinition definition;
            definition.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            grid.RowDefinitions().Append(definition);
            for (int col = 0; col < columns; ++col)
            {
                auto cell = CreateCellElement(row, col).as<FrameworkElement>();
                Grid::SetRow(cell, row);
                Grid::SetColumn(cell, col);
                grid.Children().Append(cell);
            }
        }
    }

    UIElement JackArrangementView::CreateCellElement(int32_t row, int32_t col)
    {
        const int columns = 8;
        auto data = m_gridData.GetAt(row * columns + col);
        auto drawing = Resources().Lookup(box_value(L"CylinderVisualTemplate")).as<DataTemplate>();
        auto cell = drawing.LoadContent().as<FrameworkElement>();
        cell.FindName(L"CylinderLowerBody").as<Microsoft::UI::Xaml::Shapes::Path>().Fill(
            SolidColorBrush(ParseColor(GetColorHex(data.Color()))));
        cell.FindName(L"CylinderNumber").as<TextBlock>().Text(data.Label());
        return cell;
    }
    // =========================================================
    //  UpdateTemplateBorders — 템플릿 선택 테두리 업데이트
    //  HTML: :class="activeTemplate === N ? 'border-[#2563EB]' : 'border-transparent'"
    // =========================================================
    void JackArrangementView::UpdateTemplateBorders()
    {
        auto activeBrush = SolidColorBrush(MakeColor(0xFF, 0x25, 0x63, 0xEB));
        auto inactiveBrush = SolidColorBrush(Microsoft::UI::Colors::Transparent());

        auto t1 = this->FindName(L"Template1Border").try_as<Border>();
        auto t2 = this->FindName(L"Template2Border").try_as<Border>();
        auto t3 = this->FindName(L"Template3Border").try_as<Border>();
        auto t4 = this->FindName(L"Template4Border").try_as<Border>();

        if (t1) t1.BorderBrush(m_activeTemplate == 1 ? activeBrush : inactiveBrush);
        if (t2) t2.BorderBrush(m_activeTemplate == 2 ? activeBrush : inactiveBrush);
        if (t3) t3.BorderBrush(m_activeTemplate == 3 ? activeBrush : inactiveBrush);
        if (t4) t4.BorderBrush(m_activeTemplate == 4 ? activeBrush : inactiveBrush);
        if (auto stacked = StackedTwoLineBorder())
            stacked.BorderBrush(m_activeTemplate == ::DHLS_StrandJack::Models::StackedTwoLineTemplate ? activeBrush : inactiveBrush);
    }

    // =========================================================
    //  GetColorHex — 셀 색상 -> hex 문자열
    //  HTML: getColorHex(colorStr)
    //    red    -> #B91C1C
    //    green  -> #15803D
    //    yellow -> #FACC15
    //    cyan   -> #0E7490
    // =========================================================
    hstring JackArrangementView::GetColorHex(DHLS_StrandJack::JackColor color)
    {
        switch (color)
        {
        case DHLS_StrandJack::JackColor::Red:     return L"#FF2020";
        case DHLS_StrandJack::JackColor::Green:   return L"#00E534";
        case DHLS_StrandJack::JackColor::Yellow:  return L"#F5EB00";
        case DHLS_StrandJack::JackColor::Cyan:    return L"#00DDE6";
        default:                            return L"#000000";
        }
    }

    // =========================================================
    //  GetTextColor — 텍스트 색상
    //  HTML: getTextColor(colorStr)
    //    yellow -> #000000 (검정)
    //    나머지 -> #FFFFFF (흰색)
    // =========================================================
    hstring JackArrangementView::GetTextColor(DHLS_StrandJack::JackColor color)
    {
        return (color == DHLS_StrandJack::JackColor::Yellow) ? L"#000000" : L"#FFFFFF";
    }

    // =========================================================
    //  MakeColor — ARAB -> Color 직접 생성 (ColorHelper 대체)
    //  WinUI 3에서 ColorHelper::FromArgb 대신 구조체 직접 생성
    // =========================================================
    winrt::Windows::UI::Color JackArrangementView::MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        winrt::Windows::UI::Color color;
        color.A = a;
        color.R = r;
        color.G = g;
        color.B = b;
        return color;
    }

    // =========================================================
    //  ParseColor — "#RRGGBB" -> Color 파싱
    // =========================================================
    winrt::Windows::UI::Color JackArrangementView::ParseColor(hstring const& hex)
    {
        auto str = winrt::to_string(hex);
        if (str.length() >= 7 && str[0] == '#')
        {
            uint8_t r = static_cast<uint8_t>(std::stoul(str.substr(1, 2), nullptr, 16));
            uint8_t g = static_cast<uint8_t>(std::stoul(str.substr(3, 2), nullptr, 16));
            uint8_t b = static_cast<uint8_t>(std::stoul(str.substr(5, 2), nullptr, 16));
            return MakeColor(0xFF, r, g, b);
        }
        return MakeColor(0xFF, 0, 0, 0);
    }


}
