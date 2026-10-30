"""Compile and exercise the actual advance gate and projection helper.
Randomized policy scenarios, not StarCraft-engine matches.
"""
from pathlib import Path
import re,subprocess,tempfile
s=Path('source/StarterBot.cpp').read_text()
gate=re.search(r'static bool AIV59CanAdvance\(.*?\n\}',s,re.S).group()
progress=re.search(r'static int AIV56Progress\(.*?\n\}',s,re.S).group()
prefix='''#include <algorithm>
#include <cmath>
#include <random>
#include <cassert>
#include <iostream>
namespace BWAPI {struct Position{int x,y;bool isValid(){return x>=0&&y>=0;}};}
'''
checks=r'''
int main(){std::mt19937 rng(590001);int allow=0,block=0;
for(int i=0;i<1000;++i){
 int tanks=1+rng()%6,cover=rng()%4,screen=rng()%7,gap=128+rng()%400;
 bool contact=rng()%3==0,path=rng()%4!=0;
 bool actual=AIV59CanAdvance(tanks,cover,screen,contact,gap,path);
 if(actual){++allow;assert(tanks>2);assert(cover>1);assert(screen>2);assert(!contact);assert(gap<=320);assert(path);}else ++block;
 assert(!AIV59CanAdvance(1,cover,screen,contact,gap,path));
 assert(!AIV59CanAdvance(tanks,0,screen,contact,gap,path));
 assert(!AIV59CanAdvance(tanks,cover,screen,true,gap,path));
}
assert(AIV59CanAdvance(3,2,3,false,320,true));
assert(!AIV59CanAdvance(3,2,3,false,321,true));
assert(AIV56Progress({300,400},{0,0},{300,400})==500);
assert(AIV56Progress({0,500},{0,0},{0,1000})==500);
std::cout<<"1000 policy scenarios: allowed="<<allow<<", blocked="<<block<<"; safety assertions and diagonal projection PASS\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'advance.cpp';exe=Path(d)/'advance';p.write_text(prefix+gate+progress+checks)
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
# Guard the previously independent natural controller against regression.
natural=s[s.index('static void AIV55PrepareNatural()'):s.index('void AIV20MedicExitClearance()')]
assert 'tank->move(' not in natural and 'tank->unsiege(' not in natural
assert 'tank->siege(' not in natural
assert 'game->getGroundHeight' in s
print('Natural controller issues no Tank movement/siege orders: PASS')
