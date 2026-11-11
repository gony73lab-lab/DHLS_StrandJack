// ⠄ Data Models — WinUI 3 Migration Architecture Analysis ⠄
//
// ═════════════════════════════════════════════════════════════════
// MFC → WinUI 3 Architectural Paradigm Shift Summary
// ═════════════════════════════════════════════════════════════════
//
// [MFC Original Architecture]
//   • ConfigManager.h — struct FModelInfo, FJackSpec, FJackSettings,
//     FControlSettings, FSoundSettings, FSensorSettings, FPLCSettings,
//     FJackSetupDetail, FSystemConfig  (all legacy-string/CArray/CBOOL)
//   • ConfigManager.cpp — JSON serialize/deserialize via nlohmann::json
//   • Util.h/cpp — static math: area, pressure, force calculations
//
// [WinUI 3 Target Architecture]
//   Phase 1: Data structs → Plain C++ structs with std::wstring/std::vector
//   Phase 2: ConfigService → IDL-exposed WinRT component (I/O + business logic)
//   Phase 3: ViewModels → INotifyPropertyChanged via winrt::implements
//   Phase 4: XAML Views → {x:Bind} TwoWay data binding
//
// ───────────────────────────────────────────────────────────────────
//
// This file replaces:
//   ConfigManager.h  (all struct definitions)
//   Util.h           (math constants & function signatures)
//
#pragma once

