// ViewModelBase ? INotifyPropertyChanged base for C++/WinRT
#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace DHLS_StrandJack::Common
{
	struct ViewModelBase : winrt::implements<ViewModelBase, winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged>
	{
		winrt::event_token PropertyChanged(winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
		{
			return m_propertyChanged.add(handler);
		}

		void PropertyChanged(winrt::event_token const& token) noexcept
		{
			m_propertyChanged.remove(token);
		}

		template<typename T>
		bool SetProperty(T& storage, T const& value, const wchar_t* propertyName)
		{
			if (storage == value) return false;
			storage = value;
			RaisePropertyChanged(propertyName);
			return true;
		}

		bool SetProperty(winrt::hstring& storage, winrt::hstring const& value, const wchar_t* propertyName)
		{
			if (storage == value) return false;
			storage = value;
			RaisePropertyChanged(propertyName);
			return true;
		}

		bool SetProperty(double& storage, double const& value, const wchar_t* propertyName, double epsilon = 1e-9)
		{
			if (std::abs(storage - value) < epsilon) return false;
			storage = value;
			RaisePropertyChanged(propertyName);
			return true;
		}

		void RaisePropertyChanged(const wchar_t* propertyName)
		{
			m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
		}

		// Alias used by several ViewModels
		void OnPropertyChanged(const wchar_t* propertyName)
		{
			RaisePropertyChanged(propertyName);
		}

		void RaiseAllPropertiesChanged()
		{
			m_propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ L"" });
		}

	protected:
		winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
	};

	// Type conversion helpers
	inline winrt::hstring ToHstring(const std::wstring& ws) { return winrt::hstring{ ws }; }
	inline std::wstring ToWstring(const winrt::hstring& hs) { return std::wstring{ hs.c_str(), hs.size() }; }

	// Numeric formatting helper
	inline winrt::hstring FormatDouble(double value, int precision = 0)
	{
		std::wstringstream ss;
		if (precision > 0)
			ss << std::fixed << std::setprecision(precision) << value;
		else
			ss << static_cast<int>(std::round(value));
		return winrt::hstring{ ss.str() };
	}

	// PROP_GET_SET macro ? note: parameter order is (type, name, member)
#define PROP_GET_SET(type, name, member) \
        type name() const { return member; } \
        void name(type const& value) { SetProperty(member, value, L#name); }

#define PROP_GET_SET_HSTRING(name, member) \
        winrt::hstring name() const { return member; } \
        void name(winrt::hstring const& value) { SetProperty(member, value, L#name); }

} // namespace DHLS_StrandJack::Common
