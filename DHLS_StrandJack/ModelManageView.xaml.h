#pragma once
#include "Localization.h"

#include "ModelManageView.g.h"
#include "RelayCommand.h"
#include "ConfigManager.h"
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::DHLS_StrandJack::implementation
{
  struct ModelManageView : ModelManageViewT<ModelManageView>
  {
        ModelManageView();

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


        // INotifyPropertyChanged
        winrt::event_token PropertyChanged(winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        { return m_propertyChanged.add(handler); }
        void PropertyChanged(winrt::event_token const& token) noexcept
        { m_propertyChanged.remove(token); }

        // Properties
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ModelFiles() const { return m_modelFiles; }
        int32_t SelectedModelIndex() const { return m_selectedIndex; }
        void SelectedModelIndex(int32_t value);

        // Commands
        winrt::Microsoft::UI::Xaml::Input::ICommand LoadModelCommand() const { return m_loadModelCommand; }
        winrt::Microsoft::UI::Xaml::Input::ICommand DeleteModelCommand() const { return m_deleteModelCommand; }
        winrt::Microsoft::UI::Xaml::Input::ICommand AddNewModelCommand() const { return m_addNewModelCommand; }

        void BackButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

  private:
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_modelFiles;
    int32_t m_selectedIndex = -1;
        std::vector<std::wstring> m_fileNames;

        winrt::Microsoft::UI::Xaml::Input::ICommand m_loadModelCommand{ nullptr };
        winrt::Microsoft::UI::Xaml::Input::ICommand m_deleteModelCommand{ nullptr };
        winrt::Microsoft::UI::Xaml::Input::ICommand m_addNewModelCommand{ nullptr };
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

        void RaisePropertyChanged(const wchar_t* name)
        {
            m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ name });
    }

        void RefreshList();
        void LoadModel();
        winrt::fire_and_forget DeleteModelAsync();

  };
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
  struct ModelManageView : ModelManageViewT<ModelManageView, implementation::ModelManageView>
  {
  };
}