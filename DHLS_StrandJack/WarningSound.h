#pragma once
#include "SystemConfigModels.h"
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
namespace DHLS_StrandJack::Services {
inline void PlayWarningSound(Models::SoundSettings const& settings,bool warning=true) noexcept {
    if(!settings.bUseWarningSound)return;
    auto const& path=warning?settings.WarningSound:settings.CautionSound;
    auto const& fallback=settings.WarningSound;
    if(!path.empty())::PlaySoundW(path.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
    else if(!fallback.empty())::PlaySoundW(fallback.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
}
}
