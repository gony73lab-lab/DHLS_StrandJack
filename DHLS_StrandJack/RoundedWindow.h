#pragma once

#include <windows.h>
#include <algorithm>
#pragma comment(lib, "gdi32.lib")

namespace DHLS_StrandJack::Common
{
    // XAML CornerRadius does not clip the HWND behind it. These captionless
    // windows need a matching native region, including on Windows 10.
    inline void UpdateRoundedWindowRegion(HWND hwnd) noexcept
    {
        if (!hwnd || !::IsWindow(hwnd)) return;
        // SetWindowRgn sends window-position messages; avoid reentrant updates.
        static thread_local bool updating = false;
        if (updating) return;
        updating = true;
        struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{ updating };

        const auto style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (::IsZoomed(hwnd) || (style & WS_CAPTION) == WS_CAPTION)
        {
            ::SetWindowRgn(hwnd, nullptr, TRUE);
            return;
        }

        RECT bounds{};
        if (!::GetWindowRect(hwnd, &bounds)) return;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) return;
        const UINT dpi = ::GetDpiForWindow(hwnd);
        const int diameter = (std::max)(1, ::MulDiv(16, dpi ? dpi : 96, 96));
        HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
        if (!region) return;
        // Success transfers ownership to Windows; only delete on failure.
        if (!::SetWindowRgn(hwnd, region, TRUE)) ::DeleteObject(region);
    }
}
