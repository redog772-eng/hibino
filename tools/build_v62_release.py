"""Package v62 with verified-source upgrades and retained rollback safeguards."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment


def main():
    source = Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v61_single_owner_natural.zip') as archive:
        installer = archive.read('install_ai_v61.py').decode()
        command = archive.read('upgrade_ai_v61.cmd').decode().replace('v61', 'v62')
    prior = assignment(installer, 'NEW_SHA256')
    installer = installer.replace('v61', 'v62')
    installer = installer.replace('AI v62: single combat owner and natural staging',
                                  'AI v62: bunker boarding and opening mech priority')
    installer = installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
                                  f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer = replace_assignment(installer, 'SOURCE_B64', base64.b64encode(zlib.compress(source,9)).decode())
    installer = replace_assignment(installer, 'NEW_SHA256', hashlib.sha256(source).hexdigest())
    installer = installer.replace('Expected verified v54-v60.1', 'Expected verified v54-v61')
    compile(installer, 'install_ai_v62.py', 'exec')
    readme = '''STARTcraft v62 - 벙커 탑승 중복과 초반 메카닉 우선순위 수정
게임과 봇 종료 -> 새 폴더에 압축 해제 -> upgrade_ai_v62.cmd 실행.
검증된 v54-v61 소스를 지원합니다. 알 수 없는 수정 소스는 거부합니다.
벙커가 해병을 부르는 명령 대신 해병이 직접 벙커로 우클릭합니다.
접근 중 해병을 포함해 네 자리까지만 탑승을 요청하고 192프레임 동안 재요청하지 않습니다.
기지 침투 중에는 기존 탑승 제한과 기지 방어 우선권을 유지합니다.
TvP 한 기지에서 첫 팩토리/탱크 두 대/시즈업 자금을 SCV와 추가 해병보다 우선합니다.
최소 해병 네 기까지는 생산 여유를 두고, 준비 완료 또는 두 기지 이후 자금 보류를 해제합니다.
시즈업은 팩토리 생산 지출 전에 시도하고, 추가 팩토리는 탱크 두 대와 시즈업 착수 후 증설합니다.
초반 일반 서플라이 예비 자금은 실제 차단 위험에 가까울 때만 보류합니다.
v61의 전투 명령 통합과 앞마당 배치 규칙을 유지합니다.
AI_v59_construction.csv의 version 열은 62이며 OPENING_BUDGET 진단을 추가합니다.
AI_v59_breach.csv / AI_v59_siege_line.csv 파일명도 유지됩니다.
실제 제어기 명령 스텁, 정책 경계, 공식 BWAPI 헤더 문법 및 설치/복원 모의 검증 완료.
전체 Windows 빌드와 새 Pluto 경기의 성능은 아직 미검증입니다.
'''
    path = Path('releases/STARTcraft_AI_v62_bunker_opening_priority.zip')
    with zipfile.ZipFile(path, 'w') as archive:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v62.py',installer.encode()),
                          ('upgrade_ai_v62.cmd',command.encode()),('README_KO.txt',readme.encode())]:
            add_file(archive,name,data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)


if __name__ == '__main__':
    main()
