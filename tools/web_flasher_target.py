"""PlatformIO target that derives and builds the ESP Web Tools factory bundle."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import shutil

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


PROJECT_DIR = pathlib.Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
TOOLS_DIR = PROJECT_DIR / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from web_flasher import FlashPart, build_bundle, flash_size_bytes  # noqa: E402
from product_features import community_enabled  # noqa: E402


def git_short_sha() -> str:
    return subprocess.check_output(
        ["git", "-C", str(PROJECT_DIR), "rev-parse", "HEAD"],
        text=True,
    ).strip()


def evaluated_path(value: object) -> pathlib.Path:
    return pathlib.Path(env.subst(str(value)))  # type: ignore[name-defined]


def build_web_flasher(source: object, target: object, env: object) -> None:
    del source, target
    build_env = env
    board = build_env.BoardConfig()
    build_dir = pathlib.Path(build_env.subst("$BUILD_DIR"))
    if build_env.subst("$PIOENV") != "wt32-sc01-plus":
        raise RuntimeError("Only production firmware may be distributed")
    image_name = "opentag-station-factory.bin"
    manifest_name = "manifest.json"
    product_name = "OpenTag Station"
    application = build_dir / f"{build_env.subst('$PROGNAME')}.bin"
    extra_images = build_env.get("FLASH_EXTRA_IMAGES", [])
    parts = [
        FlashPart(
            name=evaluated_path(path).name,
            offset=int(str(offset), 0),
            path=evaluated_path(path),
        )
        for offset, path in extra_images
    ]
    parts.append(
        FlashPart(
            name="application",
            offset=int(build_env.subst("$ESP32_APP_OFFSET"), 0),
            path=application,
        )
    )
    catalog_pack = PROJECT_DIR / "community" / "community.pack"
    filesystem_root = build_dir / "community-filesystem-root"
    if filesystem_root.exists():
        shutil.rmtree(filesystem_root)
    filesystem_root.mkdir(parents=True, exist_ok=True)
    if community_enabled():
        shutil.copy2(catalog_pack, filesystem_root / "community.pack")
    filesystem_image = build_dir / "community-littlefs.bin"
    filesystem_tool = pathlib.Path(str(build_env.subst("$MKFSTOOL")))
    if not filesystem_tool.is_absolute():
        package_dir = build_env.PioPlatform().get_package_dir("tool-mklittlefs")
        if not package_dir:
            raise RuntimeError("Pinned LittleFS image tool is unavailable")
        filesystem_tool = pathlib.Path(package_dir) / filesystem_tool
    if not filesystem_tool.is_file():
        raise RuntimeError(f"LittleFS image tool is unavailable: {filesystem_tool}")
    subprocess.run([str(filesystem_tool), "-c", str(filesystem_root),
                    "-b", "4096", "-p", "256", "-s", str(0x5D0000),
                    str(filesystem_image)], check=True)
    listing = subprocess.check_output(
        [str(filesystem_tool), "-l", str(filesystem_image)], text=True)
    expected_catalog = f"{catalog_pack.stat().st_size}\t/community.pack\t"
    if community_enabled() and expected_catalog not in listing:
        raise RuntimeError("Generated LittleFS image does not contain the exact Community catalog")
    if not community_enabled() and "community.pack" in listing:
        raise RuntimeError("Disabled Community must not be seeded into factory LittleFS")
    parts.append(FlashPart(name="factory-littlefs", offset=0xA10000,
                           path=filesystem_image))

    command, size = build_bundle(
        python_executable=pathlib.Path(build_env.subst("$PYTHONEXE")),
        esptool=pathlib.Path(build_env.subst("$UPLOADER")),
        chip=str(board.get("build.mcu")),
        actual_flash_mode=str(board.get("build.flash_mode")),
        memory_type=str(board.get("build.arduino.memory_type", "")),
        flash_frequency=board.get("build.f_flash"),
        flash_size=str(board.get("upload.flash_size")),
        maximum_size=flash_size_bytes(str(board.get("upload.flash_size"))),
        parts=parts,
        application=application,
        source_sha=git_short_sha(),
        project_version=(PROJECT_DIR / "VERSION").read_text(encoding="utf-8").strip(),
        page_source=PROJECT_DIR / "web-flasher" / "index.html",
        manifest_source=PROJECT_DIR / "web-flasher" / manifest_name,
        manifest_name=manifest_name,
        image_name=image_name,
        product_name=product_name,
        output_dir=build_dir / "web-flasher",
        catalog_pack=catalog_pack,
        catalog_manifest=PROJECT_DIR / "community" / "manifest.json",
    )
    print(f"generated web flasher with {len(command)} merge arguments ({size} bytes)")


env.AddCustomTarget(  # type: ignore[name-defined]
    name="web-flasher",
    dependencies=["$BUILD_DIR/${PROGNAME}.bin"],
    actions=[
        env.VerboseAction(  # type: ignore[name-defined]
            build_web_flasher,
            "Generating ESP Web Tools first-install/recovery bundle",
        )
    ],
    title="Web Flasher",
    description="Build and validate the ESP Web Tools factory image and Pages assets",
)
