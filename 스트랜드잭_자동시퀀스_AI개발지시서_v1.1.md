# 스트랜드잭 상승·하강 자동 반복 시퀀스 — AI 개발 실행 지시서

> **이 파일 전체를 AI 코딩 에이전트(Cursor, Claude Code, ChatGPT, Codex 등)에게 작업 지시로 전달한다.**  
> 문서 ID: `SJ-AI-DEV-001` / 버전: `v1.1` / 작성일: `2026-11-08`  
> 기초 자료: 사용자 지정 Lifting/Lowering 순서, `스트랜드잭 시퀀스_20260326.xlsx`, `스트랜드잭_상승하강_반복시퀀스_개발작업지시서_v1.1.md`  
> 목표: **상승·하강 자동 반복 시퀀스 + 전체 목표거리 제어 + 구간별 가감속 제어 + 하중전이/Anchor/클램프 인터록 + 시험 가능한 C++ 구현**


> **v1.1 추가:** 본 문서의 **「부록 A. AI 개발 공통 작업 규칙」**을 모든 구현 단계에 의무 적용한다. 특히 **Main Bin 고정 빌드**, `HISTORY.md` 기록, **PC/GUI 최종 검증 집중**, AI Usage 절감, 기존 기능 보호를 최우선 운영 규칙으로 사용한다.

---

## 0. AI에게 부여하는 역할 및 절대 규칙

너는 **산업용 유압 스트랜드잭의 제어 소프트웨어 설계·구현 담당 수석 엔지니어**다. 아래 요구사항을 단순 요약하지 말고 **설계, 실제 코드, 모의시험, 테스트 결과, 요구사항 추적표까지** 작성한다.

### 반드시 지켜야 할 원칙

1. **기존 프로그램 보호:** 프로젝트가 제공되면 먼저 구조·제어권·PLC 인터페이스·기존 상태머신을 파악한다. 기존 기능을 임의로 삭제하거나 기존 PLC 로직을 변경하지 않는다. 독립 모듈 중심으로 통합한다.
2. **원문과 제안을 구분:** 사용자가 지정한 순서는 원문 그대로 별도 기록한다. 안전상 변경이 필요한 부분은 `제안/승인 대기`로 표시하고, 실제 출력 명령의 실행 경로와 분리한다.
3. **안전 우선:** 클램프 열림이나 Anchor OPEN은 **상대편의 실제 하중 지지 검증 전 절대 출력하지 않는다.** 하중 지지를 증명할 피드백이 없으면 안전이 확보된 것으로 가정하지 않는다.
4. **미확정 사항은 가짜로 채우지 말 것:** Anchor의 정확한 기능, 하중전이 거리 적용 방식, 위치/압력/하중 피드백, PLC D 레지스터 맵, 안전 회로 등 미확정 항목을 임의로 결정하지 않는다. `BLOCKED` 또는 `PENDING_...`로 표기한다.
5. **실장비 구동 금지:** 안전 승인·I/O 검증 전 생성 코드는 `SIMULATION` 기본 모드로 작성한다. 실제 밸브·유압·클램프 출력은 명시적 승인된 인터페이스가 붙기 전까지 비활성화한다.
6. **안전 기능은 SW만으로 대체 불가:** 비상정지·중력하중 유지·파열/압력 이상 대응은 위험성 평가에 따라 적합한 하드웨어 및 안전 관련 제어기에서 처리한다. PC 통신/화면이 안전 기능의 단일 수단이 되어서는 안 된다.
7. **목표거리와 실린더 위치를 혼동하지 말 것:** `VerifiedLoadTravelMm`는 하중의 순이동량, `CylinderPositionMm`는 실린더 변위다. Retracting/Extending을 하중 이동거리로 누적하지 않는다.
8. **동작거리와 25mm 하중전이를 혼합하지 말 것:** 본동작·복귀에는 거리 기반 가감속을 사용하고, Anchor 전이는 승인된 별도 저속 프로파일을 사용한다.
9. **실행 전 확인이 필요한 부분을 남긴 채로도 작업 진행:** 안전 불명 사항으로 코딩을 전부 중단하지 말고, 우선 시뮬레이션 가능한 상태머신·계산·검증 코드를 구현한다. 실제 출력 경로만 차단한다.
10. **출력은 설명만으로 끝내지 말 것:** 요청한 실제 소스 파일, 빌드/테스트 방법, 테스트 결과 및 미확정 목록을 제공한다. 빌드·테스트를 실행하지 못했다면 `미실행`으로 명시한다.

### 개발 절차(단계별 완료 기준 필수)

`요구사항 분석 → 완성 목표 정의 → 설계 → 구현 → 검증 → 품질 개선`

각 단계의 산출물과 완료 기준을 남긴다. 확정되지 않은 정보는 `[확정]`, `[조건부]`, `[미확정]`으로 구분한다.

---

## 1. 프로젝트 범위 및 기술 환경

### 필수 기능

- Lifting Ready / Start / 자동 반복 / 정상 완료
- Lowering Ready / Start / 자동 반복 / 정상 완료
- 목표 순이동거리(`TargetTravelMm`)와 매 회차 계획 순이동거리(`CycleTravelMm`) 처리
- 마지막 부분 스트로크(예: 목표 5,000mm, 회당 300mm → 16회×300mm + 200mm)
- 각 이동 세그먼트 ACCEL / CRUISE / DECEL / SETTLING / DONE 상태와 정지 검증
- 25mm Anchor 하중전이 단계와 일반 이동 세그먼트 분리
- 상·하부 클램프, Anchor, 하중지지 검증 인터록 및 Step Timeout
- 활성 잭 그룹 완료/편차/데이터 신선도 감시
- Pause / Normal Stop / Fault Lock / Emergency Stop / Recover 상태 분리
- WinUI 3 화면 바인딩용 읽기 전용 상태 DTO 및 이벤트·로그 생성
- 시뮬레이터와 자동 테스트

### 권장 구현 언어 및 통합 조건

- 권장 언어: **C++ (기존 솔루션 표준 및 컴파일러에 맞춤)**
- UI가 WinUI 3이면 **WinRT UI와 실시간 제어 로직 분리**. UI 스레드에서 제어 루프나 Blocking I/O 실행 금지.
- 기존 DHLS/LS XGB 환경을 사용하는 경우: 먼저 **실제 연결 환경을 확인**한다. LS XGB / XGT TCP / D 영역 읽기·쓰기만 허용되는 기존 프로젝트라면 그 제약을 유지한다. **PLC 주소, 비트 의미, 명령 패킷은 프로젝트 근거가 없으면 생성하지 않는다.**
- 기존 PLC 변경이 불가능할 때: 장치에서 제공되는 인터록/속도제어 기능의 범위를 분석하고, 안전을 보장할 수 없는 제어 책임을 PC로 옮기지 않는다.
- 저장소/소스가 입력되지 않았으면 **컴파일 가능한 독립 C++ 코어 + Mock Gateway + 예제 실행/테스트 프로젝트**를 먼저 작성한다. 특정 WinUI 3 파일의 존재를 가정하지 않는다.

---

## 2. 원본 시퀀스 (사용자 지시 보존)

### 2.1 상승 Lifting — 원문

```text
Lifting Ready
Lifting Start
 1. 상부클램프 닫힘, 하부 클램프 닫힘
 2. Lifting (동작거리만큼)
 3. 상부 Anchor check
 4. 상부클램프 열림, 하부 클램프 닫힘
 5. Retracting
 6. 상부클램프 닫힘, 하부 클램프 닫힘
 7. Lifting (동작거리만큼)
 8. 상부 Anchor check
 9. 상부클램프 열림, 하부 클램프 닫힘
10. Retracting
11. 상부클램프 닫힘, 하부 클램프 닫힘
12. Lifting (동작거리만큼)
13. Stop
```

**반복 개념:** `양 클램프 닫힘 → Lifting → 상부 Anchor check → 안전한 하중지지 검증 → 상부 OPEN / 하부 CLOSED → Retracting → 상부 재체결`을 목표거리까지 반복한다.

