#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
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
