"""Build the single-owner / natural staging upgrade from the v60.1 installer."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment


def main():
    source = Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v60_1_marker_fix.zip') as archive:
        installer = archive.read('install_ai_v60.py').decode()
        command = archive.read('upgrade_ai_v60.cmd').decode().replace('v60', 'v61')
    prior = assignment(installer, 'NEW_SHA256')
    installer = installer.replace('v60', 'v61')
    installer = installer.replace('AI v61: combat order and medic support',
                                  'AI v61: single combat owner and natural staging')
    installer = installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
                                  f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer = replace_assignment(installer, 'SOURCE_B64', base64.b64encode(zlib.compress(source, 9)).decode())
    installer = replace_assignment(installer, 'NEW_SHA256', hashlib.sha256(source).hexdigest())
    installer = installer.replace('Expected verified v54-v59', 'Expected verified v54-v60.1')
    compile(installer, 'install_ai_v61.py', 'exec')
    readme = '''STARTcraft v61 - 전투 명령 소유권과 앞마당 준비 수정
게임과 봇 종료 -> 새 폴더에 압축 해제 -> upgrade_ai_v61.cmd 실행.
검증된 v54-v60.1 소스를 지원합니다. 알 수 없는 수정 소스는 거부합니다.
TvP 해병/벌처/골리앗/메딕 이동·교전·벙커 탑승은 하나의 제어기가 담당합니다.
예약된 벌처 지뢰 작업과 스팀팩 능력은 별도 담당을 유지합니다.
호위병은 가까운 벙커 대신 전진한 시즈 탱크를 따라갑니다.
방어선 주변에서 사거리 안에 들어온 적에게 사격하고, 이탈한 적은 추격하지 않습니다.
앞마당 준비는 탱크 2대부터 짧은 교대 이동을 허용합니다.
엄호 탱크 1대 이상, 호위병 2기 이상, 접근한 적 없음, 보행 가능 조건을 요구합니다.
공격 전진은 탱크 3대/엄호 2대/호위 3기 조건을 유지합니다.
확장은 앞마당 400픽셀 안에 배치된 시즈 탱크 2대와 접근로 안전 확인 후 허용합니다.
기존 AI_v59_construction.csv 파일명을 유지하되 version 열에는 61을 기록합니다.
NATURAL_WAIT / NATURAL_READY / NATURAL_LINE으로 앞마당 정체 이유를 기록합니다.
명령 스텁·정책 검사와 설치 트랜잭션 모의 검증 완료.
전체 Windows/BWAPI 빌드와 새 Pluto 경기 결과는 미검증입니다.
기존 v59_siege_line.csv와 v59_breach.csv 파일명도 유지됩니다.
'''
    path = Path('releases/STARTcraft_AI_v61_single_owner_natural.zip')
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in [('source/StarterBot.cpp', source), ('install_ai_v61.py', installer.encode()),
                           ('upgrade_ai_v61.cmd', command.encode()), ('README_KO.txt', readme.encode())]:
            add_file(archive, name, data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n')
    print(path)


if __name__ == '__main__':
    main()
