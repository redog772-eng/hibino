"""Build v59 from the v58 verified-source, rollback-capable installer."""
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file, assignment, replace_assignment

def main():
    source=Path('source/StarterBot.cpp').read_bytes()
    with zipfile.ZipFile('releases/STARTcraft_AI_v58_hold_screen_one_barracks.zip') as z:
        installer=z.read('install_ai_v58.py').decode()
        command=z.read('upgrade_ai_v58.cmd').decode().replace('v58','v59')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v58','v59').replace('hold screen and one barracks mech','high ground covered tank advance')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',hashlib.sha256(source).hexdigest())
    installer=installer.replace('Expected verified v54-v57.1 or recoverable v59','Expected verified v54-v58 or recoverable v59')
    compile(installer,'install_ai_v59.py','exec')
    readme='''STARTcraft v59 - 언덕 집결과 엄호 후 전진
게임과 봇 종료 -> upgrade_ai_v59.cmd 실행 -> SUCCESS 확인 -> 기존 경기 실행 파일 실행.
확인된 v54~v58 소스에서 설치할 수 있습니다.
앞마당 준비 로직은 탱크에게 이동/시즈 명령을 내리지 않습니다.
본진 높이 이상의 보행 가능한 공간에 먼저 탱크 3대를 시즈 배치합니다.
시즈 탱크 2대, 호위 3기, 적 접근 없음, 이동 구간 보행 가능 조건에서만 한 대를 전진시킵니다.
목표 지점은 엄호 탱크로부터 320픽셀 이내여야 합니다.
이동 중 적 지상 전투 유닛이 192픽셀 안에 접근하면 엄호 탱크 쪽으로 후퇴합니다.
1배럭/팩토리 증설과 지원 병력 홀드는 유지합니다.
로그: AI_v59_construction.csv, AI_v59_breach.csv, AI_v59_siege_line.csv.
1,000회 전진 정책 검사와 1,000회 지원 병력 명령 검사 통과.
이 검사는 StarCraft 엔진 경기가 아닙니다. 전체 Windows 빌드/실전은 설치 후 확인해야 합니다.
'''

    path=Path('releases/STARTcraft_AI_v59_high_ground_cover.zip')
    with zipfile.ZipFile(path,'w') as z:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v59.py',installer.encode()),('upgrade_ai_v59.cmd',command.encode()),('README_KO.txt',readme.encode())]:add_file(z,name,data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)
if __name__=='__main__':main()
