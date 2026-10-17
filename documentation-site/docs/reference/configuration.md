# Configuration keys

Every setting the station stores, with its default and accepted range. Read them with
`GET /api/v1/config` and change them with `PATCH /api/v1/config` ([REST API](api.md)), or use the
Settings screens.

## Storage

| Item | Value |
|---|---|
| File | `/configuration.json` on the `littlefs` partition, at most 16,384 bytes |
| Save sequence | Written to `/configuration.new`, read back, then renamed over `/configuration.json`. The previous file becomes `/configuration.bak`. |
| Unreadable main file | The backup is loaded instead. |
| Schema version | 3 (`schema_version`). Files with schema 1 or 2 are upgraded when loaded. |
| Hardware ID | `wt32-sc01-plus-rev-a` (`hardware_id`); a file with another ID is rejected. |
| Newer schema | A file written by newer firmware is left untouched and every save is refused until the firmware is updated or the station is factory-reset. |
| Revision | A counter in memory that increases with every save and restarts at each boot. Not stored. |

A firmware update keeps the file. A factory reset deletes it, its backup and the scale
calibration.

## Keys

"Never returned" means `GET /config` reports only whether the value is set.

### `device`

| Key | Default | Accepted |
|---|---|---|
| `hostname` | `opentag-station` | 1–63 characters of `a–z 0–9 -`, not starting or ending with `-` |
| `brightness_percent` | `80` | 5–100 |
| `dim_after_ms` | `120000` | 1–86,400,000 |
| `sleep_after_ms` | `300000` | `dim_after_ms`–86,400,000 |
| `update_channel` | `stable` | `stable`, `beta` or `development`. Stored only; it changes nothing. |

### `wifi`

| Key | Default | Accepted |
|---|---|---|
| `ssid` | empty | ≤ 32 bytes |
| `password` | empty | ≤ 64 bytes. Never returned (`password_configured`). |
| `auto_reconnect` | `true` | boolean |
| `connect_timeout_ms` | `15000` | 1,000–60,000 |
| `reconnect_initial_ms` | `1000` | 500–60,000 |
| `reconnect_max_ms` | `60000` | `reconnect_initial_ms`–600,000 |

### `web`

| Key | Default | Accepted |
|---|---|---|
| `access_token` | empty (no authentication) | Empty, or 16–128 characters of `A–Z a–z 0–9 - . _ ~`. Never returned (`access_token_configured`). |

### `spoolman`

| Key | Default | Accepted |
|---|---|---|
| `url` | empty | `http://` or `https://`, ≤ 256 bytes, no `@`, `#` or spaces |
| `authentication_token` | empty | ≤ 512 bytes, no control characters. Never returned (`authentication_token_configured`). |
| `identity_field` | `opentag_instance_uuid` | 1–64 characters of `a–z 0–9 _`; must differ from `nfc_uid_field` |
| `nfc_uid_field` | `nfc_uid` | 1–64 characters of `a–z 0–9 _` |
| `ca_certificate_pem` | empty | ≤ 4,096 bytes. Never returned (`custom_ca_configured`). API only. |

### `filabridge`

| Key | Default | Accepted |
|---|---|---|
| `url` | empty | Same rules as `spoolman.url` |
| `authentication_token` | empty | ≤ 512 bytes, no control characters. Never returned. |
| `selected_printer_id` | empty | ≤ 128 bytes |
| `ca_certificate_pem` | empty | ≤ 4,096 bytes. Never returned. API only. |

A backend token that does not start with `Basic ` or `Bearer ` is sent as `Bearer <token>`.
An `https://` URL works only when the matching `ca_certificate_pem` holds the CA certificate;
there is no Settings field for it, so set it with `PATCH /config`.

### `scale_profile`

| Key | Default | Accepted |
|---|---|---|
| `id` | `yzc-133-5kg` | `yzc-133-2kg` or `yzc-133-5kg`. Derived from the capacity; not stored. |
| `model` | `YZC-133` | `YZC-133` only. `load_cell_model` is accepted as an alias in a PATCH and is the name in the stored file. |
| `rated_capacity_grams` | `5000` | `2000` or `5000`; must agree with `id` when both are sent |
| `overload_ratio` | `1.10` | 1.01–2.0 |

Changing the model or capacity discards the scale calibration. `GET /config` reports
`calibration_configured`; the calibration values themselves are set only by
`POST /scale/calibrate` ([Scale behaviour](scale.md)).

