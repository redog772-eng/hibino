"""Check current Siege gas reservation and both consumer integration points."""
from pathlib import Path
import subprocess
import tempfile
from check_v61_ownership import function

produce = function('AIV11Produce')
assert 'me->gas()>=100+(std::max)(g_v28GasReserve,siegeGasReserve)' in produce
assert 'me->gas()>=25+medicGasReserve' in produce
cpp = '#include <cassert>\nint ' + function('AIV63SiegeGasReserve') + r'''
int main(){
 assert(AIV63SiegeGasReserve(true,false,0)==0);
 assert(AIV63SiegeGasReserve(true,false,1)==150);
 assert(AIV63SiegeGasReserve(true,true,1)==0);
 assert(AIV63SiegeGasReserve(false,false,1)==0);
 for(int gas=0;gas<=500;++gas){
  bool tankAllowed=gas>=100+AIV63SiegeGasReserve(true,false,1);
  if(tankAllowed)assert(gas-100>=150);
  bool medicAllowed=gas>=25+AIV63SiegeGasReserve(true,false,1);
  if(medicAllowed)assert(gas-25>=150);
 }
 assert(249<100+AIV63SiegeGasReserve(true,false,1));
 assert(250>=100+AIV63SiegeGasReserve(true,false,1));
 assert(100>=100+AIV63SiegeGasReserve(true,true,1));
 assert(100>=100+AIV63SiegeGasReserve(true,false,0));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'siege.cpp';exe=Path(directory)/'siege'
    path.write_text(cpp)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra',str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: after the first Tank, spending preserves 150 Siege gas until research starts, then releases it')
