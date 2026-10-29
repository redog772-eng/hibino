#!/usr/bin/env python3
"""Extract and verify StarterBot.cpp from a STARTcraft installer ZIP."""

from __future__ import annotations

import argparse
import ast
import base64
import hashlib
from pathlib import Path
import zipfile
import zlib


def installer_values(source: str) -> dict[str, str]:
    wanted = {"SOURCE_B64", "NEW_SHA256"}
    values: dict[str, str] = {}
    for node in ast.parse(source).body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id in wanted:
            values[target.id] = ast.literal_eval(node.value)
    missing = wanted - values.keys()
    if missing:
        raise ValueError(f"installer is missing constants: {sorted(missing)}")
    return values


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--installer", default=None)
    args = parser.parse_args()

    with zipfile.ZipFile(args.archive) as package:
        installer = args.installer
        if installer is None:
            matches = [name for name in package.namelist() if name.startswith("install_ai_v") and name.endswith(".py")]
            if len(matches) != 1:
                raise ValueError(f"expected one embedded installer, found {matches}")
            installer = matches[0]
        values = installer_values(package.read(installer).decode("utf-8"))

    decoded = zlib.decompress(base64.b64decode(values["SOURCE_B64"], validate=True))
    digest = hashlib.sha256(decoded).hexdigest()
    if digest != values["NEW_SHA256"]:
        raise ValueError(f"source hash mismatch: expected {values['NEW_SHA256']}, got {digest}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(decoded)
    print(f"extracted {len(decoded)} bytes to {args.output} (sha256 {digest})")


if __name__ == "__main__":
    main()
