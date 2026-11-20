#include "ReferenceMesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
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
        const auto output = app.arguments().value(1, temporary.path());
        check(QDir().mkpath(output), "fixture directory");
        QString error;
        auto white = std::make_shared<wb::HeightmapData>();
        white->width = white->depth = 2;
        white->values.assign(4, 65535);
        auto black = std::make_shared<wb::HeightmapData>(*white);
        black->values.assign(4, 0);
        wb::TerrainInstance smart;
        smart.id = "smart";
        smart.name = "智能混合";
        smart.image = white;
        smart.x = smart.z = -64;
        smart.width = smart.depth = 129;
        smart.low = 0;
        smart.high = 40;
        smart.blend = 4;
        smart.blendSmoothness = 16;
        smart.blendEdge = 16;
        check(smart.apply(24, 0, 0) == 40, "absolute target does not add base height");
        check(smart.apply(60, 0, 0) == 60, "higher underlying ground preserved beyond join");
        check(smart.apply(40, 0, 0) == 44, "equal surfaces have bounded four-block shoulder");
        for (double ground = -63; ground <= 318; ground += .5) {
            const double value = smart.apply(ground, 0, 0);
            const double maximum = std::max(ground, 40.);
            check(value >= maximum && value <= maximum + 4, "smooth max bounded and never cuts ground");
            if (std::abs(ground - 40) >= 16)
                check(value == maximum, "outside vertical join equals hard max");
        }
        const double epsilon = 1e-4;
        auto derivative = [&](double h) {
            return (smart.apply(h + epsilon, 0, 0) - smart.apply(h - epsilon, 0, 0)) / (2 * epsilon);
        };
        check(std::abs(derivative(24)) < 1e-5 && std::abs(derivative(40) - .5) < 1e-5 &&
                  std::abs(derivative(56) - 1) < 1e-5,
              "join slopes are continuous at both ends and crossing");
        smart.blendSmoothness = smart.blendEdge = 0;
        smart.feather = 50; // Legacy percentage is kept but inactive in smart mode.
        auto hard = smart;
        hard.blend = 2;
        hard.feather = 0;
        for (int z = -65; z <= 65; z += 5)
            for (int x = -65; x <= 65; x += 5)
                check(smart.apply(24, x, z) == hard.apply(24, x, z), "zero settings exactly match hard max");
        smart.blendSmoothness = 16;
        smart.blendEdge = 16;
        check(smart.apply(24, -64, 0) == 24 && smart.apply(24, -65, 0) == 24 &&
                  smart.apply(24, -56, 0) == 32 && smart.apply(24, -48, 0) == 40,
              "edge fade returns to ground within footprint");
        check(smart.apply(24, -56, -56) == 28, "corner combines both edge fades");
        auto small = smart;
        small.x = small.z = -1;
        small.width = small.depth = 3;
        small.blendEdge = 1024;
        check(small.apply(24, 0, 0) == 40 && small.apply(24, -1, 0) == 24,
              "small footprint retains center when edge distance is large");
        smart.mask = black;
        check(smart.apply(24, 0, 0) == 24, "black mask preserves terrain");
        smart.mask = white;
        smart.opacity = 50;
        check(smart.apply(24, 0, 0) == 32, "mask and opacity affect smart contribution");
        smart.opacity = 0;
        check(smart.apply(24, 0, 0) == 24, "zero opacity ignored");
        smart.opacity = 100;
        smart.enabled = false;
        check(smart.apply(24, 0, 0) == 24, "disabled ignored");
        smart.enabled = true;
        smart.mask.reset();
        auto rotated = smart;
        rotated.rotationX = 25;
        rotated.rotationY = 90;
        rotated.rotationZ = -15;
        rotated.scaleX = 160;
        rotated.scaleY = 120;
        auto maximum = rotated;
        maximum.blend = 2;
        maximum.feather = 0;
        rotated.blendEdge = 0;
        for (int z = -96; z <= 96; z += 8)
            for (int x = -96; x <= 96; x += 8) {
                const double result = rotated.apply(24, x, z), reference = maximum.apply(24, x, z);
                check(result >= reference - 1e-8 && result <= reference + 4 + 1e-8,
                      "rotated scaled target uses the same ray surface as hard max");
            }
        auto world = wb::World::create(wb::WorldOptions{32, 42, -60, 24, 0, true});
        auto instance = smart;
        instance.x = instance.z = -12;
        instance.width = instance.depth = 25;
        instance.high = 30;
        instance.blendEdge = 6;
        world.heights.instances = {instance};
        const int before = world.height(0, 0);
        check(before == 32, "authoritative height quantizes smooth shoulder once");
        world.heights.set(world.column(0, 0), before + 3);
        world.heights.instances[0].enabled = false;
        check(world.height(0, 0) == 27, "relative sculpt survives smart visibility change");
        world.heights.instances[0].enabled = true;
        world.setBlock(0, before + 3, 0, wb::Planks);
        const auto hit = world.raycast({.5f, 200, .5f}, {0, -1, 0});
        check(hit.valid && hit.y == before + 3 && hit.block == wb::Planks,
              "picking and override use same result");
        check(wb::saveProject(output + "/smart-roundtrip.wbench", world, error), "save smart project");
        wb::World loaded;
        check(wb::loadProject(output + "/smart-roundtrip.wbench", loaded, error) &&
                  loaded.heights.instances == world.heights.instances && loaded.heights == world.heights &&
                  loaded.overrides == world.overrides,
              "project roundtrip preserves parameters pixels layers and overrides");
        auto json = wb::saveTerrain({smart});
        std::vector<wb::TerrainInstance> decoded{smart};
        for (const auto &key : {"blendSmoothness", "blendEdge"}) {
            for (auto bad : {QJsonValue(-1), QJsonValue(1025), QJsonValue(.5), QJsonValue("16")}) {
                auto invalid = json;
                auto entries = invalid["instances"].toArray();
                auto entry = entries[0].toObject();
                entry[key] = bad;
                entries[0] = entry;
                invalid["instances"] = entries;
                check(!wb::loadTerrain(invalid, decoded, error) && decoded == std::vector{smart},
                      "invalid smart parameters rejected atomically");
            }
        }
        QFile project(output + "/smart-roundtrip.wbench");
        check(project.open(QIODevice::ReadOnly), "read project envelope");
        auto root = QJsonDocument::fromJson(qUncompress(project.readAll().mid(40))).object();
        project.close();
        check(root["format"].toInt() == 8, "new format prevents old clients silently dropping settings");
        auto writeRoot = [&] {
            const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
            check(project.open(QIODevice::WriteOnly), "write migration fixture");
            project.write(QByteArray("WBENCH01", 8) +
                          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) + qCompress(bytes));
            project.close();
        };
        root["format"] = 6;
        writeRoot();
        check(!wb::loadProject(project.fileName(), loaded, error) && loaded.heights == world.heights,
              "legacy version rejects unknown smart mode without replacing world");
        auto legacy = root["terrainInstances"].toObject();
        auto entries = legacy["instances"].toArray();
        auto entry = entries[0].toObject();
        entry["blend"] = 2;
        entry.remove("blendSmoothness");
        entry.remove("blendEdge");
        entries[0] = entry;
        legacy["instances"] = entries;
        root["terrainInstances"] = legacy;
        writeRoot();
        check(wb::loadProject(project.fileName(), loaded, error) && loaded.heights.instances[0].blend == 2 &&
                  loaded.heights.instances[0].blendSmoothness == 16 &&
                  loaded.heights.instances[0].blendEdge == 16,
              "v6 instances keep their old mode and gain inactive defaults");
        // Check all mesh chunks, then independently decode the exported world using verify_export.py.
        QJsonArray chunks, ids;
        for (int i = 0; i < wb::BlockCount; ++i)
            ids.append(wb::info(i).minecraft);
        const auto exportPath = output + "/smart-export";
        const auto exported = wb::exportMinecraft(world, exportPath);
        check(exported.ok && exported.chunks == 4, "smart export succeeds");
        for (int cz = -1; cz <= 0; ++cz)
            for (int cx = -1; cx <= 0; ++cx) {
                check(
                    sameMesh(wb::buildChunkMesh(world, cx, cz, 1), wb::referenceChunkMesh(world, cx, cz, 1)),
                    "smart chunk mesh matches exhaustive reference across negative and positive coordinates");
                QByteArray blocks;
                for (int y = -64; y < 320; ++y)
                    for (int z = cz * 16; z < cz * 16 + 16; ++z)
                        for (int x = cx * 16; x < cx * 16 + 16; ++x)
                            blocks.append(char(world.block(x, y, z)));
                const auto name = QString("expected-%1-%2.bin").arg(cx).arg(cz);
                QFile expected(exportPath + "/" + name);
                check(expected.open(QIODevice::WriteOnly) && expected.write(blocks) == blocks.size(),
                      "expected blocks");
                chunks.append(QJsonObject{{"x", cx}, {"z", cz}, {"file", name}});
            }
        QFile manifest(exportPath + "/expected.json");
        check(manifest.open(QIODevice::WriteOnly), "manifest");
        manifest.write(QJsonDocument(QJsonObject{{"chunks", chunks},
                                                 {"ids", ids},
                                                 {"area", QJsonArray{-16, -16, 15, 15}},
                                                 {"spawn", QJsonArray{0, 0}},
                                                 {"bounds", QJsonArray{-16, -16, 15, 15}}})
                           .toJson());
        manifest.close();
        // Portable analytic fixtures, without external or proprietary resources.
        auto hill = std::make_shared<wb::HeightmapData>();
        hill->width = hill->depth = 129;
        QImage image(129, 129, QImage::Format_Grayscale16);
        for (int z = 0; z < 129; ++z)
            for (int x = 0; x < 129; ++x) {
                const double dx = (x - 64) / 35., dz = (z - 64) / 28.;
                const auto value = uint16_t(std::lround(65535 * std::exp(-dx * dx - dz * dz)));
                hill->values.push_back(value);
                reinterpret_cast<quint16 *>(image.scanLine(z))[x] = value;
            }
        hill->buildBounds();
        check(image.save(output + "/hill-16.png"), "portable heightmap fixture");
        auto demo = wb::World::create(wb::WorldOptions{192, 42, -60, 24, 0, true});
        smart.image = hill;
        smart.high = 80;
        smart.blendSmoothness = 32;
        demo.heights.instances = {smart};
        check(wb::saveProject(output + "/smart-demo.wbench", demo, error), "smart demo");
        demo.heights.instances[0].blend = 2;
        demo.heights.instances[0].feather = 0;
        check(wb::saveProject(output + "/max-demo.wbench", demo, error), "hard max comparison");
        demo.heights.instances[0] = smart;
        demo.heights.instances[0].image = white;
        demo.heights.instances[0].high = 48;
        check(wb::saveProject(output + "/edge-demo.wbench", demo, error), "edge comparison");
        std::cout << "PASS " << checks << " smart blend checks; fixture=" << output.toStdString() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL smart blend after " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
