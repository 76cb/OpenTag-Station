"""Decode native writer output using the independently pinned upstream codec."""
import argparse
import pathlib
import subprocess
import sys
import yaml

parser = argparse.ArgumentParser()
parser.add_argument('--spec-root', required=True, type=pathlib.Path)
args = parser.parse_args()
root = args.spec_root.resolve()
revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
assert revision == '7e09cc38df1c8e7824a67f5b1ae93071f52519ad'
image = pathlib.Path('.pio/writer-mapping.bin').read_bytes()
assert len(image) == 312
result = subprocess.run([sys.executable, str(root / 'utils/rec_info.py'), '--show-data', '--validate'],
                        input=image, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=root)
assert result.returncode == 0, result.stdout.decode() + result.stderr.decode()
data = yaml.safe_load(result.stdout)
assert not data.get('unknown_fields'), data
main = data['data']['main']
aux = data['data']['aux']
assert main['material_class'] == 'FFF'
assert main['material_type'] == 'PLA'
assert main['material_name'] == 'PLA Blue'
assert main['brand_name'] == 'Acme'
assert main['brand_specific_package_id'] == 'SKU123'
assert main['filament_diameter_v2'] == 1750
assert abs(main['density'] - 1.24) < 0.00001
assert main['actual_netto_full_weight'] == main['nominal_netto_full_weight'] == 1000
assert main['empty_container_weight'] == 200
assert aux['consumed_weight'] == 100
assert 'preheat_temperature' not in main
print('PASS: populated production writer image independently decoded and schema-validated by pinned upstream')
