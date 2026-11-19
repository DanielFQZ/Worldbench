#include "Project.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <climits>
namespace wb {
bool saveProject(const QString &path, const World &w, QString &error) {
    QJsonArray heights, cells;
    for (int h : w.heights)
        heights.append(h);
    std::vector<std::pair<uint32_t, Block>> ordered(w.overrides.begin(), w.overrides.end());
    std::sort(ordered.begin(), ordered.end());
    for (auto [k, v] : ordered)
        cells.append(QJsonArray{int(k), int(v)});
    QJsonObject root{{"format", 1},          {"size", w.size},    {"originX", w.originX},
                     {"originZ", w.originZ}, {"seed", w.seed},    {"seaLevel", w.seaLevel},
                     {"heights", heights},   {"overrides", cells}};
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
    if (o["format"].toInt() != 1 || !integer(o["size"], 32, 256) || o["size"].toInt() % 16)
        return fail("首版只支持 32–256、16 对齐的工程尺寸");
    w.size = o["size"].toInt();
    if (!integer(o["originX"], -1000000, 1000000) || !integer(o["originZ"], -1000000, 1000000) ||
        o["originX"].toInt() % 16 || o["originZ"].toInt() % 16 ||
        !integer(o["seaLevel"], World::MinY, World::MaxY - 1) || !integer(o["seed"], -1000000, 1000000))
        return fail("工程坐标或参数不合法");
    w.originX = o["originX"].toInt();
    w.originZ = o["originZ"].toInt();
    w.seed = o["seed"].toInt();
    w.seaLevel = o["seaLevel"].toInt();
    const auto hs = o["heights"].toArray();
    if (hs.size() != w.size * w.size)
        return fail("高度数据数量错误");
    for (auto h : hs) {
        if (!integer(h, World::MinY + 1, World::MaxY - 2))
            return fail("高度越界");
        w.heights.push_back(int16_t(h.toInt()));
    }
    if (!o["overrides"].isArray())
        return fail("缺少体素差量");
    for (auto v : o["overrides"].toArray()) {
        auto a = v.toArray();
        if (a.size() != 2 || !integer(a[0], 0, w.size * w.size * (World::MaxY - World::MinY) - 1) ||
            !integer(a[1], 0, BlockCount - 1))
            return fail("体素差量无效");
        if (a[0].toInt() < w.size * w.size)
            return fail("基岩保护层不允许包含体素差量");
        if (!w.overrides.emplace(uint32_t(a[0].toInt()), Block(a[1].toInt())).second)
            return fail("重复的体素键");
    }
    world = std::move(w);
    return true;
}
} // namespace wb