### `toolheads`

A list of at most 8 profiles. A PATCH replaces the whole list.

| Key | Default | Accepted |
|---|---|---|
| `backend_id` | – | 0–31, unique in the list |
| `display_name` | – | 1–32 bytes |
| `nozzle_diameter_mm` | `0.4` | 0.1–2.0 |
| `enabled` | `true` | boolean |
| `nozzle_material` | `brass` | 1–32 bytes |
| `maximum_temperature_c` | `300` | 100–500 |
| `notes` | empty | ≤ 256 bytes; optional in a PATCH, the other keys are required |

### `reconciliation`

| Key | Default | Accepted |
|---|---|---|
| `auto_update_after_weigh` | `false` | boolean |
| `normal_tolerance_grams` | `5.0` | 0–1,000 |
| `warning_tolerance_grams` | `20.0` | `normal_tolerance_grams`–1,000 |

### Stored but not settable

| Key | Content |
|---|---|
| `scale` | Calibration: `zero_offset_counts`, `counts_per_gram`, `reference_grams`, `load_cell_capacity_grams`, `schema_version` |
| `spool_identity_mappings` | Up to 64 confirmed tag-to-spool links (`spool_id` with `instance_uuid` and/or `nfc_uid`). A cache; Spoolman remains the source. When full, the oldest entry is dropped. |
| `setup` | First-run progress (`completed_steps`, `ready_confirmed`) |

None of these appear in `GET /config`.

## Changing settings over the API

- Send `expected_revision` from the last `GET /config`. A stale value returns `409`.
- A field you omit keeps its value.
- An empty string clears `wifi.password`, either `authentication_token`, either
  `ca_certificate_pem` and `web.access_token`.
- The complete result is validated before it is saved; one invalid value rejects the whole
  request.

## Secrets

`GET /config` never returns `wifi.password`, `web.access_token`, the two `authentication_token`
values or the two `ca_certificate_pem` values. A response that contained one would be replaced by
`500 unsafe_configuration_snapshot`.

## Export and import

Both are functions of the browser page, built on the two `/config` routes.

| Action | Behaviour |
|---|---|
| **Download redacted JSON** | Saves the `GET /config` response as `opentag-station-redacted.json`. It contains no secrets. |
| **Choose JSON to import** | Accepts a file of 1 to 16,384 bytes. Sends the sections `device`, `wifi`, `spoolman`, `filabridge`, `scale_profile`, `toolheads` and `reconciliation` as one PATCH. |

Import ignores `web`, any password, token or certificate in the file, and the `*_configured`
flags. Secrets already on the station are kept unless you type new ones into the Settings form
before importing. The scale calibration is neither exported nor imported.

Steps: [Back up and restore settings](../configuration/backup.md).

## Wi-Fi provisioning

The station joins 2.4 GHz networks only.

### Setup network

| Item | Value |
|---|---|
| Starts when | No Wi-Fi name is saved; or 3 connection attempts in a row fail; or it is requested (`POST /network/setup-mode`, **Start setup access point** in the browser). |
| Name | `OpenTag-Setup-XXXX`; `XXXX` is the last four hex digits of the station's MAC address |
| Security | WPA2. The 10-character password is generated once after each restart of the station and shown only on the touchscreen. It is not available over the API. |
| Address | `http://192.168.4.1/`. Every DNS name resolves to it, and unknown paths redirect to it. |
| Home Wi-Fi | Stays active alongside the setup network. |
| Stops | 30 seconds after the station joins your Wi-Fi. It stays up while the join fails. |

### What a setup-network client may do

Without the access token, only `POST /network/scan` and `POST /network/connect`. A scan returns
up to 32 networks, one entry per name, strongest first, and gives up after 15 seconds.
`/network/connect` can set an access token only if none exists.

### Joining and retries

| Item | Value |
|---|---|
| Join timeout | `wifi.connect_timeout_ms` (15 seconds) |
| Retry delay | Starts at `wifi.reconnect_initial_ms` (1 second) and doubles after each failure up to `wifi.reconnect_max_ms` (60 seconds) |
| Address after joining | `http://<hostname>.local/` (mDNS; `http://opentag-station.local/` by default) or the IP address shown on the touchscreen under Settings |

Steps: [First boot and Wi-Fi](../getting-started/first-boot.md).
