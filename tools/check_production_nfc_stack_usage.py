#!/usr/bin/env python3
"""Shared backend/NFC task paths, including adversarial bounded CBOR recursion.

Compiler frames are not a proof of whole-library/runtime stack use. Reserve an
additional 4 KiB for framework, interrupts, logging and unmodelled leaf calls;
bench high-water readings remain required. Missing critical frames fail closed.
"""
import pathlib
import re
import argparse
from stack_frames import frame_entries, require_frame

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=pathlib.Path,
                        default=ROOT / ".pio/build/wt32-sc01-plus")
    entries = frame_entries(parser.parse_args().build_dir)
    def frame(file, method):
        return require_frame(entries, "src/" + file + ".cpp.su", method)
    worker = (ROOT / "src/application/backend_worker.hpp").read_text()
    stack = int(re.search(r"stack_bytes\s*=\s*(\d+)U", worker)[1])
    safety = int(re.search(r"safety_bytes\s*=\s*(\d+)U", worker)[1])
    assert safety >= 4096
    depth = int(re.search(r"maximum_nesting_depth\s*=\s*(\d+)U",
                (ROOT / "src/nfc/formats/openprinttag/cbor.hpp").read_text())[1])
    codec = "nfc/formats/openprinttag/codec"
    cbor = "nfc/formats/openprinttag/cbor"
    backend_owner = frame("application/backend_worker", "BackendWorker::task_entry(") + frame("application/backend_worker", "BackendWorker::run(")
    owner = backend_owner + frame("application/backend_worker", "BackendWorker::poll_nfc(") + frame("application/nfc_worker", "NfcWorker::poll(") + frame("application/nfc_worker", "NfcWorker::run_once(")
    polling = frame("nfc/read_only_service", "ReadOnlyService::poll(")
    reading = frame("nfc/read_only_service", "ReadOnlyService::read_tag(")
    decode = frame(codec, "Codec::decode(opentag::core::ByteView, opentag::nfc::openprinttag::DecodedTag&)")
    # Include the rejecting depth+1 frame too (depth begins at zero), map
    # parser, and a worst CBOR leaf frame in addition to recursive skip_item.
    cbor_leaf = max(size for path, size, _ in entries if path.as_posix().endswith(cbor + ".cpp.su"))
    recursive = frame(cbor, "CborMapView::parse(") + (depth + 2) * frame(cbor, "skip_item(") + cbor_leaf
    recursive = max(recursive, frame(codec, "validate_material(") + 512)
    decoded = owner + polling + reading + decode + max(
        frame(codec, "parse_envelope(opentag::core::ByteView)"),
        frame(codec, "decode_material(opentag::core::ByteView")) + recursive
    # RFAL is iterative; reserve 2 KiB for its transceive/platform chain in
    # addition to project read/confirmation/driver frames and the safety margin.
    transport = owner + polling + reading + frame("nfc/read_only_service", "ReadOnlyService::read_image(") + frame("nfc/read_only_service", "ReadOnlyService::confirm_uid(") + max(
        size for path, size, _ in entries if path.as_posix().endswith("src/hardware/nfc/st25r3916b/i2c_reader.cpp.su")) + 2048
    def largest(file):
        sizes = [size for path, size, _ in entries if path.as_posix().endswith("src/" + file + ".cpp.su")]
        assert sizes, f"missing frames: {file}"
        return max(sizes)
    spoolman = "integrations/spoolman/spoolman_adapter"
    filabridge = "integrations/filabridge/filabridge_adapter"
    workflow = "services/station_workflow"
    # Separate paths: command/probe frames are NOT ancestors of NFC. Model
    # bounded adapter/resolver helper depth with the largest file frames and
    # reserve 2 KiB for HTTP/TCP/TLS calls, plus the common 4 KiB safety reserve.
    probe = frame("application/backend_worker", "BackendWorker::probe_backends(") + max(
        frame(spoolman, "SpoolmanAdapter::probe(") + frame(spoolman, "SpoolmanAdapter::probe_read_capabilities(") + 2 * largest(spoolman),
        frame(filabridge, "FilaBridgeAdapter::probe(") + 2 * largest(filabridge))
    identify = frame(workflow, "StationWorkflow::accept_identified_spool(") + 2 * largest("services/spool_identity_resolver") + 2 * largest(spoolman)
    assignment = frame(workflow, "StationWorkflow::assign(") + 2 * largest("services/toolhead_assignment_service") + 2 * largest(filabridge)
    backend = backend_owner + frame("application/backend_worker", "BackendWorker::process(") + max(probe, identify, assignment) + frame("network/http_transport", "HttpTransport::perform(") + 2048
    # Explicit spool confirmation adds a serialized configuration persistence
    # path on this owner. It runs after HTTP has returned, not beneath it.
    persistence = backend_owner + frame("application/backend_worker", "BackendWorker::process(") + frame("services/spool_identity_resolver", "SpoolIdentityResolver::confirm(") + frame("config/configuration_service", "ConfigurationService::confirm_spool_identity_mapping(") + frame("config/configuration_service", "ConfigurationService::persist_locked(") + 2 * largest("config/configuration_service") + 2048
    writer_controller = backend_owner + frame("application/tag_writer_commands", "BackendWorker::process_writer(") + frame("services/tag_writer_service", "TagWriterService::process(")
    writer_path = "nfc/openprinttag_writer"
    writer_prepare = frame("services/tag_writer_service", "TagWriterService::prepare(")
    writer_commit = frame("services/tag_writer_service", "TagWriterService::commit_write(")
    writer_mapping = frame("nfc/formats/openprinttag/spoolman_mapping", "map_spoolman(")
    writer_decode = writer_controller + max(
        writer_prepare + frame(writer_path, "OpenPrintTagWriter::read("),
        writer_prepare + writer_mapping + frame(codec, "Codec::update_consumed_weight(opentag::core::ByteView, double, opentag::nfc::openprinttag::DecodedTag&)"),
        writer_prepare + writer_mapping + frame("nfc/formats/openprinttag/spoolman_mapping", "encode_main("),
        writer_commit + frame(writer_path, "OpenPrintTagWriter::execute(")) + decode + max(
        frame(codec, "parse_envelope(opentag::core::ByteView)"), frame(codec, "decode_material(opentag::core::ByteView")) + recursive
    writer_transport = writer_controller + max(writer_prepare + frame(writer_path, "OpenPrintTagWriter::read("),
        writer_commit + frame(writer_path, "OpenPrintTagWriter::execute(")) + frame(writer_path, "OpenPrintTagWriter::full_read(") + frame(writer_path, "OpenPrintTagWriter::fence(") + max(
        largest("hardware/nfc/st25r3916b/i2c_reader"), largest("hardware/nfc/st25r3916b/openprinttag_write_binding")) + 2048
    # UID ownership queries now have an explicit helper chain beneath preview
    # or association/previous-owner cleanup. Include every ancestor instead of
    # assuming the existing two-largest-frame allowance covers that depth.
    writer_uid_http = max(
        writer_prepare + frame("services/tag_writer_service", "TagWriterService::prepare_uid_owner("),
        writer_commit + frame("services/tag_writer_service", "TagWriterService::associate(") + frame("services/tag_writer_service", "TagWriterService::clear_previous_uid(")) + frame("services/tag_writer_service", "TagWriterService::uid_owner(") + frame("services/tag_writer_service", "TagWriterService::api(")
    writer_edit_http = frame("services/tag_writer_service", "TagWriterService::edit_and_report(") + frame("services/tag_writer_service", "TagWriterService::edit_record(") + frame("services/tag_writer_service", "TagWriterService::api(")
    writer_http = writer_controller + max(2 * largest("services/tag_writer_service"), writer_uid_http, writer_edit_http) + frame(spoolman, "SpoolmanAdapter::request(") + frame("network/http_transport", "HttpTransport::perform(") + 2048
    writer_storage = writer_controller + max(writer_prepare, writer_commit) + largest("platform/storage/writer_journal") + 2048
    service = "services/tag_writer_service"
    clear_prepare = frame(service, "TagWriterService::prepare_clear(")
    clear_commit = frame(service, "TagWriterService::commit_clear(")
    clear_unlink = frame(service, "TagWriterService::unlink(")
    clear_decode = writer_controller + clear_prepare + max(frame(writer_path, "OpenPrintTagWriter::read("), frame(writer_path, "OpenPrintTagWriter::plan_clear(")) + decode + max(
        frame(codec, "parse_envelope(opentag::core::ByteView)"), frame(codec, "decode_material(opentag::core::ByteView")) + recursive
    clear_transport = writer_controller + max(clear_prepare + frame(writer_path, "OpenPrintTagWriter::read("), clear_commit + frame(writer_path, "OpenPrintTagWriter::execute(")) + frame(writer_path, "OpenPrintTagWriter::full_read(") + frame(writer_path, "OpenPrintTagWriter::fence(") + max(largest("hardware/nfc/st25r3916b/i2c_reader"), largest("hardware/nfc/st25r3916b/openprinttag_write_binding")) + 2048
    clear_http = writer_controller + clear_commit + clear_unlink + max(frame(service, "TagWriterService::uid_owner("), frame(service, "TagWriterService::unique_identity(")) + frame(service, "TagWriterService::api(") + frame(spoolman, "SpoolmanAdapter::request(") + frame("network/http_transport", "HttpTransport::perform(") + 2048
    clear_storage = writer_controller + clear_commit + clear_unlink + max(
        frame(service, "TagWriterService::persist_clear(") + 2 * largest("platform/storage/writer_journal"),
        frame("config/configuration_service", "ConfigurationService::clear_verified_spool_identity_mapping(") + frame("config/configuration_service", "ConfigurationService::persist_locked(") + 2 * largest("config/configuration_service")) + 2048
    association_storage = writer_controller + writer_commit + frame(service, "TagWriterService::associate(") + frame("config/configuration_service", "ConfigurationService::sync_verified_spool_identity_mapping(") + frame("config/configuration_service", "ConfigurationService::persist_locked(") + 2 * largest("config/configuration_service") + 2048
    restore_storage = writer_controller + frame("application/tag_writer_commands", "BackendWorker::ensure_writer(") + frame(service, "TagWriterService::restore_ready(") + frame(service, "TagWriterService::restore_cleanup(") + 2 * largest("platform/storage/writer_journal") + 2048
    weigh_owner = backend_owner + frame("application/weigh_commands", "BackendWorker::auto_weight_update(") + frame("application/weigh_commands", "BackendWorker::process_weight_update(") + frame("services/weigh_sync", "WeighSync::update(")
    weigh_http = weigh_owner + frame(spoolman, "SpoolmanAdapter::set_remaining_weight(") + frame(spoolman, "SpoolmanAdapter::get_spool(") + frame(spoolman, "SpoolmanAdapter::request(") + frame("network/http_transport", "HttpTransport::perform(") + 2048
    weigh_inventory = weigh_owner + frame(spoolman, "SpoolmanAdapter::set_remaining_weight(") + frame("application/weigh_commands", "BackendWorker::weight_fence(") + largest("hardware/nfc/st25r3916b/i2c_reader") + 2048
    worst = max(decoded, transport, backend, persistence, writer_decode, writer_transport, writer_http, writer_storage, clear_decode, clear_transport, clear_http, clear_storage, association_storage, restore_storage, weigh_http, weigh_inventory)
    print(f"Clear decode={clear_decode}; transport={clear_transport}; HTTP={clear_http}; persistence={clear_storage}; association persistence={association_storage}; restart={restore_storage}; weigh HTTP={weigh_http}; inventory={weigh_inventory}")
    print(f"Approved writer decode={writer_decode}; transport={writer_transport}; HTTP={writer_http}")
    print(f"Shared backend stack={stack}; nested NFC decode={decoded}; NFC transport={transport}; backend HTTP={backend}; confirmation persistence={persistence}; remaining={stack-worst}; required={safety}")
    assert stack - worst >= safety, "Shared backend/NFC stack headroom below 4 KiB budget"


if __name__ == "__main__":
    main()
