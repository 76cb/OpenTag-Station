#!/usr/bin/env python3
"""Enforce the audited loopTask stack budget for the NFC-V diagnostic."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

from analyze_stack_usage import parse_line


def frame_entries(build_dir: pathlib.Path) -> list[tuple[pathlib.Path, int, str]]:
    entries: list[tuple[pathlib.Path, int, str]] = []
    for path in sorted(build_dir.rglob("*.su")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            parsed = parse_line(line)
            if parsed is not None:
                size, function, _ = parsed
                entries.append((path, size, function))
    return entries


def require_frame(
    entries: list[tuple[pathlib.Path, int, str]],
    source_suffix: str,
    function_fragment: str,
) -> int:
    matches = [
        size
        for path, size, function in entries
        if path.as_posix().endswith(source_suffix)
        and function_fragment in function
        and "::<lambda" not in function
    ]
    if len(matches) != 1:
        raise ValueError(
            f"expected one {function_fragment!r} frame in {source_suffix}, found {len(matches)}"
        )
    return matches[0]


def optional_frame(
    entries: list[tuple[pathlib.Path, int, str]],
    source_suffix: str,
    function_fragment: str,
) -> int:
    matches = [
        size
        for path, size, function in entries
        if path.as_posix().endswith(source_suffix)
        and function_fragment in function
        and "::<lambda" not in function
    ]
    if len(matches) > 1:
        raise ValueError(
            f"expected at most one {function_fragment!r} frame in {source_suffix}, found {len(matches)}"
        )
    return matches[0] if matches else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-dir",
        type=pathlib.Path,
        default=pathlib.Path(".pio/build/wt32-sc01-plus-i2c-test"),
    )
    parser.add_argument(
        "--source",
        type=pathlib.Path,
        default=pathlib.Path("src/diagnostics/shared_i2c_firmware.cpp"),
    )
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    stack_match = re.search(
        r"diagnostic_loop_task_stack_bytes\s*=\s*(\d+)U", source
    )
    safety_match = re.search(
        r"diagnostic_loop_task_stack_safety_bytes\s*=\s*(\d+)U", source
    )
    if stack_match is None or safety_match is None:
        print("diagnostic loop stack or safety budget constant is missing", file=sys.stderr)
        return 1
    stack_bytes = int(stack_match.group(1))
    safety_bytes = int(safety_match.group(1))
    if "SET_LOOP_TASK_STACK_SIZE(diagnostic_loop_task_stack_bytes);" not in source:
        print("Arduino loopTask stack override is missing", file=sys.stderr)
        return 1
    if source.count("Codec::decode(") != 1 or "Codec::decode(image, *decoded)" not in source:
        print(
            "diagnostic must use exactly one heap-backed output-parameter Codec::decode call",
            file=sys.stderr,
        )
        return 1

    entries = frame_entries(args.build_dir)
    if not entries:
        print(f"no .su files found under {args.build_dir}", file=sys.stderr)
        return 1

    firmware_su = "src/diagnostics/shared_i2c_firmware.cpp.su"
    codec_su = "src/nfc/formats/openprinttag/codec.cpp.su"
    cbor_su = "src/nfc/formats/openprinttag/cbor.cpp.su"
    try:
        setup = require_frame(entries, firmware_su, "void setup()")
        loop = require_frame(entries, firmware_su, "void loop()")
        prepare = require_frame(entries, firmware_su, "prepare_initialization_image()")
        decode_heap = require_frame(entries, firmware_su, "decode_openprinttag_off_stack")
        class_setup = optional_frame(entries, firmware_su, "DualI2cFirmware::setup()")
        run_initialization = optional_frame(entries, firmware_su, "run_initialization(")
        decode_into = require_frame(
            entries, codec_su, "Codec::decode(opentag::core::ByteView, opentag::nfc::openprinttag::DecodedTag&)"
        )
        parse_envelope = require_frame(entries, codec_su, "parse_envelope(opentag::core::ByteView)")
        decode_material = require_frame(entries, codec_su, "decode_material(opentag::core::ByteView")
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1

    cbor_frames = [
        size for path, size, _ in entries if path.as_posix().endswith(cbor_su)
    ]
    if not cbor_frames:
        print("no OpenPrintTag CBOR stack frames found", file=sys.stderr)
        return 1
    largest_cbor = max(cbor_frames)

    # These are compiler-reported static frames for the two known loopTask
    # decode paths. Callees that GCC inlines are already included in their
    # caller's frame. The CBOR term covers the largest one-level parser/reader
    # callee used by the fixed empty main/auxiliary initialization image.
    decode_path = decode_heap + decode_into + max(
        parse_envelope + largest_cbor,
        decode_material + largest_cbor,
    )
    boot_path = setup + class_setup + prepare + decode_path
    transaction_path = loop + run_initialization + decode_path
    worst_path = max(boot_path, transaction_path)
    remaining = stack_bytes - worst_path

    print(f"diagnostic loopTask configured stack: {stack_bytes} bytes")
    print(f"audited boot decode path: {boot_path} bytes")
    print(f"audited post-write decode path: {transaction_path} bytes")
    print(f"compiler-estimated remaining stack: {remaining} bytes")
    print(f"required safety budget: {safety_bytes} bytes")
    if remaining < safety_bytes:
        print(
            "diagnostic loopTask compiler-estimated stack headroom is below budget",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
