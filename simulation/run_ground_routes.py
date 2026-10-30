"""Exercise current natural route/bound functions on two-Tank grid scenarios.

This is a movement-policy model, not engine movement, combat or map playback.
Cells are assumed to have already been cleared for a Tank footprint.
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
parser.add_argument('--seed',type=int,default=640001)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
if args.runs<1:parser.error('--runs must be positive')
raw=args.source.read_bytes();source=raw.decode()


def function(name):
    m=re.search(r'\b'+re.escape(name)+r'\([^;]*?\)\s*\{',source,re.S)
    if not m:raise RuntimeError('missing current route function: '+name)
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]


cpp=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
'''
for result,name in [('std::vector<int>','AIV64GroundDistances'),('bool','AIV64ClearSegment'),
                    ('int','AIV64BoundCell'),('bool','AIV61CanStageNatural')]:
    cpp+='\n'+result+' '+function(name)+'\n'
cpp+=r'''
int main(int,char** argv){
 const int runs=std::atoi(argv[1]);std::mt19937 rng(std::strtoul(argv[2],nullptr,10));
 std::cout<<"scenario,reachable,path_cells,bounds,arrived,unsafe\n";
 for(int scenario=0;scenario<runs;++scenario){
  const int w=32,h=24,start=2*w+2,goal=(h-3)*w+w-3;
  std::vector<unsigned char> open(w*h,1);
  for(int x=0;x<w;++x)open[x]=open[(h-1)*w+x]=0;
  for(int y=0;y<h;++y)open[y*w]=open[y*w+w-1]=0;
  // Alternating barrier gates force bends and sometimes backwards travel.
  for(int x=6;x<29;x+=6){const int gap=2+rng()%(h-5);
   for(int y=1;y<h-1;++y)if(y!=gap && y!=gap+1)open[y*w+x]=0;}
  if(scenario%10==0)for(int y=0;y<h;++y)open[y*w+16]=0;
  const auto distance=AIV64GroundDistances(open,w,h,goal);
  int cell[2]={start,start};int bounds=0,unsafe=0;
  const bool reachable=distance[start]>=0;
  for(;reachable && bounds<2048 && (distance[cell[0]]>3 || distance[cell[1]]>3);++bounds){
   const int front=distance[cell[0]]<=distance[cell[1]]?0:1,rear=1-front;
   const int rx=cell[rear]%w*32+16,ry=cell[rear]/w*32+16;
   const int fx=cell[front]%w*32+16,fy=cell[front]/w*32+16;
   const int target=AIV64BoundCell(open,distance,w,h,rx,ry,fx,fy);
   if(target<0)break;
   const int x=target%w*32+16,y=target/w*32+16;
   const int gap=(int)std::ceil(std::hypot(x-fx,y-fy));
   // Validate segment occupancy independently at one-pixel intervals.
   const int samples=std::max(std::abs(x-rx),std::abs(y-ry))+1;
   for(int k=0;k<=samples;++k){
    const int px=rx+(x-rx)*k/samples,py=ry+(y-ry)*k/samples;
    for(int ox:{-16,15})for(int oy:{-16,15})
     if(!open[((py+oy)/32)*w+(px+ox)/32])++unsafe;
   }
   if(distance[target]>=distance[cell[rear]] || gap>160 || std::hypot(x-rx,y-ry)>288)++unsafe;
   if(!AIV61CanStageNatural(2,1,2,false,gap,true))++unsafe;
   cell[rear]=target;
  }
  if(!reachable && AIV64BoundCell(open,distance,w,h,80,80,80,80)>=0)++unsafe;
  const bool arrived=reachable && distance[cell[0]]<=3 && distance[cell[1]]<=3;
  std::cout<<scenario+1<<','<<reachable<<','<<distance[start]<<','<<bounds<<','<<arrived<<','<<unsafe<<'\n';
 }
}
'''
args.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'routes.cpp';exe=Path(directory)/'routes'
    path.write_text(cpp)
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra',str(path),'-o',str(exe)],check=True)
    with (args.output/'scenarios.csv').open('w')as out:
        subprocess.run([str(exe),str(args.runs),str(args.seed)],stdout=out,check=True)
rows=list(csv.DictReader((args.output/'scenarios.csv').open()))
assert len(rows)==args.runs
reachable=[r for r in rows if r['reachable']=='1']
summary={
    'source_sha256':hashlib.sha256(raw).hexdigest(),'seed':args.seed,'runs':len(rows),
    'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'reachable':len(reachable),'arrived':sum(r['arrived']=='1'for r in rows),
    'unreachable_held':sum(r['reachable']=='0'and r['unsafe']=='0'for r in rows),
    'reachable_stalled':sum(r['arrived']=='0'for r in reachable),
    'unsafe_cases':sum(int(r['unsafe'])>0 for r in rows),
    'scope':'two-Tank route/waypoint policy; no engine movement, unit collision, enemies or live map-grid adapter',
}
(args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
if summary['reachable_stalled'] or summary['unsafe_cases']:raise SystemExit(1)
