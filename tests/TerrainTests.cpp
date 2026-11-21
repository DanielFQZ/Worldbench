#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include "core/ProjectFormat.h"
#include "core/World.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    };
    try {
        const QString base = app.arguments().value(1, temporary.path());
        check(QDir().mkpath(base), "fixture output");
        QImage precision(4, 1, QImage::Format_Grayscale16);
        auto *row = reinterpret_cast<quint16 *>(precision.scanLine(0));
        row[0] = 0;
        row[1] = 1;
        row[2] = 32768;
        row[3] = 65535;
        check(precision.save(base + "/precision.png"), "write 16-bit PNG");
        QString error;
        auto decoded = wb::loadHeightmap(base + "/precision.png", error);
        check(decoded && decoded->values == std::vector<uint16_t>{0, 1, 32768, 65535},
              "16-bit precision retained");
        QImage eight(2, 2, QImage::Format_Grayscale8);
        eight.fill(127);
        check(eight.save(base + "/eight.png"), "write 8-bit PNG");
        auto eightData = wb::loadHeightmap(base + "/eight.png", error);
        check(eightData && eightData->values[0] == 127 * 257, "8-bit full-range conversion");
        QImage bad(2, 2, QImage::Format_RGBA8888);
        bad.fill(Qt::red);
        bad.save(base + "/color.png");
        check(!wb::loadHeightmap(base + "/color.png", error), "color input rejected");
        bad.fill(Qt::transparent);
        bad.save(base + "/alpha.png");
        check(!wb::loadHeightmap(base + "/alpha.png", error), "transparent input rejected");
        auto white = std::make_shared<wb::HeightmapData>();
        white->width = white->depth = 2;
        white->values.assign(4, 65535);
        auto black = std::make_shared<wb::HeightmapData>(*white);
        black->values.assign(4, 0);
        wb::TerrainInstance a;
        a.id = "a";
        a.name = "mountain";
        a.image = white;
        a.x = -8;
        a.z = -8;
        a.width = a.depth = 17;
        a.low = 10;
        a.high = 30;
        a.feather = 0;
        check(a.apply(10, 0, 0) == 30 && a.apply(10, 9, 0) == 10, "instance bounds and replace");
        a.blend = 1;
        check(a.apply(10, 0, 0) == 40, "add mode");
        a.blend = 2;
        check(a.apply(50, 0, 0) == 50, "max mode");
        a.blend = 3;
        check(a.apply(10, 0, 0) == 10, "min mode");
        a.blend = 0;
        a.opacity = 50;
        check(a.apply(10, 0, 0) == 20, "opacity blending");
        a.opacity = 100;
        a.mask = black;
        check(a.apply(10, 0, 0) == 10, "black mask preserves base");
        a.mask = white;
        check(a.apply(10, 0, 0) == 30, "white mask applies height");
        a.feather = 50;
        check(a.apply(10, -8, 0) == 10 && a.apply(10, 0, 0) == 30, "edge feather");
        a.feather = 0;
        a.mask.reset();
        a.enabled = false;
        check(a.apply(10, 0, 0) == 10, "disabled instance ignored");
        a.enabled = true;
        wb::HeightmapData ramp{2, 2, {0, 65535, 0, 65535}};
        check(std::abs(ramp.sample(.5, .3) - .5) < 1e-9, "bilinear sampling");
        auto world = wb::World::create(wb::WorldOptions{32, 42, -60, 10, 0, true});
        world.heights.instances = {a};
        check(world.height(0, 0) == 30 && world.height(10, 0) == 10, "authoritative composed terrain");
        auto b = a;
        b.id = "b";
        b.high = 40;
        world.heights.instances.push_back(b);
        check(world.height(0, 0) == 40, "later instance overlays earlier");
        std::swap(world.heights.instances[0], world.heights.instances[1]);
        check(world.height(0, 0) == 30, "reorder changes overlap");
        auto snapshot = world;
        world.heights.instances[1].x = 10;
        check(snapshot.height(0, 0) == 30 && world.height(0, 0) == 40, "snapshot isolates transforms");
        world.heights.set(world.column(0, 0), 55);
        world.heights.instances[0].enabled = false;
        check(world.height(0, 0) == 25,
              "relative sculpt survives while removed instance contribution disappears");
        wb::Command command;
        command.terrainBefore =
            std::make_shared<const std::vector<wb::TerrainInstance>>(world.heights.instances);
        auto changed = world.heights.instances;
        changed.clear();
        command.terrainAfter = std::make_shared<const std::vector<wb::TerrainInstance>>(changed);
        world.apply(command, true);
        check(world.heights.instances.empty(), "instance removal command");
        world.apply(command, false);
        check(world.heights.instances.size() == 2 && world.height(0, 0) == 25,
              "instance undo preserves manual edits");
        world.heights.instances[0].mask = decoded;
        world.heights.instances[0].maskName = "precision.png";
        check(wb::saveProject(base + "/embedded.wbench", world, error), "save embedded instances");
        QFile::remove(base + "/precision.png");
        wb::World loaded;
        check(wb::loadProject(base + "/embedded.wbench", loaded, error) && loaded.heights == world.heights &&
                  loaded.heights.instances == world.heights.instances,
              "reopen without source image preserves data and edits");
        auto pinned = wb::World::create(wb::WorldOptions{32, 42, -60, 10, 0, true});
        pinned.heights.instances = {a};
        pinned.heights.set(pinned.column(0, 0), 40);
        pinned.heights.instances[0].high = 40;
        check(wb::saveProject(base + "/pinned.wbench", pinned, error) &&
                  wb::loadProject(base + "/pinned.wbench", loaded, error),
              "coincident manual edit saved");
        wb::Stroke stroke(loaded);
        stroke.stamp({true, 0, 40, 0, {.5f, 41, .5f}, {0, 1, 0}, wb::Grass}, 0, 1, 1, wb::Stone, false);
        auto change = stroke.finish("raise");
        loaded.apply(change, false);
        loaded.heights.instances[0].high = 60;
        check(loaded.height(0, 0) == 70, "save and undo preserve relative sculpt after instance remapping");
        auto layered = wb::World::create(wb::WorldOptions{32, 42, -60, 10, 0, true});
        auto addA = a;
        addA.blend = 1;
        addA.low = 0;
        addA.high = 20;
        auto addB = addA;
        addB.id = "second";
        addB.high = 30;
        layered.heights.instances = {addA, addB};
        wb::Stroke raise(layered);
        raise.stamp({true, 0, 60, 0, {.5f, 61, .5f}, {0, 1, 0}, wb::Grass}, 0, 1, 4, wb::Stone, false);
        auto raised = raise.finish("relative");
        check(layered.height(0, 0) == 64, "two additive instances plus sculpt");
        layered.heights.instances[0].enabled = false;
        layered.heights.instances[1].enabled = false;
        check(layered.height(0, 0) == 14, "disabling both instances keeps only plus four sculpt");
        wb::SculptLayer second;
        second.id = "layer2";
        second.name = "第二层";
        layered.heights.layers.push_back(second);
        layered.heights.activeLayer = second.id;
        layered.heights.set(layered.column(0, 0), 21);
        auto layerSnapshot = layered;
        layered.heights.layers[0].enabled = false;
        check(layered.height(0, 0) == 17 && layerSnapshot.height(0, 0) == 21,
              "independent layer visibility and snapshot");
        layered.apply(raised, false);
        layered.heights.layers[0].enabled = true;
        check(layered.height(0, 0) == 17, "undo targets original layer after active layer changes");
        check(wb::saveProject(base + "/layers.wbench", layered, error) &&
                  wb::loadProject(base + "/layers.wbench", loaded, error) &&
                  loaded.heights.layers == layered.heights.layers && loaded.heights.activeLayer == second.id,
              "layer persistence");
        {
            auto legacy = wb::World::create(wb::WorldOptions{32, 42, -60, 10, 0, true});
            legacy.heights.instances = {a};
            check(wb::saveProject(base + "/legacy.wbench", legacy, error), "legacy fixture source");
            QFile source(base + "/legacy.wbench");
            source.open(QIODevice::ReadOnly);
            auto root = wb::projectMetadata(legacy, true);
            source.close();
            root["format"] = 4;
            root.remove("sculptLayers");
            root.remove("activeLayer");
            root["heightChanges"] = QJsonArray{QJsonValue(QJsonArray{legacy.column(0, 0), 35})};
            auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
            source.open(QIODevice::WriteOnly);
            source.write(QByteArray("WBENCH01", 8) +
                         QCryptographicHash::hash(data, QCryptographicHash::Sha256) + qCompress(data));
            source.close();
            const bool migrated = wb::loadProject(source.fileName(), loaded, error);
            if (!migrated)
                throw std::runtime_error(error.toStdString());
            check(loaded.height(0, 0) == 35, "v4 migration preserves visible heights");
            loaded.heights.instances[0].enabled = false;
            check(loaded.height(0, 0) == 15, "v4 migrated sculpt becomes relative to instances");
        }
        auto rotated = a;
        rotated.image = black;
        rotated.low = 10;
        rotated.high = 10;
        rotated.rotationX = 30;
        check(std::abs(rotated.apply(0, 0, 4) - (10 - 4 / std::sqrt(3.))) < 1e-4,
              "X rotation plane intersection");
        rotated.rotationX = 0;
        rotated.rotationZ = 30;
        check(std::abs(rotated.apply(0, 4, 0) - (10 + 4 / std::sqrt(3.))) < 1e-4,
              "Z rotation plane intersection");
        rotated.rotationZ = 0;
        rotated.rotationY = 90;
        rotated.width = 5;
        rotated.depth = 17;
        rotated.x = -2;
        check(rotated.apply(-5, 6, 0) == 10 && rotated.apply(-5, 0, 6) == -5,
              "Y rotation changes rectangular footprint");
        rotated.rotationY = 0;
        rotated.scaleX = 400;
        rotated.y = 7;
        check(rotated.apply(-5, 6, 0) == 17, "horizontal scaling and vertical translation");
        auto curved = std::make_shared<wb::HeightmapData>();
        curved->width = curved->depth = 17;
        for (int z = 0; z < 17; ++z)
            for (int x = 0; x < 17; ++x)
                curved->values.push_back(uint16_t((x * x + z * 17) * 70));
        auto accelerated = std::make_shared<wb::HeightmapData>(*curved);
        accelerated->buildBounds();
        rotated = a;
        rotated.image = curved;
        rotated.rotationX = 23;
        rotated.rotationY = 31;
        rotated.rotationZ = -17;
        auto fast = rotated;
        fast.image = accelerated;
        for (int z = -14; z <= 14; ++z)
            for (int x = -14; x <= 14; ++x)
                check(std::abs(fast.apply(-50, x, z) - rotated.apply(-50, x, z)) < 1e-7,
                      "hierarchy matches uncached bilinear ray intersection");
        auto json = wb::saveTerrain(world.heights.instances);
        auto entries = json["instances"].toArray();
        auto invalid = entries[0].toObject();
        invalid["image"] = 999;
        entries[0] = invalid;
        json["instances"] = entries;
        auto preserved = world.heights.instances;
        check(!wb::loadTerrain(json, preserved, error) && preserved == world.heights.instances,
              "bad image reference rejected atomically");
        json = wb::saveTerrain(world.heights.instances);
        auto images = json["images"].toArray();
        auto broken = images[0].toObject();
        broken["pixels"] = "AAAA";
        images[0] = broken;
        json["images"] = images;
        check(!wb::loadTerrain(json, preserved, error), "truncated pixels rejected");
        // Export a complete 32-square result for the independent Python NBT decoder.
        world = wb::World::create(wb::WorldOptions{32, 42, -60, 10, 0, true});
        world.heights.instances = {a};
        const auto hit = world.raycast({.5f, 100, .5f}, {0, -1, 0});
        check(hit.valid && hit.y == 30, "raycast uses composed height");
        auto mesh = wb::buildChunkMesh(world, 0, 0, 1);
        check(!mesh->opaque.empty(), "composed instance mesh builds");
        world.heights.instances[0].rotationX = 15;
        world.heights.instances[0].rotationY = 25;
        world.heights.instances[0].y = 4;
        world.heights.instances[0].scaleY = 120;
        world.heights.set(world.column(0, 0), world.height(0, 0) + 7);
        auto result = wb::exportMinecraft(world, base + "/terrain");
        check(result.ok && result.chunks == 4, "instance export");
        QJsonArray chunks, ids;
        for (int i = 0; i < wb::BlockCount; ++i)
            ids.append(wb::info(i).minecraft);
        for (int cz = -1; cz <= 0; ++cz)
            for (int cx = -1; cx <= 0; ++cx) {
                QByteArray blocks;
                for (int y = -64; y < 320; ++y)
                    for (int z = cz * 16; z < cz * 16 + 16; ++z)
                        for (int x = cx * 16; x < cx * 16 + 16; ++x)
                            blocks.append(char(world.block(x, y, z)));
                const auto name = QString("expected-%1-%2.bin").arg(cx).arg(cz);
                QFile f(base + "/terrain/" + name);
                check(f.open(QIODevice::WriteOnly) && f.write(blocks) == blocks.size(),
                      "independent source blocks");
                chunks.append(QJsonObject{{"x", cx}, {"z", cz}, {"file", name}});
            }
        QFile manifest(base + "/terrain/expected.json");
        check(manifest.open(QIODevice::WriteOnly), "export manifest");
        manifest.write(QJsonDocument(QJsonObject{{"chunks", chunks},
                                                 {"ids", ids},
                                                 {"area", QJsonArray{-16, -16, 15, 15}},
                                                 {"spawn", QJsonArray{0, 0}},
                                                 {"bounds", QJsonArray{-16, -16, 15, 15}}})
                           .toJson());
        // Human-test samples, generated from an analytic ridge rather than proprietary terrain data.
        QImage ridge(256, 256, QImage::Format_Grayscale16), mask(256, 256, QImage::Format_Grayscale8);
        for (int z = 0; z < 256; ++z)
            for (int x = 0; x < 256; ++x) {
                double dx = (x - 127.5) / 75, dz = (z - 127.5) / 55;
                reinterpret_cast<quint16 *>(ridge.scanLine(z))[x] =
                    quint16(std::lround(65535 * std::exp(-(dx * dx + dz * dz))));
                mask.scanLine(z)[x] = uchar(x);
            }
        check(ridge.save(base + "/ridge-16.png") && mask.save(base + "/mask-gradient.png"),
              "human-test PNG samples");
        std::cout << "PASS " << checks << " terrain checks; fixture=" << base.toStdString() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL terrain after " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
