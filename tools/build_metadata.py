"""Inject deterministic firmware metadata into PlatformIO builds.

The build date is the Git commit date, not wall-clock time. This keeps rebuilds
of the same commit stable. A release builder may override it with
SOURCE_DATE_EPOCH.
"""

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.

import datetime
import os
import pathlib
import subprocess


PROJECT_DIR = pathlib.Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]


def read_version() -> str:
    return (PROJECT_DIR / "VERSION").read_text(encoding="utf-8").strip()


def git_value(*args: str, fallback: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(PROJECT_DIR), *args],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return fallback


def build_date() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch:
        instant = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        return instant.isoformat().replace("+00:00", "Z")
    return git_value("log", "-1", "--format=%cI", fallback="uncommitted")


def quoted(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


env.Append(  # type: ignore[name-defined]
    CPPDEFINES=[
        ("OPENTAG_PROJECT_VERSION", quoted(read_version())),
        ("OPENTAG_GIT_SHA", quoted(git_value("rev-parse", "--short=12", "HEAD", fallback="uncommitted"))),
        ("OPENTAG_BUILD_DATE", quoted(build_date())),
        ("OPENTAG_ESP32_PLATFORM", quoted("espressif32@6.13.0")),
        ("OPENTAG_ARDUINO_FRAMEWORK", quoted("2.0.17")),
        ("OPENTAG_RFAL_REVISION", quoted("not-vendored")),
        ("OPENTAG_OPENPRINTTAG_REVISION", quoted("e0dab1ae16838d2c342e7cfc509455441b7d8eba")),
    ]
)
