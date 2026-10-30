// Bounded opening model. The runner inserts actual bot functions below this
// adapter. Resource rates, placement, delays and damage are model assumptions.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>
namespace BWAPI {
namespace UnitTypes {enum T {Terran_SCV,Terran_Marine,Terran_Medic,Terran_Vulture,
 Terran_Goliath,Terran_Siege_Tank_Tank_Mode,Terran_Siege_Tank_Siege_Mode,
 Terran_Command_Center,Terran_Barracks,Terran_Factory,Terran_Machine_Shop,
 Terran_Refinery,Terran_Academy,Terran_Armory,Terran_Supply_Depot,Protoss_Gateway};}
using UnitType=UnitTypes::T;
struct Type {UnitType t;operator UnitType()const{return t;}
 bool isBuilding()const{return t>=UnitTypes::Terran_Command_Center && t<=UnitTypes::Terran_Supply_Depot;}};
namespace Races {enum T {Protoss,Terran,Zerg};} using Race=Races::T;
namespace TechTypes {enum T {Tank_Siege_Mode,Spider_Mines};}
struct Position {};
namespace TilePositions {const Position None{};}
struct U;using Unit=U*;struct P;
int frame=3600;
struct U {
 UnitType type;bool alive=true,completed=true;int finish=-1,queueFinish=-1,researchFinish=-1;
 UnitType queued=UnitTypes::Terran_SCV;Unit addon=nullptr;int id;
 Type getType(){return {type};}bool exists(){return alive;}bool isCompleted(){return completed;}
 bool isVisible(){return true;}bool isFlying(){return false;}
 bool isTraining(){return queueFinish>=0;}bool isResearching(){return researchFinish>=0;}
 int getID(){return id;}Unit getAddon(){return addon;}
 bool train(UnitType);bool buildAddon(UnitType);bool research(TechTypes::T);
};
struct P {
 double mineralCash=0,gasCash=0;int cap=52,supply=0;bool siege=false,researching=false;
 std::vector<std::unique_ptr<U>> storage;std::list<Unit> units;
 int firstTank=-1,secondTank=-1,siegeStart=-1,siegeDone=-1,factoryDone=-1,tankCount=0;
 int minerals(){return (int)mineralCash;}int gas(){return (int)gasCash;}
 int supplyTotal(){return cap;}int supplyUsed(){return supply;}
 Race getRace(){return Races::Terran;}auto& getUnits(){return units;}
 bool hasResearched(TechTypes::T t){return t==TechTypes::Tank_Siege_Mode && siege;}
 bool isResearching(TechTypes::T t){return t==TechTypes::Tank_Siege_Mode && researching;}
 Unit add(UnitType t,bool complete=true) {
  auto u=std::make_unique<U>();u->type=t;u->id=(int)storage.size();u->completed=complete;
  Unit p=u.get();storage.push_back(std::move(u));units.push_back(p);return p;
 }
};
P player;
int mineralCost(UnitType t){using namespace UnitTypes;return t==Terran_Siege_Tank_Tank_Mode?150:t==Terran_Vulture?75:t==Terran_Goliath?100:50;}
int gasCost(UnitType t){using namespace UnitTypes;return t==Terran_Siege_Tank_Tank_Mode?100:t==Terran_Goliath?50:t==Terran_Medic?25:0;}
int supplyCost(UnitType t){using namespace UnitTypes;return t==Terran_Siege_Tank_Tank_Mode||t==Terran_Vulture||t==Terran_Goliath?4:2;}
int trainTime(UnitType t){using namespace UnitTypes;return t==Terran_Siege_Tank_Tank_Mode?750:t==Terran_Vulture?450:t==Terran_SCV?300:360;}
struct G {
 P* self(){return &player;}int getFrameCount(){return frame;}std::vector<P*> enemies(){return {};}
 template<class... T> void printf(const char*,T...){}
 bool canMake(UnitType t,Unit p){return p && p->alive && p->completed && !p->isTraining() &&
  player.minerals()>=mineralCost(t) && player.gas()>=gasCost(t) &&
  player.cap-player.supply>=supplyCost(t) &&
  (t!=UnitTypes::Terran_Siege_Tank_Tank_Mode || (p->addon && p->addon->completed));}
};
G game;G* Broodwar=&game;
bool U::train(UnitType t){if(!game.canMake(t,this))return false;
 player.mineralCash-=mineralCost(t);player.gasCash-=gasCost(t);player.supply+=supplyCost(t);
 queued=t;queueFinish=frame+trainTime(t);return true;}
bool U::buildAddon(UnitType t){if(addon||isTraining()||player.minerals()<50||player.gas()<50)return false;
 player.mineralCash-=50;player.gasCash-=50;addon=player.add(t,false);addon->finish=frame+600;return true;}
bool U::research(TechTypes::T t){if(t!=TechTypes::Tank_Siege_Mode || !completed ||
 isResearching() || player.researching || player.siege || player.minerals()<150 || player.gas()<150)return false;
 player.mineralCash-=150;player.gasCash-=150;player.researching=true;
 player.siegeStart=frame;researchFinish=frame+1200;return true;}
}
int g_v11NoExpansionUntil=0,g_v11Reserve=0,g_v28MilitaryMineralReserve=0,g_v28GasReserve=0;
bool g_v29TerranMech=false;std::map<int,int> g_v11ShopFailures;
BWAPI::Race AIV13OpponentRace(){return BWAPI::Races::Protoss;}
void AIV11Reset(){}
int AIV11WorkerTarget(){return 25;}
int AIV25Seen(BWAPI::UnitType){return 1;}
bool AIV11PendingBuild(BWAPI::UnitType t){for(auto u:BWAPI::player.units)if(u->alive && u->type==t && !u->completed)return true;return false;}
template<class... T> void AIV353Log(T...){}

