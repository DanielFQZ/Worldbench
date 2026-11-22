#include "ProjectFormat.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <set>

namespace wb {
namespace {
constexpr qint64 HeaderSize = 52, MaxFile = 1ll << 30, MaxManifest = 16ll << 20, MaxBlock = 1ll << 20;
constexpr int Tile = 256, MaxRecords = 200000;
bool integer(QJsonValue value, qint64 low, qint64 high) {
    const double n = value.toDouble(-1);
    return value.isDouble() && std::isfinite(n) && n == std::floor(n) && n >= low && n <= high;
}
QByteArray digest(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool backupLegacy(const QString &path, QString &error) {
    QFile original(path);
    if (!original.exists())
        return true;
    if (!original.open(QIODevice::ReadOnly)) {
        error = original.errorString();
        return false;
    }
    if (original.read(8) != "WBENCH01")
        return true;
    original.seek(0);
    QString backup;
    for (int n = 0; n < 100; ++n) {
        backup = path.left(path.endsWith(".wbench", Qt::CaseInsensitive) ? path.size() - 7 : path.size()) +
                 ".pre-v9" + (n ? QString("-%1").arg(n) : QString()) + ".wbench";
        if (!QFile::exists(backup))
            break;
        backup.clear();
    }
    if (backup.isEmpty()) {
        error = "旧格式迁移备份名称已用尽，请另存到新路径";
        return false;
    }
    QSaveFile file(backup);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    while (!original.atEnd()) {
        const auto data = original.read(1 << 20);
        if (data.isEmpty() || file.write(data) != data.size()) {
            error = "旧格式备份失败，未覆盖工程";
            return false;
        }
    }
    if (!file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
} // namespace

bool saveProject(const QString &path, const World &world, QString &error) {
    try {
        Environment environment;
        if (!validateTerrain(world.heights.instances, error) ||
            !validateTheme(world.theme, world.palette.size(), error) ||
            !loadEnvironment(saveEnvironment(world.environment), environment, error))
            return false;
        QSaveFile file(path);
        auto fail = [&](QString message) {
            error = message;
            return false;
        };
        if (!file.open(QIODevice::WriteOnly))
            return fail(file.errorString());
        if (file.write(QByteArray(HeaderSize, '\0')) != HeaderSize)
            return fail(file.errorString());
        QJsonArray blocks;
        uint64_t totalRaw = 0;
        auto write = [&](QJsonObject entry, const QByteArray &raw) {
            totalRaw += raw.size();
            if (raw.isEmpty() || raw.size() > MaxBlock || totalRaw > MaxFile || blocks.size() >= MaxRecords)
                return fail("工程分块数据超过当前1 GiB/200000块预算，未覆盖原文件");
            const auto compressed = qCompress(raw, 6);
            if (file.pos() + compressed.size() > MaxFile)
                return fail("工程超过1 GiB文件预算");
            entry["offset"] = double(file.pos());
            entry["stored"] = compressed.size();
            entry["raw"] = raw.size();
            entry["sha256"] = QString::fromLatin1(digest(raw).toHex());
            if (file.write(compressed) != compressed.size())
                return fail(file.errorString());
            blocks.append(entry);
            return true;
        };
        auto writeGrid = [&](const auto &values, int width, int depth, QJsonObject base) {
            if (values.size() != size_t(width) * depth)
                return fail("工程图像/基底大小不匹配");
            for (int z = 0; z < depth; z += Tile)
                for (int x = 0; x < width; x += Tile) {
                    const int w = std::min(Tile, width - x), h = std::min(Tile, depth - z);
                    QByteArray raw(w * h * 2, Qt::Uninitialized);
                    for (int dz = 0; dz < h; ++dz)
                        for (int dx = 0; dx < w; ++dx)
                            qToLittleEndian<uint16_t>(uint16_t(values[size_t(z + dz) * width + x + dx]),
                                                      raw.data() + (dz * w + dx) * 2);
                    auto entry = base;
                    entry["x"] = x;
                    entry["z"] = z;
                    entry["width"] = w;
                    entry["depth"] = h;
                    if (!write(entry, raw))
                        return false;
                }
            return true;
        };
        std::set<const HeightmapData *> seen;
        int asset = 0;
        for (const auto &instance : world.heights.instances)
            for (const auto &image : {instance.image, instance.mask})
                if (image && seen.insert(image.get()).second)
                    if (!writeGrid(image->values, image->width, image->depth,
                                   {{"kind", "image"}, {"asset", asset++}}))
                        return false;
        if (world.heights.dense &&
            !writeGrid(*world.heights.dense, world.width, world.depth, {{"kind", "dense"}}))
            return false;
        uint64_t deltaBytes = 0;
        for (size_t layer = 0; layer < world.heights.layers.size(); ++layer) {
            const auto &store = *world.heights.layers[layer].deltas;
            deltaBytes += store.allocatedBytes();
            if (deltaBytes > 512ull * 1024 * 1024)
                return fail("雕刻数据超过512 MiB分块驻留预算");
            for (const auto &[groupId, group] : store.groups()) {
                QByteArray raw(2 + qsizetype(group->pages.size()) * (2 + HeightDeltas::PageColumns * 4),
                               Qt::Uninitialized);
                qToLittleEndian<uint16_t>(uint16_t(group->pages.size()), raw.data());
                int pos = 2;
                for (const auto &[pageId, page] : group->pages) {
                    qToLittleEndian<uint16_t>(uint16_t(pageId), raw.data() + pos);
                    pos += 2;
                    for (int value : page->values) {
                        qToLittleEndian<int32_t>(value, raw.data() + pos);
                        pos += 4;
                    }
                }
                if (!write({{"kind", "deltas"}, {"layer", int(layer)}, {"group", groupId}}, raw))
                    return false;
            }
        }
        if (world.overrides.size() > 4 * 1024 * 1024)
            return fail("独立体素修改超过当前4M格存储预算");
        QByteArray cells;
        auto flush = [&] {
            if (cells.isEmpty())
                return true;
            const bool ok = write({{"kind", "cells"}}, cells);
            cells.clear();
            return ok;
        };
        for (auto [key, block] : world.overrides) {
            const auto pos = cells.size();
            cells.resize(pos + 12);
            qToLittleEndian<uint64_t>(key, cells.data() + pos);
            qToLittleEndian<uint32_t>(block, cells.data() + pos + 8);
            if (cells.size() >= 8192 * 12 && !flush())
                return false;
        }
        if (!flush())
            return false;
        const QJsonObject manifest{{"world", projectMetadata(world, false)},
                                   {"blocks", blocks},
                                   {"hasDense", bool(world.heights.dense)}};
        const auto raw = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
        if (raw.size() > MaxManifest)
            return fail("分块工程索引超过16 MiB预算");
        const auto compressed = qCompress(raw, 6);
        const qint64 offset = file.pos();
        if (offset + compressed.size() > MaxFile)
            return fail("工程超过1 GiB文件预算");
        if (file.write(compressed) != compressed.size())
            return fail(file.errorString());
        QByteArray header = QByteArray("WBENCH09", 8) + QByteArray(12, '\0') + digest(raw);
        qToLittleEndian<uint64_t>(uint64_t(offset), header.data() + 8);
        qToLittleEndian<uint32_t>(uint32_t(compressed.size()), header.data() + 16);
        if (!file.seek(0) || file.write(header) != header.size())
            return fail(file.errorString());
        if (!backupLegacy(path, error))
            return false;
        if (!file.commit())
            return fail(file.errorString());
        return true;
    } catch (const std::bad_alloc &) {
        error = "保存内存不足，未覆盖原文件";
        return false;
    }
}

bool loadProject(const QString &path, World &world, QString &error) {
    try {
        auto fail = [&](QString message) {
            error = message;
            return false;
        };
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return fail(file.errorString());
        const auto header = file.read(HeaderSize);
        if (header.left(8) == "WBENCH01") {
            file.close();
            return loadLegacyProject(path, world, error);
        }
        if (header.size() != HeaderSize || header.left(8) != "WBENCH09" || file.size() > MaxFile)
            return fail("不支持的工程格式或文件超过1 GiB预算");
        const auto offset = qFromLittleEndian<uint64_t>(header.constData() + 8);
        const auto stored = qFromLittleEndian<uint32_t>(header.constData() + 16);
        if (offset < HeaderSize || offset > uint64_t(file.size()) || stored < 4 ||
            stored > MaxManifest + 65536 || offset + stored != uint64_t(file.size()) ||
            !file.seek(qint64(offset)))
            return fail("分块工程索引位置错误");
        const auto compressed = file.read(stored);
        if (compressed.size() != stored || qFromBigEndian<uint32_t>(compressed.constData()) > MaxManifest)
            return fail("分块工程索引大小错误");
        QByteArray raw;
        if (!unpackProjectBlock(compressed, MaxManifest, raw) || digest(raw) != header.mid(20, 32))
            return fail("分块工程索引校验失败");
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(raw, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject())
            return fail("分块工程索引JSON错误");
        const auto manifest = doc.object(), metadata = manifest["world"].toObject();
        if (!integer(metadata["format"], 9, 9) || !integer(metadata["width"], 32, 16384) ||
            !integer(metadata["depth"], 32, 16384) || !manifest["blocks"].isArray() ||
            !manifest["hasDense"].isBool() || manifest["blocks"].toArray().size() > MaxRecords)
            return fail("分块工程元数据错误");
        const int width = metadata["width"].toInt(), depth = metadata["depth"].toInt();
        // Own the value: operator[] on a temporary mutable QJsonObject returns
        // a QJsonValueRef that dangles after this statement (visible with /O2).
        const QJsonValue imageDefs = metadata["terrainInstances"].toObject().value("images");
        const auto layerDefs = metadata["sculptLayers"];
        if (!imageDefs.isArray() || imageDefs.toArray().size() > 64 || !layerDefs.isArray() ||
            layerDefs.toArray().isEmpty() || layerDefs.toArray().size() > 32)
            return fail("资源或雕刻层列表错误");
        ProjectPayload payload;
        std::vector<std::shared_ptr<HeightmapData>> images;
        uint64_t pixels = 0;
        for (const auto &value : imageDefs.toArray()) {
            const auto image = value.toObject();
            if (!integer(image["width"], 1, MaxHeightmapEdge) ||
                !integer(image["depth"], 1, MaxHeightmapEdge))
                return fail("高度图尺寸超过8192边长预算");
            auto data = std::make_shared<HeightmapData>();
            data->width = image["width"].toInt();
            data->depth = image["depth"].toInt();
            pixels += uint64_t(data->width) * data->depth;
            if (pixels > MaxTerrainPixels)
                return fail("工程高度图超过64M像素预算");
            data->values.resize(size_t(data->width) * data->depth);
            images.push_back(data);
            payload.images.push_back(data);
        }
        std::shared_ptr<std::vector<int16_t>> dense;
        if (manifest["hasDense"].toBool()) {
            dense = std::make_shared<std::vector<int16_t>>(size_t(width) * depth);
            payload.dense = dense;
        }
        for (int i = 0; i < layerDefs.toArray().size(); ++i)
            payload.deltas.push_back(std::make_shared<HeightDeltas>());
        std::vector<std::set<int>> tiles(images.size() + 1), groups(payload.deltas.size());
        uint64_t expectedOffset = HeaderSize, totalRaw = 0, deltaBytes = 0;
        for (const auto &value : manifest["blocks"].toArray()) {
            const auto entry = value.toObject();
            if (!integer(entry["offset"], HeaderSize, qint64(offset)) ||
                uint64_t(entry["offset"].toDouble()) != expectedOffset ||
                !integer(entry["stored"], 4, MaxBlock + 65536) || !integer(entry["raw"], 1, MaxBlock))
                return fail("工程分块索引重叠或大小无效");
            const int length = entry["stored"].toInt(), rawSize = entry["raw"].toInt();
            expectedOffset += length;
            totalRaw += rawSize;
            if (expectedOffset > offset || totalRaw > MaxFile || !file.seek(qint64(expectedOffset - length)))
                return fail("分块数据越界");
            const auto packed = file.read(length);
            if (packed.size() != length || qFromBigEndian<uint32_t>(packed.constData()) != uint32_t(rawSize))
                return fail("分块压缩长度错误");
            QByteArray bytes;
            if (!unpackProjectBlock(packed, MaxBlock, bytes) || bytes.size() != rawSize ||
                entry["sha256"].toString() != QString::fromLatin1(digest(bytes).toHex()))
                return fail("分块校验失败，当前工程未替换");
            const auto kind = entry["kind"].toString();
            if (kind == "image" || kind == "dense") {
                int id = int(images.size()), w = width, h = depth;
                if (kind == "image") {
                    if (!integer(entry["asset"], 0, qint64(images.size()) - 1))
                        return fail("图像资源索引错误");
                    id = entry["asset"].toInt();
                    w = images[id]->width;
                    h = images[id]->depth;
                } else if (!dense)
                    return fail("工程含未声明的高度基底");
                if (!integer(entry["x"], 0, w - 1) || !integer(entry["z"], 0, h - 1) ||
                    !integer(entry["width"], 1, Tile) || !integer(entry["depth"], 1, Tile))
                    return fail("图像分块范围错误");
                const int x = entry["x"].toInt(), z = entry["z"].toInt(), tw = entry["width"].toInt(),
                          th = entry["depth"].toInt();
                if (x % Tile || z % Tile || tw != std::min(Tile, w - x) || th != std::min(Tile, h - z) ||
                    rawSize != tw * th * 2 ||
                    !tiles[id].insert(z / Tile * ((w + Tile - 1) / Tile) + x / Tile).second)
                    return fail("图像分块重复或尺寸错误");
                for (int dz = 0; dz < th; ++dz)
                    for (int dx = 0; dx < tw; ++dx) {
                        const auto sample =
                            qFromLittleEndian<uint16_t>(bytes.constData() + (dz * tw + dx) * 2);
                        const size_t index = size_t(z + dz) * w + x + dx;
                        if (kind == "image")
                            images[id]->values[index] = sample;
                        else
                            (*dense)[index] = int16_t(sample);
                    }
            } else if (kind == "deltas") {
                if (!integer(entry["layer"], 0, qint64(payload.deltas.size()) - 1) ||
                    !integer(entry["group"], 0,
                             (qint64(width) * depth - 1) /
                                 (HeightDeltas::PageColumns * HeightDeltas::GroupPages)) ||
                    rawSize < 2)
                    return fail("雕刻分块范围错误");
                const int layer = entry["layer"].toInt(), group = entry["group"].toInt();
                if (!groups[layer].insert(group).second)
                    return fail("重复雕刻分块");
                const int pages = qFromLittleEndian<uint16_t>(bytes.constData());
                if (pages < 1 || pages > 64 || rawSize != 2 + pages * (2 + 256 * 4))
                    return fail("雕刻分块长度错误");
                const auto before = payload.deltas[layer]->allocatedBytes();
                std::set<int> pageIds;
                int pos = 2;
                for (int p = 0; p < pages; ++p) {
                    const int page = qFromLittleEndian<uint16_t>(bytes.constData() + pos);
                    pos += 2;
                    if (page >= 64 || !pageIds.insert(page).second)
                        return fail("雕刻页重复或越界");
                    int used = 0;
                    for (int i = 0; i < 256; ++i, pos += 4) {
                        const int delta = qFromLittleEndian<int32_t>(bytes.constData() + pos);
                        const int index = (group * 64 + page) * 256 + i;
                        if (delta < -1000000 || delta > 1000000 || (delta && index >= width * depth))
                            return fail("雕刻差量越界");
                        if (delta) {
                            payload.deltas[layer]->set(index, delta);
                            ++used;
                        }
                    }
                    if (!used)
                        return fail("空雕刻页不应存储");
                }
                deltaBytes += payload.deltas[layer]->allocatedBytes() - before;
                if (deltaBytes > 512ull * 1024 * 1024)
                    return fail("雕刻驻留数据超过512 MiB预算");
            } else if (kind == "cells") {
                if (rawSize % 12 || payload.overrides.size() + rawSize / 12 > 4 * 1024 * 1024)
                    return fail("体素分块数量无效");
                for (int pos = 0; pos < rawSize; pos += 12)
                    if (!payload.overrides
                             .emplace(qFromLittleEndian<uint64_t>(bytes.constData() + pos),
                                      qFromLittleEndian<uint32_t>(bytes.constData() + pos + 8))
                             .second)
                        return fail("重复体素键");
            } else
                return fail("不支持的工程分块类型");
        }
        if (expectedOffset != offset)
            return fail("分块数据缺失或含未索引字节");
        for (size_t id = 0; id < images.size(); ++id) {
            if (tiles[id].size() !=
                size_t((images[id]->width + Tile - 1) / Tile) * ((images[id]->depth + Tile - 1) / Tile))
                return fail("高度图分块不完整");
            images[id]->buildBounds();
        }
        if (dense && tiles.back().size() != size_t((width + Tile - 1) / Tile) * ((depth + Tile - 1) / Tile))
            return fail("高度基底分块不完整");
        return decodeProjectMetadata(metadata, world, error, &payload);
    } catch (const std::bad_alloc &) {
        error = "读取工程内存不足，当前工程未替换";
        return false;
    }
}
} // namespace wb