### 2.2 하강 Lowering — 원문

```text
Lowering Ready
Lowering Start
 1. 상부클램프 열림, 하부 클램프 닫힘
 2. (원문 번호 없음)
 3. 하부 Anchor open
 4. 하부 클램프 열림
 5. 상부클램프 닫힘, 하부 클램프 열림
 6. Lowering
 7. 상부클램프 닫힘, 하부 클램프 닫힘
 8. 상부 Anchor open
 9. 상부클램프 열림
10. Extending
11. 상부클램프 닫힘, 하부 클램프 닫힘
12. 하부 Anchor open
13. 하부 클램프 열림
14. Lowering
15. Stop
```

**안전 충돌 — 구현 차단:** 원문대로라면 상부 OPEN 상태에서 하부 Anchor/클램프까지 OPEN이 발생할 수 있어 하중 지지 상실 위험이 있다. **상부 그립의 유효한 하중 지지를 확보한 뒤에만 하부 해제를 허용**하는 개선안을 `REQUIRES_APPROVAL`로 별도 정의한다. 원문과 개선안을 섞어서 조용히 변경하면 안 된다.

---

## 3. 시퀀스 상태머신 설계 지시

### 3.1 상위 Job FSM

```text
IDLE
  └─ READY_CHECK ──[전제조건 통과]──> READY
                                        └─ START ──> RUNNING
                                                      ├─ LIFT_CYCLE
                                                      └─ LOWER_CYCLE
RUNNING ──[목표 순이동 검증]──> FINISH_HOLD ──[하중유지 검증]──> COMPLETE
RUNNING ──[Pause 요청]──────> PAUSING ──> PAUSED_HOLD
RUNNING/READY/PAUSED_HOLD ──[Fault]──> FAULT_LOCK
ANY APPLICABLE STATE ──[E-Stop]──────> EMERGENCY_STOP
```

- 동작 중 모드 전환 및 레시피 수정 차단.
- `FAULT_LOCK`, `EMERGENCY_STOP`, 전원 재기동 후 자동 Step 점프/자동 재시작 금지.
- `READY_CHECK`는 위치, 압력, 하중, 온전한 센서 값, 그룹 동기, 클램프/Anchor, 현재 작업 상태, 목표 범위, 기계적 스트로크를 확인.
- 모든 Step은 `EntryGuard`, `OneShotCommand`, `Ack`, `FeedbackGuard`, `Timeout`, `CompletionGuard`, `FaultAction`을 가진다.

### 3.2 상승 Lifting 상세 FSM

| Step ID | 기능 / 출력(시뮬레이션) | 다음 Step 조건 |
|---|---|---|
| `L_READY` | 상승 Ready 검증 | 전체 활성 잭 준비, 이동 공간, 하중지지 검증 |
| `L_BOTH_CLOSED` | 상·하부 CLOSE 확인/명령 | 양 클램프 닫힘 피드백 및 장비별 지지 조건 |
| `L_MOVE` | `CycleTravelMm`만큼 Lifting 세그먼트 | 실제 순상승량, 목표 정착, 그룹 편차 검증 |
| `L_UPPER_ANCHOR_CHECK` | 원문 상부 Anchor check | 승인된 매크로/센서 기준 성공 |
| `L_TRANSFER_TO_LOWER` | 하부 하중지지 증명 | `LowerSupportsLoad = true` (승인된 판정식) |
| `L_OPEN_UPPER` | 상부 OPEN, 하부 CLOSE 유지 | 상부 OPEN 피드백 + 하부 지지 유지 |
| `L_RETRACT` | 실린더 복귀 이동(순하중 거리 미누적) | 복귀 위치·정착·하부 지지 유지 |
| `L_RECLOSE_UPPER` | 상부 CLOSE 후 반복 준비 | 양쪽 CLOSE 및 지지/센서 조건 |
| `L_FINISH_HOLD` | 최종 하중유지 확인 | `VerifiedLoadTravelMm` 목표 만족 + 하중유지 확인 |

- `L_MOVE` 직후 하중 순이동 완료와 잔여거리를 평가한다.
- **목표 도달 시 불필요한 `L_RETRACT`를 실행하지 않는다.** 종료 전 클램프/Anchor의 승인된 안전 유지 상태는 별도로 충족시킨다.
- 하부 클램프 CLOSED 상태에서 실제 상승이 가능한 한 방향 통과형 웨지인지 [미확정]. 원문에 없는 하부 OPEN을 AI가 추가하지 않는다.
- 엑셀의 `330→305mm` 하중전이가 `상부 Anchor check` 매크로에 포함되는지 [미확정]. 중복 이동 금지.

### 3.3 하강 Lowering 상세 FSM — 안전 변경안, 승인 필요

| Step ID | 기능 / 출력(시뮬레이션) | 다음 Step 조건 |
|---|---|---|
| `R_READY` | 하강 Ready 검증 | 하강용 시작 위치, 지지/유압/센서 정상 |
| `R_HOLD_LOWER` | 초기 하부 지지 확인 | `LowerSupportsLoad = true` |
| `R_CLAMP_UPPER` | **하부 해제 전 상부 CLOSE 및 하중전이** | `UpperSupportsLoad = true`, 전이 완료 |
| `R_LOWER_ANCHOR_OPEN` | 하부 Anchor OPEN 매크로 | 상부 지지 유지 + Anchor 완료 피드백 |
| `R_OPEN_LOWER` | 하부 OPEN | 상부 지지 유지, 하부 OPEN 확인 |
| `R_MOVE` | 계획된 순하강거리 대응 Lowering 이동 | 실제 순하강량, 정착, 그룹 편차 검증 |
| `R_CLOSE_LOWER` | 하부 CLOSE | 닫힘 확인 + 하부 지지 확보 |
| `R_UPPER_ANCHOR_OPEN` | 상부 Anchor OPEN 매크로 | 하부 지지 유지 + Anchor 완료 피드백 |
| `R_OPEN_UPPER` | 상부 OPEN | 하부 지지 유지, 상부 OPEN 확인 |
| `R_EXTEND` | 실린더 재연장(순하중 거리 미누적) | 목표 연장 위치, 정착, 하부 지지 유지 |
| `R_RECLOSE_UPPER` | 상부 CLOSE 후 반복 준비 | 양 클램프 피드백, 상부 지지 조건 |
| `R_FINISH_HOLD` | 최종 하중유지 확인 | 목표 거리 달성, 종료 하중지지 확립 |

- `R_MOVE` 직후 실제 순하강량과 목표 달성 여부를 평가한다.
- 목표 도달 시 **후속 Extending은 수행하지 않고**, 그 자리에서 종료용 안전 하중지지/클램프 확립 절차만 허용한다. 즉, 최종 `R_CLOSE_LOWER` 등은 생략 대상이 아니다.
- `R_CLAMP_UPPER` 등 순서 변경은 엔지니어 승인 전 하드웨어 실행 금지. 승인 전 `Mock`에서만 동작.
- `Anchor OPEN`이 그립 자체 OPEN, 25mm 하중전이 매크로, 별도 유압 동작 중 무엇인지 확정 필요.

---

## 4. 목표 이동거리 반복 로직 (핵심 요구사항)

### 4.1 변수

```cpp
struct TravelJob {
    double targetTravelMm;        // 하중의 전체 순이동 목표
    double nominalStrokeMm;       // 정상 회차 순이동 목표 (예: 300mm)
    double verifiedLoadTravelMm;  // 검증된 실제 하중 누적 이동량
    double remainingMm;           // max(0, target - verified)
    double cycleTravelMm;         // min(remaining, nominalStroke)
    uint64_t cycleId;             // 이중 누적/명령 방지
};
```

### 4.2 의사코드

