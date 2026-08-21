#!/usr/bin/env python3
"""Build and validate the ESP Web Tools first-install/recovery bundle."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shlex
import shutil
import subprocess
from dataclasses import dataclass
from typing import Any, Iterable


FACTORY_IMAGE_NAME = "opentag-station-factory.bin"
MANIFEST_NAME = "manifest.json"
PAGE_NAME = "index.html"
ESP_WEB_TOOLS_MODULE = (
    "https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module"
)


class FlasherError(RuntimeError):
    """Raised when a flasher input or generated asset is unsafe."""


@dataclass(frozen=True)
class FlashPart:
    name: str
    offset: int
    path: pathlib.Path


def web_tools_flash_mode(actual_mode: str, memory_type: str) -> str:
    """Apply ESP Web Tools' compatibility rule without changing flash layout."""
    normalized_mode = actual_mode.lower()
    normalized_memory = memory_type.lower()
    if normalized_memory.startswith("opi_"):
        return "dout"
    if normalized_mode in {"qio", "qout"}:
        return "dio"
    if normalized_mode not in {"dio", "dout"}:
        raise FlasherError(f"unsupported evaluated flash mode: {actual_mode}")
    return normalized_mode


def flash_frequency_label(value: int | str) -> str:
    match = re.fullmatch(r"\s*([0-9]+)L?\s*", str(value), re.IGNORECASE)
    if match is None:
        raise FlasherError(f"invalid evaluated flash frequency: {value}")
    digits = match.group(1)
    hertz = int(digits)
    if hertz <= 0 or hertz % 1_000_000 != 0:
        raise FlasherError(f"flash frequency is not an integral MHz value: {value}")
    return f"{hertz // 1_000_000}m"


def flash_size_bytes(value: str) -> int:
    match = re.fullmatch(r"\s*([0-9]+)\s*(KB|MB)\s*", value, re.IGNORECASE)
    if match is None:
        raise FlasherError(f"invalid evaluated flash size: {value}")
    amount = int(match.group(1))
    multiplier = 1024 if match.group(2).lower() == "kb" else 1024 * 1024
    size = amount * multiplier
    if size <= 0:
        raise FlasherError(f"invalid evaluated flash size: {value}")
    return size


