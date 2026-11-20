#include "MinecraftExport.h"
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <QtEndian>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

namespace wb {
namespace {
struct Nbt {
    QByteArray b;
    void u8(uint8_t v) { b.append(char(v)); }
    void i16(int16_t v) {
        char p[2];
        qToBigEndian(v, p);
        b.append(p, 2);
    }
    void i32(int32_t v) {
        char p[4];
        qToBigEndian(v, p);
        b.append(p, 4);
    }
    void i64(int64_t v) {
        char p[8];
        qToBigEndian(v, p);
        b.append(p, 8);
    }
    void str(const QByteArray &v) {
        i16(int16_t(v.size()));
        b += v;
    }
    void tag(int t, const char *name) {
        u8(uint8_t(t));
        str(name);
    }
    void compound(const char *name) { tag(10, name); }
    void end() { u8(0); }
    void byte(const char *name, int v) {
        tag(1, name);
        u8(uint8_t(v));
    }
    void integer(const char *name, int v) {
        tag(3, name);
        i32(v);
    }
    void lng(const char *name, int64_t v) {
        tag(4, name);
        i64(v);
    }
    void text(const char *name, const QByteArray &v) {
        tag(8, name);
        str(v);
    }
    void list(const char *name, int type, int n) {
        tag(9, name);
        u8(uint8_t(type));
        i32(n);
    }
    void longs(const char *name, const std::vector<uint64_t> &a) {
        tag(12, name);
        i32(int(a.size()));
        for (auto v : a)
            i64(int64_t(v));
    }
};
std::vector<uint64_t> pack(const std::vector<int> &values, int bits) {
    int per = 64 / bits;
    std::vector<uint64_t> out((values.size() + per - 1) / per);
    for (size_t i = 0; i < values.size(); ++i)
        out[i / per] |= uint64_t(values[i]) << ((i % per) * bits);
    return out;
}
uint32_t crc32(const QByteArray &b) {
    uint32_t crc = 0xffffffff;
    for (unsigned char c : b) {
        crc ^= c;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int(crc & 1)));
    }
    return ~crc;
}
QByteArray gzip(const QByteArray &raw) {
    const QByteArray z = qCompress(raw, 6).mid(4);
    QByteArray b = QByteArray::fromHex("1f8b0800000000000003");
    b += z.mid(2, z.size() - 6);
    char tail[8];
    qToLittleEndian(crc32(raw), tail);
    qToLittleEndian(uint32_t(raw.size()), tail + 4);
    b.append(tail, 8);
    return b;
}
void write(const QString &path, const QByteArray &b) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly) || f.write(b) != b.size() || !f.flush())
        throw std::runtime_error(("写入失败: " + path + " " + f.errorString()).toStdString());
}
void paletteEntry(Nbt &n, const BlockState &b) {
    n.text("Name", b.name.toUtf8());
    if (!b.properties.isEmpty()) {
        n.compound("Properties");
        for (auto p = b.properties.begin(); p != b.properties.end(); ++p)
            n.text(p.key().toUtf8().constData(), p.value().toUtf8());
        n.end();
    }
    n.end();
}
QByteArray chunk(const World &w, int cx, int cz) {
    int columnHeights[16][16];
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            columnHeights[z][x] = w.height(cx * 16 + x, cz * 16 + z);
    Nbt n;
    n.compound("");
    n.integer("DataVersion", 3465);
    n.integer("xPos", cx);
    n.integer("zPos", cz);
    n.integer("yPos", -4);
    n.text("Status", "minecraft:full");
    n.lng("LastUpdate", 0);
    n.lng("InhabitedTime", 0);
    n.byte("isLightOn", 0);
    n.list("sections", 10, 24);
    for (int sy = -4; sy < 20; ++sy) {
        n.byte("Y", sy);
        std::vector<int> indices(w.palette.size(), -1);
        std::vector<Block> palette;
        std::vector<int> values;
        values.reserve(4096);
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    Block b = w.blockAtHeight(cx * 16 + x, sy * 16 + y, cz * 16 + z, columnHeights[z][x]);
                    if (indices[b] < 0) {
                        indices[b] = int(palette.size());
                        palette.push_back(b);
                    }
                    values.push_back(indices[b]);
                }
        n.compound("block_states");
        n.list("palette", 10, int(palette.size()));
        for (Block b : palette)
            paletteEntry(n, w.palette[b]);
        if (palette.size() > 1) {
            int bits = 4;
            while ((1u << bits) < palette.size())
                ++bits;
            n.longs("data", pack(values, bits));
        }
        n.end();
        n.compound("biomes");
        n.list("palette", 8, 1);
        n.str("minecraft:plains");
        n.end();
        n.end();
    }
    std::vector<int> surface(256), floor(256);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            bool s = false, f = false;
            for (int y = World::MaxY - 1; y >= World::MinY; --y) {
                Block b = w.blockAtHeight(cx * 16 + x, y, cz * 16 + z, columnHeights[z][x]);
                if (!s && b != Air) {
                    surface[z * 16 + x] = y - World::MinY + 1;
                    s = true;
                }
                if (!f && b != Air && b != Water) {
                    floor[z * 16 + x] = y - World::MinY + 1;
                    f = true;
                }
                if (s && f)
                    break;
            }
        }
    n.compound("Heightmaps");
    n.longs("WORLD_SURFACE", pack(surface, 9));
    n.longs("MOTION_BLOCKING", pack(surface, 9));
    n.longs("MOTION_BLOCKING_NO_LEAVES", pack(surface, 9));
    n.longs("OCEAN_FLOOR", pack(floor, 9));
    n.end();
    n.list("block_entities", 10, 0);
    n.list("block_ticks", 10, 0);
    n.list("fluid_ticks", 10, 0);
    n.list("PostProcessing", 9, 24);
    for (int i = 0; i < 24; ++i) {
        n.u8(2);
        n.i32(0);
    }
    n.compound("structures");
    n.compound("starts");
    n.end();
    n.compound("References");
    n.end();
    n.end();
    n.end();
    return n.b;
}
QByteArray level(const World &w, const QString &name, QRect area) {
    Nbt n;
    n.compound("");
    n.compound("Data");
    n.integer("DataVersion", 3465);
    n.integer("version", 19133);
    n.compound("Version");
    n.integer("Id", 3465);
    n.text("Name", "1.20.1");
    n.text("Series", "main");
    n.byte("Snapshot", 0);
    n.end();
    n.text("LevelName", name.toUtf8());
    n.integer("GameType", 1);
    n.byte("allowCommands", 1);
    n.byte("hardcore", 0);
    n.byte("initialized", 1);
    n.byte("Difficulty", 1);
    n.byte("DifficultyLocked", 0);
    int sx = area.center().x(), sz = area.center().y(), spawnY = w.height(sx, sz) + 3;
    for (int y = World::MaxY - 1; y >= World::MinY; --y)
        if (w.block(sx, y, sz) != Air) {
            spawnY = std::min(World::MaxY - 1, y + 3);
            break;
        }
    n.integer("SpawnX", sx);
    n.integer("SpawnY", spawnY);
    n.integer("SpawnZ", sz);
    n.lng("Time", 0);
    n.lng("DayTime", 1000);
    n.lng("LastPlayed", QDateTime::currentMSecsSinceEpoch());
    n.lng("SizeOnDisk", 0);
    n.integer("clearWeatherTime", 6000);
    n.byte("raining", 0);
    n.byte("thundering", 0);
    n.compound("GameRules");
    n.text("doMobSpawning", "false");
    n.text("doDaylightCycle", "false");
    n.end();
    n.compound("DataPacks");
    n.list("Enabled", 8, 1);
    n.str("vanilla");
    n.list("Disabled", 8, 0);
    n.end();
    n.compound("WorldGenSettings");
    n.lng("seed", w.seed);
    n.byte("generate_features", 0);
    n.byte("bonus_chest", 0);
    n.compound("dimensions");
    n.compound("minecraft:overworld");
    n.text("type", "minecraft:overworld");
    n.compound("generator");
    n.text("type", "minecraft:flat");
    n.compound("settings");
    n.text("biome", "minecraft:plains");
    n.byte("lakes", 0);
    n.byte("features", 0);
    n.list("layers", 10, 0);
    n.list("structure_overrides", 8, 0);
    n.end();
    n.end();
    n.end();
    for (bool nether : {true, false}) {
        n.compound(nether ? "minecraft:the_nether" : "minecraft:the_end");
        n.text("type", nether ? "minecraft:the_nether" : "minecraft:the_end");
        n.compound("generator");
        n.text("type", "minecraft:noise");
        n.text("settings", nether ? "minecraft:nether" : "minecraft:end");
        n.compound("biome_source");
        n.text("type", nether ? "minecraft:multi_noise" : "minecraft:the_end");
        if (nether)
            n.text("preset", "minecraft:nether");
        n.end();
        n.end();
        n.end();
    }
    n.end();
    n.end();
    n.end();
    n.end();
    return gzip(n.b);
}
} // namespace
ExportResult exportMinecraft(const World &w, const QString &directory, std::atomic_bool *cancelled,
                             std::function<void(int, int)> progress, QRect area) {
    ExportResult result;
    result.path = QDir::cleanPath(directory);
    QString temp;
    try {
        if (!w.resourceVersion.isEmpty() && w.resourceVersion != "1.20.1")
            throw std::runtime_error("本版只支持导出 1.20.1，资源版本不匹配");
        QSet<Block> checked;
        auto validateMaterial = [&](Block b) {
            if (b >= w.palette.size())
                throw std::runtime_error("工程包含越界的方块索引");
            if (b < BlockCount || checked.contains(b))
                return;
            checked.insert(b);
            QString error;
            if (!w.assets || w.assets->version != "1.20.1" || !w.assets->validate(w.palette[b], error))
                throw std::runtime_error(("动态方块需要对应的 1.20.1 客户端资源：" + error).toStdString());
        };
        for (const auto &[key, b] : w.overrides)
            validateMaterial(b);
        QString themeError;
        if (!validateTheme(w.theme, w.palette.size(), themeError))
            throw std::runtime_error(themeError.toStdString());
        if (w.theme.enabled)
            for (const auto &band : w.theme.bands) {
                validateMaterial(band.surface);
                validateMaterial(band.subsurface);
            }
        if (w.width < 32 || w.width > 16384 || w.depth < 32 || w.depth > 16384 || w.originX % 16 ||
            w.originZ % 16 || w.heights.size() != size_t(w.width) * w.depth)
            throw std::runtime_error("不支持的世界尺寸或区块对齐");
        const QRect bounds(w.originX, w.originZ, w.width, w.depth);
        if (area.isNull())
            area = bounds;
        else if (!area.isValid() || !bounds.contains(area))
            throw std::runtime_error("导出选区越界");
        if (QFile::exists(result.path))
            throw std::runtime_error("目标目录已存在，请使用新的存档名称");
        temp = result.path + ".partial-" + QUuid::createUuid().toString(QUuid::Id128);
        if (!QDir().mkpath(temp + "/region"))
            throw std::runtime_error("无法创建导出目录");
        const int minCX = floorDiv(area.left(), 16), maxCX = floorDiv(area.right(), 16);
        const int minCZ = floorDiv(area.top(), 16), maxCZ = floorDiv(area.bottom(), 16);
        const int total = (maxCX - minCX + 1) * (maxCZ - minCZ + 1);
        for (int rz = floorDiv(minCZ, 32); rz <= floorDiv(maxCZ, 32); ++rz)
            for (int rx = floorDiv(minCX, 32); rx <= floorDiv(maxCX, 32); ++rx) {
                QByteArray region(8192, 0);
                for (int cz = std::max(minCZ, rz * 32); cz <= std::min(maxCZ, rz * 32 + 31); ++cz)
                    for (int cx = std::max(minCX, rx * 32); cx <= std::min(maxCX, rx * 32 + 31); ++cx) {
                        if (cancelled && cancelled->load())
                            throw std::runtime_error("导出已取消");
                        const QByteArray data = qCompress(chunk(w, cx, cz), 6).mid(4);
                        const int sectors = (int(data.size()) + 5 + 4095) / 4096;
                        if (sectors > 255)
                            throw std::runtime_error("区块超过普通 Region 记录容量");
                        const int offset = int(region.size()) / 4096,
                                  index = (cx - rx * 32) + (cz - rz * 32) * 32;
                        qToBigEndian(uint32_t((offset << 8) | sectors), region.data() + index * 4);
                        qToBigEndian(uint32_t(QDateTime::currentSecsSinceEpoch()),
                                     region.data() + 4096 + index * 4);
                        QByteArray record(sectors * 4096, 0);
                        qToBigEndian(uint32_t(data.size() + 1), record.data());
                        record[4] = 2;
                        std::copy(data.begin(), data.end(), record.begin() + 5);
                        region += record;
                        ++result.chunks;
                        if (progress)
                            progress(result.chunks, total);
                    }
                write(temp + QString("/region/r.%1.%2.mca").arg(rx).arg(rz), region);
            }
        write(temp + "/level.dat", level(w, QFileInfo(result.path).fileName(), area));
        if (cancelled && cancelled->load())
            throw std::runtime_error("导出已取消");
        if (!QDir().rename(temp, result.path))
            throw std::runtime_error("导出完成但目录发布失败");
        result.ok = true;
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        if (!temp.isEmpty())
            QDir(temp).removeRecursively();
    }
    return result;
}
} // namespace wb
