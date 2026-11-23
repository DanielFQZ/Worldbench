#include "ReferenceMesh.h"
#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include "core/ProjectFormat.h"
#include "core/Search.h"
#include "core/World.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static bool equal(const wb::World &a, const wb::World &b) {
    return a.width == b.width && a.depth == b.depth && a.originX == b.originX && a.originZ == b.originZ &&
           a.seed == b.seed && a.seaLevel == b.seaLevel && a.heights == b.heights &&
           a.overrides == b.overrides;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    try {
        check(tmp.isValid(), "temporary directory");
        using namespace wb;
        check(floorDiv(-1, 16) == -1 && floorDiv(-16, 16) == -1 && floorDiv(-17, 16) == -2,
              "negative chunk coordinates");
        auto flat = World::create(WorldOptions{32, 17, -60, 10, 12, true});
        check(flat.overrides.empty() && flat.height(0, 0) == 10 && flat.height(-16, -16) == 10,
              "configured flat world without sample structures");
        auto hills = World::create(WorldOptions{64, 19, 8, 30, 14, false});
        check(hills.heights == World::create(WorldOptions{64, 19, 8, 30, 14, false}).heights,
              "seed is deterministic");
        check(hills.heights != World::create(WorldOptions{64, 20, 8, 30, 14, false}).heights,
              "seed changes hills");
        bool rejected = false;
        try {
            World::create(WorldOptions{31});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid world configuration rejected");
        auto rectangle = World::create(WorldOptions{35, 42, -60, 24, 12, false, 51});
        check(rectangle.width == 35 && rectangle.depth == 51 && rectangle.chunksX() == 3 &&
                  rectangle.chunksZ() == 4,
              "non-square non-chunk-aligned dimensions");
        check(rectangle.contains(rectangle.originX + 34, 0, rectangle.originZ + 50) &&
                  !rectangle.contains(rectangle.originX + 35, 0, rectangle.originZ + 50),
              "rectangular bounds");
        auto large = World::create(WorldOptions{16384, 42, -60, 24, 12, false, 12289});
        check(!large.heights.dense && large.heights.changes().empty(),
              "large creation is procedural and sparse");
        auto snapshot = large;
        const int farX = large.originX + 16383, farZ = large.originZ + 12288;
        large.heights.set(large.column(farX, farZ), 80);
        large.setBlock(farX, 200, farZ, Glass);
        const auto farKey = large.key(farX, 200, farZ);
        int decodedX, decodedY, decodedZ;
        large.decode(farKey, decodedX, decodedY, decodedZ);
        check(farKey > UINT32_MAX && decodedX == farX && decodedY == 200 && decodedZ == farZ,
              "64-bit distant voxel key");
        check(snapshot.height(farX, farZ) != 80 && snapshot.block(farX, 200, farZ) == Air,
              "snapshot isolates height and cell edits");
        QString bigError;
        World bigLoaded;
        check(saveProject(tmp.filePath("large.wbench"), large, bigError) &&
                  QFileInfo(tmp.filePath("large.wbench")).size() < 10000 &&
                  loadProject(tmp.filePath("large.wbench"), bigLoaded, bigError) && equal(large, bigLoaded),
              "sparse rectangular large project roundtrip");
        {
            QFile saved(tmp.filePath("large.wbench"));
            check(saved.open(QIODevice::ReadOnly), "open v3 validation fixture");
            const auto valid = projectMetadata(large, true);
            auto rejects = [&](QJsonObject invalid) {
                const auto json = QJsonDocument(invalid).toJson(QJsonDocument::Compact);
                QFile file(tmp.filePath("invalid-v3.wbench"));
                check(file.open(QIODevice::WriteOnly), "write invalid v3 fixture");
                file.write(QByteArray("WBENCH01", 8) +
                           QCryptographicHash::hash(json, QCryptographicHash::Sha256) + qCompress(json));
                file.close();
                auto preserved = bigLoaded;
                check(!loadProject(file.fileName(), bigLoaded, bigError) && equal(preserved, bigLoaded),
                      "invalid v3 rejected without mutating current world");
            };
            auto invalid = valid;
            auto invalidLayers = valid["sculptLayers"].toArray();
            auto invalidLayer = invalidLayers[0].toObject();
            invalidLayer["deltas"] = QJsonArray{QJsonArray{0, 30}, QJsonArray{0, 31}};
            invalidLayers[0] = invalidLayer;
            invalid["sculptLayers"] = invalidLayers;
            rejects(invalid);
            invalid = valid;
            invalid["overrides"] = QJsonArray{QJsonArray{double(farKey) + .5, int(Glass)}};
            rejects(invalid);
            invalid = valid;
            invalid["heights"] = "invalid";
            rejects(invalid);
            invalid = valid;
            invalid["terrain"] = QJsonObject{{"baseHeight", 400}, {"variation", 12}, {"flat", false}};
            rejects(invalid);
        }
        auto farMesh = buildChunkMesh(bigLoaded, floorDiv(farX, 16), floorDiv(farZ, 16), 1);
        check(!farMesh->opaque.empty() && !farMesh->transparent.empty(),
              "partial edge chunk and far glass rendered");
        auto decayed = World::create(WorldOptions{32, 0, -60, 10, 0, true});
        BrushOptions decay;
        decay.decay = 1;
        decay.falloff = 0;
        Stroke fading(decayed);
        Hit decayHit{true, 0, 10, 0, {.5f, 11, .5f}, {0, 1, 0}, Grass};
        for (int n = 0; n < 4; ++n)
            fading.stamp(decayHit, 0, 4, 1, Stone, false, decay);
        check(decayed.height(0, 0) == 14 && decayed.height(3, 0) == 11 && decayed.height(4, 0) == 10,
              "fractional decay accumulates and outer boundary stays untouched");
        auto selected = flat;
        BrushOptions mask;
        mask.selectionEnabled = true;
        mask.selectionMinX = 0;
        mask.selectionMaxX = 1;
        mask.selectionMinZ = 0;
        mask.selectionMaxZ = 1;
        Stroke masked(selected);
        masked.stamp(decayHit, 0, 4, 4, Stone, false, mask);
        check(selected.height(0, 0) > 10 && selected.height(-1, 0) == 10 && selected.height(2, 0) == 10,
              "selection restricts edits");
        auto cave = flat;
        for (int y = 4; y <= 10; ++y)
            cave.setBlock(1, y, 0, Air);
        cave.setBlock(1, 8, 0, Glass);
        auto caveBefore = cave;
        Hit plane{true, 0, 10, 0, {.5f, 11, .5f}, {0, 1, 0}, Grass};
        Stroke seal(cave);
        seal.stamp(plane, 1, 2, 4, Stone, false);
        auto sealCmd = seal.finish("seal");
        auto sealed = cave;
        check(cave.block(1, 10, 0) == Grass && cave.block(1, 9, 0) == Air && cave.block(1, 7, 0) == Air &&
                  cave.block(1, 8, 0) == Glass,
              "flatten caps holes while preserving cave and solids");
        cave.apply(sealCmd, false);
        check(equal(cave, caveBefore), "cap and height undo exactly");
        cave.apply(sealCmd, true);
        check(equal(cave, sealed), "cap redo exactly");
        cave = caveBefore;
        BrushOptions fillOptions;
        fillOptions.fillHoles = true;
        fillOptions.fillDepth = 3;
        Stroke fillCave(cave);
        fillCave.stamp(plane, 1, 2, 4, Stone, false, fillOptions);
        check(cave.block(1, 9, 0) == Stone && cave.block(1, 7, 0) == Stone && cave.block(1, 6, 0) == Air &&
                  cave.block(1, 8, 0) == Glass,
              "fill depth is inclusive below cap and does not replace solid material");
        auto holeCommand = fillCave.finish("fill cave");
        auto filled = cave;
        cave.apply(holeCommand, false);
        check(equal(cave, caveBefore), "fill cave undo");
        cave.apply(holeCommand, true);
        check(equal(cave, filled), "fill cave redo");
        QString caveError;
        World caveLoaded;
        check(saveProject(tmp.filePath("cave.wbench"), cave, caveError) &&
                  loadProject(tmp.filePath("cave.wbench"), caveLoaded, caveError) && equal(cave, caveLoaded),
              "cave flatten survives project roundtrip");
        auto raisedCap = World::create(WorldOptions{32, 0, -60, 6, 0, true});
        BrushOptions constant;
        constant.falloff = 1;
        Stroke raiseCap(raisedCap);
        raiseCap.stamp(plane, 1, 2, 4, Stone, false, constant);
        check(raisedCap.height(0, 0) == 10 && raisedCap.block(0, 10, 0) == Grass &&
                  raisedCap.block(0, 9, 0) == Air,
              "raising flatten builds one cap and preserves air below");
        auto lowerCap = caveBefore;
        plane.y = 8;
        Stroke lowCap(lowerCap);
        lowCap.stamp(plane, 1, 2, 4, Stone, false, constant);
        check(lowerCap.height(1, 0) == 8 && !lowerCap.isAir(lowerCap.block(1, 8, 0)) &&
                  lowerCap.block(1, 9, 0) == Air && lowerCap.block(1, 7, 0) == Air,
              "lowering flatten clears top and preserves deeper cave");
        auto direction = flat;
        BrushOptions onlyRaise;
        onlyRaise.flattenMode = 1;
        Stroke skipLower(direction);
        check(!skipLower.stamp(plane, 1, 2, 4, Stone, false, onlyRaise) && equal(direction, flat),
              "raise-only flatten rejects lowering");
        plane.y = 12;
        BrushOptions onlyLower;
        onlyLower.flattenMode = 2;
        Stroke skipRaise(direction);
        check(!skipRaise.stamp(plane, 1, 2, 4, Stone, false, onlyLower) && equal(direction, flat),
              "lower-only flatten rejects raising");
        plane.y = 10;
        auto circle = flat, square = flat;
        BrushOptions squareOptions;
        squareOptions.shape = 1;
        squareOptions.falloff = 1;
        Stroke circular(circle), rectangular(square);
        BrushOptions linear;
        linear.decay = 1;
        circular.stamp(plane, 0, 2, 4, Stone, false, linear);
        rectangular.stamp(plane, 0, 2, 4, Stone, false, squareOptions);
        check(circle.height(2, 2) == 10 && square.height(2, 2) == 14,
              "round and square footprints differ at corners");
        check(circle.height(2, 0) < circle.height(0, 0) && square.height(2, 0) == square.height(0, 0),
              "linear versus constant strength");
        auto smooth = flat;
        smooth.heights.set(smooth.column(0, 0), 20);
        Stroke smoothing(smooth);
        smoothing.stamp(plane, 5, 2, 4, Stone, false, constant);
        check(smooth.height(0, 0) == 16 && smooth.height(-1, 0) == smooth.height(1, 0),
              "smooth reduces spike with symmetric frozen kernel");
        auto filtered = flat;
        BrushOptions filter;
        filter.filterEnabled = true;
        filter.minHeight = 11;
        Stroke filterHeight(filtered);
        check(!filterHeight.stamp(plane, 0, 2, 4, Stone, false, filter), "height filter prevents changes");
        filter.minHeight = -63;
        filter.minSlope = 1;
        Stroke filterSlope(filtered);
        check(!filterSlope.stamp(plane, 0, 2, 4, Stone, false, filter), "slope filter excludes flat terrain");
        filter.minSlope = 0;
        filter.materialFilter = 1;
        filter.filterBlock = "minecraft:stone";
        Stroke filterMaterial(filtered);
        check(!filterMaterial.stamp(plane, 0, 2, 4, Stone, false, filter), "only material filter");
        filter.materialFilter = 2;
        Stroke excludeMaterial(filtered);
        check(excludeMaterial.stamp(plane, 0, 2, 4, Stone, false, filter), "except material filter");
        auto vol = flat;
        filter.materialFilter = 0;
        filter.minHeight = filter.maxHeight = 11;
        Stroke filterVolume(vol);
        filterVolume.stamp(plane, 3, 2, 4, Planks, false, filter);
        check(vol.block(0, 11, 0) == Planks && vol.block(0, 12, 0) == Air && vol.block(0, 10, 0) == Grass,
              "volume filter applies to each voxel");
        auto weakWorld = flat, strongWorld = flat;
        Stroke weakBrush(weakWorld), strongBrush(strongWorld);
        weakBrush.stamp(plane, 3, 4, 1, Planks, false);
        strongBrush.stamp(plane, 3, 4, 4, Planks, false);
        check(!weakWorld.overrides.empty() && weakWorld.overrides.size() < strongWorld.overrides.size(),
              "volume brush strength controls first-stamp density");
        Stroke noChange(flat);
        check(!noChange.stamp(plane, 1, 2, 4, Stone, false), "settled flatten avoids needless mesh jobs");
        Command inner;
        inner.cells.push_back({flat.key(-8, 8, -8), -1, int(Air)});
        check(affectedChunks(flat, inner).size() == 1, "undo interior affects one chunk");
        inner.cells[0].key = flat.key(-1, 8, -1);
        check(affectedChunks(flat, inner).size() == 4, "undo boundary includes adjacent chunk faces");
        const auto keys = searchKeys("minecraft:anvil", "铁砧");
        check(searchScore(keys, "tiezhen") >= 0 && searchScore(keys, "tz") >= 0 &&
                  searchScore(keys, "tie zhen") >= 0,
              "pinyin full spelling initials and segmented search");
        check(searchScore(keys, "tiezhn") >= 0 && searchScore(keys, "anvl") >= 0,
              "bounded fuzzy typo search");
        check(searchScore(keys, "灯笼") < 0 && searchScore(keys, "xyzq") < 0, "unrelated search excluded");
        check(searchScore(keys, "铁砧") < searchScore(keys, "tiezhn"),
              "exact results rank ahead of fuzzy matches");
        auto w = World::create(32);
        w.overrides.clear();
        w.seaLevel = -60;
        w.heights.fill(8);
        for (int x : {-16, -1, 0, 15})
            for (int z : {-16, 0, 15})
                for (int y : {-64, -1, 319}) {
                    int xx, yy, zz;
                    w.decode(w.key(x, y, z), xx, yy, zz);
                    check(x == xx && y == yy && z == zz, "voxel key roundtrip");
                }
        auto h = w.raycast({.5f, 50, .5f}, {0, -1, 0});
        check(h.valid && h.y == 8 && std::abs(h.position.y() - 9) < .001 && h.normal.y() == 1,
              "vertical ray");
        h = w.raycast({-30, 5, .5f}, {1, 0, 0});
        check(h.valid && h.x == -16 && h.normal.x() == -1, "outside axis ray");
        check(!w.raycast({30, 50, 30}, {0, -1, 0}).valid, "miss ray");
        check(!w.raycast({0, 30, 0}, {0, 0, 0}).valid, "zero ray rejected");
        w.setBlock(0, 15, 0, Slab);
        h = w.raycast({.5f, 30, .5f}, {0, -1, 0});
        check(h.block == Slab && std::abs(h.position.y() - 15.5f) < .001, "half slab hit geometry");
        h = w.raycast({-2, 15.75f, .5f}, {1, 0, 0});
        check(!h.valid, "ray through empty slab upper half");
        for (float distance : {1000.f, 35000.f, 90000.f}) {
            h = w.raycast({.5f, distance, .5f}, {0, -1, 0}, distance + 100);
            check(h.valid && h.block == Slab && std::abs(h.position.y() - 15.5f) < .002,
                  "overview ray preserves partial model geometry");
            check(!w.raycast({.5f, distance, .5f}, {0, -1, 0}, distance - 16).valid,
                  "overview ray respects maximum distance");
        }
        check(!w.raycast({0, 30, 0}, {0, -1, 0}, -1).valid, "negative ray range rejected");
        w.setBlock(1, 8, 1, Air);
        h = w.raycast({1.5f, 35000, 1.5f}, {0, -1, 0}, 40000);
        check(h.valid && h.y == 7, "far authoritative ray honors carved top air");
        {
            auto protruding = w;
            auto model = std::make_shared<BlockModel>(*w.models[Planks]);
            for (auto &face : model->faces)
                for (auto &point : face.points)
                    point.setY(point.y() + .5f);
            protruding.models[Planks] = model;
            protruding.setBlock(0, 319, 0, Planks);
            const auto hit = protruding.raycast({.5f, 35000, .5f}, {0, -1, 0}, 40000);
            check(hit.valid && std::abs(hit.position.y() - 320.5f) < .001,
                  "rebased DDA preserves JSON model faces outside world height AABB");
        }
        auto before = w;
        h = w.raycast({5.5f, 40, 5.5f}, {0, -1, 0});
        Stroke raise(w);
        raise.stamp(h, 0, 3, 2, Stone, false);
        raise.stamp(h, 0, 3, 2, Stone, false);
        auto c = raise.finish("raise");
        check(!c.heights.empty(), "raise delta");
        auto raised = w;
        w.apply(c, false);
        check(equal(w, before), "multi stamp undo");
        w.apply(c, true);
        check(equal(w, raised), "redo");
        w = before;
        Stroke lower(w);
        lower.stamp(h, 0, 3, 2, Stone, true);
        check(w.height(5, 5) == 6, "shift lowers height");
        w = before;
        Stroke carve(w);
        carve.stamp(h, 2, 2, 4, Stone, false);
        auto carveCmd = carve.finish("carve");
        check(w.block(5, 8, 5) == Air && w.block(5, 6, 5) == Air, "carve volume");
        w.apply(carveCmd, false);
        check(equal(w, before), "carve undo");
        Stroke fill(w);
        fill.stamp(h, 3, 1, 4, Planks, false);
        auto fillCmd = fill.finish("fill");
        check(w.block(5, 10, 5) == Planks, "volume above ground");
        w.apply(fillCmd, false);
        check(equal(w, before), "fill undo");
        Stroke paint(w);
        paint.stamp(h, 4, 2, 4, Sand, false);
        auto paintCmd = paint.finish("paint");
        check(w.block(5, 8, 5) == Sand && w.block(5, 10, 5) == Air, "paint preserves air");
        w.apply(paintCmd, false);
        check(equal(w, before), "paint undo");
        w.setBlock(0, -64, 0, Air);
        check(w.block(0, -64, 0) == Bedrock, "bedrock protected");
        w.heights.set(w.column(5, 5), 12);
        h = {true, 5, 12, 5, {5.5f, 13, 5.5f}, {0, 1, 0}, Grass};
        Stroke flatten(w);
        flatten.stamp(h, 1, 3, 4, Stone, false);
        check(w.height(6, 5) > 8 && w.height(6, 5) <= 12, "flatten to initial height");
        auto mesh = buildChunkMesh(w, 0, 0, 1);
        check(!mesh->opaque.empty(), "opaque mesh");
        bool half = false;
        for (const auto &v : mesh->opaque) {
            check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), "finite mesh vertex");
            if (v.y == 15.5f)
                half = true;
        }
        check(half, "half slab mesh");
        QString error;
        const auto path = tmp.filePath("roundtrip.wbench");
        check(saveProject(path, w, error), "save project");
        World loaded;
        check(loadProject(path, loaded, error) && equal(w, loaded), "project exact roundtrip");
        {
            QFile current(path);
            check(current.open(QIODevice::ReadOnly), "read v2 migration fixture");
            auto bytes = current.readAll();
            auto root = projectMetadata(w, true);
            root["format"] = 1;
            root["size"] = w.width;
            QJsonArray legacyHeights;
            for (int i = 0; i < int(w.heights.size()); ++i)
                legacyHeights.append(w.heights[i]);
            root["heights"] = legacyHeights;
            root.remove("palette");
            root.remove("resources");
            root.remove("resourceVersion");
            auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
            QFile legacy(tmp.filePath("legacy.wbench"));
            check(legacy.open(QIODevice::WriteOnly), "write v1 fixture");
            legacy.write(QByteArray("WBENCH01", 8) +
                         QCryptographicHash::hash(data, QCryptographicHash::Sha256) + qCompress(data));
            legacy.close();
            World migrated;
            check(loadProject(legacy.fileName(), migrated, error) && equal(w, migrated) &&
                      migrated.palette == w.palette,
                  "v1 loads with preserved built-in IDs and states");
        }
        QFile corrupt(path);
        check(corrupt.open(QIODevice::ReadWrite), "open corruption fixture");
        corrupt.seek(12);
        corrupt.write("X", 1);
        corrupt.close();
        auto untouched = loaded;
        check(!loadProject(path, loaded, error) && equal(loaded, untouched),
              "corruption rejected without mutation");
        auto fixture = World::create(32);
        fixture.overrides.clear();
        fixture.seaLevel = 4;
        fixture.heights.fill(2);
        fixture.setBlock(-1, 9, -1, Slab);
        fixture.setBlock(0, 10, 0, Glass);
        fixture.setBlock(1, 11, 1, Planks);
        fixture.setBlock(-2, 0, -2, Air);
        fixture.setBlock(4, 20, 3, Stone);
        QString base = app.arguments().value(1, tmp.path() + "/fixture");
        check(QDir().mkpath(base), "fixture directory");
        const auto exported = exportMinecraft(fixture, base + "/minecraft");
        check(exported.ok && exported.chunks == 4, "export four chunks including negatives");
        check(!exportMinecraft(fixture, base + "/minecraft").ok, "refuse overwriting world");
        std::atomic_bool cancel = true;
        check(!exportMinecraft(fixture, base + "/cancelled", &cancel).ok &&
                  !QFile::exists(base + "/cancelled"),
              "cancel cleans partial output");
        check(QDir(base).entryList({"*.partial-*"}, QDir::Dirs).isEmpty(),
              "no partial directories after cancellation");
        auto regionResult =
            exportMinecraft(large, base + "/selected", nullptr, {}, QRect(farX - 31, farZ - 31, 32, 32));
        check(regionResult.ok && regionResult.chunks == 6,
              "large world local export expands partial area to chunks");
        auto rectResult = exportMinecraft(rectangle, base + "/rectangle");
        check(rectResult.ok && rectResult.chunks == 12, "rectangular partial edge export");
        auto comparison = [&](const World &source, const QString &name, QRect area) {
            QJsonArray chunks, ids;
            for (int i = 0; i < BlockCount; ++i)
                ids.append(info(Block(i)).minecraft);
            for (int cz = floorDiv(area.top(), 16); cz <= floorDiv(area.bottom(), 16); ++cz)
                for (int cx = floorDiv(area.left(), 16); cx <= floorDiv(area.right(), 16); ++cx) {
                    QByteArray values;
                    for (int y = World::MinY; y < World::MaxY; ++y)
                        for (int z = cz * 16; z < cz * 16 + 16; ++z)
                            for (int x = cx * 16; x < cx * 16 + 16; ++x)
                                values.append(char(source.block(x, y, z)));
                    const auto fileName = QString("expected-%1-%2.bin").arg(cx).arg(cz);
                    QFile bytes(base + "/" + name + "/" + fileName);
                    check(bytes.open(QIODevice::WriteOnly) && bytes.write(values) == values.size(),
                          "rectangular export source blocks");
                    chunks.append(QJsonObject{{"x", cx}, {"z", cz}, {"file", fileName}});
                }
            QFile manifest(base + "/" + name + "/expected.json");
            check(manifest.open(QIODevice::WriteOnly), "rectangular export manifest");
            manifest.write(
                QJsonDocument(
                    QJsonObject{{"chunks", chunks},
                                {"ids", ids},
                                {"area", QJsonArray{area.left(), area.top(), area.right(), area.bottom()}},
                                {"spawn", QJsonArray{area.center().x(), area.center().y()}},
                                {"bounds",
                                 QJsonArray{source.originX, source.originZ, source.originX + source.width - 1,
                                            source.originZ + source.depth - 1}}})
                    .toJson());
        };
        comparison(large, "selected", QRect(farX - 31, farZ - 31, 32, 32));
        comparison(rectangle, "rectangle",
                   QRect(rectangle.originX, rectangle.originZ, rectangle.width, rectangle.depth));
        QByteArray expected;
        for (int y = World::MinY; y < World::MaxY; ++y)
            for (int z = -16; z < 16; ++z)
                for (int x = -16; x < 16; ++x)
                    expected.append(char(fixture.block(x, y, z)));
        QFile bytes(base + "/expected_blocks.bin");
        check(bytes.open(QIODevice::WriteOnly) && bytes.write(expected) == expected.size(),
              "export comparison fixture");
        bytes.close();
        QJsonArray ids;
        for (int i = 0; i < BlockCount; ++i)
            ids.append(info(Block(i)).minecraft);
        QFile meta(base + "/expected.json");
        check(meta.open(QIODevice::WriteOnly), "export manifest");
        meta.write(QJsonDocument(
                       QJsonObject{{"size", 32}, {"origin", -16}, {"minY", -64}, {"maxY", 320}, {"ids", ids}})
                       .toJson());
        meta.close();
        // Compare the optimized mesher against the prior exhaustive implementation.
        auto meshWorld = World::create(WorldOptions{35, 42, 40, 60, 30, false, 51});
        for (int z = -16; z < 16; z += 3)
            for (int x = -16; x < 16; x += 3) {
                meshWorld.setBlock(x, -40, z, Air);
                meshWorld.setBlock(x, -39, z, Glass);
                meshWorld.setBlock(x, 120, z, Slab);
            }
        for (int z = -1; z <= 2; ++z)
            for (int x = -1; x <= 1; ++x)
                check(sameMesh(buildChunkMesh(meshWorld, x, z, 1), referenceChunkMesh(meshWorld, x, z, 1)),
                      "exact surface mesher matches exhaustive geometry including cavities and edges");
        auto special = std::make_shared<BlockModel>(*meshWorld.models[Stone]);
        special->faces.front().cull = -1;
        special->full = false;
        meshWorld.models[Stone] = special;
        check(sameMesh(buildChunkMesh(meshWorld, 0, 0, 1), referenceChunkMesh(meshWorld, 0, 0, 1)),
              "nonstandard resource model retains exhaustive path");
        QElapsedTimer t;
        t.start();
        auto defaultWorld = World::create();
        size_t vertices = 0;
        for (int z = -4; z < 4; ++z)
            for (int x = -4; x < 4; ++x) {
                auto m = buildChunkMesh(defaultWorld, x, z, 2);
                vertices += m->opaque.size() + m->transparent.size();
            }
        std::cout << "PASS " << checks << " checks; default mesh " << vertices << " vertices in "
                  << t.elapsed() << " ms; fixture=" << base.toStdString() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
