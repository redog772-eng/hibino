"""Build v63, binding included model receipts to the packaged source hash."""
import base64
import hashlib
import json
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file,assignment,replace_assignment


def main():
    source=Path('source/StarterBot.cpp').read_bytes()
    digest=hashlib.sha256(source).hexdigest()
    receipts=[]
    for directory in ['current_v63_1000','current_v63_validation_1000']:
        path=Path('simulation/results')/directory/'summary.json'
        result=json.loads(path.read_text())
        assert result['source_version']==63 and result['source_sha256']==digest
        assert result['runner_sha256']==hashlib.sha256(Path('simulation/run_current.py').read_bytes()).hexdigest()
        assert result['adapter_sha256']==hashlib.sha256(Path('simulation/current_runtime.cpp').read_bytes()).hexdigest()
        assert result['mobile_test_sha256']==hashlib.sha256(Path('tools/check_v58_hold.py').read_bytes()).hexdigest()
        assert result['runs']==1000 and result['current_mobile_command_checks']=='passed'
        assert result['core_not_ready_by_deadline']==0
        receipts.append((directory,result,path.read_bytes()))
    assert receipts[0][1]['seed']!=receipts[1][1]['seed']
    with zipfile.ZipFile('releases/STARTcraft_AI_v62_bunker_opening_priority.zip') as archive:
        installer=archive.read('install_ai_v62.py').decode()
        command=archive.read('upgrade_ai_v62.cmd').decode().replace('v62','v63')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v62','v63').replace(
        'AI v63: bunker boarding and opening mech priority','AI v63: reserve siege research gas')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',
        f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',digest)
    installer=installer.replace('Expected verified v54-v61','Expected verified v54-v62')
    compile(installer,'install_ai_v63.py','exec')
    readme='''STARTcraft v63 - 첫 탱크 이후 시즈업 가스 확보
게임과 봇 종료 -> 새 폴더에 압축 해제 -> upgrade_ai_v63.cmd 실행.
검증된 v54-v62 소스를 지원합니다. 알 수 없는 수정 소스는 거부합니다.
첫 탱크는 기존처럼 생산하고, 이후 시즈업 착수 전까지 가스 150을 남깁니다.
탱크/메딕 생산이 시즈업 가스를 먼저 쓰지 못하게 하며 연구 시작 후 예약을 해제합니다.
기존 v62의 벙커 탑승·전투 명령 소유권·앞마당 준비 규칙을 유지합니다.
현재 v63 소스를 실제 실행하는 제한 모델로 1,000개 조건 + 다른 시드 1,000개 조건을 검증했습니다.
두 실행 모두 탱크 두 대와 시즈업 준비 완료 1,000/1,000입니다.
같은 조건의 v62는 978/1,000이며, v63의 첫 탱크 완성 프레임은 1,000개 조건 전부 동일합니다.
이 숫자는 승률이 아닙니다. 적 병력·지형 전투·전체 경제 제어기는 모델에 포함되지 않습니다.
검증 요약은 ZIP의 validation 폴더에 포함됩니다.
로그 파일명 AI_v59_construction.csv는 유지하며 version 열은 63입니다.
OPENING_BUDGET의 siege_gas_reserve 필드로 예약 상태를 확인할 수 있습니다.
공식 BWAPI 헤더 문법/명령/설치 복원 검증 대상이며 전체 Windows 링크 빌드와 실전 결과는 미검증입니다.
'''
    path=Path('releases/STARTcraft_AI_v63_siege_gas_priority.zip')
    with zipfile.ZipFile(path,'w') as archive:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v63.py',installer.encode()),
                          ('upgrade_ai_v63.cmd',command.encode()),('README_KO.txt',readme.encode())]:
            add_file(archive,name,data)
        for name,result,data in receipts:
            add_file(archive,'validation/'+name+'.json',data)
    path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
    print(path)


if __name__=='__main__':main()