def read_manifest(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FlasherError(f"invalid manifest {path}: {error}") from error
    if not isinstance(value, dict):
        raise FlasherError("manifest root must be an object")
    return value


def validate_manifest(manifest: dict[str, Any]) -> str:
    if manifest.get("name") != "OpenTag Station":
        raise FlasherError("manifest name must be OpenTag Station")
    version = manifest.get("version")
    if not isinstance(version, str) or not version.strip():
        raise FlasherError("manifest version must be a non-empty string")
    if manifest.get("new_install_prompt_erase") is not True:
        raise FlasherError("manifest must let users review the new-install erase choice")
    if manifest.get("new_install_improv_wait_time") != 0:
        raise FlasherError("manifest must not wait for unsupported Improv Serial")
    builds = manifest.get("builds")
    if not isinstance(builds, list) or len(builds) != 1:
        raise FlasherError("manifest must contain exactly one build")
    build = builds[0]
    if not isinstance(build, dict) or build.get("chipFamily") != "ESP32-S3":
        raise FlasherError("manifest build must target ESP32-S3")
    if build.get("improv") is not False:
        raise FlasherError("manifest must declare that Improv Serial is unavailable")
    parts = build.get("parts")
    if not isinstance(parts, list) or len(parts) != 1:
        raise FlasherError("manifest must reference one merged factory image")
    part = parts[0]
    if not isinstance(part, dict):
        raise FlasherError("manifest part must be an object")
    if part.get("path") != FACTORY_IMAGE_NAME or part.get("offset") != 0:
        raise FlasherError("merged factory image must be flashed at offset 0")
    return version


def validate_page(path: pathlib.Path) -> None:
    try:
        page = path.read_text(encoding="utf-8")
    except OSError as error:
        raise FlasherError(f"cannot read installer page {path}: {error}") from error
    required = (
        "OpenTag Station",
        "Browser Firmware Installer",
        "Install OpenTag Station",
        "Chrome or Edge",
        "USB connection to a WT32-SC01 Plus",
        'manifest="manifest.json"',
        ESP_WEB_TOOLS_MODULE,
    )
    missing = [text for text in required if text not in page]
    if missing:
        raise FlasherError(f"installer page is missing: {', '.join(missing)}")
    if page.count("<esp-web-install-button") != 1:
        raise FlasherError("installer page must contain exactly one install button")


def validate_source_assets(page: pathlib.Path, manifest: pathlib.Path) -> None:
    validate_page(page)
    validate_manifest(read_manifest(manifest))


def validate_parts(parts: Iterable[FlashPart], maximum_size: int) -> list[FlashPart]:
    ordered = sorted(parts, key=lambda part: part.offset)
    if not ordered or ordered[0].offset != 0:
        raise FlasherError("factory layout must begin with a bootloader at offset 0")
    previous_end = 0
    for part in ordered:
        if part.offset < 0:
            raise FlasherError(f"negative flash offset for {part.name}")
        if not part.path.is_file():
            raise FlasherError(f"missing factory input: {part.path}")
        size = part.path.stat().st_size
        if size <= 0:
            raise FlasherError(f"empty factory input: {part.path}")
        if part.offset < previous_end:
            raise FlasherError(f"overlapping factory input at {part.offset:#x}: {part.name}")
        previous_end = part.offset + size
        if previous_end > maximum_size:
            raise FlasherError(f"{part.name} exceeds the evaluated flash size")
    return ordered


def validate_embedded_sha(application: pathlib.Path, source_sha: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{12}", source_sha):
        raise FlasherError(f"expected the existing 12-character Git SHA, got {source_sha!r}")
    if source_sha.encode("ascii") not in application.read_bytes():
        raise FlasherError(f"application does not embed source Git SHA {source_sha}")


def validate_bundle(bundle_dir: pathlib.Path, maximum_size: int) -> tuple[str, int]:
    page = bundle_dir / PAGE_NAME
    manifest_path = bundle_dir / MANIFEST_NAME
    validate_page(page)
    version = validate_manifest(read_manifest(manifest_path))
    image = bundle_dir / FACTORY_IMAGE_NAME
    if not image.is_file():
        raise FlasherError(f"manifest target is missing: {image}")
    size = image.stat().st_size
    if size <= 0 or size > maximum_size:
        raise FlasherError(f"factory image size {size} is outside the flash bound")
    if image.read_bytes()[:1] != b"\xe9":
        raise FlasherError("factory image does not begin with an ESP image header")
    if not (bundle_dir / ".nojekyll").is_file():
        raise FlasherError("GitHub Pages bundle is missing .nojekyll")
    return version, size


def build_bundle(
    *,
    python_executable: pathlib.Path,
    esptool: pathlib.Path,
    chip: str,
    actual_flash_mode: str,
    memory_type: str,
    flash_frequency: int | str,
    flash_size: str,
    maximum_size: int,
    parts: Iterable[FlashPart],
    application: pathlib.Path,
    source_sha: str,
    project_version: str,
    page_source: pathlib.Path,
    manifest_source: pathlib.Path,
    output_dir: pathlib.Path,
) -> tuple[list[str], int]:
    validate_source_assets(page_source, manifest_source)
    validate_embedded_sha(application, source_sha)
    ordered = validate_parts(parts, maximum_size)
    names = {part.path.name for part in ordered}
    required_names = {"bootloader.bin", "partitions.bin", "boot_app0.bin", application.name}
    if len(ordered) != len(required_names) or names != required_names:
        raise FlasherError(
            f"evaluated upload inputs changed: expected {sorted(required_names)}, got {sorted(names)}"
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    image = output_dir / FACTORY_IMAGE_NAME
    image.unlink(missing_ok=True)
    shutil.copy2(page_source, output_dir / PAGE_NAME)
    (output_dir / ".nojekyll").write_bytes(b"")

    merge_mode = web_tools_flash_mode(actual_flash_mode, memory_type)
    command = [
        str(python_executable),
        str(esptool),
        "--chip",
        chip,
        "merge_bin",
        "--output",
        str(image),
        "--flash_mode",
        merge_mode,
        "--flash_freq",
        flash_frequency_label(flash_frequency),
        "--flash_size",
        flash_size,
    ]
    for part in ordered:
        command.extend((f"0x{part.offset:x}", str(part.path)))
    print(f"factory merge command: {shlex.join(command)}")
    subprocess.run(command, check=True)
    subprocess.run(
        [str(python_executable), str(esptool), "--chip", chip, "image_info", str(image)],
        check=True,
        stdout=subprocess.DEVNULL,
    )

    merged = image.read_bytes()
    for part in ordered:
        if part.offset == 0:
            continue  # esptool intentionally rewrites the boot header flash parameters.
        content = part.path.read_bytes()
        if merged[part.offset : part.offset + len(content)] != content:
            raise FlasherError(f"merged image does not preserve {part.name} at {part.offset:#x}")

    manifest = read_manifest(manifest_source)
    manifest["version"] = f"{project_version}+{source_sha}"
    (output_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    _, size = validate_bundle(output_dir, maximum_size)
    expected_end = max(part.offset + part.path.stat().st_size for part in ordered)
    if size < expected_end:
        raise FlasherError("merged image is shorter than its final evaluated upload input")
    print(f"factory image: {image} ({size} bytes, flash bound {maximum_size} bytes)")
    return command, size


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    source = subparsers.add_parser("validate-source")
    source.add_argument("--page", type=pathlib.Path, required=True)
    source.add_argument("--manifest", type=pathlib.Path, required=True)

    bundle = subparsers.add_parser("validate-bundle")
    bundle.add_argument("--bundle-dir", type=pathlib.Path, required=True)
    bundle.add_argument("--maximum-size", type=int, required=True)

    args = parser.parse_args()
    try:
        if args.command == "validate-source":
            validate_source_assets(args.page, args.manifest)
            print("web flasher source assets valid")
        else:
            version, size = validate_bundle(args.bundle_dir, args.maximum_size)
            print(f"web flasher bundle valid: {version}, {size} bytes")
    except FlasherError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
