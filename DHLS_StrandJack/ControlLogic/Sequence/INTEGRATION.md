# DHLS 시퀀스 통합 결과

## 적용 범위

`DHLS_AI_Package/AI_WORK_ORDER.md`와 동봉 SequenceEngine/MockPlant를 기준으로 현재 WinUI 3 프로젝트에 **Mock 시퀀스**를 통합했다. 원본 패키지는 보존하고 `ControlLogic/Sequence`에서 통합본을 관리한다. 실제 PLC 운전 쓰기를 추가로 활성화한 작업이 아니다.

프로젝트는 VS 2022 v143 / C++17 / x64 Debug로 검증한다. 프로젝트 설정은 Unpackaged(`WindowsPackageType=None`), Windows App SDK Self-contained이다. SDK/패키지 버전, 배포 방식, 화면 배치, 언어·단위·포트 수·모델 JSON은 변경하지 않았다. 현재 NuGet 참조는 WindowsAppSDK 2.5.1, WinUI 2.3.9, CppWinRT 3.0.260818.1이다. 다른 아키텍처와 클린 PC 배포는 별도 검증 대상이다.

## 원본 대조

실제 패키지에는 작업 지시서의 설명과 달리 `DHLS SLS Manual_Rev2.docx`와 `동조리프팅시스템 시퀀스_20261002.xlsx`가 있다. Word 문서 OOXML 본문 527~560, 675~685 문단과 Excel sheet1 E5의 이동 예시를 직접 대조했다. 문단 번호는 추출 순서이며 문서 페이지 번호가 아니다.

- 매뉴얼: AUTO는 설정 이동량, MANUAL은 START를 누르는 동안 동작한다.
- Excel E5: 초기 0/5/0/6 mm에서 10 mm 이동하면 10/15/10/16 mm가 된다. 따라서 시작값을 빼지 않은 원시 높이 차이를 동조 오차로 삼지 않는다.
- 매뉴얼: 선행 실린더 정지 보정, 허용 편차 초과 전체 정지, 작업하중 × (1+허용%)/과압 우선 감시를 기술한다.
- 매뉴얼의 최고 압력 예시는 kg/cm², 압력 접촉 설명은 Bar를 사용한다. 두 단위를 같은 숫자로 변환하지 않았다. 통합 Mock 내부는 모델의 bar 명명 필드를 사용하며, 실제 제작사 단위 확정은 남겨 두었다.

## 소유권과 스레드

`MainDashboardView` → `EquipmentControlService` → `SimulationSequence` → `MockPlant` + `SequenceEngine` 순서다. 기존 서비스 Worker가 시뮬레이션 세션을 소유하고 엔진은 별도 Worker에서 Read/Apply/StopAll을 실행한다. Mock은 mutex로 상태·출력·샘플을 보호한다. 엔진이 먼저 소멸되고 plant와 콜백 상태가 나중에 소멸되도록 멤버 순서를 고정했다.

엔진 콜백은 시간·상태·모드·선택 잭·정지 확인을 네이티브 이력에 기록한다. Worker는 XAML에 접근하지 않는다. 기존 200 ms DispatcherTimer의 weak reference 경로가 스냅샷을 UI 스레드에 반영한다. 기존 UI 갱신 방식을 유지하기 위해 새 DispatcherQueue를 중복 도입하지 않았다.

화면 Unloaded는 수동 해제를 요청하고 서비스를 백그라운드에서 Stop한다. 시스템 창 닫기 요청은 AppWindow.Closing에서 취소한 뒤 비동기 종료 절차를 시작한다. Mock의 최종 정지 확인과 서비스 종료가 끝나면 Window.Close를 호출하며 확인 실패 시 창을 유지한다. 엔진의 StopAll/정지 확인/join은 서비스 Worker에서 끝난다. UI 스레드에서 Worker join으로 대기하지 않는다. 한 IPlant 호출을 강제 취소하는 기능은 없으므로 실제 어댑터의 호출 제한시간과 PLC watchdog은 여전히 필요하다.

