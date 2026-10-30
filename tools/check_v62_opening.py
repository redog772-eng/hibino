"""Check actual opening budget and its spending/research integration points."""
from pathlib import Path
import subprocess
import tempfile
from check_v61_ownership import function

worker = function('AIV11TrainWorkers')
produce = function('AIV11Produce')
assert 'me->minerals()<openingBudget+50' in worker
assert 'c.marine>=4 && me->minerals()<openingBudget+50' in produce
assert produce.index('if(tvp)AIV3ResearchSiegeMode();') < produce.index('for(auto fact:')
assert '(tvp?8:22)' in produce
scale = function('AIV22ScaleProduction')
assert scale.index('c.tank<2') < scale.index('AIV25AdaptiveGoals(')
mobile = function('AIV58HoldScreen')
assert 'anchor->load(u)' not in mobile
assert 'u->rightClick(anchor)' in mobile and 'size()+incoming<4' in mobile

cpp = '#include <cassert>\nint ' + function('AIV62OpeningBudget') + r'''
int main() {
    assert(AIV62OpeningBudget(true,1,14,1,0,0,false,100)==200);
    assert(AIV62OpeningBudget(true,1,14,1,0,0,false,99)==0);
    assert(AIV62OpeningBudget(true,1,13,1,0,0,false,100)==0);
    assert(AIV62OpeningBudget(true,1,16,1,1,0,false,200)==150);
    assert(AIV62OpeningBudget(true,1,20,1,1,1,true,200)==150);
    assert(AIV62OpeningBudget(true,1,20,1,1,2,false,200)==150);
    assert(AIV62OpeningBudget(true,1,20,1,1,2,true,200)==0);
    assert(AIV62OpeningBudget(true,2,30,1,2,0,false,200)==0);
    assert(AIV62OpeningBudget(false,1,20,1,1,0,false,200)==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'budget.cpp'
    exe = Path(directory) / 'budget'
    path.write_text(cpp)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra',str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: TvP opening funding/release boundaries; research order and Marine-side boarding integration')
