#pragma once
#include <atomic>
namespace DHLS_StrandJack::Control {
// Session-only developer switch. Never persisted in a model or configuration.
inline std::atomic<bool> DeveloperSimulationEnabled{false};
}
