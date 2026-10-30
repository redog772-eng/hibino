"""Package the combat-order fix without rewriting historical releases."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment


def main():
    source = Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v59_high_ground_cover.zip') as archive:
        installer = archive.read('install_ai_v59.py').decode()
        command = archive.read('upgrade_ai_v59.cmd').decode().replace('v59', 'v60')
    prior = assignment(installer, 'NEW_SHA256')
    installer = installer.replace('v59', 'v60')
    installer = installer.replace(
        'current_sha not in (OLD_SHA256,NEW_SHA256,',
        f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer = replace_assignment(installer, 'SOURCE_B64', base64.b64encode(zlib.compress(source, 9)).decode())
    installer = replace_assignment(installer, 'NEW_SHA256', hashlib.sha256(source).hexdigest())
    installer = installer.replace('Expected verified v54-v58', 'Expected verified v54-v59')
    compile(installer, 'install_ai_v60.py', 'exec')
    readme = '''STARTcraft v60 - 공격 동작 보존과 방어선 메딕 치료
게임과 봇 종료 -> upgrade_ai_v60.cmd 실행 -> SUCCESS 확인.
검증된 v54-v59 소스를 지원하며 알 수 없는 수정 소스는 거부합니다.
마지막 홀드 제어기가 공격 동작을 취소하지 않도록 보호합니다.
메딕은 방어 거점 160픽셀 안의 부상 생체 병력을 치료합니다.
치료 명령이 같은 대상에게 이미 내려졌으면 반복하지 않습니다.
탱크 전진 규칙과 기존 v59 로그 파일명은 유지합니다.
C++ 명령 스텁 검증 완료. 전체 Windows/BWAPI 빌드와 실제 경기는 미검증입니다.
실전 전투력 개선은 아직 측정하지 않았습니다.
'''
    path = Path('releases/STARTcraft_AI_v60_combat_order_support.zip')
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in [('source/StarterBot.cpp', source),
                           ('install_ai_v60.py', installer.encode()),
                           ('upgrade_ai_v60.cmd', command.encode()),
                           ('README_KO.txt', readme.encode())]:
            add_file(archive, name, data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n')
    print(path)


if __name__ == '__main__':
    main()
