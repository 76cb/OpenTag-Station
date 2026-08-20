# Upstream fixture provenance

`test_main.cpp` embeds the exact 312-byte contents of:

- `tests/encode_decode/01_data.bin`
- `tests/specific/unknown_data_2.bin`

from the official OpenPrintTag repository at commit
`e0dab1ae16838d2c342e7cfc509455441b7d8eba` (2026-07-02). The fixtures are used
only for format-conformance tests. They remain covered by the upstream MIT
license reproduced in `LICENSE.openprinttag`.
