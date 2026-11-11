# DHLS_StrandJack PLC 읽기·진단 및 PC 비상정지

## 지원 범위

`evo_cog_work` 원본은 `ControlLogic/Evo`에 복사했습니다. 계산 알고리즘은 유지합니다. 표준 C++17 `EquipmentControlService`가 하나의 작업 스레드에서 소켓과 요청을 소유하고 UI에는 값 스냅샷만 전달합니다. WinUI 타이머/이벤트는 UI 스레드 및 weak reference를 사용하며, 화면 종료 시 소켓 종료와 join은 백그라운드에서 수행합니다.

프로토콜은 XGT dedicated TCP 2004입니다. PLC_Model 문자열에 따라 다른 프로토콜로 바꾸지 않습니다. 일반 상승/하강/시작/정지/HPU/속도/영점/교정은 PC 진단 또는 UI 의도이며 PLC 운전 명령을 전송하지 않습니다. **유일한 쓰기 예외는 사용자가 PC EMERGENCY 버튼을 누른 경우의 D051.0 ON 요청입니다.**

## 접속과 상태 표시

모델 로딩 후 MainMenuView에서 PLC_Count(1~4), PLC_IPs에 해당하는 PLC를 자동 조회합니다. 확인 중 회색, 전체 정상 녹색, 오류 빨강이며 메시지는 0.5초 간격으로 점멸합니다. 화면을 나가면 조회를 종료합니다.

대시보드 일반 데이터 접속은 `PLC 연결` 버튼을 사용합니다. D10, D11~D26, D51, D56, D59를 조회하며, Port(1~8)는 실제 PT/HT 채널을 지정합니다. 중복/잘못된 채널은 진단 구성 오류입니다. PC 비상 버튼을 접속 전에 누르면 PLC 개수/IP만으로 별도 접속을 시작하므로 잭 교정이나 포트 매핑에 의존하지 않습니다.

## 비상정지

주소표: `evo_cog_work/8POINT ADDRESS.xlsx`, Sheet1 H2/I2 및 H21/I21.

- D010.0 = LOCAL EMERGENCY STOP (PLC → PC). 어느 PLC에서든 ON을 읽으면 센서 읽기를 기다리지 않고 PC 진단 정지를 게시하고 대시보드 중앙에 비상 이미지를 표시합니다. 시작을 차단합니다. 이후 센서 읽기 실패/통신 단절로 비상 상태를 해제하지 않습니다. 모든 구성 PLC의 신선한 D10 읽기에서 OFF를 확인해야 입력 차단/표시를 해제하며 자동 재시작하지 않습니다. D010.0 수신 자체가 새로운 PLC 쓰기를 발생시키지는 않습니다.
- D051.0 = PC EMERGENCY STOP (PC → PLC). PC EMERGENCY 버튼 클릭 즉시 이미지와 정지 상태를 표시하고, 모델의 모든 PLC에 ON을 한 번씩 요청합니다. 각 PLC의 전송 대기/ON 확인/전송 실패 또는 상태 불명을 표시합니다. OFF, 자동 해제, 자동 재전송은 없습니다. 재클릭해야 새 요청을 보냅니다. 현재 화면 세션에서는 PC 비상 상태를 유지하며 일반 정지나 연결 해제로 시작 차단을 해제하지 않습니다.
- 재접속 때 D51.0 ON이 이미 존재하면 읽기로 비상 상태를 복원합니다. 비상 비트 읽기가 새 쓰기를 발생시키지는 않습니다.

`XgtEmergencyTransport`의 쓰기 함수는 `SetEmergencyOn()` 하나뿐입니다. D51을 새로 읽어 다른 비트를 보존한 채 bit0을 OR하고, 워드 쓰기 응답과 D51 재읽기로 ON을 확인합니다. 이미 ON이면 쓰지 않습니다. D51.0 외 주소나 OFF 요청을 받는 API는 없습니다.

**ON 확인은 D51.0 비트 확인이며 물리 출력 전체 OFF 확인이 아닙니다.** 실제 정지 동작은 PLC 비상정지 시퀀스/인터록에 달려 있습니다. 워드 read-modify-write는 PLC/다른 작성자와 원자적이지 않으므로 명령 워드 소유권과 동시 갱신 여부를 현장에서 검증해야 합니다. 통신 실패는 성공으로 취급하지 않습니다. PC 통신은 하드웨어 비상정지 회로를 대체하지 않습니다.

