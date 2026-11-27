#include "core/World.h"
#include "core/WorldCreation.h"
#include "core/MinecraftExport.h"
#include "core/NbtPayload.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
QByteArray entityChunk(const QString &path, int cx, int cz) {
    QFile f(path + QString("/entities/r.%1.%2.mca").arg(wb::floorDiv(cx, 32)).arg(wb::floorDiv(cz, 32)));
    check(f.open(QIODevice::ReadOnly), "entity region present");
    const auto data = f.readAll();
    const int entry = ((cx % 32 + 32) % 32 + (cz % 32 + 32) % 32 * 32) * 4;
    const auto offset = (qFromBigEndian<quint32>(data.constData() + entry) >> 8) * 4096;
    check(offset >= 8192 && offset + 5 < quint32(data.size()), "entity chunk location valid");
    const auto size = qFromBigEndian<quint32>(data.constData() + offset);
    QByteArray framed(4, '\0');
    qToBigEndian<quint32>(1024 * 1024, framed.data());
    framed += data.mid(offset + 5, size - 1);
    const auto raw = qUncompress(framed);
    check(!raw.isEmpty(), "entity NBT decompresses");
    return raw;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        wb::StructureAsset asset;
        asset.id = "audit-asset"; asset.name = "Audit";
        asset.width = asset.height = asset.depth = 4;
        asset.blocks.push_back({1, 1, 1, "minecraft:stone"});
        const auto valid = asset.toJson();
        QString error;
        wb::StructureAsset decoded;
        check(wb::StructureAsset::fromJson(valid, decoded, error), "valid coordinates accepted");
        for (const auto &key : {"x", "y", "z"}) {
            for (const auto &value : QJsonArray{"invalid", 1.5, true, QJsonValue(), -1, 4, 1e20}) {
                auto bad = valid;
                auto blocks = bad["blocks"].toArray();
                auto block = blocks[0].toObject(); block[key] = value;
                blocks[0] = block; bad["blocks"] = blocks;
                check(!wb::StructureAsset::fromJson(bad, decoded, error), "malformed block coordinate rejected");
            }
        }
        for (const auto &key : {"width", "height", "depth", "groundBlend"}) {
            auto bad = valid; bad[key] = "invalid";
            check(!wb::StructureAsset::fromJson(bad, decoded, error), "non-numeric dimension rejected");
        }
        wb::CaveMarker marker;
        marker.id = "marker"; marker.assetId = asset.id; marker.kind = "portal";
        marker.position = marker.end = {2, 2, 2};
        auto json = marker.toJson(); json["end"] = QJsonArray{1, "invalid", 1};
        wb::CaveMarker parsed;
        check(!wb::CaveMarker::fromJson(json, parsed, error), "malformed cave endpoint rejected");
        json.remove("end");
        check(wb::CaveMarker::fromJson(json, parsed, error) && parsed.end == marker.position,
              "legacy missing endpoint retains position");
        wb::StructureAsset large;
        large.width = large.depth = 512; large.height = 61;
        int checkpoints = 0;
        const auto cancelled = wb::analyzeAssetCave(large, {}, [&] { return ++checkpoints < 3; });
        check(cancelled.cancelled && cancelled.labels.empty() && !cancelled.valid,
              "large cave analysis cancels without publishing partial labels");
        for (int side : {-1, 1}) {
            auto world = wb::World::create(wb::WorldOptions{32, 1, -60, 10, 0, true});
            auto generated = std::make_shared<wb::GeneratedCreation>(32, 32);
            const int coordinate = side < 0 ? -15 : 1, selected = side < 0 ? -16 : 0;
            const auto entity = wb::nbtText("id", "minecraft:armor_stand") +
                                wb::nbtVector("Pos", {float(coordinate), 12, float(coordinate)}) + QByteArray(1, '\0');
            generated->entities.push_back({{float(coordinate), 12, float(coordinate)}, entity});
            // An entity in the adjacent chunk must remain excluded.
            generated->entities.push_back({{float(side < 0 ? 1 : -1), 12, float(coordinate)},
                                          wb::nbtText("id", "minecraft:pig") + QByteArray(1, '\0')});
            world.generated = generated;
            auto partial = wb::exportMinecraft(world, temp.filePath(QString("partial%1").arg(side)),
                                                nullptr, {}, QRect(selected, selected, 1, 1));
            auto aligned = wb::exportMinecraft(world, temp.filePath(QString("aligned%1").arg(side)),
                                                nullptr, {}, QRect(selected, selected, 16, 16));
            check(partial.ok && aligned.ok && partial.chunks == 1 && aligned.chunks == 1,
                  "partial and aligned selections export the same chunk");
            const int c = side < 0 ? -1 : 0;
            auto raw = entityChunk(partial.path, c, c);
            check(raw == entityChunk(aligned.path, c, c) && raw.contains("minecraft:armor_stand") &&
                      !raw.contains("minecraft:pig"), "entities match effective chunk bounds");
        }
        std::cout << "PASS " << checks << " stability core checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " stability checks: " << e.what() << '\n';
        return 1;
    }
}