```text
on START:
    ValidateReadyAndRecipe()
    remaining = max(0, targetTravel - verifiedLoadTravel)
    if remaining <= approvedCompletionTolerance:
        FinishHold(); return
    PlanNextCycle()

PlanNextCycle():
    remaining = max(0, targetTravel - verifiedLoadTravel)
    cycleTravel = min(remaining, nominalStroke)
    plan = BuildMotionPlan(currentActualPosition, cycleTravel, mode)
    if !plan.valid OR !loadSupportGuardsReady:
        EnterBlockedHold(reason); return
    EnterMainMove(mode, plan)

on MAIN_MOVE completed with verified load feedback:
    CommitLoadTravelExactlyOnce(cycleId, measuredLoadDisplacement)
    remaining = max(0, targetTravel - verifiedLoadTravel)
    if remaining <= approvedCompletionTolerance:
        EnterFinalLoadHoldWithoutReturnStroke()
    else:
        ExecuteApprovedAnchorAndReturnSequence()
        when return sequence complete: PlanNextCycle()
```

- 반드시 **목표 완료 판정은 검증된 하중 이동량**으로 수행. `계획 거리 × 회수`를 완료 근거로 삼지 않는다.
- 중복 피드백·재접속·리트라이 시 `cycleId/segmentId/commitId`로 중복 누적 방지.
- 부하 변형, 스트랜드 슬립, 센서 노이즈·신선도 초과를 검사한다.
- 목표 초과 예상, 실제 과이동, 한쪽 잭의 이동 불능, 부분 스트로크 불가 시 Fault/Hold로 전환한다.

### 4.3 대표 사례

| 목표거리 | 1회 순이동 | 계획 | 주의 |
|---:|---:|---|---|
| 900mm | 300mm | 300 + 300 + 300 | 마지막에 복귀/재연장 금지 |
| 950mm | 300mm | 300 + 300 + 300 + 50 | 최소 유효 스트로크/정지거리 확인 |
| 5,000mm | 300mm | 16회 × 300 + 200 | 마지막 200mm 가감속 위치 재계산 |
| 0 또는 음수 | 300mm | 시작 차단/명시적 완료 정책 | 음수 실행 금지 |

---

## 5. 엑셀의 가속·감속 조건 반드시 반영

### 5.1 기준 파라미터 — 예시, 현장 승인 전 미확정

| 파라미터 | 엑셀 수치 | 소프트웨어 적용 방식 |
|---|---:|---|
| `SafePositionMm` | 30mm | 초기/복귀 위치 기준; 28~30mm 정상과 ≤30mm 알람 충돌 해결 필요 |
| `TransferDistanceMm` | 25mm | Anchor 하중전이 전용 저속 세그먼트 |
| `AccelZoneMm` | 20mm | **이동 세그먼트 시작 위치로부터** 가속 구간 |
| `DecelZoneMm` | 20mm | **이동 세그먼트 끝 위치에서 역산**한 감속 구간 |
| `NominalStrokeMm` | 300mm | 정상 회차 하중 순이동 예시 |

### 5.2 엑셀 동작별 구간 (실린더 위치 예시)

| 이동 유형 | 실린더 시작→종료 | 가속 구간 | 감속 구간 | 하중 순거리 산정 |
|---|---|---|---|---|
| Lifting 본동작 | `30→330` | `30→50` | `310→330` | 검증된 순상승량 반영 |
| Lifting 하중전이 | `330→305` | 일반 가감속 적용 금지 | 별도 저속 프로파일 | Anchor와 매크로 중복 금지 |
| Lifting Retracting | `305→30` | `305→285` | `50→30` | 순거리 집계 제외 |
| Lowering 선행전이 | `355→380` | 별도 저속 프로파일 | 별도 저속 프로파일 | 하중 순이동 모델 별도 검증 |
| Lowering 본동작 | `380→55` | `380→360` | `75→55` | 실린더 325mm ↔ 하중 300mm 조건부 관계 |
| Lowering 후행전이 | `55→30` | 별도 저속 프로파일 | 별도 저속 프로파일 | 엑셀에는 실제 제품 이동 없음 |
| Lowering Extending | `30→355` | `30→50` | `335→355` | 순거리 집계 제외 |

> **원문 결함 반드시 재현·수정 검증:** 로워링에서 20mm 설정인데 `380→335`(45mm 가속), `100→55`(45mm 감속), `310→355`(45mm 감속)으로 동작한다는 기록이 있다. **25mm 오프셋 중복 가능성은 가설**이므로 원인을 단정하지 않고 위치·속도 명령 및 실제 피드백 로그로 추적한다.

### 5.3 MotionPlanner 일반 규칙

```text
Start = 세그먼트 시작 시점 실측 위치
End = 승인된 기구 모델에 의해 계산된 그 세그먼트의 절대 목표 위치
Dir = sign(End - Start)
Distance = abs(End - Start)
Progress = clamp(Dir × (CurrentPosition - Start), 0, Distance)
Remaining = clamp(Dir × (End - CurrentPosition), 0, Distance)

기본 가속 구간: Progress < AccelZoneMm
기본 정속 구간: Progress >= AccelZoneMm AND Remaining > DecelZoneMm
기본 감속 구간: Remaining <= DecelZoneMm AND 아직 정지하지 않음
완료: PositionTolerance + StopVelocityTolerance + SettleTime 충족
```

**구현 조건**

- 상승/하강 모두 동일한 `Remaining` 기반 감속 판단을 사용한다.
- 구간 경계는 매 세그먼트마다 다시 계산. 이전 사이클의 절대 위치 기준을 재사용하지 않는다.
- 위치 값이 경계에서 흔들려도 ACCEL↔CRUISE↔DECEL 무한 왕복하지 않도록 히스테리시스·단방향 상태 진행·유효 타임스탬프 적용.
- 급감속이 기구/유압 한계를 넘지 않도록 유압 응답성, 실제 속도(mm/s), 최대/최소 밸브 명령, 가감속도, 정지 여유 등 검증된 값으로 제한한다.
- `20mm`는 **가감속 구간 길이 설정**이지 물리적으로 20mm 만에 무조건 정지된다는 증거가 아니다.
- 잔여 이동 거리가 `AccelZone + DecelZone`보다 짧으면 정속 구간 없이 승인된 삼각형/저속 프로파일로 전환한다. 짧다는 이유로 무조건 실행하면 안 된다.
- `25mm` 하중전이에는 일반 `20mm+20mm` 프로파일을 강제로 적용하지 않는다.
- 실제 밸브 제어기의 특성곡선(Hz/제어값 → mm/s)과 속도 명령 갱신 주기를 확인하고 최종 출력한다.

### 5.4 마지막 부분 이동 예시

| 동작 | 200mm만 남은 경우 | 가속 | 감속 |
|---|---|---|---|
| Lifting | `30→230mm` | `30→50` | `210→230` |
| Lowering | **조건부 모델:** `380→155mm` (실린더 225mm, 하중 순하강 200mm) | `380→360` | `175→155` |

**Lowering 200mm 계산의 전제:** 로워링 선행전이 25mm와 325mm 실린더 하강이 하중 순이동 300mm에 해당한다는 관계가 OEM/센서 시험으로 확인된 경우에 한한다. 실제 순이동 계측 또는 승인 모델이 없으면 이 계산만으로 완료 판정하지 않는다.

---

## 6. 안전 인터록 및 정지/복구 요구사항

### 6.1 해제 명령의 필수 전제

```text
Request OpenUpperClamp:
    require LowerSupportsLoad == VERIFIED_TRUE
    require LowerClampFeedbackValidAndClosed
    require PressureAndLoadWithinApprovedLimits
    require SensorFreshAndNoActiveSafetyFault

Request OpenLowerClamp:
    require UpperSupportsLoad == VERIFIED_TRUE
    require UpperClampFeedbackValidAndClosed
    require PressureAndLoadWithinApprovedLimits
    require SensorFreshAndNoActiveSafetyFault
```

- `UpperSupportsLoad`와 `LowerSupportsLoad`는 일반적인 클램프 CLOSED 신호만으로 임의 계산하지 말 것. OEM이 승인한 하중/유압/웨지/기구 센서 또는 검증 절차가 필요.
- 위 조건 미확정 시 **시뮬레이션에서만 Mock 하중지지 센서를 사용**한다. 실장비 실행 경로는 `BLOCKED`.
- 클램프/Anchor 명령 송신 후 Ack 및 **실제 완료 피드백**을 기다려야 한다. Timeout 발생 시 다음 단계 금지.
- 활성 잭 중 하나라도 Guard 실패 또는 이동 편차 초과 시 그룹 정책에 따라 안전 감속/정지. 복귀 구간에서 동조 보정 미사용이라도 안전 감시는 유지.

