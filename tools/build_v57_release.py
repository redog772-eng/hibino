"""Package v57 using the v56.1 rollback installer and verified upgrade hashes."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment

def main():
    source = Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v56_1_installer_fix.zip') as z:
        installer = z.read('install_ai_v56.py').decode()
        command = z.read('upgrade_ai_v56.cmd').decode().replace('v56', 'v57')
    prior = assignment(installer, 'NEW_SHA256')
    with zipfile.ZipFile('releases/STARTcraft_AI_v57_bunker_breach_defense.zip') as z:
        original_v57 = assignment(z.read('install_ai_v57.py').decode(), 'NEW_SHA256')
    installer = installer.replace('v56', 'v57')
    installer = installer.replace('anchored natural and leapfrog siege line', 'bunker breach emergency defense')
    installer = installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
                                  f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},{original_v57!r},')
    installer = installer.replace('Expected exact v54, v55 or recoverable v57',
                                  'Expected exact v54, v55, v56 or recoverable v57')
    installer = replace_assignment(installer, 'SOURCE_B64', base64.b64encode(zlib.compress(source,9)).decode())
    installer = replace_assignment(installer, 'NEW_SHA256', hashlib.sha256(source).hexdigest())
    compile(installer, 'install_ai_v57.py', 'exec')
    assert zlib.decompress(base64.b64decode(assignment(installer,'SOURCE_B64')))==source
    readme = '''STARTcraft AI v57.1 - AIV25Seen 선언 누락 컴파일 오류 수정
게임과 봇을 종료한 뒤 upgrade_ai_v57.cmd를 실행하세요.
SUCCESS가 표시된 뒤 기존 run_STARTcraft_vs_Pluto.cmd로 한 경기 실행하세요.
v54/v55/v56의 확인된 소스에서 직접 업데이트할 수 있습니다.

벙커 사거리 밖으로 들어온 적에게 벙커 방어력을 잘못 계산하지 않습니다.
본진 안쪽 지상 적 발견 시 건강한 SCV를 최대 10기까지 지원합니다.
최소 4기는 채취용으로 남기며 건설/수리/가스 작업자는 제외합니다.
멀리 접근 중인 적에게는 SCV를 본진 앞에 모아 홀드합니다.
벙커 전방에 적이 없으면 마린을 내려 본진 방어에 합류시킵니다.
위협이 사라진 후 96프레임 동안 재탑승을 막습니다.
긴급 방어 명령은 일반 집결/탑승 명령 뒤에 실행합니다.
건물로 완전 봉쇄하는 맵별 배치는 이번 변경에 포함되지 않습니다.

AI_reports/AI_v57_construction.csv로 실행 버전을 확인하세요.
AI_reports/AI_v57_breach.csv 열: frame,cc_id,breached,ground_threat,defender_power,desired_scv,no_load_until
이 환경에서는 Windows 컴파일과 실제 경기를 실행하지 못했습니다.
오프라인 검사는 승률을 의미하지 않습니다.
'''
    path=Path('releases/STARTcraft_AI_v57_1_compile_fix.zip')
    with zipfile.ZipFile(path,'w') as z:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v57.py',installer.encode()),('upgrade_ai_v57.cmd',command.encode()),('README_KO.txt',readme.encode())]:
            add_file(z,name,data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)

if __name__=='__main__':main()
