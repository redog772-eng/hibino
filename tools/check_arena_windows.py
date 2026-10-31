"""Exercise actual INI transformation on both runtime fixtures, preserving AI config."""
from pathlib import Path
import tempfile
from configure_arena_windows import configure,window_ini

samples=[
    '[ai]\nai = custom.dll\n[window]\nwindowed = OFF ; old\nleft=9\n[auto_menu]\ngame = JOIN_FIRST\n',
    '[ai]\r\nai =\r\n[WINDOW]\r\n WINDOWED = ON\r\nwindowed=OFF\r\nwidth=800\r\n',
    '[ai]\nai = different.dll',
    '[window]\nwindowed=OFF\n[window]\nwindowed=OFF\n',
    '[window]\ncustom_key=keep',
]
values={'windowed':'ON','left':0,'top':0,'width':640,'height':480}
for sample in samples:
    result=window_ini(sample,values)
    assert window_ini(result,values)==result
    assert 'windowed = OFF' not in result and 'windowed=OFF' not in result
    assert 'keepwindowed' not in result
    for line in sample.splitlines():
        if line.startswith('ai =') or line.startswith('game ='):assert line in result
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    for runtime in ['host','pluto']:
        path=root/'arena/STARTcraft_vs_Pluto'/runtime/'bwapi-data/bwapi.ini'
        path.parent.mkdir(parents=True);path.write_text(samples[0])
    configure(root)
    for runtime,left in [('host',0),('pluto',680)]:
        text=(root/'arena/STARTcraft_vs_Pluto'/runtime/'bwapi-data/bwapi.ini').read_text()
        assert f'left = {left}' in text and 'windowed = ON' in text and 'ai = custom.dll' in text
    backups=list(root.rglob('window_backups/*'))
    assert len(backups)==1
    configure(root);assert len(list(root.rglob('window_backups/*')))==1
ps=Path('tools/run_arena.ps1').read_text()
assert "'BWAPI_CONFIG_WINDOW__WINDOWED'" in ps
assert 'Start-BwapiGame $hostRoot 0' in ps and 'Start-BwapiGame $guestRoot 680' in ps
assert 'Disable-WindowMode' not in ps
print('PASS: matching window settings, duplicate/missing keys, preservation, backup and repeatability; Windows GUI unrun')