// @ACTUAL_SOURCE@

int main(int argc,char** argv){
 using namespace BWAPI;using namespace BWAPI::UnitTypes;
 const int runs=argc>1?std::atoi(argv[1]):1000;
 std::mt19937 rng(argc>2?(unsigned)std::strtoul(argv[2],nullptr,10):620002);
 std::cout<<"scenario,initial_workers,initial_marines,mineral_rate,placement_delay,build_interruption,planned_worker_losses,factory_frame,first_tank_frame,second_tank_frame,siege_start_frame,siege_done_frame,ready_frame,workers_lost,final_minerals,final_gas,final_tanks,model_completed\n";
 for(int trial=0;trial<runs;++trial){
  player=P{};frame=3600;g_v11ShopFailures.clear();
  player.mineralCash=50+rng()%151;player.gasCash=100+rng()%151;
  const int initialWorkers=14+rng()%5;
  for(int i=0;i<initialWorkers;++i)player.add(Terran_SCV);
  const int marines=2+rng()%3;
  for(int i=0;i<marines;++i)player.add(Terran_Marine);
  player.supply=2*(initialWorkers+marines);
  player.add(Terran_Command_Center);player.add(Terran_Barracks);player.add(Terran_Refinery);
  const double rate=(18+rng()%19)/1000.0; // assumed minerals/worker/frame
  const int placementDelay=48+rng()%433;
  const int buildInterruption=rng()%3==0?(int)(rng()%601):0;
  const int shockFrame=4800+rng()%3601;
  const int losses=rng()%3==0?1+rng()%4:0;
  int lost=0,ready=-1;
  for(frame=3600;frame<=18000;++frame){
   const auto tickUnits=player.units;
   for(auto u:tickUnits){
    if(!u->alive)continue;
    if(!u->completed && u->finish<=frame){u->completed=true;u->finish=-1;
     if(u->type==Terran_Factory)player.factoryDone=frame;
     if(u->type==Terran_Supply_Depot)player.cap+=16;}
    if(u->queueFinish>=0 && u->queueFinish<=frame){
     const auto t=u->queued;u->queueFinish=-1;player.add(t);
     if(t==Terran_Siege_Tank_Tank_Mode){++player.tankCount;
      if(player.firstTank<0)player.firstTank=frame;
      if(player.tankCount==2)player.secondTank=frame;}}
    if(u->researchFinish>=0 && u->researchFinish<=frame){u->researchFinish=-1;
     player.researching=false;player.siege=true;player.siegeDone=frame;}
   }
   if(frame==shockFrame)for(auto u:player.units)if(lost<losses && u->alive && u->type==Terran_SCV){
    u->alive=false;++lost;player.supply-=2;}
   int workers=0;for(auto u:player.units)if(u->alive && u->type==Terran_SCV)++workers;
   const int gasWorkers=player.gas()>=350 && player.gas()>=player.minerals()+150?1:
                        player.gas()>=220 && player.gas()>=player.minerals()+80?2:3;
   player.mineralCash+=std::max(0,workers-gasWorkers-1)*rate;
   player.gasCash+=gasWorkers*0.022;
   if(frame%12==0){
    // Assumed placement/supply scheduler; not extracted production AI. The
    // spending, worker and unit/research commands below use actual source.
    auto c=AIV11Count();
    if(player.cap-player.supply<=8 && !AIV11PendingBuild(Terran_Supply_Depot) && player.minerals()>=100){
     player.mineralCash-=100;auto depot=player.add(Terran_Supply_Depot,false);depot->finish=frame+600;
    }else if(c.factory==0 && c.scv>=14 && player.minerals()>=200 && player.gas()>=100){
     player.mineralCash-=200;player.gasCash-=100;auto factory=player.add(Terran_Factory,false);
     factory->finish=frame+1200+placementDelay+buildInterruption;
    }
    AIV11Produce(AIV11Count());
    AIV11TrainWorkers();
   }
   if(player.secondTank>=0 && player.siege){ready=frame;break;}
  }
  std::cout<<trial+1<<','<<initialWorkers<<','<<marines<<','<<rate<<','<<placementDelay<<','<<buildInterruption<<','<<losses
   <<','<<player.factoryDone<<','<<player.firstTank<<','<<player.secondTank
   <<','<<player.siegeStart<<','<<player.siegeDone<<','<<ready<<','<<lost
   <<','<<player.minerals()<<','<<player.gas()<<','<<player.tankCount<<','<<(ready>=0?1:0)<<'\n';
 }
}
