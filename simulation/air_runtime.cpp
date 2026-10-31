// Scripted command/economy adapter; not BWAPI physics, terrain or combat.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
namespace BWAPI {
struct Position {int x=-1,y=-1;Position()=default;Position(int a,int b):x(a),y(b){}
 bool isValid()const{return x>=0&&y>=0;}double getDistance(Position p)const{return std::hypot(x-p.x,y-p.y);}};
struct TilePosition:Position {using Position::Position;bool operator==(TilePosition p)const{return x==p.x&&y==p.y;}};
namespace TilePositions {const TilePosition None{-1,-1};}
namespace UnitTypes {enum T{Terran_Command_Center,Terran_SCV};}
namespace Races {const int Protoss=1;}
int frame=0,kind=0,cash=1200,spawn=-1,finish=-1,seedFinish=-1,lifts=0,lands=0,seeds=0,active=0,lost=0,builds=0;
bool available=true,unsafe=false,pending=false;
struct U;using Unit=U*;
struct Cmd {Unit target=nullptr;Unit getTarget(){return target;}};
struct U {
 int id;UnitTypes::T type;bool alive=true,complete=true,flying=false,training=false;TilePosition tile;
 Position pos,dest;Unit depot=nullptr,builder=nullptr;int last=0;Cmd cmd;
 U(int n,UnitTypes::T t,TilePosition p):id(n),type(t),tile(p),pos(p.x*32+64,p.y*32+48),dest(pos){}
 bool exists(){return alive;}int getID(){return id;}auto getType(){return type;}
 bool isCompleted(){return complete;}bool isFlying(){return flying;}bool isTraining(){return training;}
 bool isIdle(){return !training;}auto getTilePosition(){return tile;}auto getPosition(){return pos;}
 bool isGatheringMinerals(){return depot!=nullptr;}bool isCarryingMinerals(){return false;}
 int getDistance(Position p){return pos.getDistance(p);}int getDistance(Unit u){return getDistance(u->pos);}
 Unit getBuildUnit(){return builder;}Cmd getLastCommand(){return cmd;}int getLastCommandFrame(){return last;}
 bool rightClick(Unit u){cmd.target=u;last=frame;u->builder=this;return true;}
 bool lift(){if(kind==7)return false;flying=true;++lifts;return true;}
 bool move(Position p){dest=p;return kind!=8;}
 bool canLand(TilePosition p){return p.isValid() && !(kind==1 && p.x==40) && kind!=8;}
 bool land(TilePosition p){if(!canLand(p))return false;tile=p;pos={p.x*32+64,p.y*32+48};flying=false;++lands;return true;}
 bool train(UnitTypes::T){if(training||cash<50)return false;cash-=50;training=true;seedFinish=frame+300;++seeds;return true;}
};
std::vector<Unit> units;Unit managed=nullptr;
struct Player {auto& getUnits(){return units;}int minerals(){return cash;}TilePosition getStartLocation(){return {5,5};}}player;
struct Game {Player* self(){return &player;}int getFrameCount(){return frame;}
 int mapWidth(){return 96;}int mapHeight(){return 64;}bool canMake(UnitTypes::T,Unit){return true;}}game;
Game* Broodwar=&game;
}
static int g_v66CCId=-1,g_v66Reserve=0;
static bool g_v66Staging=false,g_v66Remote=false;
static bool g_v55NaturalPrepared=false;
static std::map<int,int>g_v39BuilderUntil;
int AIV13OpponentRace(){return BWAPI::Races::Protoss;}
struct Counts {int cc=0,scv=0,factory=1,tank=2;};
Counts AIV11Count(){Counts c;for(auto u:BWAPI::units)if(u->alive){if(u->type==BWAPI::UnitTypes::Terran_Command_Center)++c.cc;else ++c.scv;}return c;}
bool AIV11PendingBuild(BWAPI::UnitTypes::T,int){return BWAPI::pending;}
BWAPI::Unit AIV11BuildWorker(BWAPI::Position={}){for(auto u:BWAPI::units)if(u->alive&&u->type==BWAPI::UnitTypes::Terran_SCV)return u;return nullptr;}
bool AIV11BuildNear(BWAPI::UnitTypes::T,BWAPI::TilePosition){
 using namespace BWAPI;if(pending)return false;++builds;pending=true;spawn=frame+96;return true;
}
BWAPI::Unit AIV31PatchForWorker(BWAPI::Unit u){return u;}
BWAPI::Unit AIV31DepotForPatch(BWAPI::Unit u){return u?u->depot:nullptr;}
void AIV353Log(const char* stage,BWAPI::UnitTypes::T,BWAPI::Unit,BWAPI::TilePosition,std::string=""){
 using namespace BWAPI;if(std::string(stage)=="AIR_ACTIVE")++active;if(std::string(stage)=="AIR_CC_LOST")++lost;
}
// @STATE@
bool AIV66SiteSafe(BWAPI::Position){return !BWAPI::unsafe;}
BWAPI::TilePosition AIV66AirSite(){
 using namespace BWAPI;if(!available)return TilePositions::None;
 for(auto tile:std::vector<TilePosition>{{40,25},{60,15}}){
  auto it=g_v66RejectedBases.find(tile.y*96+tile.x);
  if(it==g_v66RejectedBases.end()||it->second<=frame)return tile;
 }return TilePositions::None;
}
BWAPI::TilePosition AIV66MainPad(BWAPI::Unit){return {20,6};}
BWAPI::Position AIV66AirWaypoint(BWAPI::Unit,BWAPI::Position goal){return goal;}
// @DIRECTOR@
int main(int,char** argv){
 using namespace BWAPI;const int runs=std::atoi(argv[1]);int passed=0,activated=0,blocked=0,losses=0;
 for(int i=0;i<runs;++i){
  for(auto u:units)delete u;units.clear();kind=i%10;cash=kind==6?0:1200;spawn=finish=seedFinish=-1;
  lifts=lands=seeds=active=lost=builds=0;available=true;unsafe=false;pending=false;managed=nullptr;
  units.push_back(new U(1,UnitTypes::Terran_Command_Center,{5,5}));
  for(int j=0;j<20;++j)units.push_back(new U(10+j,UnitTypes::Terran_SCV,{5,5}));
  g_v66Previous=1; // force per-match reset through the actual director
  bool lossInjected=false,threatInjected=false,workersRemoved=false;
  for(frame=0;frame<=20000;++frame){
   if(kind==6&&frame%24==0)++cash;
   if(spawn==frame){
    if(kind==9&&builds==1){pending=false;spawn=-1;}
    else {cash-=400;managed=new U(100+builds,UnitTypes::Terran_Command_Center,{20,6});managed->complete=false;
     managed->builder=AIV11BuildWorker();units.push_back(managed);finish=frame+1800;}
   }
   if(finish==frame&&managed){managed->complete=true;pending=false;}
   if(managed&&managed->alive&&managed->flying&&kind!=8){
    auto p=managed->pos;double d=p.getDistance(managed->dest);if(d>1){double step=1.2+(i%11)*0.04;
     managed->pos={int(p.x+(managed->dest.x-p.x)*std::min(1.0,step/d)),int(p.y+(managed->dest.y-p.y)*std::min(1.0,step/d))};
     // Ensure subpixel truncation cannot freeze this integer-coordinate adapter.
     if(managed->pos.x==p.x&&managed->pos.y==p.y)managed->pos=managed->dest;
    }
   }
   if(seedFinish==frame&&managed&&managed->alive){auto w=new U(200+seeds,UnitTypes::Terran_SCV,managed->tile);w->depot=managed;units.push_back(w);managed->training=false;}
   if(kind==2&&managed&&managed->flying&&!threatInjected){available=false;unsafe=true;threatInjected=true;}
   if(kind==2&&frame>7000){available=true;unsafe=false;}
   if(kind==3&&managed&&managed->flying&&!lossInjected){managed->alive=false;available=false;lossInjected=true;}
   if(kind==4&&managed&&managed->flying){available=false;unsafe=true;}
   if(kind==5&&managed&&managed->complete&&!workersRemoved){for(auto u:units)if(u->type==UnitTypes::Terran_SCV)u->alive=false;workersRemoved=true;}
   AIV66ExpansionDirector();
   if(active||lost)break;
  }
  bool expectActive=kind==0||kind==1||kind==2||kind==5||kind==6||kind==9;
  bool ok=expectActive?(active==1&&seeds>=4&&lands>0):kind==3?(lost==1&&active==0):(active==0&&units[0]->alive&&!units[0]->flying);
  if(!ok){std::cerr<<"director scenario "<<i<<" kind="<<kind<<" active="<<active<<" seeds="<<seeds<<" lost="<<lost<<"\n";return 1;}
  ++passed;if(active)++activated;else if(lost)++losses;else ++blocked;
 }
 std::cout<<"{\"passed\":"<<passed<<",\"activated\":"<<activated<<",\"lost\":"<<losses<<",\"blocked\":"<<blocked<<"}";
}