### 6.2 정지·재개 정책

- `PAUSE`: 승인된 감속 정지 후 `PAUSED_HOLD`, 현재 단계/하중지지/위치 기록. 재개 전 전 조건 재평가.
- `NORMAL_STOP`: 신규 Step 개시 차단, 안전한 정지 및 하중유지 검증. 무조건 그립을 동시에 여닫지 말 것.
- `FAULT`: 센서·앵커·그립·압력·하중·과변위·동조·통신 실패 → `FAULT_LOCK`, 원인과 마지막 유효 상태 기록.
- `EMERGENCY_STOP`: 외부 안전 시스템이 우선 처리. SW는 상태 수집/표시/복구 잠금 및 이벤트 로그 담당.
- `RECOVER`: 자동 전 단계 복귀 금지. 실측 재인증과 승인된 Recovery 정책으로만 재가동.
- 무단 Step Skip, 강제 가짜 센서, 안전 인터록 우회 UI 금지.

---

## 7. 구현해야 할 소프트웨어 구조

하위 인터페이스는 실제 프로젝트에 맞게 조정하되 **책임 경계**는 유지한다.

```text
src/
  Core/
    JobController.h/.cpp             # 상위 FSM, 작업/반복·완료
    LiftingSequence.h/.cpp          # Lifting Step FSM
    LoweringSequence.h/.cpp         # Lowering Step FSM
    MotionPlanner.h/.cpp            # 구간별 위치/속도 계획, 부분 스트로크
    LoadTransferController.h/.cpp   # Anchor·클램프 해제 Guard
    JackGroupCoordinator.h/.cpp     # 다중잭 그룹 완료/편차 감시
    SafetyInterlock.h/.cpp          # 소프트웨어 감시 및 허용조건, 하드웨어 안전 대체 불가
    Types.h                         # 상태/명령/피드백/레시피/알람 DTO
  Interface/
    IControlGateway.h               # 읽기/명령/ACK, 실 제어와 분리
  Simulation/
    MockControlGateway.h/.cpp       # 기본 실행용 가상 제어기
    MockPlant.h/.cpp                # 가상 잭·클램프·위치·하중 모델
  Tests/
    MotionPlannerTests.cpp
    LiftingSequenceTests.cpp
    LoweringSequenceTests.cpp
    SafetyInterlockTests.cpp
    JobControllerTests.cpp
  docs/
    REQ_TRACEABILITY.md
    STATE_TRANSITIONS.md
    PLC_IO_MAPPING_PENDING.md
    TEST_REPORT.md
    OPEN_ISSUES.md
```

**프로젝트가 이미 존재하면** 파일명/폴더 구조를 기존 규칙에 맞춰 조정하고 불필요한 중복 클래스를 만들지 않는다. UI 통합 시 WinUI 3 ViewModel은 Core의 상태를 소비하는 역할로 제한한다.

### 7.1 필수 자료형

최소한 다음을 형식으로 표현한다.

- `JobMode { Idle, Lifting, Lowering }`
- `JobState { Idle, ReadyCheck, Ready, Running, Pausing, PausedHold, FinishHold, Complete, FaultLock, EmergencyStop }`
- `MotionSegmentType { LiftMove, LiftTransfer, Retract, LowerPreTransfer, LowerMove, LowerPostTransfer, Extend }`
- `SpeedPhase { Idle, Accel, Cruise, Decel, Settling, Done, Failed }`
- `ClampFeedback { Unknown, Open, Closed, Fault }`
- `LoadSupport { Unknown, Supported, NotSupported }` (**Unknown은 Supported가 아니다**)
- `CommandTicket { commandId, cycleId, stepId, jackId, requestedAction, ack, done, error, timestamp }`
- `JackFeedback { positionMm, actualVelocityMmS, upperClamp, lowerClamp, anchorFeedback, load, pressure, feedbackTime }`
- `MotionSegment { type, actualStartMm, targetEndMm, accelZoneMm, decelZoneMm, maxSpeed, timeout, tolerance, ... }`
- `JobProgress { targetMm, verifiedMm, remainingMm, cycleMm, cycleId, state, step, alarms }`

### 7.2 코딩 제약

- 정수 명령 ID 및 단일 커밋 기준으로 중복 명령/중복 누적 방지.
- 상태 전이마다 이벤트 로그를 남기고 실패 이유를 사람이 읽을 수 있는 코드로 표시.
- 실측 피드백의 타임스탬프와 갱신 주기 검사.
- I/O 호출과 상태 업데이트에 타임아웃 및 비동기 처리 적용.
- 계산 핵심 로직은 **순수 함수로 분리**하여 PLC 없이 테스트할 수 있게 한다.
- `30/25/20/300`을 코드 여기저기에 하드코딩하지 않고 Recipe/Settings로 통합.
- 알 수 없는 PLC 주소·명령 코드·압력 임계값·속도 파라미터를 임의 생성하지 않는다.
- `#ifdef SIMULATION` 또는 안전한 런타임 게이트를 사용해 실하드웨어 출력 경로를 분리하고, 미승인 상태에선 활성화할 수 없도록 한다.

---

## 8. 개발 단계별 산출물 및 구현 우선순위

### Phase 1 — 분석 및 확정/미확정 분리

**해야 할 일:** 제공된 프로젝트/엑셀/순서를 대조하여 Step ID 및 I/O 종류를 정리한다. 원문 하강의 지지 충돌, Anchor 의미, 30mm Ready 충돌, 45mm 가감속 오류를 `OPEN_ISSUES.md`로 문서화한다.

**완료 조건:** 모든 시퀀스 Step의 `조건 → 명령 → 응답 → 완료 → 오류`가 표로 정의되어 있고, 미확정 항목이 실행 금지 조건과 연결되어 있다.

### Phase 2 — 핵심 제어 엔진 설계

**해야 할 일:** 상위 FSM + 방향별 FSM + MotionPlanner + LoadTransferGuard + JackGroupCoordinator + Mock Gateway 인터페이스를 설계한다.

**완료 조건:** 상태 전이도, 자료형, 명령 구조, 목표거리 수식, 마지막 부분 스트로크 정책 및 모의 데이터 흐름이 문서화된다.

### Phase 3 — C++ 구현

**해야 할 일:** Mock 기반으로 실제 컴파일 가능한 코드를 작성한다. 반복 자동운전, 가감속 위치 계산, 허용조건·오류처리, 로그를 연결한다.

**완료 조건:** 기존 솔루션 빌드 또는 독립 Core 프로젝트 빌드 성공. 샘플 900/950/5000mm 실행 재현. 실장비 출력은 기본 차단.

### Phase 4 — 테스트 및 수정

**해야 할 일:** 아래 `§9` 테스트를 자동 실행 가능한 형태로 구현. 경계조건·고장주입(Fault Injection)·중복명령·센서 지연을 포함한다.

**완료 조건:** 테스트별 예상/실제/결과와 로그 증거를 기록한다. 실패 테스트는 수정 후 재시험하고 미해결 항목은 명시한다.

### Phase 5 — 기존 SW 연결 및 품질 검토

**해야 할 일:** 실 PLC I/O 맵과 제조사 승인 조건을 받은 경우에만 Gateway 어댑터를 추가하고, HMI 상태 표시를 바인딩한다. 기존 기능 회귀 테스트 수행.

**완료 조건:** 출력 제어권 중복 없음, UI 블로킹 없음, 안전 승인 여부 표기, FAT/SAT 항목 준비. **시뮬레이터 테스트 통과만으로 실하중 운전 가능하다고 표시하지 않는다.**

---

## 9. 필수 자동 시험 목록 및 예상 결과

