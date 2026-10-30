"""Package ground-route natural reinforcement with source-bound model receipts."""
import base64
import hashlib
import json
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file,assignment,replace_assignment


def main():
    source=Path('source/StarterBot.cpp').read_bytes();digest=hashlib.sha256(source).hexdigest()
    receipts=[]
    for directory in ['current_v64_1000','current_v64_validation_1000']:
        path=Path('simulation/results')/directory/'summary.json';result=json.loads(path.read_text())
        assert result['source_version']==64 and result['source_sha256']==digest
        assert result['runner_sha256']==hashlib.sha256(Path('simulation/run_current.py').read_bytes()).hexdigest()
        assert result['adapter_sha256']==hashlib.sha256(Path('simulation/current_runtime.cpp').read_bytes()).hexdigest()
        assert result['mobile_test_sha256']==hashlib.sha256(Path('tools/check_v58_hold.py').read_bytes()).hexdigest()
        assert result['runs']==1000 and result['core_not_ready_by_deadline']==0
        assert result['current_mobile_command_checks']=='passed'
        route=result['movement_route_model']
        assert route['source_sha256']==digest and route['runs']==1000
        assert route['runner_sha256']==hashlib.sha256(Path('simulation/run_ground_routes.py').read_bytes()).hexdigest()
        assert route['reachable_stalled']==0 and route['unsafe_cases']==0
        assert route['arrived']==route['reachable']
        receipts.append((directory,result,path.read_bytes()))
    assert receipts[0][1]['seed']!=receipts[1][1]['seed']
    with zipfile.ZipFile('releases/STARTcraft_AI_v63_siege_gas_priority.zip') as archive:
        installer=archive.read('install_ai_v63.py').decode()
        command=archive.read('upgrade_ai_v63.cmd').decode().replace('v63','v64')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v63','v64').replace(
        'AI v64: reserve siege research gas','AI v64: ground route natural reinforcement')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
        f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',digest)
    installer=installer.replace('Expected verified v54-v62','Expected verified v54-v63')
    compile(installer,'install_ai_v64.py','exec')
    readme='''STARTcraft v64 - 앞마당 지상 경로와 증원 합류
게임과 봇 종료 -> 새 폴더에 압축 해제 -> upgrade_ai_v64.cmd 실행.
검증된 v54-v63 소스를 지원합니다. 알 수 없는 수정 소스는 거부합니다.
앞마당 준비 중 탱크와 호위병이 같은 지상 경로를 기준으로 전방 탱크를 고릅니다.
새 탱크는 가까운 후방 탱크 대신 전방 방어선으로 합류합니다.
앞마당 이동은 지상 격자 경로를 따라 짧은 엄호 구간으로 나눕니다.
탱크 몸체와 장애물 모서리를 확인하고, 막힌 직선으로 반복 전진하지 않습니다.
이동 중인 탱크는 목표 가까이 도착한 뒤 시즈합니다.
기존 적 접촉·엄호·호위 조건과 v63의 시즈업 가스 예약을 유지합니다.
두 시드에서 현재 버전 생산 모델 1,000/1,000 완료.
이동 모델도 시드마다 이동 가능 900건 도착, 단절 100건 정지, 안전 조건 위반 0건.
모델은 실제 Destination 지형, 실전 충돌, 적 병력 교전을 재현하지 않습니다.
전체 Windows 빌드와 새 Pluto 경기 결과는 미검증입니다. 승률 수치가 아닙니다.
기존 AI_v59_construction.csv 파일명의 version 열은 64입니다.
NATURAL_BOUND에 route/cover/screen/path/gap을 기록합니다.
다음 경기에서는 리플레이와 construction/breach/siege_line CSV를 함께 보관해 주세요.
검증 요약은 ZIP의 validation 폴더에 포함됩니다.
'''
    path=Path('releases/STARTcraft_AI_v64_ground_route_natural.zip')
    with zipfile.ZipFile(path,'w') as archive:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v64.py',installer.encode()),
                          ('upgrade_ai_v64.cmd',command.encode()),('README_KO.txt',readme.encode())]:
            add_file(archive,name,data)
        for name,result,data in receipts:add_file(archive,'validation/'+name+'.json',data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)


if __name__=='__main__':main()
