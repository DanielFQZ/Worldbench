#include "Assets.h"
#include "NbtPayload.h"
#include "WorldCreation.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QVariant>
#include <QtEndian>
#include <QtZlib/zlib.h>
#include <cstring>
#include <stdexcept>
namespace wb {
namespace {
struct NbtReader {
    QByteArray bytes;
    qsizetype at = 0;
    int nodes = 0;
    const char *take(qsizetype n) {
        if (n < 0 || n > bytes.size() - at)
            throw std::runtime_error("NBT数据截断");
        auto p = bytes.constData() + at;
        at += n;
        return p;
    }
    template <class T> T number() { return qFromBigEndian<T>(take(sizeof(T))); }
    QString string() {
        auto n = number<quint16>();
        return QString::fromUtf8(take(n), n);
    }
    QVariant value(int tag, int level = 0, bool capture = false) {
        if (level > 64 || ++nodes > 12000000)
            throw std::runtime_error("NBT嵌套或节点超出预算");
        switch (tag) {
        case 1:
            return int(number<qint8>());
        case 2:
            return int(number<qint16>());
        case 3:
            return number<qint32>();
        case 4:
            return QVariant::fromValue(number<qint64>());
        case 5: {
            uint32_t u = number<quint32>();
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        }
        case 6: {
            uint64_t u = number<quint64>();
            double d;
            std::memcpy(&d, &u, 8);
            return d;
        }
        case 7: {
            auto n = number<qint32>();
            return QByteArray(take(n), n);
        }
        case 8:
            return string();
        case 9: {
            int type = number<quint8>(), n = number<qint32>();
            if (n < 0 || n > 2000000 || (type == 0 && n))
                throw std::runtime_error("NBT列表过大或类型无效");
            QVariantList list;
            list.reserve(n);
            for (int i = 0; i < n; ++i)
                list.append(value(type, level + 1));
            return list;
        }
        case 10: {
            const auto begin = at;
            QVariantMap map;
            for (;;) {
                int type = number<quint8>();
                if (!type) {
                    if (capture || map.contains("id") || map.contains("Id"))
                        map.insert("__rawNbt", bytes.mid(begin, at - begin));
                    return map;
                }
                auto name = string();
                if (map.contains(name))
                    throw std::runtime_error("NBT复合标签重复");
                map.insert(name, value(type, level + 1, name == "nbt" || name == "Data"));
            }
        }
        case 11: {
            int n = number<qint32>();
            if (n < 0 || n > 2000000)
                throw std::runtime_error("NBT整数数组过大");
            QVariantList list;
            list.reserve(n);
            for (int i = 0; i < n; ++i)
                list.append(number<qint32>());
            return list;
        }
        case 12: {
            int n = number<qint32>();
            if (n < 0 || n > 2000000)
                throw std::runtime_error("NBT长整数数组过大");
            QVariantList list;
            list.reserve(n);
            for (int i = 0; i < n; ++i)
                list.append(QVariant::fromValue(number<qint64>()));
            return list;
        }
        default:
            throw std::runtime_error("NBT标签类型无效");
        }
    }
    QVariantMap read() {
        if (number<quint8>() != 10)
            throw std::runtime_error("NBT根节点必须为复合标签");
        string();
        auto out = value(10).toMap();
        if (at != bytes.size())
            throw std::runtime_error("NBT尾部存在多余数据");
        return out;
    }
};
QByteArray unpack(const QByteArray &in) {
    if (in.size() < 2 || uchar(in[0]) != 0x1f || uchar(in[1]) != 0x8b)
        return in;
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK)
        throw std::runtime_error("无法初始化gzip");
    QByteArray out;
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    z.avail_in = uInt(in.size());
    int result = Z_OK;
    while (result == Z_OK) {
        char chunk[65536];
        z.next_out = reinterpret_cast<Bytef *>(chunk);
        z.avail_out = sizeof(chunk);
        result = inflate(&z, Z_NO_FLUSH);
        out.append(chunk, sizeof(chunk) - z.avail_out);
        if (out.size() > 128 * 1024 * 1024) {
            inflateEnd(&z);
            throw std::runtime_error("解压结构超过128MiB预算");
        }
    }
    inflateEnd(&z);
    if (result != Z_STREAM_END)
        throw std::runtime_error("gzip校验失败或数据截断");
    return out;
}
QString stateText(const QVariantMap &p) {
    BlockState s{p.value("Name").toString(), {}};
    auto props = p.value("Properties").toMap();
    for (auto i = props.begin(); i != props.end(); ++i)
        s.properties[i.key()] = i.value().toString();
    return s.label();
}
} // namespace
bool importStructure(const QString &path, StructureAsset &asset, QString &error) {
    error.clear();
    QFile file(path);
    if (file.size() > 64 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        error = "资产不可读或超过64MiB输入预算";
        return false;
    }
    auto bytes = file.readAll();
    if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0 ||
        QFileInfo(path).suffix().compare("wbasset", Qt::CaseInsensitive) == 0) {
        auto doc = QJsonDocument::fromJson(bytes);
        if (!doc.isObject()) {
            error = "资产JSON无效";
            return false;
        }
        auto object = doc.object();
        if (object.contains("asset"))
            object = object["asset"].toObject();
        if (!StructureAsset::fromJson(object, asset, error))
            return false;
    } else
        try {
            auto root = NbtReader{unpack(bytes)}.read();
            if (root.contains("Schematic"))
                root = root["Schematic"].toMap();
            asset = {};
            asset.id = "imported-asset";
            asset.name = QFileInfo(path).completeBaseName();
            if (root.contains("size") && root.contains("palette")) {
                auto size = root["size"].toList();
                if (size.size() != 3)
                    throw std::runtime_error("结构尺寸无效");
                asset.width = size[0].toInt();
                asset.height = size[1].toInt();
                asset.depth = size[2].toInt();
                auto palette = root["palette"].toList();
                for (auto v : root["blocks"].toList()) {
                    auto b = v.toMap();
                    auto p = b["pos"].toList();
                    int index = b["state"].toInt();
                    if (p.size() != 3 || index < 0 || index >= palette.size())
                        throw std::runtime_error("结构方块引用无效");
                    asset.blocks.push_back({p[0].toInt(), p[1].toInt(), p[2].toInt(),
                                            stateText(palette[index].toMap()),
                                            b["nbt"].toMap()["__rawNbt"].toByteArray()});
                }
                asset.format = "minecraft-structure-nbt";
            } else {
                asset.width = root["Width"].toInt();
                asset.height = root["Height"].toInt();
                asset.depth = root["Length"].toInt();
                const int64_t count = int64_t(asset.width) * asset.height * asset.depth;
                if (asset.width < 1 || asset.height < 1 || asset.depth < 1 || count > 16000000)
                    throw std::runtime_error("Schematic尺寸无效或超过1600万体素预算");
                auto blocks = root.contains("Blocks") && root["Blocks"].typeId() == QMetaType::QVariantMap
                                  ? root["Blocks"].toMap()
                                  : root;
                auto palette = blocks["Palette"].toMap();
                if (!palette.isEmpty() && root["Version"].toInt() != 2 && root["Version"].toInt() != 3)
                    throw std::runtime_error("仅支持Sponge Schematic v2/v3；请先转换格式");
                QMap<int, QString> names;
                for (auto i = palette.begin(); i != palette.end(); ++i) {
                    int n = i.value().toInt();
                    if (n < 0 || names.contains(n))
                        throw std::runtime_error("Schematic调色板无效");
                    names[n] = i.key();
                }
                QByteArray data = blocks.value("Data", root.value("BlockData")).toByteArray();
                qsizetype cursor = 0;
                if (palette.isEmpty()) {
                    QByteArray ids = root["Blocks"].toByteArray(), meta = root["Data"].toByteArray(),
                               extra = root["AddBlocks"].toByteArray();
                    if (ids.size() != count || meta.size() != count ||
                        (!extra.isEmpty() && extra.size() != (count + 1) / 2))
                        throw std::runtime_error("旧Schematic数字ID/元数据长度错误");
                    QFile mapping(":/translations/legacy_blockstates.json");
                    if (!mapping.open(QIODevice::ReadOnly))
                        throw std::runtime_error("旧版方块映射资源缺失");
                    auto registry = QJsonDocument::fromJson(mapping.readAll()).object()["blocks"].toObject();
                    for (int64_t i = 0; i < count; ++i) {
                        int id = uchar(ids[i]);
                        if (!extra.isEmpty())
                            id |= ((uchar(extra[i / 2]) >> ((i % 2) * 4)) & 15) << 8;
                        const int variant = uchar(meta[i]) & 15;
                        QString key = QString::number(id) + ":" + QString::number(variant);
                        QString state = registry[key].toString();
                        if (state.isEmpty())
                            throw std::runtime_error(("旧版方块缺失映射：" + key).toStdString());
                        if (state != "minecraft:air")
                            asset.blocks.push_back({int(i % asset.width),
                                                    int(i / (asset.width * asset.depth)),
                                                    int(i / asset.width % asset.depth), state});
                    }
                    asset.format = "mcedit-schematic-1.12";
                } else {
                    for (int64_t i = 0; i < count; ++i) {
                        uint32_t n = 0;
                        int shift = 0;
                        for (;;) {
                            if (cursor >= data.size() || shift > 28)
                                throw std::runtime_error("Schematic VarInt无效或截断");
                            uint8_t byte = data[cursor++];
                            n |= uint32_t(byte & 127) << shift;
                            if (!(byte & 128))
                                break;
                            shift += 7;
                        }
                        if (!names.contains(int(n)))
                            throw std::runtime_error("方块索引不在调色板中");
                        auto state = names[int(n)];
                        if (state != "minecraft:air" && state != "minecraft:cave_air" &&
                            state != "minecraft:void_air")
                            asset.blocks.push_back({int(i % asset.width),
                                                    int(i / (asset.width * asset.depth)),
                                                    int(i / asset.width % asset.depth), state});
                        if (asset.blocks.size() > 2000000)
                            throw std::runtime_error("结构超过200万非空气方块预算");
                    }
                    if (cursor != data.size())
                        throw std::runtime_error("Schematic方块数据长度不匹配");
                    asset.format = "sponge-schem-v" + root["Version"].toString();
                }
                auto bes =
                    blocks.value("BlockEntities", root.value("BlockEntities", root.value("TileEntities")))
                        .toList();
                for (auto v : bes) {
                    auto be = v.toMap();
                    auto p = be["Pos"].toList();
                    int x = p.size() == 3 ? p[0].toInt() : be["x"].toInt(),
                        y = p.size() == 3 ? p[1].toInt() : be["y"].toInt(),
                        z = p.size() == 3 ? p[2].toInt() : be["z"].toInt();
                    QString id = be.value("Id", be.value("id")).toString();
                    if (!id.contains(':'))
                        throw std::runtime_error(
                            "旧式或缺失方块实体ID无法安全迁移；请先转换为1.20.1结构NBT/现代schem");
                    auto raw = be.contains("Data") ? be["Data"].toMap()["__rawNbt"].toByteArray()
                                                   : be["__rawNbt"].toByteArray();
                    if (raw.isEmpty())
                        raw = QByteArray(1, 0);
                    raw = patchNbtPayload(raw, {{"id", nbtText("id", id)}}, {"Id", "Pos"});
                    wb::SharedValues<StructureBlock> rewritten;
                    for (auto b : asset.blocks) {
                        if (b.x == x && b.y == y && b.z == z)
                            b.nbt = raw;
                        rewritten.push_back(std::move(b));
                    }
                    asset.blocks = std::move(rewritten);
                }
            }
            for (auto v : root.value("Entities", root.value("entities")).toList()) {
                auto wrapper = v.toMap(), e = wrapper.contains("nbt") ? wrapper["nbt"].toMap() : wrapper;
                auto pos = wrapper.value("pos", e.value("Pos")).toList();
                if (pos.size() != 3)
                    throw std::runtime_error("实体缺少三维坐标");
                auto raw = e["__rawNbt"].toByteArray();
                if (e.contains("Data"))
                    raw = e["Data"].toMap()["__rawNbt"].toByteArray();
                if (raw.isEmpty())
                    raw = QByteArray(1, 0);
                QString id = e.value("id", e.value("Id")).toString();
                if (!id.contains(':'))
                    throw std::runtime_error(
                        "旧式或缺失实体ID无法安全迁移；请先转换为1.20.1结构NBT/现代schem");
                raw = patchNbtPayload(raw, {{"id", nbtText("id", id)}}, {"Id", "Data"});
                asset.entities.push_back(
                    {{float(pos[0].toDouble()), float(pos[1].toDouble()), float(pos[2].toDouble())}, raw});
            }
            if (!asset.entities.empty())
                asset.diagnostic = "包含实体：保留数据并导出；编辑视口当前只显示结构方块。";
            const bool foliage = std::any_of(asset.blocks.begin(), asset.blocks.end(), [](const auto &b) {
                return b.state.section('[', 0, 0).endsWith("_leaves");
            });
            asset.category = foliage ? "open" : "closed";
            asset.pivot = {float(asset.width / 2), 0, float(asset.depth / 2)};
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
            return false;
        }
    asset.sourcePath = QFileInfo(path).absoluteFilePath();
    CreationState state;
    state.assets.push_back(asset);
    return validateCreation(state, error);
}
} // namespace wb
