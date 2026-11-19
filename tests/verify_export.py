"""Independent standard-library decoder: compare every exported block and heightmap.
Run core tests with an output path first; this never launches or accepts the game EULA.
"""
import gzip
import json
import pathlib
import struct
import sys
import zlib


class Nbt:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        assert 0 <= n <= len(self.data) - self.pos, "NBT bounds"
        value = self.data[self.pos:self.pos + n]
        self.pos += n
        return value

    def num(self, fmt):
        return struct.unpack(">" + fmt, self.take(struct.calcsize(fmt)))[0]

    def string(self):
        return self.take(self.num("H")).decode("utf-8")

    def payload(self, tag):
        if tag in (1, 2, 3, 4, 5, 6):
            return self.num({1: "b", 2: "h", 3: "i", 4: "q", 5: "f", 6: "d"}[tag])
        if tag == 7:
            return self.take(self.num("i"))
        if tag == 8:
            return self.string()
        if tag == 9:
            element, count = self.num("B"), self.num("i")
            assert 0 <= count <= 1000000
            return [self.payload(element) for _ in range(count)]
        if tag == 10:
            result = {}
            while (child := self.num("B")) != 0:
                name = self.string()
                assert name not in result, f"duplicate NBT key {name}"
                result[name] = self.payload(child)
            return result
        if tag in (11, 12):
            count = self.num("i")
            assert 0 <= count <= 1000000
            return [self.num("i" if tag == 11 else "Q") for _ in range(count)]
        raise AssertionError(f"unknown NBT type {tag}")

    def root(self):
        assert self.num("B") == 10
        self.string()
        value = self.payload(10)
        assert self.pos == len(self.data), "NBT trailing bytes"
        return value


