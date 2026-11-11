// RelayCommand ICommand Implementation for C++/WinRT
//
// ═══════════════════════════════════════════════
// MFC → WinUI 3 Migration: Command Infrastructure
// ═══════════════════════════════════════════════
//
// [MFC Original]
//   Message map entries: ON_BN_CLICKED(IDC_BTN_*, &Dlg::OnBnClicked*)
//   Direct procedural coupling between button click and handler function.
//
// [WinUI 3 Migration]
//   ICommand implementation enabling {x:Bind} to ViewModel methods.
//   Completely decoupled from UI controls ViewModel has zero references
//   to Microsoft.UI.Xaml.Controls.
//
// Constraint Compliance:
//   [C01] No 1:1 event binding all button actions routed through ICommand
//
#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <functional>
#include <memory>

namespace DHLS_StrandJack::Common
{
	// Implementation of ICommand for use in C++/WinRT ViewModels.
	// Replaces MFC ON_BN_CLICKED message map entries with bindable commands.
	struct RelayCommand : winrt::implements<RelayCommand, winrt::Microsoft::UI::Xaml::Input::ICommand>
	{
	private:
		std::function<void(winrt::Windows::Foundation::IInspectable const&)> m_execute;
		std::function<bool(winrt::Windows::Foundation::IInspectable const&)> m_canExecute;

	public:
		RelayCommand(
			std::function<void(winrt::Windows::Foundation::IInspectable const&)> execute,
			std::function<bool(winrt::Windows::Foundation::IInspectable const&)> canExecute = nullptr)
			: m_execute(std::move(execute))
			, m_canExecute(std::move(canExecute))
		{
		}

		// Convenience constructor for parameterless commands
		RelayCommand(std::function<void()> execute, std::function<bool()> canExecute = nullptr)
			: m_execute([fn = std::move(execute)](auto&&) { fn(); })
			, m_canExecute(canExecute
				? std::function<bool(winrt::Windows::Foundation::IInspectable const&)>(
					[fn = std::move(canExecute)](auto&&) { return fn(); })
				: nullptr)
		{
		}

		// ── ICommand implementation ──

		winrt::event_token CanExecuteChanged(winrt::Windows::Foundation::EventHandler<winrt::Windows::Foundation::IInspectable> const& handler)
		{
			return m_canExecuteChanged.add(handler);
		}

		void CanExecuteChanged(winrt::event_token const& token) noexcept
		{
			m_canExecuteChanged.remove(token);
		}

		bool CanExecute(winrt::Windows::Foundation::IInspectable const& parameter)
		{
			if (m_canExecute) return m_canExecute(parameter);
			return true;
		}

		void Execute(winrt::Windows::Foundation::IInspectable const& parameter)
		{
			if (m_execute) m_execute(parameter);
		}

		// Notify that CanExecute may have changed
		void RaiseCanExecuteChanged()
		{
			m_canExecuteChanged(*this, nullptr);
		}

	private:
		winrt::event<winrt::Windows::Foundation::EventHandler<winrt::Windows::Foundation::IInspectable>> m_canExecuteChanged;
	};
} // namespace DHLS_StrandJack::Common
