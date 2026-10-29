#!/usr/bin/env python3
"""Build the deterministic v55 Windows installer from the verified v54 ZIP."""

from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib

from build_v53_release import add_file, assignment, replace_assignment


README = """STARTcraft AI v55 - 준비된 멀티와 절제된 방어 반응

설치 방법:
1. StarCraft, StarterBot, Pluto를 모두 종료합니다.
2. upgrade_ai_v55.cmd를 실행합니다.
3. SUCCESS: AI v55 compiled and arena updated가 나올 때까지 기다립니다.
4. 기존 run_STARTcraft_vs_Pluto.cmd로 실제 경기 한 판을 실행합니다.

v54 실제 경기에서 확인한 문제와 수정:
- 플토전 본진 마린을 10기, 2게이트면 12기로 제한하고 이후 자원을 메카닉에 씁니다.
- 옵저버와 멀리 있는 적 한 기 때문에 전체 병력이 자리를 이탈하지 않습니다.
- 방어 위치에 이미 도착한 마린을 세어서 추가 이동 명령을 반복하지 않습니다.
- 멀티를 짓기 전에 탱크 두 기가 멀티 앞에 도착해 시즈해야 합니다.
- 시즈모드 다음에 마인을 연구하고 벌처 두 기가 멀티 진입로에 마인을 설치합니다.
- 건설 기록: AI_reports\\AI_v55_construction.csv

검증:
- 업로드된 v54 실제 경기: Pluto 2Gate 상대 패배.
- 해당 경기에서 멀티 커맨드센터는 착공 4초 뒤 파괴됐습니다.
- 새 조건을 포함한 오프라인 정책 검사: v55 1000/1000 통과, seed 550001.
- 오프라인 검사는 StarCraft 엔진 경기 승률이 아니므로 v55 실제 경기 한 판이 추가로 필요합니다.

검증된 내장 소스는 source/StarterBot.cpp에도 포함되어 있습니다.
"""

UPGRADE = """@echo off
setlocal
cd /d "%~dp0"
echo STARTcraft AI v55 installer started.
echo The build may take several minutes. Keep this window open.
echo.
py -3 install_ai_v55.py
if errorlevel 1 (
  echo.
  echo INSTALL FAILED. Send the ERROR line back.
) else (
  echo.
  echo AI v55 installation finished.
)
pause
"""


def installer_from_v54(template: str, source: bytes) -> str:
    old_hash = assignment(template, "NEW_SHA256")
    new_hash = hashlib.sha256(source).hexdigest()
    encoded = base64.b64encode(zlib.compress(source, level=9)).decode("ascii")
    result = replace_assignment(template, "SOURCE_B64", encoded)
    result = replace_assignment(result, "OLD_SHA256", old_hash)
    result = replace_assignment(result, "NEW_SHA256", new_hash)
    replacements = {
        ".v54tmp": ".v55tmp",
        "AI_v54_build.log": "AI_v55_build.log",
        "v54 source checksum failed": "v55 source checksum failed",
        "Expected exact v53 or recoverable v54 source":
            "Expected exact v54 or recoverable v55 source",
        "before_v54_": "before_v55_",
        "BUILDING AI v54": "BUILDING AI v55",
        "AI v54: resumed construction and tech guard":
            "AI v55: prepared natural and disciplined reaction",
        "v54 marker missing": "v55 marker missing",
        "v54_last_backup.txt": "v55_last_backup.txt",
        "SUCCESS: AI v54 compiled": "SUCCESS: AI v55 compiled",
    }
    for old, new in replacements.items():
        if old not in result:
            raise ValueError(f"installer template text not found: {old}")
        result = result.replace(old, new)
    stale = (
        ".v54tmp",
        "AI_v54_build.log",
        "before_v54_",
        "AI v54:",
        "v54_last_backup.txt",
    )
    if any(text in result for text in stale):
        raise ValueError("stale v54 installer text remains")
    compile(result, "install_ai_v55.py", "exec")
    embedded = zlib.decompress(
        base64.b64decode(assignment(result, "SOURCE_B64"), validate=True)
    )
    if embedded != source or hashlib.sha256(embedded).hexdigest() != new_hash:
        raise ValueError("generated installer payload verification failed")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.read_bytes()
    if b"AI v55: prepared natural and disciplined reaction" not in source:
        raise ValueError("v55 source marker is missing")
    with zipfile.ZipFile(args.template) as old_package:
        template = old_package.read("install_ai_v54.py").decode("utf-8")
    installer = installer_from_v54(template, source)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as package:
        add_file(package, "README_KO.txt", README.encode("utf-8"))
        add_file(package, "install_ai_v55.py", installer.encode("utf-8"))
        add_file(package, "upgrade_ai_v55.cmd", UPGRADE.encode("utf-8"))
        add_file(package, "source/StarterBot.cpp", source)

    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    checksum = args.output.with_suffix(".sha256")
    checksum.write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
    print(f"built {args.output} ({args.output.stat().st_size} bytes)")
    print(f"sha256 {digest}")


if __name__ == "__main__":
    main()
