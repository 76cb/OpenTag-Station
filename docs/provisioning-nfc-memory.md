# Provisioning memory / production NFC startup

PR #23's production NFC was physically validated, but creating its 16,384-byte
task before Wi-Fi initialization consumed internal RAM needed by the first-run
AP, DHCP and HTTP server. Reported internal free/minimum/largest values fell to
6,028 / 3,316 / 3,060 bytes, with WiFiUdp ENOMEM (12).

## Startup lifecycle

NFC remains enabled. Before startup, API state is `deferred`, reason is
`provisioning`, available is false and last_error is null. Both displays explain
the deferral rather than claiming a hardware failure. No NFC task or RFAL
initialization occurs during unconfigured Wi-Fi, connection attempts, an active
setup AP, or its grace period.

The network owner attempts startup once configuration initialization succeeded
(including the existing safely degraded configuration mode), station settings
are configured, station Wi-Fi is connected, and both AP/provisioning and grace
flags are false. Save and Connect reconfigures in place; the same transition
works without a reboot and on a configured boot. This is the network provisioning
boundary, not completion of every optional setup-wizard step (which includes an
NFC status step). A subsequent disconnect does not kill or recreate an existing
NFC owner. Task creation failure is latched until reboot, logged and exposed in
the NFC API; it is not a fatal application/network startup error.

## Allocation audit

- NFC stack remains 16 KiB, internal, with the existing >=4 KiB compiler margin.
- Exact bundled Arduino-ESP32 is 2.0.17 (espressif32@6.13.0). Board-selected
  `qio_qspi/include/sdkconfig.h` enables `CONFIG_SPIRAM_USE_MALLOC` and sets
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`; external task stacks are not enabled.
  The bundled Xtensa `portmacro.h` defines `portStackMemoryCaps` and
  `portTcbMemoryCaps` as `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`.
- Both full-read images now explicitly prefer PSRAM: 640 bytes total for the
  validated 320-byte tag, bounded at 8,192 bytes total for supported geometry.
  Their RAII buffers are allocated only during NFC reading, checked for failure,
  and released on every exit. RFAL reads still use their existing internal
  transfer buffers before copying into the memory images.
- The shared `IdentifiedTag`, including its inline decoded object, explicitly
  prefers PSRAM and retains shared snapshot lifetime/destruction semantics.
  Small nested codec strings/vectors and the shared-pointer control block keep
  their existing allocator; changing all codec/container semantics is not needed
  for the provisioning fix. CBOR automatic workspaces stay on the audited stack.
- PSRAM exhaustion/unavailability falls back to internal 8-bit heap; image/object
  allocation failure reports an NFC error with RF cleanup. No ISR, DMA, Wire or
  RFAL storage, and no task stack, has been moved to PSRAM.

## Diagnostics and regression coverage

Immediately before task creation, serial reports `internal_free`,
`internal_min`, `internal_largest`, `psram_free` and unchanged `stack_bytes`.
The first three use capability APIs with `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`.
Existing boot milestones and 30-second health/API heap fields now explicitly use
that same internal-8-bit capability mask. Every 30 seconds, serial also reports
all application-task high-water values including NFC and httpd. `/health` task
stack diagnostics include `opentag-nfc`; zero means the task has not started.

Native tests exercise every startup gate, same-boot transition, start-once,
failure latching, deferred API/display semantics and shared-storage lifetime.
Browser coverage verifies a neutral deferred state and hidden read button.
The existing CI production source guard additionally rejects unconditional NFC
startup in setup; existing firmware, native, stack, web, golden-vector and
flasher/bundle checks remain required.

## Read-only physical acceptance (pending bench execution)

1. Boot an unconfigured device. Associate phone/PC, obtain DHCP, load setup,
   repeatedly scan Wi-Fi, and poll the API for at least five minutes. Require
   no ENOMEM/reboots, `enabled=true,state=deferred,reason=provisioning`, no NFC
   startup/RFAL logs and zero NFC task margin (not created). Record internal
   free/minimum/largest every 30 seconds. Target >=16 KiB minimum internal free
   and >=8 KiB largest block throughout this test; investigate below these
   bench targets rather than treating deferral alone as a pass.
2. Save valid Wi-Fi. Require station connection, grace expiry/AP shutdown and
   exactly one NFC task creation/initialization. Repeat on a configured reboot.
   Require RFAL=0, UID `E0:04:01:08:66:27:D8:D4`, checksum `9E639911`, OpenPrintTag
   PASS, stable generation while stationary, bus_errors=0, working touch/scale
   and normal web/API access. Bad credentials must keep NFC deferred while the
   setup AP remains usable; correcting them must recover the transition.
3. Soak for at least five minutes with browser polling, tag removal/reinsertion
   and normal read-only scale/touch use. Record minimum internal heap, lowest
   observed largest block, and every task high-water value. NFC must retain
   >=4 KiB runtime margin. Check no progressive heap loss or networking errors.

No physical test, flash, NFC write, reinitialization or merge is performed by
this change. Wiring, bus clock, RFAL/UID/decode behavior, checksum and read-only
enforcement are unchanged. CI cannot establish physical memory headroom.
