"""Compile the actual hold-controller body against a minimal command-recording stub.
Not a BWAPI integration test or a StarCraft simulation.
"""
from pathlib import Path
import subprocess
import tempfile
s=Path('source/StarterBot.cpp').read_text()
body=s[s.index('static void AIV58HoldScreen()'):s.index('void StarterBot::onFrame()')]
stub=r'''
#include <vector>
#include <set>
#include <cmath>
#include <cassert>
#include <random>
#include <algorithm>
namespace BWAPI {
struct Position {int x=0,y=0;bool isValid()const{return x>=0&&y>=0;} bool operator!=(Position b)const{return x!=b.x||y!=b.y;}};
namespace UnitTypes {enum T {Terran_Marine,Terran_Vulture,Terran_Goliath,Terran_Medic,Terran_Command_Center,Terran_Siege_Tank_Siege_Mode,Terran_Bunker,Protoss_Observer,Protoss_High_Templar};}
namespace UnitCommandTypes {enum T {Attack_Unit,Move,Hold_Position,Use_Tech_Unit,Right_Click_Unit};}
namespace TechTypes {enum T {Healing};}
struct Type {UnitTypes::T value;operator UnitTypes::T()const{return value;}bool isOrganic()const{return value==UnitTypes::Terran_Marine || value==UnitTypes::Terran_Medic;}bool isBuilding()const{return value==UnitTypes::Terran_Command_Center || value==UnitTypes::Terran_Bunker;}int maxHitPoints()const{return 100;}};
namespace Races {enum T {Protoss,Terran};}
struct U; struct P;
int frame=1000;
struct Command {UnitCommandTypes::T type=UnitCommandTypes::Attack_Unit;Position pos;U* target=nullptr;U* getTarget()const{return target;}auto getTechType()const{return TechTypes::Healing;}auto getType()const{return type;}auto getTargetPosition()const{return pos;}};
struct U {int id=0;UnitTypes::T type=UnitTypes::Terran_Marine;Position pos;bool loaded=false,fly=false,visible=true,detected=true,invincible=false;Command cmd;int orders=0,lastFrame=-10000;P* player=nullptr;
 int getLastCommandFrame(){return lastFrame;}bool rightClick(U* target){cmd.type=UnitCommandTypes::Right_Click_Unit;cmd.target=target;lastFrame=frame;++orders;return true;}
 bool isVisible(){return visible;}bool isDetected(){return detected;}bool isInvincible(){return invincible;}bool isInWeaponRange(U* e){return getDistance(e)<=128 && !(type==UnitTypes::Terran_Vulture && e->fly);}
 std::vector<U*> passengers;auto& getLoadedUnits(){return passengers;}bool load(U* u){cmd.target=u;lastFrame=frame;++orders;return true;}
 bool attack(U* e){cmd.type=UnitCommandTypes::Attack_Unit;cmd.target=e;++orders;return true;}
 bool attacking=false;int hp=100;bool isAttackFrame(){return attacking;}int getHitPoints(){return hp;}bool useTech(TechTypes::T,U* patient){cmd.type=UnitCommandTypes::Use_Tech_Unit;cmd.target=patient;++orders;return true;}
 P* getPlayer(){return player;}bool exists(){return true;}bool isCompleted(){return true;}bool isLoaded(){return loaded;}bool isFlying(){return fly;}Type getType(){return {type};}int getID(){return id;}
 int getDistance(Position p){return int(std::hypot(pos.x-p.x,pos.y-p.y));}int getDistance(U* u){return getDistance(u->pos);}Position getPosition(){return pos;}
 bool hasPath(Position){return true;}auto getLastCommand(){return cmd;}
 bool move(Position p){cmd={UnitCommandTypes::Move,p};lastFrame=frame;++orders;return true;}bool holdPosition(){cmd.type=UnitCommandTypes::Hold_Position;cmd.target=nullptr;lastFrame=frame;++orders;return true;}
}; using Unit=U*;
struct P {std::vector<Unit> units;auto& getUnits(){return units;}Position getStartLocation(){return {1000,1000};}};
struct G {int getFrameCount(){return frame;}P mine,foe;P* self(){return &mine;}std::vector<P*> enemies(){return {&foe};}};
G state;G* Broodwar=&state;
}
int g_v57NoLoadUntil=-1;
std::set<int> g_v55NaturalGuardIds;
BWAPI::Races::T race=BWAPI::Races::Protoss;
auto AIV13OpponentRace(){return race;}
bool AIV23VisibleThreat(BWAPI::Unit e){return e->visible;}
BWAPI::Position AIV33AssemblyPoint(){return {1000,1000};}
BWAPI::Position AIV6AttackTarget(){return {4000,1000};}
'''
progress=s[s.index('static int AIV56Progress('):s.index('static BWAPI::Position AIV56PointOnLine(')]
checks=r'''
int main(){using namespace BWAPI;std::mt19937 rng(580001);
for(int i=0;i<1000;++i){
 U marine,cc,tank,enemy;marine.id=1;cc.id=2;tank.id=3;enemy.id=4;
 cc.type=UnitTypes::Terran_Command_Center;cc.pos={1000,1000};
 tank.type=UnitTypes::Terran_Siege_Tank_Siege_Mode;tank.pos={1800,1000};
 marine.pos={int(1000+rng()%1300),1000};enemy.pos={int(4000+rng()%500),1000};
 state.mine.units={&marine,&cc,&tank};state.foe.units={&enemy};
 AIV58HoldScreen();assert(marine.cmd.type!=UnitCommandTypes::Attack_Unit);
 if(marine.cmd.type==UnitCommandTypes::Move)assert(!(marine.cmd.pos!=tank.pos));
 marine.pos={1800,1000};marine.cmd.type=UnitCommandTypes::Attack_Unit;
 AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Hold_Position);
 enemy.pos={1100,1000};marine.cmd.type=UnitCommandTypes::Attack_Unit;
 AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Move);assert(!(marine.cmd.pos!=cc.pos));
 marine.loaded=true;int count=marine.orders;AIV58HoldScreen();assert(marine.orders==count);
 marine.loaded=false;marine.type=UnitTypes::Terran_Vulture;g_v55NaturalGuardIds.insert(1);
 AIV58HoldScreen();assert(marine.orders==count);g_v55NaturalGuardIds.clear();
}
U medic,patient,tank;
medic.type=UnitTypes::Terran_Medic;medic.pos={1800,1000};
patient.pos={1830,1000};patient.hp=35;
tank.type=UnitTypes::Terran_Siege_Tank_Siege_Mode;tank.pos={1800,1000};
state.mine.units={&medic,&patient,&tank};state.foe.units.clear();
AIV58HoldScreen();assert(medic.cmd.type==UnitCommandTypes::Use_Tech_Unit);assert(medic.cmd.target==&patient);
int count=medic.orders;AIV58HoldScreen();assert(medic.orders==count);
patient.pos={2300,1000};AIV58HoldScreen();assert(medic.cmd.type==UnitCommandTypes::Hold_Position);
patient.pos={1800,1000};patient.attacking=true;patient.cmd.type=UnitCommandTypes::Attack_Unit;
count=patient.orders;AIV58HoldScreen();assert(patient.orders==count);assert(patient.cmd.type==UnitCommandTypes::Attack_Unit);
// In-range fire is preserved, while a retreating or undetected target cannot
// pull a mobile unit away from the Tank line.
U marine,enemy,bunker,rearTank;
marine.pos={1800,1000};enemy.pos={1880,1000};bunker.type=UnitTypes::Terran_Bunker;bunker.pos={1800,1000};
state.mine.units={&marine,&tank,&bunker};state.foe.units={&enemy};
AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Attack_Unit);assert(marine.cmd.target==&enemy);
count=marine.orders;AIV58HoldScreen();assert(marine.orders==count);
enemy.pos={2100,1000};AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Hold_Position);
enemy.pos={1880,1000};enemy.detected=false;AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Hold_Position);
enemy.detected=true;marine.type=UnitTypes::Terran_Vulture;enemy.fly=true;
AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Hold_Position);
marine.type=UnitTypes::Terran_Marine;enemy.fly=false;enemy.type=UnitTypes::Protoss_High_Templar;
AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Attack_Unit);
enemy.type=UnitTypes::Protoss_Observer;AIV58HoldScreen();assert(marine.cmd.type==UnitCommandTypes::Hold_Position);
// A nearby bunker must not strand the screen behind the forward Tank.
state.foe.units.clear();marine.type=UnitTypes::Terran_Marine;
rearTank.type=UnitTypes::Terran_Siege_Tank_Siege_Mode;rearTank.pos={1800,1000};tank.pos={2200,1000};
state.mine.units={&marine,&rearTank,&tank,&bunker};AIV58HoldScreen();
assert(marine.cmd.type==UnitCommandTypes::Move);assert(!(marine.cmd.pos!=tank.pos));
// Before Tanks are available, this owner also supplies bunker boarding.
state.mine.units={&marine,&bunker};marine.cmd.target=nullptr;AIV58HoldScreen();assert(marine.cmd.target==&bunker);
assert(marine.cmd.type==UnitCommandTypes::Right_Click_Unit);assert(bunker.orders==0);
// Real BWAPI load() updates the Bunker's command, not the Marine's. Model
// that distinction and reserve four incoming seats without a per-frame loop.
bunker.player=&state.mine;marine.player=&state.mine;
count=marine.orders;for(int i=0;i<100;++i){++frame;AIV58HoldScreen();}
assert(marine.orders==count);assert(bunker.orders==0);
frame+=192;AIV58HoldScreen();assert(marine.orders==count+1);
U recruits[5];state.mine.units={&bunker};
for(auto& recruit:recruits){recruit.pos={1800,1000};recruit.player=&state.mine;state.mine.units.push_back(&recruit);}
AIV58HoldScreen();int boarding=0;
for(auto& recruit:recruits)if(recruit.cmd.type==UnitCommandTypes::Right_Click_Unit)++boarding;
assert(boarding==4);assert(bunker.orders==0);
g_v57NoLoadUntil=frame+96;AIV58HoldScreen();
for(auto& recruit:recruits)assert(recruit.cmd.type!=UnitCommandTypes::Right_Click_Unit);
g_v57NoLoadUntil=-1;
state.mine.units={&marine,&tank};race=Races::Terran;count=marine.orders;AIV58HoldScreen();assert(marine.orders==count);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'hold.cpp';exe=Path(d)/'hold';p.write_text(stub+progress+body+checks)
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: actual hold-controller body, 1000 randomized command-stub cases; not engine matches')
