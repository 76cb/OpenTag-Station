# Local control and security

The station serves plain HTTP on a trusted local network. It does not provide
TLS at its own browser endpoint. Public read-only GET routes and the read-only
event stream expose operational state; credentials are omitted from their
responses. Do not publish a running station directly to the internet.

| Setting | Behavior |
|---|---|
| Local API token blank | Normal local mutations work in trusted-LAN mode; this is not an unhealthy state |
| Local API token configured | Protected mutations require its exact bearer token |
| Setup AP | Scoped scan/connect provisioning access; cannot authorize arbitrary backend/scale/OTA mutations |
| Credential field omitted | Preserve saved value |
| Explicit credential clear | Clear that value under normal authorization |

Set or rotate the token in authenticated configuration controls. It must meet the
documented 16–128-character allowed alphabet. The browser keeps the entered token
only in that tab's memory and clears it on authentication failure. It is not stored
in URLs, cookies or browser persistent storage. A token over HTTP is not encrypted
by the station; use a protected trusted network.

For setup, the AP is intentionally unencrypted and local. Existing token recovery
cannot replace the token through provisioning. Keep the device physically
controlled while entering Wi-Fi credentials. Store exports and screenshots
privately unless sanitized; public examples use reserved domains and synthetic
tag IDs. See [Wi-Fi setup](../getting-started/first-boot.md) and
[configuration reference](../reference/configuration.md).
