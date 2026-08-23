"""Generate deterministic precompressed assets in the PlatformIO build tree."""

from __future__ import annotations

import pathlib
import sys


Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.

TOOLS = pathlib.Path(env.subst("$PROJECT_DIR")) / "tools"  # type: ignore[name-defined]
sys.path.insert(0, str(TOOLS))

from web_asset_compression import generated_include  # noqa: E402


generated_directory = pathlib.Path(env.subst("$BUILD_DIR")) / "generated"  # type: ignore[name-defined]
generated_directory.mkdir(parents=True, exist_ok=True)
target = generated_directory / "opentag_web_assets_gzip.inc"
content = generated_include()
if not target.exists() or target.read_text(encoding="utf-8") != content:
    target.write_text(content, encoding="utf-8", newline="\n")

env.Append(CPPPATH=[str(generated_directory)])  # type: ignore[name-defined]
