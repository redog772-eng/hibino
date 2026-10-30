"""Regression checks for command ownership and the natural staging gate.

Checks extracted C++ predicates and guards on the actual legacy order paths.
Not an engine match or a full BWAPI build.
"""
from pathlib import Path
import re
import subprocess
import tempfile

source = Path('source/StarterBot.cpp').read_text()


def function(name):
    start = source.index(name + '(')
    # Some helpers have forward declarations; require the actual definition.
    match = re.search(r'\b' + re.escape(name) + r'\([^;]*?\)\s*\{', source[start:], re.S)
    assert match, name
    start += match.start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


paths = ['AIV8UnblockTraffic', 'AIV8ArmyController', 'AIV10ArmyController',
         'AIV17TryMoveAside', 'AIV32DefenseAndWorkers', 'AIV26EarlyDefenseAndRecon',
         'AIV23ReactiveDefense', 'AIV34UnitFlow', 'AIV56SiegeLineController']
for name in paths:
    assert 'AIV61OwnsMobile(' in function(name), name
assert 'AIV13OpponentRace()==BWAPI::Races::Protoss' in function('AIV20MedicExitClearance')
assert 'AIV13OpponentRace()==BWAPI::Races::Protoss' in function('AIV37HighApmMicro')
assert 'AIV13OpponentRace()==BWAPI::Races::Protoss' in function('AIV373EnemyExpansionResponse')
natural = function('AIV55PrepareNatural')
assert 'tank->getDistance(natural)<=400' in natural
assert 'tank->move(' not in natural and 'tank->siege(' not in natural
assert 'NATURAL_WAIT' in natural and 'NATURAL_READY' in natural
siege = function('AIV56SiegeLineController')
assert 'AIV61CanStageNatural(' in siege and 'preparingNatural?2u:3u' in siege
assert 'neededBarracks=tvp?1:' in function('AIV16Expand')
economy = function('AIV13EconomyDirector')
assert 'c.barracks>=(versusProtoss?1:3)' in economy
assert 'c.barracks>=(versusProtoss?1:4)' in economy

cpp = r'''
#include <cassert>
#include <random>
'''
cpp += 'bool ' + function('AIV61CanStageNatural')
cpp += '\nbool ' + function('AIV59CanAdvance')
cpp += r'''
int main() {
    // The historical three-Tank gate blocked an otherwise covered natural.
    assert(!AIV59CanAdvance(2,1,2,false,151,true));
    assert(AIV61CanStageNatural(2,1,2,false,151,true));
    assert(!AIV61CanStageNatural(1,1,2,false,151,true));
    assert(!AIV61CanStageNatural(2,0,2,false,151,true));
    assert(!AIV61CanStageNatural(2,1,1,false,151,true));
    assert(!AIV61CanStageNatural(2,1,2,true,151,true));
    assert(!AIV61CanStageNatural(2,1,2,false,161,true));
    assert(!AIV61CanStageNatural(2,1,2,false,151,false));
    std::mt19937 rng(610001);
    for(int i=0;i<1000;++i) {
        int tanks=rng()%7,cover=rng()%4,screen=rng()%7,gap=rng()%400;
        bool contact=rng()%2,path=rng()%2;
        if(AIV61CanStageNatural(tanks,cover,screen,contact,gap,path)) {
            assert(tanks>=2 && cover>=1 && screen>=2);
            assert(!contact && path && gap<=160);
        }
        if(AIV59CanAdvance(tanks,cover,screen,contact,gap,path)) {
            assert(tanks>=3 && cover>=2 && screen>=3);
            assert(!contact && path && gap<=320);
        }
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'staging.cpp'
    exe = Path(directory) / 'staging'
    path.write_text(cpp)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: legacy mobile-order guards; covered two-Tank staging and offensive safety gates')
