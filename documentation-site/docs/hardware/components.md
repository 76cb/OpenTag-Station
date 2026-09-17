# Controller, ADC and reader

The **WT32-SC01 Plus** production profile uses ESP32-S3, a 480 × 320 landscape
ST7796 panel and FT6336 touch. GPIOs are fixed in the central board profile.
The non-Plus board and other ESP32 display modules require a separate reviewed
hardware port; their appearance is not evidence of compatibility.

The **NAU7802** adapter owns the scale bus and performs bounded converter reset,
power-ready checks, 3.0 V LDO selection, gain 128 and 10-sample/second setup.
Internal AFE calibration is different from user calibration with a reference mass.
Initialization and sample operations have deadlines, so a missing ADC can be
reported/retried without blocking touch. The selected breakout's supply and pull-up
specifications remain a hardware-builder responsibility.

The **ELECHOUSE NFC_ST25R3916B** module supplies the reader and integrated antenna.
Close its I²C solder bridge, use the dedicated Wire1 bus and connect IRQ. Leave
SPI-only CS/BSS and MOSI unconnected. The pinned ELECHOUSE driver is used by
production; routine polling reads, while the guarded writer is the only approved
physical mutation path.

| Peripheral | Address | Bus owner | Normal bus speed |
|---|---|---|---|
| NAU7802 | 0x2A | Scale / Wire 0 | 400 kHz |
| ST25R3916B | 0x50 | Backend NFC owner / Wire1 1 | 100 kHz |
| Built-in touch | 0x38 | LovyanGFX software I²C | 400 kHz |

See [BOM](bill-of-materials.md) before purchasing and [complete wiring](wiring.md)
before assembly. GPIO21 is unused; internal LCD/touch pins are not peripheral
expansion contacts.
