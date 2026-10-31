"""Launcher-only display correction; leaves historical AI packages intact."""
import hashlib
from pathlib import Path
import zipfile
from build_v53_release import add_file

path=Path('releases/STARTcraft_large_window_fix.zip')
readme='''STARTcraft - 화면을 꽉 채우는 창 모드 수정

게임과 봇을 모두 종료 -> 새 폴더에 압축 해제 -> 이 폴더의
run_STARTcraft_vs_Pluto.cmd 실행.
AI 재설치나 재컴파일은 필요 없습니다. 기존 v66 AI를 그대로 사용합니다.
기본값은 주 모니터 크기의 테두리 없는 창 모드입니다.
두 게임 모두 화면을 채우므로 Alt+Tab으로 전환합니다.
1920x1080 등 화면 크기를 자동으로 읽습니다. 640x480 고정 크기를 해제했습니다.
원래 bwapi.ini는 arena/STARTcraft_vs_Pluto/window_backups에 백업합니다.

다른 설치 위치는 다음처럼 지정합니다:
run_STARTcraft_vs_Pluto.cmd -Root "D:\\starcraft-api\\STARTcraft"

INI 변경/백업 및 PowerShell 구문을 검사했습니다.
실제 Windows 게임 화면 동작은 이 클라우드 환경에서 검증하지 못했습니다.
'''
with zipfile.ZipFile(path,'w') as z:
    add_file(z,'README_KO.txt',readme.encode())
    add_file(z,'run_STARTcraft_vs_Pluto.cmd',Path('tools/run_STARTcraft_vs_Pluto.cmd').read_bytes())
    for name in ['run_arena.ps1','configure_arena_windows.py','arena_display.ps1']:
        add_file(z,'tools/'+name,Path('tools',name).read_bytes())
path.with_suffix('.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
print(path)
