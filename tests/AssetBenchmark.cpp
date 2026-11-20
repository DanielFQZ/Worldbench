#include "ReferenceMesh.h"
#include "core/Project.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>

// Manual profiling harness: identical world data, no resource degradation or GPU timing claim.
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        if (app.arguments().size() != 4)
            throw std::runtime_error("Usage: worldbench_asset_benchmark base.jar overlay.zip output-dir");
        const QString output = app.arguments()[3];
        if (!QDir().mkpath(output))
            throw std::runtime_error("Cannot create output directory");
        QJsonArray results;
        for (int scenario = 0; scenario < 3; ++scenario) {
            const QString name = QStringList{"builtin", "vanilla", "overlay"}[scenario];
            auto world = wb::World::create(128);
            QString error;
            QElapsedTimer clock;
            clock.start();
            QJsonArray stages;
            if (scenario) {
                QStringList paths{app.arguments()[1]};
                if (scenario == 2)
                    paths << app.arguments()[2];
                auto assets = wb::AssetLibrary::load(paths, error, [&](int p, const QString &stage) {
                    if (p >= 70)
                        stages.append(
                            QJsonObject{{"percent", p}, {"ms", double(clock.elapsed())}, {"stage", stage}});
                });
                if (!assets)
                    throw std::runtime_error(error.toStdString());
                world.setAssets(assets);
            }
            QJsonObject result{{"name", name}, {"load_ms", double(clock.elapsed())}, {"load_stages", stages}};
            QJsonArray materials;
            for (wb::Block id : {wb::Grass, wb::Dirt, wb::Stone, wb::Sand, wb::Bedrock}) {
                const auto &m = *world.models[id];
                int unculled = 0;
                for (const auto &f : m.faces)
                    unculled += f.cull < 0;
                materials.append(QJsonObject{{"name", world.palette[id].name},
                                             {"faces", int(m.faces.size())},
                                             {"unculled_faces", unculled},
                                             {"full", m.full},
                                             {"translucent", m.translucent}});
            }
            result["materials"] = materials;
            if (world.assets) {
                result["atlas_width"] = world.assets->atlas.width();
                result["atlas_bytes"] = double(world.assets->atlas.sizeInBytes());
                result["textures"] = world.assets->thumbnails.size();
                result["skipped"] = world.assets->skippedEntries;
                result["unsupported"] = world.assets->unsupportedEntries;
            }
            QJsonArray samples;
            size_t opaque = 0, alpha = 0;
            for (int trial = 0; trial < 3; ++trial) {
                opaque = alpha = 0;
                clock.restart();
                for (int z = -4; z < 4; ++z)
                    for (int x = -4; x < 4; ++x) {
                        auto mesh = wb::buildChunkMesh(world, x, z, 1);
                        opaque += mesh->opaque.size();
                        alpha += mesh->transparent.size();
                    }
                samples.append(double(clock.elapsed()));
            }
            result["sequential_mesh_ms"] = samples;
            result["opaque_vertices"] = double(opaque);
            result["transparent_vertices"] = double(alpha);
            result["vertex_upload_bytes"] = double((opaque + alpha) * sizeof(wb::Vertex));
            for (auto chunk : {QPoint(-4, -4), QPoint(0, 0)})
                if (!sameMesh(wb::buildChunkMesh(world, chunk.x(), chunk.y(), 1),
                              wb::referenceChunkMesh(world, chunk.x(), chunk.y(), 1)))
                    throw std::runtime_error("Mesh differs from exhaustive reference");
            clock.restart();
            for (int n = 0; n < 1000; ++n)
                world.raycast({float(-50 + n % 100) + .25f, 200, float(-50 + n * 17 % 100) + .25f},
                              {0, -1, 0}, 320);
            result["downward_picks_1000_ms"] = double(clock.elapsed());
            if (!wb::saveProject(output + "/" + name + ".wbench", world, error))
                throw std::runtime_error(error.toStdString());
            results.append(result);
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
        }
        QFile file(output + "/benchmark.json");
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot write benchmark JSON");
        file.write(QJsonDocument(results).toJson());
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