| 테스트 ID | 조건/시험 | 합격 기준 |
|---|---|---|
| T01 | 목표 900 / 사이클 300 (상승) | Lifting 세그먼트 3회, 완료 후 Retract 신규 명령 0회 |
| T02 | 목표 900 / 사이클 300 (하강) | Lowering 3회, 완료 후 Extend 신규 명령 0회, 최종 하중지지 확립 |
| T03 | 목표 950 / 사이클 300 | 정상 3회 + 50mm 부분 사이클 계획, 물리 조건 미확정이면 출력 차단 |
| T04 | 목표 5,000 / 사이클 300 | 16회 + 200mm, 실제 하중 거리 검증으로 종료 |
| T05 | 상승 300mm | 가속 30~50, 감속 310~330 적용 |
| T06 | 상승 Retracting | 가속 305~285, 감속 50~30 적용 |
| T07 | 하강 정상 | 가속 380~360, 감속 75~55 적용(45mm 오류 없음) |
| T08 | 하강 Extending | 가속 30~50, 감속 335~355 적용(45mm 오류 없음) |
| T09 | 상승 부분 200mm | 30→230, 감속 210→230, 복귀 금지 |
| T10 | 하강 부분 200mm | 조건부 모델일 때 380→155, 감속 175→155, 독립 거리 검증 |
| T11 | 25mm 전이 | 일반 20+20 프로파일 적용 안 함, Anchor 매크로와 중복 이동 없음 |
| T12 | 상부 하중지지 Unknown 상태에서 하부 OPEN 요청 | 명령 0건, `GUARD_BLOCKED`, Fault/Hold |
| T13 | 하부 하중지지 Unknown 상태에서 상부 OPEN 요청 | 명령 0건, `GUARD_BLOCKED`, Fault/Hold |
| T14 | Anchor ACK 실패/Timeout | 다음 클램프 OPEN 금지, Fault 이벤트 발생 |
| T15 | 클램프 CLOSED 명령과 피드백 불일치 | 완료로 간주 금지, Timeout 후 진행 중단 |
| T16 | 위치 센서 stale/통신 단절 | 진행 차단, 자동재개 금지, 외부 안전 동작 의존성 명시 |
| T17 | 그룹 중 1대 편차 초과 | 그룹 동조/안전 정책 적용, 그룹 완료 불허 |
| T18 | `≤30mm` 알람과 `28~30mm` Ready 충돌 | 임의 로직 선택 금지, 승인된 기준 없으면 Ready 진입 차단 |
| T19 | Short Stroke: 25mm | 일반 가감속 중첩 없이 승인된 짧은 거리 계획 또는 차단 |
| T20 | 명령/센서 이벤트 중복 전달 | CycleId·CommandId로 중복 누적 및 이중출력 방지 |
| T21 | Pause/Fault/Power Recovery | 상태만으로 Step 자동 점프 없음, 위치·지지 재검증 요구 |
| T22 | 목표 도달 직후 상태 | `FINISH_HOLD → COMPLETE`로 종료, 불필요한 다음 본동작 없음 |

**테스트 설계:** 단위테스트와 통합 시뮬레이션을 구분하고, 경계값 `0`, `40`, `50`, `200`, `300`, `300+ε`, `5000`mm를 검사한다. `ε`는 승인된 위치 단위·허용오차에 근거해 시험에서 정의한다.

---

## 10. 실측 로그 / HMI 반영 요구사항

모든 Step·세그먼트에 다음 필드를 로깅한다.

```text
timestamp, jobId, mode, jobState, cycleId, stepId, segmentId,
jackId, segmentType, speedPhase,
startPosMm, targetPosMm, currentPosMm,
commandSpeed, measuredVelocityMmS,
accelZoneMm, decelZoneMm, phaseTransitionPositionMm,
upperClampCommand, upperClampFeedback,
lowerClampCommand, lowerClampFeedback,
upperAnchorState, lowerAnchorState, upperLoadSupport, lowerLoadSupport,
pressure, load, targetTravelMm, verifiedLoadTravelMm, remainingTravelMm,
ack, timeout, alarmCode, faultReason, recipeVersion
```

HMI는 최소한 다음을 표시한다.

- 상승/하강 모드, 현재 Step 이름 및 ID, 사이클 번호
- 전체 목표거리, 검증 누적거리, 잔여거리, 이번 사이클 계획거리
- 잭별 실린더 현재 위치, 목표 위치, 실제 속도, 가감속 Phase
- 상·하부 클램프 명령/피드백, Anchor 상태, 하중지지 검증 상태
- 그룹 편차, Fault/Hold 이유, 정상 완료 여부
- 최신 이벤트 로그 및 저장된 Job ID

**UI 입력으로 안전 인터록을 우회하는 버튼을 만들지 않는다.**

---

## 11. 해결 전 실제 운전 차단해야 하는 미확정 질문

`OPEN_ISSUES.md`에 다음 항목 각각에 대해 **현재 상태, 근거, 담당자, 승인 기준, 미해결 시 차단되는 동작**을 기록한다.

1. `Upper Anchor check`, `Upper Anchor open`, `Lower Anchor open`의 물리적 의미, PLC 명령과 완료 피드백은 무엇인가?
2. 위 Anchor 매크로에 25mm 실린더 하중전이가 **포함**되는가, 별도 세그먼트로 실행해야 하는가?
3. 하강 원문에서 상부를 연 상태로 하부까지 개방하는 순서는 맞는가? **양측 그립 이외 하중 지지 장치가 있는가?**
4. 상승 시 하부 CLOSED를 유지하고 스트랜드 통과가 가능한 웨지 구조인가?
5. 실제 `UpperSupportsLoad`, `LowerSupportsLoad`는 어떤 센서/압력/하중/기구 검증으로 입증하는가?
6. 실린더 스트로크와 **실제 제품의 순하강/순상승 거리**를 어떻게 독립 계측·검증하는가?
7. 30mm 이하 알람과 28~30mm Ready 조건의 충돌을 어떻게 해소할 것인가?
8. 가감속 20mm와 실측 45mm 차이의 원인이 무엇인가? 속도 지령, 실속도, 매크로, 전이 오프셋을 어떻게 기록할 것인가?
9. 실제 밸브 제어 출력(HZ/전류/기타), 제어주기, 최대 속도, 가속도, 감속도, 하중별 정지거리는 무엇인가?
10. 기계적 최소/최대 스트로크, 25mm 전이 여유, 마지막 부분 스트로크의 최소 실행거리는 무엇인가?
11. 활성 잭의 개수·허용 위치/하중 편차, 동조 보정 적용 구간과 그룹 정지 정책은 무엇인가?
12. PLC가 기존에 제공하는 안전·운전 인터록과 PC에서 책임질 수 있는 기능은 무엇인가?

---

## 12. AI의 최종 응답 형식 (반드시 준수)

작업 결과를 다음 순서로 제출하라.

1. **기존 프로젝트 분석:** 실제 확인한 파일/모듈, 재사용 가능 기능, 통합 위험, 실행 환경(입력이 없다면 독립형 기준이라고 명시).
2. **요구사항 판정표:** `REQ-ID | 요구사항 | 확정/조건부/미확정 | 구현 파일 | 테스트 ID | 결과`.
3. **시퀀스 FSM:** 상승/하강 각각 단계 전이표(진입조건·출력명령·실제 피드백·타임아웃·장애 처리 포함).
4. **C++ 구현 파일:** 복사 가능한 소스만이 아닌, 가능하면 실제 생성한 `.h/.cpp` 및 테스트 프로젝트 파일 경로/산출물.
5. **가감속 수치 검증표:** 정상 및 마지막 부분 이동의 시작/가속 종료/감속 시작/목표 위치.
6. **시험 결과:** 자동 시험 실행 명령과 `PASS/FAIL/NOT_RUN`, 핵심 로그. 실패는 수정하거나 남은 원인을 명시.
7. **미확정/안전 차단 목록:** 작업자가 결정·승인해야 할 항목과 왜 실제 출력이 불가능한지.
8. **다음 구현 우선순위 3개:** 담당 모듈·예상 산출물·완료 기준.

