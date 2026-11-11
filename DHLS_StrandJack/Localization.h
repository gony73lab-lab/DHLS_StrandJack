#pragma once
#include "ConfigManager.h"
#include "AuditLog.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string_view>
#include <vector>

namespace DHLS_StrandJack::Services
{
	// UI-thread-only localization. Only explicitly registered display properties
	// are changed; editable text, numbers, model names and other data are untouched.
	class Localization
	{
		using DependencyObject = winrt::Microsoft::UI::Xaml::DependencyObject;
		using DependencyProperty = winrt::Microsoft::UI::Xaml::DependencyProperty;
		using FrameworkElement = winrt::Microsoft::UI::Xaml::FrameworkElement;
		using ComboBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox;
		struct Translation { std::wstring_view korean; std::wstring_view english; };
		struct Entry
		{
			winrt::weak_ref<DependencyObject> target;
			DependencyProperty property;
			size_t translation;
			std::wstring suffix;
		};
		static auto const& Catalog()
		{
			static const Translation catalog[] = {
                { L"시작 조건 확인", L"Checking start conditions" },
                { L"다음 회차 시작 위치로 이동", L"Reposition for next stroke" },
                { L"시작 거부: 변경한 동작거리가 가동 범위를 초과합니다", L"START rejected: updated stroke exceeds available range" },
                { L"양 클램프 닫힘", L"Both clamps closed" },
                { L"리프팅", L"Lifting" },
                { L"상부 Anchor 확인 (모의)", L"Upper Anchor check (mock)" },
                { L"안전한 하중 지지 검증 (모의)", L"Safe load support verified (mock)" },
                { L"상부 열림 / 하부 닫힘", L"Upper OPEN / Lower CLOSED" },
                { L"실린더 복귀", L"Retracting" },
                { L"상부 클램프 재체결", L"Upper clamp re-engaged" },
                { L"스트랜드잭 준비 요청", L"PrepareStrandJob" },
                { L"모의 장비 준비 완료", L"Mock READY confirmed" },
                { L"시작 거부: 선택 잭과 거리 설정 후 준비를 실행하세요", L"START rejected: run READY for the selected jacks and distances" },
                { L"모의 운전 완료", L"Mock job completed" },
                { L"시각", L"Time" },
                { L"메시지", L"Message" },
                { L"현재 시각", L"Local time" },
                { L"운전 시간", L"Run time" },
                { L"운전 시간 결과", L"Run time result" },
                { L"모의 장비 연결", L"Virtual equipment connected" },
                { L"모의 장비 연결 해제", L"Virtual equipment disconnected" },
                { L"개발자 인증 실패", L"Developer authentication failed" },
                { L"상승 방향 선택", L"UpRequested" },
                { L"하강 방향 선택", L"DownRequested" },
                { L"이동 목표 변경", L"TargetChanged" },
                { L"압력 영점 요청", L"PressureZero" },
                { L"누적거리 영점 요청", L"HeightZero" },
                { L"하중 기준 설정 요청", L"LoadSet" },
                { L"전체 정지 요청", L"Full stop requested" },
                { L"시작 거부: 감속 설정 확인", L"START rejected: invalid distance deceleration settings" },
                { L"영점/하중 설정 거부: 잠금 해제 또는 초기화 필요", L"Setup rejected: unlock or RESET first" },
                { L"영점/하중 설정 거부: 유효한 센서를 선택하고 기준하중은 양수로 설정하세요", L"Setup rejected: select fresh valid sensors; LOAD SET requires positive load" },
                { L"압력 영점: 음수 압력은 0으로, 양수 압력은 유지", L"PRESSURE ZERO: negative P.T clamped to zero; positive values unchanged" },
                { L"선택 포트의 현재 하중을 기준으로 저장", L"LOAD SET: measured port references saved" },
                { L"누적거리 표시 기준 초기화", L"HEIGHT ZERO: display reference only" },
                { L"현재 변위", L"Stroke" },
                { L"현재 압력", L"Pressure" },
                { L"현재 하중", L"Load" },
                { L"누적 거리", L"Travel" },
                { L"유압유", L"Oil T" },
                { L"펌프 --", L"PUMP --" },
                { L"펌프 켜짐", L"PUMP ON" },
                { L"펌프 꺼짐", L"PUMP OFF" },
                { L"상부 웨지 열림", L"Upper wedge opened" },
                { L"상부 웨지 닫힘", L"Upper wedge closed" },
                { L"하부 웨지 열림", L"Lower wedge opened" },
                { L"하부 웨지 닫힘", L"Lower wedge closed" },
                { L"경고", L"Warning" },
                { L"펌프 켜짐 확인", L"Pump ON confirmed" },
                { L"펌프 꺼짐 확인", L"Pump OFF confirmed" },
                { L"장비 세션이 없습니다", L"No equipment session" },
                { L"운전 중에는 속도를 변경할 수 없습니다.", L"Speed cannot be changed during operation." },
                { L"시작 명령", L"StartRequested" },
                { L"정지 명령", L"StopRequested" },
                { L"완전 정지 명령", L"FullStopRequested" },
                { L"비상정지 명령", L"EmergencyRequested" },
                { L"시퀀스 초기화 명령", L"ResetSequence" },
                { L"Model 명", L"Model name" },
                { L"PLC A/D 분해능", L"PLC A/D resolution" },
                { L"SIEMENS (미사용)", L"SIEMENS (unavailable)" },
                { L"감속 범위/속도", L"Deceleration distance/speed" },
                { L"누적거리 적산(수동)", L"Accumulate travel (manual)" },
                { L"닫고 내림", L"Lower with wedge closed" },
                { L"동조", L"Sync" },
                { L"범위", L"Range" },
                { L"범위 단위", L"Distance unit" },
                { L"범위 허용 변수", L"Travel tolerance" },
                { L"사용하중", L"Working load" },
                { L"상부웨지 물림 시점", L"Upper wedge engagement" },
                { L"속도", L"Speed" },
                { L"속도 제어 분해능", L"Speed control resolution" },
                { L"수동", L"Manual" },
                { L"스트랜드잭 동조", L"Strand jack synchronization" },
                { L"압력 단위", L"Pressure unit" },
                { L"압력 소수점", L"Pressure decimals" },
                { L"엔진컨트롤(%)", L"Engine control (%)" },
                { L"열고 내림", L"Lower with wedge open" },
                { L"온도 A/D 분해능", L"Temperature A/D resolution" },
                { L"온도 센서 범위", L"Temperature sensor range" },
                { L"웨지작동 추가 가압시간", L"Additional wedge pressurization time" },
                { L"자동", L"Automatic" },
                { L"잭 수량", L"Jack count" },
                { L"잭 타이틀", L"Jack title" },
                { L"잭 타입", L"Jack type" },
                { L"정지하중(%)", L"Stop load (%)" },
                { L"초과하중 허용시간", L"Overload allowance time" },
                { L"최대", L"Max" },
                { L"최대 초과 하중", L"Maximum overload" },
                { L"최소", L"Min" },
                { L"편차 (동조/허용)", L"Deviation (sync/limit)" },
                { L"프리텐셔닝 범위", L"Pretensioning distance" },
                { L"하중 단위", L"Load unit" },
                { L"하중 소수점", L"Load decimals" },
                { L"허용", L"Limit" },
                { L"1라인 배치", L"Single row" },
                { L"1라인 배치 (8포트)", L"Single row (8 ports)" },
                { L"2라인 배치", L"Two rows" },
                { L"2라인 배치 (위·아래 각 8포트)", L"Two rows (8 ports each)" },
                { L"2라인 병렬", L"Two parallel banks" },
                { L"2라인 병렬 배치 (16포트)", L"Two parallel banks (16 ports)" },
                { L"3라인 배치", L"Three rows" },
                { L"4라인 배치", L"Four rows" },
                { L"변위센서", L"Stroke sensor" },
                { L"상승 단면적", L"Lifting area" },
                { L"스트랜드잭 설정", L"Strand jack settings" },
                { L"실린더 변위", L"Cylinder stroke" },
                { L"실린더 색상", L"Cylinder color" },
                { L"안전 범위", L"Safety margin" },
                { L"압력센서", L"Pressure sensor" },
                { L"웨지닫힘 확인시간", L"Wedge close timeout" },
                { L"웨지열림 확인시간", L"Wedge open timeout" },
                { L"잭 최대하중", L"Jack maximum load" },
                { L"초과", L"Over limit" },
                { L"최대 하중 가동 범위", L"Maximum load operating range" },
                { L"최대 하중 가동 범위 (mm)", L"Maximum load operating range (mm)" },
                { L"하강 단면적", L"Lowering area" },
                { L"하중전이 범위", L"Load transfer range" },
                { L"% 사용하중", L"% working load" },
                { L"PLC 고장 — 정지", L"PLC fault — stopped" },
                { L"교정 절차 미연결", L"Calibration procedure unavailable" },
                { L"누적거리 영점", L"Zero cumulative travel" },
                { L"누적거리영점", L"Zero cumulative travel" },
                { L"닫힘", L"Closed" },
                { L"동작거리", L"Travel distance" },
                { L"동작거리 (mm)", L"Travel distance (mm)" },
                { L"동작거리 선택", L"Select travel distance" },
                { L"목표거리", L"Target distance" },
                { L"목표거리 (mm)", L"Target distance (mm)" },
                { L"사용하중 설정", L"Set working load" },
                { L"상 부", L"Upper" },
                { L"상승", L"Lift" },
                { L"상승 요청", L"Lift request" },
                { L"상승 운전", L"Lifting operation" },
                { L"상승속도", L"Lifting speed" },
                { L"상승속도 (Hz)", L"Lifting speed (Hz)" },
                { L"속도 조절 SET", L"Speed control set" },
                { L"시뮬레이션", L"Simulation" },
                { L"시뮬레이션 표시 전용", L"Simulation display only" },
                { L"압력 0점", L"Zero pressure" },
                { L"압력 자동 진단", L"Automatic pressure diagnostics" },
                { L"열림", L"Open" },
                { L"일시정지", L"Pause" },
                { L"정지 명령을 요청합니다", L"Request stop" },
                { L"준비", L"Ready" },
                { L"캡처", L"Capture" },
                { L"클램프", L"Clamp" },
                { L"텐셔닝 제어 미연결", L"Tensioning control unavailable" },
                { L"텐션거리", L"Tension distance" },
                { L"텐션거리 선택", L"Select tension distance" },
                { L"텐션하중", L"Tension load" },
                { L"하 부", L"Lower" },
                { L"하강", L"Lower" },
                { L"하강 요청", L"Lower request" },
                { L"하강속도", L"Lowering speed" },
                { L"하강속도 (Hz)", L"Lowering speed (Hz)" },
                { L"현재변위영점", L"Zero current stroke" },
                { L"모델 관리 닫기", L"Close model manager" },
                { L"전체 하중", L"Total load" },
                { L"리프팅", L"Lifting" },
                { L"하강 운전", L"Lowering operation" },
                { L"수동 운전", L"Manual operation" },
                { L"텐셔닝 작업", L"Pretensioning" },
                { L"시뮬레이션 활성화", L"Simulation enabled" },
                { L"시뮬레이션 해제", L"Simulation disabled" },

{ L"압력 자동 설정", L"Automatic pressure settings" },
{ L"준비 완료", L"Ready" },
{ L"압력 감속 중", L"Decelerating" },
{ L"목표 안정화 확인", L"TargetPending" },
{ L"정지 요청", L"StopRequested" },
{ L"정지 확인 완료", L"StoppedConfirmed" },
{ L"완료", L"Completed" },
{ L"보류", L"Hold" },
{ L"고장", L"Fault" },
{ L"통신 상실", L"CommunicationLost" },
{ L"센서 무효", L"SensorInvalid" },
{ L"압력 상한 초과", L"PressureLimitExceeded" },
{ L"동조 한계 초과", L"SyncLimitExceeded" },
{ L"출력 차단", L"OutputInhibited" },
{ L"시작 요청", L"StartRequested" },
{ L"설정 확인", L"Configuring" },
{ L"압력 설정값이 올바르지 않습니다.", L"Invalid pressure configuration" },
{ L"포트별 목표가 올바르지 않습니다.", L"Invalid cylinder target" },
{ L"준비 완료 · 시작 조작 필요", L"Ready; explicit START required" },
{ L"모니터 전용 · 압력 자동 PLC 출력 미승인", L"Monitor Only: pressure automatic PLC output not commissioned" },
{ L"압력 자동 시작 요청 확인", L"Explicit pressure START accepted" },
{ L"센서 미수집/지연 · 자동 재시작 금지", L"Missing/stale pressure feedback; no automatic restart" },
{ L"운전 허가 또는 비상정지 인터록 확인", L"Plant permission or emergency interlock" },
{ L"선택 포트 번호 중복 또는 무효", L"Invalid/duplicate selected port" },
{ L"유효한 압력 포트를 선택하세요.", L"Select configured pressure ports" },
{ L"압력 운전 중 선택 변경 거부", L"Selection changed during pressure cycle" },
{ L"센서·스케일·잭 한계 설정 무효", L"Invalid sensor/scaling/hydraulic limits" },
{ L"목표 압력이 잭/하중 운전 한계를 초과합니다.", L"Pressure target exceeds cylinder/load operating limit" },
{ L"최대 압력/하중 한계 초과", L"Maximum pressure/load exceeded" },
{ L"압력 목표 도달 전 스트로크 상한 도달", L"Stroke upper limit before pressure target" },
{ L"변위 동조 한계 초과", L"Stroke synchronization trip" },
{ L"압력 불균형 · 하중 분포 확인 필요", L"Pressure imbalance; review load distribution" },
{ L"전체 목표 안정화·출력 OFF·정지 확인 완료", L"All selected targets settled; output OFF and standstill confirmed" },
{ L"정지 완료 · 작업자 확인 후 새 시작 또는 RESET 필요", L"Stopped; operator review/new START or RESET required" },
{ L"정지 미확인 · 출력 OFF와 P.T/Stroke 정체 확인 필요", L"Stop unconfirmed: PLC OFF and stable P.T/Stroke required" },
{ L"완료 후 압력 변화 · 자동 재시작하지 않음", L"Pressure changed after completion; no automatic restart" },
{ L"압력 운전 제한시간 초과", L"Pressure operation timeout" },
{ L"일부 목표 도달 · 공통 구동으로 완료 불가", L"PartialTargetReached / CannotCompleteWithSharedDrive" },
{ L"전체 목표 안정화 · 정상 정지 요청", L"All targets settled; normal STOP requested" },
{ L"일부 목표 도달 · 도달 포트 정지 유지", L"PartialTargetReached; reached ports held" },
{ L"압력 목표 추종 · COG는 진단만 사용", L"Pressure target tracking; COG diagnostic only" },
{ L"압력 센서 응답 없음", L"No pressure response timeout" },
{ L"작업자 정지", L"Operator stop" },

            {L"PLC 맵",L"PLC map"},
            { L"정지 확인 실패 · 창을 닫지 않았습니다", L"Stop not confirmed; window remains open" },
            { L"대기", L"Idle" },
            { L"시작 조건 확인 중", L"Checking" },
            { L"목표 준비 중", L"Preparing" },
            { L"운전 중", L"Running" },
            { L"정지 확인 중", L"Stopping" },
            { L"목표 도달 · 운전 완료", L"Target reached" },
            { L"사용자 정지", L"Stop requested" },
            { L"일시정지 · 시작으로 재개 · 정지를 3초간 누르면 완전 정지", L"Paused" },
            { L"완전 정지 · 재시작하려면 시퀀스 RESET", L"Full stop · Sequence RESET required to restart" },
            { L"목표 도달 · 일시정지", L"Target reached · Paused" },
            { L"완전 정지 확인 중", L"Full stop requested" },
            { L"완전 정지 확인", L"Confirm full stop" },
            { L"완전 정지하시겠습니까? 일시정지한 운전 목표가 취소됩니다.", L"Stop completely? The paused operation target will be cancelled." },
            { L"완전 정지 · 다음 시작은 새 운전", L"Full stop confirmed" },
            { L"통신·센서·인터록 상태를 확인하세요", L"Plant not ready" },
            { L"선택된 잭이 없습니다", L"No jack selected" },
            { L"잭 번호가 중복되었습니다", L"Duplicate jack ID" },
            { L"선택한 잭을 찾을 수 없습니다", L"Selected jack missing" },
            { L"동조 편차 설정을 확인하세요", L"Invalid synchronization limits" },
            { L"압력·데이터 최신성 설정을 확인하세요", L"Invalid pressure/freshness limits" },
            { L"허용 범위 설정을 확인하세요", L"Invalid limits" },
            { L"이동량을 확인하세요", L"Invalid travel" },
            { L"목표 압력을 확인하세요", L"Invalid pressure target" },
            { L"목표 위치가 잭 스트로크를 벗어납니다", L"Target outside stroke" },
            { L"잭 센서·압력·하중 상태를 확인하세요", L"Invalid jack status" },
            { L"동조 허용 편차 초과", L"Synchronization trip" },
            { L"운전 제한시간 초과", L"Operation timeout" },
            { L"장치 명령 거부", L"Command rejected" },
            { L"명령 응답 번호 불일치", L"Command ACK ID mismatch" },
            { L"데이터 갱신 정지 또는 중복 샘플", L"Repeated/stale sample" },
            { L"스트로크 한계 도달", L"Stroke limit" },
            { L"최대 압력 도달 · 이상 정지", L"Maximum pressure trip" },
            { L"하강 최소 압력 도달 · 이상 정지", L"Minimum lowering pressure trip" },
            { L"센서 무응답 제한시간 초과", L"No sensor response timeout" },
            { L"장치 비상정지", L"Plant emergency" },
            { L"비상정지 확인됨", L"Emergency stop confirmed" },
            { L"비상정지 확인 실패", L"Emergency stop not confirmed" },
            { L"준비 전 취소", L"Canceled before preparing" },
            { L"각도·개별 목표 높이 제어는 미확정입니다", L"Angle/target-height mode not commissioned" },
            { L"RESET 완료 · 새 START가 필요합니다", L"Reset accepted; new START required" },
            { L"RESET 거부 · 원인 해소와 정지 확인이 필요합니다", L"Reset rejected: cause/stop confirmation required" },
            { L"운전 중에는 설정과 잭 선택을 변경할 수 없습니다", L"Operation active: settings/selection changes rejected" },
            { L"HEIGHT ZERO · 상대변위 표시 기준 설정", L"HEIGHT ZERO: display reference only" },
            { L"교정·중량 설정 시퀀스는 미구현입니다", L"Calibration/weight setup not implemented by simulation" },
            { L"잠금을 해제한 후 시작하세요", L"Unlock before START" },
            { L"수동 압력·압력 하강 운전은 미정의입니다", L"Manual pressure / pressure lowering not defined; START rejected" },
            { L"시작 거부 · 운전 상태 확인 또는 RESET이 필요합니다", L"START rejected: busy, Fault or Emergency; RESET/new START required" },
            { L"교정 시퀀스는 미구현입니다", L"Calibration sequence is not implemented" },
            { L"수동 운전은 START를 누르는 동안만 동작합니다", L"Manual: hold START to run" },
            { L"시퀀스 RESET", L"Sequence RESET" },
            { L"운전 이상", L"Operation fault" },
            { L"시퀀스 처리 오류", L"Unhandled operation error" },
            { L"◀ 뒤로", L"◀ Back" },
            { L"뒤로", L"Back" },
            { L"화살표 표시", L"Show motion arrows" },
            { L"동작 중에는 화면을 전환할 수 없습니다. 정지 후 다시 시도하세요.", L"You cannot switch screens while running. Stop the operation and try again." },
            { L"PLC 비상정지 신호 감지: 출력 정지 확인 필요", L"PLC emergency input detected: verify outputs are stopped" },
            { L"비상정지 신호 해제: 시작 버튼으로 다시 시작하세요.", L"Emergency input cleared: press Start to restart diagnostics." },
            { L"읽기/비상정지", L"Read / Emergency stop" },
            { L"실장비 읽기 전용", L"Live PLC monitoring only" },
            { L"비상정지 전송 대기", L"Emergency stop transmission pending" },
            { L"비상정지 전송 실패: PLC 연결을 확인하세요.", L"Emergency stop transmission failed: check the PLC connection." },
            { L"ON 확인", L"ON verified" },
            { L"전송 실패/상태 불명", L"Failed / state unknown" },
            { L"전송 대기", L"Pending" },
            { L"미요청", L"Not requested" },
            { L"이동변위", L"Move distance" },
            { L"설정압력", L"Set pressure" },
            { L"잠금", L"Lock" },
            { L"최소화", L"Minimize" },
            { L"PLC 연결", L"Connect PLC" },
            { L"PLC 연결 해제", L"Disconnect PLC" },
            { L"읽기 전용", L"Read only" },
            { L"진단 중지: 센서/통신 상태를 확인하세요.", L"Diagnostics halted: check sensors / communication." },
            { L"선택 포트의 환산·수집 상태를 확인하세요.", L"Check calibration / acquisition of the selected ports." },
            { L"진단 목표와 선택 포트를 확인하세요.", L"Check diagnostic targets and selected ports." },
            { L"동기화/압력 진단 확인 필요", L"Check synchronization / pressure diagnostics" },
            { L"연결 대기", L"Disconnected" },
            { L"연결 중", L"Connecting" },
            { L"연결됨", L"Connected" },
            { L"통신 대기/오류", L"Waiting / communication error" },
            { L"읽기 전용 진단 중", L"Read-only diagnostics running" },
            { L"센서 환산 설정 확인 필요", L"Sensor calibration required" },
            { L"먼저 PLC를 연결하세요.", L"Connect the PLC first." },
            { L"PLC 설정을 확인하세요.", L"Check PLC configuration." },
            { L"읽기 전용: 속도 명령을 전송하지 않았습니다.", L"Read only: speed command was not transmitted." },
            { L"읽기 전용: 비상정지 명령은 PLC로 전송되지 않습니다.", L"Read only: emergency stop is NOT sent to the PLC." },
            { L"읽기 전용: PLC 운전 명령을 전송하지 않았습니다.", L"Read only: no PLC operation command was transmitted." },
            { L"COG 계산에는 교정된 하중, 확인된 X/Y 좌표와 3개 이상의 비직선 지지점이 필요합니다.", L"COG requires calibrated loads, verified X/Y coordinates and at least three non-collinear supports." },

            { L"변위제어", L"Displacement" },
            { L"압력제어", L"Pressure" },
            { L"자동모드", L"Auto" },
            { L"수동모드", L"Manual" },
            { L"현재압력", L"Pressure" },
            { L"현재중량", L"Load" },
            { L"상대변위", L"Relative" },
            { L"절대변위", L"Absolute" },
            { L"총 중량", L"Total weight" },
            { L"전체하중", L"Total load" },
            { L"총중량", L"Total" },
            { L"전체", L"All" },
            { L"비상정지", L"EMERGENCY" },
            { L"시작", L"START" },
            { L"정지", L"STOP" },
            { L"상승속도\n(Hz)", L"LIFTING\nSPEED (Hz)" },
            { L"하강속도\n(Hz)", L"LOWERING\nSPEED (Hz)" },
            { L"변위\n영점", L"HEIGHT\nZERO" },
            { L"압력\n영점", L"PRESSURE\nZERO" },
            { L"하중\n설정", L"LOAD\nSET" },
            { L"교정\n영점", L"CALIBRATION\nZERO" },
            { L"교정\n스팬", L"CALIBRATION\nSPAN" },
            { L"모델 변경", L"CHANGE MODEL" },
            { L"캡처 저장에 실패했습니다.", L"Failed to save the capture." },
            { L"캡처할 화면이 준비되지 않았습니다.", L"The view is not ready to capture." },
            { L"캡처 이미지 데이터가 올바르지 않습니다.", L"The captured image data is invalid." },
            { L"기본 잭 설정을 읽지 못했습니다. Models\\Default.json 파일을 확인하세요.",
              L"Unable to read default jack settings. Please check Models\\Default.json." },
			{ L"템플릿", L"Template" },
			{ L"실린더 색상 선택", L"Select Cylinder Color" },
			{ L"피스톤 색상 선택", L"Select Piston Color" },
			{ L"예: ProjectName_SLS8H-1000", L"e.g. ProjectName_SLS8H-1000" },
			{ L"DHLS 동조 리프팅 - 시작", L"DHLS Strand Jack - Launcher" },
{ L"제어설정", L"Control Settings" },
{ L"모델 및 제어 설정", L"Model and Control Settings" },
{ L"언어", L"Language" },
{ L"모델", L"Model" },
{ L"고객", L"Customer" },
{ L"작업자", L"Operator" },
{ L"문서번호", L"Document No." },
{ L"상승 동조 편차", L"Rise Sync Deviation" },
{ L"상승 동조 범위", L"Rise Sync Range" },
{ L"하강 동조 편차", L"Fall Sync Deviation" },
{ L"하강 동조 범위", L"Fall Sync Range" },
{ L"중량 편차 비율(%)", L"Weight Deviation (%)" },
{ L"최대 정지 압력", L"Maximum Stop Pressure" },
{ L"하강 정지 압력", L"Fall Stop Pressure" },
{ L"거리 비례 감속", L"Distance Deceleration" },
{ L"필터링", L"Filtering" },
{ L"화살표 색상", L"Arrow Color" },
{ L"수동 동조", L"Manual Sync" },
{ L"소리 설정", L"SOUND Settings" },
{ L"경고음 사용", L"Enable Warning Sound" },
{ L"주의 소리", L"Caution Sound" },
{ L"경고 소리", L"Warning Sound" },
{ L"센서 설정", L"SENSOR Settings" },
{ L"압력단위", L"Pressure Unit" },
{ L"중량단위", L"Weight Unit" },
{ L"변위표시 자리수", L"Displacement Digits" },
{ L"하중 게이지 위치", L"Load Gauge Position" },
{ L"하중 최소 영점", L"Minimum Load Zero" },
{ L"압력센서 AD 스케일", L"Pressure Sensor AD Scale" },
{ L"변위센서 AD 스케일", L"Displacement Sensor AD Scale" },
{ L"PLC 설정", L"PLC Settings" },
{ L"PLC 모델", L"PLC MODEL" },
{ L"PLC 수", L"PLC #" },
{ L"PT(HT) 센서 수", L"PT(HT) SENSOR# /" },
{ L"엑셀 로고", L"Excel LOGO" },
{ L"잭 설정", L"Jack Settings" },
{ L"최대중량 사용 범위", L"Maximum Weight Range" },
{ L"번호", L"No" },
{ L"잭", L"Jack" },
{ L"목표", L"Target" },
{ L"작업중", L"Working Load" },
{ L"최대중량", L"Maximum Load" },
{ L"잭선정", L"Jack Selection" },
{ L"포트", L"Port" },
{ L"잭 배치", L"Jack Arrangement" },
{ L"저장", L"Save" },
{ L"화면 캡처", L"Capture" },
{ L"나가기", L"Exit" },
{ L"닫기", L"Close" },
{ L"잭 설정 상세", L"Jack Details" },
{ L"잭 종류", L"Jack Type" },
{ L"압력센서 스케일", L"Pressure Sensor Scale" },
{ L"변위센서 스케일", L"Displacement Sensor Scale" },
{ L"실린더색상", L"Cylinder Color" },
{ L"피스톤색상", L"Piston Color" },
{ L"사용압력", L"Working Pressure" },
{ L"D1 지름", L"D1 Diameter" },
{ L"D2 지름", L"D2 Diameter" },
{ L"D3 지름", L"D3 Diameter" },
{ L"잭 스트로크", L"Jack Stroke" },
{ L"추가", L"Add" },
{ L"삭제", L"Delete" },
{ L"동조 리프팅 시스템", L"SYNCHRONOUS LIFTING SYSTEM" },
{ L"변위 제어", L"STROKE CONTROL" },
{ L"각도 제어", L"ANGLE CONTROL" },
{ L"중량 제어", L"WEIGHING CONTROL" },
{ L"교정", L"CALIBRATION" },
{ L"제어 설정", L"CONTROL SETTING" },
{ L"잭 설정 화면", L"JACK SETTING" },
{ L"한글", L"ENG" },
{ L"(주)대하 051-326-1870", L"DAEHA Co., Ltd. 051-326-1870" },
{ L"신규 모델", L"New Model" },
{ L"기존 모델", L"Existing Model" },
{ L"모델 관리", L"Model Management" },
{ L"모델 로드", L"Load Model" },
{ L"모델 삭제", L"Delete Model" },
{ L"새 모델 추가", L"Add New Model" },
{ L"신규 모델 생성", L"Create New Model" },
{ L"생성할 모델명을 입력하세요", L"Enter a model name" },
{ L"모델 생성", L"Create Model" },
{ L"취소", L"Cancel" },
{ L"확인", L"OK" },
{ L"비밀번호", L"PASSWORD" },
{ L"비밀번호 입력", L"Password" },
{ L"설정 화면 접근 비밀번호를 입력하세요.", L"Enter the password to access settings." },
{ L"비밀번호가 올바르지 않습니다.", L"The password is incorrect." },
{ L"프로그램 종료", L"Exit Application" },
{ L"정말 프로그램을 종료하시겠습니까?", L"Are you sure you want to exit?" },
{ L"모델 중복", L"Duplicate Model" },
{ L"모델 생성 실패", L"Model creation failed" },
{ L"오류", L"Error" },
{ L"모델 파일 저장에 실패했습니다.", L"Failed to save the model file." },
{ L"파일 선택...", L"Select a file..." },
{ L"파일 경로...", L"File path..." },
{ L"예: Bridge_Project_A", L"e.g. Bridge_Project_A" },
{ L" 모델이 이미 존재합니다.\n다른 모델명을 입력해 주세요.", L" already exists.\nPlease enter a different model name." },
{ L"모델 생성 중 오류가 발생했습니다.", L"An error occurred while creating the model." },
{ L"잭 ", L"Jack " },
			};
			return catalog;
		}
		static auto& Entries() { static std::vector<Entry> entries; return entries; }
		static auto& Selectors() { static std::vector<winrt::weak_ref<ComboBox>> selectors; return selectors; }
		static bool& Updating() { static bool updating = false; return updating; }

