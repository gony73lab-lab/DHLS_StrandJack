// ConfigManager.cpp — JSON I/O via winrt::Windows::Data::Json
// Replaces ConfigService.h (nlohmann::json implementation)
//
#include "pch.h"
#include "ConfigManager.h"
#include "PlcDefaults.h"
#include "AuditLog.h"
#include "Localization.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>

using namespace winrt::Windows::Data::Json;
using namespace DHLS_StrandJack::Models;


// ═══════════════════════════════════════════════
// §SECTION 0.1 — ConfigManager Constructor (default password)
// ═══════════════════════════════════════════════

DHLS_StrandJack::Services::ConfigManager::ConfigManager()
	: m_SettingsPassword(L"1234")  // TODO: 배포 전 설정 파일에서 암호를 변경해야 합니다.
{
}

// ═══════════════════════════════════════════════
// §SECTION 0 — Encoding & Safe Access Helpers
// ═══════════════════════════════════════════════

namespace
{
	inline std::string ToUtf8(std::wstring_view wstr)
	{
		return winrt::to_string(wstr);
	}


	// Formats valid Stringify() output without changing string contents or number tokens.
	std::string PrettyPrintJson(std::string_view json)
	{
		std::string result;
		result.reserve(json.size() * 2);
		std::size_t depth = 0;
		bool inString = false, escaped = false;
		auto newline = [&] { result += "\r\n"; result.append(depth * 4, ' '); };
		for (std::size_t i = 0; i < json.size(); ++i)
		{
			const char c = json[i];
			if (inString)
			{
				result += c;
				if (escaped) escaped = false;
				else if (c == '\\') escaped = true;
				else if (c == '"') inString = false;
				continue;
			}
			if (c == '"') { inString = true; result += c; }
			else if (c == '{' || c == '[')
			{
				result += c;
				std::size_t next = i + 1;
				while (next < json.size() && (json[next] == ' ' || json[next] == '\t' || json[next] == '\r' || json[next] == '\n')) ++next;
				if (next < json.size() && json[next] == (c == '{' ? '}' : ']'))
				{
					result += json[next]; i = next;
				}
				else { ++depth; newline(); }
			}
			else if (c == '}' || c == ']')
			{
				if (depth > 0) --depth;
				newline(); result += c;
			}
			else if (c == ',') { result += c; newline(); }
			else if (c == ':') result += ": ";
			else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') result += c;
		}
		result += "\r\n";
		return result;
	}

	inline std::wstring ToWString(std::string_view str)
	{
		return std::wstring(winrt::to_hstring(str));
	}

	// Safe string getter — mimics nlohmann::json .value() behavior
	std::wstring GetStr(const JsonObject& obj, const winrt::hstring& key, const std::wstring& defaultVal = L"")
	{
		if (obj && obj.HasKey(key))
		{
			auto val = obj.GetNamedValue(key);
			if (val.ValueType() == JsonValueType::String)
				return std::wstring(val.GetString());
		}
		return defaultVal;
	}

	// Safe number getter
	double GetNum(const JsonObject& obj, const winrt::hstring& key, double defaultVal = 0.0)
	{
		if (obj && obj.HasKey(key))
		{
			auto val = obj.GetNamedValue(key);
			if (val.ValueType() == JsonValueType::Number)
				return val.GetNumber();
		}
		return defaultVal;
	}

	// Safe boolean getter
	bool GetBool(const JsonObject& obj, const winrt::hstring& key, bool defaultVal = false)
	{
		if (obj && obj.HasKey(key))
		{
			auto val = obj.GetNamedValue(key);
			if (val.ValueType() == JsonValueType::Boolean)
				return val.GetBoolean();
		}
		return defaultVal;
	}
}

// ═══════════════════════════════════════════════
// §SECTION 1 — LoadFromJson
// ═══════════════════════════════════════════════

namespace
{

	std::vector<JackSpec> ReadJackSpecs(JsonArray const& values)
	{
		std::vector<JackSpec> specs;
		for (auto const& item : values)
		{
			if (item.ValueType() != JsonValueType::Object) continue;
			auto value = item.GetObject();
			JackSpec spec;
			spec.TypeName = GetStr(value, L"TypeName");
			spec.Capacity_ton = GetNum(value, L"Capacity_ton");
			spec.WedgeOpenConfirm_sec = GetNum(value, L"WedgeOpenConfirm_sec");
			spec.WedgeCloseConfirm_sec = GetNum(value, L"WedgeCloseConfirm_sec");
			spec.LoadTransferRange_mm = GetNum(value, L"LoadTransferRange_mm");
			spec.SafeRange_mm = GetNum(value, L"SafeRange_mm");
			spec.PressureSensor_ADScale = GetNum(value, L"PressureSensor_ADScale");
            spec.DisplacementSensor_ADScale = GetNum(value, L"DisplacementSensor_ADScale");
            spec.PressureSensorScale_bar = GetNum(value, L"PressureSensorScale_bar");
			spec.DisplacementSensorScale_mm = GetNum(value, L"DisplacementSensorScale_mm");
			spec.WorkingPressure_bar = GetNum(value, L"WorkingPressure_bar");
			spec.D1_Diameter_mm = GetNum(value, L"D1_Diameter_mm");
			spec.D2_Diameter_mm = GetNum(value, L"D2_Diameter_mm");
			spec.D3_Diameter_mm = GetNum(value, L"D3_Diameter_mm");
			spec.JackStroke_mm = GetNum(value, L"JackStroke_mm");
			spec.MaxLoadOperatingRange_mm = GetNum(value, L"MaxLoadOperatingRange_mm",
				(std::max)(0.0, spec.JackStroke_mm - 2.0 * spec.SafeRange_mm - spec.LoadTransferRange_mm));
			spec.CylinderColor = GetStr(value, L"CylinderColor");
			spec.PistonColor = GetStr(value, L"PistonColor");
			spec.ShowMotionArrow = GetBool(value, L"ShowMotionArrow");
			specs.push_back(std::move(spec));
		}
		return specs;
	}

}

