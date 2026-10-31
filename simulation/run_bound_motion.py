"""Execute the actual current bound-state handler with delayed/blocked motion.

This adapter models command acceptance, a delayed Siege transition and blockers;
it is not StarCraft physics, pathfinding, combat or the full formation controller.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=ROOT/'source/StarterBot.cpp')
parser.add_argument('--runs',type=int,default=1000)
parser.add_argument('--seed',type=int,default=650001)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
if args.runs<1:parser.error('--runs must be positive')
raw=args.source.read_bytes();source=raw.decode()
match=re.search(r'\bAIV65HandleBound\([^;]*?\)\s*\{',source,re.S)
current=match is not None
if current:
    end=source.index('{',match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    body=source[match.start():end]
else:
    controller=source[source.index('static void AIV56SiegeLineController()'):]
    start=controller.index('    BWAPI::Unit moving=nullptr;')
    end=controller.index('    // Every non-moving Tank is cover.',start)
    # Preserve the old inline command logic; translate its outer-controller
    # return into this isolated adapter's return value only.
    body='AIV65HandleBound(const std::vector<BWAPI::Unit>& tanks, BWAPI::Position home, bool natural) {'
    body+='const int now=BWAPI::Broodwar->getFrameCount();const bool preparingNatural=natural;'
    body+=controller[start:end].replace('return;','return true;')+'return false;}'
cpp=r'''
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>
namespace BWAPI {
struct Position {int x,y;bool isValid()const{return x>=0 && y>=0;}};
namespace Positions {const Position None{-1,-1};}
namespace TilePositions {const Position None{-1,-1};}
namespace UnitTypes {enum T {Terran_Command_Center,Terran_Siege_Tank_Tank_Mode,Terran_Siege_Tank_Siege_Mode};}
int frame=0;bool danger=false;
struct U {
 int id=0;bool alive=true,rejectSiege=false,rejectMove=false;int transition=-1;bool pendingSiege=false;
 UnitTypes::T type=UnitTypes::Terran_Siege_Tank_Tank_Mode;
 double x=96,y=160;Position destination{96,160};int command=0;int moves=0,sieges=0,holds=0;
 int getID(){return id;}auto getType(){return type;}
 Position getPosition(){return {(int)std::round(x),(int)std::round(y)};}
 int getDistance(Position p){return (int)std::hypot(x-p.x,y-p.y);}
 int getDistance(U* u){return getDistance(u->getPosition());}
 bool move(Position p){if(!alive || rejectMove || transition>=0)return false;destination=p;command=1;++moves;return true;}
 bool siege(){if(!alive || rejectSiege || transition>=0 || type==UnitTypes::Terran_Siege_Tank_Siege_Mode)return false;
  command=0;pendingSiege=true;transition=frame+37;++sieges;return true;}
 bool unsiege(){if(!alive || transition>=0 || type!=UnitTypes::Terran_Siege_Tank_Siege_Mode)return false;
  command=0;pendingSiege=false;transition=frame+37;return true;}
 bool holdPosition(){if(!alive || transition>=0)return false;command=2;++holds;return true;}
};using Unit=U*;
struct G {int getFrameCount(){return frame;}};
G game;G* Broodwar=&game;
}
int g_v56MovingTank=-1,g_v56LastBound=-10000;
BWAPI::Position g_v56MoveTarget=BWAPI::Positions::None;
std::map<int,int> g_v56LastOrder;
int g_v65WatchedTank=-1,g_v65MoveStarted=0,g_v65ProgressFrame=0,g_v65BestDistance=0,g_v65RecoveryStarted=0;
bool g_v65Settling=false;std::map<int,int>g_v65RejectedCells;
int g_v64Width=32;int aborts=0,settleFailures=0;
bool AIV56GroundContact(BWAPI::Position,int){return BWAPI::danger;}
void AIV353Log(const char* stage,BWAPI::UnitTypes::T,BWAPI::Unit,BWAPI::Position,std::string){
 if(std::string(stage)=="NATURAL_MOVE_ABORT")++aborts;
 if(std::string(stage)=="NATURAL_SETTLE_FAILED")++settleFailures;
}
'''
cpp+='\nbool '+body+'\n'
cpp+='\nconstexpr bool CURRENT_HANDLER='+('true' if current else 'false')+';\n'
cpp+=r'''
int main(int,char** argv){
 using namespace BWAPI;const int runs=std::atoi(argv[1]);std::mt19937 rng(std::strtoul(argv[2],nullptr,10));
 std::cout<<"scenario,kind,released,release_frame,aborts,settle_failures,moves,sieges,final_mode,unsafe\n";
 for(int trial=0;trial<runs;++trial){
  const int kind=trial%8;U moving,cover;moving.id=trial*2+1;cover.id=trial*2+2;
  moving.x=80+rng()%33;moving.rejectSiege=kind==4;
  moving.rejectMove=kind==6;
  cover.x=160;cover.y=240;cover.type=UnitTypes::Terran_Siege_Tank_Siege_Mode;
  std::vector<Unit> tanks{&moving};if(kind!=6)tanks.push_back(&cover);
  g_v56MovingTank=moving.id;g_v56MoveTarget={320,160};g_v56LastOrder.clear();g_v56LastOrder[moving.id]=0;
  g_v65WatchedTank=-1;g_v65Settling=false;g_v65RejectedCells.clear();aborts=settleFailures=0;
  int released=-1,unsafe=0;double speed=kind==3?0.2:2.0+(rng()%201)/100.0;
  for(frame=0;frame<=900;++frame){
   danger=kind==2 && frame>=60;
   if(kind==5 && frame==80){moving.alive=false;tanks.clear();tanks.push_back(&cover);}
   if(moving.transition>=0 && frame>=moving.transition){
    moving.type=moving.pendingSiege?UnitTypes::Terran_Siege_Tank_Siege_Mode:UnitTypes::Terran_Siege_Tank_Tank_Mode;
    moving.transition=-1;
   }
   if(moving.alive && moving.command==1 && moving.transition<0){
    double dx=moving.destination.x-moving.x,dy=moving.destination.y-moving.y;
    const double length=std::hypot(dx,dy),step=std::min(speed,length);
    const double nx=moving.x+(length?dx/length*step:0),ny=moving.y+(length?dy/length*step:0);
    const bool blocked=(kind==1 || kind==4 || kind==6 || (kind==7 && frame<120)) &&
                       std::hypot(nx-320,ny-160)<40;
    const bool collision=kind!=6 && std::hypot(nx-cover.x,ny-cover.y)<40;
    if(!blocked && !collision){moving.x=nx;moving.y=ny;}
   }
   if(frame%12==0){
    AIV65HandleBound(tanks,{80,80},true);
    if(g_v56MovingTank<0){released=frame;break;}
   }
  }
  if(released<0)++unsafe;
  if(released>660)++unsafe;
  if(kind==0 || kind==7){if(moving.type!=UnitTypes::Terran_Siege_Tank_Siege_Mode)++unsafe;}
  if(CURRENT_HANDLER && kind==4 && (settleFailures!=1 || moving.command!=2 ||
     moving.type!=UnitTypes::Terran_Siege_Tank_Tank_Mode))++unsafe;
  if(kind==5 && released>96)++unsafe;
  if(CURRENT_HANDLER && (kind==1 || kind==3 || kind==4 || kind==6) && aborts!=1)++unsafe;
  if(aborts && g_v65RejectedCells.empty())++unsafe;
  // Accepted advance ownership is never released as fictitious Siege cover.
  // A mobile recovery ends in a hold/retreat, and stays non-cover by type.
  if(moving.alive && moving.type!=UnitTypes::Terran_Siege_Tank_Siege_Mode &&
     moving.command!=2 && !(moving.command==1 && moving.destination.x<=160))++unsafe;
  std::cout<<trial+1<<','<<kind<<','<<(released>=0)<<','<<released<<','<<aborts<<','<<settleFailures
   <<','<<moving.moves<<','<<moving.sieges<<','<<moving.type<<','<<unsafe<<'\n';
 }
}
'''
args.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'motion.cpp';exe=Path(directory)/'motion'
    path.write_text(cpp)
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra',str(path),'-o',str(exe)],check=True)
    with (args.output/'scenarios.csv').open('w')as out:
        subprocess.run([str(exe),str(args.runs),str(args.seed)],stdout=out,check=True)
rows=list(csv.DictReader((args.output/'scenarios.csv').open()));assert len(rows)==args.runs
summary={
    'source_sha256':hashlib.sha256(raw).hexdigest(),'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'runs':len(rows),'seed':args.seed,'ownership_released':sum(r['released']=='1'for r in rows),
    'handler':'current_extracted_function' if current else 'historical_extracted_inline_block',
    'unsafe_or_unbounded':sum(int(r['unsafe'])>0 for r in rows),
    'max_release_frame':max(int(r['release_frame'])for r in rows),
    'expected_settle_failures_handled':sum(int(r['settle_failures'])for r in rows),
    'scope':'actual bound handler with approximate motion/blockers and delayed Siege; not full engine/pathfinder/formation',
    'kinds':['normal','permanent_landing_block','ground_contact','slow_progress_deadline',
             'Siege_command_failure','unit_lost','no_cover_retreat_rejected','temporary_block'],
}
(args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
if summary['unsafe_or_unbounded']:raise SystemExit(1)
