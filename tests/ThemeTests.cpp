#include "ReferenceMesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include "core/ProjectFormat.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
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
        check(QDir().mkpath(base), "create fixtures");
        QString error;
        auto world = wb::World::create(wb::WorldOptions{32, 42, -60, 24, 0, true});
        auto &theme = world.theme;
        theme.enabled = true;
        theme.beaches = false;
        theme.bands = {{-63, "低地", wb::Planks, wb::Sand, 3}, {25, "山地", wb::Stone, wb::Dirt, 2}};
        check(world.block(0, 24, 0) == wb::Planks && world.block(0, 23, 0) == wb::Sand &&
                  world.block(0, 22, 0) == wb::Sand && world.block(0, 21, 0) == wb::Stone,
              "height band surface and total soil thickness");
        check(world.block(0, -64, 0) == wb::Bedrock && world.block(0, 25, 0) == wb::Air,
              "theme preserves bedrock and atmosphere");
        world.heights.set(world.column(0, 0), 25);
        check(world.block(0, 25, 0) == wb::Stone && world.block(0, 24, 0) == wb::Dirt,
              "sculpt crossing threshold selects new band without baking");
        world.setBlock(0, 25, 0, wb::Glass);
        world.setBlock(0, 23, 0, wb::Air);
        theme.bands[1].surface = wb::Sand;
        check(world.block(0, 25, 0) == wb::Glass && world.block(0, 23, 0) == wb::Air,
              "independent paint and cavities override theme");
        theme.beaches = true;
        world.seaLevel = 24;
        check(world.block(1, 24, 1) == wb::Sand && world.block(1, 21, 1) == wb::Sand,
              "beach rule overrides bands near and under water");
        theme.beaches = false;
        world.seaLevel = -60;
        theme.bands[1].surface = wb::Stone;
        theme.noise = 8;
        theme.scale = 4;
        std::vector<wb::Block> distribution;
        int stone = 0, planks = 0;
        for (int z = -16; z < 16; ++z)
            for (int x = -16; x < 16; ++x) {
                const auto b = world.block(x, 24, z);
                distribution.push_back(b);
                stone += b == wb::Stone;
                planks += b == wb::Planks;
                check(world.block(x, 24, z) == world.blockAtHeight(x, 24, z, world.height(x, z)),
                      "cached mesh/export height path matches direct world query");
            }
        check(stone > 0 && planks > 0, "noise creates both materials along boundary");
        auto copy = world;
        int different = 0, index = 0;
        copy.theme.seed += 1;
        for (int z = -16; z < 16; ++z)
            for (int x = -16; x < 16; ++x, ++index) {
                check(world.block(x, 24, z) == distribution[index] && copy.height(x, z) == world.height(x, z),
                      "noise reproducible and does not change geometry");
                different += copy.block(x, 24, z) != distribution[index];
            }
        check(different > 0, "changing seed changes boundary pattern");
        auto flattened = wb::World::create(wb::WorldOptions{32, 42, -60, 24, 0, true});
        flattened.theme.enabled = true;
        flattened.theme.beaches = false;
        flattened.theme.bands = {{-63, "low", wb::Grass, wb::Dirt, 4}, {25, "high", wb::Stone, wb::Dirt, 4}};
        flattened.heights.set(flattened.column(0, 0), 26);
        flattened.setBlock(1, 22, 0, wb::Air);
        wb::Stroke flattenStroke(flattened);
        wb::BrushOptions flatOptions;
        flatOptions.shape = 1;
        flatOptions.falloff = 1;
        flattenStroke.stamp({true, 0, 26, 0, {.5f, 27, .5f}, {0, 1, 0}, wb::Stone}, 1, 1, 4, wb::Stone, false,
                            flatOptions);
        check(flattened.height(1, 0) == 26 && flattened.block(1, 26, 0) == wb::Stone &&
                  flattened.block(1, 22, 0) == wb::Air,
              "flatten crossing theme threshold updates cap and preserves cavity");
        flattened.theme.bands[1].surface = wb::Planks;
        check(flattened.block(1, 26, 0) == wb::Planks, "flatten generated cap remains live theme material");
        const auto validTheme = world.theme;
        wb::SurfaceTheme parsed;
        auto json = wb::saveTheme(validTheme);
        check(wb::loadTheme(json, parsed, world.palette.size(), error) && parsed == validTheme,
              "theme serialization retains all fields");
        auto invalid = json;
        auto rows = invalid["bands"].toArray();
        auto row = rows[1].toObject();
        row["minHeight"] = -63;
        rows[1] = row;
        invalid["bands"] = rows;
        check(!wb::loadTheme(invalid, parsed, world.palette.size(), error) && parsed == validTheme,
              "overlapping thresholds rejected atomically");
        invalid = json;
        invalid["noise"] = 1.5;
        check(!wb::loadTheme(invalid, parsed, world.palette.size(), error), "fractional settings rejected");
        invalid = json;
        invalid["bands"] = QJsonArray{};
        check(!wb::loadTheme(invalid, parsed, world.palette.size(), error), "empty theme rejected");
        const auto project = base + "/theme.wbench";
        check(wb::saveProject(project, world, error), "save theme project");
        wb::World loaded;
        check(wb::loadProject(project, loaded, error) && loaded.theme == world.theme &&
                  loaded.overrides == world.overrides && loaded.heights == world.heights,
              "v6 theme project roundtrip");
        QFile input(project);
        check(input.open(QIODevice::ReadOnly), "read migration fixture");
        auto root = wb::projectMetadata(world, true);
        auto writeRoot = [&](QString path, const QJsonObject &object) {
            const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
            QFile file(path);
            check(file.open(QIODevice::WriteOnly), "write project fixture");
            file.write(QByteArray("WBENCH01", 8) +
                       QCryptographicHash::hash(data, QCryptographicHash::Sha256) + qCompress(data));
        };
        auto badRoot = root;
        badRoot["surfaceTheme"] = invalid;
        writeRoot(base + "/invalid.wbench", badRoot);
        check(!wb::loadProject(base + "/invalid.wbench", loaded, error) && loaded.theme == world.theme,
              "malformed project does not replace world");
        root["format"] = 5;
        root.remove("surfaceTheme");
        writeRoot(base + "/legacy-v5.wbench", root);
        check(wb::loadProject(base + "/legacy-v5.wbench", loaded, error) && !loaded.theme.enabled &&
                  loaded.heights == world.heights,
              "v5 migration preserves legacy appearance");
        for (int z = -1; z <= 0; ++z)
            for (int x = -1; x <= 0; ++x)
                check(sameMesh(wb::buildChunkMesh(world, x, z, 1), wb::referenceChunkMesh(world, x, z, 1)),
                      "themed culling matches exhaustive reference including cavities");
        auto custom = std::make_shared<wb::BlockModel>(*world.models[wb::Planks]);
        custom->faces[0].cull = -1;
        world.models[wb::Planks] = custom;
        check(sameMesh(wb::buildChunkMesh(world, 0, 0, 1), wb::referenceChunkMesh(world, 0, 0, 1)),
              "custom theme material disables unsafe buried culling");
        auto dynamic = world;
        dynamic.theme.bands[0].surface = dynamic.intern({"minecraft:gold_block", {}});
        check(!wb::exportMinecraft(dynamic, base + "/invalid-export").ok &&
                  !QFile::exists(base + "/invalid-export"),
              "dynamic material used only by theme still requires validated game resources");
        world.models[wb::Planks] = wb::fallbackModel(wb::Planks);
        for (int z = -16; z < 16; ++z)
            for (int x = -16; x < 16; ++x)
                world.heights.set(world.column(x, z), 22 + x + 16);
        theme.bands = {{-63, "低地", wb::Sand, wb::Sand, 4},
                       {30, "中段", wb::Grass, wb::Dirt, 4},
                       {44, "高地", wb::Planks, wb::Stone, 2}};
        check(wb::exportMinecraft(world, base + "/theme-export").ok, "export themed world");
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
                QFile file(base + "/theme-export/" + name);
                check(file.open(QIODevice::WriteOnly) && file.write(blocks) == blocks.size(),
                      "export source blocks");
                chunks.append(QJsonObject{{"x", cx}, {"z", cz}, {"file", name}});
            }
        QFile manifest(base + "/theme-export/expected.json");
        check(manifest.open(QIODevice::WriteOnly), "export manifest");
        manifest.write(QJsonDocument(QJsonObject{{"chunks", chunks},
                                                 {"ids", ids},
                                                 {"area", QJsonArray{-16, -16, 15, 15}},
                                                 {"spawn", QJsonArray{0, 0}},
                                                 {"bounds", QJsonArray{-16, -16, 15, 15}}})
                           .toJson());
        auto demo = wb::World::create(wb::WorldOptions{128, 42, -60, 40, 32, false});
        demo.theme = world.theme;
        demo.theme.bands[2].surface = wb::Stone;
        check(wb::saveProject(base + "/theme-demo.wbench", demo, error), "human theme demo");
        std::cout << "PASS " << checks << " theme checks; fixture=" << base.toStdString() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL theme after " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
