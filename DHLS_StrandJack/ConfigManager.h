// ? ConfigManager ? JSON Serialization / Deserialization via WinRT ?
//
// ConfigService.h를 대체합니다 (nlohmann::json → winrt::Windows::Data::Json).
// Reuses DHLS_StrandJack::Models structs from SystemConfigModels.h
//
#pragma once

#include "SystemConfigModels.h"
#include <string>
#include <vector>
#include <filesystem>

namespace DHLS_StrandJack::Services
{
	using namespace DHLS_StrandJack::Models;

	class ConfigManager
	{
	public:
		static ConfigManager& Instance()
		{
			static ConfigManager s_instance;
			return s_instance;
		}

		SystemConfig& GetConfig() { return m_Config; }
		const SystemConfig& GetConfig() const { return m_Config; }

		const std::wstring& GetSettingsPassword() const { return m_SettingsPassword; }
		void SetSettingsPassword(const std::wstring& pwd) { m_SettingsPassword = pwd; }

		// ═══════════════════════════════════════════════
		// §SECTION 1 — JSON Serialization (winrt::Windows::Data::Json)
		// ═══════════════════════════════════════════════

		bool LoadFromJson(const std::wstring& filePath);
		bool SaveToJson(const std::wstring& filePath);
		static std::string SerializeSnapshot(SystemConfig const& config, std::wstring const& password);
		static bool SaveSnapshotToJson(const std::wstring& filePath, SystemConfig const& config, std::wstring const& password);
		static bool LoadDefaultJackSettings(JackSettings& settings);
        static bool IsValidModelName(std::wstring const& name);

		// ═══════════════════════════════════════════════
		// §SECTION 2 — Model File Management
		// ═══════════════════════════════════════════════

        // Models is a sibling of the executable's Bin directory.
        static std::filesystem::path GetModelsDirectory();
		std::vector<std::wstring> GetModelFileList(const std::wstring& modelsDir);
		bool DeleteModelFile(const std::wstring& filePath);
		bool CreateNewModel(const std::wstring& modelDir, const std::wstring& modelName);
		void EnsureModelsDirectory(const std::wstring& modelsDir);

	private:
		SystemConfig m_Config;
		std::wstring m_SettingsPassword;

		ConfigManager();
		~ConfigManager() = default;
		ConfigManager(const ConfigManager&) = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;
	};
} // namespace DHLS_StrandJack::Services