#include "ControlLogic/PressureAutoControl.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace DHLS_StrandJack::Models
{
	inline constexpr int StackedTwoLineTemplate = 5;

	inline int NormalizeJackArrangementTemplate(double value) noexcept
	{
		if (value == StackedTwoLineTemplate) return StackedTwoLineTemplate;
		return std::isfinite(value) && value < 2.0 ? 1 : 2;
	}
	// ═══════════════════════════════════════════════
	// §SECTION 1 — Domain Data Models
	// ═══════════════════════════════════════════════

	// Replaces: FModelInfo (legacy-string → std::wstring)
	struct ModelInfo
	{
		std::wstring Language = L"ko";
		std::wstring ModelName;
		std::wstring ModelVersion;
		std::wstring Description;
		std::wstring DocumentNumber;
		std::wstring ProgramName;
		std::wstring CustomerName;
		std::wstring OperatorName;
		std::wstring CreatedDate;
		std::wstring LastModifiedDate;
	};

	// Replaces: FJackSpec (legacy-string → std::wstring)
	struct JackSpec
	{
		std::wstring TypeName;
		double Capacity_ton = 0;
		double WedgeOpenConfirm_sec = 0;
		double WedgeCloseConfirm_sec = 0;
		double LoadTransferRange_mm = 0;
		double SafeRange_mm = 0;
		double PressureSensor_ADScale = 0; // 0 inherits the model AD scale for legacy files.
        double DisplacementSensor_ADScale = 0;
        double PressureSensorScale_bar = 0;
		double DisplacementSensorScale_mm = 0;
		std::wstring CylinderColor;
		std::wstring PistonColor;
        bool ShowMotionArrow = false;
		double WorkingPressure_bar = 0;
		double D1_Diameter_mm = 0;
		double D2_Diameter_mm = 0;
		double D3_Diameter_mm = 0;
		double JackStroke_mm = 0;
		double MaxLoadOperatingRange_mm = 0;
	};

	// Replaces: FJackSettings (CArray → std::vector)
	struct JackSettings
	{
		std::vector<JackSpec> Specs;
	};

	// Replaces: FControlSettings
	struct ControlSettings
	{
		// Control settings dashboard fields (the legacy controls below remain for compatibility).
		int PressureDisplayDigits = 0;
		int WeightDisplayDigits = 1;
		int SpeedControlResolution = 6000;
		int PlcAdResolution = 4000;
		int DashboardJackCount = 16;
		double SyncDeviationMm = 10;
		double AllowedDeviationMm = 20;
		double AutoOverloadPercent = 120;
		double ManualOverloadPercent = 140;
		double WedgeExtraPressureSec = 2;
		double OverloadAllowedSec = 2;
		double PretensionRangeMm = 6;
		bool CloseUpperWedgeOnLowering = false;
		bool SynchronizeTotalLift = false;
		bool AccumulateManualDistance = true;
		double DecelerationSpeedHz = 30;
		double DecelerationRangeMm = 20;
		double RangeToleranceMm = 2;
		double TemperatureMinC = 0;
		double TemperatureMaxC = 0;
		double RiseSync_Level1 = 0;
		double RiseSync_Level2 = 0;
		double FallSync_Level1 = 0;
		double FallSync_Level2 = 0;
		double WeightDeviation = 0;
        bool LoadToleranceEnabled = false;
		double MaxStop = 0;
		double FallStop = 0;
		double Filtering = 0;
		double ManualSync = 0;
		std::wstring ArrowColor;
		double MaxWeightUsageRange_Percent = 60.0;
		int JackArrangementTemplate = 2;
        // Distance-proportional deceleration editor/preview settings (mm, %, mm).
        double DistanceDecelerationStartMm = 20.0;
        double DistanceDecelerationMinimumPercent = 25.0;
        double DistanceDecelerationToleranceMm = 0.5;
        double MovementDistanceMm = 50.0;
        double GoalDistanceMm = 10000.0;
        double PressureTargetBar = 50.0;
        ::DHLS_StrandJack::Control::PressureAutoSettings PressureAuto;
	};

	// Replaces: FSoundSettings
	struct SoundSettings
	{
		bool bUseWarningSound = false;
		std::wstring CautionSound;
		std::wstring WarningSound;
	};

	// Replaces: FSensorSettings
	struct SensorSettings
	{
		std::wstring PressureUnit;
		std::wstring WeightUnit;
		std::wstring LoadGaugePosition;
		int DisplacementDisplayDigits = 0;
		double LoadMinZero_bar = 0;
		double PressureSensor_ADScale = 0;
		double DisplacementSensor_ADScale = 0;
	};

	// Replaces: FPLCSettings (CStringArray → std::vector<std::wstring>)
	struct PLCSettings
	{
		std::wstring PLC_Model;
		int PLC_Count = 4;
		int Sensor_HPU_Count = 8;
		std::vector<std::wstring> PLC_IPs;
        // Commissioned input interpretation only. This integration has no PLC writes.
        bool SensorScalingVerified = false;
        bool EffectiveAreaVerified = false;
        bool CoordinatesVerified = false;
        bool PressureSigned16 = false;
        bool StrokeSigned16 = false;
        double PressureBalanceTolerance_bar = 0;
        void InvalidateEquipmentVerification()
        {
            SensorScalingVerified = false;
            EffectiveAreaVerified = false;
            CoordinatesVerified = false;
        }

	};

	// Replaces: FJackSetupDetail
	struct JackSetupDetail
	{
		std::wstring IPAddress; // Per-jack address shown in General Setting.
		double StopLoadPercent = 120;
		std::wstring JackTitle;
		double EngineControlPercent = 100;
		int TemperatureAdResolution = 0;
		double MaxWeightUsageRange_Percent = 80;
		int No = 0;
		int JackNumber = 0;
		int Port = 0;
		double X_mm = 0;
		double Y_mm = 0;
		double Target = 0;
		double WorkingWeight_ton = 0;
		double MaxWeight_ton = 0;
		std::wstring JackSelection;
	};

	// ═══════════════════════════════════════════════
	// §SECTION 2 — Aggregate Root: SystemConfig
	// ═══════════════════════════════════════════════

	// Replaces: FSystemConfig
	struct SystemConfig
	{
		ModelInfo ModelInfo;
		JackSettings JackSettings;
		ControlSettings ControlSettings;
		SoundSettings SoundSettings;
		SensorSettings SensorSettings;
		PLCSettings PLCSettings;
		std::vector<JackSetupDetail> JackSetupDetails;

		// Replaces: FSystemConfig::InitializeJackDetails()

		void InitializeJackDetails()
		{
			int TotalJackCount = (std::max)(PLCSettings.PLC_Count * PLCSettings.Sensor_HPU_Count, ControlSettings.DashboardJackCount);
			JackSetupDetails.clear();
			JackSetupDetails.resize(TotalJackCount);
			for (int i = 0; i < TotalJackCount; ++i)
			{
				JackSetupDetails[i].No = i + 1;
				JackSetupDetails[i].JackNumber = i + 1;
				JackSetupDetails[i].Port = (i % PLCSettings.Sensor_HPU_Count) + 1;
			}
		}
	};

	// ═══════════════════════════════════════════════
	// §SECTION 3 — Utility: Hydraulic Force Calculations
	// ═══════════════════════════════════════════════
	// Replaces: CUtil (Util.h / Util.cpp)
	// Migrated from static MFC class to namespace functions

	namespace Util
	{
		// Constants from CUtil
		inline constexpr double BAR_TO_KGF_CM2 = 1.01971621297793;
		inline constexpr double PI = 3.14159265358979323846;

		// Replaces: CUtil::CalcAdvanceArea_cm2
		inline double CalcAdvanceArea_cm2(double diameter_mm)
		{
			double r_cm = (diameter_mm / 2.0) / 10.0;
			return PI * r_cm * r_cm;
		}

		// Replaces: CUtil::CalcRetractArea_cm2
		inline double CalcRetractArea_cm2(double inner_mm, double outer_mm)
		{
			double r1_cm = (inner_mm / 2.0) / 10.0;
			double r2_cm = (outer_mm / 2.0) / 10.0;
			double area = PI * (r2_cm * r2_cm - r1_cm * r1_cm);
			return (area > 0) ? area : 0.0;
		}

		// Replaces: CUtil::CalcAdvancePressure_bar
		inline double CalcAdvancePressure_bar(double diameter_mm, double targetForce_ton)
		{
			double area_cm2 = CalcAdvanceArea_cm2(diameter_mm);
			if (area_cm2 <= 0) return 0.0;
			double force_kgf = targetForce_ton * 1000.0;
			double pressure_kgf_cm2 = force_kgf / area_cm2;
			return pressure_kgf_cm2 / BAR_TO_KGF_CM2;
		}

		// Replaces: CUtil::CalcRetractPressure_bar
		inline double CalcRetractPressure_bar(double inner_mm, double outer_mm, double targetForce_ton)
		{
			double area_cm2 = CalcRetractArea_cm2(inner_mm, outer_mm);
			if (area_cm2 <= 0) return 0.0;
			double force_kgf = targetForce_ton * 1000.0;
			double pressure_kgf_cm2 = force_kgf / area_cm2;
			return pressure_kgf_cm2 / BAR_TO_KGF_CM2;
		}

		// Replaces: CUtil::CalcAdvanceForce_ton
		inline double CalcAdvanceForce_ton(double diameter_mm, double workingPressure_bar)
		{
			double area_cm2 = CalcAdvanceArea_cm2(diameter_mm);
			double pressure_kgf_cm2 = workingPressure_bar * BAR_TO_KGF_CM2;
			double force_kgf = pressure_kgf_cm2 * area_cm2;
			return force_kgf / 1000.0;
		}

		// Replaces: CUtil::CalcRetractForce_ton
		inline double CalcRetractForce_ton(double inner_mm, double outer_mm, double workingPressure_bar)
		{
			double area_cm2 = CalcRetractArea_cm2(inner_mm, outer_mm);
			double pressure_kgf_cm2 = workingPressure_bar * BAR_TO_KGF_CM2;
			double force_kgf = pressure_kgf_cm2 * area_cm2;
			return force_kgf / 1000.0;
		}

		// ── Helpers extracted from ControlSettingsDlg::CalculateMaxWeightForJack ──
		// Given a jack capacity string like "10 ton" and a list of specs,
		// calculates the maximum advance force based on D3 diameter.
		inline double CalculateMaxWeightForJack(
			const std::wstring& jackSel,
			const std::vector<JackSpec>& specs)
		{
			for (const auto& spec : specs)
			{
				if (!spec.TypeName.empty() && spec.TypeName == jackSel)
					return CalcAdvanceForce_ton(spec.D3_Diameter_mm, spec.WorkingPressure_bar);
			}
			// Extract numeric part from "10 ton" style string
			double targetTon = 0;
			try { targetTon = std::stod(jackSel); }
			catch (...) { return 0.0; }

			for (const auto& spec : specs)
			{
				if (std::abs(spec.Capacity_ton - targetTon) < 0.1)
				{
					double d3 = spec.D3_Diameter_mm;
					double p = spec.WorkingPressure_bar;

					// Fallback for uninitialized values
					if (p <= 0.0) p = 700.0;
					if (d3 <= 0.0)
					{
						if (std::abs(targetTon - 10.0) < 0.1) d3 = 136.2;
						else d3 = std::sqrt(targetTon * 185.3);
					}

					return CalcAdvanceForce_ton(d3, p);
				}
			}

			// Complete fallback if spec not found
			double d3 = (std::abs(targetTon - 10.0) < 0.1) ? 136.2 : std::sqrt(targetTon * 185.3);
			return CalcAdvanceForce_ton(d3, 700.0);
		}

		// ── Default jack specs initialization ──
		// Replaces: NewModelDlg.cpp lines 25-34 and ConfigManager.cpp lines 28-38
		inline std::vector<JackSpec> CreateDefaultSpecs()
		{
			JackSpec dsj70;
			dsj70.TypeName = L"DSJ-70";
			dsj70.Capacity_ton = 70;
			dsj70.WedgeOpenConfirm_sec = dsj70.WedgeCloseConfirm_sec = 30;
			dsj70.LoadTransferRange_mm = 25;
			dsj70.SafeRange_mm = 30;
			dsj70.PressureSensorScale_bar = 400;
			dsj70.DisplacementSensorScale_mm = 500;
			dsj70.CylinderColor = L"#FFFFFF00";
			dsj70.PistonColor = L"#FF55C5F4";
			dsj70.WorkingPressure_bar = 400;
			dsj70.D1_Diameter_mm = 140;
			dsj70.D2_Diameter_mm = 190;
			dsj70.D3_Diameter_mm = 250;
			dsj70.JackStroke_mm = 480;
			JackSpec dsj300s = dsj70;
			dsj300s.TypeName = L"DSJ-300S";
			dsj300s.Capacity_ton = 300;
			dsj300s.DisplacementSensorScale_mm = 450;
			dsj300s.CylinderColor = L"#FF000080";
			dsj300s.D1_Diameter_mm = 400;
			dsj300s.D2_Diameter_mm = 260;
			dsj300s.D3_Diameter_mm = 480;
			dsj300s.JackStroke_mm = 445;
			return {dsj70, dsj300s};
		}
	} // namespace Util
} // namespace DHLS_StrandJack::Models
