"""Package the current source, reviewed launcher and hash-bound receipts."""
import base64
import hashlib
import json
from pathlib import Path
import zipfile
import zlib
from build_v53_release import add_file,assignment,replace_assignment

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    source=Path('source/StarterBot.cpp').read_bytes();digest=sha('source/StarterBot.cpp')
    receipts=[]
    for name in ['current_v66_1000','current_v66_validation_1000']:
        path=Path('simulation/results')/name/'summary.json';r=json.loads(path.read_text())
        assert r['source_version']==66 and r['source_sha256']==digest
        assert r['runner_sha256']==sha('simulation/run_current.py')
        assert r['adapter_sha256']==sha('simulation/current_runtime.cpp')
        assert r['mobile_test_sha256']==sha('tools/check_v58_hold.py')
        assert r['runs']==1000 and r['core_not_ready_by_deadline']==0
        assert r['current_mobile_command_checks']=='passed'
        for key,script in [('movement_route_model','run_ground_routes.py'),('bound_motion_model','run_bound_motion.py'),('air_expansion_decisions','run_air_expansion.py')]:
            child=r[key]
            assert child['status']=='passed' and child['source_sha256']==digest and child['runs']==1000
            assert child['runner_sha256']==sha('simulation/'+script)
        assert r['movement_route_model']['reachable_stalled']==r['movement_route_model']['unsafe_cases']==0
        assert r['bound_motion_model']['ownership_released']==1000
        assert r['air_expansion_decisions']['failed']==0
        assert r['air_expansion_decisions']['adapter_sha256']==sha('simulation/air_runtime.cpp')
        assert r['air_expansion_decisions']['director']==dict(passed=1000,activated=600,lost=100,blocked=300)
        receipts.append((name,path.read_bytes(),r['seed']))
    assert receipts[0][2]!=receipts[1][2]
    with zipfile.ZipFile('releases/STARTcraft_AI_v65_tank_movement_recovery.zip') as old:
        installer=old.read('install_ai_v65.py').decode()
        command=old.read('upgrade_ai_v65.cmd').decode().replace('v65','v66')
    prior=assignment(installer,'NEW_SHA256')
    installer=installer.replace('v65','v66').replace('AI v66: bounded Tank movement recovery','AI v66: alternate air expansion and matched windows')
    installer=installer.replace('current_sha not in (OLD_SHA256,NEW_SHA256,',f'current_sha not in (OLD_SHA256,NEW_SHA256,{prior!r},')
    installer=replace_assignment(installer,'SOURCE_B64',base64.b64encode(zlib.compress(source,9)).decode())
    installer=replace_assignment(installer,'NEW_SHA256',digest)
    installer=installer.replace('Expected verified v54-v64','Expected verified v54-v65')
    compile(installer,'install_ai_v66.py','exec')
    path=Path('releases/STARTcraft_AI_v66_air_expansion_windows.zip')
    with zipfile.ZipFile(path,'w') as archive:
        for name,data in [('source/StarterBot.cpp',source),('install_ai_v66.py',installer.encode()),('upgrade_ai_v66.cmd',command.encode()),('README_KO.txt',Path('releases/README_v66_KO.txt').read_bytes())]:add_file(archive,name,data)
        for file in ['run_arena.ps1','configure_arena_windows.py','arena_display.ps1']:
            add_file(archive,'tools/'+file,Path('tools',file).read_bytes())
        add_file(archive,'run_STARTcraft_vs_Pluto.cmd',Path('tools/run_STARTcraft_vs_Pluto.cmd').read_bytes())
        for name,data,_ in receipts:add_file(archive,'validation/'+name+'.json',data)
    path.with_suffix('.sha256').write_text(sha(path)+'  '+path.name+'\n')
    print(path)

if __name__=='__main__':main()
