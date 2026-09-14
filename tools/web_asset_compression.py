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
    writer = extract_asset((ROOT / "src/web/writer_assets.cpp").read_text(), "writer_javascript", "WRITER")
    assert len(writer) <= 20 * 1024, "writer module exceeds its independent flash budget"
    return (
        "// Generated deterministically by tools/precompress_web_assets.py.\n"
        "// Do not edit this build artifact.\n\n"
        + _array("application_css_gzip", gzip_asset(stylesheet))
        + "\n"
        + _array("application_javascript_gzip", gzip_asset(javascript))
        + _array("writer_javascript_gzip", gzip_asset(writer))
    )
