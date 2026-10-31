"""Package v65 with source-matching production, route and motion receipts."""
import base64
import hashlib
import json
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file,assignment,replace_assignment


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    source=Path('source/StarterBot.cpp').read_bytes();digest=hashlib.sha256(source).hexdigest()
    receipts=[]
    for directory in ['current_v65_1000','current_v65_validation_1000']:
        path=Path('simulation/results')/directory/'summary.json';r=json.loads(path.read_text())
        assert r['source_version']==65 and r['source_sha256']==digest
        assert r['runner_sha256']==sha('simulation/run_current.py')
        assert r['adapter_sha256']==sha('simulation/current_runtime.cpp')
        assert r['mobile_test_sha256']==sha('tools/check_v58_hold.py')
        assert r['runs']==1000 and r['core_not_ready_by_deadline']==0 and r['current_mobile_command_checks']=='passed'
        route=r['movement_route_model'];motion=r['bound_motion_model']
        assert route['status']==motion['status']=='passed'
        assert route['source_sha256']==motion['source_sha256']==digest
        assert route['runner_sha256']==sha('simulation/run_ground_routes.py')
        assert motion['runner_sha256']==sha('simulation/run_bound_motion.py')
        assert route['runs']==motion['runs']==1000 and route['reachable_stalled']==route['unsafe_cases']==0
        assert motion['ownership_released']==1000 and motion['unsafe_or_unbounded']==0
        receipts.append((directory,r,path.read_bytes()))
    assert receipts[0][1]['seed']!=receipts[1][1]['seed']
    with zipfile.ZipFile('releases/STARTcraft_AI_v64_ground_route_natural.zip') as archive:
        installer=archive.read('install_ai_v64.py').decode()
        command=archive.read('upgrade_ai_v64.cmd').decode().replace('v64','v65')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v64','v65').replace(
        'AI v65: ground route natural reinforcement','AI v65: bounded Tank movement recovery')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
        f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',digest)
    installer=installer.replace('Expected verified v54-v63','Expected verified v54-v64')
    compile(installer,'install_ai_v65.py','exec')
    readme='''STARTcraft v65 - 탱크 이동 정체 회복
게임과 봇 종료 -> 새 폴더에 압축 해제 -> upgrade_ai_v65.cmd 실행.
검증된 v54-v64 소스를 지원합니다. 알 수 없는 수정 소스는 거부합니다.
앞마당 이동 중 144프레임 동안 충분한 진전이 없거나 480프레임을 넘으면 회복을 시작합니다.
엄호 아래에서는 현재 위치에 시즈한 것을 확인한 뒤 이동 담당을 해제합니다.
위험하거나 엄호가 없으면 후퇴 명령 후 담당을 해제합니다.
시즈/후퇴 명령 실패도 추가 144프레임 뒤 정지/재분류하며, 기동 모드 탱크를 엄호로 세지 않습니다.
막힌 도착점은 720프레임 재사용을 보류하며 더 짧은 빈 중간점을 선택할 수 있습니다.
선두 탱크도 뒤 탱크의 엄호 조건을 충족하면 이동 후보가 될 수 있습니다.
두 시드 각각: 생산 모델 1,000회 완료, 경로 모델 900건 도착/단절 100건 정지.
실제 이동 상태 처리 코드도 시드마다 지연·막힘·위험·명령 실패 등 1,000건 검증했습니다.
기존 v64 동일 이동 모델은 625건 미해제, v65는 1,000건 모두 해제했습니다.
이 숫자는 승률이 아닙니다. 실제 엔진 충돌/전체 전투/전체 편대 제어는 재현하지 않습니다.
공식 BWAPI 헤더 문법 및 설치/복원 모의 검사 대상이며 전체 Windows 빌드와 실전 결과는 미검증입니다.
기존 CSV 파일명을 유지하며 construction의 version 열은 65입니다.
NATURAL_MOVE / NATURAL_MOVE_ABORT / NATURAL_SETTLE_FAILED로 정체와 회복을 기록합니다.
다음 경기 리플레이와 construction/breach/siege_line CSV를 함께 보관해 주세요.
검증 요약은 ZIP의 validation 폴더에 포함됩니다.
'''
    path=Path('releases/STARTcraft_AI_v65_tank_movement_recovery.zip')
    with zipfile.ZipFile(path,'w') as archive:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v65.py',installer.encode()),
                          ('upgrade_ai_v65.cmd',command.encode()),('README_KO.txt',readme.encode())]:
            add_file(archive,name,data)
        for name,r,data in receipts:add_file(archive,'validation/'+name+'.json',data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)


if __name__=='__main__':main()
