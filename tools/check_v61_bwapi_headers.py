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
prefix+=source[source.index('static int g_v66CCId='):source.index('// TvP mobile combat has one command owner.')]
prefix+=source[source.index('enum AIV66Action'):source.index('static bool AIV66SiteSafe(')]
prefix+=r'''
extern std::map<int,BWAPI::Position> g_v10EnemyBuildings;
extern std::map<int,int> g_v39BuilderUntil;
BWAPI::Unit AIV11BuildWorker(BWAPI::Position);
BWAPI::TilePosition AIV12BuildingTile(BWAPI::UnitType,BWAPI::Unit);
bool AIV35ForbiddenBuildPad(BWAPI::Position,BWAPI::TilePosition,BWAPI::Player,int);
bool AIV11BuildNear(BWAPI::UnitType,BWAPI::TilePosition);
BWAPI::Unit AIV31DepotForPatch(BWAPI::Unit);
BWAPI::Unit AIV31PatchForWorker(BWAPI::Unit);
'''
prefix += source[source.index('struct AIV11Counts {'):source.index('static int AIV62OpeningBudget(')]
prefix += r'''
AIV11Counts AIV11Count();
void AIV11Reset();
int AIV11WorkerTarget();
bool AIV11PendingBuild(BWAPI::UnitType,int=420);
void AIV3ResearchSiegeMode();
int AIV25Seen(BWAPI::UnitType);
extern int g_v11NoExpansionUntil,g_v5ScoutId,g_v11Reserve,g_v28MilitaryMineralReserve,g_v28GasReserve;
extern std::map<int,int> g_v11ShopFailures;
extern bool g_v29TerranMech;
'''
cpp = prefix + source[source.index('static int g_v56PreviousFrame='):source.index('static bool AIV59CanAdvance(')]
for result, name in [('int', 'AIV62OpeningBudget'), ('void', 'AIV11TrainWorkers'),
                     ('int', 'AIV63SiegeGasReserve'), ('void', 'AIV11Produce'),
                     ('bool', 'AIV61OwnsMobile'), ('bool', 'AIV59CanAdvance'),
                     ('bool', 'AIV61CanStageNatural'), ('BWAPI::Position', 'AIV59HighGround'),
                     ('int', 'AIV56Progress'), ('BWAPI::Position', 'AIV56PointOnLine'),
                     ('bool', 'AIV56GroundContact'), ('void', 'AIV55PrepareNatural'),
                     ('bool', 'AIV65HandleBound'),
                     ('bool','AIV66SiteSafe'),('BWAPI::TilePosition','AIV66AirSite'),
                     ('bool','AIV66PadClear'),('BWAPI::TilePosition','AIV66MainPad'),
                     ('BWAPI::Position','AIV66AirWaypoint'),('void','AIV66ExpansionDirector'),
                     ('void', 'AIV56SiegeLineController'), ('void', 'AIV58HoldScreen')]:
    cpp += '\n' + result + ' ' + function(name) + '\n'
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'controllers.cpp'
    path.write_text(cpp)
    subprocess.run(['g++', '-std=c++20', '-fsyntax-only', '-isystem', str(args.include), str(path)], check=True)
print('PASS: actual changed natural/Tank/mobile controllers compile against official BWAPI headers (syntax only)')
