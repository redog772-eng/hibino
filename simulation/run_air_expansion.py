"""Exercise actual C++ decisions/director with scripted commands, not engine matches."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=ROOT/'source/StarterBot.cpp')
p.add_argument('--runs',type=int,default=1000)
p.add_argument('--seed',type=int,default=660001)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if a.runs<1:p.error('positive runs required')
raw=a.source.read_bytes();s=raw.decode()
actual=s[s.index('enum AIV66Action'):s.index('static int g_v66Previous=')]
cpp=r'''
#include <random>
#include <iostream>
'''+actual+r'''
int main(int,char** argv){
 const int runs=std::stoi(argv[1]);std::mt19937 rng(std::stoul(argv[2]));
 int checks=0;
 for(int i=0;i<runs;++i){
  int workers=rng()%4, age=rng()%1200, stalled=rng()%720;
  auto test=[&](bool e,bool p,bool h,bool c,bool f,bool at,bool near,bool safe,bool land,int w,int ag,int st,int expected){
   ++checks;int got=AIV66FlightAction(e,p,h,c,f,at,near,safe,land,w,ag,st);
   if(got!=expected){std::cerr<<"scenario "<<i<<" expected "<<expected<<" got "<<got;std::exit(1);}
  };
  test(0,0,0,0,0,0,0,1,0,0,age,stalled,A66_BUILD);
  test(0,1,0,0,0,0,0,1,0,0,age,stalled,A66_WAIT);
  test(0,1,0,0,0,0,0,1,0,0,1200+rng()%3000,stalled,A66_REPLAN);
  test(0,0,1,0,0,0,0,1,0,0,age,stalled,A66_LOST);
  test(1,0,1,0,0,0,0,1,0,0,age,stalled,A66_WAIT);
  test(1,0,1,1,0,0,0,1,0,0,age,stalled,A66_LIFT);
  test(1,0,1,1,1,0,0,1,0,0,age,stalled,A66_MOVE);
  test(1,0,1,1,1,0,1,1,0,0,age,stalled,A66_MOVE);
  test(1,0,1,1,1,0,1,1,1,0,age,stalled,A66_LAND);
  test(1,0,1,1,0,1,1,1,1,workers,age,stalled,A66_SEED);
  test(1,0,1,1,0,1,1,1,1,4+rng()%12,age,stalled,A66_ACTIVE);
  test(1,0,1,1,1,0,1,0,1,workers,age,stalled,A66_REPLAN);
  test(1,0,1,1,0,0,1,1,1,workers,age,720+rng()%3000,A66_REPLAN);
  test(1,0,1,1,1,0,1,1,1,workers,age,720+rng()%3000,A66_REPLAN);
 }
 std::cout<<checks;
}
'''
with tempfile.TemporaryDirectory() as d:
    src=Path(d)/'air.cpp';exe=Path(d)/'air';src.write_text(cpp)
    subprocess.run(['g++','-std=c++20','-O2',str(src),'-o',str(exe)],check=True)
    checks=int(subprocess.check_output([str(exe),str(a.runs),str(a.seed)],text=True))
    template=(ROOT/'simulation/air_runtime.cpp').read_text()
    start=s.index('static void AIV66ExpansionDirector()')
    end=s.index('\nvoid AIV13EconomyDirector()',start)
    state=s[s.index('enum AIV66Action'):s.index('static bool AIV66SiteSafe(')]
    src.write_text(template.replace('// @STATE@',state).replace('// @DIRECTOR@',s[start:end]))
    subprocess.run(['g++','-std=c++20','-O2',str(src),'-o',str(exe)],check=True)
    director=json.loads(subprocess.check_output([str(exe),str(a.runs)],text=True))
r=dict(source_sha256=hashlib.sha256(raw).hexdigest(),runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),runs=a.runs,seed=a.seed,passed=a.runs,failed=0,decision_checks=checks,
 adapter_sha256=hashlib.sha256(template.encode()).hexdigest(),director=director,
 validation_type='actual_cpp_flight_decisions_and_scripted_director_not_engine_matches',
 unrun=['Real worker/depot association (scripted in director adapter)',
        'Mineral-only site search, placement, terrain and safety perception',
        'BWAPI command acceptance, travel, landing, construction and mining in the engine'])
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'summary.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
