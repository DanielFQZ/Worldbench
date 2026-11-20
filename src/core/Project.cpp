#include "Project.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <climits>
#include <cmath>
namespace wb {
bool saveProject(const QString &path, const World &w, QString &error) {
    if (!validateTheme(w.theme, w.palette.size(), error) || !validateTerrain(w.heights.instances, error))
        return false;
    QJsonArray heights, cells;
    if (w.heights.dense)
        for (int h : *w.heights.dense)
            heights.append(h);
    std::vector<std::pair<uint64_t, Block>> ordered(w.overrides.begin(), w.overrides.end());
    std::sort(ordered.begin(), ordered.end());
    for (auto [k, v] : ordered)
        cells.append(QJsonArray{double(k), int(v)});
    QJsonArray palette, paths;
    for (const auto &state : w.palette) {
        QJsonObject props;
        for (auto p = state.properties.begin(); p != state.properties.end(); ++p)
            props[p.key()] = p.value();
        palette.append(QJsonObject{{"name", state.name}, {"properties", props}});
    }
    for (const auto &path : w.resourcePaths)
        paths.append(path);
    QJsonArray layers;
    for (const auto &l : w.heights.layers) {
        QJsonArray edits;
        for (auto [i, h] : *l.deltas)
            edits.append(QJsonArray{i, h});
        layers.append(QJsonObject{{"id", l.id}, {"name", l.name}, {"enabled", l.enabled}, {"deltas", edits}});
    }
    QJsonObject root{{"format", 7},
                     {"surfaceTheme", saveTheme(w.theme)},
                     {"sculptLayers", layers},
                     {"activeLayer", w.heights.activeLayer},
                     {"width", w.width},
                     {"depth", w.depth},
                     {"terrain", QJsonObject{{"baseHeight", w.heights.baseHeight},
                                             {"variation", w.heights.variation},
                                             {"flat", w.heights.flat}}},
                     {"terrainInstances", saveTerrain(w.heights.instances)},
                     {"originX", w.originX},
                     {"originZ", w.originZ},
                     {"seed", w.seed},
                     {"seaLevel", w.seaLevel},
                     {"heights", heights},
                     {"overrides", cells},
                     {"palette", palette},
                     {"resources", paths},
                     {"resourceVersion", w.resourceVersion}};
    QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() > 64 * 1024 * 1024) {
        error = "工程数据超过 P0 格式的 64 MiB 解压上限，未覆盖原文件";
        return false;
    }
    const QByteArray bytes = QByteArray("WBENCH01", 8) +
                             QCryptographicHash::hash(data, QCryptographicHash::Sha256) + qCompress(data, 6);
    if (bytes.size() > 16 * 1024 * 1024) {
        error = "工程超过 P0 格式的 16 MiB 文件上限，未覆盖原文件";
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        error = f.errorString();
        return false;
    }
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        error = f.errorString();
        return false;
    }
    return true;
}
bool loadProject(const QString &path, World &world, QString &error) {
    auto fail = [&](QString msg) {
        error = msg;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    if (f.size() > 16 * 1024 * 1024 || f.size() < 44)
        return fail("工程大小不合法或文件损坏");
    const QByteArray b = f.readAll();
    if (b.left(8) != "WBENCH01")
        return fail("不支持的工程格式");
    const uint32_t size = qFromBigEndian<uint32_t>(b.constData() + 40);
    if (size > 64 * 1024 * 1024)
        return fail("解压后工程超过首版上限");
    const QByteArray data = qUncompress(b.mid(40));
    if (uint32_t(data.size()) != size ||
        QCryptographicHash::hash(data, QCryptographicHash::Sha256) != b.mid(8, 32))
        return fail("工程校验失败，未替换当前世界");
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail("工程 JSON 无效");
    const auto o = doc.object();
    World w;
    auto integer = [&](const QJsonValue &v, int lo, int hi) {
        return v.isDouble() && v.toDouble() == v.toInt(INT_MIN) && v.toInt() >= lo && v.toInt() <= hi;
    };
    const int format = o["format"].toInt();
    if (!integer(o["format"], 1, 7))
        return fail("不支持的工程版本");
    const auto widthValue = o[format >= 3 ? "width" : "size"];
    const auto depthValue = format >= 3 ? o["depth"] : widthValue;
    if (!integer(widthValue, 32, format >= 3 ? 16384 : 256) ||
        !integer(depthValue, 32, format >= 3 ? 16384 : 256))
        return fail("工程长宽越界");
    w.width = widthValue.toInt();
    w.depth = depthValue.toInt();
    if (!integer(o["originX"], -1000000, 1000000) || !integer(o["originZ"], -1000000, 1000000) ||
        o["originX"].toInt() % 16 || o["originZ"].toInt() % 16 ||
        !integer(o["seaLevel"], World::MinY, World::MaxY - 1) || !integer(o["seed"], -1000000, 1000000))
        return fail("工程坐标或参数不合法");
    w.originX = o["originX"].toInt();
    w.originZ = o["originZ"].toInt();
    w.seed = o["seed"].toInt();
    w.seaLevel = o["seaLevel"].toInt();
    WorldOptions options{w.width, w.seed, w.seaLevel, 0, 0, true, w.depth};
    if (format >= 3) {
        const auto t = o["terrain"].toObject();
        if (!integer(t["baseHeight"], -63, 318) || !integer(t["variation"], 0, 128) || !t["flat"].isBool())
            return fail("基础地形参数无效");
        options.baseHeight = t["baseHeight"].toInt();
        options.variation = t["variation"].toInt();
        options.flat = t["flat"].toBool();
    }
    w.heights.configure(options, w.originX, w.originZ);
    if (!o["heights"].isArray())
        return fail("高度基底数据无效");
    const auto hs = o["heights"].toArray();
    if (format < 3 || !hs.isEmpty()) {
        if (hs.size() != w.width * w.depth)
            return fail("高度数据数量错误");
        std::vector<int16_t> values;
        values.reserve(hs.size());
        for (auto h : hs) {
            if (!integer(h, World::MinY + 1, World::MaxY - 2))
                return fail("高度越界");
            values.push_back(int16_t(h.toInt()));
        }
        w.heights.assign(std::move(values));
    }
    if (format >= 4 && !loadTerrain(o["terrainInstances"].toObject(), w.heights.instances, error))
        return false;
    if (format < 7 && std::any_of(
                          w.heights.instances.begin(), w.heights.instances.end(),
                          [](const auto &i) { return i.blend == 4; }))
        return fail("智能混合需要工程格式 v7");
    if (format == 3 || format == 4) {
        if (!o["heightChanges"].isArray())
            return fail("缺少高度差量");
        std::set<int> seen;
        for (const auto &v : o["heightChanges"].toArray()) {
            auto a = v.toArray();
            if (a.size() != 2 || !integer(a[0], 0, w.width * w.depth - 1) || !integer(a[1], -63, 318) ||
                !seen.insert(a[0].toInt()).second)
                return fail("高度差量无效");
            w.heights.restore(a[0].toInt(), a[1].toInt(), true);
        }
    }
    if (format >= 5) {
        if (!o["sculptLayers"].isArray() || o["sculptLayers"].toArray().isEmpty() ||
            o["sculptLayers"].toArray().size() > 32)
            return fail("地形层列表无效");
        std::set<QString> ids;
        w.heights.layers.clear();
        for (auto value : o["sculptLayers"].toArray()) {
            auto l = value.toObject();
            SculptLayer layer;
            layer.id = l["id"].toString();
            layer.name = l["name"].toString();
            layer.enabled = l["enabled"].toBool();
            if (layer.id.isEmpty() || layer.id.size() > 80 || layer.name.isEmpty() ||
                layer.name.size() > 128 || !ids.insert(layer.id).second || !l["enabled"].isBool() ||
                !l["deltas"].isArray())
                return fail("地形层参数无效");
            for (const auto &instance : w.heights.instances)
                if (instance.id == layer.id)
                    return fail("场景标识重复");
            for (auto edit : l["deltas"].toArray()) {
                auto a = edit.toArray();
                if (a.size() != 2 || !integer(a[0], 0, w.width * w.depth - 1) ||
                    !integer(a[1], -1000000, 1000000) ||
                    !layer.deltas->emplace(a[0].toInt(), a[1].toInt()).second)
                    return fail("地形层差量无效");
            }
            w.heights.layers.push_back(std::move(layer));
        }
        if (w.heights.layers.front().id != "terrain-base-1")
            return fail("缺少基础地形层");
        w.heights.activeLayer = o["activeLayer"].toString();
        if (w.heights.layerIndex(w.heights.activeLayer) < 0)
            return fail("活动地形层无效");
    }
    if (format >= 2) {
        const auto states = o["palette"].toArray();
        if (states.size() < BlockCount || states.size() > 65536)
            return fail("方块调色板大小错误");
        const QRegularExpression id("^[a-z0-9_.-]+:[a-z0-9_./-]+$"), prop("^[a-z0-9_]+$");
        const auto defaults = w.palette;
        w.palette.clear();
        w.models.clear();
        for (int i = 0; i < states.size(); ++i) {
            const auto object = states[i].toObject();
            BlockState s{object["name"].toString(), {}};
            if (s.name.size() > 256 || !id.match(s.name).hasMatch() || !object["properties"].isObject())
                return fail("方块标识无效");
            const auto props = object["properties"].toObject();
            if (props.size() > 32)
                return fail("方块属性过多");
            for (auto p = props.begin(); p != props.end(); ++p) {
                if (p.key().size() > 64 || !prop.match(p.key()).hasMatch() || !p.value().isString() ||
                    p.value().toString().size() > 64 || !prop.match(p.value().toString()).hasMatch())
                    return fail("方块属性无效");
                s.properties[p.key()] = p.value().toString();
            }
            if (i < BlockCount && !(s == defaults[i]))
                return fail("基础方块调色板不匹配");
            w.palette.push_back(s);
            w.models.push_back(fallbackModel(i));
        }
        auto paths = o["resources"].toArray();
        if (paths.size() > 8)
            return fail("资源来源数量过多");
        for (const auto &path : paths) {
            if (!path.isString() || path.toString().size() > 4096)
                return fail("资源路径无效");
            w.resourcePaths << path.toString();
        }
        w.resourceVersion = o["resourceVersion"].toString();
    }
    if (format >= 6 && !loadTheme(o["surfaceTheme"].toObject(), w.theme, w.palette.size(), error))
        return false;
    if (!o["overrides"].isArray())
        return fail("缺少体素差量");
    for (auto v : o["overrides"].toArray()) {
        auto a = v.toArray();
        if (a.size() != 2 ||
            (!a[0].isDouble() || !std::isfinite(a[0].toDouble()) || a[0].toDouble() < 0 ||
             a[0].toDouble() != std::floor(a[0].toDouble()) ||
             a[0].toDouble() >= double(w.width) * w.depth * (World::MaxY - World::MinY)) ||
            !integer(a[1], 0, int(w.palette.size()) - 1))
            return fail("体素差量无效");
        if (a[0].toDouble() < double(w.width) * w.depth)
            return fail("基岩保护层不允许包含体素差量");
        if (!w.overrides.emplace(uint64_t(a[0].toDouble()), Block(a[1].toInt())).second)
            return fail("重复的体素键");
    }
    world = std::move(w);
    return true;
}
} // namespace wb
