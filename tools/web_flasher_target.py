"""PlatformIO target that derives and builds the ESP Web Tools factory bundle."""

from __future__ import annotations

import pathlib
import subprocess
import sys

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


PROJECT_DIR = pathlib.Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
TOOLS_DIR = PROJECT_DIR / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from web_flasher import FlashPart, build_bundle, flash_size_bytes  # noqa: E402


def git_short_sha() -> str:
    return subprocess.check_output(
        ["git", "-C", str(PROJECT_DIR), "rev-parse", "--short=12", "HEAD"],
        text=True,
    ).strip()


def evaluated_path(value: object) -> pathlib.Path:
    return pathlib.Path(env.subst(str(value)))  # type: ignore[name-defined]


def build_web_flasher(source: object, target: object, env: object) -> None:
    del source, target
    build_env = env
    board = build_env.BoardConfig()
    build_dir = pathlib.Path(build_env.subst("$BUILD_DIR"))
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
        manifest_source=PROJECT_DIR / "web-flasher" / "manifest.json",
        output_dir=build_dir / "web-flasher",
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
