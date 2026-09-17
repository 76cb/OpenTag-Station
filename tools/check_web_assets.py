#!/usr/bin/env python3
"""Extract and syntax-check the dependency-free embedded JavaScript asset."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile

from web_asset_compression import browser_assets, generated_include, gzip_asset, writer_assets

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    try:
        stylesheet_bytes, javascript_bytes = browser_assets()
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    stylesheet_gzip = gzip_asset(stylesheet_bytes)
    javascript_gzip = gzip_asset(javascript_bytes)
    if (len(stylesheet_gzip) >= len(stylesheet_bytes) or
            len(javascript_gzip) >= len(javascript_bytes)):
        print("precompressed browser assets are not smaller than source", file=sys.stderr)
        return 1
    generated = generated_include()
    if (generated != generated_include() or
            "application_css_gzip_size" not in generated or
            "application_javascript_gzip_size" not in generated):
        print("precompressed C++ include generation is invalid or nondeterministic", file=sys.stderr)
        return 1
    writer_logic, writer_layout = writer_assets()
    writer = writer_logic + writer_layout
    javascript = javascript_bytes.decode("utf-8") + "\n" + writer.decode("utf-8")

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
        print(
            "embedded assets OK "
            f"(CSS {len(stylesheet_bytes)} -> {len(stylesheet_gzip)} gzip bytes; "
            f"JS {len(javascript_bytes)} -> {len(javascript_gzip)} gzip bytes)"
        )
        print(f"writer assets OK (behavior {len(writer_logic)}, layout/styles {len(writer_layout)}, "
              f"total {len(writer)} -> {len(gzip_asset(writer))} gzip bytes)")
    return checked.returncode


if __name__ == "__main__":
    raise SystemExit(main())