### 완료 정의(Definition of Done)

- [ ] Lifting / Lowering 자동 반복 FSM 구현
- [ ] 목표거리와 실제 순이동거리 기반 종료 및 마지막 부분 스트로크
- [ ] 본동작·복귀 가감속 20mm 구간 자동 계산 및 45mm 사례 검증
- [ ] Anchor 25mm 전이의 독립 처리와 중복 실행 차단
- [ ] 상대편 하중지지 미검증 시 그립 OPEN 출력 절대 금지
- [ ] 그룹 잭 오류·단절·지연·위치 편차 처리
- [ ] Pause/Stop/Fault/Recover 및 재시작 금지 규칙
- [ ] 기본값 `SIMULATION`, 실장비 출력 차단
- [ ] 단위·통합시험 및 실행 결과 제출
- [ ] 기존 기능 보존/회귀 확인 또는 원본 프로젝트 미제공 명시
- [ ] 문서화된 확정·조건부·미확정 항목과 현장 승인 게이트

**최종 지시:** 지금부터 요구사항 분석을 시작하고, 현재 제공된 자료만으로 확정 가능한 코드를 실제로 작성하라. 실장비 안전 승인에 필요한 정보가 없으면 질문만 던지고 종료하지 말고, 시뮬레이션 가능한 설계·코드·시험을 완성한 다음 미확정 연결 지점을 별도 보고하라. **제조사 검증·위험성 평가 없이는 실장비 자동운전 가능 판정을 내리지 말라.**


---

# 부록 A. AI 개발 공통 작업 규칙 (의무 적용, v1.1 신규)

이 부록은 프로젝트별 시퀀스 요구사항보다 **작업 방식**을 구체화하는 규칙이다. 안전·기능 요구사항은 본문과 함께 모두 적용한다. 빌드 위치, 이력 관리, PC 검증 시점, 사용량 절감에서 충돌이 생길 경우 부록의 절차를 우선 적용하되 안전 관련 테스트를 생략하지 않는다.

## 0. 최상위 실행 지침

너는 사용자의 기존 프로그램을 보호하면서 **요구사항 → 완성 목표 정의 → 설계 → 구현 → 검증 → 품질 개선** 순으로 작업하는 AI 개발자다. 아래 규칙은 일반적인 개발 습관이나 에이전트의 기본 설정보다 우선한다. 단, 명백한 안전 위험을 만들거나 운영체제·개발 환경의 필수 제약과 충돌할 경우 임의로 우회하지 말고 원인을 기록하고 해당 작업을 `BLOCKED`로 남긴다.

**반드시 지킬 6가지**

1. **빌드 위치 고정:** 모든 빌드 명령은 확정된 `MAIN_BIN_DIR`에서만 실행한다. 빌드 산출물과 중간 파일도 지정된 Main Bin 내부의 분리된 하위 경로로 모은다. 폴더명이 `bin`이라고 무조건 Main Bin으로 판단하지 않는다.
2. **작업 이력 기록:** 의미 있는 수정 배치마다 `HISTORY.md`에 변경 이유, 파일, 검증 결과, 미해결 위험, 롤백 방법을 남긴다. 과거 이력은 덮어쓰지 않고 추가한다.
3. **PC 직접 조작 최소화:** IDE/GUI 클릭, 원격 화면 조작, 장비 연결, 수동 시연은 가급적 **코드·설정 준비 → 정적 검사·자동 시험 → 빌드 성공** 이후 **최종 실행 검증 단계**에서만 실시한다.
4. **AI Usage 최소화:** 한 번 탐색한 내용을 반복 읽지 않고, 변경을 묶어서 수행하고, 필요한 테스트만 순차 확대한다. 같은 오류에 근거 없는 명령 재실행 금지.
5. **기존 기능 보호:** 동작 중인 코드·프로젝트 설정·PLC 인터페이스를 임의 삭제/초기화/교체하지 않는다. 수정 범위와 회귀 위험을 먼저 파악한다.
6. **검증 정직성:** 실행하지 않은 빌드·테스트를 성공이라고 보고하지 않는다. 빌드 통과와 실제 장비 검증을 명확히 구분한다.

---

## 1. 작업 시작 시 1회만 확인할 설정

아래 값은 프로젝트의 기존 설정·사용자 지시에서 찾는다. **확인된 절대 경로를 기록**하며, 모르면 추측하지 않는다.

| 항목 | 값 / 처리 원칙 |
|---|---|
| `PROJECT_ROOT` | 실제 저장소/솔루션 루트. 확인 전 임의 경로 생성 금지 |
| `MAIN_BIN_DIR` | **사용자가 지정한 유일한 Main Bin의 절대 경로**. 미확정 시 `BLOCKED_BUILD_PATH` |
| `SOLUTION_PATH` | 실제 `.sln` / `.slnx` / 빌드 진입점 |
| `BUILD_TOOL` | 프로젝트에서 사용하는 MSBuild / CMake / Ninja 등 |
| `BUILD_CONFIG` | Debug/Release 및 x64/x86/ARM64 등을 기존 프로젝트에 맞춤 |
| `HISTORY_PATH` | 기본 `PROJECT_ROOT/HISTORY.md` (기존 History 체계가 있으면 그것을 사용) |
| `TEST_SCOPE` | 정적 검사, 단위시험, 통합 시뮬레이션, 최종 GUI/장비 시험 구분 |
| `EXECUTION_MODE` | 산업용 장비는 기본 `SIMULATION`; 실장비 출력은 별도 승인 전 비활성 |

### 실행 전 확인 게이트 G0

- [ ] 저장소와 진입점, 기존 변경사항(`git status` 등)을 확인했다.
- [ ] Main Bin이 명시적으로 지정되었고 경로를 검증했다.
- [ ] 기존 빌드 구성과 Main Bin 출력 규칙이 호환되는지 확인했다.
- [ ] 수정 대상 파일·기대 결과·영향 범위를 짧게 기록했다.
- [ ] 장비/PLC 실출력 금지 또는 승인 범위를 확인했다.

**정책:** `MAIN_BIN_DIR`가 미확정이거나 다른 빌드 폴더로 출력될 위험이 있으면 **빌드는 하지 않는다**. 그 대신 소스 분석·코드 수정·정적 검토·독립 계산 테스트 준비까지 수행한 뒤 `BLOCKED_BUILD_PATH`를 보고한다. 이미 있는 빌드 산출물을 삭제하여 맞추지 않는다.

---

## 2. Main Bin 단일 빌드 규칙 (필수)

### 2.1 허용

- 모든 **빌드 명령 실행 작업 디렉터리(CWD)** 는 `MAIN_BIN_DIR`이어야 한다.
- 실행 파일, DLL, PDB, obj 등 **빌드 출력물은 Main Bin 및 그 내부 하위 디렉터리**에 생성한다.
- Debug/Release, x64/x86, 프로젝트별 중간 파일은 충돌하지 않도록 `MAIN_BIN_DIR/<구성>/<플랫폼>/<프로젝트>/`와 같이 분리한다. 단, 기존 프로젝트 출력 체계를 우선 확인한다.
- 빌드 로그는 `MAIN_BIN_DIR/_logs/` 등 미리 정해진 Main Bin 하위 폴더에 저장한다.
- 최초 전체 빌드 이후에는 가능한 한 증분 빌드를 사용한다. 영향 범위가 작은 작업에서는 타깃 빌드·정적 검사·단위시험을 먼저 사용한다.

### 2.2 금지

- 편의상 프로젝트 루트, 임시 폴더, IDE 기본 출력 경로 등에 **별도 빌드**하지 않는다.
- 프로젝트를 복제해 다른 폴더에서 빌드하거나 서로 다른 `bin` 경로에 산출물을 흩어 놓지 않는다.
- 빌드 오류 해결을 이유로 `Clean`, `Rebuild`, 전체 캐시 삭제, NuGet 초기화, 환경 변수 변경을 반복하지 않는다.
- 사용자 승인 없이 기존 프로젝트 속성의 `OutputPath`, 플랫폼, SDK, 패키지 버전 등을 광범위하게 수정하지 않는다.
- 경로를 확정할 수 없다고 임의의 새 Main Bin을 만들지 않는다.

