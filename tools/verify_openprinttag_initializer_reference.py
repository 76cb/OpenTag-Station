#!/usr/bin/env python3
"""Verify the committed C++ golden vector against pinned upstream Python."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


REFERENCE_COMMIT = "7e09cc38df1c8e7824a67f5b1ae93071f52519ad"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-root", required=True, type=pathlib.Path)
    parser.add_argument(
        "--fixture",
        type=pathlib.Path,
        default=pathlib.Path("test/fixtures/openprinttag_initializer_7e09cc3_312_no_aux.hpp"),
    )
    args = parser.parse_args()
    spec_root = args.spec_root.resolve()
    fixture = args.fixture.resolve()

    revision = subprocess.run(
        ["git", "-c", f"safe.directory={spec_root}", "rev-parse", "HEAD"],
        cwd=spec_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if revision != REFERENCE_COMMIT:
        raise SystemExit(f"reference checkout is {revision}, expected {REFERENCE_COMMIT}")

    generated = subprocess.run(
        [
            sys.executable,
            str(spec_root / "utils" / "nfc_initialize.py"),
            "--size=312",
            "--block-size=4",
        ],
        cwd=spec_root,
        check=True,
        stdout=subprocess.PIPE,
    ).stdout

    fixture_text = fixture.read_text(encoding="utf-8")
    segments = re.findall(r'"([0-9a-f]+)"', fixture_text)
    expected = bytes.fromhex("".join(segments))
    if generated != expected:
        raise SystemExit(
            "pinned Python initializer output differs from the committed C++ golden vector"
        )
    print(f"OpenPrintTag initializer reference matches: {len(generated)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