		static size_t Find(std::wstring_view text)
		{
			auto const& catalog = Catalog();
			for (size_t i = 0; i < std::size(catalog); ++i)
				if (text == catalog[i].korean) return i;
			for (size_t i = 0; i < std::size(catalog); ++i)
				if (text == catalog[i].english) return i;
			return std::size(catalog);
		}
		static void Track(DependencyObject const& target, DependencyProperty const& property)
		{
			auto value = target.GetValue(property).try_as<winrt::Windows::Foundation::IPropertyValue>();
			if (!value || value.Type() != winrt::Windows::Foundation::PropertyType::String) return;
			const auto text = value.GetString();
			const auto index = Find(std::wstring_view{ text });
			if (index == std::size(Catalog())) return;
			auto& entries = Entries();
			entries.erase(std::remove_if(entries.begin(), entries.end(),
				[](auto const& entry) { return !entry.target.get(); }), entries.end());
			auto existing = std::find_if(entries.begin(), entries.end(), [&](auto const& entry)
				{
					return entry.target.get() == target && entry.property == property;
				});
			const auto trackedIndex = existing == entries.end() ? index : existing->translation;
			if (existing == entries.end()) entries.push_back({ winrt::make_weak(target), property, index });
			auto const& translation = Catalog()[trackedIndex];
			target.SetValue(property, winrt::box_value(winrt::hstring{
					IsEnglish() ? translation.english : translation.korean }));
		}
	public:
		static bool IsEnglish()
		{
			auto language = ConfigManager::Instance().GetConfig().ModelInfo.Language;
			std::transform(language.begin(), language.end(), language.begin(),
				[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
			return language == L"en" || language == L"eng" || language == L"english" ||
				language.rfind(L"en-", 0) == 0;
		}
		static winrt::hstring Text(std::wstring_view source)
		{
			const auto index = Find(source);
			if (index == std::size(Catalog())) return winrt::hstring{ source };
			auto const& translation = Catalog()[index];
			return winrt::hstring{ IsEnglish() ? translation.english : translation.korean };
		}
		static void RegisterElement(FrameworkElement const& element)
		{
            using ButtonBase=winrt::Microsoft::UI::Xaml::Controls::Primitives::ButtonBase;
            if(auto button=element.try_as<ButtonBase>()){
                static std::vector<winrt::weak_ref<ButtonBase>> observed;
                observed.erase(std::remove_if(observed.begin(),observed.end(),[](auto const& v){return !v.get();}),observed.end());
                if(std::none_of(observed.begin(),observed.end(),[&](auto const& v){return v.get()==button;})){
                    observed.push_back(winrt::make_weak(button));
                    button.Click([](auto const& sender,auto const&){
                        auto control=sender.template as<FrameworkElement>();
                        auto label=control.Name();
                        if(label.empty()){
                            auto button=control.template as<ButtonBase>();
                            label=winrt::unbox_value_or<winrt::hstring>(button.Tag(),L"");
                            if(label.empty())label=winrt::unbox_value_or<winrt::hstring>(button.Content(),L"");
                        }
                        if(!label.empty())AuditLog::Write("UI_CLICK",winrt::to_string(label));
                    });
                }
            }
			using namespace winrt::Microsoft::UI::Xaml::Controls;
			if (auto text = element.try_as<TextBlock>())
			{
				Track(text, TextBlock::TextProperty());
				text.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
			}
			if (auto content = element.try_as<ContentControl>()) Track(content, ContentControl::ContentProperty());
			Track(element, FrameworkElement::TagProperty());
			Track(element, winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::NameProperty());
			Track(element, ToolTipService::ToolTipProperty());
			if (auto text = element.try_as<TextBox>()) Track(text, TextBox::PlaceholderTextProperty());
			if (auto password = element.try_as<PasswordBox>()) Track(password, PasswordBox::PlaceholderTextProperty());
			if (auto number = element.try_as<NumberBox>()) Track(number, NumberBox::HeaderProperty());
			if (auto combo = element.try_as<ComboBox>()) Track(combo, ComboBox::HeaderProperty());
		}
		static void RegisterFormattedText(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& text,
			std::wstring_view prefix, std::wstring suffix)
		{
			const auto index = Find(prefix);
			if (index == std::size(Catalog())) return;
			Entries().push_back({ winrt::make_weak(text.as<DependencyObject>()),
					winrt::Microsoft::UI::Xaml::Controls::TextBlock::TextProperty(), index, std::move(suffix) });
			text.Text(winrt::hstring{ std::wstring{ Text(prefix) } + Entries().back().suffix });
		}
		static void Refresh()
		{
			if (Updating()) return;
			Updating() = true;
			struct Reset { ~Reset() { Updating() = false; } } reset;
			auto& entries = Entries();
			for (auto it = entries.begin(); it != entries.end();)
			{
				if (auto target = it->target.get())
				{
					auto const& translation = Catalog()[it->translation];
					target.SetValue(it->property, winrt::box_value(winrt::hstring{
							std::wstring{ IsEnglish() ? translation.english : translation.korean } + it->suffix }));
					++it;
				}
				else it = entries.erase(it);
			}
			auto& selectors = Selectors();
			for (auto it = selectors.begin(); it != selectors.end();)
			{
				if (auto selector = it->get()) { selector.SelectedIndex(IsEnglish() ? 1 : 0); ++it; }
				else it = selectors.erase(it);
			}
		}
		static void SetEnglish(bool english)
		{
			if (IsEnglish() == english) return;
			ConfigManager::Instance().GetConfig().ModelInfo.Language = english ? L"en" : L"ko";
			AuditLog::Write("LANGUAGE",english ? "en" : "ko");
			Refresh();
		}
		static void RegisterLanguageSelector(ComboBox const& selector)
		{
			Selectors().push_back(winrt::make_weak(selector));
			selector.SelectedIndex(IsEnglish() ? 1 : 0);
			// Publishers own these delegates; only weak references point back to controls.
			selector.Loaded([weak = winrt::make_weak(selector)](auto const&, auto const&)
				{
					if (auto control = weak.get()) control.SelectedIndex(IsEnglish() ? 1 : 0);
				});
			selector.SelectionChanged([weak = winrt::make_weak(selector)](auto const&, auto const&)
				{
					if (Updating()) return;
					if (auto control = weak.get())
					{
						if (control.SelectedIndex() >= 0) SetEnglish(control.SelectedIndex() == 1);
					}
				});
		}
	};
}

