import sys,zipfile,tempfile,base64,zlib
from pathlib import Path
sys.path.insert(0,'tools')
from build_v53_release import assignment
with zipfile.ZipFile('releases/STARTcraft_AI_v59_high_ground_cover.zip') as z:
 assert z.testzip() is None
 src=z.read('source/StarterBot.cpp');code=z.read('install_ai_v59.py').decode()
ns={'__name__':'test'};exec(code,ns)
with tempfile.TemporaryDirectory() as d:
 root=Path(d);ns['ROOT']=root;ns['MSBUILD']=root/'msbuild'
 for name in ['src/starterbot/StarterBot.cpp','bin/StarterBot.exe','visualstudio/STARTcraft.sln','msbuild']:
  f=root/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b'fixture')
 f=root/'src/starterbot/StarterBot.cpp';sys.argv=['installer','--check-only']
 for v,name in [(54,'construction_resume_tech_guard'),(55,'prepared_natural_disciplined_response'),(56,'leapfrog_siege_no_chase'),(57,'bunker_breach_defense'),(57,'1_compile_fix'),(58,'hold_screen_one_barracks')]:
  with zipfile.ZipFile(f'releases/STARTcraft_AI_v{v}_{name}.zip') as z:
   data=zlib.decompress(base64.b64decode(assignment(z.read(f'install_ai_v{v}.py').decode(),'SOURCE_B64')))
  f.write_bytes(data);ns['main']();assert f.read_bytes()==data
 f.write_bytes(b'unknown')
 try:ns['main']()
 except RuntimeError:pass
 else:raise AssertionError('unknown accepted')
 assert f.read_bytes()==b'unknown'
 f.write_bytes(src);ns['main']();assert f.read_bytes()==src
s=src.decode();body=s[s.index('void StarterBot::onFrame()'):s.index('void StarterBot::sendIdleWorkersToMinerals()')]
assert body.count('AIV23ReactiveDefense();')==1
assert body.index('AIV23ReactiveDefense();')>body.index('AIV26EarlyDefenseAndRecon();')
assert 'now>g_v57NoLoadUntil' in s
assert src==Path('source/StarterBot.cpp').read_bytes()
print('PASS: upgrades v54-v59, unknown rejection, emergency precedence, embedded source')

assert body.index('AIV58HoldScreen();')>body.index('AIV23ReactiveDefense();')
