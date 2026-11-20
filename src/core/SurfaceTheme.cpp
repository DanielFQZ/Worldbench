#include "World.h"
#include <QJsonArray>
#include <climits>
#include <cmath>
namespace wb {
namespace {
double lattice(int x, int z, int seed) {
    uint32_t n = uint32_t(x) * 0x8da6b343u ^ uint32_t(z) * 0xd8163841u ^ uint32_t(seed);
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return double(n) / 4294967295.0 * 2 - 1;
}
double edgeNoise(int x, int z, int scale, int seed) {
    const int ix = floorDiv(x, scale), iz = floorDiv(z, scale);
    double u = double(x - ix * scale) / scale, v = double(z - iz * scale) / scale;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    return std::lerp(std::lerp(lattice(ix, iz, seed), lattice(ix + 1, iz, seed), u),
                     std::lerp(lattice(ix, iz + 1, seed), lattice(ix + 1, iz + 1, seed), u), v);
}
} // namespace
Block SurfaceTheme::block(int x, int y, int z, int height, int seaLevel) const {
    const int below = height - y;
    if (below >= 32)
        return Stone;
    if (beaches && height <= seaLevel + 1)
        return below < 4 ? Sand : Stone;
    const double h = height + (noise ? noise * edgeNoise(x, z, scale, seed) : 0);
    const SurfaceBand *band = &bands.front();
    for (const auto &candidate : bands)
        if (h >= candidate.minHeight)
            band = &candidate;
        else
            break;
    return below == 0 ? band->surface : below < band->depth ? band->subsurface : Stone;
}
bool validateTheme(const SurfaceTheme &t, size_t paletteSize, QString &error) {
    if (t.bands.empty() || t.bands.size() > 16 || t.noise < 0 || t.noise > 32 || t.scale < 1 ||
        t.scale > 256 || t.seed < -1000000 || t.seed > 1000000 || t.bands.front().minHeight != -63) {
        error = "主题需1–16个高度区间，首个起点为−63；边界变化0–32格、尺度1–256、种子±1000000";
        return false;
    }
    int previous = -64;
    for (const auto &b : t.bands) {
        if (b.minHeight <= previous || b.minHeight > 318 || b.name.trimmed().isEmpty() ||
            b.name.size() > 64 || b.depth < 1 || b.depth > 32 || b.surface == Air || b.subsurface == Air ||
            b.surface >= paletteSize || b.subsurface >= paletteSize) {
            error = "主题区间起点须递增且不重复；厚度1–32，名称及材料索引须有效";
            return false;
        }
        previous = b.minHeight;
    }
    return true;
}
QJsonObject saveTheme(const SurfaceTheme &theme) {
    QJsonArray bands;
    for (const auto &b : theme.bands)
        bands.append(QJsonObject{{"minHeight", b.minHeight},
                                 {"name", b.name},
                                 {"surface", int(b.surface)},
                                 {"subsurface", int(b.subsurface)},
                                 {"depth", b.depth}});
    return {{"enabled", theme.enabled}, {"beaches", theme.beaches}, {"noise", theme.noise},
            {"scale", theme.scale},     {"seed", theme.seed},       {"bands", bands}};
}
bool loadTheme(const QJsonObject &o, SurfaceTheme &theme, size_t paletteSize, QString &error) {
    auto integer = [](const QJsonValue &v, int lo, int hi) {
        return v.isDouble() && v.toDouble() == v.toInt(INT_MIN) && v.toInt() >= lo && v.toInt() <= hi;
    };
    auto fail = [&] {
        error = "工程地表主题数据无效";
        return false;
    };
    if (!o["enabled"].isBool() || !o["beaches"].isBool() || !integer(o["noise"], 0, 32) ||
        !integer(o["scale"], 1, 256) || !integer(o["seed"], -1000000, 1000000) || !o["bands"].isArray() ||
        o["bands"].toArray().size() > 16)
        return fail();
    SurfaceTheme t;
    t.enabled = o["enabled"].toBool();
    t.beaches = o["beaches"].toBool();
    t.noise = o["noise"].toInt();
    t.scale = o["scale"].toInt();
    t.seed = o["seed"].toInt();
    t.bands.clear();
    for (auto value : o["bands"].toArray()) {
        const auto b = value.toObject();
        if (!integer(b["minHeight"], -63, 318) || !b["name"].isString() ||
            !integer(b["surface"], 1, int(paletteSize) - 1) ||
            !integer(b["subsurface"], 1, int(paletteSize) - 1) || !integer(b["depth"], 1, 32))
            return fail();
        t.bands.push_back({b["minHeight"].toInt(), b["name"].toString(), Block(b["surface"].toInt()),
                           Block(b["subsurface"].toInt()), b["depth"].toInt()});
    }
    if (!validateTheme(t, paletteSize, error))
        return false;
    theme = std::move(t);
    return true;
}
} // namespace wb
