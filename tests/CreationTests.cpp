#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/NbtPayload.h"
#include "core/Project.h"
#include "core/SkyImage.h"
#include "core/WorldCreation.h"
#include "third_party/tinyexr.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtZlib/zlib.h>
#include <iostream>
#include <stdexcept>
using namespace wb;
static int checks = 0;
static void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
static PcgGraph surface() {
    PcgGraph g;
    g.id = "surface";
    g.nodes = {
        {"constant", "constant", {{"value", 1}}},
        {"out", "output_surface", {{"block", "minecraft:stone"}, {"under", "minecraft:sand"}, {"depth", 3}}}};
    g.connections = {{"constant", "value", "out", "value"}};
    return g;
}
static StructureAsset tree() {
    StructureAsset a;
    a.id = "tree";
    a.name = "Tree";
    a.width = 3;
    a.depth = 3;
    a.height = 5;
    a.pivot = {1, 0, 1};
    for (int y = 0; y < 4; ++y)
        a.blocks.push_back({1, y, 1, "minecraft:oak_log[axis=y]"});
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x)
            a.blocks.push_back({x, 4, z, "minecraft:oak_leaves[persistent=true]"});
    return a;
}
static QByteArray nbtString(QString s) {
    auto b = s.toUtf8();
    QByteArray out(2, 0);
    qToBigEndian<uint16_t>(uint16_t(b.size()), out.data());
    return out + b;
}
static void tag(QByteArray &b, int type, QString name) {
    b.append(char(type));
    b += nbtString(name);
}
static void integer(QByteArray &b, int n) {
    char data[4];
    qToBigEndian<int32_t>(n, data);
    b.append(data, 4);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QString error;
        auto graph = surface();
        check(graph.validate(error), "graph validation");
        check(graph.evaluate(0, 0, 24, 0, 1) == 1, "output evaluation");
        auto cycle = graph;
        cycle.nodes.push_back({"sum", "add", {}});
        cycle.connections.push_back({"sum", "value", "sum", "a"});
        check(!cycle.validate(error), "cycle rejection");
        auto badPort = graph;
        badPort.connections.front().toPort = "missing";
        check(!badPort.validate(error), "typed port rejection");
        auto noise = graph;
        noise.nodes[0].type = "noise";
        noise.nodes[0].parameters = {{"scale", 16}, {"octaves", 4}};
        double n = noise.evaluate(-13, 27, 12, 0, 451);
        check(n == noise.evaluate(-13, 27, 12, 0, 451), "deterministic negative-coordinate noise");
        check(n != noise.evaluate(-13, 27, 12, 0, 452), "seed changes noise");
        WorldOptions options;
        options.width = 64;
        options.depth = 64;
        options.flat = true;
        options.baseHeight = 24;
        options.seaLevel = 4;
        World world = World::create(options);
        {
            auto placed = World::create(options);
            auto asset = tree();
            asset.groundBlend = 2;
            placed.creation.assets = {asset};
            AssetPreset instance;
            instance.id = "root-test";
            instance.assetId = asset.id;
            instance.position = {0, 25, 0};
            placed.creation.presets = {instance};
            check(placed.rebuildCreation(error), "tree ground blend fixture");
            check(placed.block(1, 28, 0) == Air && placed.block(1, 27, 0) == Air,
                  "canopy must not create floating stone foundation");
            auto original = placed.generated->cells;
            auto meshes = buildCreationMeshes(placed, 1);
            check(meshes.size() == 1 && !meshes[0].mesh->opaque.empty(), "asset has independent mesh");
            bool simplified = false;
            for (auto &lod : meshes[0].lods)
                simplified |=
                    !lod->opaqueIndices.empty() && lod->opaqueIndices.size() < meshes[0].mesh->opaque.size();
            check(simplified, "distant asset simplifies triangle count");
            check(original == placed.generated->cells, "LOD keeps authoritative generated blocks");
            placed.setBlock(0, 26, 0, Air);
            auto edited = buildCreationMeshes(placed, 2);
            check(edited[0].mesh->opaque.size() != meshes[0].mesh->opaque.size(),
                  "manual carve honored by independent asset mesh");
        }
        world.creation.graphs.push_back(graph);
        world.creation.volumes.push_back(
            {"volume", "Paint", graph.id, {-16, 0, -16}, {16, 64, 16}, 0, 7, true, true});
        check(world.rebuildCreation(error), "PCG generate");
        check(world.block(0, 24, 0) == Stone && world.block(0, 23, 0) == Sand,
              "PCG writes real surface and subsurface blocks");
        check(world.block(22, 24, 22) == Grass, "volume bounds leave exterior intact");
        world.setBlock(0, 24, 0, Planks);
        check(world.rebuildCreation(error) && world.block(0, 24, 0) == Planks,
              "manual edits remain above procedural material");
        world.creation.volumes[0].enabled = false;
        check(world.rebuildCreation(error) && world.block(1, 24, 1) == Grass &&
                  world.block(0, 24, 0) == Planks,
              "hidden PCG restores base and preserves manual edit");
        world.creation.volumes[0].enabled = true;
        world.creation.assets.push_back(tree());
        PcgGraph scatter;
        scatter.id = "scatter";
        scatter.nodes = {{"out", "output_scatter", {{"assetId", "tree"}, {"spacing", 8}, {"density", 1}}}};
        world.creation.graphs.push_back(scatter);
        auto volume = world.creation.volumes[0];
        volume.id = "forest";
        volume.graphId = "scatter";
        world.creation.volumes.push_back(volume);
        check(world.rebuildCreation(error) && !world.generated->scatterPoints.empty(),
              "scatter produces structures");
        auto points = world.generated->scatterPoints;
        auto cells = world.generated->cells;
        check(world.rebuildCreation(error) && world.generated->scatterPoints == points &&
                  world.generated->cells == cells,
              "scatter identical across rebuilds");
        for (size_t i = 0; i < points.size(); ++i) {
            check(points[i].y() == 25, "roots lie on surface");
            for (size_t j = i + 1; j < points.size(); ++j)
                check((points[i] - points[j]).length() >= 4, "scatter spacing");
        }
        auto first = points.front();
        check(world.palette[world.block(int(first.x()), 25, int(first.z()))].name == "minecraft:oak_log",
              "generated structure visible to block reader");
        world.creation.graphs.back().nodes[0].parameters["embed"] = 2;
        check(world.rebuildCreation(error) && !world.generated->scatterPoints.empty() &&
                  world.generated->scatterPoints.front().y() == 23,
              "embed roots through procedural soil");
        world.creation.graphs.back().nodes[0].parameters["embed"] = 0;
        auto building = tree();
        building.id = "building";
        building.width = building.depth = 32;
        building.height = 40;
        building.pivot = {16, 0, 16};
        building.groundBlend = 0;
        world.creation.assets.push_back(building);
        AssetPreset buildingInstance;
        buildingInstance.id = "reserved-building";
        buildingInstance.assetId = building.id;
        buildingInstance.position = {0, 24, 0};
        world.creation.presets.push_back(buildingInstance);
        check(world.rebuildCreation(error) && world.generated->scatterPoints.empty(),
              "buildings reserve full bounds before scattering");
        world.creation.presets.clear();
        world.creation.assets.pop_back();
        check(world.rebuildCreation(error), "restore forest after collision test");
        auto mesh = buildChunkMesh(world, floorDiv(int(first.x()), 16), floorDiv(int(first.z()), 16), 1);
        check(mesh->upper.y() > 29 && mesh->opaque.size() > 0,
              "generated structures have actual mesh geometry");
        QTemporaryDir dir;
        check(dir.isValid(), "temp directory");
        check(saveProject(dir.filePath("generated.wbench"), world, error), "save generated project");
        World loaded;
        check(loadProject(dir.filePath("generated.wbench"), loaded, error) &&
                  loaded.creation == world.creation,
              "creation round trip");
        check(loaded.generated->cells == world.generated->cells, "reopen reproduces generated result");
        auto invalid = saveCreation(world.creation);
        auto volumes = invalid["volumes"].toArray();
        auto v = volumes[0].toObject();
        v["graphId"] = "missing";
        volumes[0] = v;
        invalid["volumes"] = volumes;
        CreationState rejected;
        check(!loadCreation(invalid, rejected, error), "dangling graph rejected");
        auto roadWorld = World::create(options);
        roadWorld.creation.roadNodes = {{"a", "A", {-12, 25, 0}, true}, {"b", "B", {12, 25, 0}, true}};
        roadWorld.creation.roadEdges = {{"ab", "a", "b", 5, "minecraft:gravel", false, false, true}};
        check(roadWorld.rebuildCreation(error), "road generate");
        check(roadWorld.palette[roadWorld.block(0, roadWorld.height(0, 0), 0)].name == "minecraft:gravel",
              "road material is authoritative");
        check(roadWorld.block(0, 24, 8) == Grass, "road cross section bounded");
        roadWorld.creation.roadEdges[0].bridge = true;
        roadWorld.creation.roadNodes[0].position.setY(30);
        roadWorld.creation.roadNodes[1].position.setY(30);
        check(roadWorld.rebuildCreation(error) && roadWorld.block(0, 29, 0) == Air &&
                  roadWorld.block(0, 30, 0) != Air,
              "bridge retains air below deck");
        roadWorld.creation.roadEdges[0].bridge = false;
        roadWorld.creation.roadEdges[0].tunnel = true;
        roadWorld.creation.roadNodes[0].position.setY(12);
        roadWorld.creation.roadNodes[1].position.setY(12);
        check(roadWorld.rebuildCreation(error) && roadWorld.block(0, 14, 0) == Air &&
                  roadWorld.block(0, 24, 0) == Grass,
              "tunnel carves clearance without flattening roof");
        StructureAsset cave;
        cave.id = "cave";
        cave.name = "cave";
        cave.width = cave.height = cave.depth = 5;
        cave.pivot = {0, 0, 0};
        cave.placement = "cave";
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z)
                for (int x = 0; x < 5; ++x)
                    if ((x == 0 || x == 4 || y == 0 || y == 4 || z == 0 || z == 4) &&
                        !(x == 0 && y == 2 && z == 2))
                        cave.blocks.push_back({x, y, z, "minecraft:stone"});
        CaveMarker seed;
        seed.id = "seed";
        seed.assetId = "cave";
        seed.kind = "interior";
        seed.position = seed.end = {2, 2, 2};
        auto leak = analyzeAssetCave(cave, {seed});
        check(!leak.valid && leak.labels[(2 * 5 + 2) * 5 + 2] == 3,
              "X boundary leak detected and seed cannot override flood");
        CaveMarker portal;
        portal.id = "portal";
        portal.assetId = "cave";
        portal.position = portal.end = {0, 2, 2};
        auto closed = analyzeAssetCave(cave, {seed, portal});
        check(closed.valid && closed.labels[(2 * 5 + 2) * 5] == 2, "virtual portal seals analysis only");
        auto sealed = cave;
        sealed.blocks.push_back({0, 2, 2, "minecraft:stone"});
        auto unconfirmed = analyzeAssetCave(sealed, {});
        check(!unconfirmed.valid && unconfirmed.unconfirmedChambers == 1,
              "isolated chamber requires explicit seed confirmation");
        check(analyzeAssetCave(sealed, {seed}).valid, "seed confirms isolated chamber");
        auto caveWorld = World::create(options);
        caveWorld.creation.assets = {cave};
        caveWorld.creation.caveMarkers = {seed, portal};
        AssetPreset cp;
        cp.id = "placed-cave";
        cp.assetId = "cave";
        cp.position = {0, 8, 0};
        caveWorld.creation.presets.push_back(cp);
        check(caveWorld.rebuildCreation(error), "underground cave place");
        check(caveWorld.block(2, 10, 2) == Air && caveWorld.block(0, 10, 2) == Air,
              "internal and entrance air carved");
        check(caveWorld.block(-1, 10, 2) == Stone, "external terrain preserved");
        // Minimal real Minecraft NBT asset (one block, palette, size, no JSON shortcut).
        QByteArray nbt;
        tag(nbt, 10, "");
        tag(nbt, 9, "size");
        nbt.append(char(3));
        integer(nbt, 3);
        integer(nbt, 1);
        integer(nbt, 1);
        integer(nbt, 1);
        tag(nbt, 9, "palette");
        nbt.append(char(10));
        integer(nbt, 1);
        tag(nbt, 8, "Name");
        nbt += nbtString("minecraft:stone");
        nbt.append(char(0));
        tag(nbt, 9, "blocks");
        nbt.append(char(10));
        integer(nbt, 1);
        tag(nbt, 3, "state");
        integer(nbt, 0);
        tag(nbt, 9, "pos");
        nbt.append(char(3));
        integer(nbt, 3);
        integer(nbt, 0);
        integer(nbt, 0);
        integer(nbt, 0);
        nbt.append(char(0));
        nbt.append(char(0));
        QFile f(dir.filePath("one.nbt"));
        check(f.open(QIODevice::WriteOnly) && f.write(nbt) == nbt.size(), "write NBT fixture");
        f.close();
        StructureAsset imported;
        check(importStructure(f.fileName(), imported, error) && imported.blocks.size() == 1 &&
                  imported.blocks[0].state == "minecraft:stone",
              "real NBT import");
        QFile hdr(dir.filePath("sky.hdr"));
        check(hdr.open(QIODevice::WriteOnly), "HDR fixture open");
        hdr.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n");
        hdr.write(QByteArray::fromHex("8040208280402082"));
        hdr.close();
        auto sky = loadSkyImage(hdr.fileName(), error);
        check(!sky.isNull() && sky.format() == QImage::Format_RGBA32FPx4 &&
                  reinterpret_cast<const float *>(sky.constBits())[0] > 1.9f,
              "HDR preserves scene-linear values above one");
        float floats[8] = {4, 2, 1, 1, 8, 4, 2, 1};
        const char *exrError = nullptr;
        auto exrPath = dir.filePath("sky.exr").toLocal8Bit();
        check(SaveEXR(floats, 2, 1, 4, 0, exrPath.constData(), &exrError) == 0, "EXR fixture saved");
        sky = loadSkyImage(QString::fromLocal8Bit(exrPath), error);
        check(!sky.isNull() && reinterpret_cast<const float *>(sky.constBits())[0] > 3.9f,
              "EXR preserves HDR range");

        // Scalar subgraphs and per-instance exposed parameters use the same plan.
        PcgGraph field;
        field.id = "field";
        field.nodes = {{"in", "input", {}},
                       {"factor", "parameter", {{"name", "gain"}, {"default", 2}}},
                       {"mul", "multiply", {}}};
        field.connections = {{"in", "value", "mul", "a"}, {"factor", "value", "mul", "b"}};
        PcgGraph parent;
        parent.id = "parent";
        parent.nodes = {{"v", "constant", {{"value", .2}}}, {"sub", "subgraph", {{"graph", field.toJson()}}}};
        parent.connections = {{"v", "value", "sub", "value"}};
        PcgPlan plan;
        check(compileGraph(parent, plan, error), "compile portable subgraph");
        PcgSample sample;
        QJsonObject overrideParameters{{"gain", 3}};
        sample.parameters = &overrideParameters;
        check(std::abs(plan.evaluate(sample) - .6) < 1e-9, "subgraph input and instance override");
        auto cancelled = world.generated;
        int callbacks = 0;
        check(!world.rebuildCreation(error, [&](int, const QString &) { return ++callbacks < 3; }) &&
                  world.generated == cancelled,
              "cancelled generation never publishes partial cells");
        auto copy = tree();
        auto shared = copy;
        check(copy.blocks.identity() == shared.blocks.identity(), "asset snapshots share block payload");
        copy.blocks.push_back({0, 0, 0, "minecraft:stone"});
        check(copy.blocks.size() == shared.blocks.size() + 1 &&
                  copy.blocks.identity() != shared.blocks.identity(),
              "voxel edits detach payload");
        auto invalidScale = world.creation;
        AssetPreset invalidAsset;
        invalidAsset.id = "invalid-scale";
        invalidAsset.assetId = "tree";
        invalidAsset.scale = {1.3f, 1, 1};
        invalidScale.presets.push_back(invalidAsset);
        check(!validateCreation(invalidScale, error), "fractional voxel scale rejected");
        auto clipped = World::create(options);
        clipped.creation.assets.push_back(tree());
        AssetPreset edgeAsset;
        edgeAsset.id = "edge";
        edgeAsset.assetId = "tree";
        edgeAsset.position = {31, 25, 31};
        clipped.creation.presets.push_back(edgeAsset);
        check(!clipped.rebuildCreation(error), "explicit asset is not silently clipped at border");
        auto badSky = dir.filePath("bad.hdr");
        QFile badFile(badSky);
        check(badFile.open(QIODevice::WriteOnly), "bad sky fixture");
        badFile.write("#?RADIANCE\nbroken");
        badFile.close();
        check(loadSkyImage(badSky, error).isNull() && !error.isEmpty(), "malformed HDR diagnosed");
        // Real gzip-compressed Sponge and numeric MCEdit input, independent of JSON.
        auto gzipBytes = [](const QByteArray &input) {
            z_stream z{};
            if (deflateInit2(&z, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
                throw std::runtime_error("gzip setup");
            QByteArray output(compressBound(input.size()) + 32, 0);
            z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
            z.avail_in = input.size();
            z.next_out = reinterpret_cast<Bytef *>(output.data());
            z.avail_out = output.size();
            int r = deflate(&z, Z_FINISH);
            output.resize(z.total_out);
            deflateEnd(&z);
            if (r != Z_STREAM_END)
                throw std::runtime_error("gzip encode");
            return output;
        };
        auto shortTag = [&](QByteArray &bytes, QString name, int n) {
            tag(bytes, 2, name);
            char value[2];
            qToBigEndian<qint16>(qint16(n), value);
            bytes.append(value, 2);
        };
        QByteArray sponge;
        tag(sponge, 10, "");
        tag(sponge, 3, "Version");
        integer(sponge, 2);
        shortTag(sponge, "Width", 1);
        shortTag(sponge, "Height", 1);
        shortTag(sponge, "Length", 1);
        tag(sponge, 10, "Palette");
        tag(sponge, 3, "minecraft:stone");
        integer(sponge, 0);
        sponge.append(char(0));
        tag(sponge, 7, "BlockData");
        integer(sponge, 1);
        sponge.append(char(0));
        sponge.append(char(0));
        QFile schem(dir.filePath("one.schem"));
        check(schem.open(QIODevice::WriteOnly), "Sponge file open");
        schem.write(gzipBytes(sponge));
        schem.close();
        check(importStructure(schem.fileName(), imported, error) && imported.blocks.size() == 1 &&
                  imported.blocks[0].state == "minecraft:stone",
              "Sponge v2 gzip import");
        QByteArray sponge3;
        tag(sponge3, 10, "");
        tag(sponge3, 10, "Schematic");
        tag(sponge3, 3, "Version");
        integer(sponge3, 3);
        shortTag(sponge3, "Width", 1);
        shortTag(sponge3, "Height", 1);
        shortTag(sponge3, "Length", 1);
        tag(sponge3, 10, "Blocks");
        tag(sponge3, 10, "Palette");
        tag(sponge3, 3, "minecraft:rail[shape=north_south,waterlogged=false]");
        integer(sponge3, 0);
        sponge3.append(char(0));
        tag(sponge3, 7, "Data");
        integer(sponge3, 1);
        sponge3.append(QByteArray(4, 0));
        QFile v3(dir.filePath("rail.schem"));
        check(v3.open(QIODevice::WriteOnly), "v3 fixture open");
        v3.write(gzipBytes(sponge3));
        v3.close();
        check(importStructure(v3.fileName(), imported, error) && imported.blocks.size() == 1,
              "Sponge v3 nested Blocks import");
        auto rotatedWorld = World::create(options);
        imported.id = "rail";
        imported.pivot = {};
        imported.groundBlend = 0;
        rotatedWorld.creation.assets.push_back(imported);
        AssetPreset rotated;
        rotated.id = "rail-instance";
        rotated.assetId = "rail";
        rotated.position = {0, 25, 0};
        rotated.rotation = {0, 90, 0};
        rotatedWorld.creation.presets.push_back(rotated);
        check(rotatedWorld.rebuildCreation(error) &&
                  rotatedWorld.palette[rotatedWorld.block(0, 25, 0)].properties["shape"] == "east_west",
              "quarter-turn rail state matches rotated geometry");
        QByteArray legacy;
        tag(legacy, 10, "");
        shortTag(legacy, "Width", 1);
        shortTag(legacy, "Height", 1);
        shortTag(legacy, "Length", 1);
        tag(legacy, 7, "Blocks");
        integer(legacy, 1);
        legacy.append(char(1));
        tag(legacy, 7, "Data");
        integer(legacy, 1);
        legacy.append(char(0));
        legacy.append(char(0));
        QFile mcEdit(dir.filePath("one.schematic"));
        check(mcEdit.open(QIODevice::WriteOnly), "legacy file open");
        mcEdit.write(gzipBytes(legacy));
        mcEdit.close();
        check(importStructure(mcEdit.fileName(), imported, error) &&
                  imported.blocks[0].state == "minecraft:stone",
              "legacy numeric mapping uses checked registry");
        // Preserve byte-exact unknown block-entity tags while replacing coordinates.
        QByteArray payload = nbtText("id", "minecraft:chest") + nbtText("CustomName", "{\"text\":\"Kept\"}") +
                             nbtInt("x", 99) + QByteArray(1, 0);
        auto patched = patchNbtPayload(payload, {{"x", nbtInt("x", 3)}});
        check(patched.contains("Kept") && patched.contains(nbtInt("x", 3)) &&
                  !patched.contains(nbtInt("x", 99)),
              "NBT patch preserves metadata and replaces coordinate once");
        auto entityWorld = World::create(options);
        StructureAsset chest;
        chest.id = "data-asset";
        chest.width = chest.height = chest.depth = 1;
        chest.blocks.push_back(
            {0, 0, 0, "minecraft:chest[facing=north,type=single,waterlogged=false]", payload});
        QByteArray entity =
            nbtText("id", "minecraft:armor_stand") + nbtVector("Pos", {.5f, 1, .5f}) + QByteArray(1, 0);
        chest.entities.push_back({{.5f, 1, .5f}, entity});
        entityWorld.creation.assets.push_back(chest);
        AssetPreset place;
        place.id = "data-instance";
        place.assetId = chest.id;
        place.position = {4, 25, 5};
        entityWorld.creation.presets.push_back(place);
        check(entityWorld.rebuildCreation(error), "entity generation");
        check(entityWorld.generated->blockEntities.begin()->second.contains(nbtInt("x", 4)),
              "block entity transformed");
        check(entityWorld.generated->entities[0].position == QVector3D(4.5f, 26, 5.5f),
              "entity position transformed");
        auto entities = entityWorld.generated->entities;
        check(entityWorld.rebuildCreation(error) && entityWorld.generated->entities == entities,
              "entity UUID is stable across regeneration");
        auto library = std::make_shared<AssetLibrary>();
        library->version = "1.20.1";
        CatalogBlock chestCatalog;
        chestCatalog.name = "minecraft:chest";
        chestCatalog.options = {{"facing", {"north", "south", "east", "west"}},
                                {"type", {"single", "left", "right"}},
                                {"waterlogged", {"false", "true"}}};
        library->catalog[chestCatalog.name] = chestCatalog;
        entityWorld.setAssets(library);
        auto exported = exportMinecraft(entityWorld, dir.filePath("entity-export"));
        check(exported.ok && QFile::exists(dir.filePath("entity-export/entities/r.0.0.mca")),
              "separate Java entity regions exported");
        check(saveProject(dir.filePath("entity-data.wbench"), entityWorld, error) &&
                  loadProject(dir.filePath("entity-data.wbench"), loaded, error) &&
                  loaded.creation == entityWorld.creation,
              "NBT data survives project serialization");
        bool attachmentRejected = false;
        try {
            transformEntityNbt(nbtText("id", "minecraft:item_frame") + nbtInt("TileX", 0) + QByteArray(1, 0),
                               {}, 0, false, false, "test");
        } catch (const std::exception &) {
            attachmentRejected = true;
        }
        check(attachmentRejected, "unsupported attached entity rejects rather than losing attachment data");
        auto largeWorld = World::create(options);
        StructureAsset bulk;
        bulk.id = "bulk";
        bulk.width = 256;
        bulk.depth = 256;
        bulk.height = 5;
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 256; ++z)
                for (int x = 0; x < 256; ++x)
                    bulk.blocks.push_back({x, y, z, "minecraft:stone"});
        largeWorld.creation.assets.push_back(bulk);
        check(QJsonDocument(saveCreation(largeWorld.creation)).toJson(QJsonDocument::Compact).size() >
                  16 * 1024 * 1024,
              "fixture exceeds old manifest limit");
        auto bulkPath = dir.filePath("bulk.wbench");
        check(saveProject(bulkPath, largeWorld, error) && loadProject(bulkPath, loaded, error) &&
                  loaded.creation == largeWorld.creation,
              "large assets use checksummed creation chunks");
        QFile corrupt(bulkPath);
        check(corrupt.open(QIODevice::ReadWrite) && corrupt.seek(64), "corrupt fixture accessible");
        auto byte = corrupt.read(1);
        byte[0] ^= 0x40;
        corrupt.seek(64);
        corrupt.write(byte);
        corrupt.close();
        auto unchanged = loaded.creation;
        check(!loadProject(bulkPath, loaded, error) && loaded.creation == unchanged,
              "corrupt creation storage cannot replace current world");
        std::cout << "Creation tests passed: " << checks << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