bool DHLS_StrandJack::Services::ConfigManager::LoadFromJson(const std::wstring& filePath)
{
    SystemConfig candidate{};
    std::wstring password;
    try
    {
    AuditLog::Write("MODEL_LOAD_REQUEST",winrt::to_string(filePath));
	std::ifstream file(std::filesystem::path(filePath), std::ios::in | std::ios::binary);
	if (!file.is_open()) return false;

	std::ostringstream ss;
	ss << file.rdbuf();
	std::string jsonStr = ss.str();

	JsonObject root = nullptr;
	if (!JsonObject::TryParse(winrt::to_hstring(jsonStr), root))
		return false;

	if (!root.HasKey(L"SystemConfig"))
		return false;

	JsonObject sys = root.GetNamedObject(L"SystemConfig");

	// ── ModelInfo ──
	if (sys.HasKey(L"ModelInfo"))
	{
		JsonObject m = sys.GetNamedObject(L"ModelInfo");
		candidate.ModelInfo.ModelName = GetStr(m, L"ModelName");
		candidate.ModelInfo.Language = GetStr(m, L"Language", L"ko");
		candidate.ModelInfo.ProgramName = GetStr(m, L"ProgramName");
		candidate.ModelInfo.CustomerName = GetStr(m, L"CustomerName");
		candidate.ModelInfo.OperatorName = GetStr(m, L"OperatorName");
		candidate.ModelInfo.DocumentNumber = GetStr(m, L"DocumentNumber");
        candidate.ModelInfo.ModelVersion = GetStr(m, L"ModelVersion");
        candidate.ModelInfo.Description = GetStr(m, L"Description");
        candidate.ModelInfo.CreatedDate = GetStr(m, L"CreatedDate");
        candidate.ModelInfo.LastModifiedDate = GetStr(m, L"LastModifiedDate");
	}

	// ── ControlSettings ──
	if (sys.HasKey(L"ControlSettings"))
	{
		JsonObject c = sys.GetNamedObject(L"ControlSettings");
		candidate.ControlSettings.PressureDisplayDigits = static_cast<int>(GetNum(c, L"PressureDisplayDigits", candidate.ControlSettings.PressureDisplayDigits));
		candidate.ControlSettings.WeightDisplayDigits = static_cast<int>(GetNum(c, L"WeightDisplayDigits", candidate.ControlSettings.WeightDisplayDigits));
		candidate.ControlSettings.SpeedControlResolution = static_cast<int>(GetNum(c, L"SpeedControlResolution", candidate.ControlSettings.SpeedControlResolution));
		candidate.ControlSettings.PlcAdResolution = static_cast<int>(GetNum(c, L"PlcAdResolution", candidate.ControlSettings.PlcAdResolution));
		candidate.ControlSettings.DashboardJackCount = static_cast<int>((std::clamp)(GetNum(c, L"DashboardJackCount", candidate.ControlSettings.DashboardJackCount), 1.0, 16.0));
		candidate.ControlSettings.SyncDeviationMm = GetNum(c, L"SyncDeviationMm", candidate.ControlSettings.SyncDeviationMm);
		candidate.ControlSettings.AllowedDeviationMm = GetNum(c, L"AllowedDeviationMm", candidate.ControlSettings.AllowedDeviationMm);
		candidate.ControlSettings.AutoOverloadPercent = GetNum(c, L"AutoOverloadPercent", candidate.ControlSettings.AutoOverloadPercent);
		candidate.ControlSettings.ManualOverloadPercent = GetNum(c, L"ManualOverloadPercent", candidate.ControlSettings.ManualOverloadPercent);
		candidate.ControlSettings.WedgeExtraPressureSec = GetNum(c, L"WedgeExtraPressureSec", candidate.ControlSettings.WedgeExtraPressureSec);
		candidate.ControlSettings.OverloadAllowedSec = GetNum(c, L"OverloadAllowedSec", candidate.ControlSettings.OverloadAllowedSec);
		candidate.ControlSettings.PretensionRangeMm = GetNum(c, L"PretensionRangeMm", candidate.ControlSettings.PretensionRangeMm);
		candidate.ControlSettings.DecelerationSpeedHz = GetNum(c, L"DecelerationSpeedHz", candidate.ControlSettings.DecelerationSpeedHz);
		candidate.ControlSettings.DecelerationRangeMm = GetNum(c, L"DecelerationRangeMm", candidate.ControlSettings.DecelerationRangeMm);
		candidate.ControlSettings.RangeToleranceMm = GetNum(c, L"RangeToleranceMm", candidate.ControlSettings.RangeToleranceMm);
		candidate.ControlSettings.TemperatureMinC = GetNum(c, L"TemperatureMinC", candidate.ControlSettings.TemperatureMinC);
		candidate.ControlSettings.TemperatureMaxC = GetNum(c, L"TemperatureMaxC", candidate.ControlSettings.TemperatureMaxC);
		candidate.ControlSettings.CloseUpperWedgeOnLowering = GetBool(c, L"CloseUpperWedgeOnLowering", candidate.ControlSettings.CloseUpperWedgeOnLowering);
		candidate.ControlSettings.SynchronizeTotalLift = GetBool(c, L"SynchronizeTotalLift", candidate.ControlSettings.SynchronizeTotalLift);
		candidate.ControlSettings.AccumulateManualDistance = GetBool(c, L"AccumulateManualDistance", candidate.ControlSettings.AccumulateManualDistance);
		candidate.ControlSettings.RiseSync_Level1 = GetNum(c, L"RiseSync_Level1");
		candidate.ControlSettings.RiseSync_Level2 = GetNum(c, L"RiseSync_Level2");
		candidate.ControlSettings.FallSync_Level1 = GetNum(c, L"FallSync_Level1");
		candidate.ControlSettings.FallSync_Level2 = GetNum(c, L"FallSync_Level2");
		candidate.ControlSettings.WeightDeviation = GetNum(c, L"WeightDeviation");
        candidate.ControlSettings.LoadToleranceEnabled = GetBool(c,L"LoadToleranceEnabled",false);
		candidate.ControlSettings.MaxStop = GetNum(c, L"MaxStop");
		candidate.ControlSettings.FallStop = GetNum(c, L"FallStop");
		candidate.ControlSettings.Filtering = GetNum(c, L"Filtering");
		candidate.ControlSettings.ManualSync = GetNum(c, L"ManualSync");
        candidate.ControlSettings.ArrowColor = GetStr(c,L"ArrowColor");
		candidate.ControlSettings.MaxWeightUsageRange_Percent = GetNum(c, L"MaxWeightUsageRange_Percent", 60.0);
		candidate.ControlSettings.JackArrangementTemplate = NormalizeJackArrangementTemplate(GetNum(c, L"JackArrangementTemplate", 2.0));
		candidate.ControlSettings.DistanceDecelerationStartMm = GetNum(c,L"DistanceDecelerationStartMm",20.0);
        candidate.ControlSettings.DistanceDecelerationMinimumPercent = GetNum(c,L"DistanceDecelerationMinimumPercent",25.0);
        candidate.ControlSettings.DistanceDecelerationToleranceMm = GetNum(c,L"DistanceDecelerationToleranceMm",0.5);
        candidate.ControlSettings.MovementDistanceMm = GetNum(c, L"MovementDistanceMm", 50.0);
        candidate.ControlSettings.GoalDistanceMm = GetNum(c, L"GoalDistanceMm", 10000.0);
		candidate.ControlSettings.PressureTargetBar = GetNum(c, L"PressureTargetBar", 50.0);
        auto& pa=candidate.ControlSettings.PressureAuto;
        pa.toleranceMPa=GetNum(c,L"PressureAutoToleranceMPa",0.05);pa.slowdownMPa=GetNum(c,L"PressureAutoSlowdownMPa",1);
        pa.minimumPercent=GetNum(c,L"PressureAutoMinimumPercent",25);pa.gainPercentPerMPa=GetNum(c,L"PressureAutoGainPercentPerMPa",100);
        pa.balanceToleranceMPa=GetNum(c,L"PressureAutoBalanceToleranceMPa",0);pa.ratePercentPerSecond=GetNum(c,L"PressureAutoRatePercentPerSecond",200);
        const double pressureSettle=GetNum(c,L"PressureAutoSettleMs",500);pa.settleMs=std::isfinite(pressureSettle)&&pressureSettle>=0&&pressureSettle<=60000?static_cast<int>(pressureSettle):-1;
	}

	// JackSettings is parsed identically for model files and the default template.
	if (sys.HasKey(L"JackSettings"))
	{
		auto settings = sys.GetNamedObject(L"JackSettings");
		if (settings.HasKey(L"Specs"))
			candidate.JackSettings.Specs = ReadJackSpecs(settings.GetNamedArray(L"Specs"));
	}

	// ── SoundSettings ──
	if (sys.HasKey(L"SoundSettings"))
	{
		JsonObject s = sys.GetNamedObject(L"SoundSettings");
		candidate.SoundSettings.bUseWarningSound = GetBool(s, L"bUseWarningSound");
        candidate.SoundSettings.CautionSound=GetStr(s,L"CautionSound");
        candidate.SoundSettings.WarningSound=GetStr(s,L"WarningSound");
	}

	// ── SensorSettings ──
	if (sys.HasKey(L"SensorSettings"))
	{
		JsonObject s = sys.GetNamedObject(L"SensorSettings");
		candidate.SensorSettings.PressureUnit = GetStr(s, L"PressureUnit");
		candidate.SensorSettings.WeightUnit = GetStr(s, L"WeightUnit");
		candidate.SensorSettings.LoadGaugePosition = GetStr(s, L"LoadGaugePosition");
		candidate.SensorSettings.DisplacementDisplayDigits = (int)GetNum(s, L"DisplacementDisplayDigits");
		candidate.SensorSettings.LoadMinZero_bar = GetNum(s, L"LoadMinZero_bar");
		candidate.SensorSettings.PressureSensor_ADScale = GetNum(s, L"PressureSensor_ADScale");
		candidate.SensorSettings.DisplacementSensor_ADScale = GetNum(s, L"DisplacementSensor_ADScale");
	}

	// ── PLCSettings ──
		// Older/incomplete models must not inherit commissioning flags from another model.
	candidate.PLCSettings = PLCSettings{};
	candidate.PLCSettings.PressureSigned16 = false;
	candidate.PLCSettings.StrokeSigned16 = false;
	candidate.PLCSettings.PressureBalanceTolerance_bar = 0;
	if (sys.HasKey(L"PLCSettings"))
	{
		JsonObject p = sys.GetNamedObject(L"PLCSettings");
		candidate.PLCSettings.PLC_Model = GetStr(p, L"PLC_Model");
		if ((p.HasKey(L"PLC_Count") && p.GetNamedValue(L"PLC_Count").ValueType() != JsonValueType::Number) ||
            (p.HasKey(L"Sensor_HPU_Count") && p.GetNamedValue(L"Sensor_HPU_Count").ValueType() != JsonValueType::Number)) return false;
		const double plcCount = GetNum(p, L"PLC_Count", 4);
        const double hpuCount = GetNum(p, L"Sensor_HPU_Count", 8);
        if (!std::isfinite(plcCount) || std::floor(plcCount) != plcCount || plcCount < 1 || plcCount > 4 ||
            !std::isfinite(hpuCount) || std::floor(hpuCount) != hpuCount || hpuCount < 1 || hpuCount > 8) return false;
        candidate.PLCSettings.PLC_Count = static_cast<int>(plcCount);
        candidate.PLCSettings.Sensor_HPU_Count = static_cast<int>(hpuCount);
		candidate.PLCSettings.SensorScalingVerified = GetBool(p, L"SensorScalingVerified");
		candidate.PLCSettings.EffectiveAreaVerified = GetBool(p, L"EffectiveAreaVerified");
		candidate.PLCSettings.CoordinatesVerified = GetBool(p, L"CoordinatesVerified");
		candidate.PLCSettings.PressureSigned16 = GetBool(p, L"PressureSigned16");
		candidate.PLCSettings.StrokeSigned16 = GetBool(p, L"StrokeSigned16");
		candidate.PLCSettings.PressureBalanceTolerance_bar = GetNum(p, L"PressureBalanceTolerance_bar");

		if (p.HasKey(L"PLC_IPs"))
		{
			JsonArray ipArr = p.GetNamedArray(L"PLC_IPs");
			candidate.PLCSettings.PLC_IPs.clear();
			for (const auto& ip : ipArr)
			{
				if (ip.ValueType() == JsonValueType::String)
					candidate.PLCSettings.PLC_IPs.push_back(std::wstring(ip.GetString()));
			}
		}
	}

	// Legacy model files do not store the dashboard count; derive it from the
	// actual PLC/sensor topology instead of showing unconfigured jack rows.
	if (!sys.HasKey(L"ControlSettings") ||
		!sys.GetNamedObject(L"ControlSettings").HasKey(L"DashboardJackCount"))
		candidate.ControlSettings.DashboardJackCount = (std::clamp)(candidate.PLCSettings.PLC_Count * candidate.PLCSettings.Sensor_HPU_Count, 1, 16);

	// ── Initialize JackSetupDetails ──
	candidate.InitializeJackDetails();

	// ── JackSetupDetails (overlay onto initialized details) ──
	if (sys.HasKey(L"JackSetupDetails"))
	{
		JsonArray arr = sys.GetNamedArray(L"JackSetupDetails");
		size_t limit = (std::min)((size_t)arr.Size(), candidate.JackSetupDetails.size());
		for (size_t i = 0; i < limit; i++)
		{
			JsonObject d = arr.GetObjectAt((uint32_t)i);
			candidate.JackSetupDetails[i].IPAddress = GetStr(d, L"IPAddress");
			candidate.JackSetupDetails[i].StopLoadPercent = GetNum(d, L"StopLoadPercent", 120);
			candidate.JackSetupDetails[i].JackTitle = GetStr(d, L"JackTitle");
			candidate.JackSetupDetails[i].EngineControlPercent = GetNum(d, L"EngineControlPercent", 100);
			candidate.JackSetupDetails[i].TemperatureAdResolution = static_cast<int>(GetNum(d, L"TemperatureAdResolution", 0));
			candidate.JackSetupDetails[i].Target = GetNum(d, L"Target", 0.0);
			candidate.JackSetupDetails[i].X_mm = GetNum(d, L"X_mm", 0.0);
			candidate.JackSetupDetails[i].Y_mm = GetNum(d, L"Y_mm", 0.0);
			candidate.JackSetupDetails[i].WorkingWeight_ton = GetNum(d, L"WorkingWeight_ton", 61.0);
			candidate.JackSetupDetails[i].MaxWeight_ton = GetNum(d, L"MaxWeight_ton", 102.0);
			candidate.JackSetupDetails[i].JackSelection = GetStr(d, L"JackSelection", L"DSJ-70");
			candidate.JackSetupDetails[i].Port = (int)GetNum(d, L"Port", (double)candidate.JackSetupDetails[i].Port);
		}
	}

	// ── Default specs if empty ──
	if (candidate.JackSettings.Specs.empty())
	{
		candidate.JackSettings.Specs = Util::CreateDefaultSpecs();
	}

    password = GetStr(sys, L"SettingsPassword", m_SettingsPassword);
    }
    catch (...) { return false; }
    m_Config = std::move(candidate);
    m_SettingsPassword = std::move(password);
    ::DHLS_StrandJack::Services::Localization::Refresh();
    AuditLog::Context(winrt::to_string(m_Config.ModelInfo.ModelName),winrt::to_string(m_Config.ModelInfo.CustomerName),winrt::to_string(m_Config.ModelInfo.OperatorName),winrt::to_string(m_Config.ModelInfo.DocumentNumber));
    AuditLog::Write("MODEL_LOADED",winrt::to_string(filePath));
	return true;
}

