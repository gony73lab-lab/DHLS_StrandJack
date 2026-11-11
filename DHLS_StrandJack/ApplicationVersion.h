#pragma once
#include <windows.h>
#include "AppVersion.h"
#include <winver.h>
#include <winrt/base.h>
#include <vector>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "version.lib")

namespace DHLS_StrandJack::Services
{
    // Read once from the running executable; works without package identity.
    inline winrt::hstring ApplicationVersionText()
    {
        static const winrt::hstring version = []() -> winrt::hstring
        {
            try
            {
                wchar_t path[MAX_PATH]{};
                auto length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
                if (!length || length >= MAX_PATH) return winrt::to_hstring("V " DHLS_VERSION_STRING);
                DWORD ignored{};
                DWORD size = ::GetFileVersionInfoSizeW(path, &ignored);
                if (!size) return winrt::to_hstring("V " DHLS_VERSION_STRING);
                std::vector<uint8_t> data(size);
                if (!::GetFileVersionInfoW(path, 0, size, data.data())) return winrt::to_hstring("V " DHLS_VERSION_STRING);
                void* value{}; UINT bytes{};
                if (!::VerQueryValueW(data.data(), L"\\", &value, &bytes) || !value || bytes < sizeof(VS_FIXEDFILEINFO)) return winrt::to_hstring("V " DHLS_VERSION_STRING);
                auto info = static_cast<VS_FIXEDFILEINFO const*>(value);
                std::wostringstream text;
                text << L"V " << ((info->dwFileVersionMS >> 16) & 0xFFFF) << L"."
                     << (info->dwFileVersionMS & 0xFFFF) << L"."
                     << ((info->dwFileVersionLS >> 16) & 0xFFFF) << L"."
                     << (info->dwFileVersionLS & 0xFFFF);
                return winrt::hstring{text.str()};
            }
            catch (...) { return winrt::to_hstring("V " DHLS_VERSION_STRING); }
        }();
        return version;
    }
}
