#!/usr/bin/env python3
"""Enforce the audited httpd-task stack budget for the NFC-V diagnostic."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

from check_diagnostic_stack_usage import frame_entries, optional_frame, require_frame


def read_constant(source: str, name: str) -> int:
    match = re.search(rf"{name}\s*=\s*(\d+)U", source)
    if match is None:
        raise ValueError(f"{name} is missing")
    return int(match.group(1))


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
    try:
        stack_bytes = read_constant(source, "diagnostic_http_task_stack_bytes")
        safety_bytes = read_constant(
            source, "diagnostic_http_task_stack_safety_bytes"
        )
        response_bytes = read_constant(source, "diagnostic_http_response_capacity")
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1

    required_source_fragments = (
        "config.stack_size = diagnostic_http_task_stack_bytes;",
        "new (std::nothrow) char[diagnostic_http_response_capacity]()",
        "new (std::nothrow) PreviewWorkspace()",
        "httpd api_handler entry",
        "httpd preview_handler entry",
        "httpd before preview_initialization_status",
        "httpd after OpenPrintTag decode",
        "httpd after target generation",
        "httpd after WritePlan generation",
        "httpd preview_handler before response send",
    )
    missing = [fragment for fragment in required_source_fragments if fragment not in source]
    if missing:
        print(f"diagnostic HTTP stack safeguard is missing: {missing[0]}", file=sys.stderr)
        return 1
    if source.count(
        "new (std::nothrow) char[diagnostic_http_response_capacity]()"
    ) < 2:
        print("both diagnostic JSON responses must use bounded heap storage", file=sys.stderr)
        return 1
    if f"std::array<char, {response_bytes}U>" in source:
        print("diagnostic HTTP response buffer returned to the task stack", file=sys.stderr)
        return 1
    if "constexpr bool diagnostic_initialization_write_enabled = false;" not in source:
        print("diagnostic initialization write gate must remain disabled", file=sys.stderr)
        return 1
    if "id=\"initialize\"" in source or "fetch('/api/v1/openprinttag/initialize'" in source:
        print("the one-time initialization control returned to the diagnostic UI", file=sys.stderr)
        return 1

    entries = frame_entries(args.build_dir)
    if not entries:
        print(f"no .su files found under {args.build_dir}", file=sys.stderr)
        return 1
    emitted_write_frames = [
        function
        for _, _, function in entries
        if any(
            fragment in function
            for fragment in (
                "initialize_handler(httpd_req_t*)",
                "run_initialization(",
                "write_block_once(",
            )
        )
    ]
    if emitted_write_frames:
        print(
            f"disabled diagnostic NFC write path was emitted: {emitted_write_frames[0]}",
            file=sys.stderr,
        )
        return 1

    firmware_su = "src/diagnostics/shared_i2c_firmware.cpp.su"
    codec_su = "src/nfc/formats/openprinttag/codec.cpp.su"
    cbor_su = "src/nfc/formats/openprinttag/cbor.cpp.su"
    diagnostic_su = "src/diagnostics/shared_i2c_diagnostic.cpp.su"
    tag_su = "src/nfc/protocols/nfcv/tag.cpp.su"
    try:
        api_handler = require_frame(entries, firmware_su, "api_handler(httpd_req_t*)")
        preview_handler = require_frame(
            entries, firmware_su, "preview_handler(httpd_req_t*)"
        )
        preview_status = optional_frame(
            entries, firmware_su, "preview_initialization_status("
        )
        decode_heap = require_frame(
            entries, firmware_su, "decode_openprinttag_off_stack"
        )
        copy_snapshot = optional_frame(entries, firmware_su, "copy_snapshot(")
        copy_status = optional_frame(
            entries, firmware_su, "copy_initialization_status("
        )
        decode_into = require_frame(
            entries,
            codec_su,
            "Codec::decode(opentag::core::ByteView, opentag::nfc::openprinttag::DecodedTag&)",
        )
        parse_envelope = require_frame(
            entries, codec_su, "parse_envelope(opentag::core::ByteView)"
        )
        decode_material = require_frame(
            entries, codec_su, "decode_material(opentag::core::ByteView"
        )
        build_target = require_frame(
            entries, diagnostic_su, "build_initialization_target("
        )
        build_plan = require_frame(entries, tag_su, "WritePlan::build(")
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

    # The handler frame stays live throughout preview generation. Heap-backed
    # Snapshot, InitializationStatus, response bytes, target bytes, WritePlan
    # blocks, and DecodedTag payloads therefore contribute only their small
    # owner objects to the compiler frames below. The remaining safety budget
    # covers ESP-IDF's pre-handler httpd dispatch frames and dynamic libc use,
    # neither of which is represented in project .su files.
    decode_path = decode_heap + decode_into + max(
        parse_envelope + largest_cbor,
        decode_material + largest_cbor,
    )
    preview_work = preview_status + max(
        copy_snapshot,
        copy_status,
        decode_path,
        build_target,
        build_plan,
    )
    api_path = api_handler + copy_snapshot
    preview_path = preview_handler + preview_work
    worst_path = max(api_path, preview_path)
    remaining = stack_bytes - worst_path

    print(f"diagnostic httpd configured stack: {stack_bytes} bytes")
    print(f"bounded heap response capacity: {response_bytes} bytes")
    print(f"audited API handler path: {api_path} bytes")
    print(f"audited nonblank preview path: {preview_path} bytes")
    print(f"compiler-estimated httpd remaining stack: {remaining} bytes")
    print(f"required httpd safety budget: {safety_bytes} bytes")
    if remaining < safety_bytes:
        print(
            "diagnostic httpd compiler-estimated stack headroom is below budget",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