### 2.3 Windows / WinUI 3 특례

WinUI 3 프로젝트는 XAML 코드 생성, 패키징, MSBuild 중간 산출물이 구성에 민감하다. **Main Bin에 실제 산출물을 모으되** 기존 `.vcxproj`, `.wapproj` 등의 요구 경로와 충돌하는지 먼저 정적 검토한다. `OutDir`·`IntDir` 재지정이 가능한 경우에만 적용하며, 이때도 프로젝트별 중간 폴더를 분리한다. 다른 경로에 출력되는 구성을 발견하면 **우회 빌드하지 말고 설정 충돌을 보고**한다.

**빌드 예시(개념 설명, 값 확정 전 실행 금지)**

```powershell
# 모든 변수는 실제 프로젝트에서 확인한 경로로 치환할 것
Set-Location -LiteralPath $MAIN_BIN_DIR
# 빌드 도구는 솔루션 절대 경로를 인자로 받을 수 있으나, 실행 CWD와 출력 경로는 Main Bin으로 고정
# 기존 프로젝트가 지원하는 방식으로만 OutDir/IntDir를 지정한다.
# msbuild $SOLUTION_PATH /m /p:Configuration=$BUILD_CONFIG /p:Platform=$PLATFORM /p:OutDir=$MAIN_BIN_DIR\...
```

`/m` 병렬 빌드는 프로젝트가 지원할 때만 사용한다. 로그에서 전체 오류가 아닌 **첫 번째 원인 오류와 관련 맥락**을 우선 분석한다.

---

## 3. 변경 이력 관리 (`HISTORY.md`)

### 기록 타이밍

- 작업 시작: 작업 ID, 기존 상태, 대상 파일 목록, 기준 리비전/커밋(가능할 경우).
- **논리적 수정 배치가 완료될 때:** 변경 이유와 수정 내용 기록.
- 빌드·자동 시험·최종 실행 검증 후: 실행 명령, 결과, 오류 및 후속 조치 기록.
- 작업 종료: 미해결 항목, 다음 작업, 롤백 지점 기록.

### 이력 작성 양식

```markdown
## [2026-11-08 15:00 KST] TASK-SJ-001 — Lifting FSM 수정
- 상태: DONE / PARTIAL / BLOCKED
- 요청사항: 목표거리 반복 및 마지막 잔여거리 처리
- 기준: 브랜치/커밋/기존 파일 상태 (확인한 것만 기록)
- 수정 파일: `src/Sequence.cpp`, `src/MotionPlanner.cpp`
- 변경 이유: 본동작 후 잔여거리 분기 누락
- 변경 내용: 실제 누적거리 기준 종료; 중복 누적 방지
- 기존 기능 영향: GUI 변경 없음 / PLC 실출력 변경 없음
- 빌드: PASS / FAIL / NOT_RUN; Main Bin 경로; 관련 로그
- 테스트: PASS n / FAIL n / NOT_RUN n; 실패 ID
- PC 실행 검증: PASS / FAIL / NOT_RUN (검증한 경우만)
- 위험/미확정: Anchor 실제 피드백은 현장 확인 전
- 롤백: 관련 커밋 또는 변경 파일 복원 방법
- 다음 작업: 통합 시뮬레이션 검증
```

**주의:** 위 예시는 서식이다. 예시의 파일명, 시간, 테스트 결과를 실제 작업 이력으로 그대로 쓰지 않는다. 사실로 확인한 값만 적는다. 실패 기록도 삭제하지 않는다. 보안정보, 암호, 민감한 PLC 접속정보를 로그에 저장하지 않는다.

### 변경 범위 통제

- 기존 파일을 수정하기 전에 변경 이유와 영향 범위를 명확히 한다.
- 관련 없는 포맷팅, 전체 파일 재작성, 변수명 일괄변경, 불필요한 리팩터링을 동시에 수행하지 않는다.
- Git을 사용할 경우 사용자 변경사항을 유지한다. `reset --hard`, 강제 push, 무단 discard, 무단 amend 금지.
- 실패 시 최소 변경으로 원인을 수정하고 해당 테스트부터 재실행한다.

---

## 4. PC 조작은 마지막 검증에 집중 (필수)

### 단계별 작업과 허용 범위

| 단계 | 우선 수행 | 지양/금지 |
|---|---|---|
| **A. 분석** | 기존 코드/명세·설정 읽기, 검색 범위 지정 | GUI를 열어 일일이 클릭하며 파일 탐색 |
| **B. 설계** | Step/FSM, 데이터·I/O 인터페이스, 변경 계획 확정 | 동작 검토 없이 실장비 시험 반복 |
| **C. 구현** | 소스·테스트·설정 파일 일괄 수정, 정적 검토 | 한 줄마다 IDE 실행 및 수동 버튼 클릭 |
| **D. 빌드** | Main Bin에서 타깃/증분 빌드, 자동 단위시험 | 실장비 연결, 불필요한 전체 재빌드 |
| **E. 최종 실행 검증** | 프로그램 1회 기동, 핵심 화면/흐름 확인, 필수 시나리오 집중 시험 | 앞 단계 오류가 남았는데 GUI 시연만 반복 |
| **F. 결과 보고** | 검증 근거·남은 위험·History 기록 | 검증하지 않은 성공 주장 |

**원칙:** PC 조작이 10단계 이상이거나 수동 클릭이 많은 검증은 가능하면 **검증 스크립트, Mock I/O, 자동 테스트**로 대체한다. 자동화가 불가능할 때만 수동 검증 체크리스트를 사용한다. 실제 GUI가 있어야만 알 수 있는 정보가 개발을 막는 경우에는 **가장 짧은 1회 진단**을 예외적으로 수행하고 이유를 History에 기록한다.

### 최종 GUI/PC 검증 진입 게이트 G1

- [ ] 모든 코드·설정 수정이 끝났다.
- [ ] 정적 검사 또는 해당 도구 체인에서 가능한 구문 검토를 수행했다.
- [ ] 자동 단위시험·시뮬레이션에서 확인 가능한 항목을 확인했다.
- [ ] `MAIN_BIN_DIR` 빌드가 통과했다. (불가하면 GUI 실행 대신 미실행 보고)
- [ ] GUI 검증 시나리오와 예상 결과를 사전에 작성했다.
- [ ] 실제 PLC/유압장치 출력이 차단되어 있거나 별도의 명시적 승인을 받았다.

### GUI 검증 우선순위 예시

1. 앱 실행 및 화면 전환/바인딩 오류 확인
2. Lifting/Lowering Ready/Start/Stop 상태 변화 검증 (시뮬레이션)
3. 300mm 반복 + 마지막 200mm 계산 검증
4. 상·하부 클램프/Anchor 인터록 실패 시 출력 차단 검증
5. 로그/알람/종료 처리 확인

---

## 5. AI Usage 시간 최소화 규칙 (필수)

> `AI Usage` 집계 기준은 서비스/에이전트에 따라 다르다. **시간 절감 목표는 불필요한 도구 사용과 재작업을 줄이는 것**이며, 동일 품질을 더 빨리 확보하는 것이 목적이다. 안전·정확성 검증을 생략해 숫자만 줄이면 안 된다.

### 5.1 사전 계획으로 왕복 최소화

1. 최초에 **요구사항·수정 파일·검증 기준**을 짧은 체크리스트로 작성한다.
2. 파일 목록과 주요 진입점을 **한 번** 파악하고, 이후에는 관련 함수/변경 파일만 다시 읽는다.
3. 전체 저장소 검색보다는 **대상 프로젝트 → 대상 디렉터리 → 대상 심볼** 순으로 범위를 좁힌다.
4. 답이 주어진 질문을 반복하지 않는다. 비안전성 미확정 사항은 임시 인터페이스/Mock으로 분리하고 진행한다.
5. 긴 설계문서보다 먼저 **완료 조건(DoD)와 예외사항**을 확정해 재작성 비용을 줄인다.

