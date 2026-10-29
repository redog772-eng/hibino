from pathlib import Path
import re,subprocess,tempfile,zipfile
s=Path('source/StarterBot.cpp').read_text()
decl='static int AIV25Seen(BWAPI::UnitType kind);'
assert s.index(decl)<s.index('(AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway)')
cap=re.search(r'const int marineCap=c.completedCC>=2\?16:.*?;',s,re.S).group()
fn=re.search(r'static int AIV25Seen\(BWAPI::UnitType kind\) \{.*?\n\}',s,re.S).group()
prefix='''#include <map>
#include <utility>
#include <cassert>
namespace BWAPI { struct UnitType {int id;int getID()const{return id;}}; namespace UnitTypes {const UnitType Protoss_Gateway{1};}}
namespace {
'''
body='\nint cap(int bases){struct{int completedCC;}c{bases};'+cap+'return marineCap;}\nstd::map<int,std::pair<int,int>> g_v25SeenBuilding;\n'+fn+'\n}\nint main(){assert(cap(1)==10);g_v25SeenBuilding[1]={1,0};g_v25SeenBuilding[2]={1,0};assert(cap(1)==12);assert(cap(2)==16);}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';exe=Path(d)/'test'
 for fixed in [False,True]:
  p.write_text(prefix+(decl if fixed else '')+body)
  r=subprocess.run(['g++','-std=c++20',str(p),'-o',str(exe)],capture_output=True,text=True)
  assert (r.returncode==0)==fixed,r.stderr
  if fixed:subprocess.run([str(exe)],check=True)
print('PASS: reproduces undeclared-call error before fix; corrected extracted C++ compiles and cap cases 10/12/16 pass')
with zipfile.ZipFile('releases/STARTcraft_AI_v57_bunker_breach_defense.zip') as z:old=z.read('source/StarterBot.cpp').decode()
assert s.replace('// Used by the production policy before the intelligence implementation below.\n'+decl+'\n','')==old
print('PASS: sole gameplay-source change is forward declaration and comment')
