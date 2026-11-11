# 변경 이력

## [2026-11-08 16:13 KST] TASK-SJ-JACK-RANGE — 잭 설정 최대 하중 가동 범위
- 상태: 구현 완료, 빌드 확인 전
- 요청사항: 잭 설정 글자·행 간격을 일반 설정에 맞추고 최대 하중 가동 범위를 추가
- 기준: `ControlSettingsView.xaml`의 입력 행 배치와 기존 `JackSettingsView` 계산 표시 방식
- 수정 파일: `DHLS_StrandJack/JackSettingsView.xaml`, `DHLS_StrandJack/JackSettingsView.xaml.cpp`
- 변경 이유: 잭 설정 입력 밀도를 맞추고 실린더 변위에서 안전·하중전이 여유를 뺀 값을 보여주기 위함
- 변경 내용: 왼쪽 입력 행 간격 축소, 안전 범위 아래 읽기 전용 계산값 추가. 계산식은 `실린더 변위 - 안전 범위 × 2 - 하중전이 범위`이며 세 값 변경 시 갱신
- 기존 기능 영향: 저장 형식과 PLC 출력 경로 변경 없음. 음수 결과는 설정 검토를 위해 그대로 표시
- 빌드: 확인 전. Main Bin `C:\ProjectDH\DHLS_StrandJack\DHLS_StrandJack\Bin`
- 자동 시험: XAML XML 구문 확인 통과
- PC 실행 검증: NOT_RUN (요청 없음)
- 위험/미확정: 실제 화면의 시각적 일치 여부는 실행 검증 전까지 미확인
- 롤백: 위 두 파일에서 이 작업의 행 배치·계산 표시 변경만 되돌림

## [2026-11-08 KST] TASK-SJ-JACK-RANGE-INPUT — 가동 범위 입력 및 초과 경고
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 최대 하중 가동 범위를 직접 입력하고 허용 상한 초과 시 경고
- 수정 파일: `DHLS_StrandJack/JackSettingsView.xaml`, `DHLS_StrandJack/JackSettingsView.xaml.cpp`, `DHLS_StrandJack/JackSettingsView.xaml.h`, `DHLS_StrandJack/SystemConfigModels.h`, `DHLS_StrandJack/ConfigManager.cpp`
- 변경 내용: 읽기 전용 값을 숫자 입력으로 변경. `실린더 변위 - 안전 범위 × 2 - 하중전이 범위` 초과 시 빨간 테두리·초과 표시와 경고음을 1회 표시하고 저장을 차단. JSON에 입력값 저장; 기존 파일에 필드가 없으면 상한값으로 초기화
- 기존 기능 영향: 잭 설정 JSON에 선택적 숫자 필드 추가. PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청에 따라 실행하지 않음)
- 미확정: C++/WinRT 컴파일 및 실제 입력 UI 동작은 빌드·실행 전까지 미확인
- 롤백: 위 5개 파일에서 `MaxLoadOperatingRange_mm` 직렬화·입력·경고 변경 제거

## [2026-11-08 KST] TASK-SJ-HEADER-ICONS — 대시보드 상단 아이콘 통일
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 뒤로·잠금 아이콘의 크기와 빨간 배경·마우스 오버 배경을 카메라와 일치
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 세 버튼에 동일한 64×64 템플릿을 적용하고 빨간 헤더 위 투명 기본 배경과 같은 오버 상태를 사용. 뒤로·잠금 아이콘을 흰색으로 확대. 잠금 상태는 아이콘 모양으로 구분
- 기존 기능 영향: 버튼 클릭 처리와 PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 렌더링과 아이콘 크기는 실행 전까지 미확인
- 롤백: 위 두 파일의 `HeaderIconButtonStyle` 적용과 아이콘·잠금 표시 변경 복원

## [2026-11-08 KST] TASK-SJ-BOTTOM-RATIO — 대시보드 하단 높이·폭 비율
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 하단 높이를 기존의 75%로 줄이고 비상정지:거리:준비:전체하중을 1:2:1:2 폭으로 배치, 로그는 나머지 사용
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 하단 높이 225→169, 비상정지 157×157 정사각형, 고정 폭 157/314/157/314 및 남은 로그 폭. 내부 행 높이도 157 높이에 맞춤. 좁은 화면에서는 최소 디자인 폭 1200을 Viewbox로 축소
- 기존 기능 영향: 버튼 클릭 처리·PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 화면에서 글자·슬라이더의 시각적 크기는 실행 검증 전까지 미확인
- 롤백: 위 두 파일의 하단 행 높이·고정 폭·내부 높이·최소 디자인 폭 변경 복원

## [2026-11-08 KST] TASK-SJ-BOTTOM-ALIGN — 하단 버튼·거리 입력 정렬
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 준비 블록 버튼을 폭에 맞추고 글자를 확대·굵게 표시, 거리 블록 폭 10% 축소 및 두 거리 행 정렬
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 준비/일시정지/시작 버튼을 블록 전체 폭으로 확장하고 20pt Bold 적용. 거리 블록 314→283, 목표거리·이동거리의 행 높이 42, 입력 열 100, 단위 열 40으로 통일. 라벨·입력값·mm 글자 크기와 수직 정렬 통일
- 기존 기능 영향: 버튼 클릭 처리와 PLC 출력 경로 변경 없음. 화면 표기 `이동변위`를 `이동거리`로 변경
- 검증: XAML XML 구문 정적 확인. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 렌더링에서 글자 잘림 여부는 미확인
- 롤백: 위 두 파일의 거리 블록 폭·행 정렬·버튼 서식 변경 복원

## [2026-11-08 KST] TASK-SJ-ARROWS-HOVER — 방향 화살표 및 상단 오버 색상
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 상승·하강·텐셔닝·뒤로 화살표를 첨부한 둥근 실루엣으로 통일하고 상단 아이콘 오버 배경을 파란색으로 변경
- 수정 파일: `DHLS_StrandJack/MainMenuView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml`
- 변경 내용: 동일한 둥근 화살표 경로를 방향별로 회전해 적용. 대시보드 상승·하강 버튼도 같은 형태로 변경. 뒤로·카메라·잠금 버튼의 오버 배경을 둥근 파란색(`#0056A8`)으로 통일
- 기존 기능 영향: 메뉴/버튼 클릭 처리와 PLC 출력 경로 변경 없음
- 검증: 두 XAML 파일의 XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 렌더링의 곡선·회전·색상은 실행 전까지 미확인
- 롤백: 위 두 XAML 파일의 화살표 경로 및 `HeaderIconButtonStyle` 오버 색상 복원

## [2026-11-08 KST] TASK-SJ-ACTION-BUTTON-SIZE — 준비·정지·시작 버튼 크기
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 세 버튼 폭을 비상정지 버튼과 같게 하고 겹치지 않는 최대 높이로 확대
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 변경 내용: 준비·일시정지·시작 버튼을 각각 157×51로 설정. 157 높이의 블록에 2 간격으로 세 버튼을 배치
- 기존 기능 영향: 버튼 클릭 처리와 PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 화면 렌더링은 미확인
- 롤백: 위 파일의 세 버튼 Width/Height 설정 복원

