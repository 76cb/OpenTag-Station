"""Verify the documentation wiring contract against production, generate SVGs."""
import argparse
import html
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / 'documentation-site'


def diagram(kind, pins):
    rows = []
    if kind in ('system', 'scale'):
        rows += [('EXT 1 / qualified supply', 'NAU7802 VIN (verify rating)', '#e5a84b'),
                 ('EXT 2 / GND', 'NAU7802 GND', '#87969b'),
                 (f"EXT 3 / GPIO{pins['scale_sda']}", 'NAU7802 SDA', '#55d7b3'),
                 (f"EXT 4 / GPIO{pins['scale_scl']}", 'NAU7802 SCL', '#55d7b3')]
    if kind in ('system', 'nfc'):
        rows += [('EXT 1 / +5 V', 'NFC 6 / +5V', '#e5a84b'), ('EXT 2 / GND', 'NFC 7 / GND', '#87969b'),
                 (f"EXT 5 / GPIO{pins['diagnostic_nfc_interrupt']}", 'NFC 1 / IRQ', '#bb9ae8'),
                 (f"EXT 6 / GPIO{pins['diagnostic_nfc_sda']}", 'NFC 5 / MISO-SDA', '#74b8ee'),
                 (f"EXT 7 / GPIO{pins['diagnostic_nfc_scl']}", 'NFC 3 / SCLK-SCL', '#74b8ee')]
    height = 215 + len(rows) * 42 + (85 if kind == 'scale' else 0)
    title = {'system': 'Complete station wiring', 'scale': 'Scale and load cell', 'nfc': 'ELECHOUSE NFC in I2C mode'}[kind]
    elements = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="{height}" viewBox="0 0 1100 {height}" role="img" aria-labelledby="title desc">',
                f'<title id="title">{title}</title><desc id="desc">Electrical connections, board-contact numbering; not a cable-side physical orientation drawing.</desc>',
                f'<rect width="1100" height="{height}" rx="16" fill="#10191e"/>',
                '<g font-family="sans-serif" fill="#edf5f2">',
                f'<text x="32" y="42" font-size="27">{title}</text>',
                '<text x="32" y="73" font-size="16" fill="#adbcbf">Board contacts • identify pin 1 from labels and continuity • GPIO is 3.3 V logic</text>',
                '<text x="32" y="112" font-size="20">WT32-SC01 Plus / EXT</text>',
                '<text x="690" y="112" font-size="20">Peripheral connector</text>']
    for i, (left, right, color) in enumerate(rows):
        y = 150 + i * 42
        elements += [f'<text x="32" y="{y}" font-size="18">{html.escape(left)}</text>',
                     f'<path d="M370 {y-6} H665" stroke="{color}" stroke-width="3"/>',
                     f'<text x="690" y="{y}" font-size="18">{html.escape(right)}</text>']
    y = 160 + len(rows) * 42
    footer = {'system': 'Scale: Wire / 0, 0x2A, 400 kHz. NFC: Wire1 / 1, 0x50, 100 kHz. Touch: software GPIO6/5.',
              'scale': 'NAU7802 E+ / E- → cell excitation + / -; A+ / A- → signal + / -.',
              'nfc': 'I2C solder bridge CLOSED. NFC pin 2 CS/BSS and pin 4 MOSI: DISCONNECTED.'}[kind]
    elements.append(f'<text x="32" y="{y}" font-size="16" fill="#55d7b3">{html.escape(footer)}</text>')
    if kind == 'scale':
        elements.append(f'<text x="32" y="{y+34}" font-size="16">Wire / controller 0 • 0x2A • 400 kHz bus • 10 samples/s • gain 128</text>')
        elements.append(f'<text x="32" y="{y+66}" font-size="16">Use cell datasheet for lead pairs. Colors vary. VIN and pull-up voltage depend on breakout.</text>')
    elements.append('</g></svg>\n')
    return '\n'.join(elements)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    pins = json.loads((SITE / 'hardware.json').read_text())
    board = (ROOT / 'src/boards/wt32_sc01_plus_rev_a.hpp').read_text()
    for name, value in pins.items():
        if name == 'scale_frequency_hz':
            scale = (ROOT / 'src/hardware/scale/nau7802_device.hpp').read_text()
            assert f'i2c_frequency_hz{{{value}U}}' in scale
        else:
            match = re.search(r'\b' + name + r'\s*=\s*(-?(?:0x[\da-fA-F]+|\d+))U?;', board)
            assert match and int(match[1], 0) == value, f'Documentation GPIO/config drift: {name}'
    for name in ('sda', 'scl', 'interrupt', 'i2c_address'):
        assert f'nfc_{name} = diagnostic_nfc_{name};' in board
    assert '&Wire1' in (ROOT / 'src/hardware/nfc/st25r3916b/i2c_reader.hpp').read_text()
    application = (ROOT / 'src/application/application.hpp').read_text()
    assert re.search(r'scale_adc_\s*\{\s*Wire\s*,', application), 'Scale controller ownership changed'
    writer = (ROOT / 'src/nfc/openprinttag_writer.cpp').read_text()
    # A geometry/profile change requires reviewing supported-tags, not just pins.
    for token in ('p.uid.bytes[0] != 0xe0', 'p.uid.bytes[1] != 0x04',
                  'p.geometry.block_size != 4', 'p.geometry.block_count != 80'):
        assert token in writer, 'Approved tag profile changed; review the tag documentation'
    for kind in ('system', 'scale', 'nfc'):
        path = SITE / f'docs/assets/images/wiring/{kind}.svg'
        content = diagram(kind, pins)
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
        assert path.read_text(encoding='utf-8') == content, f'Stale diagram: {path}'
    wiring = (SITE / 'docs/hardware/wiring.md').read_text()
    for token in ('GPIO10', 'GPIO11', 'GPIO12', 'GPIO13', 'GPIO14', '0x2A', '0x50', '400 kHz', '100 kHz', 'DISCONNECTED'):
        assert token in wiring, f'Missing wiring contract: {token}'
    print('Hardware documentation matches production pins, clocks, controllers and tag profile; 3 SVGs verified')


if __name__ == '__main__':
    main()
