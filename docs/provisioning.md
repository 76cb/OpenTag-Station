# Browser provisioning and local management

OpenTag Station uses the existing embedded administration UI for both initial
provisioning and normal local management. There is no second setup application
and no cloud dependency.

## Setup access point

The network owner starts a temporary access point when:

- no Wi-Fi SSID is configured;
- three consecutive saved-network connection attempts fail; or
- an authenticated administrator selects **Start setup access point**.

The SSID is `OpenTag-Setup-XXXX`, where `XXXX` is derived from a stable,
non-secret portion of the ESP32-S3 hardware identifier. The AP uses
`192.168.4.1/24` and the station is always reachable at
`http://192.168.4.1/`. AP+STA mode keeps the setup UI reachable while the
station attempts to join the selected permanent network.

Captive DNS resolves names to `192.168.4.1` while setup is active. Known
platform probes and unknown browser GET paths redirect to the root setup page.
Captive-portal detection varies by operating system, so direct navigation to
`http://192.168.4.1/` remains the supported fallback.

The setup AP is intentionally local and temporary, but it is not encrypted.
Nearby AP clients can view public diagnostics and submit only the scoped
network scan/connect setup operations. They cannot use provisioning authority
for scale, backend, device-control, or OTA mutations. On a recovery AP, an
existing local API token cannot be replaced through the provisioning route.
Use setup mode only in a physically controlled location.

## Browser workflow

1. Connect a phone or computer to `OpenTag-Setup-XXXX`.
2. Open `http://192.168.4.1/` if a captive page does not open automatically.
3. Select **Scan for networks**. Results are asynchronous, deduplicated by
   SSID, strongest-first, and show RSSI and whether the network is secured.
4. Select a result or enter an SSID manually.
5. Enter the Wi-Fi password and hostname.
6. On a new device, create a 16-128 character local API access token. It is
   write-only and is never returned by the station.
7. Select **Save and connect**.

Configuration is committed by the existing revision-checked configuration
worker before the network owner applies it. The AP remains available throughout
the attempt. The page polls live network state and reports the selected SSID,
assigned IP, hostname, mDNS URL, and failures without showing passwords or
tokens.

After a successful join, the AP remains for a 30-second grace period, then
shuts down. A failed join leaves the AP active so credentials can be corrected.

## Normal local management

After provisioning, open either:

- `http://<assigned-ip>/`; or
- `http://<hostname>.local/` when mDNS is supported by the client network.

This is the same full administration UI. It retains scale raw/filtered counts,
grams, stability, profile/capacity, calibration coefficients, tare, reference
mass calibration, persisted result reporting, backend setup, diagnostics,
device controls, and authenticated A/B OTA.

Normal mutations require the current bearer token. Wi-Fi scanning and
reconfiguration are available in the Configuration section. Starting the setup
AP deliberately also requires the bearer token.

## On-device guidance and serial diagnostics

When Wi-Fi is unconfigured, the touchscreen shows **SETUP REQUIRED**, the AP
SSID, and `http://192.168.4.1/`; touchscreen text entry is not required.
Connected workflow and diagnostic screens show the assigned IP.

Serial diagnostics report only state transitions, SSID, IP, AP state, setup
reason, bounded failure count, and redacted error text. Passwords, API tokens,
and backend credentials are never printed.

## Physical validation still required

Host tests validate policy transitions, millisecond wrap safety, scan
normalization, scoped provisioning authorization, request bounds, and response
redaction. The firmware build validates the ESP32-S3 integration boundary.
Actual AP radio behavior, captive-portal behavior across operating systems,
association/DHCP/mDNS, grace-period shutdown, and reconnect behavior remain
unverified until exercised on the WT32-SC01 Plus.