## [2026-11-08 KST] TASK-SJ-BACK-ARROW — 뒤로가기 화살표 표시 수정
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 보이지 않는 뒤로가기 화살표를 카메라처럼 빨간 배경의 흰색 화살표로 표시
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 변경 내용: 회전 변환을 제거하고 표시 영역 안에 직접 그린 흰색 왼쪽 화살표 경로로 교체. 카메라와 공유하는 상단 버튼 스타일 유지
- 기존 기능 영향: 뒤로가기 클릭 처리 및 PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 화면 표시 여부는 실행 검증 전까지 미확인
- 롤백: 위 파일의 BackNavigationButton 경로를 이전 경로로 복원

## [2026-11-08 KST] TASK-SJ-DISTANCE-ALIGN — 하단 거리 행 정렬
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 그림의 빨간 X 빈 간격 제거, 목표거리·이동거리 라벨/입력창/mm 수직 정렬
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 하단 블록 간 간격을 0으로 변경. 두 거리 행에 동일한 283 폭과 143/100/40 열 너비, 42 행 높이를 적용하고 이동거리 행도 거리 블록 왼쪽에 맞춤
- 기존 기능 영향: 거리값 및 버튼 동작, PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 렌더링 정렬은 실행 전까지 미확인
- 롤백: 위 두 파일의 열 너비·거리 패널 폭·블록 간격 변경 복원

## [2026-11-08 KST] TASK-SJ-STARTSTOP-LEFT — 운전 버튼 시작점 정렬
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: StartStopPanel 왼쪽 시작점과 세 버튼의 시작점 일치
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 준비 블록·StartStopPanel·준비/정지/시작 버튼 모두 폭 157 및 왼쪽 정렬로 고정
- 기존 기능 영향: 버튼 클릭 처리 및 PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 화면 렌더링은 미확인
- 롤백: 위 두 파일의 StartStopPanel·버튼·BottomActionPanel 정렬 설정 복원

## [2026-11-08 KST] TASK-SJ-BOTTOM-OPERATIONS — 하단 운전 패널 통합
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: Bottom operation panel 정의, 목표거리 입력, 거리 행 간격 축소, SET별 속도 조절을 하단으로 이동, 전체하중/속도 높이 50:25:25
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.h`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`, `DHLS_StrandJack/SystemConfigModels.h`, `DHLS_StrandJack/ConfigManager.cpp`
- 변경 내용: 하단 패널 이름 변경. 목표거리 NumberBox와 `GoalDistanceMm` JSON 필드를 추가하고 기존 모델에는 10000mm 기본값 적용. 거리 행 간격을 1로 축소. 상단 SET별 속도 조절 UI를 숨기고 하단 SET 선택기와 상승/하강 속도 슬라이더에 기존 속도 변경 액션을 연결. 전체하중 79, 상승속도 39, 하강속도 39 높이로 배치
- 기존 기능 영향: 기존 SET별 속도 값과 명령 경로는 유지. 목표거리는 현재 설정 보관용이며 자동운전의 이동 명령은 기존 `MovementDistanceMm`를 계속 사용. PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 및 이벤트 이름·설정 필드 정적 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: C++/WinRT 컴파일과 실제 레이아웃 동작은 빌드·실행 전까지 미확인
- 롤백: 위 다섯 파일에서 `GoalDistanceMm` 및 FooterSpeed 컨트롤·이벤트 변경 복원

## [2026-11-08 KST] TASK-SJ-DISTANCE-FONT-WHEEL — 거리 입력 서식과 숫자 증감
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 목표거리/동작거리/사용하중 설정/누적거리 영점 글자 크기 통일, 두 입력값 글자 크기 통일, 숫자 스크롤 증감 추가
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.h`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 네 항목을 18pt Bold, 두 입력값을 21pt Bold로 설정. 입력창에 Compact 증감 버튼 추가. 초점이 있는 숫자 입력창에서 마우스 휠로 1단위 증감. 화면 라벨을 동작거리로 통일
- 기존 기능 영향: 거리값 갱신 경로 유지. PLC 출력 경로 변경 없음
- 검증: XAML XML 구문 및 이벤트 선언·정의 정적 확인 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: WinUI 이벤트 컴파일과 실제 휠 입력 동작은 빌드·실행 전까지 미확인
- 롤백: 위 세 파일의 글자 서식, SpinButtonPlacementMode 및 휠 이벤트 변경 복원
## [2026-11-08 KST] TASK-SJ-MOVEMENT-IN-BOTTOM-DISTANCE — 동작거리 입력 배치 이동
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: MovementInputPanel의 동작거리 관련 내용을 BottomDistancePanel 내부로 이동
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 동작거리 라벨·NumberBox·단위를 BottomDistancePanel의 두 번째 행에 직접 배치하고 생성자의 런타임 재배치 코드를 제거. 기존 이름과 값 변경·마우스 휠 이벤트 유지
- 검증: XAML XML 구문 검사 통과, MovementInputPanel 단일 선언 확인. 빌드 NOT_RUN, GUI/PC NOT_RUN (사용자 요청 없음)
- 미확정: 실제 렌더링과 컴파일 결과는 확인하지 않음
- 롤백: 두 파일의 MovementInputPanel 배치와 생성자 재배치 코드 복원

## [2026-11-08 KST] TASK-SJ-MOVEMENT-FORMAT — 동작거리 행 형식 통일
- 상태: 코드 수정 완료, 빌드 미실행
- 변경 내용: BottomDistancePanel의 동작거리 행 높이를 목표거리와 같은 42로 조정하고 별도 배경을 제거해 부모 배경을 공유. 양쪽 행의 열 폭, 글꼴, 입력창 스타일 및 여백 동일
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN

## [2026-11-08 KST] TASK-SJ-REMOVE-SET-TOOLBAR-ITEMS — 잭별 상단 표시 정리
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 메인 화면에서 PLC1, HPU, 총중 표시 제거
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 잭별 도구 영역에서 PLC/HPU 상태 표시와 총중량 표시를 제거. ERROR, All 및 하단 전체 하중 표시 유지. 상태 갱신용 내부 객체 유지
- 검증: 소스 위치 확인. 빌드 NOT_RUN, GUI/PC NOT_RUN

## [2026-11-08 KST] TASK-SJ-FOOTER-LOAD-REFERENCE — 전체하중·속도 영역 참조 배치
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 첨부 참조 이미지의 전체하중 영역 형태 반영
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`, `DHLS_StrandJack/Localization.h`
- 변경 내용: 전체하중 명칭과 흰색 대형 수치·Ton 배치, 하단 상승·하강속도 행의 흰색 Hz 값 상자와 밝은 슬라이더 영역 반영. 기존 SET 선택과 속도 변경 이벤트 유지
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN
- 미확정: 실제 WinUI 렌더링 및 슬라이더 강조색은 실행 전까지 미확인

