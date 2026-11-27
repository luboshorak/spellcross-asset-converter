"""Minimal, read-only consumer for converter PAK v1. Python 3, stdlib only.

Use validated local datasets. This is a contract example, not a game engine
or a substitute for the converter's full structural/source validation.
"""
import argparse
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile


def key(name):
    return "".join(c.upper() if "a" <= c <= "z" else c for c in name.replace("\\", "/"))


def unique(items, name):
    if len(items) != 1:
        raise ValueError(f"{name}: expected one match, found {len(items)}; use exact ID or full group")
    return items[0]


class Dataset:
    def __init__(self, path):
        self.path = Path(path)
        root = json.loads((self.path / "manifest.json").read_text(encoding="utf-8"))
        if root["schema"] != "spellcross-dataset" or root["version"] != 1:
            raise ValueError("Unsupported dataset schema")
        if root.get("resource_catalog") != "resources.json":
            raise ValueError("Export with resource catalog support")
        self.packages = {p["file"] for p in root["packages"]}
        self.catalog = json.loads((self.path / "resources.json").read_text(encoding="utf-8"))
        if self.catalog["schema"] != "spellcross-resource-catalog" or self.catalog["version"] != 1:
            raise ValueError("Unsupported resource catalog")
        self.records = {r["id"]: r for r in self.catalog["records"]}
        if len(self.records) != len(self.catalog["records"]):
            raise ValueError("Duplicate record ID")

    def read(self, package, entry):
        # Never extract entries or interpret source names as filesystem paths.
        if package not in self.packages or Path(package).name != package or "\\" in package or ":" in package:
            raise ValueError("Invalid package")
        parts = entry.split("/")
        if any(p in ("", ".", "..") for p in parts) or "\\" in entry or ":" in entry:
            raise ValueError("Invalid package entry")
        with zipfile.ZipFile(self.path / package) as archive:
            return archive.read(entry)

    def resource(self, name):
        if name in self.records:
            return self.records[name]
        lookup = key(name)
        ids = self.catalog["by_name"].get(lookup)
        if ids is None:
            ids = self.catalog["by_leaf"].get(lookup, [])
        return self.records[unique(ids, name)]

    def payload(self, record):
        if record["status"] != "converted":
            raise ValueError("Not a converted payload: " + record["id"])
        return json.loads(self.read(record["package"], record["payload"]))

    def unit(self, unit_id):
        data = self.payload(self.resource("JEDNOTKY.DEF"))
        if data["schema"] != "spellcross-units" or data["version"] != 1:
            raise ValueError("Unsupported unit schema")
        return unique([u for u in data["units"] if u["id"] == unit_id], str(unit_id))

    def map_tile(self, name, x, y):
        record = self.resource(name)
        data = self.payload(record)
        if data["schema"] != "spellcross-tactical-map" or data["version"] != 1:
            raise ValueError("Unsupported map schema")
        w, h = data["width"], data["height"]
        if not (0 <= x < w and 0 <= y < h):
            raise ValueError("Tile outside map")
        base = PurePosixPath(record["payload"]).parent
        ground = self.read(record["package"], str(base / data["ground"]["entry"]))
        objects = self.read(record["package"], str(base / data["objects"]["entry"]))
        if len(ground) != w*h*2 or len(objects) != w*h*2:
            raise ValueError("Invalid map planes")
        pos = (y*w+x)*2
        value = struct.unpack_from("<H", ground, pos)[0]
        index, flags = objects[pos:pos+2]
        return {"map": record["id"], "width": w, "height": h, "terrain": data["terrain"],
                "x": x, "y": y, "ground_sprite": data["ground"]["sprites"][value & 4095],
                "elevation": value >> 12, "object_sprite": data["objects"]["sprites"][index-1] if index else None,
                "object_flags": flags}

    def group(self, name):
        lookup = key(name)
        indices = self.catalog["by_group"].get(lookup)
        if indices is None:
            indices = self.catalog["by_group_leaf"].get(lookup, [])
        group = self.catalog["atlas_groups"][unique(indices, name)]
        atlas = json.loads(self.read(group["package"], group["entry"]))
        if atlas["schema"] != "spellcross-atlas" or atlas["version"] != 1:
            raise ValueError("Unsupported atlas schema")
        # Pages are indexed-value RGB PNG + separate opacity PNG, NOT RGB artwork.
        return {"group": group, "pages": atlas["pages"], "frame_count": len(atlas["frames"]),
                "first_frame": atlas["frames"][0] if atlas["frames"] else None,
                "fsu_layout": atlas.get("fsu_layout")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", help="Export directory with manifest.json")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("resource").add_argument("name")
    sub.add_parser("unit").add_argument("id", type=int)
    tile = sub.add_parser("tile")
    tile.add_argument("name")
    tile.add_argument("x", type=int)
    tile.add_argument("y", type=int)
    sub.add_parser("group").add_argument("name")
    args = parser.parse_args()
    dataset = Dataset(args.dataset)
    if args.command == "resource":
        result = dataset.resource(args.name)
    elif args.command == "unit":
        result = dataset.unit(args.id)
    elif args.command == "tile":
        result = dataset.map_tile(args.name, args.x, args.y)
    else:
        result = dataset.group(args.name)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
