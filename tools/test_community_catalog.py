import json
import tempfile
import unittest
from pathlib import Path

from tools.community_catalog import CatalogError, compile_catalog, inspect_pack


def record(identifier="acme-pla", **changes):
    value = {"id": identifier, "manufacturer": "Acme", "name": "PLA Blue",
             "material": "PLA", "density": 1.24, "diameter": 1.75,
             "weight": 1000, "color_hex": "123456", "codes": ["A-1"]}
    value.update(changes)
    return value


class CommunityCompilerTests(unittest.TestCase):
    def compile(self, values):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        root = Path(folder.name)
        source, output = root / "source.json", root / "community.pack"
        source.write_text(json.dumps(values), encoding="utf-8")
        manifest = compile_catalog(source, output, "2026-09-18", "fixture-revision")
        return folder, output, manifest

    def test_deterministic_and_stable_source_identity(self):
        first, one, manifest = self.compile([record(), record("other")])
        second, two, again = self.compile([record(), record("other")])
        self.addCleanup(first.cleanup); self.addCleanup(second.cleanup)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        self.assertEqual(manifest, again)
        self.assertEqual(inspect_pack(one), manifest)
        self.assertEqual(manifest["records"], 2)
        self.assertEqual(manifest["schema"], 1)

    def test_source_schema_validation(self):
        for value in [record(future=True), record(density=None),
                      record(manufacturer=""), [], "broken"]:
            with self.subTest(value=value), self.assertRaises(CatalogError):
                folder, *_ = self.compile([value])
                folder.cleanup()

    def test_malformed_source_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root / "source.json").write_text("[{bad")
            with self.assertRaises(CatalogError):
                compile_catalog(root / "source.json", root / "out", "v", "r")

    def test_checksum_and_offsets_fail_closed(self):
        folder, output, _ = self.compile([record()]); self.addCleanup(folder.cleanup)
        damaged = bytearray(output.read_bytes()); damaged[-1] ^= 1; output.write_bytes(damaged)
        with self.assertRaises(CatalogError): inspect_pack(output)

    def test_size_gate(self):
        folder, output, manifest = self.compile([record(str(n), name="X" * 128) for n in range(2000)])
        self.addCleanup(folder.cleanup)
        self.assertLess(manifest["size"], 2_621_440)


if __name__ == "__main__": unittest.main()
