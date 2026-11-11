#include "pch.h"
#include "Localization.h"
#include "NewModelView.xaml.h"
#if __has_include("NewModelView.g.cpp")
#include "NewModelView.g.cpp"
#endif
#include "ControlSettingsView.xaml.h"
#include "ModelManageView.xaml.h"
#include "MainMenuView.xaml.h"
#include "MainWindow.xaml.h"
#include "ConfigManager.h"
#include "PlcDefaults.h"
#include "SettingsAudit.h"
#include <cwchar>
#include <filesystem>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project steructure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::DHLS_StrandJack::implementation
{
	NewModelView::NewModelView()
	{
		InitializeComponent();
		m_confirmCreateCommand = winrt::make<::DHLS_StrandJack::Common::RelayCommand>(
			[weak = get_weak()](auto) { if (auto self = weak.get()) self->ConfirmCreate(); }
		);
	}

	winrt::fire_and_forget NewModelView::ConfirmCreate()
	{
		auto lifetime = get_strong();
		if (m_isCreating) co_return;
		m_isCreating = true;
		hstring error;
		try
		{
			co_await ConfirmCreateAsync();
		}
		catch (...)
		{
			const auto result = winrt::to_hresult();
			::OutputDebugStringW(winrt::hresult_error(result).message().c_str());
			error = ::DHLS_StrandJack::Services::Localization::Text(L"모델 생성 중 오류가 발생했습니다.");
			wchar_t code[32]{};
			swprintf_s(code, L"HRESULT: 0x%08X\n", static_cast<unsigned int>(result.value));
			error = hstring{ std::wstring{ code } + std::wstring{ error } };
			::OutputDebugStringW(error.c_str());
		}
		m_isCreating = false;

		if (!error.empty())
		{
			if (auto root = XamlRoot())
			{
				Controls::ContentDialog dialog;
				dialog.XamlRoot(root);
				dialog.Title(box_value(::DHLS_StrandJack::Services::Localization::Text(L"Model creation failed")));
				dialog.Content(box_value(error));
				dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"OK"));
                dialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
				try { co_await dialog.ShowAsync(); }
				catch (...) { ::OutputDebugStringW(L"Unable to display model creation error.\n"); }
			}
		}
	}

	winrt::Windows::Foundation::IAsyncAction NewModelView::ConfirmCreateAsync()
	{
		if (m_modelName.empty()) co_return;

		// Prevent use-after-free: keep self alive across co_await
		auto strong_this = get_strong();
		auto pConfigManager = &::DHLS_StrandJack::Services::ConfigManager::Instance();
		const std::wstring modelName{ std::wstring_view{ m_modelName } };
		if (!pConfigManager->IsValidModelName(modelName))
        {
            Controls::ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(L"모델 이름 오류"));
            dialog.Content(box_value(L"모델 이름에 사용할 수 없는 문자 또는 예약된 이름이 있습니다."));
            dialog.CloseButtonText(L"확인");
            co_await dialog.ShowAsync();
            co_return;
        }
		const auto modelsDirectory = pConfigManager->GetModelsDirectory();
		const auto modelPath = modelsDirectory / (modelName + L".json");
		winrt::apartment_context ui_thread{};

		// Check before resetting the active config or opening a file for writing.
		co_await winrt::resume_background();
		std::error_code lookupError;
		const bool alreadyExists = std::filesystem::exists(modelPath, lookupError);
		::DHLS_StrandJack::Models::JackSettings defaultJackSettings;
		const bool defaultsLoaded = !lookupError && !alreadyExists &&
			pConfigManager->LoadDefaultJackSettings(defaultJackSettings);
		co_await ui_thread;
		if (!XamlRoot()) co_return;
		if (lookupError)
			throw std::filesystem::filesystem_error("Unable to check model file", modelPath, lookupError);
		if (alreadyExists)
		{
			Controls::ContentDialog dialog;
			dialog.XamlRoot(XamlRoot());
			Controls::StackPanel warningTitle;
			warningTitle.Orientation(Controls::Orientation::Horizontal);
			warningTitle.Spacing(8);
			Controls::FontIcon warningIcon;
			warningIcon.Glyph(L"\uE7BA");
			warningIcon.FontSize(24);
			warningIcon.Foreground(Media::SolidColorBrush(Microsoft::UI::Colors::DarkOrange()));
			warningIcon.VerticalAlignment(VerticalAlignment::Center);
			Controls::TextBlock warningLabel;
			warningLabel.Text(::DHLS_StrandJack::Services::Localization::Text(L"모델 중복"));
			warningLabel.VerticalAlignment(VerticalAlignment::Center);
			warningTitle.Children().Append(warningIcon);
			warningTitle.Children().Append(warningLabel);
			dialog.Title(warningTitle);
			dialog.Content(box_value(hstring{ L"\"" + modelName + L"\"" +
				std::wstring{ ::DHLS_StrandJack::Services::Localization::Text(
					L" 모델이 이미 존재합니다.\n다른 모델명을 입력해 주세요.") } }));
			dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
                dialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
			co_await dialog.ShowAsync();
			co_return;
		}

		if (!defaultsLoaded)
		{
			Controls::ContentDialog dialog;
			dialog.XamlRoot(XamlRoot());
			dialog.Title(box_value(::DHLS_StrandJack::Services::Localization::Text(L"오류")));
			dialog.Content(box_value(::DHLS_StrandJack::Services::Localization::Text(
				L"기본 잭 설정을 읽지 못했습니다. Models\\Default.json 파일을 확인하세요.")));
			dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
                dialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
			co_await dialog.ShowAsync();
			co_return;
		}

		const auto currentLanguage = pConfigManager->GetConfig().ModelInfo.Language;
		::DHLS_StrandJack::Models::SystemConfig config{};
		config.ModelInfo.Language = currentLanguage.empty() ? L"ko" : currentLanguage;
		config.JackSettings = std::move(defaultJackSettings);
		config.PLCSettings.PLC_Count = 1;
		config.PLCSettings.PLC_IPs.clear();
		for (int plc = 1; plc <= config.PLCSettings.PLC_Count; ++plc)
		{
			config.PLCSettings.PLC_IPs.push_back(::DHLS_StrandJack::Models::DefaultPlcIp(plc - 1));
		}
		config.PLCSettings.Sensor_HPU_Count = 1;
		config.ModelInfo.ModelName = modelName;
		config.InitializeJackDetails();

		// Models 폴더가 없으면 생성
		std::error_code ec;
		std::filesystem::create_directories(modelsDirectory, ec);

		std::wstring path = modelPath.wstring();
        const auto password = pConfigManager->GetSettingsPassword();
		co_await winrt::resume_background();
		bool saved = pConfigManager->SaveSnapshotToJson(path, config, password);
		co_await ui_thread;

		// The user may have cancelled or closed the window while saving.
		if (!XamlRoot()) co_return;

		if (!saved)
		{
			auto root = this->XamlRoot();
			if (root)
			{
				Microsoft::UI::Xaml::Controls::ContentDialog dialog;
				dialog.XamlRoot(root);
				dialog.Title(winrt::box_value(::DHLS_StrandJack::Services::Localization::Text(L"오류")));
				dialog.Content(winrt::box_value(::DHLS_StrandJack::Services::Localization::Text(L"모델 파일 저장에 실패했습니다.")));
				dialog.CloseButtonText(::DHLS_StrandJack::Services::Localization::Text(L"확인"));
                dialog.CloseButtonStyle(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(
                    winrt::box_value(L"DialogAcceptIconStyle")).as<Microsoft::UI::Xaml::Style>());
				co_await dialog.ShowAsync();
			}
			co_return;
		}

        pConfigManager->GetConfig() = std::move(config);

		// Navigate after save — use MainWindow::NavigateToView
		::OutputDebugStringW(L"NewModelView: creating ControlSettingsView.\n");
		auto view = winrt::DHLS_StrandJack::ControlSettingsView{};
		::OutputDebugStringW(L"NewModelView: navigating to ControlSettingsView.\n");
		MainWindow::NavigateToView(view, 1280, 780);
	}

	void NewModelView::CancelCreate()
	{
        CancelCreateAsync();
	}
    winrt::fire_and_forget NewModelView::CancelCreateAsync(){
        auto lifetime=get_strong();if(m_cancelBusy||m_isCreating)co_return;m_cancelBusy=true;
        try{
            if(!m_modelName.empty()&&!co_await ::DHLS_StrandJack::Services::ConfirmDiscardAsync(XamlRoot(),::DHLS_StrandJack::Services::Localization::IsEnglish())){m_cancelBusy=false;co_return;}
            if(IsLoaded()){
                ::DHLS_StrandJack::Services::AuditLog::Write("DISCARD","NewModel draft");
                MainWindow::NavigateToView(nullptr,640,230);
            }
        }catch(...){::DHLS_StrandJack::Services::AuditLog::Write("ERROR","NewModel exit confirmation failed");}
        m_cancelBusy=false;
    }
}