def unpack(data, bits, count):
    per = 64 // bits
    assert len(data) == (count + per - 1) // per
    return [(data[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1) for i in range(count)]


def verify(base):
    meta = json.loads((base / "expected.json").read_text())
    expected = (base / "expected_blocks.bin").read_bytes()
    size, origin, low, high = (meta[k] for k in ("size", "origin", "minY", "maxY"))
    ids = meta["ids"]

    def block(x, y, z):
        return expected[((y - low) * size + z - origin) * size + x - origin]

    level = Nbt(gzip.decompress((base / "minecraft/level.dat").read_bytes())).root()["Data"]
    assert level["DataVersion"] == 3465 and level["Version"]["Name"] == "1.20.1"
    assert level["GameType"] == 1
    assert set(level["WorldGenSettings"]["dimensions"]) == {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"}
    assert block(level["SpawnX"], level["SpawnY"], level["SpawnZ"]) == 0
    seen = set()
    compared = 0
    for path in (base / "minecraft/region").glob("*.mca"):
        _, rx, rz, _ = path.name.split(".")
        region = path.read_bytes()
        assert len(region) % 4096 == 0 and len(region) >= 8192
        occupied = {0, 1}
        for index in range(1024):
            location = int.from_bytes(region[index * 4:index * 4 + 4], "big")
            if not location:
                continue
            offset, sectors = location >> 8, location & 255
            assert offset >= 2 and sectors > 0 and (offset + sectors) * 4096 <= len(region)
            space = set(range(offset, offset + sectors))
            assert not occupied & space, "overlapping region allocation"
            occupied |= space
            start = offset * 4096
            length = int.from_bytes(region[start:start + 4], "big")
            assert 1 < length <= sectors * 4096 - 4 and region[start + 4] == 2
            chunk = Nbt(zlib.decompress(region[start + 5:start + 4 + length])).root()
            cx, cz = int(rx) * 32 + index % 32, int(rz) * 32 + index // 32
            assert (cx, cz) not in seen
            seen.add((cx, cz))
            assert (chunk["xPos"], chunk["zPos"]) == (cx, cz)
            assert chunk["DataVersion"] == 3465 and chunk["Status"] == "minecraft:full"
            assert chunk["isLightOn"] == 0 and len(chunk["sections"]) == 24
            for section in chunk["sections"]:
                sy = section["Y"]
                state = section["block_states"]
                palette = state["palette"]
                bits = max(4, (len(palette) - 1).bit_length())
                values = unpack(state["data"], bits, 4096) if len(palette) > 1 else [0] * 4096
                assert section["biomes"]["palette"] == ["minecraft:plains"]
                for j, value in enumerate(values):
                    assert value < len(palette)
                    x, y, z = cx * 16 + j % 16, sy * 16 + j // 256, cz * 16 + (j // 16) % 16
                    actual, wanted = palette[value], ids[block(x, y, z)]
                    assert actual["Name"] == wanted, (x, y, z, actual, wanted)
                    if wanted == "minecraft:oak_slab":
                        assert actual["Properties"] == {"type": "bottom", "waterlogged": "false"}
                    compared += 1
            for name in ("WORLD_SURFACE", "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES", "OCEAN_FLOOR"):
                values = unpack(chunk["Heightmaps"][name], 9, 256)
                for j, value in enumerate(values):
                    x, z = cx * 16 + j % 16, cz * 16 + j // 16
                    wanted = next(y - low + 1 for y in range(high - 1, low - 1, -1) if block(x, y, z) != 0 and (name != "OCEAN_FLOOR" or block(x, y, z) != 5))
                    assert value == wanted, (name, x, z, value, wanted)
    assert seen == {(-1, -1), (-1, 0), (0, -1), (0, 0)}
    assert compared == size * size * (high - low)
    print(f"PASS independent NBT/Anvil verification: {len(seen)} chunks, {compared:,} blocks, 4,096 heightmap values")


def verify_rectangle(base):
    meta = json.loads((base / "expected.json").read_text())
    wanted = {(c["x"], c["z"]): c for c in meta["chunks"]}
    x0, z0, x1, z1 = meta["area"]
    assert set(wanted) == {(x, z) for z in range(z0 // 16, z1 // 16 + 1)
                           for x in range(x0 // 16, x1 // 16 + 1)}
    level = Nbt(gzip.decompress((base / "level.dat").read_bytes())).root()["Data"]
    assert [level["SpawnX"], level["SpawnZ"]] == meta["spawn"]
    assert x0 <= level["SpawnX"] <= x1 and z0 <= level["SpawnZ"] <= z1
    seen, compared = set(), 0
    for path in (base / "region").glob("*.mca"):
        _, rx, rz, _ = path.name.split(".")
        region = path.read_bytes()
        occupied = {0, 1}
        assert len(region) % 4096 == 0 and len(region) >= 8192
        for index in range(1024):
            location = int.from_bytes(region[index * 4:index * 4 + 4], "big")
            if not location:
                continue
            offset, sectors = location >> 8, location & 255
            space = set(range(offset, offset + sectors))
            assert sectors and offset >= 2 and (offset + sectors) * 4096 <= len(region)
            assert not occupied & space
            occupied |= space
            start = offset * 4096
            length = int.from_bytes(region[start:start + 4], "big")
            assert 1 < length <= sectors * 4096 - 4 and region[start + 4] == 2
            chunk = Nbt(zlib.decompress(region[start + 5:start + 4 + length])).root()
            cx, cz = int(rx) * 32 + index % 32, int(rz) * 32 + index // 32
            assert (cx, cz) in wanted and (cx, cz) not in seen
            seen.add((cx, cz))
            assert (chunk["xPos"], chunk["zPos"]) == (cx, cz)
            assert chunk["DataVersion"] == 3465 and chunk["Status"] == "minecraft:full"
            assert len(chunk["sections"]) == 24
            expected = (base / wanted[cx, cz]["file"]).read_bytes()
            assert len(expected) == 16 * 16 * 384
            for section in chunk["sections"]:
                sy, state = section["Y"], section["block_states"]
                palette = state["palette"]
                values = unpack(state["data"], max(4, (len(palette) - 1).bit_length()), 4096) if len(palette) > 1 else [0] * 4096
                for j, value in enumerate(values):
                    actual = palette[value]["Name"]
                    assert actual == meta["ids"][expected[(sy + 4) * 4096 + j]]
                    x, z = cx * 16 + j % 16, cz * 16 + (j // 16) % 16
                    bx0, bz0, bx1, bz1 = meta["bounds"]
                    if not (bx0 <= x <= bx1 and bz0 <= z <= bz1):
                        assert actual == "minecraft:air", "partial edge must remain void"
                    compared += 1
            for name in ("WORLD_SURFACE", "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES", "OCEAN_FLOOR"):
                values = unpack(chunk["Heightmaps"][name], 9, 256)
                for j, value in enumerate(values):
                    height = next((y + 1 for y in range(383, -1, -1)
                                   if expected[y * 256 + j] != 0 and
                                   (name != "OCEAN_FLOOR" or expected[y * 256 + j] != 5)), 0)
                    assert value == height
    assert seen == set(wanted)
    print(f"PASS independent {base.name} export: {len(seen)} chunks, {compared:,} blocks, partial edges and spawn")


def verify_models(base):
    expected = json.loads((base / "expected_models.json").read_text(encoding="utf-8"))
    assert len(expected) >= 6
    for item in expected:
        x, y, z = item["position"]
        cx, cz = x // 16, z // 16
        region = (base / f"minecraft/region/r.{cx // 32}.{cz // 32}.mca").read_bytes()
        index = cx % 32 + (cz % 32) * 32
        location = int.from_bytes(region[index * 4:index * 4 + 4], "big")
        start = (location >> 8) * 4096
        length = int.from_bytes(region[start:start + 4], "big")
        chunk = Nbt(zlib.decompress(region[start + 5:start + 4 + length])).root()
        section = next(s for s in chunk["sections"] if s["Y"] == y // 16)
        states = section["block_states"]
        palette = states["palette"]
        values = unpack(states["data"], max(4, (len(palette) - 1).bit_length()), 4096) if len(palette) > 1 else [0] * 4096
        actual = palette[values[(y % 16) * 256 + (z % 16) * 16 + x % 16]]
        assert actual["Name"] == item["Name"]
        assert actual.get("Properties", {}) == item["Properties"], (item, actual)
    print(f"PASS independent model export: {len(expected)} placed states including both door halves and connected fence")


if __name__ == "__main__":
    if sys.argv[1] == "--models":
        verify_models(pathlib.Path(sys.argv[2]))
    else:
        base = pathlib.Path(sys.argv[1])
        verify(base)
        verify_rectangle(base / "rectangle")
        verify_rectangle(base / "selected")
