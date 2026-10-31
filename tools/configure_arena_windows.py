"""Set both runtimes to one explicit BWAPI window mode, preserving other INI sections."""
import argparse
from pathlib import Path
import re
import shutil
import uuid


def window_ini(text, values):
    newline='\r\n' if '\r\n' in text else '\n'
    lines=text.splitlines(keepends=True);out=[];in_window=False;seen=set();found=False

    def finish():
        if in_window:
            if out and not out[-1].endswith(('\n','\r')):out[-1]+=newline
            for key,value in values.items():
                if key not in seen:out.append(f'{key} = {value}{newline}')

    for line in lines:
        section=re.match(r'^\s*\[\s*([^\]]+?)\s*\]',line)
        if section:
            finish();in_window=section.group(1).strip().lower()=='window';seen=set()
            found|=in_window
        key=re.match(r'^\s*([A-Za-z_]+)\s*=',line) if in_window else None
        if key and key.group(1).lower() in values:
            name=key.group(1).lower();out.append(f'{name} = {values[name]}{newline}');seen.add(name)
        else:
            out.append(line)
    finish()
    if not found:
        if out and not out[-1].endswith(('\n','\r')):out[-1]+=newline
        out.append('[window]'+newline)
        out.extend(f'{key} = {value}{newline}'for key,value in values.items())
    return ''.join(out)


def configure(root, mode='Windowed'):
    arena=root/'arena/STARTcraft_vs_Pluto';changes=[]
    for runtime,left in [('host',0),('pluto',680)]:
        path=arena/runtime/'bwapi-data/bwapi.ini'
        original=path.read_bytes() # Preflight both files before changing either.
        encoding='utf-16' if original.startswith((b'\xff\xfe',b'\xfe\xff')) else (
            'utf-8-sig' if original.startswith(b'\xef\xbb\xbf') else 'utf-8')
        try:text=original.decode(encoding)
        except UnicodeDecodeError:encoding='cp1252';text=original.decode(encoding)
        values={'windowed':'ON' if mode=='Windowed' else 'OFF','left':left,'top':0,'width':640,'height':480}
        updated=window_ini(text,values).encode(encoding)
        changes.append((runtime,path,original,updated))
    backup=arena/'window_backups'/uuid.uuid4().hex
    for runtime,path,original,updated in changes:
        if original==updated:continue
        destination=backup/runtime/'bwapi.ini';destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,destination)
        temporary=path.with_suffix('.ini.windowtmp');temporary.write_bytes(updated);temporary.replace(path)
    print(f'Both BWAPI runtimes configured: {mode}; host=(0,0), Pluto=(680,0), client=640x480')
    if backup.exists():print('Original INI backups:',backup)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--mode',choices=['Windowed','Fullscreen'],default='Windowed')
    args=parser.parse_args();configure(args.root,args.mode)
