#!/usr/bin/env python3
"""Fail closed on writes outside the approved OpenPrintTag service boundary."""
import pathlib
import re
import os
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def check():
    ini = (ROOT / "platformio.ini").read_text()
    production = ini.split("[env:wt32-sc01-plus]", 1)[1].split("[env:", 1)[0]
    assert "-DOPENTAG_ENABLE_ST25R3916B=1" in production
    for obsolete in ("platform/rfal/esp32_rfal_platform.cpp",
                     "hardware/nfc/st25r3916b/frontend_backend.cpp",
                     "hardware/nfc/st25r3916b/service.cpp"):
        assert f"-<{obsolete}>" in production
    allowed = {
        "rfalNfcInitialize", "rfalNfcvPollerInitialize", "rfalFieldOff",
        "rfalFieldOnAndStartGT", "rfalNfcvPollerCollisionResolution",
        "rfalNfcvPollerGetSystemInformation", "rfalNfcvPollerExtendedGetSystemInformation",
        "rfalNfcvPollerReadSingleBlock", "rfalNfcvPollerExtendedReadSingleBlock",
        "rfalNfcvPollerReadMultipleBlocks", "rfalNfcvPollerExtendedReadMultipleBlocks",
    }
    excluded = {"src/diagnostics", "src/platform/rfal"}
    for source in (ROOT / "src").rglob("*"):
        if source.suffix not in (".cpp", ".hpp"):
            continue
        relative = source.relative_to(ROOT).as_posix()
        if any(relative.startswith(p + "/") for p in excluded):
            continue
        if relative.endswith(("frontend_backend.cpp", "service.cpp")) and "/st25r3916b/" in relative:
            continue
        text = source.read_text(encoding="utf-8")
        calls = set(re.findall(r"(?:\.|->)\s*(rfal\w+)\s*\(", text))
        writer_binding = relative == "src/hardware/nfc/st25r3916b/openprinttag_write_binding.cpp"
        permitted = allowed | ({"rfalNfcvPollerWriteSingleBlock"} if writer_binding else set())
        assert calls <= permitted, f"unexpected RFAL calls in {relative}: {calls - permitted}"
        if "commit_openprinttag_block(" in text:
            assert relative in {"src/nfc/openprinttag_writer.hpp", "src/nfc/openprinttag_writer.cpp",
                                "src/hardware/nfc/st25r3916b/i2c_reader.hpp",
                                "src/hardware/nfc/st25r3916b/openprinttag_write_binding.cpp"}, relative
        if "writer_.execute(" in text:
            assert relative == "src/services/tag_writer_service.cpp", relative
        if "writer_->process(" in text:
            assert relative == "src/application/tag_writer_commands.cpp", relative
        if re.search(r"\bOpenPrintTagWriter\b", text):
            assert relative in {"src/nfc/openprinttag_writer.hpp", "src/nfc/openprinttag_writer.cpp",
                                "src/services/tag_writer_service.hpp"}, relative
        if re.search(r"\bTagWriterService\b", text):
            assert relative in {"src/services/tag_writer_service.hpp", "src/services/tag_writer_service.cpp",
                                "src/application/backend_worker.hpp", "src/application/tag_writer_commands.cpp",
                                "src/integrations/spoolman/spoolman_adapter.hpp"}, relative
        if relative.startswith(("src/application/", "src/ui/", "src/web/",
                                "src/services/", "src/hardware/nfc/")):
            assert not re.search(r"(?:Initializer::generate|Codec::update_consumed_weight|WritePlan::|\.write_blocks\s*\()", text), relative
            assert "Codec::decode" not in text, f"decode must stay on NFC owner: {relative}"
    routes = (ROOT / "src/web/api_router.cpp").read_text()
    assert '"/api/v1/tag-writer"' in routes
    assert '"uid", "generation", "target_checksum"' in routes
    assert "confirm_openprinttag_block" not in routes
    driver = (ROOT / "src/hardware/nfc/st25r3916b/i2c_reader.hpp").read_text()
    assert "&Wire1" in driver and "::nfc_interrupt" in driver
    board = (ROOT / "src/boards/wt32_sc01_plus_rev_a.hpp").read_text()
    assert re.search(r"touch_i2c_port\s*=\s*-1", board)
    service = (ROOT / "src/nfc/read_only_service.cpp").read_text()
    assert service.count("Codec::decode(") == 1
    assert "tag->decoded" in service
    application = (ROOT / "src/application/application.cpp").read_text()
    setup = application.split("void Application::setup()", 1)[1].split("void Application::record_task_stack_margins", 1)[0]
    assert "nfc_worker_.start(" not in application
    assert "enable_when_configured" not in setup
    assert application.count("nfc_worker_.enable_when_configured(") == 1
    assert "backend_worker_.start(nfc_worker_)" in setup
    assert "network_status.provisioning_grace_active);" in application
    worker = (ROOT / "src/application/nfc_worker.cpp").read_text()
    assert "xTaskCreate" not in worker and "task_entry" not in worker
    backend = (ROOT / "src/application/backend_worker.cpp").read_text()
    assert backend.count("xTaskCreatePinnedToCore(") == 1
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in backend
    for metric in ("heap_caps_get_free_size", "heap_caps_get_minimum_free_size", "heap_caps_get_largest_free_block"):
        assert metric in backend
    # NFC must never nest under HTTP/command/workflow frames. Only backend run
    # boundaries call the cooperative owner, including continuously busy queues.
    run = backend.split("void BackendWorker::run()", 1)[1]
    assert run.count("poll_nfc();") == 3
    assert "poll_nfc();" not in backend.split("void BackendWorker::run()", 1)[0]
    assert "transport_.begin_operation(millis())" in run
    assert "nfc_->poll();" in run
    header = (ROOT / "src/application/nfc_worker.hpp").read_text()
    assert "TaskHandle_t" not in header and "stack_bytes" not in header
    transport = (ROOT / "src/network/http_transport.cpp").read_text()
    assert "DeadlineClient<WiFiClient" in transport
    assert "ReadImage first" in service and "make_read_storage<IdentifiedTag>" in service
    diagnostic = (ROOT / "src/diagnostics/shared_i2c_firmware.cpp").read_text()
    assert "diagnostic_initialization_write_enabled = false" in diagnostic
    writer = (ROOT / "src/nfc/openprinttag_writer.cpp").read_text()
    service_writer = (ROOT / "src/services/tag_writer_service.cpp").read_text()
    boundary = (ROOT / "src/nfc/openprinttag_writer.hpp").read_text()
    assert "private:\n  friend class OpenPrintTagWriter;" in boundary
    assert "p.scratch != p.target" in writer and "p.security[block]" in writer
    assert "p.verified = true" in writer and re.search(r"if\s*\(!plan_\s*\|\|\s*!plan_->verified\)", service_writer)
    assert "commit_openprinttag_block" not in service
    assert "CommandType::writer) process_writer(*command)" in run
    assert "process_writer(" not in backend.split("void BackendWorker::run()", 1)[0]
    print("PASS: approved OpenPrintTag writer, private destructive binding, sole Wire1 backend owner")