## [2026-11-08 KST] TASK-SJ-SPEED-20-60 — 대시보드 속도 변경 범위
- 상태: 코드 수정 완료, 빌드 미실행
- 요청사항: 상승·하강 속도 변경 범위를 20~60Hz로 제한
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 변경 내용: 하단 및 내부 슬라이더의 최솟값을 20으로 수정, 이전 세션 속도를 복원할 때 20~60으로 제한, 범위 밖 값 변경 거부와 경고 문구 수정
- 검증: XAML XML 구문 검사 통과. 빌드 NOT_RUN, GUI/PC NOT_RUN

## [2026-11-08 KST] 하단 전체하중 표시 정렬
- 변경 내용: SET 선택 상자를 하단에서 상단으로 이동하고 여러 SET이 있을 때만 표시. 전체하중 라벨을 왼쪽에 배치하고 값과 단위를 라벨 크기의 1.5배 굵은 글씨로 표시
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`, `DHLS_StrandJack/MainDashboardView.xaml.cpp`
- 검증: XAML XML 구문 확인. 빌드 및 실행 미실시

## [2026-11-08 KST] 비상정지 버튼 강조
- 변경 내용: 비상정지 버튼 테두리를 4에서 1로 줄이고 마우스를 올리면 빨간 배경과 노란 글씨를 적용
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 검증: XAML XML 구문 검사 통과. 빌드 및 실행 미실시

## [2026-11-08 KST] 일시정지·시작 버튼 마우스 오버
- 변경 내용: 일시정지와 시작 버튼의 마우스 오버 배경을 초록색으로 표시
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 검증: XAML XML 구문 검사 통과. 빌드 및 실행 미실시

## [2026-11-08 KST] 운전 중 시작 버튼 표시
- 변경 내용: 시작 버튼의 운전 중 배경과 마우스 오버에 동일한 초록색 리소스를 사용해 운전 중 포인터 이동 시 색상 유지
- 검증: XAML XML 구문 검사. 빌드 및 실행 미실시

## [2026-11-08 KST] 전체하중 패널 폭
- 변경 내용: 전체하중 패널을 하단 하중·속도 그리드에 직접 선언해 314×79 영역을 채우도록 수정. 기존 220 폭과 여백을 적용하던 생성자 재배치 경로에서 분리
- 검증: XAML XML 구문 및 부모 열·행 설정 확인. 빌드 및 실행 미실시

## [2026-11-08 KST] 전체하중 수치 색상
- 변경 내용: 잠금 해제 상태를 갱신할 때 전체하중 수치의 연두색이 흰색으로 덮어써지던 부분을 수정
- 검증: XAML 초기 색상과 C++ 갱신 색상 일치 확인. 빌드 및 실행 미실시

## [2026-11-08 KST] 일시정지·시작 버튼 마우스 오버 글자색
- 변경 내용: 마우스 오버 상태에서 배경 대신 글자에 진한 초록색을 적용
- 수정 파일: `DHLS_StrandJack/MainDashboardView.xaml`
- 검증: XAML XML 구문 검사 통과. 빌드 및 실행 미실시

## [2026-11-08 KST] 운전 모드별 하단 메뉴
- 변경 내용: 수동·텐셔닝·Calibration 하단 배치를 추가. 기존 비상정지·하중·속도·로그 영역을 공유. 수동 상승·하강에 기존 방향 명령과 누르는 동안의 운전 경로 연결
- 제한: 웨지·클램프, 텐셔닝 준비·시작, Calibration 이동 및 센서 영점은 검증된 전용 절차가 없어 비활성화
- 검증: XAML XML 구문, 컨트롤 이름 및 모드 분기 확인. 빌드 및 실행 미실시

## [2026-11-08 KST] 하단 입력 상태·눈금·마우스 오버
- 변경 내용: 텐션거리 입력을 비활성화하고 동작거리 입력을 상시 활성화. 속도 눈금 5/10 Hz 표시, 웨지 닫힘·열림 반전 색상과 일시정지·시작 라임그린 글자 적용
- 검증: XAML XML 구문 검사. 빌드 및 실행 미실시

## [2026-11-08 KST] 상승·하강과 Calibration 버튼 색상
- 변경 내용: 상승·하강 마우스 오버 청록 배경과 초록 글자, 누름 노랑 배경 적용. Calibration 정지·영점 마우스 오버 라임그린 글자, 누름 주황 배경 적용
- 검증: XAML XML 구문 검사. 빌드 및 실행 미실시

## [2026-11-08 KST] 하단 버튼 누름 색상 및 본문 모드명
- 변경 내용: 웨지 열림·닫힘 노랑, 누적거리 영점·텐셔닝 준비 주황, 텐셔닝 시작 라임그린 누름 배경 적용. 첫 SET 도구줄에 현재 모드명 표시
- 검증: XAML XML 구문 검사 통과. 빌드 및 실행 미실시

## [2026-11-08 KST] 모드명 가운데 정렬
- 변경 내용: 모드명을 본문 전체 너비의 중앙에 굵은 라임그린으로 배치. 두 열 배치에서는 첫 행 오른쪽 SET의 도구줄을 오른쪽으로 정렬해 제목 공간 확보
- 검증: XAML XML 구문 검사. 빌드 및 실행 미실시

## [2026-11-08 KST] 첨부 HMI 기준 잭 카드 재구성
- 변경 내용: 공통 대시보드의 잭 카드를 유온·펌프, 상·하 웨지, 고정 하부 실린더, 이동 상부 조립체, 계측 4행과 하중 막대로 재구성
- 이동 표시: 자동 시퀀스의 시작점과 목표점 또는 운전 시작 위치와 설정 동작거리를 기준으로 0~100% 계산. 실제 변위 피드백으로 상부 조립체와 피스톤 로드 갱신
- 데이터 제한: 실제 유온·웨지 위치는 확인 가능한 PLC 피드백이 없어 미확인으로 표시. 웨지 프리뷰와 펌프 메뉴는 시뮬레이션에서만 동작
- 검증: 첨부 HTML 스크립트 실행으로 펌프 색상, 웨지 방향, 이동 범위 및 펌프 OFF 시 정지 확인. Debug x64 메인 Bin 빌드 통과. 빌드 명령에만 x64 도구, 직렬 컴파일과 /Z7 적용. WinUI 실행 미실시

## [2026-11-09 KST] 대시보드 COG 비활성화 및 파일 분류
- 변경 내용: 상단·보고 도구 COG 버튼과 호출을 비활성화하고 잠금 갱신에서도 비활성 상태 유지
- 분석: `docs/WORKSPACE_AUDIT_20261109.md`에 파일 용도, 중복 빌드·캐시, 보존 데이터와 후속 순서 기록
- 검증: XAML XML 및 COG 선언 확인. 빌드, 앱 실행, 파일 삭제 미실시

## [2026-11-09 KST] 중복 빌드와 Visual Studio 캐시 정리
- 삭제: `DHLS_StrandJack/x64`, `DHLS_StrandJack/DHLS_StrandJack/x64`, `build_ui_hpu2` 및 잠겨 있지 않은 Visual Studio 캐시. 조사 시점 대비 약 7.03 GiB
- 보존: 검증 모델 1개와 로그 3개를 `build_history/cleanup_20261109`에 복사하고 SHA-256 일치 확인. 메인 Bin과 중간 파일, 모델 원본, Visual Studio 개인 설정 유지
- 남은 항목: 다른 프로세스가 사용 중인 Copilot·파일 인덱스와 Browse DB. 빌드 및 앱 실행 미실시
- 후속 정리: 잠금 해제 후 남은 Copilot·파일 인덱스, Browse DB와 관련 캐시를 삭제해 약 272 MiB 추가 확보. `.suo`와 레이아웃 파일 2개 보존 확인

## [2026-11-09 KST] 자동시퀀스 준비 및 기본 PLC 출력 차단
- 이유: PC 제어 선택 비트와 스트랜드잭 장비 출력 승인은 별개. 자동시퀀스 지시서 v1.1에 따라 시뮬레이션을 기본으로 두고 앱의 실제 PLC는 읽기 전용으로 제한
- 대상: `App.xaml.cpp`, `MainMenuView.xaml.cpp`, `MainDashboardView.xaml*`, `Localization.h`, `ControlLogic/EquipmentControlService.*`, `ControlLogic/tests/test_simulation.cpp`
- 검증: Main Bin의 `_intermediate/offline`에 네이티브 시험 빌드 및 실행 PASS. 기본 서비스 명령 거부와 기존 시뮬레이션 회귀 확인. XAML XML 검사 PASS. WinUI 전체 빌드·실행 및 PLC 시험 NOT_RUN
- 미해결: 실제 I/O 맵과 웨지 하중 지지 절차, 하강 변경안 승인, 대시보드 오프라인 화면 검증
- 롤백: 위 파일의 이번 변경을 배치 단위로 복원. 출력 경로 복원 여부는 승인된 장비 절차와 대조 후 결정

## [2026-11-09 KST] 5개 운전 모드 및 8·16잭 화면 검증 준비
- 변경 내용: 메인 메뉴 안내의 시뮬레이션 플래그를 올바른 네임스페이스로 지정. 검증용 8·16잭 JSON을 Main Bin의 `_intermediate/offline/models`에 작성
- 검증: Debug x64 WinUI 빌드 PASS, 5개 메뉴 진입과 검증 모델 HPU 식별자 정적 확인 PASS. GUI/PLC NOT_RUN
- 근거: `build_history/hmi_followup_20261109.log`, `docs/WORKSPACE_AUDIT_20261109.md`

## [2026-11-09 KST] 시뮬레이션 WinUI 화면 실행 검증
- 확인: 5개 모드 진입과 복귀, 8잭 한 줄 및 16잭 좌우 SET 배치, HPU 번호 순서, COG 비활성화. 모의 상승은 0→48→83→100 mm를 표시했고 상부 조립체 이동, 고정 하부, 펌프 ON/OFF 및 완료 확인
- 발견: 모델 목표거리 300 mm가 화면에서 10000 mm로 표시됨. 전체하중 21.0 Ton에서 라벨 줄바꿈. 동조 편차 미설정으로 시작이 거부된 뒤에도 복귀가 RESET 전까지 차단됨
- 종료: 검증 앱 정상 종료, 임시 모델 사본 제거. 실 PLC 미접속

## [2026-11-09 KST] 잭 선택과 펌프 스위치 표시 분리
- 변경 내용: 펌프 스위치 평상시 은색 그라데이션과 오버 회색, OFF 왼쪽 검정 및 ON 오른쪽 초록 표시. 펌프 OFF에서 잭 선택은 실린더 색상만 활성화하고 흐린 회색 점선을 유지. ON에서 선택하면 카드와 그림 배경도 활성화
- 제어: 스위치 누름은 잭 선택을 토글하지 않고 기존 SET별 시뮬레이션 펌프 명령을 사용
- 검증: 소스 상태 분기 확인. 빌드 및 실행 미실시

## [2026-11-09 KST] PLC별 펌프 상태 연결
- 변경 내용: 대시보드 펌프 상태 API를 1부터 시작하는 PLC 번호 기반으로 변경해 해당 SET의 카드만 갱신. 실제 피드백은 PLC별 D010.2를 사용하고 실제 연결은 읽기 전용
- 검증: Main Bin 오프라인 네이티브 시험 PASS. 두 PLC의 펌프 ON/OFF를 독립적으로 전환하고 기존 모의 시퀀스 회귀 확인. WinUI 전체 빌드와 화면 검증 미실시

## [2026-11-09 KST] 일반 설정 PLC·HPU 매핑 및 IPv4 입력 검증
- 변경 내용: 첫 열에 PLC 번호와 1:N 매핑 및 HPU 번호 표시, HPU 클릭 시 노란 행 배경 적용. IP 열 비율 1.20→1.32, 잭 타입 2.20→2.08
- 연결: 대시보드 잭 설정 행을 PLC당 HPU 수 기준으로 찾아 설정 표·서비스와 동일한 인덱스 사용
- 입력: IPv4 네 구간과 각 0~255 확인. 유효하지 않은 입력에 빨간 테두리를 표시하고 저장 차단. PLC 접속 IP와 개별 잭 행 IP는 기존 데이터 필드를 유지
- 검증: XAML XML 검사 및 열 비율·인덱스 정적 확인. WinUI 빌드·실행 미실시

## [2026-11-09 KST] HPU IP별 펌프 그룹
- 변경 내용: 일반 설정 HPU 인덱스 표기를 복원하고, 펌프 스위치와 표시를 HPU 행의 IP 기준으로 연결. 같은 IP의 카드는 SET이 달라도 상태 공유
- 실제 연결: 등록 PLC IP의 D010.2 피드백을 같은 IP의 카드에 표시. 등록되지 않은 IP는 미확인. 실장비 쓰기 비활성 정책 유지
- 검증: 두 IP와 SET 간 공유를 확인한 Main Bin 오프라인 네이티브 시험 PASS. WinUI 전체 빌드·실행 미실시

## [2026-11-09 KST] 잭 카드 여유 높이 활용 및 2라인 배치
- 변경 내용: 카드 영역에서 남는 높이를 점선·피스톤 표시 구간에 배분하고 하부 실린더와 계측부를 아래로 배치. 동작거리 대비 표시 비율 유지
- 배치: 기존 좌우 병렬과 별도로 상하 2라인 옵션 활성화. 미리보기, 대시보드, JSON에 같은 배치 값 적용
- 검증: XAML XML 및 2라인 배치·카드 좌표 정적 확인. WinUI 빌드·실행 미실시

## [2026-11-09 KST] 거리 비례 감속 설정 활성화
- 변경 내용: 제어 설정의 거리 비례 감속 버튼을 다시 사용할 수 있게 변경. 기존 입력 검증, 미리보기 및 모델 저장 경로 사용
- 검증: XAML XML 구문과 버튼 이벤트 연결 확인. WinUI 빌드·실행 미실시

## [2026-11-09 23:38 KST] TASK-SJ-DISTANCE-PREVIEW — 거리 비례 감속 설정 미리보기
- 상태: 소스 반영 완료, 빌드 및 PC 실행 미실시
- 근거: 스트랜드잭 시퀀스_20260326.xlsx의 리프팅·로워링 시트 오른쪽 정지 전 20 mm 사례
- 수정 파일: DHLS_StrandJack/ControlSettingsView.Deceleration.cpp
- 변경: 입력 항목을 목표까지 남은 거리 기준으로 표현하고 감속 시작·정지 요청 위치, 색상 구간 및 속도 그래프의 현재 위치를 연동
- 기존 기능 영향: 계산식, 모델별 저장 및 PLC 출력 경로 수정 없음
- 검증: 일반 이동, 감속 거리보다 짧은 이동, 허용 범위 안에서 시작, 허용 거리 0의 네 가지 미리보기 경계 계산 확인. 화면 렌더링 및 장비 동작 미확인
- 롤백: 위 소스 파일의 DistanceEditor 및 거리 비례 감속 다이얼로그 UI 변경을 되돌림

## [2026-11-09 23:47 KST] TASK-SJ-CLAMP-ARROW — 잭 열림·닫힘 화살표
- 수정 파일: DHLS_StrandJack/MainDashboardView.xaml.cpp
- 변경: 대시보드 상·하부 웨지의 삼각형 표시를 첨부 이미지처럼 몸통과 검은 테두리가 있는 화살표로 교체. 닫힘은 안쪽 녹색, 열림은 바깥쪽 주황색으로 구분.
- 검증: 상·하부에 공통 적용되는 좌표와 기존 열림·닫힘 표시 조건을 소스에서 확인. 빌드 및 PC 실행 미실시.
- 롤백: CreatePortCard 내부 clamp 화살표 생성부만 되돌림.

## [2026-11-09 23:53 KST] TASK-SJ-MOTION-ARROW — 상승·하강 단계 표시
- 수정 파일: DHLS_StrandJack/MainDashboardView.xaml.cpp, DHLS_StrandJack/MainDashboardView.xaml.h
- 변경: 자동 변위 운전의 시작·감속에 단일 화살표, 감속 범위 진입 전 구간에 화살표와 두 개의 꼬리 표시. 상승은 위, 하강은 180도 회전. 시작 주황 점멸 1.2초, 정지 확인된 완료 녹색 점멸 1.6초.
- 검증: 소스에서 방향, 감속 거리 비교, 시작 및 완료 시간 조건과 기존 UI 타이머 재사용을 확인. 빌드 및 PC 실행 미실시.
- 미확정: 시각적 일치와 실제 장비 운전 미확인. 고속 모양은 설정 거리 기준으로 표시되며 측정 속도 표시가 아님.
- 롤백: 두 파일의 PortVisual 도형, 운전 상태 표시 및 RefreshPortMotion 분기를 되돌림.

## [2026-11-10 00:09 KST] TASK-SJ-LOCALIZATION-SIM-LOG — 언어 전환·시뮬레이션·운전 로그
- 비교 기준: C:\ProjectDH\DHLS_Sync\DHLS_Sync의 MainMenuView, MainDashboardView, SimulationSequence, MockPlant, AuditLog.
- 수정 범위: Localization.h 및 화면 XAML, MainMenuView/MainDashboardView, AuditLog.h, EquipmentControlService.cpp, ControlLogic/Sequence.
- 변경: 고정 라벨과 동적 잭 계측·웨지·모드의 영한 전환 보완. 메인 메뉴에서 기존 개발자 인증으로 모의 장비 진입. 모의 자동 변위 운전에 감속 거리·최저 속도·정지 허용오차 적용. 일시정지 재개는 엔진이 보존한 원래 목표 사용.
- 로그: 화면에서 시뮬레이션 이벤트 최근 80건 표시. 비동기 날짜별 로그에 모드, 세션, 명령·거부, 펌프 IP 결과, 웨지 및 시퀀스 상태 추가. 실제 PLC 송신 경로는 변경하지 않음.
- 검증: 원본 XAML XML 구문, 한글 표시 속성 190개 등록, 메인 메뉴 5개 진입 연결 확인. 빌드·PC 실행·장비 검증 미실시.
- 미확정: 교정 ZERO/SPAN 절차는 DHLS_Sync에도 미정의. 일부 장비 진단 문구는 원문 유지.
- 롤백: 위 수정 범위의 언어 등록, 모의 감속, 시뮬레이션 진입 및 로그 변경을 되돌림.

## [2026-11-10] TASK-SJ-DASHBOARD-CLOCK — 대시보드 현재 시각과 운전 경과
- 수정 파일: MainDashboardView.xaml, MainDashboardView.xaml.cpp, MainDashboardView.xaml.h, Localization.h.
- 변경: 모드명 행 오른쪽에 현재 시각과 운전 경과를 구분 표시. 모드명은 중앙 유지. 경과는 운전 시작 시 0부터 표시하고 정지 시 고정하며 다음 시작 때 다시 측정.
- 검증: XAML XML 구문과 기존 UI 타이머 및 운전 상태 연결을 정적으로 확인. 빌드 및 PC 실행 미실시.
- 롤백: 모드명 행, 시간 표시, 경과 상태 및 추가 번역만 되돌림.

## [2026-11-10] TASK-SJ-DASHBOARD-WORK-TIME-LABEL — 작업 시간 표기
- 수정 파일: MainDashboardView.xaml, Localization.h.
- 변경: 운전 경과 라벨을 작업 시간(Work time)으로 변경.
- 검증: XAML XML 구문과 번역 키 일치 확인. 빌드 및 실행 미실시.
- 롤백: 두 파일의 라벨과 번역만 되돌림.

## [2026-11-10] TASK-SJ-DASHBOARD-LOG-TIME — 대시보드 로그 시각과 운전 시간 결과
- 수정 파일: MainDashboardView.xaml, MainDashboardView.xaml.cpp, MainDashboardView.xaml.h, Localization.h, AuditLog.h.
- 변경: 대시보드 로그 항목 앞에 현지 시각 HH:mm:ss.fff 추가. 운전 시간 라벨을 Run time과 연결하고 운전 상태 종료 시 경과 시간을 모드별 로그 파일과 UI에 기록.
- 검증: XAML XML과 시간 포맷, 운전 상태 전환 및 로그 경로 정적 확인. 빌드와 실행 미실시.
- 롤백: 로그 시각 및 운전 결과 기록과 라벨 변경을 되돌림.

## [2026-11-10] TASK-SJ-READABLE-AUDIT — 작업자용 로그 형식
- 수정 파일: AuditLog.h, EquipmentControlService.cpp, SimulationSequence.h, MainDashboardView.xaml.cpp, MainMenuView.xaml.cpp.
- 변경: 밀리초 시각, 이벤트 설명과 원본 코드, 상세 상태, 작업 정보를 구분해 날짜별 파일에 기록. PLC 통신·읽기·PC 제어·펌프·고장·비상 상태와 명령 방향·목표·포트를 명시하고 모의 시퀀스 상태 및 운전 시간 결과를 보완.
- 검증: 파일 및 UI 이벤트 조합과 기록 조건을 소스에서 확인. 빌드, 파일 생성 및 실행 검증 미실시.
- 롤백: 로그 포맷과 위 이벤트의 상세 메시지 변경만 되돌림.

## [2026-11-10] TASK-SJ-LOG-COLUMNS — 대시보드 로그 시간과 메시지 열
- 수정 파일: MainDashboardView.xaml, MainDashboardView.xaml.cpp, Localization.h.
- 변경: 시각과 메시지 제목, 고정 폭 시각 열 및 구분선 적용. 두 열 글자 크기 13으로 통일하고 긴 메시지는 같은 열 안에서 줄바꿈.
- 검증: XAML XML 구문 및 로그 행 연결 확인. 빌드 및 실행 미실시.
- 롤백: 로그 열 UI와 행 생성 코드 및 제목 번역만 되돌림.

## [2026-11-10] TASK-SJ-SET-ZONE-HEADER — SET 영역에 모드명과 시간 배치
- 수정 파일: MainDashboardView.xaml.
- 변경: SET 카드와 공통 헤더를 한 테두리 안으로 묶고 모드명과 두 시계를 영역 상단에 배치.
- 검증: XAML XML 구문과 SET 영역 하위 요소 확인. 빌드 및 실행 미실시.
- 롤백: SET 영역과 헤더의 XAML 배치만 되돌림.

## [2026-11-10] TASK-SJ-MODE-TIME-POSITION — 기존 모드명 위치와 우측 시간
- 수정 파일: MainDashboardView.xaml.
- 변경: 공통 헤더를 제거하고 원래 대시보드 상단 모드명 위치를 복원. 현재 시각 및 운전 시간을 같은 높이의 우측 끝에 배치.
- 검증: XAML XML 구문 및 요소 배치 확인. 빌드 및 실행 미실시.
- 롤백: 대시보드 상단 XAML 배치만 되돌림.

## [2026-11-10] TASK-SJ-PARALLEL-CLOCK-PLC-MAP — 2라인 병렬 시각 위치와 PLC 선택
- 수정 파일: MainDashboardView.xaml, MainDashboardView.xaml.cpp, PlcMapMonitor.h.
- 변경: 2라인 병렬 배치에서만 SET 2 ERROR 바로 왼쪽으로 시각 표시 이동. PLC 맵의 장치 콤보박스를 제거하고 여러 PLC는 가로 선택 버튼으로 전환.
- 검증: XAML XML, 배치 전환 시 재부착 및 PLC 선택 연결을 정적으로 확인. 빌드와 실행 미실시.
- 롤백: 시각 컨테이너 이동 및 PLC 맵의 선택 UI 변경만 되돌림.

## [2026-11-10] TASK-SJ-LOG-MESSAGE-AREA — 로그 안내 문구 정렬
- 수정 파일: MainDashboardView.xaml.
- 변경: 메인·모드 안내를 메시지 열로 옮기고 글자 크기를 로그와 같은 13으로 설정. 안내 행의 시각 열은 비워 둠.
- 검증: XAML XML 구문과 안내 요소의 열 지정 확인. 빌드 및 실행 미실시.
- 롤백: 로그 안내 행의 XAML 배치만 되돌림.

## [2026-11-10] TASK-SJ-LOG-MODE-HEADER — 메시지 제목에 운전 모드 표시
- 수정 파일: MainDashboardView.xaml, MainDashboardView.xaml.cpp.
- 변경: 로그 안내 행을 제거하고 메시지 열 제목에 현재 모드명 표시.
- 검증: XAML XML 구문과 모드 갱신 경로 확인. 빌드와 실행 미실시.
- 롤백: 메시지 열 제목과 이전 로그 안내 행을 복원.

## [2026-11-10] TASK-SJ-MAIN-MENU-CLOCK — 메인 메뉴 현재 시각
- 수정 파일: MainMenuView.xaml, MainMenuView.xaml.cpp, MainMenuView.xaml.h.
- 변경: 상단 바 아래 오른쪽 끝에 PC 현지 시각을 초 단위로 표시. 화면을 떠나면 타이머 종료.
- 검증: XAML XML 구문과 Loaded/Unloaded 경로 확인. 빌드와 실행 미실시.
- 롤백: 메인 메뉴 시각 표시와 타이머만 되돌림.

## [2026-11-10] TASK-SJ-FOOTER-BUILD-FIX — 속도 선택기 제거 후 참조 정리
- 수정 파일: MainDashboardView.xaml.cpp, MainDashboardView.xaml.h, ControlSettingsView.Deceleration.cpp.
- 변경: XAML에서 제거된 FooterSpeedSetSelect의 C++ 참조를 정리하고 하단 속도 표시 및 조작은 기존 기본 SET 1에 연결. 압력 그래프 샘플 변수 이름을 변경해 C4456 원인 제거.
- 검증: 소스에서 남은 선택기 참조가 없음을 확인. 빌드 및 실행 미실시.
- 롤백: 위 파일의 선택기 연동과 샘플 변수 변경만 되돌림.

## [2026-11-10] TASK-SJ-MOCK-STRAND-CYCLE — 모의 상승·하강 준비 및 반복 운전
- 변경: 모의 동조에 제어 설정의 동조·허용 편차를 연결하고 무효 시 10/20 mm, 재개는 동조 편차의 절반으로 적용. 상승·하강 준비, 목표거리와 회차 거리 분리, 검증된 모의 회차 누적, 가감속, 저속 하중전이, 클램프 상태 및 로그 연결.
- 모의 전제: 안전 위치 30 mm, 허용 2 mm, 전이 25 mm, 가속 20 mm, 전이 속도 기준의 25%. 감속 및 최소 속도는 모델 설정. 첫 모의 준비는 정지·유효 센서 확인 후 모의 시작 위치 초기화. 실제 PLC에 적용하지 않음.
- 검증: XAML XML 구문, 900/950/5000 mm 분할 및 양방향 구간 경계를 독립 계산으로 확인. 빌드, 모의 엔진 통합 실행, GUI 및 PLC 검증 미실시.
- 한계: 하중지지는 Mock 상태로만 표현. 물리 Anchor/하중 지지 검증 및 현장 기준은 별도 확정 필요.
- 롤백: 모의 준비 버튼, 진단 요청/스냅샷 필드, 모의 FSM·속도 프로파일 및 번역 변경만 되돌림.

## [2026-11-10] TASK-SJ-LIVE-LOG-MOCK-VERIFY — 최신 로그와 모의 운전 검증
- 수정: MainDashboardView.xaml/.h/.cpp에서 대시보드 진입 후 이벤트를 시간순으로 표시하고 최신 행으로 자동 스크롤. 주요 단계, 시퀀스 상태, 운전 결과 굵게 표시.
- 수정: SequenceEngine.h, MockPlant.h, SimulationSequence.h에서 모의 장비용 절대 위치 세그먼트와 계획 피드백만 허용하고 모의 압력 운전에도 동일한 동조 설정 적용.
- 빌드: Debug x64 Main Bin 성공. C1902 및 컴파일 메모리 부족을 명령 한정 /Z7 /utf-8 /MP1로 우회. 프로젝트 빌드 속성은 수정하지 않음.
- 모의 엔진 시험: 상승/하강 각각 두 회차 55 mm, 최종 클램프 닫힘 및 정지 확인. 수동 펌프·양방향·정지, 압력 목표 도달, 준비 전 차단·일시정지·재개·비상정지·리셋 통과. 교정 백엔드는 미구현 응답 확인.
- GUI: 새 실행 파일을 시작해 초기 모델 선택 창을 확인. Windows 잠금으로 모드별 화면 조작 및 스크롤·굵기 관찰은 대기.
- 미구현: 텐셔닝 화면의 준비/시작 핸들러와 교정 ZERO/SPAN 연결.
- 롤백: 로그 갱신과 단계 분류, 모의 절대 위치 계획 및 모의 동조 설정 변경만 되돌림.

## [2026-11-10] TASK-SJ-LIVE-LOG-MOCK-VERIFY — 잠금 해제 후 GUI 확인
- Main Bin Debug x64의 test1 시뮬레이션에서 4개 잭 선택, 준비, 목표 55 mm/동작거리 50 mm 상승을 실행. 50+5 mm 두 회차 및 종료 시 양측 클램프 닫힘을 로그에서 확인.
- 운전 중 새 로그가 하단에 표시되며 주요 시퀀스 단계가 굵게 렌더링되는 것을 화면에서 확인. 운전 종료 결과는 00:00:29.488.
- 다른 모드로 이동하려면 시퀀스 RESET의 작업자 인증 필요. 인증 창을 연 상태로 대기.

## [2026-11-10] TASK-SJ-LIVE-LOG-MOCK-VERIFY — 5개 모드 GUI 확인 및 하강 재준비
- 상승: test1, 4개 모의 잭, 50+5 mm 완료 및 양측 클램프 닫힘 확인. 새 로그 자동 스크롤과 굵은 단계 확인.
- 수동: 상부 웨지 열림·닫힘 피드백, 상승/하강 누름 및 놓을 때 정지 요청 확인.
- 텐셔닝/교정: 실제 UI에서 텐셔닝 시작과 교정 압력·변위 영점 조작 시 시퀀스 명령이 발생하지 않음. 연결 미구현 상태.
- 수정: SimulationSequence.h에서 RESET 성공 시 모의 위치 초기화 이력도 지우도록 변경. 동일 엔진에서 상승 후 RESET, 하강 READY 회귀 시험 통과.
- 빌드: Main Bin Debug x64 재빌드 성공. 수정본에서 하강 4개 모의 잭 50+5 mm 완료, 양측 클램프 닫힘 및 운전 시간 00:01:14.110 확인.
- 실행 상태: 하강 완료 화면을 유지. 모의 장비 사용.

## [2026-11-10] TASK-SJ-STEP-LOG-INTERLOCK — 단계 로그와 작업 중 변경 차단
- 수정: SimulationSequence.h, SequenceEngine.h, EquipmentControlService.h, MainDashboardView.xaml.cpp/.h, Localization.h.
- 변경: 모의 자동 작업 및 일시정지 중 펌프·잭 선택·웨지·거리 설정 변경을 차단. 상승 단계 로그를 양 클램프 확인, 이동 명령 수락, 모의 Anchor 점검, 전이 정지 및 하부 지지 점검, 상부 열림, 복귀 수락, 재체결에 연결. 단계와 결과만 굵게 표시.
- 검증: Main Bin 내부 모의 엔진 재컴파일. 상승/하강 55 mm 최종 정지 및 양측 클램프 닫힘, 운전 중 선택·일시정지 중 펌프 변경 거부, 재개·비상정지·리셋 통과. WinUI 앱 재빌드 및 화면 재실행 미실시.
- 운영 미확정: 실제 Anchor 완료 신호, 하중 지지 검증, 펌프/잭 제어권과 안전 인터록, 위치/속도 허용 조건은 현장 승인 필요.
- 롤백: 모의 단계의 명칭·이벤트 분류·피드백 조건 및 작업 변경 차단만 되돌림.

## [2026-11-10] TASK-SJ-WORK-HISTORY-LOG — 작업 로그와 이력 로그 분리
- 수정: AuditLog.h, MainDashboardView.xaml/.h/.cpp, SimulationSequence.h.
- 변경: 작업 단계·운전 피드백·정지·결과는 Work_YYYYMMDD.log 및 대시보드에 표시. UI 조작·명령 요청/거부·설정 변경 및 일반 진단은 History_YYYYMMDD.log로 기록. 기존 혼합 파일 보존.
- 검증: 독립 AuditLog 시험에서 작업 2건, 이력 3건의 파일 분리 및 작업만 대시보드 공급 확인. XAML XML 구문 확인. 앱 전체 빌드 및 GUI 실행 미실시.
- 롤백: 파일 분류와 대시보드 상태 행 제거, 모의 운전 피드백 코드 변경을 되돌림.

## [2026-11-10] TASK-SJ-STOP-HOLD — 3초 완전 정지와 RESET 전 재시작 차단
- 수정: MainDashboardView.xaml.cpp/.h, Localization.h, EquipmentControlService.cpp, SimulationSequence.h.
- 변경: 정지 확인된 일시정지 상태에서 일시정지 버튼을 3초 유지할 때만 완전 정지 요청. 짧게 다시 누르면 상태 유지. 완전 정지는 시작·수동 방향 버튼과 모의 엔진의 시작·준비를 RESET 성공까지 차단.
- 검증: 모의 엔진에서 반복 일시정지, 완전 정지, 재시작·준비 거부, RESET 후 준비 성공을 실행 확인. WinUI 전체 빌드 및 포인터 조작 미실시.
- 롤백: 버튼 홀드 처리, 완전 정지 차단 및 안내 변경만 되돌림.

## [2026-11-10] TASK-SJ-LIFT-FINAL-HOLD — 상승 목표 완료 및 정지 표기
- 수정: MainDashboardView.xaml.cpp, Localization.h, EquipmentControlService.h, SimulationSequence.h.
- 변경: 완전 정지 상태의 버튼은 정지로 표시. 모의 상승 마지막 회차는 정지·위치·모의 상부 지지가 확인되면 양 클램프 닫힘 상태에서 위치 유지. 중간 복귀만 수행. 목표 도달 뒤 시작 색을 기본색으로 복원하고 목표 도달·일시정지 상태 표시. 누적 거리는 모의 하중 이동량 사용.
- 검증: Main Bin 내 모의 엔진에서 목표 200 mm, 동작 50 mm, 상승 4회 및 중간 복귀 3회, 최종 79.5188 mm 유지 확인. WinUI 앱 빌드 및 UI 실행 미실시.
- 롤백: 완료 상태, 상승 최종 유지, 정지 라벨과 누적 거리 표시만 되돌림.

## [2026-11-11] TASK-SJ-STROKE-REMAINING-DISPLAY — 회차 동작거리와 남은 거리
- 수정 파일은 MainDashboardView.xaml/.h/.cpp, EquipmentControlService.h/.cpp, EquipmentSession.h, SimulationSequence.h.
- 잭 카드의 상부 유닛 표시 범위를 회차 동작거리로 정규화. 모의 자동 운전은 본동작 중 그룹 이동 피드백으로 누적거리와 목표 잔여거리를 갱신. 목표 입력값은 별도로 보존.
- 운전 중 거리, 누적거리 영점, 준비, 운전 수치 입력을 잠금. 완료 후 누적거리 영점은 표시값에 적용.
- PLC 화면 변위 환산은 설정된 센서 A/D 스케일을 우선 사용하고 값이 없으면 PLC A/D 분해능을 사용. 운전 판정의 교정 경로는 별도 유지.
- 검증은 200 mm 모의 운전, 회차 중간 누적, 최종 위치 유지, 누적거리 영점, 4000/16000 A/D 환산 및 XAML 구문을 확인. WinUI 전체 빌드와 실장비 확인은 미실시.
- 롤백은 거리 표시·입력 잠금·PLC 표시 환산 변경을 되돌림.

## [2026-11-11] TASK-SJ-PAUSED-STROKE-LOG — 일시정지 동작거리와 간결한 UI 로그
- 변경 파일은 MainDashboardView.xaml.cpp/.h, AuditLog.h, Localization.h, EquipmentControlService.h, SimulationSequence.h.
- 모의 자동운전의 일시정지 중 동작거리 편집을 허용. 재개 시 현재 세그먼트는 기존 목표를 유지하고 변경값은 다음 회차부터 반영. 하강 시 필요한 경우 다음 회차 준비 위치를 다시 잡음.
- UI는 시작 조건 확인 1회, 주요 단계, 정지·결과 및 경고를 표시. 내부 운전 ID와 상세 피드백은 파일에 보존.
- 모의 엔진 시험에서 35 mm 회차 도중 일시정지 후 10 mm로 변경해 35+10+10 mm 완료, UI 시작 확인 1회 및 상세 상태 제외 확인. WinUI 전체 빌드와 실행 미실시.

## [2026-11-11] TASK-SJ-PLC-MODE-HANDOFF — 모의 및 실제 PLC 연결 전환
- 메인 메뉴에서 시뮬레이션을 끄면 현재 모델의 PLC 설정으로 연결을 시작하고 대시보드에서 해당 세션을 재사용. 켜면 모의 세션으로 전환.
- AGENTS.md에 모의·실제 자동운전의 공통 조건과 입출력 경계 원칙을 기록. 실제 PLC 자동운전 경로 통합은 남은 작업.
- 가짜 PLC 전송을 사용한 세션 전환과 기존 전송 종료 시험 통과. WinUI 전체 빌드, 앱 실행, 실제 PLC 연결은 미실시.
- 롤백은 메인 메뉴의 연결 전환과 상태 표시, 세션 시험 변경을 되돌림.

## [2026-11-11] TASK-SJ-PLC-MAP-RUN — 운전 중 PLC 맵 열기
- 대시보드의 PLC 맵 버튼 및 열기 동작을 운전 중 허용. 읽기 전용 창은 기존 PLC 세션의 스냅샷을 계속 사용. 화면 잠금 동작 유지.
- 버튼 활성화, 운전 중 창 열기 및 닫기 경로를 소스에서 확인. 빌드와 실장비 연결 미실시.

## [2026-11-11] TASK-SJ-PUMP-START-VERIFY — 잭 선택 및 펌프 조건 검증
- 활성화 및 펌프 ON 잭만 준비·시작 요청에 포함. 모의 엔진은 등록된 펌프가 OFF인 요청을 거부.
- 메인 Bin Debug x64 재빌드 성공. 명령줄에서 임시로 /Z7을 적용해 C1902를 우회. 프로젝트 빌드 설정 변경 없음.
- test1 모의 상승에서 펌프 OFF 시작 경고 확인. 선택된 두 잭 중 펌프 ON인 70T_1만 50 mm 누적 이동, OFF인 70T_2 및 나머지 잭은 0 mm 유지.
- 펌프 대상 시험 통과. 전체 모의 시험의 다중 PLC 동조 완료 거리 검사는 실패. 실제 PLC 연결 및 동작 미검증.

## [2026-11-11] TASK-SJ-MOCK-SYNC-VERIFY — 다중 PLC 동조 및 모드 점검
- 다중 PLC 모의 시험에서 실제 동조 설정 필드와 위치 허용오차를 사용하도록 수정. 운전 로직의 허용오차 변경 없음.
- 20 mm 목표에 세 잭이 19.5764, 19.5014, 19.5314 mm에서 정지. 최대 편차 0.661179 mm, 앞선 잭의 동조 대기 및 미선택 잭 정지 확인. 전체 모의 회귀 시험 통과.
- 수동 상승·하강·정지 및 범용 압력 제어 완료 확인. 텐셔닝 준비·시작, Calibration 영점 버튼은 전용 동작 연결이 없는 상태 확인. 실제 PLC 및 하강 운전 메뉴 검증 범위 제외.

## [2026-11-11] TASK-SJ-MOCK-LOWER-TENSION-CAL — 하강 시험과 모의 텐셔닝·교정 연결
- 모의 하강에서 두 잭의 35+20 mm 이동 및 최종 정지·양쪽 클램프 닫힘 확인.
- 모의 텐셔닝 준비·시작을 연결. 선택된 이동거리 한계와 잭별 사용하중 비율을 적용하고 준비 이후 잭·설정 변경 시 재준비 요구. 목표 하중 도달 및 이동거리 한계 정지를 각각 시험.
- 모의 Calibration의 압력 0점·현재변위영점은 표시 오프셋에만 적용. 물리 제어 위치, 원본 압력 및 하중 보존을 시험.
- 실제 텐셔닝 웨지 시퀀스와 교정 PLC 쓰기·저장 절차는 미검증. 모의 전용으로 연결.

## [2026-11-11] TASK-SJ-PAUSED-PUMP-MENU — 상승·하강 일시정지 펌프 전환
- 정지가 확인된 모의 일시정지에서 카드의 펌프 메뉴를 열 수 있도록 수정. 카드 선택과 전체 선택 잠금 유지.
- 상승·하강 모의 시험에서 운전 중 펌프 전환 차단, 일시정지 중 OFF/ON, 위치·클램프 유지 및 OFF 상태의 재개 차단을 확인.
- 수동·텐셔닝 모의 시험에서 운전 중 전환 차단, 정지 확인 후 OFF/ON 허용, OFF 상태의 시작 차단을 확인. Calibration은 정지 중 전환 허용, 화면 잠금 중 전환 차단, 영점 조작 후 펌프 OFF 유지를 확인.
- AGENTS.md에 운전 조건 변경 시 상승·하강·수동·텐셔닝·Calibration을 모두 검증하도록 기록. 이번 다섯 모드 결과는 모의 엔진 기준이며 앱 화면과 실 PLC는 미검증.
