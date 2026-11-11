# 시퀀스 검증

## 실행 방법

Visual Studio 2022 x64 개발자 명령 프롬프트에서 프로젝트 폴더를 기준으로 실행한다.

```bat
cmake -S DHLS_AI_Package -B BuildCheckSequenceSample -G "Visual Studio 17 2022" -A x64
cmake --build BuildCheckSequenceSample --config Debug
ctest --test-dir BuildCheckSequenceSample -C Debug --output-on-failure

cmake -S ControlLogic\Sequence -B BuildCheckSequence -G "Visual Studio 17 2022" -A x64
cmake --build BuildCheckSequence --config Debug
ctest --test-dir BuildCheckSequence -C Debug --output-on-failure --timeout 30

ControlLogic\tests\RunOfflineTests.cmd
```

## 확인된 결과

- 원본 패키지 CMake/CTest: SequenceSmoke 1/1 통과.
- 통합 CMake/CTest: SequenceEngine 및 SimulationService 2/2 통과.
- 전체 오프라인 묶음: 13개 통과. 계산·XGT 프레임/쓰기 정책·COG·하강/압력 진단·서비스·절대 통신 제한시간·기존 emergency 전송·시뮬레이션·새 시퀀스 포함.
- 모의/loopback만 사용. 실제 PLC와 네트워크 운전 출력 시험은 하지 않았다.
- 최신 엔진·서비스 CTest 2개를 각각 10회 연속 실행해 모두 통과했다. 이력/RESET, 운전·비상 요청 중 종료, 읽기 예외에도 StopAll 시도, 정지 ACK만 수신했을 때 미확인 처리까지 포함한다.
- 최종 CTest 로그: `BuildCheckSequence/Testing/Temporary/LastTest.log`.
- 최종 전체 회귀 로그: 솔루션 폴더의 `sequence-offline-final-tests.log`.
- 최종 WinUI 3 C++/WinRT x64 Debug 빌드 성공. 포인터 입력, AppWindow 닫기 취소 및 비동기 정리 경로까지 컴파일/링크 검증했다.
- 최종 CTest 확장 사례(검사·준비·운전 중 비상정지, 운전 후 통신 두절·샘플 정지, 압력 무응답·전체 제한시간)도 2/2 통과.
- WinUI 실제 마우스/터치·Alt+Tab·창 닫기 상호작용 및 물리 장비 검증은 미실시.
- 최종 WinUI 로그: 솔루션 폴더의 `sequence-integration-final-build.log`.
- 기존 JackArrangementView IDL의 MIDL4066 이름 충돌 경고는 일부 재생성 빌드에서 남아 있으며 이번 범위에서 변경하지 않았다.

## 새 테스트의 범위

초기 높이 차이와 상대 이동 목표, 1개/복수 잭, 선택 없음·중복·누락, NaN/Infinity, 센서 무효, 목표 범위 초과, 선행 잭 Pause/Resume 및 Trip, 자동 상승/하강, 수동 시작/정지, 압력 최초 도달, 최대/최소 압력 우선 정지, 무응답, 명령 거부/ACK 불일치, 반복/stale 데이터, Checking/Preparing/Running 중 STOP, Emergency/RESET, 정지 확인 실패 후 RESET 거부 및 원인 해소 후 재확인, 미선택 잭 Hold와 실제 PLC 쓰기 거부를 검사한다.

서비스 테스트는 실제 transport factory 호출이 0인 상태로 움직임·정지·비상정지·RESET을 검증하고, 시뮬레이션 해제 시 실제 서비스 경로로 돌아감을 검사한다. UI 이벤트를 실제 마우스로 조작하는 시험과 물리 PLC 검증을 대신하지 않는다.


## AppWindow 이벤트 구독 충돌 수정

사용자 호출 스택으로 `Closing(auto_revoke)`의 IAppWindow weak reference 생성 실패를 확인했다. 명시적인 이벤트 토큰 등록과 Closed/소멸 시 해제로 교체했으며 x64 Debug 빌드가 통과했다(`appwindow-token-build.log`). 실행 중인 이전 프로세스 종료를 확인한 뒤 Bin의 EXE/PDB/PRI/XBF/WinMD 13개를 함께 교체하고 해시를 확인했다. 새 빌드의 실제 시작·대시보드 종료 반복 조작은 사용자 재실행 확인이 필요하다.