## 동작

개발자 진입은 기존 메인 메뉴 왼쪽 위 24×24 영역 더블 클릭과 비밀번호를 유지한다. 앱 종료 시 해제된다. 정상 PLC 배너 숨김과 헤더 시뮬레이션 점멸 표시도 유지한다.

| 기능 | 이번 적용 |
| --- | --- |
| 자동 변위 상승/하강 | 시작 절대위치 ± 입력 이동량. 목표 범위 검사, 진행량 기반 Pause/Resume/Trip, 잭별 목표 도달 정지, 전체 정지 확인 후 Completed |
| 수동 변위 | START 누름 시작, 해제·취소·캡처 상실·포인터 이탈·포커스 상실·화면 이탈 시 STOP. 다른 창 활성화도 기존 UI 타이머에서 최대 약 200 ms 내 확인 |
| 수동 동조 | 모델 ManualSync 사용. OFF에서는 동조 보정/Trip을 하지 않으며 일반 센서·압력·스트로크 한계는 감시 |
| 압력 접촉 | 자동 상승만 허용. 잭별 최초 도달 latch 후 재가압하지 않음. 모두 도달 후 정지 확인. 무응답/전체 시간/압력/스트로크 감시 |
| 상태 | Idle/Checking/Preparing/Running/Stopping/Completed/Stopped/Fault/Emergency |
| RESET | 시뮬레이션에서 정지 버튼 우클릭 → 시퀀스 RESET. 정지 확인과 외부 원인 해소 필요. RESET은 운전을 시작하지 않음 |
| HEIGHT ZERO | 선택된 잭의 상대변위 표시 기준만 변경. 센서 절대위치와 다음 이동량 기준을 바꾸지 않음 |
| 운전 중 조작 | 모드·방향·선택 잭·영점·목표·속도·연결·화면 이동을 차단. 정지·비상정지는 유지 |
| 출력 소유권 | Apply가 선택하지 않은 잭은 Hold. 이전 운전의 출력이 남지 않음 |
| 최신성/응답 | 단조 증가 sample ID와 수집시각 검사. 명령 ID/ACK 일치 검사. 정지 후 새 샘플에서 모든 잭 stopped 확인 |
| 기록 | 최근 128개 전이: 단조 시각(ms), 운전 ID, 모드, 선택 잭, 메시지, 정지 확인. EquipmentSnapshot.sequenceHistory에서 접근. 파일 영구 저장/이력 화면은 미구현 |

Mock은 초당 10 mm 및 10 bar 변화를 쓰는 합성 장치이며 유압 물리 모델이 아니다. 실제 모델에 설정된 스트로크·압력·하중 한계를 사용하며, 스트로크 등 필수 값이 없으면 START를 거부한다. 디스플레이 소수점 자리수로 제어 계산을 반올림하지 않는다.

기존 사용자가 지정한 숫자 진행률 `활성 잭 절대위치 평균 ÷ 이동변위 ×100` 및 압력 평균 비율은 유지했다. 이는 새 엔진의 **현재 운전 시작점 기준 진행량**과 다른 표시다. 화살표 목표 판정에는 잭별 실제 자동 목표(시작값 ± 이동량)를 연결했다.

## 실제 PLC 경계

[PLC 연결표](PLC_CONNECTION_MAP.md)를 참조한다. `PlcReadOnlyPlant`는 기존 소유 서비스의 Latest 스냅샷을 변환하며 새 소켓을 만들지 않는다. Apply와 StopAll은 false를 반환한다. 실제 출력 쓰기나 실제 정지 확인을 제공하는 완료 어댑터가 아니다. 이 어댑터를 실제 운전 엔진에 연결하지 않았다.

