#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
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
    return a.size == b.size && a.originX == b.originX && a.originZ == b.originZ && a.seed == b.seed &&
           a.seaLevel == b.seaLevel && a.heights == b.heights && a.overrides == b.overrides;
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
        check(flat.overrides.empty() &&
                  std::all_of(flat.heights.begin(), flat.heights.end(), [](int h) { return h == 10; }),
              "configured flat world without sample structures");
        auto hills = World::create(WorldOptions{64, 19, 8, 30, 14, false});
        check(hills.heights == World::create(WorldOptions{64, 19, 8, 30, 14, false}).heights,
              "seed is deterministic");
        check(hills.heights != World::create(WorldOptions{64, 20, 8, 30, 14, false}).heights,
              "seed changes hills");
        bool rejected = false;
        try {
            World::create(WorldOptions{33});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid world configuration rejected");
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
        circular.stamp(plane, 0, 2, 4, Stone, false);
        rectangular.stamp(plane, 0, 2, 4, Stone, false, squareOptions);
        check(circle.height(2, 2) == 10 && square.height(2, 2) == 14,
              "round and square footprints differ at corners");
        check(circle.height(2, 0) < circle.height(0, 0) && square.height(2, 0) == square.height(0, 0),
              "linear versus constant strength");
        auto smooth = flat;
        smooth.heights[smooth.column(0, 0)] = 20;
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
        filterVolume.stamp(plane, 3, 2, 1, Planks, false, filter);
        check(vol.block(0, 11, 0) == Planks && vol.block(0, 12, 0) == Air && vol.block(0, 10, 0) == Grass,
              "volume filter applies to each voxel");
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
        std::fill(w.heights.begin(), w.heights.end(), 8);
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
        carve.stamp(h, 2, 2, 1, Stone, false);
        auto carveCmd = carve.finish("carve");
        check(w.block(5, 8, 5) == Air && w.block(5, 6, 5) == Air, "carve volume");
        w.apply(carveCmd, false);
        check(equal(w, before), "carve undo");
        Stroke fill(w);
        fill.stamp(h, 3, 1, 1, Planks, false);
        auto fillCmd = fill.finish("fill");
        check(w.block(5, 10, 5) == Planks, "volume above ground");
        w.apply(fillCmd, false);
        check(equal(w, before), "fill undo");
        Stroke paint(w);
        paint.stamp(h, 4, 2, 1, Sand, false);
        auto paintCmd = paint.finish("paint");
        check(w.block(5, 8, 5) == Sand && w.block(5, 10, 5) == Air, "paint preserves air");
        w.apply(paintCmd, false);
        check(equal(w, before), "paint undo");
        w.setBlock(0, -64, 0, Air);
        check(w.block(0, -64, 0) == Bedrock, "bedrock protected");
        w.heights[w.column(5, 5)] = 12;
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
            auto root = QJsonDocument::fromJson(qUncompress(bytes.mid(40))).object();
            root["format"] = 1;
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
        std::fill(fixture.heights.begin(), fixture.heights.end(), 2);
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
