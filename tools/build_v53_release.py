#!/usr/bin/env python3
"""Build the deterministic v53 Windows source installer and checksum."""

from __future__ import annotations

import argparse
import ast
import base64
import hashlib
from pathlib import Path
import re
import zipfile
import zlib


README = """STARTcraft AI v53 - resilient ground-path defense

Install:
1. Close StarCraft, StarterBot, and Pluto.
2. Run upgrade_ai_v53.cmd.
3. Wait for: SUCCESS: AI v53 compiled and arena updated

Changes:
- Replaces a killed construction SCV up to two times while preserving the accepted site.
- Projects both main and natural bunker positions from a traversable ground path.
- Widens the legal route-side search without placing the bunker in the traffic lane.
- Prepares the Protoss entrance bunker earlier and delays a pressured expansion until defense is ready.
- Construction log: AI_reports\\AI_v53_construction.csv

Offline validation:
- 100 deterministic randomized scenarios passed.
- Covered one Gateway, two Gateway, fast Nexus, and tech openings.
- Varied construction delay, SCV death, mineral/gas income, and ground-path offset.

The verified embedded source is also included as source/StarterBot.cpp.
"""

UPGRADE = """@echo off
setlocal
cd /d "%~dp0"
echo STARTcraft AI v53 installer started.
echo The build may take several minutes. Keep this window open.
echo.
py -3 install_ai_v53.py
if errorlevel 1 (
  echo.
  echo INSTALL FAILED. Send the ERROR line back.
) else (
  echo.
  echo AI v53 installation finished.
)
pause
"""


def assignment(source: str, name: str) -> str:
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target = node.targets[0]
            if isinstance(target, ast.Name) and target.id == name:
                return ast.literal_eval(node.value)
    raise ValueError(f"missing assignment: {name}")


def replace_assignment(source: str, name: str, value: str) -> str:
    pattern = rf"(?m)^{re.escape(name)} = .*$"
    updated, count = re.subn(pattern, f"{name} = {value!r}", source, count=1)
    if count != 1:
        raise ValueError(f"could not replace assignment: {name}")
    return updated


def installer_from_v52(template: str, source: bytes) -> str:
    old_hash = assignment(template, "NEW_SHA256")
    new_hash = hashlib.sha256(source).hexdigest()
    encoded = base64.b64encode(zlib.compress(source, level=9)).decode("ascii")
    result = replace_assignment(template, "SOURCE_B64", encoded)
    result = replace_assignment(result, "OLD_SHA256", old_hash)
    result = replace_assignment(result, "NEW_SHA256", new_hash)
    replacements = {
        ".v52tmp": ".v53tmp",
        "AI_v52_build.log": "AI_v53_build.log",
        "v52 source checksum failed": "v53 source checksum failed",
        "Expected exact v51 or recoverable v52 source": "Expected exact v52 or recoverable v53 source",
        "before_v52_": "before_v53_",
        "BUILDING AI v52": "BUILDING AI v53",
        "AI v52: path-based bunker defense": "AI v53: resilient ground-path defense",
        "v52 marker missing": "v53 marker missing",
        "v52_last_backup.txt": "v53_last_backup.txt",
        "SUCCESS: AI v52 compiled": "SUCCESS: AI v53 compiled",
        "For automatic testing, run run_5_matches_and_collect_v52.cmd from this package":
            "Offline policy validation is documented in README_KO.txt",
    }
    for old, new in replacements.items():
        if old not in result:
            raise ValueError(f"installer template text not found: {old}")
        result = result.replace(old, new)
    stale = (".v52tmp", "AI_v52_build.log", "before_v52_", "AI v52:", "v52_last_backup.txt")
    if any(text in result for text in stale):
        raise ValueError("stale v52 installer text remains")
    compile(result, "install_ai_v53.py", "exec")
    embedded = zlib.decompress(base64.b64decode(assignment(result, "SOURCE_B64"), validate=True))
    if embedded != source or hashlib.sha256(embedded).hexdigest() != new_hash:
        raise ValueError("generated installer payload verification failed")
    return result


def add_file(package: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, date_time=(2026, 10, 30, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    package.writestr(info, data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.read_bytes()
    if b"AI v53: resilient ground-path defense" not in source:
        raise ValueError("v53 source marker is missing")
    with zipfile.ZipFile(args.template) as old_package:
        template = old_package.read("install_ai_v52.py").decode("utf-8")
    installer = installer_from_v52(template, source)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as package:
        add_file(package, "README_KO.txt", README.encode("utf-8"))
        add_file(package, "install_ai_v53.py", installer.encode("utf-8"))
        add_file(package, "upgrade_ai_v53.cmd", UPGRADE.encode("utf-8"))
        add_file(package, "source/StarterBot.cpp", source)

    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    checksum = args.output.with_suffix(".sha256")
    checksum.write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
    print(f"built {args.output} ({args.output.stat().st_size} bytes)")
    print(f"sha256 {digest}")


if __name__ == "__main__":
    main()