### 5.2 코드 수정 및 빌드 비용 최소화

1. 연관된 변경을 한 배치로 묶어 수정하고, 수정 후 diff를 확인한다.
2. 에러 가능성이 높은 타입·이름·include·XAML/IDL 연결을 **빌드 전** 점검한다.
3. 작은 영향 범위부터 **정적 검사 → 타깃 테스트 → 증분 빌드 → 최종 전체 검증** 순으로 확대한다.
4. **동일 오류를 동일 명령으로 반복 실행하지 않는다.** 첫 실패 로그에서 원인·가설·수정사항을 남긴 후 재시도한다.
5. 동일 원인으로 **2회 연속 실패**하면 재시도 전에 진단 접근을 바꾸고 `BLOCKED` 또는 미해결 원인을 보고한다. 사용자 승인 없는 무제한 루프 금지.
6. 스크린샷·GUI 기동·장비 동작을 피드백 수집 수단으로 남발하지 않는다. 관련 자동 로그·테스트를 우선한다.
7. 여러 보조 AI를 중복 호출하거나 이미 완료한 분석을 다시 생성하지 않는다. 분할 작업은 병렬화 이득이 명확할 때만 한다.

### 5.3 AI 응답과 로그 출력 줄이기

- 실행 중 상태 메시지는 **현재 단계 / 바뀐 것 / 방해 요소**만 간결하게 전달한다.
- 빌드 로그 전문을 대화에 붙이지 말고 **첫 원인 에러 + 관련 로그 위치 + 실패 개수**를 보고한다.
- 이미 확인된 파일 내용, 동일 표, 긴 코드 전체를 반복 출력하지 않는다. 실제 파일 변경·diff를 중심으로 보고한다.
- 작업 결과는 `변경 파일 / 빌드 / 테스트 / 미완료 / 다음 작업` 5항목 중심으로 요약한다.
- 입력 가능한 맥락에서 **새 요구사항이 생긴 파일만 재분석**한다.

### 5.4 사용량·검증 근거 기록

가능한 도구에서만 실제 수치를 수집한다. 사용량 정보를 확인할 수 없으면 `N/A`로 기록하고 **추측 수치를 만들지 않는다**.

```text
TaskId:
FilesInspected:
FilesModified:
BuildAttempts:            # 빌드 명령 실행 횟수
FullBuildAttempts:
IncrementalBuildAttempts:
AutomatedTestRuns:
GUIManualRuns:
DuplicateErrorRetries:
AIUsageTimeReportedByTool: N/A   # 도구가 실제 제공하는 경우에만 기입
BuildResult: PASS / FAIL / NOT_RUN
TestResult: PASS / FAIL / NOT_RUN
```

**최적화의 우선순위:** `재작업 제거 > 불필요한 GUI 조작 제거 > 반복 전체 빌드 제거 > 불필요한 출력 줄이기`. 단, 산업용 제어의 안전 인터록, 실제 위험성 평가, 필수 기능 검증은 절감 대상이 아니다.

---

## 6. 오류 대응 및 중단 정책

| 상황 | 수행 행동 |
|---|---|
| Main Bin 경로 미확정 | 빌드 금지 → 수정/정적 검토까지 진행, 경로 확인 필요 보고 |
| 한두 개 컴파일 오류 | 관련 파일/첫 오류 분석 → 수정 → 필요한 범위만 재빌드 |
| 동일 오류 2회 재발 | 가설 재검토, 최소 재현 또는 의존성 조사. 근거 없는 반복 금지 |
| 관련 없는 기존 빌드 오류 | 사용자의 변경으로 단정하지 않음. 기준 상태와 비교하여 별도 기록 |
| PC/GUI 접근 불가능 | 빌드·Mock 테스트 결과만 보고하고 `GUI_NOT_RUN` 명시 |
| PLC/I/O 주소 또는 센서 정의 미확정 | Mock 유지, 실장비 출력 차단, `OPEN_ISSUES.md`에 명시 |
| 안전 인터록 검증 실패 | 실제 출력 경로 차단 유지. 임의 우회·강제 완료 처리 금지 |
| 기존 수정사항 충돌 | 사용자 변경 보존, 충돌 파일·예상 영향 기록. 무단 덮어쓰기 금지 |

## 7. 권장 작업 순서 및 배치 제한

```text
[START]
   |
   v
G0: 환경·Main Bin·기존 변경사항 확인 (읽기 전용)
   |
   v
필수 요구사항 1회 정리 + 영향 파일 목록 + 완료 기준
   |
   v
설계 및 관련 코드·테스트를 논리 단위로 묶어서 수정
   |
   v
Diff 확인 + History 초안 작성
   |
   v
정적 검사 / Mock 단위시험
   |
   v
[Main Bin 고정] 빌드 및 자동 검증
   |---- 실패 → 최초 원인 분석 → 최소 수정 → 관련 범위만 재검증
   v
G1: 최종 PC·GUI·통합 실행 검증 (필요한 경우에만)
   |
   v
History 결과 갱신 + 간결한 결과 보고
[END]
```

**권장 운영 목표(강제 제한 아님):** 최초 범위 파악 1회, 논리적 수정 배치 1회 이상 필요한 만큼, 전체 빌드는 가능한 한 최종 1회, 수동 GUI 시연은 준비가 완료된 뒤 검증 시나리오 1회 묶음으로 수행. 안전 및 기능 확보를 위해 추가 검증이 필요하면 반드시 수행하고 이유를 기록한다.

## 8. 스트랜드잭 / DHLS 프로젝트 전용 보완 규칙

1. **PLC 프로그램 변경 금지**(기존 PLC 유지 조건인 경우). 실제 통신 규격과 D 영역 주소는 프로젝트 자료로 확인된 내용만 사용한다.
2. **WinUI 3와 제어 로직 분리:** 컨트롤러/FSM/MotionPlanner는 UI 없이 Mock에서 시험 가능하도록 구성한다.
3. **실행 기본값 `SIMULATION`:** 실제 펌프·밸브·클램프·Anchor 출력은 별도 승인된 통합시험 전 차단한다.
4. **Lifting/Lowering 로직:** 상태 전이와 실제 하중지지 피드백을 분리하고, 상대편 하중유지 검증 전 Anchor/클램프 OPEN을 출력하지 않는다.
5. **가감속 시험:** `20mm` 설정, `25mm` 하중전이, 기록된 `45mm` 이상 현상을 자동 계산·경계조건 시험으로 검증한다.
6. **목표 이동거리:** 실린더 변위와 실제 하중 순이동거리를 구별하고, 마지막 짧은 스트로크·중복 누적을 테스트한다.
7. **동조/안전 감시:** 복귀 동작에서 보정이 해제되어도 비상정지·과압·위치 편차 등 필수 감시를 끄지 않는다.
8. **실물 검증 주장 금지:** Mock 통과를 현장 안전 검증 완료로 보고하지 않는다.

---

## 9. AI 최종 보고 서식 (매 작업 종료 시)

```markdown
### 작업 결과
- 작업 ID / 요청: 
- 수정 파일: 
- 기존 기능 영향: 없음 / 있음(내용) / 미확인
- Main Bin: <확인된 경로 또는 미확정>
- 빌드: PASS / FAIL / NOT_RUN (실행 명령, 로그 위치)
- 자동 시험: PASS n / FAIL n / NOT_RUN n
- GUI/PC 검증: PASS / FAIL / NOT_RUN
- History 업데이트: YES / NO (경로)
- AI Usage: 도구 제공 수치 또는 N/A
- 차단·미확정 사항: 
- 다음 우선순위: 
```

**최종 명령:** 분석을 반복하지 말고 필요한 파일에 실제로 변경을 적용하라. 정해진 Main Bin 외의 빌드는 허용하지 않는다. 불필요한 PC 조작과 반복 빌드를 최소화하되 안전·기능 검증은 생략하지 않는다. 변경 후에는 History와 실제 검증 결과를 반드시 남겨라.