// ═══════════════════════════════════════════════
// §SECTION 2 — SaveToJson
// ═══════════════════════════════════════════════

bool DHLS_StrandJack::Services::ConfigManager::SaveToJson(const std::wstring& filePath)
{
	return SaveSnapshotToJson(filePath, m_Config, m_SettingsPassword);
}

std::string DHLS_StrandJack::Services::ConfigManager::SerializeSnapshot(
	SystemConfig const& config, std::wstring const& password)
{
	JsonObject root;
	JsonObject sys;

	// ── ModelInfo ──
	JsonObject m;
	m.SetNamedValue(L"ModelName", JsonValue::CreateStringValue(config.ModelInfo.ModelName));
	m.SetNamedValue(L"Language", JsonValue::CreateStringValue(config.ModelInfo.Language));
	m.SetNamedValue(L"ProgramName", JsonValue::CreateStringValue(config.ModelInfo.ProgramName));
	m.SetNamedValue(L"CustomerName", JsonValue::CreateStringValue(config.ModelInfo.CustomerName));
	m.SetNamedValue(L"OperatorName", JsonValue::CreateStringValue(config.ModelInfo.OperatorName));
	m.SetNamedValue(L"DocumentNumber", JsonValue::CreateStringValue(config.ModelInfo.DocumentNumber));
	m.SetNamedValue(L"ModelVersion", JsonValue::CreateStringValue(config.ModelInfo.ModelVersion));
	m.SetNamedValue(L"Description", JsonValue::CreateStringValue(config.ModelInfo.Description));
	m.SetNamedValue(L"CreatedDate", JsonValue::CreateStringValue(config.ModelInfo.CreatedDate));
	m.SetNamedValue(L"LastModifiedDate", JsonValue::CreateStringValue(config.ModelInfo.LastModifiedDate));
	sys.SetNamedValue(L"ModelInfo", m);

	// ── ControlSettings ──
	JsonObject c;
	c.SetNamedValue(L"PressureDisplayDigits", JsonValue::CreateNumberValue(config.ControlSettings.PressureDisplayDigits));
	c.SetNamedValue(L"WeightDisplayDigits", JsonValue::CreateNumberValue(config.ControlSettings.WeightDisplayDigits));
	c.SetNamedValue(L"SpeedControlResolution", JsonValue::CreateNumberValue(config.ControlSettings.SpeedControlResolution));
	c.SetNamedValue(L"PlcAdResolution", JsonValue::CreateNumberValue(config.ControlSettings.PlcAdResolution));
	c.SetNamedValue(L"DashboardJackCount", JsonValue::CreateNumberValue(config.ControlSettings.DashboardJackCount));
	c.SetNamedValue(L"SyncDeviationMm", JsonValue::CreateNumberValue(config.ControlSettings.SyncDeviationMm));
	c.SetNamedValue(L"AllowedDeviationMm", JsonValue::CreateNumberValue(config.ControlSettings.AllowedDeviationMm));
	c.SetNamedValue(L"AutoOverloadPercent", JsonValue::CreateNumberValue(config.ControlSettings.AutoOverloadPercent));
	c.SetNamedValue(L"ManualOverloadPercent", JsonValue::CreateNumberValue(config.ControlSettings.ManualOverloadPercent));
	c.SetNamedValue(L"WedgeExtraPressureSec", JsonValue::CreateNumberValue(config.ControlSettings.WedgeExtraPressureSec));
	c.SetNamedValue(L"OverloadAllowedSec", JsonValue::CreateNumberValue(config.ControlSettings.OverloadAllowedSec));
	c.SetNamedValue(L"PretensionRangeMm", JsonValue::CreateNumberValue(config.ControlSettings.PretensionRangeMm));
	c.SetNamedValue(L"DecelerationSpeedHz", JsonValue::CreateNumberValue(config.ControlSettings.DecelerationSpeedHz));
	c.SetNamedValue(L"DecelerationRangeMm", JsonValue::CreateNumberValue(config.ControlSettings.DecelerationRangeMm));
	c.SetNamedValue(L"RangeToleranceMm", JsonValue::CreateNumberValue(config.ControlSettings.RangeToleranceMm));
	c.SetNamedValue(L"TemperatureMinC", JsonValue::CreateNumberValue(config.ControlSettings.TemperatureMinC));
	c.SetNamedValue(L"TemperatureMaxC", JsonValue::CreateNumberValue(config.ControlSettings.TemperatureMaxC));
	c.SetNamedValue(L"CloseUpperWedgeOnLowering", JsonValue::CreateBooleanValue(config.ControlSettings.CloseUpperWedgeOnLowering));
	c.SetNamedValue(L"SynchronizeTotalLift", JsonValue::CreateBooleanValue(config.ControlSettings.SynchronizeTotalLift));
	c.SetNamedValue(L"AccumulateManualDistance", JsonValue::CreateBooleanValue(config.ControlSettings.AccumulateManualDistance));
	c.SetNamedValue(L"RiseSync_Level1", JsonValue::CreateNumberValue(config.ControlSettings.RiseSync_Level1));
	c.SetNamedValue(L"RiseSync_Level2", JsonValue::CreateNumberValue(config.ControlSettings.RiseSync_Level2));
	c.SetNamedValue(L"FallSync_Level1", JsonValue::CreateNumberValue(config.ControlSettings.FallSync_Level1));
	c.SetNamedValue(L"FallSync_Level2", JsonValue::CreateNumberValue(config.ControlSettings.FallSync_Level2));
	c.SetNamedValue(L"WeightDeviation", JsonValue::CreateNumberValue(config.ControlSettings.WeightDeviation));
    c.SetNamedValue(L"LoadToleranceEnabled",JsonValue::CreateBooleanValue(config.ControlSettings.LoadToleranceEnabled));
	c.SetNamedValue(L"MaxStop", JsonValue::CreateNumberValue(config.ControlSettings.MaxStop));
	c.SetNamedValue(L"FallStop", JsonValue::CreateNumberValue(config.ControlSettings.FallStop));
	c.SetNamedValue(L"Filtering", JsonValue::CreateNumberValue(config.ControlSettings.Filtering));
	c.SetNamedValue(L"ManualSync", JsonValue::CreateNumberValue(config.ControlSettings.ManualSync));
    c.SetNamedValue(L"ArrowColor",JsonValue::CreateStringValue(config.ControlSettings.ArrowColor));
	c.SetNamedValue(L"MaxWeightUsageRange_Percent", JsonValue::CreateNumberValue(config.ControlSettings.MaxWeightUsageRange_Percent));
	c.SetNamedValue(L"JackArrangementTemplate", JsonValue::CreateNumberValue(config.ControlSettings.JackArrangementTemplate));
	c.SetNamedValue(L"DistanceDecelerationStartMm",JsonValue::CreateNumberValue(config.ControlSettings.DistanceDecelerationStartMm));
    c.SetNamedValue(L"DistanceDecelerationMinimumPercent",JsonValue::CreateNumberValue(config.ControlSettings.DistanceDecelerationMinimumPercent));
    c.SetNamedValue(L"DistanceDecelerationToleranceMm",JsonValue::CreateNumberValue(config.ControlSettings.DistanceDecelerationToleranceMm));
    c.SetNamedValue(L"MovementDistanceMm", JsonValue::CreateNumberValue(config.ControlSettings.MovementDistanceMm));
    c.SetNamedValue(L"GoalDistanceMm", JsonValue::CreateNumberValue(config.ControlSettings.GoalDistanceMm));
	c.SetNamedValue(L"PressureTargetBar", JsonValue::CreateNumberValue(config.ControlSettings.PressureTargetBar));
    const auto& pa=config.ControlSettings.PressureAuto;
    c.SetNamedValue(L"PressureAutoToleranceMPa",JsonValue::CreateNumberValue(pa.toleranceMPa));
    c.SetNamedValue(L"PressureAutoSlowdownMPa",JsonValue::CreateNumberValue(pa.slowdownMPa));
    c.SetNamedValue(L"PressureAutoMinimumPercent",JsonValue::CreateNumberValue(pa.minimumPercent));
    c.SetNamedValue(L"PressureAutoGainPercentPerMPa",JsonValue::CreateNumberValue(pa.gainPercentPerMPa));
    c.SetNamedValue(L"PressureAutoBalanceToleranceMPa",JsonValue::CreateNumberValue(pa.balanceToleranceMPa));
    c.SetNamedValue(L"PressureAutoRatePercentPerSecond",JsonValue::CreateNumberValue(pa.ratePercentPerSecond));
    c.SetNamedValue(L"PressureAutoSettleMs",JsonValue::CreateNumberValue(pa.settleMs));
	sys.SetNamedValue(L"ControlSettings", c);

	// ── JackSettings Specs ──
	JsonObject jackSet;
	JsonArray specsArr;
	for (const auto& spec : config.JackSettings.Specs)
	{
		JsonObject sj;
		sj.SetNamedValue(L"TypeName", JsonValue::CreateStringValue(spec.TypeName));
		sj.SetNamedValue(L"Capacity_ton", JsonValue::CreateNumberValue(spec.Capacity_ton));
		sj.SetNamedValue(L"WedgeOpenConfirm_sec", JsonValue::CreateNumberValue(spec.WedgeOpenConfirm_sec));
		sj.SetNamedValue(L"WedgeCloseConfirm_sec", JsonValue::CreateNumberValue(spec.WedgeCloseConfirm_sec));
		sj.SetNamedValue(L"LoadTransferRange_mm", JsonValue::CreateNumberValue(spec.LoadTransferRange_mm));
		sj.SetNamedValue(L"SafeRange_mm", JsonValue::CreateNumberValue(spec.SafeRange_mm));
		sj.SetNamedValue(L"PressureSensor_ADScale", JsonValue::CreateNumberValue(spec.PressureSensor_ADScale));
        sj.SetNamedValue(L"DisplacementSensor_ADScale", JsonValue::CreateNumberValue(spec.DisplacementSensor_ADScale));
        sj.SetNamedValue(L"PressureSensorScale_bar", JsonValue::CreateNumberValue(spec.PressureSensorScale_bar));
		sj.SetNamedValue(L"DisplacementSensorScale_mm", JsonValue::CreateNumberValue(spec.DisplacementSensorScale_mm));
		sj.SetNamedValue(L"CylinderColor", JsonValue::CreateStringValue(spec.CylinderColor));
		sj.SetNamedValue(L"PistonColor", JsonValue::CreateStringValue(spec.PistonColor));
		sj.SetNamedValue(L"ShowMotionArrow", JsonValue::CreateBooleanValue(spec.ShowMotionArrow));
		sj.SetNamedValue(L"WorkingPressure_bar", JsonValue::CreateNumberValue(spec.WorkingPressure_bar));
		sj.SetNamedValue(L"D1_Diameter_mm", JsonValue::CreateNumberValue(spec.D1_Diameter_mm));
		sj.SetNamedValue(L"D2_Diameter_mm", JsonValue::CreateNumberValue(spec.D2_Diameter_mm));
		sj.SetNamedValue(L"D3_Diameter_mm", JsonValue::CreateNumberValue(spec.D3_Diameter_mm));
		sj.SetNamedValue(L"JackStroke_mm", JsonValue::CreateNumberValue(spec.JackStroke_mm));
		sj.SetNamedValue(L"MaxLoadOperatingRange_mm", JsonValue::CreateNumberValue(spec.MaxLoadOperatingRange_mm));
		specsArr.Append(sj);
	}
	jackSet.SetNamedValue(L"Specs", specsArr);
	sys.SetNamedValue(L"JackSettings", jackSet);

	// ── SoundSettings ──
	JsonObject s;
	s.SetNamedValue(L"bUseWarningSound", JsonValue::CreateBooleanValue(config.SoundSettings.bUseWarningSound));
    s.SetNamedValue(L"CautionSound",JsonValue::CreateStringValue(config.SoundSettings.CautionSound));
    s.SetNamedValue(L"WarningSound",JsonValue::CreateStringValue(config.SoundSettings.WarningSound));
	sys.SetNamedValue(L"SoundSettings", s);

	// ── SensorSettings ──
	JsonObject ss;
	ss.SetNamedValue(L"PressureUnit", JsonValue::CreateStringValue(config.SensorSettings.PressureUnit));
	ss.SetNamedValue(L"WeightUnit", JsonValue::CreateStringValue(config.SensorSettings.WeightUnit));
	ss.SetNamedValue(L"LoadGaugePosition", JsonValue::CreateStringValue(config.SensorSettings.LoadGaugePosition));
	ss.SetNamedValue(L"DisplacementDisplayDigits", JsonValue::CreateNumberValue(config.SensorSettings.DisplacementDisplayDigits));
	ss.SetNamedValue(L"LoadMinZero_bar", JsonValue::CreateNumberValue(config.SensorSettings.LoadMinZero_bar));
	ss.SetNamedValue(L"PressureSensor_ADScale", JsonValue::CreateNumberValue(config.SensorSettings.PressureSensor_ADScale));
	ss.SetNamedValue(L"DisplacementSensor_ADScale", JsonValue::CreateNumberValue(config.SensorSettings.DisplacementSensor_ADScale));
	sys.SetNamedValue(L"SensorSettings", ss);

	// ── PLCSettings ──
	JsonObject p;
	p.SetNamedValue(L"PLC_Model", JsonValue::CreateStringValue(config.PLCSettings.PLC_Model));
	p.SetNamedValue(L"PLC_Count", JsonValue::CreateNumberValue(config.PLCSettings.PLC_Count));
	p.SetNamedValue(L"Sensor_HPU_Count", JsonValue::CreateNumberValue(config.PLCSettings.Sensor_HPU_Count));
	JsonArray ipArr;
	for (const auto& ip : config.PLCSettings.PLC_IPs)
		ipArr.Append(JsonValue::CreateStringValue(ip));
	p.SetNamedValue(L"PLC_IPs", ipArr);
	p.SetNamedValue(L"SensorScalingVerified", JsonValue::CreateBooleanValue(config.PLCSettings.SensorScalingVerified));
	p.SetNamedValue(L"EffectiveAreaVerified", JsonValue::CreateBooleanValue(config.PLCSettings.EffectiveAreaVerified));
	p.SetNamedValue(L"CoordinatesVerified", JsonValue::CreateBooleanValue(config.PLCSettings.CoordinatesVerified));
	p.SetNamedValue(L"PressureSigned16", JsonValue::CreateBooleanValue(config.PLCSettings.PressureSigned16));
	p.SetNamedValue(L"StrokeSigned16", JsonValue::CreateBooleanValue(config.PLCSettings.StrokeSigned16));
	p.SetNamedValue(L"PressureBalanceTolerance_bar", JsonValue::CreateNumberValue(config.PLCSettings.PressureBalanceTolerance_bar));
	sys.SetNamedValue(L"PLCSettings", p);

	// ── JackSetupDetails ──
	JsonArray detailsJ;
	for (const auto& detail : config.JackSetupDetails)
	{
		JsonObject d;
		d.SetNamedValue(L"IPAddress", JsonValue::CreateStringValue(detail.IPAddress));
		d.SetNamedValue(L"StopLoadPercent", JsonValue::CreateNumberValue(detail.StopLoadPercent));
		d.SetNamedValue(L"JackTitle", JsonValue::CreateStringValue(detail.JackTitle));
		d.SetNamedValue(L"EngineControlPercent", JsonValue::CreateNumberValue(detail.EngineControlPercent));
		d.SetNamedValue(L"TemperatureAdResolution", JsonValue::CreateNumberValue(detail.TemperatureAdResolution));
		d.SetNamedValue(L"Target", JsonValue::CreateNumberValue(detail.Target));
		d.SetNamedValue(L"X_mm", JsonValue::CreateNumberValue(detail.X_mm));
		d.SetNamedValue(L"Y_mm", JsonValue::CreateNumberValue(detail.Y_mm));
		d.SetNamedValue(L"WorkingWeight_ton", JsonValue::CreateNumberValue(detail.WorkingWeight_ton));
		d.SetNamedValue(L"MaxWeight_ton", JsonValue::CreateNumberValue(detail.MaxWeight_ton));
		d.SetNamedValue(L"JackSelection", JsonValue::CreateStringValue(detail.JackSelection));
		d.SetNamedValue(L"Port", JsonValue::CreateNumberValue(detail.Port));
		detailsJ.Append(d);
	}
	sys.SetNamedValue(L"JackSetupDetails", detailsJ);

	// 설정 암호를 JSON에 저장합니다.
	sys.SetNamedValue(L"SettingsPassword", JsonValue::CreateStringValue(password));

	root.SetNamedValue(L"SystemConfig", sys);

    return PrettyPrintJson(ToUtf8(root.Stringify()));
}

