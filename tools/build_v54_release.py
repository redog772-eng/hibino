#!/usr/bin/env python3
"""Build the deterministic v54 Windows installer from the verified v53 ZIP."""

from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib

from build_v53_release import add_file, assignment, replace_assignment


README = """STARTcraft AI v54 - construction resume and tech guard

Install:
1. Close StarCraft, StarterBot, and Pluto.
2. Run upgrade_ai_v54.cmd.
3. Wait for: SUCCESS: AI v54 compiled and arena updated
4. Run the existing run_STARTcraft_vs_Pluto.cmd for one actual validation match.

Changes from the uploaded v53 actual match:
- If an SCV dies after a Terran foundation starts, a nearby safe SCV resumes it.
- Checks BWAPI canMake before issuing tech-building commands.
- Holds one additional Marine against a confirmed two-Gateway opening.
- Construction log: AI_reports\\AI_v54_construction.csv

Validation:
- Offline randomized scenarios: 1000/1000 passed with seed 540001.
- Actual v53 match: LOSS to Pluto 1GateCore at about 15:18.
- That match exposed a Factory stopped from 6:05 to 12:36 and 41 invalid Starport orders.
- Run one new actual match after installing v54 and return AI_v54_construction.csv,
  pluto.log, pluto_infer.log, and the replay for confirmation.

The verified embedded source is also included as source/StarterBot.cpp.
"""

UPGRADE = """@echo off
setlocal
cd /d "%~dp0"
echo STARTcraft AI v54 installer started.
echo The build may take several minutes. Keep this window open.
echo.
py -3 install_ai_v54.py
if errorlevel 1 (
  echo.
  echo INSTALL FAILED. Send the ERROR line back.
) else (
  echo.
  echo AI v54 installation finished.
)
pause
"""


def installer_from_v53(template: str, source: bytes) -> str:
    old_hash = assignment(template, "NEW_SHA256")
    new_hash = hashlib.sha256(source).hexdigest()
    encoded = base64.b64encode(zlib.compress(source, level=9)).decode("ascii")
    result = replace_assignment(template, "SOURCE_B64", encoded)
    result = replace_assignment(result, "OLD_SHA256", old_hash)
    result = replace_assignment(result, "NEW_SHA256", new_hash)
    replacements = {
        ".v53tmp": ".v54tmp",
        "AI_v53_build.log": "AI_v54_build.log",
        "v53 source checksum failed": "v54 source checksum failed",
        "Expected exact v52 or recoverable v53 source": "Expected exact v53 or recoverable v54 source",
        "before_v53_": "before_v54_",
        "BUILDING AI v53": "BUILDING AI v54",
        "AI v53: resilient ground-path defense": "AI v54: resumed construction and tech guard",
        "v53 marker missing": "v54 marker missing",
        "v53_last_backup.txt": "v54_last_backup.txt",
        "SUCCESS: AI v53 compiled": "SUCCESS: AI v54 compiled",
    }
    for old, new in replacements.items():
        if old not in result:
            raise ValueError(f"installer template text not found: {old}")
        result = result.replace(old, new)
    stale = (".v53tmp", "AI_v53_build.log", "before_v53_", "AI v53:", "v53_last_backup.txt")
    if any(text in result for text in stale):
        raise ValueError("stale v53 installer text remains")
    compile(result, "install_ai_v54.py", "exec")
    embedded = zlib.decompress(base64.b64decode(assignment(result, "SOURCE_B64"), validate=True))
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
    if b"AI v54: resumed construction and tech guard" not in source:
        raise ValueError("v54 source marker is missing")
    with zipfile.ZipFile(args.template) as old_package:
        template = old_package.read("install_ai_v53.py").decode("utf-8")
    installer = installer_from_v53(template, source)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as package:
        add_file(package, "README_KO.txt", README.encode("utf-8"))
        add_file(package, "install_ai_v54.py", installer.encode("utf-8"))
        add_file(package, "upgrade_ai_v54.cmd", UPGRADE.encode("utf-8"))
        add_file(package, "source/StarterBot.cpp", source)

    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    checksum = args.output.with_suffix(".sha256")
    checksum.write_text(f"{digest}  {args.output.name}\n", encoding="ascii")
    print(f"built {args.output} ({args.output.stat().st_size} bytes)")
    print(f"sha256 {digest}")


if __name__ == "__main__":
    main()
