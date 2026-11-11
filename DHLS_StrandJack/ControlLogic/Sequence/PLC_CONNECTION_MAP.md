# PLC 연결 경계와 미확정 주소

실제 읽기는 기존 EquipmentControlService Worker의 TcpReader/XgtEmergencyTransport가 소유한다. PLC별 요청을 직렬 처리하며 각 TCP 호출은 기존 제한시간을 사용한다. 새 시퀀스는 실제 PLC 운전 쓰기를 하지 않는다.

| 주소 | 방향 | 현재 코드 | 새 시퀀스 적용 |
| --- | --- | --- | --- |
| D010.0 | PLC → PC | LOCAL EMERGENCY 감지·PC 동작 정지·표시 | 기존 경로 유지 |
| D010.1/.2/.3 | PLC → PC | PC 선택/HPU/인버터 오류로 기존 코드 해석 | 기존 읽기 유지, 제작사 readiness/정지 피드백으로 확대 해석하지 않음 |
| D011~D026 | PLC → PC | 기존 PT/HT raw WORD 및 설정된 환산 | PlcReadOnlyPlant에 기존 최신 스냅샷 전달 가능 |
| D051.0 | PC → PLC | 기존 승인된 emergency ON 한정, ACK+재읽기 확인 | 새 운전 출력에 재사용하지 않음. Mock에서는 전송 없음 |
| D051.F | PC ↔ PLC | 기존에는 heartbeat handshake 미구현 | 이번에도 미구현. PLC OFF ACK/timeout/워드 경쟁 정책 필요 |
| D056/D059 | PLC → PC | 압력/변위 센서 오류 mask 읽기 | 기존 경로 유지 |
| 상승/하강/선택/HPU/속도/중립/정지 확인/명령 ID | 미확정 | 작업 지시서만으로 확정 불가 | 주소·비트·자료형·펄스/유지·순서·ACK·실제 정지 피드백 확정 전 차단 |

`PlcReadOnlyPlant.operationAllowed=false`, `Apply=false`, `StopAll=false`이다. 네트워크 명령을 수락했다는 사실과 실린더가 정지했다는 사실은 구분한다. 실제 PLC sequence number/수집 원자성/워드 읽기 사이의 동일 scan 보장도 미확정이다.

현재 메인 메뉴와 대시보드의 읽기 서비스 수명은 기존 구조를 유지한다. 실제 출력 통합 전에 앱 범위의 PLC 소유자 통합 및 전환 중 읽기 서비스 겹침 정책도 검토해야 한다. 새 어댑터 자체는 소켓을 추가하지 않는다.
