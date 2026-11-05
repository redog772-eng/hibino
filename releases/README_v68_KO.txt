STARTcraft v68 - 마린/SCV 집결 출발 및 일꾼 회복

1. 게임과 봇을 모두 종료합니다.
2. 이 ZIP을 새 폴더에 압축 해제합니다.
3. START_MATCH.cmd를 실행합니다. AI를 백업·컴파일한 뒤 Pluto 경기를 시작합니다.
기존 STARTcraft/Pluto, Python 3, MSBuild 환경이 필요합니다.
기본 경로: C:\Users\User\starcraft-api\STARTcraft
다음 경기부터 run_STARTcraft_vs_Pluto.cmd로 시작할 수 있습니다.
AI만 설치하려면 upgrade_ai_v68.cmd를 실행하세요.

화면은 사용자 제공 STARTcraft_borderless_fullscreen_v3_fixed.zip의
전체화면 창모드 실행기와 설정 도우미를 그대로 사용합니다.
BWAPI의 작은 기본 창모드는 OFF로 두고 기존 cnc-ddraw가 확대를 담당합니다.
ddraw.dll / ddraw.ini를 변경하거나 다시 설치하지 않습니다.
화면이 복구된 기존 경기 환경에서 사용하세요.

v67 경기에서 SCV 지원대는 0명인 채 마린이 0~2기로 줄었고,
본진 위협 이후 SCV가 16기에서 0기로 줄었습니다.
이번에는 마린 3기가 모이기 전 본진에서 대기하고, 조건이 충족되면 SCV 지원대와 출발합니다.
본진 위험·마린 전멸·부상·시간 제한 시 지원대를 복귀시킵니다.
일꾼이 줄면 가스 담당도 줄이고, SCV 8기 미만에서는 가스보다 광물 회복을 우선합니다.
정제소를 조금 일찍 준비하고 팩토리 자금 저축을 앞당겼습니다.
기본 빌드는 1배럭/1팩토리, 마린/SCV 지원, 벌처 2대, 전진 벙커, 탱크/마인입니다.
적 정찰·현장 병력·건설 조건이 맞지 않으면 전진 벙커를 강제로 짓지 않습니다.

검증: seed 680001, 현재 소스의 새 제어기 1,000개 모의 시나리오.
정상 조건 800건 탱크/마인 단계 도달, 길/건설 차단 조건 200건 별도 기록, 검사 실패 0.
v67이 실패하는 2마린 개별 출발 회귀 검사를 v68은 통과합니다.
집결 출발·본진 복귀·SCV 3기 광물 재배치 및 기존 구성 요소 검사도 통과합니다.
공식 BWAPI 4.4 헤더 문법 및 모의 설치·백업·복원 검사를 수행합니다.
이 수치는 실전 승률이 아닙니다. 실제 전투, 지형/충돌, Windows 전체 빌드는 미검증입니다.

경기 후 리플레이와 AI_reports\AI_v59_construction.csv를 보내주세요.
CSV의 새 행은 version=68이며 REPLAY_PUSH_START, REPLAY_STATE의
push_active / assault_workers / gas_target으로 실제 실행을 확인할 수 있습니다.