def check_binary():
    elf = ROOT / ".pio/build/wt32-sc01-plus/firmware.elf"
    core = pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio"))
    candidates = list(core.glob("packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm*"))
    assert elf.is_file() and candidates, "production ELF/toolchain required for binding check"
    symbols = subprocess.check_output([str(candidates[0]), "-C", "--defined-only", str(elf)], text=True)
    forbidden = re.findall(r".*(?:VerifiedWriter::|rfalNfcvPoller\w*(?:Lock|Protect|Password|Privacy|SetAFI|SetDSFID|SetEAS|WriteMultiple|ExtendedWrite))[^\n]*", symbols)
    assert not forbidden, "unapproved destructive runtime linked: " + "\n".join(forbidden)
    assert "OpenPrintTagWriter::execute" in symbols and "I2cReader::commit_openprinttag_block" in symbols
    # Xtensa longcalls load literal addresses then callx; inspect relocation
    # dependencies in source objects, plus the linked primitive allowlist above.
    write_callers = []
    for obj in (elf.parent / "src").rglob("*.o"):
        undefined = subprocess.check_output([str(candidates[0]), "-C", "-u", str(obj)], text=True)
        if "rfalNfcvPollerWriteSingleBlock(" in undefined:
            write_callers.append(obj.relative_to(elf.parent).as_posix())
    assert write_callers == ["src/hardware/nfc/st25r3916b/openprinttag_write_binding.cpp.o"], write_callers
    assert "I2cReader::inventory" in symbols and "ReadOnlyService::read_tag" in symbols
    assert "Esp32RfalPlatform::" not in symbols
    assert "NfcWorker::task_entry" not in symbols and "NfcWorker::start()" not in symbols
    print("PASS: production ELF write primitive has only approved OpenPrintTag binding callers; no lock/raw/legacy transport")


if __name__ == "__main__":
    check()
    import sys
    if "--elf" in sys.argv:
        check_binary()
