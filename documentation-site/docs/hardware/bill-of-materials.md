# Bill of materials

Build the production WT32-SC01 Plus station described here. Similar-looking ESP32
display boards and arbitrary NFC stickers are not substitutes for the supported
profile. The firmware does not autodetect alternate GPIO layouts.

| Component | Qty | Role | Required? | Specification | Interface / connector | Compatibility and sourcing |
|---|---:|---|---|---|---|---|
| WT32-SC01 Plus | 1 | Controller, display, touch, Wi-Fi | Yes | ESP32-S3, 16 MiB flash; 480 × 320 landscape UI | USB data/power; 8-pin EXT IO | Exact supported board family; non-Plus WT32-SC01 is not equivalent |
| NAU7802 breakout | 1 | Differential bridge ADC | Yes | Address `0x2A`, 3.3 V-compatible I²C, usable E+/E− and A+/A− terminals | VIN, GND, SDA, SCL and load-cell terminals | Seller not pinned. Choose a breakout whose documented VIN accepts the selected supply and whose I²C pull-ups are 3.3 V; see [power](power.md) |
| YZC-133 5 kg load cell | 1 | Measures spool/platform force | Yes, default | Four-wire full bridge, 5,000 g rated capacity | E+/E− excitation, A+/A− differential output | Supported default. Verify the supplied cell's wire mapping and mounting drawing |
| YZC-133 2 kg load cell | 1 alternative | Lower-capacity scale | Optional alternative | 2,000 g rated capacity | Same bridge interface | Select matching firmware profile and recalibrate; not an additional parallel cell |
| ELECHOUSE `NFC_ST25R3916B` module | 1 | NFC reader and antenna | Yes | 5 V module supply, I²C mode, IRQ exposed, integrated antenna | 7-pin 1.25 mm connector | Known-compatible exact module type; do not assume Mini/external-antenna boards share connector order |
| NXP ICODE SLIX2 tags | As needed | OpenPrintTag spool consumables | Yes for tag workflows | NFC-V / ISO15693, NXP UID prefix `E0:04`, 80 blocks × 4 bytes | RF, no electrical cable | Approved production profile; [supported tags](../openprinttag/supported-tags.md) explains protection and geometry checks |
| USB data cable | 1 | First install and controller power | Yes | Compatible with the board's USB socket; data-capable, sound conductors | USB | Charge-only cables cannot flash firmware |
| Regulated 5 V supply | 1 | Powers station | Yes | Maintain 5 V ±5% at EXT under load; allow controller, reader and ADC headroom | USB or verified EXT supply path | Complete-station current is not characterized; no measured current rating is claimed |
| WT32 EXT mating harness | 1 | Peripheral connections | Yes | Match the actual 8-pin board connector housing/pitch/key | EXT pins 1–8 | Check board revision and manufacturer drawing before buying; generic Dupont ends are not a substitute for the mating connector |
| NFC mating harness | 1 | Reader power/data/IRQ | Yes | 7-pin, 1.25 mm, matches module keying | Module pins 1–7 | Continuity-test each lead; wire color and cable viewpoint do not establish pin order |
| Scale headers / terminal wires | 1 set | Connects breakout and bridge | Yes | Correct breakout headers, insulated short conductors | Breakout-specific | No universal connector pitch is assumed |
| Base, platform, fasteners, spacers | 1 set | Mounts load cell without binding | Yes | Match the cell drawing and actual hardware | Mechanical | No enclosure STL, hole pattern, screw length or platform dimensions are defined by this project |
| Enclosure / cable restraint | As needed | Insulates electronics, relieves cables | Optional | Nonbinding platform clearance; antenna clearance | Mechanical | Design locally using [mechanical guidance](mechanical.md); no reference enclosure is claimed |
| Reference mass and multimeter | 1 each | Calibration and wiring validation tools | Required for setup | Known mass within capacity; continuity/voltage capability | Tools | These are setup tools, not station accessories |

Reader hardware and tags are different purchases. A multi-protocol reader does
not make all of its supported RF tag families writable by this application.
NTAG213/215/216, MIFARE, unknown vendors, other block geometries and locked tags
are outside the approved production writer profile.

## Reference sources

- [WT32 manufacturer datasheet, mirrored PDF](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf): reference for EXT numbering and voltage; not a purchase endorsement.
- [ELECHOUSE I²C configuration guide](https://www.elechouse.com/st25r3916-esp32-i2c-quick-start/): manufacturer reference for the solder bridge, 5 V supply and host signals. Its generic ESP32 GPIO examples do **not** apply to this station.
- Firmware board profile and writer geometry are linked in [pinout](pinout.md).

Before ordering, confirm the exact breakout and harness documentation. Before
assembly, read the [complete wiring guide](wiring.md); a connector that fits can
still have reversed power leads.
