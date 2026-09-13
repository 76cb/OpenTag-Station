#!/usr/bin/env python3
"""Audited NFC task call paths, including adversarial bounded CBOR recursion.

Compiler frames are not a proof of whole-library/runtime stack use. Reserve an
additional 4 KiB for framework, interrupts, logging and unmodelled leaf calls;
bench high-water readings remain required. Missing critical frames fail closed.
"""
import pathlib
import re
import argparse
from check_diagnostic_stack_usage import frame_entries, require_frame

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=pathlib.Path,
                        default=ROOT / ".pio/build/wt32-sc01-plus")
    entries = frame_entries(parser.parse_args().build_dir)
    def frame(file, method):
        return require_frame(entries, "src/" + file + ".cpp.su", method)
    worker = (ROOT / "src/application/nfc_worker.hpp").read_text()
    stack = int(re.search(r"stack_bytes\s*=\s*(\d+)U", worker)[1])
    safety = int(re.search(r"safety_bytes\s*=\s*(\d+)U", worker)[1])
    assert safety >= 4096
    depth = int(re.search(r"maximum_nesting_depth\s*=\s*(\d+)U",
                (ROOT / "src/nfc/formats/openprinttag/cbor.hpp").read_text())[1])
    codec = "nfc/formats/openprinttag/codec"
    cbor = "nfc/formats/openprinttag/cbor"
    owner = frame("application/nfc_worker", "NfcWorker::task_entry(") + frame("application/nfc_worker", "NfcWorker::run_once(")
    polling = frame("nfc/read_only_service", "ReadOnlyService::poll(")
    reading = frame("nfc/read_only_service", "ReadOnlyService::read_tag(")
    decode = frame(codec, "Codec::decode(opentag::core::ByteView, opentag::nfc::openprinttag::DecodedTag&)")
    # Include the rejecting depth+1 frame too (depth begins at zero), map
    # parser, and a worst CBOR leaf frame in addition to recursive skip_item.
    cbor_leaf = max(size for path, size, _ in entries if path.as_posix().endswith(cbor + ".cpp.su"))
    recursive = frame(cbor, "CborMapView::parse(") + (depth + 2) * frame(cbor, "skip_item(") + cbor_leaf
    decoded = owner + polling + reading + decode + max(
        frame(codec, "parse_envelope(opentag::core::ByteView)"),
        frame(codec, "decode_material(opentag::core::ByteView")) + recursive
    # RFAL is iterative; reserve 2 KiB for its transceive/platform chain in
    # addition to project read/confirmation/driver frames and the safety margin.
    transport = owner + polling + reading + frame("nfc/read_only_service", "ReadOnlyService::read_image(") + frame("nfc/read_only_service", "ReadOnlyService::confirm_uid(") + max(
        size for path, size, _ in entries if path.as_posix().endswith("src/hardware/nfc/st25r3916b/i2c_reader.cpp.su")) + 2048
    worst = max(decoded, transport)
    print(f"NFC stack={stack}; nested decode={decoded}; transport={transport}; remaining={stack-worst}; required={safety}")
    assert stack - worst >= safety, "NFC stack headroom below 4 KiB budget"


if __name__ == "__main__":
    main()
