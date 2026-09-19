#!/usr/bin/env python3
"""Guard the linked internal-RAM saving, independently of source macros."""
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    core = pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio"))
    toolchain = core / "packages/toolchain-xtensa-esp32s3/bin"
    elf = ROOT / ".pio/build/wt32-sc01-plus/firmware.elf"
    nm = next(toolchain.glob("xtensa-esp32s3-elf-nm*"))
    size = next(toolchain.glob("xtensa-esp32s3-elf-size*"))
    symbols = subprocess.check_output([str(nm), "-C", "--defined-only", str(elf)], text=True)
    from product_features import community_enabled
    if not community_enabled():
        for symbol in ("CommunityCatalog::search(", "CommunityCatalog::detail(",
                       "CommunityCatalog::verify(", "CommunityCatalog::status(",
                       "CommunityCatalogUpdater::update("):
            assert symbol not in symbols, f"Disabled Community reachable in production ELF: {symbol}"
        print("Production ELF excludes Community search/detail/status/verify/update")
    assert "work_mem_int" not in symbols, "LVGL 64 KiB internal widget pool has returned"
    assert "opentag_lvgl_pool" in symbols, "PSRAM widget-pool provider missing"
    sections = subprocess.check_output([str(size), "-A", str(elf)], text=True)
    wanted = {".dram0.data", ".dram0.bss", ".noinit"}
    sizes = {parts[0]: int(parts[1]) for line in sections.splitlines()
             if (parts := line.split()) and parts[0] in wanted}
    assert ".dram0.bss" in sizes and ".dram0.data" in sizes
    total = sum(sizes.values())
    # PR #26 used 175428 bytes. Reserve at least 54 KiB of that recovered
    # headroom, allowing small future counters without silently spending it.
    assert total <= 120000, f"static internal RAM budget exceeded: {total} > 120000"
    print(f"PASS: static internal RAM={total}, PR26 saving={175428-total}; LVGL pool external")


if __name__ == "__main__":
    main()
