"""Exercise packaged installer acceptance and source integrity without MSBuild."""
import base64
import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile
import zlib
from build_v53_release import assignment

path = Path('releases/STARTcraft_AI_v60_combat_order_support.zip')
assert hashlib.sha256(path.read_bytes()).hexdigest() == path.with_suffix('.sha256').read_text().split()[0]
with zipfile.ZipFile(path) as archive:
    assert archive.testzip() is None
    source = archive.read('source/StarterBot.cpp')
    code = archive.read('install_ai_v60.py').decode()
assert source == Path('source/StarterBot.cpp').read_bytes()
assert zlib.decompress(base64.b64decode(assignment(code, 'SOURCE_B64'))) == source
namespace = {'__name__': 'test'}
exec(code, namespace)
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    namespace['ROOT'] = root
    namespace['MSBUILD'] = root / 'msbuild'
    for name in ['src/starterbot/StarterBot.cpp', 'bin/StarterBot.exe', 'visualstudio/STARTcraft.sln', 'msbuild']:
        fixture = root / name
        fixture.parent.mkdir(parents=True, exist_ok=True)
        fixture.write_bytes(b'fixture')
    target = root / 'src/starterbot/StarterBot.cpp'
    sys.argv = ['installer', '--check-only']
    checked = 0
    for version in range(54, 60):
        for previous in sorted(Path('releases').glob(f'STARTcraft_AI_v{version}_*.zip')):
            with zipfile.ZipFile(previous) as archive:
                data = archive.read('source/StarterBot.cpp')
            target.write_bytes(data)
            namespace['main']()
            assert target.read_bytes() == data
            checked += 1
    target.write_bytes(source)
    namespace['main']()
    assert target.read_bytes() == source
    target.write_bytes(b'unknown source')
    try:
        namespace['main']()
    except RuntimeError:
        pass
    else:
        raise AssertionError('unknown source accepted')
    assert target.read_bytes() == b'unknown source'
print(f'PASS: {checked} historical packages accepted, v60 accepted, unknown rejected; source and SHA-256 verified')
