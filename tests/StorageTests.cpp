#include "core/ProjectFormat.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <random>
#include <stdexcept>

static int checks = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read fixture");
    return f.readAll();
}
static void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write fixture");
}
static QByteArray hash(const QByteArray &raw) {
    return QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const auto output = app.arguments().value(1, temporary.path());
    QDir().mkpath(output);
    try {
        std::mt19937 random(20261122);
        wb::HeightDeltas deltas;
        std::map<int, int> reference;
        for (int n = 0; n < 12000; ++n) {
            const int index = int(random() % 100000), value = int(random() % 21) - 10;
            deltas.set(index, value);
            if (value)
                reference[index] = value;
            else
                reference.erase(index);
            check(deltas.get(index) == value && deltas.size() == reference.size(), "random delta update");
        }
        auto it = reference.begin();
        for (auto [index, value] : deltas) {
            check(it != reference.end() && it->first == index && it->second == value,
                  "sorted sparse iteration");
            ++it;
        }
        check(it == reference.end(), "all nonzero entries visited");
        const auto old = deltas;
        deltas.set(255, 19);
        deltas.set(256, 20);
        deltas.set(16383, 21);
        deltas.set(16384, 22);
        check(old.get(16384) != deltas.get(16384) && old.size() == reference.size(),
              "page and group boundary COW");
        for (auto [index, value] : old)
            deltas.set(index, 0);
        for (int index : {255, 256, 16383, 16384})
            deltas.set(index, 0);
        check(deltas.empty() && deltas.pageCount() == 0 && deltas.groups().empty(),
              "remove empty pages and groups");
        check(!deltas.insert(-1, 2) && !deltas.insert(0, 0) && deltas.insert(0, 4) && !deltas.insert(0, 5),
              "strict insert");

        QByteArray decoded;
        const auto raw = QByteArray(1024 * 1024, 'x');
        auto packed = qCompress(raw);
        check(wb::unpackProjectBlock(packed, raw.size(), decoded) && decoded == raw, "bounded inflate valid");
        qToBigEndian<uint32_t>(16, packed.data());
        check(!wb::unpackProjectBlock(packed, 16, decoded) && decoded.isEmpty(),
              "false small size cannot expand output");
        check(!wb::unpackProjectBlock(qCompress(raw) + "junk", raw.size(), decoded),
              "trailing deflate rejected");
        check(!wb::unpackProjectBlock(qCompress(raw).chopped(1), raw.size(), decoded),
              "truncated deflate rejected");

        QString error;
        auto world = wb::World::create({257, 42, -60, 24, 0, true, 513});
        std::vector<int16_t> dense(size_t(world.width) * world.depth);
        for (size_t n = 0; n < dense.size(); ++n)
            dense[n] = int16_t(n % 50);
        world.heights.assign(std::move(dense));
        auto makeImage = [&](int width, int depth) {
            auto image = std::make_shared<wb::HeightmapData>();
            image->width = width;
            image->depth = depth;
            image->values.resize(size_t(width) * depth);
            for (auto &sample : image->values)
                sample = uint16_t(random());
            image->buildBounds();
            return image;
        };
        for (int n = 0; n < 3; ++n) {
            wb::TerrainInstance t;
            t.id = QString("large-%1").arg(n);
            t.name = t.id;
            t.image = makeImage(2048, 2048);
            t.blend = 4;
            world.heights.instances.push_back(t);
        }
        world.heights.instances[1].mask = world.heights.instances[0].image;
        auto clone = world.heights.instances[0];
        clone.id = "shared";
        world.heights.instances.push_back(clone);
        check(wb::validateTerrain(world.heights.instances, error),
              "multiple images exceed old total pixel cap");
        world.heights.restoreDelta(world.heights.activeLayer, 0, 12);
        world.heights.restoreDelta(world.heights.activeLayer, 256, -24);
        world.heights.restoreDelta(world.heights.activeLayer, 257 * 513 - 1, 2);
        wb::SculptLayer layer;
        layer.id = "layer-2";
        layer.name = "second";
        layer.enabled = false;
        layer.deltas->set(16384, 17);
        world.heights.layers.push_back(layer);
        world.setBlock(0, 140, 0, wb::Planks);
        world.setBlock(1, 1, 1, wb::Air);
        world.environment.enabled = true;
        world.environment.haze = 67;
        QElapsedTimer timer;
        timer.start();
        const auto path = output + "/large-indexed.wbench";
        check(wb::saveProject(path, world, error), "large indexed save");
        const auto saveMs = timer.elapsed();
        timer.restart();
        wb::World loaded;
        check(wb::loadProject(path, loaded, error), "large indexed read");
        const auto loadMs = timer.elapsed();
        check(loaded.heights == world.heights && loaded.overrides == world.overrides &&
                  loaded.environment == world.environment,
              "image mask dense sculpt voxel environment roundtrip");
        check(loaded.heights.instances[0].image == loaded.heights.instances[1].mask &&
                  loaded.heights.instances[3].image == loaded.heights.instances[0].image,
              "shared image restored once");
        check(QFileInfo(path).size() > 16 * 1024 * 1024, "incompressible file exceeds old 16 MiB limit");
        const auto originalBytes = read(path);
        const auto manifestOffset = qFromLittleEndian<uint64_t>(originalBytes.constData() + 8);
        check(wb::unpackProjectBlock(originalBytes.mid(manifestOffset), 16 * 1024 * 1024, decoded),
              "read indexed manifest");
        const auto manifest = QJsonDocument::fromJson(decoded).object();
        const auto meta = manifest["world"].toObject();
        check(meta["format"] == 9 && meta["heights"].toArray().isEmpty() &&
                  !meta["terrainInstances"].toObject()["images"].toArray()[0].toObject().contains("pixels"),
              "metadata excludes binary/base64 payload");
        const auto unchanged = loaded;
        auto rejectBytes = [&](QByteArray bytes) {
            const auto broken = output + "/corrupt.wbench";
            write(broken, bytes);
            check(!wb::loadProject(broken, loaded, error) && loaded.heights == unchanged.heights &&
                      loaded.overrides == unchanged.overrides && loaded.environment == unchanged.environment,
                  "bad file leaves world untouched");
        };
        auto mutateManifest = [&](QJsonObject next) {
            const auto json = QJsonDocument(next).toJson(QJsonDocument::Compact), zip = qCompress(json);
            auto bytes = originalBytes.left(manifestOffset);
            qToLittleEndian<uint32_t>(uint32_t(zip.size()), bytes.data() + 16);
            bytes.replace(20, 32, hash(json));
            bytes += zip;
            rejectBytes(bytes);
        };
        rejectBytes(originalBytes.chopped(7));
        auto corrupt = originalBytes;
        corrupt[60] = char(corrupt[60] ^ 0x20);
        rejectBytes(corrupt);
        for (const auto &change :
             {QJsonObject{{"offset", 53}}, QJsonObject{{"raw", 1048577}}, QJsonObject{{"kind", "future"}},
              QJsonObject{{"sha256", "bad"}}, QJsonObject{{"x", 1}}, QJsonObject{{"asset", 64}}}) {
            auto next = manifest;
            auto blocks = next["blocks"].toArray();
            auto first = blocks[0].toObject();
            for (auto i = change.begin(); i != change.end(); ++i)
                first[i.key()] = i.value();
            blocks[0] = first;
            next["blocks"] = blocks;
            mutateManifest(next);
        }
        {
            auto next = manifest;
            auto blocks = next["blocks"].toArray();
            blocks.removeLast();
            next["blocks"] = blocks;
            mutateManifest(next);
        }
        {
            auto next = manifest;
            auto blocks = next["blocks"].toArray();
            auto second = blocks[1].toObject();
            second["x"] = 0;
            blocks[1] = second;
            next["blocks"] = blocks;
            mutateManifest(next);
        }
        {
            auto next = manifest;
            auto m = meta;
            auto t = m["terrainInstances"].toObject();
            auto images = t["images"].toArray();
            auto first = images[0].toObject();
            first["pixels"] = "duplicate";
            images[0] = first;
            t["images"] = images;
            m["terrainInstances"] = t;
            next["world"] = m;
            mutateManifest(next);
        }

        // Migration must leave a byte-identical, independently loadable old-format backup.
        auto small = wb::World::create({32, 1, -60, 24, 0, true, 32});
        const auto oldRaw = QJsonDocument(wb::projectMetadata(small, true)).toJson(QJsonDocument::Compact);
        const auto oldFile = QByteArray("WBENCH01", 8) + hash(oldRaw) + qCompress(oldRaw);
        const auto migration = output + "/legacy.wbench";
        write(migration, oldFile);
        check(wb::loadProject(migration, loaded, error) && loaded.heights == small.heights, "read legacy v8");
        check(wb::saveProject(migration, small, error) && read(output + "/legacy.pre-v9.wbench") == oldFile &&
                  read(migration).left(8) == "WBENCH09",
              "legacy backup before v9 replacement");
        write(migration, oldFile);
        check(wb::saveProject(migration, small, error) && read(output + "/legacy.pre-v9-1.wbench") == oldFile,
              "never overwrite earlier backup");
        const auto locked = output + "/blocked.wbench";
        write(locked, oldFile);
        for (int n = 0; n < 100; ++n)
            write(output + "/blocked.pre-v9" + (n ? QString("-%1").arg(n) : QString()) + ".wbench",
                  "occupied");
        check(!wb::saveProject(locked, small, error) && read(locked) == oldFile,
              "failed backup preserves original target");

        // Real maximum-edge PNG, retained 16-bit samples, bounds and aggregate rejection.
        QImage png(8192, 8192, QImage::Format_Grayscale16);
        for (int z = 0; z < 8192; ++z) {
            auto row = reinterpret_cast<quint16 *>(png.scanLine(z));
            for (int x = 0; x < 8192; ++x)
                row[x] = quint16((x * 7 + z * 13) % 65536);
        }
        check(png.save(output + "/8k-gray16.png"), "write 8K 16-bit PNG");
        png = {};
        timer.restart();
        const auto large = wb::loadHeightmap(output + "/8k-gray16.png", error);
        const auto pngMs = timer.elapsed();
        check(large && large->width == 8192 && large->values[8192 + 1] == 20 &&
                  large->values.back() == quint16(8191 * 20),
              "8K precise PNG conversion");
        wb::TerrainInstance big;
        big.id = "8k";
        big.name = "8K";
        big.image = large;
        check(wb::validateTerrain({big}, error) && wb::terrainMemoryBytes({big}) <= wb::MaxTerrainBytes,
              "8K values and ray bounds fit declared budget");
        check(!wb::validateTerrain({big, world.heights.instances[0]}, error),
              "reject excess aggregate with clear limit");
        auto shared = big;
        shared.id = "same";
        shared.mask = large;
        check(wb::validateTerrain({big, shared}, error) &&
                  wb::terrainMemoryBytes({big, shared}) == wb::terrainMemoryBytes({big}),
              "shared maximum asset and mask charged once");
        small.heights.instances = {big, shared};
        check(wb::saveProject(output + "/8k-embedded.wbench", small, error) &&
                  wb::loadProject(output + "/8k-embedded.wbench", loaded, error) &&
                  loaded.heights.instances[0].image->values == large->values &&
                  loaded.heights.instances[1].mask == loaded.heights.instances[0].image,
              "128 MiB embedded 8K image exceeds legacy raw limit and reopens exactly");
        QImage wide(8193, 1, QImage::Format_Grayscale8);
        wide.fill(0);
        check(wide.save(output + "/too-wide.png"), "write oversize fixture");
        check(!wb::loadHeightmap(output + "/too-wide.png", error), "reject over-edge PNG before conversion");
        std::cout << "Storage checks: " << checks << "; 3x2K random file " << originalBytes.size()
                  << " bytes; save " << saveMs << " ms, load " << loadMs << " ms; 8K PNG " << pngMs
                  << " ms, resident " << wb::terrainMemoryBytes({big}) << " bytes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Storage failed at " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