bool DHLS_StrandJack::Services::ConfigManager::SaveSnapshotToJson(
    std::wstring const& filePath, SystemConfig const& config, std::wstring const& password)
{
    if (!IsValidModelName(config.ModelInfo.ModelName)) return false;
    bool saved = false;
    std::filesystem::path temporary;
    try
    {
        const auto data = SerializeSnapshot(config, password);
        const auto destination = std::filesystem::absolute(std::filesystem::path(filePath));
        wchar_t name[MAX_PATH]{};
        if (::GetTempFileNameW(destination.parent_path().c_str(), L"cfg", 0, name))
        {
            temporary = name;
            std::ofstream file(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
            if (file.is_open())
            {
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                file.flush();
                const bool written = file.good();
                file.close();
                if (written && !file.fail())
                    saved = !!::MoveFileExW(temporary.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
        }
    }
    catch (...) { saved = false; }
    if (!temporary.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    AuditLog::Context(winrt::to_string(config.ModelInfo.ModelName),winrt::to_string(config.ModelInfo.CustomerName),winrt::to_string(config.ModelInfo.OperatorName),winrt::to_string(config.ModelInfo.DocumentNumber));
    AuditLog::Write(saved?"SAVE":"SAVE_FAILED",winrt::to_string(filePath));
    return saved;
}

bool DHLS_StrandJack::Services::ConfigManager::IsValidModelName(std::wstring const& name)
{
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
        return false;
    if (name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) return false;
    for (wchar_t ch : name) if (ch < 32) return false;
    const auto stem = name.substr(0, name.find(L'.'));
    auto equal = [&](wchar_t const* value) { return ::CompareStringOrdinal(stem.c_str(), -1, value, -1, TRUE) == CSTR_EQUAL; };
    if (equal(L"default") || equal(L"CON") || equal(L"PRN") || equal(L"AUX") || equal(L"NUL")) return false;
    if (stem.size() == 4 && (stem[3] >= L'1' && stem[3] <= L'9'))
    {
        const auto prefix = stem.substr(0, 3);
        if (::CompareStringOrdinal(prefix.c_str(), -1, L"COM", -1, TRUE) == CSTR_EQUAL ||
            ::CompareStringOrdinal(prefix.c_str(), -1, L"LPT", -1, TRUE) == CSTR_EQUAL) return false;
    }
    return true;
}

// ═══════════════════════════════════════════════
// §SECTION 3 — Model File Management
// ═══════════════════════════════════════════════

std::filesystem::path DHLS_StrandJack::Services::ConfigManager::GetModelsDirectory()
{
	// Resolve from the process executable, never from the working directory.
	std::wstring executablePath(MAX_PATH, L'\0');
	for (;;)
	{
		const auto length = ::GetModuleFileNameW(nullptr, executablePath.data(),
			static_cast<DWORD>(executablePath.size()));
		if (length == 0)
			winrt::throw_last_error();
		if (length < executablePath.size())
		{
			executablePath.resize(length);
			return std::filesystem::path(executablePath).parent_path().parent_path() / L"Models";
		}
		executablePath.resize(executablePath.size() * 2);
	}
}

bool DHLS_StrandJack::Services::ConfigManager::LoadDefaultJackSettings(JackSettings& settings)
{
	// Read into a temporary value; never overwrite the active model while parsing.
	try
	{
		std::ifstream file(GetModelsDirectory() / L"Default.json", std::ios::binary);
		if (!file.is_open()) return false;
		std::ostringstream contents;
		contents << file.rdbuf();
		auto json = contents.str();
		if (json.compare(0, 3, "\xEF\xBB\xBF") == 0) json.erase(0, 3);
		JsonObject root{ nullptr };
		if (!JsonObject::TryParse(winrt::to_hstring(json), root)) return false;
		auto sys = root.GetNamedObject(L"SystemConfig");
		auto values = sys.GetNamedObject(L"JackSettings").GetNamedArray(L"Specs");
		auto specs = ReadJackSpecs(values);
		if (specs.empty() || specs.size() != values.Size()) return false;
		settings.Specs = std::move(specs);
		return true;
	}
	catch (...) { return false; }
}

std::vector<std::wstring> DHLS_StrandJack::Services::ConfigManager::GetModelFileList(const std::wstring& modelsDir)
{
	std::vector<std::wstring> result;
	try
	{
		if (!std::filesystem::exists(modelsDir))
			return result;
		for (const auto& entry : std::filesystem::directory_iterator(modelsDir))
		{
			if (entry.is_regular_file() && entry.path().extension() == L".json" &&
				::CompareStringOrdinal(entry.path().filename().c_str(), -1, L"default.json", -1, TRUE) != CSTR_EQUAL)
				result.push_back(entry.path().filename().wstring());
		}
	}
	catch (...) {}
	return result;
}

bool DHLS_StrandJack::Services::ConfigManager::DeleteModelFile(const std::wstring& filePath)
{
	try
	{
		const bool removed=std::filesystem::remove(filePath);
        AuditLog::Write(removed?"MODEL_DELETE":"MODEL_DELETE_FAILED",winrt::to_string(filePath));
        return removed;
	}
	catch (...) { return false; }
}

bool DHLS_StrandJack::Services::ConfigManager::CreateNewModel(const std::wstring& modelDir, const std::wstring& modelName)
{
	if (!IsValidModelName(modelName)) return false;
	JackSettings defaultJackSettings;
	if (!LoadDefaultJackSettings(defaultJackSettings)) return false;
	// Reset all config to defaults
	m_Config.ModelInfo = ModelInfo{};
	m_Config.ControlSettings = ControlSettings{};
	m_Config.SoundSettings = SoundSettings{};
	m_Config.SensorSettings = SensorSettings{};

	// Initialize default jack specs
	m_Config.JackSettings = std::move(defaultJackSettings);

	// Reset PLC settings
	m_Config.PLCSettings.PLC_Model = L"";
	m_Config.PLCSettings.PLC_Count = 4;
	m_Config.PLCSettings.Sensor_HPU_Count = 8;
	m_Config.PLCSettings.PLC_IPs.clear();
    for(int i=0;i<m_Config.PLCSettings.PLC_Count;++i)m_Config.PLCSettings.PLC_IPs.push_back(DefaultPlcIp(i));
	m_Config.PLCSettings.SensorScalingVerified = false;
	m_Config.PLCSettings.EffectiveAreaVerified = false;
	m_Config.PLCSettings.CoordinatesVerified = false;
	m_Config.PLCSettings.PressureSigned16 = false;
	m_Config.PLCSettings.StrokeSigned16 = false;
	m_Config.PLCSettings.PressureBalanceTolerance_bar = 0;
	m_Config.JackSetupDetails.clear();

	// Set model name
	m_Config.ModelInfo.ModelName = modelName;
	m_Config.InitializeJackDetails();

	// Ensure Models directory exists
	std::error_code ec;
	std::filesystem::create_directories(modelDir, ec);

	// Save to file
	std::wstring filePath = modelDir + L"\\" + modelName + L".json";
	return SaveToJson(filePath);
}

void DHLS_StrandJack::Services::ConfigManager::EnsureModelsDirectory(const std::wstring& modelsDir)
{
	std::error_code ec;
	std::filesystem::create_directories(modelsDir, ec);
}
