# Browser provisioning and local management

OpenTag Station uses the existing embedded administration UI for both initial
provisioning and normal local management. There is no second setup application
and no cloud dependency.

## Setup access point

The network owner starts a temporary access point when:

- no Wi-Fi SSID is configured;
- three consecutive saved-network connection attempts fail; or
- a local administrator selects **Start setup access point** under the current
  conditional-authentication policy.

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
6. Optionally enter a 16-128 character **Local API access token (optional)**.
   During initial setup, when no token exists, leaving it blank keeps local API
   authentication disabled. On a station that already has a token, a blank
   setup field preserves that token; recovery provisioning cannot replace or
   clear it. Use Configuration's explicit clear control after authenticating.
   A configured token is write-only and is never returned by the station.
7. Select **Save and connect**.

The HTTP receipt is delivered before the configuration worker can persist the
revision-checked change or ask the network owner to apply it. The AP remains
available throughout the association attempt. The page polls live network
state and reports the selected SSID, assigned IP, hostname, mDNS URL, and
failures without showing passwords or tokens.

After a successful join, the AP remains for a 30-second grace period, then
shuts down. A failed join leaves the AP active so credentials can be corrected.

## Normal local management

After provisioning, open either:

- `http://<assigned-ip>/`; or
- `http://<hostname>.local/` when mDNS is supported by the client network.

This is the same full administration UI. It retains scale raw/filtered counts,
grams, stability, profile/capacity, calibration coefficients, tare, reference
mass calibration, persisted result reporting, backend setup, diagnostics,
device controls, and A/B OTA under the same conditional-authentication policy.

When a local API token is configured, normal mutations require that exact
bearer token. When the token is blank, local mutations are allowed without an
`Authorization` header. Wi-Fi scanning and reconfiguration are available in
the Configuration section; starting setup mode follows the same conditional
authentication policy.

The touchscreen and browser report this state directly. A blank token means
**Local API authentication: DISABLED** and **Local browser control: ENABLED**.
This trusted-LAN mode is complete and healthy: it does not degrade health, block
setup completion, disable configuration/scale/backend mutations, or require the
operator to create a credential. Setting a token reports **Local API
authentication: ENABLED**; local browser control remains available but protected
mutations require the token. Clearing it deliberately returns to trusted-LAN mode.

## On-device guidance and serial diagnostics

When Wi-Fi is unconfigured, the touchscreen shows **SETUP REQUIRED**, the AP
SSID, and `http://192.168.4.1/`; touchscreen text entry is not required.
Connected workflow and diagnostic screens show the assigned IP.

Serial diagnostics report bounded boot/stable stack margins, setup-AP and grace
state, scan request/start/completion or actual failure code, connect receipt,
configuration persistence, STA association state, SSID, and IP. Passwords, API
tokens, authorization values, and backend credentials are never printed.

In the pinned Arduino-ESP32 Wi-Fi wrapper, `WIFI_SCAN_FAILED` is `-2`. It is a
generic wrapper result, not a count and not proof of one specific radio fault.
Diagnostics therefore include whether `-2` occurred while starting a scan or
after an asynchronous scan had started, plus elapsed time and a bounded reason.
Scan requests remain pending while association or another radio-mode transition
owns the radio; setup-AP changes, reconfiguration, reconnect, and scan cleanup
are serialized rather than raced.

## Physical validation still required

Host tests validate policy transitions, millisecond wrap safety, scan
normalization, scoped provisioning authorization, request bounds, and response
redaction. The firmware build validates the ESP32-S3 integration boundary.
Actual AP radio behavior, captive-portal behavior across operating systems,
association/DHCP/mDNS, grace-period shutdown, and reconnect behavior remain
unverified until exercised on the WT32-SC01 Plus.