기존 실제 경로의 직렬 읽기·진단, D010.0 감지, 별도 승인된 D051.0 ON 비상 요청은 유지한다. 시뮬레이션에서는 이 경로로 진입하지 않아 emergency를 포함한 네트워크 전송이 없다. 재연결/RESET 후 자동 운전 시작은 없다.

## 검증과 남은 사항

[검증 방법 및 결과](VALIDATION.md), [미확정·미구현 목록](OPEN_ITEMS.md)을 참조한다. WinUI 실행 화면의 실제 포인터·포커스 이벤트와 실장비는 오프라인 검사와 별개다.

## 변경 파일

- `ControlLogic/Sequence/SequenceEngine.h`: 동봉 엔진 통합 및 최신성/ACK/정지 확인/실패 복구/운전 기록 보완
- `ControlLogic/Sequence/MockPlant.h`: 설정 가능한 thread-safe 모의 장치와 fault injection
- `ControlLogic/Sequence/SimulationSequence.h`: 서비스 명령·모델·이력·표시 스냅샷 연결
- `ControlLogic/Sequence/PlcReadOnlyPlant.h`: 기존 읽기 소유자 재사용, 실제 출력 차단 경계
- `ControlLogic/Sequence/CMakeLists.txt`: 통합 엔진·서비스 테스트 빌드
- `ControlLogic/EquipmentControlService.h/.cpp`: 모델 한계·상태·이력 추가, 시뮬레이션 경로 교체
- `MainWindow.xaml.h/.cpp`: AppWindow Closing 이벤트 토큰 및 명시적 해제, 비동기 정지 확인 후 닫기, 주 창 포인터 정리
- `MainDashboardView.xaml.h/.cpp`: 수동 포인터 해제, busy 조작 차단, 상태·RESET·목표 표시 연결
- `Localization.h`: 새 상태/오류의 한영 메시지
- `ControlLogic/tests/test_sequence_engine.cpp`, `test_simulation.cpp`, `RunOfflineTests.cmd`: 신규/회귀 검증
- `DHLS_StrandJack.vcxproj`, `.filters`: 기능별 소스/테스트/원본 참조 등록
- `ControlLogic/INTEGRATION.md`, `ControlLogic/Sequence/*.md`: 설명·연결표·검증·남은 사항


## 구현 API 참고

- [Microsoft pointer input](https://learn.microsoft.com/en-us/windows/apps/design/input/handle-pointer-input): 해제뿐 아니라 취소·캡처 상실 경로를 처리한다.
- [AppWindow.Closing](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.windowing.appwindow.closing): 시스템 닫기를 취소하고 비동기 정리 후 닫는 진입점이다. OS 강제 종료·전원 상실까지 보장하지 않는다.


## AppWindow 이벤트 구독 수정

사용자가 제공한 호출 스택에서 MainWindow 생성자의 `AppWindow.Closing(auto_revoke, ...)`가 `weak_ref<IAppWindow>::from_com_ref`로 진입한 뒤 NULL을 역참조하는 것이 확인됐다. 해당 런타임 AppWindow는 IWeakReferenceSource를 제공하지 않는다. 자동 revoker 생성 과정이 실패한 것이며, InitializeComponent나 PLC/Mock 데이터 처리 오류가 아니다.

`AppWindow` 게시자를 강한 참조로 보관하고 일반 Closing 이벤트 토큰을 저장한다. Window Closed 및 MainWindow 소멸자에서 중복 없이 해제한다. 콜백은 MainWindow의 `get_weak()`만 캡처하여 순환 참조를 만들지 않는다. 생성된 base.h는 수정하지 않는다.

빌드 검증과 실제 재실행은 구분한다. 이미 디버깅 중인 Bin 실행 파일은 중지되기 전에는 교체하지 않는다. 수정 후 시작→런처 닫기 및 대시보드에서 시퀀스 종료 후 닫기 경로를 실제 실행으로 확인해야 한다.
