#pragma once
#include "Localization.h"

#include "NewModelView.g.h"
#include "RelayCommand.h"
#include "ConfigManager.h"

namespace winrt::DHLS_StrandJack::implementation
{
	struct NewModelView : NewModelViewT<NewModelView>
	{
		NewModelView();

        void OnLocalizedLoaded(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            ::DHLS_StrandJack::Services::Localization::RegisterElement(
                sender.as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
        }


		// --- ModelName property ---
		hstring ModelName() { return m_modelName; }
		void ModelName(hstring const& value)
		{
			if (m_modelName != value)
			{
				::DHLS_StrandJack::Services::AuditLog::Write("DATA_CHANGE","NewModel.ModelName old="+winrt::to_string(m_modelName)+" new="+winrt::to_string(value));
                m_modelName = value;
				m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ L"ModelName" });
			}
		}

		// --- Create button click event ---
		winrt::fire_and_forget ConfirmCreate();

		// --- Cancel button click event ---
		void CancelCreate();

		// Commands
		winrt::Microsoft::UI::Xaml::Input::ICommand ConfirmCreateCommand() const { return m_confirmCreateCommand; }

		// --- INotifyPropertyChanged ---
		winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
		{
			return m_propertyChanged.add(handler);
		}
		void PropertyChanged(winrt::event_token const& token)
		{
			m_propertyChanged.remove(token);
		}

	private:
		bool m_isCreating = false;
        bool m_cancelBusy=false;
        winrt::fire_and_forget CancelCreateAsync();
		hstring m_modelName;
		winrt::Microsoft::UI::Xaml::Input::ICommand m_confirmCreateCommand{ nullptr };
		winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

		void RaisePropertyChanged(const wchar_t* name)
		{
			m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ name });
		}

		winrt::Windows::Foundation::IAsyncAction ConfirmCreateAsync();
	};
}

namespace winrt::DHLS_StrandJack::factory_implementation
{
	struct NewModelView : NewModelViewT<NewModelView, implementation::NewModelView>
	{
	};
}
