#!/usr/bin/env python3
"""Deterministically extract and gzip the embedded browser assets."""

from __future__ import annotations

import gzip
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSETS = ROOT / "src" / "web" / "web_assets.cpp"


def extract_asset(source: str, name: str, delimiter: str) -> bytes:
    start_marker = f'const char {name}[] = R"{delimiter}('
    end_marker = f'){delimiter}";'
    start = source.find(start_marker)
    if start < 0:
        raise ValueError(f"embedded asset start marker is missing: {name}")
    start += len(start_marker)
    end = source.find(end_marker, start)
    if end < 0:
        raise ValueError(f"embedded asset end marker is missing: {name}")
    if source.find(start_marker, start) >= 0:
        raise ValueError(f"duplicate embedded asset: {name}")
    value = source[start:end].encode("utf-8")
    if not value or b"\x00" in value:
        raise ValueError(f"embedded asset is empty or contains NUL: {name}")
    return value


def browser_assets() -> tuple[bytes, bytes]:
    source = ASSETS.read_text(encoding="utf-8")
    return (
        extract_asset(source, "application_css", "CSS"),
        extract_asset(source, "application_javascript", "JS"),
    )


def gzip_asset(value: bytes) -> bytes:
    return gzip.compress(value, compresslevel=9, mtime=0)


def writer_assets() -> tuple[bytes, bytes]:
    source = (ROOT / "src/web/writer_assets.cpp").read_text(encoding="utf-8")
    logic = extract_asset(source.replace(')WRITER"', ')WRITER";'), "writer_javascript", "WRITER")
    layout = (ROOT / "src/web/writer_layout.inc").read_text(encoding="utf-8")
    layout = layout.split('R"LAYOUT(', 1)[1].split(')LAYOUT"', 1)[0].encode("utf-8")
    # Keep core asset limits unchanged. The inventory/editor behavior gets 2 KiB
    # beyond the old writer limit after moving templates/styles/field schemas out.
    assert len(logic) <= 22 * 1024, "writer behavior exceeds its independent flash budget"
    assert len(layout) <= 10 * 1024, "writer layout exceeds its independent flash budget"
    return logic, layout


def _array(name: str, value: bytes) -> str:
    rows = []
    for offset in range(0, len(value), 16):
        row = ", ".join(f"0x{byte:02x}" for byte in value[offset : offset + 16])
        rows.append(f"    {row},")
    return (
        f"const std::uint8_t {name}[] = {{\n"
        + "\n".join(rows)
        + f"\n}};\nconst std::size_t {name}_size = sizeof({name});\n"
    )


def generated_include() -> str:
    stylesheet, javascript = browser_assets()
    writer = b"".join(writer_assets())
    return (
        "// Generated deterministically by tools/precompress_web_assets.py.\n"
        "// Do not edit this build artifact.\n\n"
        + _array("application_css_gzip", gzip_asset(stylesheet))
        + "\n"
        + _array("application_javascript_gzip", gzip_asset(javascript))
        + _array("writer_javascript_gzip", gzip_asset(writer))
    )
