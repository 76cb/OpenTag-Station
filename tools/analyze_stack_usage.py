#!/usr/bin/env python3
"""Summarize GCC -fstack-usage output without claiming full call-chain bounds."""

from __future__ import annotations

import argparse
import pathlib
import sys


def parse_line(line: str) -> tuple[int, str, str] | None:
    fields = line.rstrip().split("\t")
    if len(fields) < 3:
        return None
    try:
        size = int(fields[-2])
    except ValueError:
        return None
    return size, fields[0], fields[-1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "build_dir",
        nargs="?",
        type=pathlib.Path,
        default=pathlib.Path(".pio/build/wt32-sc01-plus"),
    )
    parser.add_argument("--limit", type=int, default=25)
    args = parser.parse_args()

    if args.limit <= 0:
        parser.error("--limit must be positive")
    files = sorted(args.build_dir.rglob("*.su"))
    if not files:
        print(
            f"no .su files found under {args.build_dir}; build with -fstack-usage",
            file=sys.stderr,
        )
        return 1

    project_root = args.build_dir / "src"
    entries: list[tuple[int, str, str, bool]] = []
    dynamic: list[tuple[int, str, str, bool]] = []
    for path in files:
        is_project = path.is_relative_to(project_root)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            parsed = parse_line(line)
            if parsed is None:
                continue
            entry = (*parsed, is_project)
            entries.append(entry)
            if "dynamic" in parsed[2]:
                dynamic.append(entry)

    if not entries:
        print("stack-usage files contained no parseable entries", file=sys.stderr)
        return 1
    project_entries = sorted(
        (entry for entry in entries if entry[3]), reverse=True
    )
    dependency_entries = sorted(
        (entry for entry in entries if not entry[3]), reverse=True
    )
    project_dynamic = [entry for entry in dynamic if entry[3]]
    print(f"parsed {len(entries)} stack frames from {len(files)} files")
    print("largest project frames (bytes; not cumulative call-chain use):")
    for size, function, qualifier, _ in project_entries[: args.limit]:
        print(f"{size:6d}  {qualifier:20s}  {function}")
    print("largest dependency/framework frames:")
    for size, function, qualifier, _ in dependency_entries[: min(args.limit, 10)]:
        print(f"{size:6d}  {qualifier:20s}  {function}")
    if project_dynamic:
        largest_dynamic = max(entry[0] for entry in project_dynamic)
        print(
            f"WARNING: {len(project_dynamic)} project and {len(dynamic)} total "
            f"frame(s) report dynamic stack use; largest project estimate "
            f"is {largest_dynamic} bytes"
        )
    else:
        print("no project compiler-reported dynamic stack frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
