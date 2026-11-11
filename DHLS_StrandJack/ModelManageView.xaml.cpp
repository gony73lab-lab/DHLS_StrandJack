#include "pch.h"
#include "Localization.h"
#include "ModelManageView.xaml.h"
#if __has_include("ModelManageView.g.cpp")
#include "ModelManageView.g.cpp"
#endif

#include "NewModelView.xaml.h"
#include "MainMenuView.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::DHLS_StrandJack::implementation
{
	ModelManageView::ModelManageView()
	{
		InitializeComponent();

		m_modelFiles = winrt::single_threaded_observable_vector<IInspectable>();

		m_loadModelCommand = winrt::make<::DHLS_StrandJack::Common::RelayCommand>(
			[this](auto) { LoadModel(); }
		);
		m_deleteModelCommand = winrt::make<::DHLS_StrandJack::Common::RelayCommand>(
			[this](auto) { DeleteModelAsync(); }
		);
		m_addNewModelCommand = winrt::make<::DHLS_StrandJack::Common::RelayCommand>(
			[this](auto) { MainWindow::NavigateToView(winrt::DHLS_StrandJack::NewModelView{}, 640, 480); }//348, 320); }
		);
		RefreshList();
	}

	void ModelManageView::SelectedModelIndex(int32_t value)
	{
		if (m_selectedIndex != value)
		{
			m_selectedIndex = value;
			RaisePropertyChanged(L"SelectedModelIndex");
		}
	}

	void ModelManageView::RefreshList()
	{
		m_fileNames = ::DHLS_StrandJack::Services::ConfigManager::Instance().GetModelFileList(
            ::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory().wstring());
		m_modelFiles.Clear();
		for (const auto& fn : m_fileNames)
		{
			m_modelFiles.Append(winrt::box_value(winrt::hstring{ fn }));
		}
		RaisePropertyChanged(L"ModelFiles");
	}

	void ModelManageView::LoadModel()
	{
		if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_fileNames.size()) return;
		std::wstring path = (::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory() / m_fileNames[m_selectedIndex]).wstring();
		if (::DHLS_StrandJack::Services::ConfigManager::Instance().LoadFromJson(path))
		{
			MainWindow::NavigateToView(winrt::DHLS_StrandJack::MainMenuView{});
		}
	}
    winrt::fire_and_forget ModelManageView::DeleteModelAsync()
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_fileNames.size()) ||
            !IsLoaded() || !XamlRoot()) co_return;

        const std::wstring fileName = m_fileNames[m_selectedIndex];
        const auto path = ::DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory() / fileName;
        const bool english = ::DHLS_StrandJack::Services::Localization::IsEnglish();
        if (_wcsicmp(path.stem().c_str(),
            ::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName.c_str()) == 0)
        {
            Microsoft::UI::Xaml::Controls::ContentDialog notice;
            notice.XamlRoot(XamlRoot());
            notice.Title(box_value(english ? L"Model in use" : L"사용 중인 모델"));
            notice.Content(box_value(english ? L"Switch to another model before deleting this one."
                                           : L"현재 사용 중인 모델입니다. 다른 모델로 변경한 뒤 삭제하세요."));
            notice.CloseButtonText(english ? L"OK" : L"확인");
            co_await notice.ShowAsync();
            co_return;
        }

        Microsoft::UI::Xaml::Controls::ContentDialog confirm;
        confirm.XamlRoot(XamlRoot());
        confirm.Title(box_value(english ? L"Delete model" : L"모델 삭제"));
        confirm.Content(box_value(winrt::hstring{english
            ? L"Delete the selected model file?\n" + fileName
            : L"선택한 모델 파일을 삭제하시겠습니까?\n" + fileName}));
        confirm.PrimaryButtonText(english ? L"Delete" : L"삭제");
        confirm.CloseButtonText(english ? L"Cancel" : L"취소");
        confirm.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        if (co_await confirm.ShowAsync() != Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary ||
            !IsLoaded()) co_return;

        const bool removed = ::DHLS_StrandJack::Services::ConfigManager::Instance().DeleteModelFile(path.wstring());
        SelectedModelIndex(-1);
        RefreshList();
        if (!removed && IsLoaded() && XamlRoot())
        {
            Microsoft::UI::Xaml::Controls::ContentDialog failure;
            failure.XamlRoot(XamlRoot());
            failure.Title(box_value(english ? L"Delete failed" : L"삭제 실패"));
            failure.Content(box_value(english ? L"Could not delete the model file." : L"모델 파일을 삭제하지 못했습니다."));
            failure.CloseButtonText(english ? L"OK" : L"확인");
            co_await failure.ShowAsync();
        }
    }

    void ModelManageView::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (::DHLS_StrandJack::Services::ConfigManager::Instance().GetConfig().ModelInfo.ModelName.empty())
            MainWindow::NavigateToView(nullptr, 480, 420);
        else
            MainWindow::NavigateToView(winrt::DHLS_StrandJack::MainMenuView{}, 1920, 1080);
    }
}
