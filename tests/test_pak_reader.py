import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from pak_reader import Dataset


class ReaderTest(unittest.TestCase):
    def test_native_payloads(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            root = {"schema": "spellcross-dataset", "version": 1,
                    "resource_catalog": "resources.json", "packages": [{"file": "common.pak"}]}
            (path / "manifest.json").write_text(json.dumps(root), encoding="utf-8")
            records = [
                {"id": "COMMON.FS::JEDNOTKY.DEF#1", "status": "converted",
                 "package": "common.pak", "payload": "records/1/units.json"},
                {"id": "COMMON.FS::M01_01.DTA#1", "status": "converted",
                 "package": "common.pak", "payload": "records/2/map.json"},
            ]
            catalog = {"schema": "spellcross-resource-catalog", "version": 1, "records": records,
                       "by_name": {}, "by_leaf": {"JEDNOTKY.DEF": [records[0]["id"]],
                                                 "M01_01.DTA": [records[1]["id"]]},
                       "atlas_groups": [{"group": "UNITS.FSU::TEST", "package": "common.pak",
                                         "entry": "graphics/1/frames.json"}],
                       "by_group": {}, "by_group_leaf": {"TEST": [0]}}
            (path / "resources.json").write_text(json.dumps(catalog), encoding="utf-8")
            with zipfile.ZipFile(path / "common.pak", "w") as archive:
                archive.writestr("records/1/units.json", json.dumps({
                    "schema": "spellcross-units", "version": 1, "units": [{"id": 0, "name": "Test"}]}))
                archive.writestr("records/2/map.json", json.dumps({
                    "schema": "spellcross-tactical-map", "version": 1, "width": 1, "height": 1,
                    "terrain": "T11", "ground": {"entry": "ground.u16le", "sprites": ["TILE"]},
                    "objects": {"entry": "objects.u8x2", "sprites": ["TREE"]}}))
                archive.writestr("records/2/ground.u16le", struct.pack("<H", 3 << 12))
                archive.writestr("records/2/objects.u8x2", bytes([1, 128]))
                archive.writestr("graphics/1/frames.json", json.dumps({
                    "schema": "spellcross-atlas", "version": 1, "pages": [],
                    "frames": [{"id": "sprite", "frame": 0}]}))
            reader = Dataset(path)
            self.assertEqual(reader.unit(0)["name"], "Test")
            self.assertEqual(reader.map_tile("m01_01.dta", 0, 0)["elevation"], 3)
            self.assertEqual(reader.map_tile("m01_01.dta", 0, 0)["object_sprite"], "TREE")
            self.assertEqual(reader.group("test")["frame_count"], 1)
            with self.assertRaises(ValueError):
                reader.map_tile("m01_01.dta", 1, 0)
            reader.catalog["by_leaf"]["JEDNOTKY.DEF"].append(records[1]["id"])
            with self.assertRaises(ValueError):
                reader.unit(0)


if __name__ == "__main__":
    unittest.main()
