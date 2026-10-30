"""Build v58 from the v57.1 verified-source, rollback-capable installer."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment

def main():
    source=Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v57_1_compile_fix.zip') as z:
        installer=z.read('install_ai_v57.py').decode()
        command=z.read('upgrade_ai_v57.cmd').decode().replace('v57','v58')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v57','v58').replace('bunker breach emergency defense','hold screen and one barracks mech')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',hashlib.sha256(source).hexdigest())
    installer=installer.replace('Expected exact v54, v55, v56 or recoverable v58','Expected verified v54-v57.1 or recoverable v58')
    compile(installer,'install_ai_v58.py','exec')
    readme='''STARTcraft v58 - 추격 중단과 1배럭 메카닉
게임과 봇 종료 -> upgrade_ai_v58.cmd 실행 -> SUCCESS 확인 -> 기존 경기 실행 파일 실행.
확인된 v54~v57.1 소스에서 바로 설치할 수 있습니다.

플토전 추가 배럭 건설 금지. 1베이스 팩토리 목표 2개, 2베이스 이상 목표 5개.
360픽셀 내 적 직접 공격과 상대 멀티 마린 출격 로직 중단.
마린/벌처/골리앗/메딕은 아군 벙커 또는 시즈 탱크 근처로 이동 후 홀드.
본진 돌파가 감지되면 가까운 병력은 본진 안으로 지원 후 홀드.
벌처 마인 설치 담당은 기존 이동/마인 설치 임무 유지.

실행 버전 확인: AI_reports/AI_v58_construction.csv
긴급 방어 기록: AI_reports/AI_v58_breach.csv
전체 Windows 빌드와 실제 경기 확인은 설치 후 필요합니다.
'''
    path=Path('releases/STARTcraft_AI_v58_hold_screen_one_barracks.zip')
    with zipfile.ZipFile(path,'w') as z:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v58.py',installer.encode()),('upgrade_ai_v58.cmd',command.encode()),('README_KO.txt',readme.encode())]:add_file(z,name,data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)
if __name__=='__main__':main()
