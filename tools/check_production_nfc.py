#!/usr/bin/env python3
"""Fail closed on writable production NFC bindings and competing transports."""
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
        assert calls <= allowed, f"unexpected RFAL calls in {relative}: {calls - allowed}"
        if relative.startswith(("src/application/", "src/ui/", "src/web/",
                                "src/services/", "src/hardware/nfc/")):
            assert not re.search(r"(?:Initializer::generate|Codec::update_consumed_weight|WritePlan::|\.write_blocks\s*\()", text), relative
            assert "Codec::decode" not in text, f"decode must stay on NFC owner: {relative}"
    routes = (ROOT / "src/web/api_router.cpp").read_text()
    assert not re.search(r'"[^"\n]*(?:nfc|openprinttag)[^"\n]*(?:write|initialize|format|lock|password)', routes, re.I)
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
    assert "start_when_configured" not in setup
    assert application.count("nfc_worker_.start_when_configured(") == 1
    assert "network_status.provisioning_grace_active);" in application
    worker = (ROOT / "src/application/nfc_worker.cpp").read_text()
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in worker
    for metric in ("heap_caps_get_free_size", "heap_caps_get_minimum_free_size", "heap_caps_get_largest_free_block"):
        assert metric in worker
    assert "ReadImage first" in service and "make_read_storage<IdentifiedTag>" in service
    diagnostic = (ROOT / "src/diagnostics/shared_i2c_firmware.cpp").read_text()
    assert "diagnostic_initialization_write_enabled = false" in diagnostic
    print("PASS: production NFC read-only bindings, single Wire1 backend, software touch, heap decode")


def check_binary():
    elf = ROOT / ".pio/build/wt32-sc01-plus/firmware.elf"
    core = pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio"))
    candidates = list(core.glob("packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm*"))
    assert elf.is_file() and candidates, "production ELF/toolchain required for binding check"
    symbols = subprocess.check_output([str(candidates[0]), "-C", "--defined-only", str(elf)], text=True)
    forbidden = re.findall(r".*(?:VerifiedWriter::|Initializer::generate|Codec::update_consumed_weight|rfalNfcvPoller\w*(?:Write|Lock|Protect|Password|Privacy|SetAFI|SetDSFID|SetEAS))[^\n]*", symbols)
    assert not forbidden, "writable NFC runtime linked: " + "\n".join(forbidden)
    assert "I2cReader::inventory" in symbols and "ReadOnlyService::read_tag" in symbols
    assert "Esp32RfalPlatform::" not in symbols
    print("PASS: production ELF contains read path and no tag-write/initializer/legacy SPI binding")


if __name__ == "__main__":
    check()
    import sys
    if "--elf" in sys.argv:
        check_binary()
