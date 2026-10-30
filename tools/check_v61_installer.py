"""Validate v61 package, historical upgrades and transaction rollback.

MSBuild/tasklist are mocked to test installer control flow, not actual Windows
compilation. The executable marker must also occur in the real source.
"""
import ast
import base64
import hashlib
from pathlib import Path
import sys
import tempfile
import types
import zipfile
import zlib
from build_v53_release import assignment

path = Path('releases/STARTcraft_AI_v61_single_owner_natural.zip')
assert hashlib.sha256(path.read_bytes()).hexdigest() == path.with_suffix('.sha256').read_text().split()[0]
with zipfile.ZipFile(path) as archive:
    assert archive.testzip() is None
    source = archive.read('source/StarterBot.cpp')
    code = archive.read('install_ai_v61.py').decode()
assert source == Path('source/StarterBot.cpp').read_bytes()
assert zlib.decompress(base64.b64decode(assignment(code, 'SOURCE_B64'))) == source
markers = [n.value for n in ast.walk(ast.parse(code)) if isinstance(n, ast.Constant)
           and isinstance(n.value, bytes) and n.value.startswith(b'AI v61:')]
assert len(markers) == 1 and b'printf("' + markers[0] + b'")' in source
assert b'out<<"61,"' in source
namespace = {'__name__': 'test'}
exec(code, namespace)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    namespace['ROOT'] = root
    namespace['MSBUILD'] = root / 'msbuild'
    for name in ['src/starterbot/StarterBot.cpp', 'bin/StarterBot.exe',
                 'visualstudio/STARTcraft.sln', 'msbuild',
                 'arena/STARTcraft_vs_Pluto/host/bin/StarterBot.exe']:
        fixture = root / name
        fixture.parent.mkdir(parents=True, exist_ok=True)
        fixture.write_bytes(b'fixture')
    target = root / 'src/starterbot/StarterBot.cpp'
    exe = root / 'bin/StarterBot.exe'
    arena = root / 'arena/STARTcraft_vs_Pluto/host/bin/StarterBot.exe'
    sys.argv = ['installer', '--check-only']
    checked = 0
    for version in range(54, 62):
        for previous in sorted(Path('releases').glob(f'STARTcraft_AI_v{version}_*.zip')):
            with zipfile.ZipFile(previous) as archive:
                data = archive.read('source/StarterBot.cpp')
            target.write_bytes(data)
            namespace['main']()
            assert target.read_bytes() == data
            checked += 1
    target.write_bytes(b'unknown source')
    try:
        namespace['main']()
    except RuntimeError:
        pass
    else:
        raise AssertionError('unknown source accepted')
    assert target.read_bytes() == b'unknown source'

    # Run the installer's full successful transaction and its rollback path.
    with zipfile.ZipFile('releases/STARTcraft_AI_v59_high_ground_cover.zip') as archive:
        old_source = archive.read('source/StarterBot.cpp')
    valid_marker = True

    class Build:
        returncode = 0

        def __init__(self, cmd, **kwargs):
            assert '/t:Rebuild' in cmd and '/p:Platform=Win32' in cmd
            assert target.read_bytes() == source
            exe.write_bytes(b'MZ-test-only-' + (markers[0] if valid_marker else b'wrong marker'))

        def poll(self):
            return 0

    namespace['subprocess'] = types.SimpleNamespace(
        run=lambda *args, **kwargs: types.SimpleNamespace(stdout=b'', returncode=0),
        Popen=Build, STDOUT=-2, DEVNULL=-3)
    sys.argv = ['installer']
    target.write_bytes(old_source)
    exe.write_bytes(b'old-executable')
    arena.write_bytes(b'old-arena')
    namespace['main']()
    assert target.read_bytes() == source
    assert exe.read_bytes() == arena.read_bytes() and markers[0] in exe.read_bytes()
    assert (root / 'ai_backups/v61_last_backup.txt').is_file()
    valid_marker = False
    target.write_bytes(old_source)
    exe.write_bytes(b'old-executable')
    arena.write_bytes(b'old-arena')
    try:
        namespace['main']()
    except RuntimeError as error:
        assert 'marker missing' in str(error)
    else:
        raise AssertionError('missing EXE marker accepted')
    assert target.read_bytes() == old_source
    assert exe.read_bytes() == b'old-executable'
    assert arena.read_bytes() == b'old-arena'
print(f'PASS: {checked} packages accepted; unknown rejected; marker, arena update and rollback verified (mock build)')
