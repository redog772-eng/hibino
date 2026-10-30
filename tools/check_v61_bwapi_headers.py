"""Syntax-check changed controllers against real BWAPI 4.4.0 headers.

Pass --include pointing to the official bwapi/include directory. This compiles
actual controller bodies, with declarations for the surrounding bot, without
linking BWAPI or claiming a Windows executable build.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from check_v61_ownership import function, source

parser = argparse.ArgumentParser()
parser.add_argument('--include', required=True, type=Path)
args = parser.parse_args()
assert (args.include / 'BWAPI.h').is_file()
prefix = r'''
#include <BWAPI.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
int _mkdir(const char*);
BWAPI::Race AIV13OpponentRace();
BWAPI::Position AIV33AssemblyPoint();
BWAPI::Position AIV6AttackTarget();
BWAPI::Position AIV10AttackTarget();
BWAPI::Position AIV25PathRally();
BWAPI::Unit AIV11BuildWorker();
BWAPI::TilePosition AIV16NaturalSite(BWAPI::Unit);
BWAPI::Position AIV55NaturalPost(BWAPI::Position,BWAPI::Position,BWAPI::Unit,int,int);
bool AIV7IsTank(BWAPI::Unit);
bool AIV23VisibleThreat(BWAPI::Unit);
void AIV353Log(const char*,BWAPI::UnitType,BWAPI::Unit,BWAPI::TilePosition,std::string = "");
extern std::set<int> g_v32GuardIds,g_v55NaturalGuardIds,g_v56SiegeLineIds,g_v7Army;
extern std::map<int,int> g_v55LastMineOrder;
extern int g_v55PreviousFrame,g_v57NoLoadUntil,g_v7State;
extern bool g_v55NaturalPrepared;
const int AIV7_ATTACK=2;
'''
cpp = prefix + source[source.index('static int g_v56PreviousFrame='):source.index('static bool AIV59CanAdvance(')]
for result, name in [('bool', 'AIV61OwnsMobile'), ('bool', 'AIV59CanAdvance'),
                     ('bool', 'AIV61CanStageNatural'), ('BWAPI::Position', 'AIV59HighGround'),
                     ('int', 'AIV56Progress'), ('BWAPI::Position', 'AIV56PointOnLine'),
                     ('bool', 'AIV56GroundContact'), ('void', 'AIV55PrepareNatural'),
                     ('void', 'AIV56SiegeLineController'), ('void', 'AIV58HoldScreen')]:
    cpp += '\n' + result + ' ' + function(name) + '\n'
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'controllers.cpp'
    path.write_text(cpp)
    subprocess.run(['g++', '-std=c++20', '-fsyntax-only', '-isystem', str(args.include), str(path)], check=True)
print('PASS: actual changed natural/Tank/mobile controllers compile against official BWAPI headers (syntax only)')