프로토콜 근거: [LS ELECTRIC XBL-EMTA 매뉴얼](https://sol.ls-electric.com/uploads/document/17422755756460/User_s%20Manual_XBL-EMTA_ENG_V2.0_20241023.pdf), 5-5절 D영역 Byte/Word 접근 및 5-6절 XGT 프레임 정의.

## 환산과 진단

모델의 SystemConfig.PLCSettings 선택 항목(구형 모델은 false/0):

```json
{
  "SensorScalingVerified": false,
  "EffectiveAreaVerified": false,
  "CoordinatesVerified": false,
  "PressureSigned16": false,
  "StrokeSigned16": false,
  "PressureBalanceTolerance_bar": 0
}
```

압력 = raw × JackSpec.PressureSensorScale_bar / SensorSettings.PressureSensor_ADScale.
변위 = raw × JackSpec.DisplacementSensorScale_mm / SensorSettings.DisplacementSensor_ADScale.
현재는 오프셋 0의 선형 16-bit 해석만 지원합니다. 다른 인코딩/오프셋이면 확인 플래그를 켜기 전에 어댑터를 수정해야 합니다. 미확인/단선/오래된 값은 `--`로 표시합니다.

하중(ton) = pressure(bar) × effective area(mm²) / 98066.5. D3를 실제 유효 면적으로 사용한다는 현장 확인은 EffectiveAreaVerified가 담당합니다. 단지 D3 값이 존재한다고 확인 처리하지 않습니다.

X/Y는 JackSetupDetails.X_mm/Y_mm에 저장합니다. COG는 검증된 하중/좌표와 3개 이상의 비직선 지지점이 필요하며 첫 선택 지지점 기준 수평 투영 좌표입니다. Z는 계산하지 않습니다. 여러 PLC의 샘플은 원자적이지 않으며 수집/신선도 한도를 넘으면 결과를 무효화합니다.

RiseSync_Level1/Level2, FallSync_Level1/Level2는 work/stop mm 기준입니다. 원본 알고리즘의 stop >= 3 × work > 0을 만족해야 합니다. 압력 편차 허용값은 PressureBalanceTolerance_bar로 명시하며 중량 %를 bar로 바꾸지 않습니다. PLC 내부와 전체 선택 포트에 걸친 PLC 간 편차를 함께 진단합니다. 시작은 PC 진단 실행이며 물리 운전을 의미하지 않습니다.

잭 환산/유효 면적, 잭선정, PLC/IP/포트 또는 X/Y 변경 시 관련 확인 플래그를 무효화합니다. 색상 변경은 환산 확인에 영향을 주지 않습니다. 진단 중 선택/방향/모드/목표가 바뀌거나 선택 센서가 무효가 되면 진단을 중지하며 자동 재시작하지 않습니다.

## 대시보드 표시와 화면 전환

숫자 진행률은 활성 포트 절대위치 평균 / 이동변위 × 100, 압력 모드는 현재압력 평균 / 설정압력 × 100이며 0~100%로 제한합니다. 활성 포트에 무효 측정이 있거나 목표가 유효하지 않으면 `--%`, 활성 포트가 없으면 0%입니다.

잭설정의 종류별 `화살표 표시`(ShowMotionArrow, 기본 false)를 저장합니다. 체크 시 실행 중에만 반투명 화살표를 표시하고, 해제 시 실린더 내부 색상으로 표시합니다. 정지/대기는 흰색/화살표 숨김입니다. 실행 시 노랑부터 시작하여 시작값에서 목표까지 진행한 비율에 따라 10% 단계로 녹색 점멸합니다. 화살표는 꼬리부터 머리까지 바뀝니다. 변위는 절대위치, 압력은 현재압력을 기준으로 하며 상승 >= 목표, 하강 <= 목표 도달 시 화살표는 숨기고 몸체는 녹색을 유지합니다. 정지 시 초기 표시로 돌아옵니다. 숫자 진행률은 별도의 위 평균 공식을 유지합니다.

동작 중 뒤로 이동, 변위/압력 화면 전환 및 공통 MainWindow 내비게이션을 차단하고 경고 아이콘 메시지를 표시합니다. 정지 후에는 전환할 수 있습니다. 경고창 연속 클릭은 중복 생성하지 않습니다.

## 검증

Visual Studio x64 Developer Command Prompt/Developer PowerShell에서 `.\ControlLogic\tests\RunOfflineTests.cmd`를 실행합니다. 기존 계산/코덱 8개와 서비스/시간 제한/비상 전송을 합쳐 시퀀스/시뮬레이션까지 13개 오프라인 테스트를 실행합니다. 서비스는 모의 PLC, TCP는 loopback만 사용합니다. 비상 테스트는 부분 읽기 실패, 단절 시 상태 유지, 재시작 차단, 모든 PLC 대상 전송 및 부분 실패, 명시적 재클릭, 기존 ON 복원, 다른 비트 보존, ACK/재읽기 실패 및 자동 재시도 금지를 검사합니다.

실제 PLC 접속/센서 교정/PLC 비상 시퀀스/물리 출력 차단은 이번 오프라인 검증으로 확인하지 않습니다.

접수된 PC 비상 ON 요청은 일반 정지 버튼이나 화면 종료에 의해 큐에서 취소하지 않습니다. 서비스 종료는 백그라운드에서 각 PLC의 제한된 1회 전송 시도를 마친 뒤 소켓을 정리합니다. OS 강제 종료/전원 단절에서 전송 완료를 보장하는 기능은 아닙니다.


## Developer simulation (session only)

MainMenuView has an invisible 24 x 24 effective-pixel double-click target at the upper-left corner. Enabling requires the developer password; double-clicking again disables without asking again. The flag is not saved to models and defaults to off on process startup. This is a convenience development gate, not a security boundary against someone inspecting the executable.

EquipmentControlService captures simulation mode when created. Its simulation branch returns before constructing any PLC transport, including emergency transports. MainMenu connection checks restart when toggled; Dashboard automatically connects to simulation when loaded. Both screens explicitly label simulated data.

The developer simulation now uses the integrated SequenceEngine/MockPlant. Automatic motion uses start position +/- travel; manual motion is hold-to-run. Pressure contact is automatic up only. See [Sequence integration](Sequence/INTEGRATION.md) for exact supported behavior, build commands, validation, PLC mapping and unconfirmed policies. The previous fixed absolute-target demo has been replaced.
