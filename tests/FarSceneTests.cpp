#include "core/FarMesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
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
            for (QPoint focus : {QPoint(0, 0), QPoint(w.originX, w.originZ),
                                 QPoint(w.originX + w.width - 1, w.originZ + w.depth - 1)}) {
                auto exact = exactChunks(w, focus.x(), focus.y());
                auto plan = wb::planFarTiles(w, exact, focus.x(), focus.y());
                std::vector<QRect> regions;
                const QRect bounds(w.originX, w.originZ, w.width, w.depth);
                for (int id : exact)
                    regions.push_back(
                        QRect(w.originX + id % w.chunksX() * 16, w.originZ + id / w.chunksX() * 16, 16, 16)
                            .intersected(bounds));
                for (const auto &t : plan)
                    regions.push_back(t.area);
                int64_t area = 0;
                for (size_t i = 0; i < regions.size(); ++i) {
                    check(bounds.contains(regions[i]), "tile stays inside rectangular map");
                    area += int64_t(regions[i].width()) * regions[i].height();
                    for (size_t j = 0; j < i; ++j)
                        check(!regions[i].intersects(regions[j]), "exact and far partitions do not overlap");
                }
                check(area == int64_t(w.width) * w.depth,
                      "partition covers every column with no blank rings");
                check(plan.size() < 1024, "bounded tile count independent of full chunk count");
            }
            auto plan = wb::planFarTiles(w, exactChunks(w, 0, 0), 0, 0);
            QElapsedTimer timer;
            timer.start();
            const auto inputs = wb::farInputs(w);
            if (assets)
                check(inputs.colors[wb::Grass] != wb::farInputs(fixture(w.width, w.depth)).colors[wb::Grass],
                      "far color summary follows real resource atlas");
            size_t vertices = 0;
            for (auto &t : plan) {
                t.mesh = wb::buildFarMesh(w, t, inputs, 1);
                const auto &v = t.mesh->opaque;
                vertices += v.size();
                check(v.size() <= 7680, "tile bounded to 16x16 columns and five faces each");
                for (size_t n = 0; n < v.size(); n += 6) {
                    const auto &a = v[n];
                    check(std::abs(a.nx) + std::abs(a.ny) + std::abs(a.nz) == 1,
                          "voxel normals remain axis aligned");
                    check(a.x >= t.area.left() && a.x <= t.area.right() + 1 && a.z >= t.area.top() &&
                              a.z <= t.area.bottom() + 1,
                          "far geometry remains in owned tile");
                }
            }
            auto elapsed = timer.elapsed();
            check(vertices < 8000000, "far vertex working set bounded");
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
                                       {"far_vertices", double(vertices)},
                                       {"far_mesh_mib", vertices * sizeof(wb::Vertex) / 1048576.},
                                       {"serial_far_ms", double(elapsed)},
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
                check(wb::visibleMesh(*t.mesh, projection * view), "whole map camera keeps every tile");
        }
        // Sparse edits between regular samples, removal of top blocks, and local
        // caves are resolved against the authoritative column at refinement.
        auto flat = wb::World::create({512, 1, -60, 24, 0, true, 512});
        wb::FarTile tile{QRect(0, 0, 64, 64), 4, {}};
        flat.setBlock(2, 150, 2, wb::Glass);
        auto mesh = wb::buildFarMesh(flat, tile, wb::farInputs(flat), 1);
        check(mesh->upper.y() == 151, "sparse translucent model contributes to distant envelope");
        flat.setBlock(2, 150, 2, wb::Air);
        mesh = wb::buildFarMesh(flat, tile, wb::farInputs(flat), 2);
        check(mesh->upper.y() == 25, "removed floating geometry no longer remains in far summary");
        flat.heights.restoreDelta(flat.heights.activeLayer, flat.column(2, 2), 100);
        mesh = wb::buildFarMesh(flat, tile, wb::farInputs(flat), 3);
        check(mesh->upper.y() == 125, "sparse sculpt spike between samples survives");
        auto exact = wb::buildChunkMesh(flat, 0, 0, 4, 15);
        check(exact->skirtMask == 15 && exact->lower.y() == wb::World::MinY,
              "exact boundary skirts span terrain depth");
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
        auto root = QJsonDocument::fromJson(qUncompress(bytes.mid(40))).object();
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
