# Community catalog (postponed)

The Community catalog is a local, searchable copy of a public filament database from which a
filament could be imported into Spoolman. It is switched off in the shipping firmware and is not
described in the user manual.

## Switch

`OPENTAG_ENABLE_COMMUNITY` in `src/config/product_features.hpp` defaults to `0`.
The firmware environment `wt32-sc01-plus` does not set it. Only the host test environment
`native-community` builds with `-DOPENTAG_ENABLE_COMMUNITY=1`.

With the switch off:

- The tag writer and the REST API reject every `community_*`, `import_preview` and `import`
  action with `Community is disabled for 1.0`.
- The touchscreen and browser offer no Community source.
- The factory image contains an empty LittleFS; the build fails if `community.pack` ends up in it.
- The installer page and the release files do not include the catalog.
- `tools/check_production_memory.py` fails if catalog search, detail, status, verify or update
  code is linked into the firmware.

## What remains in the tree

| Path | Content |
|---|---|
| `community/` | `community.pack` (the compiled catalog), `manifest.json`, `source.json` |
| `tools/community_catalog.py` | Compiles and inspects a pack (`compile`, `inspect`) |
| `tools/test_community_catalog.py`, `test/test_community_catalog/` | Tool and host tests |
| `src/services/community_catalog*.{hpp,cpp}`, `src/platform/storage/community_catalog_store.*`, `src/network/gzip_stream.hpp`, `src/network/miniz/` | Catalog reader, updater, storage and inflater |
| `[env:native-community]` in `platformio.ini` | Host tests with the feature on |

CI still runs `python -m unittest tools.test_community_catalog`,
`python tools/community_catalog.py inspect community/community.pack` and
`pio test --environment native-community`, so the code keeps compiling.

The browser assets still contain hidden Community markup. It is never shown while the switch is
off.
