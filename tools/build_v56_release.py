#!/usr/bin/env python3
"""Build the deterministic v56 Windows installer from the verified v55 ZIP."""

from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib

from build_v53_release import add_file, assignment, replace_assignment


README = """STARTcraft AI v56 - 앞마당 고정 수비와 한 대씩 전진하는 시즈 라인

설치 방법:
1. StarCraft, StarterBot, Pluto를 모두 종료합니다.
2. upgrade_ai_v56.cmd를 실행합니다.
3. SUCCESS: AI v56 compiled and arena updated가 나올 때까지 기다립니다.
4. 기존 run_STARTcraft_vs_Pluto.cmd로 실제 경기 한 판을 실행합니다.

이번 수정:
- 앞마당 마린은 지정 위치에서 홀드하며 적 한 기를 끝까지 따라가지 않습니다.
- 앞마당 탱크 두 기는 벙커 뒤와 옆의 지정 위치에서 시즈 상태를 유지합니다.
- 공격할 때 탱크는 한 대씩 최대 185픽셀만 전진하고, 나머지 탱크는 시즈 상태로 엄호합니다.
- 감지된 지상 전투 유닛이 가까우면 다음 전진을 멈추고 이동 중인 탱크도 즉시 시즈합니다.
- 마린, 메딕, 벌처가 탱크 라인보다 너무 앞서면 라인 뒤로 복귀시킵니다.
- 옵저버처럼 공격할 수 없는 공중 유닛은 시즈 라인을 움직이지 않습니다.
- 마인 화면과 v55의 앞마당 선점 조건은 그대로 유지합니다.

기록 파일:
- AI_reports\\AI_v56_construction.csv
- AI_reports\\AI_v56_siege_line.csv

검증:
- 이번에 업로드된 실제 경기 파일은 v55가 아니라 v54 실행 기록입니다.
- Pluto의 Storm~tr 전략을 상대로 패배했고, 최신 세션에는 앞마당 커맨드센터 착공 기록이 없습니다.
- 도망가는 미끼 유닛과 스톰 압박을 추가한 오프라인 정책 검사에서 v55식 추격은 583/1000,
  v56 고정 라인 정책은 1000/1000을 통과했습니다. seed 560001.
- 오프라인 검사는 StarCraft 엔진의 승률이 아니므로 v56 실제 경기 한 판이 추가로 필요합니다.

검증된 내장 소스는 source/StarterBot.cpp에도 포함되어 있습니다.
"""

UPGRADE = """@echo off
setlocal
cd /d "%~dp0"
echo STARTcraft AI v56 installer started.
echo The build may take several minutes. Keep this window open.
echo.
py -3 install_ai_v56.py
if errorlevel 1 (
  echo.
  echo INSTALL FAILED. Send the ERROR line back.
) else (
  echo.
  echo AI v56 installation finished.
)
pause
"""


def installer_from_v55(template: str, source: bytes) -> str:
    old_hash = assignment(template, "NEW_SHA256")
    new_hash = hashlib.sha256(source).hexdigest()
    encoded = base64.b64encode(zlib.compress(source, level=9)).decode("ascii")
    result = replace_assignment(template, "SOURCE_B64", encoded)
    result = replace_assignment(result, "OLD_SHA256", old_hash)
    result = replace_assignment(result, "NEW_SHA256", new_hash)
    replacements = {
        ".v55tmp": ".v56tmp",
        "AI_v55_build.log": "AI_v56_build.log",
        "v55 source checksum failed": "v56 source checksum failed",
        "Expected exact v54 or recoverable v55 source":
            "Expected exact v55 or recoverable v56 source",
        "before_v55_": "before_v56_",
        "BUILDING AI v55": "BUILDING AI v56",
        "AI v55: prepared natural and disciplined reaction":
            "AI v56: anchored natural and leapfrog siege line",
        "v55 marker missing": "v56 marker missing",
        "v55_last_backup.txt": "v56_last_backup.txt",
        "SUCCESS: AI v55 compiled": "SUCCESS: AI v56 compiled",
    }
    for old, new in replacements.items():
        if old not in result:
            raise ValueError(f"installer template text not found: {old}")
        result = result.replace(old, new)
    stale = (
        ".v55tmp",
        "AI_v55_build.log",
        "before_v55_",
        "v55_last_backup.txt",
    )
    if any(text in result for text in stale):
        raise ValueError("stale v55 installer text remains")
    compile(result, "install_ai_v56.py", "exec")
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
    if b"AI v56: anchored natural and leapfrog siege line" not in source:
        raise ValueError("v56 source marker is missing")
    with zipfile.ZipFile(args.template) as old_package:
        template = old_package.read("install_ai_v55.py").decode("utf-8")
    installer = installer_from_v55(template, source)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as package:
        add_file(package, "README_KO.txt", README.encode("utf-8"))
        add_file(package, "install_ai_v56.py", installer.encode("utf-8"))
        add_file(package, "upgrade_ai_v56.cmd", UPGRADE.encode("utf-8"))
        add_file(package, "source/StarterBot.cpp", source)

    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    checksum = args.output.with_suffix(".sha256")
    checksum.write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
    print(f"built {args.output} ({args.output.stat().st_size} bytes)")
    print(f"sha256 {digest}")


if __name__ == "__main__":
    main()
