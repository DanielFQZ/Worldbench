#include "core/MinecraftExport.h"
#include "core/Project.h"
#include "core/ProjectFormat.h"
#include "core/TerrainSurface.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMatrix4x4>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
std::set<int> exactChunks(const wb::World &w, int x, int z) {
    std::set<int> result;
    const int cx = (x - w.originX) / 16, cz = (z - w.originZ) / 16;
    for (int j = std::max(0, cz - 4); j < std::min(w.chunksZ(), cz + 5); ++j)
        for (int i = std::max(0, cx - 4); i < std::min(w.chunksX(), cx + 5); ++i)
            result.insert(j * w.chunksX() + i);
    return result;
}
wb::World fixture(int width, int depth) {
    auto w = wb::World::create({width, 2026, -60, 24, 12, false, depth});
    auto image = std::make_shared<wb::HeightmapData>();
    image->width = image->depth = 65;
    for (int z = 0; z < 65; ++z)
        for (int x = 0; x < 65; ++x) {
            double dx = (x - 32) / 18., dz = (z - 32) / 22.;
            image->values.push_back(uint16_t(65535 * std::exp(-dx * dx - dz * dz)));
        }
    image->buildBounds();
    for (int i = 0; i < 3; ++i) {
        wb::TerrainInstance t;
        t.id = QString("mountain-%1").arg(i);
        t.name = QString("山体实例 %1").arg(i + 1);
        t.sourceName = "procedural-hill.png";
        t.image = image;
        t.width = width / 3;
        t.depth = depth / 3;
        t.x = w.originX + width * (i + 1) / 4 - t.width / 2;
        t.z = w.originZ + depth * (i == 1 ? 3 : 1) / 4 - t.depth / 2;
        t.low = 24;
        t.high = 250 - i * 30;
        t.blend = 4;
        t.blendEdge = 128;
        t.blendSmoothness = 24;
        t.rotationY = i * 23;
        w.heights.instances.push_back(t);
    }
    w.heights.restoreDelta(w.heights.activeLayer, w.column(w.originX + 73, w.originZ + 91), 80);
    w.setBlock(w.originX + 121, 300, w.originZ + 71, wb::Planks);
    w.environment.enabled = true;
    return w;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    int checks = 0;
    auto check = [&](bool b, const char *message) {
        ++checks;
        if (!b)
            throw std::runtime_error(message);
    };
    try {
        const QString output = app.arguments().value(1, temp.path());
        QDir().mkpath(output);
        QString error;
        QJsonArray reports;
        std::shared_ptr<const wb::AssetLibrary> assets;
        qint64 assetMs = 0;
        if (app.arguments().size() >= 4) {
            QElapsedTimer load;
            load.start();
            assets = wb::AssetLibrary::load({app.arguments()[2], app.arguments()[3]}, error);
            check(bool(assets), "load real jar and resource pack");
            assetMs = load.elapsed();
        }
        for (auto size : {QSize(8192, 8192), QSize(16384, 16384), QSize(16384, 12289), QSize(257, 513)}) {
            auto w = fixture(size.width(), size.height());
            if (assets)
                w.setAssets(assets);
            auto plan = wb::planSurfaceTiles(w, std::max(w.width, w.depth) > 4096 ? 256 : 128);
            int64_t area = 0;
            const QRect bounds(w.originX, w.originZ, w.width - 1, w.depth - 1);
            std::set<std::pair<int, int>> origins;
            for (const auto &tile : plan) {
                check(bounds.contains(tile.area),
                      "surface interval tile stays inside rectangular source lattice");
                check(origins.emplace(tile.area.x(), tile.area.y()).second, "unique regular tile origin");
                area += int64_t(tile.area.width()) * tile.area.height();
            }
            check(area == int64_t(w.width - 1) * (w.depth - 1), "complete reference lattice coverage");
            QElapsedTimer timer;
            timer.start();
            const auto inputs = wb::surfaceMaterials(w);
            if (assets)
                check(inputs.colors[wb::Grass] !=
                          wb::surfaceMaterials(fixture(w.width, w.depth)).colors[wb::Grass],
                      "surface colors follow loaded resources");
            size_t vertices = 0;
            std::set<size_t> samples{0, plan.size() / 2, plan.size() - 1};
            for (size_t i : samples) {
                auto &tile = plan[i];
                tile.mesh = wb::buildSurfaceMesh(w, tile, inputs, 1, 1.f);
                vertices += tile.mesh->opaqueIndices.size();
                check(tile.mesh->surfaceError <= 1, "sampled surface meets reference error bound");
                check(tile.mesh->sampleCount == uint64_t(tile.area.width() + 1) * (tile.area.height() + 1),
                      "every sample in each evaluated patch participates");
            }
            auto elapsed = timer.elapsed();
            std::vector<qint64> pickTimes;
            for (int i = 0; i < 200; ++i) {
                const int x = w.originX + (i * 7919 + 13) % w.width,
                          z = w.originZ + (i * 104729 + 37) % w.depth;
                int top = wb::World::MaxY - 1;
                while (top > wb::World::MinY && w.block(x, top, z) == wb::Air)
                    --top;
                timer.restart();
                const auto hit = w.raycast({x + .5f, 35000, z + .5f}, {0, -1, 0}, 40000);
                pickTimes.push_back(timer.nsecsElapsed());
                check(hit.valid && hit.x == x && hit.z == z && hit.y == top,
                      "overview picks exact composed top across whole map");
            }
            std::sort(pickTimes.begin(), pickTimes.end());
            timer.restart();
            const auto emptyRay = w.raycast({float(w.originX - 100), 319.5f, .5f}, {1, 0, 0}, 20000);
            const auto horizontalMicros = timer.nsecsElapsed() / 1000.;
            check(!emptyRay.valid, "long horizontal empty ray does not select a coarse envelope");
            reports.append(QJsonObject{{"asset_load_ms", double(assetMs)},
                                       {"width", w.width},
                                       {"depth", w.depth},
                                       {"tiles", int(plan.size())},
                                       {"sampled_surface_indices", double(vertices)},
                                       {"evaluated_tiles", int(samples.size())},
                                       {"sampled_surface_ms", double(elapsed)},
                                       {"pick_vertical_p95_us", pickTimes[189] / 1000.},
                                       {"pick_horizontal_miss_us", horizontalMicros}});
            auto path = output + QString("/scene-%1x%2.wbench").arg(w.width).arg(w.depth);
            check(wb::saveProject(path, w, error), "save large fixture");
            wb::World loaded;
            check(wb::loadProject(path, loaded, error) && loaded.environment == w.environment &&
                      loaded.heights == w.heights && loaded.overrides == w.overrides,
                  "large sparse project and environment round trip");
            auto hit = w.raycast({float(w.originX + 121) + .5f, 20000, float(w.originZ + 71) + .5f},
                                 {0, -1, 0}, 40000);
            check(hit.valid && hit.y == 300 && hit.block == wb::Planks,
                  "authoritative distant pick includes explicit structure");
            QMatrix4x4 view, projection;
            view.lookAt({0, 20000, 10000}, {0, 24, 0}, {0, 1, 0});
            projection.perspective(60, 1.6f, 20, 60000);
            for (const auto &t : plan)
                if (t.mesh)
                    check(wb::visibleMesh(*t.mesh, projection * view),
                          "whole map camera keeps evaluated tiles");
        }
        auto flat = wb::World::create({512, 1, -60, 24, 0, true, 512});
        wb::FarTile tile{QRect(0, 0, 64, 64), 64, {}};
        flat.setBlock(2, 150, 2, wb::Glass);
        auto mesh = wb::buildSurfaceMesh(flat, tile, wb::surfaceMaterials(flat), 1);
        check(mesh->upper.y() == 25, "non-heightfield structure does not spread into a distant envelope");
        auto exact = wb::buildChunkMesh(flat, 0, 0, 2);
        check(exact->transparent.size() > 0, "real translucent structure retained in exact geometry");
        flat.setBlock(2, 150, 2, wb::Air);
        flat.heights.restoreDelta(flat.heights.activeLayer, flat.column(2, 2), 100);
        mesh = wb::buildSurfaceMesh(flat, tile, wb::surfaceMaterials(flat), 3);
        check(mesh->upper.y() == 125, "single-column sculpt spike retained without sparse sampling");
        auto extended = std::make_shared<wb::BlockModel>(*flat.models[wb::Planks]);
        for (auto &face : extended->faces)
            for (auto &point : face.points)
                point.setX(point.x() + 8);
        flat.models[wb::Planks] = extended;
        flat.setBlock(15, 200, 0, wb::Planks);
        auto overhang = wb::buildChunkMesh(flat, 0, 0, 5);
        check(overhang->upper.x() >= 24, "culling bounds include overhanging JSON elements");
        QMatrix4x4 awayView, awayProjection;
        awayView.lookAt({100, 200, 100}, {200, 200, 200}, {0, 1, 0});
        awayProjection.perspective(60, 1.f, .1f, 1000);
        check(!wb::visibleMesh(*overhang, awayProjection * awayView), "offscreen mesh is culled");
        wb::Environment e, next;
        auto original = e;
        check(wb::patchEnvironment(e, {{"enabled", true}, {"azimuth", 90}, {"elevation", 0}}, next, error),
              "environment patch");
        check((next.sunDirection() - QVector3D(1, 0, 0)).length() < .0001,
              "sun orientation matches world axes");
        for (const auto &patch :
             {QJsonObject{{"elevation", 91}}, QJsonObject{{"sun", 1}}, QJsonObject{{"sunColor", "red"}},
              QJsonObject{{"haze", 1.5}}, QJsonObject{{"unknown", true}}})
            check(!wb::patchEnvironment(e, patch, next, error) && e == original,
                  "invalid environment cannot mutate source");
        const auto projectPath = output + "/scene-8192x8192.wbench";
        QFile originalFile(projectPath);
        originalFile.open(QIODevice::ReadOnly);
        auto bytes = originalFile.readAll();
        wb::World savedWorld;
        check(wb::loadProject(projectPath, savedWorld, error),
              "read current indexed project for migration fixture");
        auto root = wb::projectMetadata(savedWorld, true);
        auto writeRoot = [&](const QJsonObject &object, const QString &path) {
            auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray("WBENCH01", 8) + QCryptographicHash::hash(data, QCryptographicHash::Sha256) +
                    qCompress(data));
        };
        auto legacy = root;
        legacy["format"] = 7;
        legacy.remove("environment");
        writeRoot(legacy, output + "/legacy-v7.wbench");
        wb::World migration;
        check(wb::loadProject(output + "/legacy-v7.wbench", migration, error) &&
                  !migration.environment.enabled,
              "v7 migrates to neutral editing illumination");
        auto invalid = root;
        invalid["environment"] = QJsonObject{{"enabled", true}};
        writeRoot(invalid, output + "/invalid-environment.wbench");
        auto preserved = migration;
        check(!wb::loadProject(output + "/invalid-environment.wbench", migration, error) &&
                  migration.heights == preserved.heights && migration.environment == preserved.environment,
              "invalid environment does not replace loaded project");
        auto small = wb::World::create({32, 1, -60, 24, 0, true, 32});
        auto before = small;
        wb::Command command;
        command.environmentBefore = std::make_shared<wb::Environment>(small.environment);
        small.environment.enabled = true;
        small.environment.sunIntensity = 180;
        command.environmentAfter = std::make_shared<wb::Environment>(small.environment);
        for (int z = small.originZ; z < small.originZ + 32; ++z)
            for (int x = small.originX; x < small.originX + 32; ++x)
                for (int y = -64; y < 320; ++y)
                    check(small.block(x, y, z) == before.block(x, y, z),
                          "lighting leaves authoritative voxels identical");
        small.apply(command, false);
        check(small.environment == before.environment, "environment undo");
        small.apply(command, true);
        check(small.environment.enabled && small.environment.sunIntensity == 180, "environment redo");
        check(wb::exportMinecraft(before, output + "/unlit-export").ok &&
                  wb::exportMinecraft(small, output + "/lit-export").ok,
              "lit and unlit exports");
        for (const QString &name : QDir(output + "/unlit-export/region").entryList({"*.mca"}, QDir::Files)) {
            QFile a(output + "/unlit-export/region/" + name), b(output + "/lit-export/region/" + name);
            a.open(QIODevice::ReadOnly);
            b.open(QIODevice::ReadOnly);
            check(a.readAll().mid(8192) == b.readAll().mid(8192),
                  "lighting leaves exported region payload identical");
        }
        QFile report(output + "/benchmark.json");
        report.open(QIODevice::WriteOnly);
        report.write(QJsonDocument(reports).toJson());
        std::cout << "PASS far scenes/environment " << checks << " checks\n"
                  << QJsonDocument(reports).toJson().constData();
    } catch (const std::exception &e) {
        std::cerr << "FAIL far scene after " << checks << ": " << e.what() << '\n';
        return 1;
    }
    return 0;
}
