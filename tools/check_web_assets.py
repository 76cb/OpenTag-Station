#!/usr/bin/env python3
"""Extract and syntax-check the dependency-free embedded JavaScript asset."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSETS = ROOT / "src" / "web" / "web_assets.cpp"
START = 'const char application_javascript[] = R"JS('
END = ')JS";'


def main() -> int:
    source = ASSETS.read_text(encoding="utf-8")
    start = source.find(START)
    if start < 0:
        print(f"{ASSETS}: embedded JavaScript start marker is missing", file=sys.stderr)
        return 1
    start += len(START)
    end = source.find(END, start)
    if end < 0:
        print(f"{ASSETS}: embedded JavaScript end marker is missing", file=sys.stderr)
        return 1
    if source.find(START, start) >= 0:
        print(f"{ASSETS}: duplicate embedded JavaScript asset", file=sys.stderr)
        return 1

    javascript = source[start:end]
    if not javascript.strip() or "\x00" in javascript:
        print(f"{ASSETS}: embedded JavaScript is empty or contains NUL", file=sys.stderr)
        return 1

    node = shutil.which("node") or shutil.which("node.exe")
    if node is None:
        print("node is required for embedded JavaScript syntax validation", file=sys.stderr)
        return 2

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".js", encoding="utf-8", delete=False, dir=ROOT
    ) as temporary:
        temporary.write(javascript)
        temporary_path = pathlib.Path(temporary.name)
    target_path = str(temporary_path)
    if node.lower().endswith(".exe"):
        target_path = subprocess.check_output(
            ["wslpath", "-w", target_path],
            text=True,
        ).strip()
    try:
        checked = subprocess.run(
            [node, "--check", target_path],
            check=False,
            text=True,
        )
    finally:
        temporary_path.unlink(missing_ok=True)
    if checked.returncode == 0:
        print(f"embedded JavaScript syntax OK ({len(javascript.encode('utf-8'))} bytes)")
    return checked.returncode


if __name__ == "__main__":
    raise SystemExit(main())
