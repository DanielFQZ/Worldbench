#include "NbtPayload.h"
#include "World.h"
#include "WorldCreation.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace wb {
namespace {
bool air(const QString &s) {
    return s == "minecraft:air" || s == "minecraft:cave_air" || s == "minecraft:void_air";
}
double random01(int x, int z, uint32_t seed) {
    uint64_t n =
        uint64_t(int64_t(x)) * 0x9e3779b185ebca87ull ^ uint64_t(int64_t(z)) * 0xc2b2ae3d27d4eb4full ^ seed;
    n ^= n >> 30;
    n *= 0xbf58476d1ce4e5b9ull;
    n ^= n >> 27;
    n *= 0x94d049bb133111ebull;
    n ^= n >> 31;
    return (n >> 11) * 0x1.0p-53;
}
BlockState transformState(BlockState state, int turn, bool mx, bool mz) {
    auto dir = [&](QString s) {
        int x = s == "east" ? 1 : s == "west" ? -1 : 0, z = s == "south" ? 1 : s == "north" ? -1 : 0;
        if (x == 0 && z == 0)
            return s;
        if (mx)
            x = -x;
        if (mz)
            z = -z;
        for (int i = 0; i < turn; ++i) {
            int q = x;
            x = -z;
            z = q;
        }
        return x > 0   ? QString("east")
               : x < 0 ? QString("west")
               : z > 0 ? QString("south")
                       : QString("north");
    };
    auto old = state.properties;
    for (auto it = old.begin(); it != old.end(); ++it) {
        if (it.key() == "facing")
            state.properties[it.key()] = dir(it.value());
        if (QStringList{"north", "south", "east", "west"}.contains(it.key())) {
            state.properties.remove(it.key());
        }
    }
    for (auto it = old.begin(); it != old.end(); ++it)
        if (QStringList{"north", "south", "east", "west"}.contains(it.key()))
            state.properties[dir(it.key())] = it.value();
    if (turn % 2 && state.properties.contains("axis")) {
        auto &a = state.properties["axis"];
        if (a == "x")
            a = "z";
        else if (a == "z")
            a = "x";
    }
    if (state.name.endsWith("rail")) {
        const auto shape = state.properties.value("shape");
        if (shape.startsWith("ascending_"))
            state.properties["shape"] = "ascending_" + dir(shape.mid(10));
        else if (!shape.isEmpty()) {
            const auto ends = shape.split('_');
            if (ends.size() == 2) {
                const QSet<QString> directions{dir(ends[0]), dir(ends[1])};
                for (const auto &candidate :
                     {QString("north_south"), QString("east_west"), QString("south_east"),
                      QString("south_west"), QString("north_west"), QString("north_east")}) {
                    const auto pair = candidate.split('_');
                    if (directions == QSet<QString>{pair[0], pair[1]})
                        state.properties["shape"] = candidate;
                }
            }
        }
    }
    if (state.properties.contains("rotation")) {
        int r = state.properties["rotation"].toInt();
        if (mx)
            r = -r;
        if (mz)
            r = 8 - r;
        state.properties["rotation"] = QString::number((r + turn * 4 + 64) % 16);
    }
    if (mx != mz) {
        for (auto k : {"hinge", "type"}) {
            auto v = state.properties.value(k);
            if (v == "left")
                state.properties[k] = "right";
            else if (v == "right")
                state.properties[k] = "left";
        }
        auto shape = state.properties.value("shape");
        if (shape.endsWith("left"))
            shape.replace("left", "right");
        else if (shape.endsWith("right"))
            shape.replace("right", "left");
        if (!shape.isEmpty())
            state.properties["shape"] = shape;
    }
    return state;
}
} // namespace
CaveAnalysis analyzeAssetCave(const StructureAsset &a, const std::vector<CaveMarker> &markers) {
    CaveAnalysis r;
    r.width = a.width;
    r.height = a.height;
    r.depth = a.depth;
    int64_t count = int64_t(a.width) * a.height * a.depth;
    if (count < 1 || count > 16000000) {
        r.diagnostics << "洞穴分析超过1600万体素预算";
        return r;
    }
    std::vector<uint8_t> solid(size_t(count), 0);
    auto index = [&](int x, int y, int z) { return (y * a.depth + z) * a.width + x; };
    for (auto &b : a.blocks)
        if (!air(b.state)) {
            // Conservative discrete containment: partial models and fluids cannot seal
            // a voxel. Full glass is sealed; transparency alone is not the criterion.
            bool partial = (b.state.contains("_slab") && !b.state.contains("type=double")) ||
                           b.state.contains("_stairs") || b.state.contains("_door") ||
                           b.state.contains("_trapdoor") || b.state.contains("_fence") ||
                           b.state.contains("_wall") || b.state.contains("glass_pane") ||
                           b.state.contains("iron_bars") || b.state.startsWith("minecraft:water") ||
                           b.state.startsWith("minecraft:lava");
            if (!partial)
                solid[index(b.x, b.y, b.z)] = 1;
        }
    std::vector<QVector3D> portals, seeds;
    for (auto &m : markers)
        if (m.assetId == a.id && m.enabled) {
            if (m.kind == "interior") {
                seeds.push_back(m.position);
                continue;
            }
            for (int axis = 0; axis < 3; ++axis)
                if (m.position[axis] < 0 || m.end[axis] < 0 ||
                    m.position[axis] >= QVector3D(a.width, a.height, a.depth)[axis] ||
                    m.end[axis] >= QVector3D(a.width, a.height, a.depth)[axis]) {
                    r.diagnostics << "封口面超出资产范围";
                    return r;
                }
            for (int y = int(std::min(m.position.y(), m.end.y()));
                 y <= int(std::max(m.position.y(), m.end.y())); ++y)
                for (int z = int(std::min(m.position.z(), m.end.z()));
                     z <= int(std::max(m.position.z(), m.end.z())); ++z)
                    for (int x = int(std::min(m.position.x(), m.end.x()));
                         x <= int(std::max(m.position.x(), m.end.x())); ++x) {
                        auto i = index(x, y, z);
                        if (!solid[i]) {
                            solid[i] = 2;
                            if (m.kind != "seal")
                                portals.push_back({float(x), float(y), float(z)});
                        }
                    }
        }
    // All six boundary faces participate. Virtual caps block flood only, never become blocks.
    std::vector<uint8_t> outside(size_t(count), 0);
    std::vector<int> queue;
    auto push = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= a.width || y >= a.height || z >= a.depth)
            return;
        int i = index(x, y, z);
        if (!solid[i] && !outside[i]) {
            outside[i] = 1;
            queue.push_back(i);
        }
    };
    for (int z = 0; z < a.depth; ++z)
        for (int x = 0; x < a.width; ++x) {
            push(x, 0, z);
            push(x, a.height - 1, z);
        }
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            push(x, y, 0);
            push(x, y, a.depth - 1);
        }
    for (int y = 0; y < a.height; ++y)
        for (int z = 0; z < a.depth; ++z) {
            push(0, y, z);
            push(a.width - 1, y, z);
        }
    for (size_t k = 0; k < queue.size(); ++k) {
        int i = queue[k], x = i % a.width, z = i / a.width % a.depth, y = i / (a.width * a.depth);
        push(x - 1, y, z);
        push(x + 1, y, z);
        push(x, y - 1, z);
        push(x, y + 1, z);
        push(x, y, z - 1);
        push(x, y, z + 1);
    }
    r.labels.assign(size_t(count), 0);
    int inside = 0;
    for (size_t i = 0; i < solid.size(); ++i)
        if (!solid[i] && !outside[i]) {
            r.labels[i] = 1;
            ++inside;
        }
    for (auto p : portals)
        r.labels[index(int(p.x()), int(p.y()), int(p.z()))] = 2;
    for (auto p : seeds) {
        int x = int(p.x()), y = int(p.y()), z = int(p.z());
        if (x < 0 || y < 0 || z < 0 || x >= a.width || y >= a.height || z >= a.depth) {
            r.diagnostics << "内部种子超出范围";
            continue;
        }
        int i = index(x, y, z);
        if (solid[i])
            r.diagnostics << "内部种子位于实体或封口上";
        else if (outside[i]) {
            r.labels[i] = 3;
            r.diagnostics << "内部种子与外界连通：存在未封闭漏口";
        }
    }
    if (!inside)
        r.diagnostics << "未检测到封闭内部空间，请标记所有入口封口面";
    // Each separate chamber needs a portal or an explicit interior seed.
    // A forgotten sealed pocket is visible in the slice instead of being silently carved.
    std::vector<uint8_t> visited(solid.size(), 0);
    QSet<int> confirmedSeeds;
    for (auto p : seeds)
        if (p.x() >= 0 && p.y() >= 0 && p.z() >= 0 && p.x() < a.width && p.y() < a.height && p.z() < a.depth)
            confirmedSeeds.insert(index(int(p.x()), int(p.y()), int(p.z())));
    for (size_t start = 0; start < r.labels.size(); ++start) {
        if (r.labels[start] != 1 || visited[start])
            continue;
        ++r.chambers;
        std::vector<int> chamber{int(start)};
        visited[start] = 1;
        bool confirmed = false;
        for (size_t n = 0; n < chamber.size(); ++n) {
            const int i = chamber[n], x = i % a.width, z = i / a.width % a.depth, y = i / (a.width * a.depth);
            if (confirmedSeeds.contains(i))
                confirmed = true;
            for (const auto delta : {QVector3D(-1, 0, 0), QVector3D(1, 0, 0), QVector3D(0, -1, 0),
                                     QVector3D(0, 1, 0), QVector3D(0, 0, -1), QVector3D(0, 0, 1)}) {
                const int nx = x + int(delta.x()), ny = y + int(delta.y()), nz = z + int(delta.z());
                if (nx < 0 || ny < 0 || nz < 0 || nx >= a.width || ny >= a.height || nz >= a.depth)
                    continue;
                const int ni = index(nx, ny, nz);
                if (r.labels[ni] == 2)
                    confirmed = true;
                if (r.labels[ni] == 1 && !visited[ni]) {
                    visited[ni] = 1;
                    chamber.push_back(ni);
                }
            }
        }
        if (!confirmed) {
            ++r.unconfirmedChambers;
            for (auto i : chamber)
                r.labels[i] = 4;
        }
    }
    if (r.unconfirmedChambers)
        r.diagnostics << QString("%1 个孤立腔室未确认（紫色）；为每个需挖空的腔室添加内部种子。")
                             .arg(r.unconfirmedChambers);
    r.valid = r.diagnostics.isEmpty();
    return r;
}
namespace {
struct ReservedBox {
    QVector3D min, max;
    bool contains(QVector3D v) const {
        for (int i = 0; i < 3; ++i)
            if (v[i] < min[i] || v[i] > max[i])
                return false;
        return true;
    }
};
ReservedBox reservation(const AssetPreset &p, const StructureAsset *a) {
    ReservedBox box{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    for (int corner = 0; corner < 8; ++corner) {
        QVector3D v((corner & 1) ? a->width - 1 : 0, (corner & 2) ? a->height - 1 : 0,
                    (corner & 4) ? a->depth - 1 : 0);
        v -= a->pivot;
        v *= p.scale;
        QVector3D padding(std::abs(p.scale.x()) - 1, std::abs(p.scale.y()) - 1, std::abs(p.scale.z()) - 1);
        for (int i = 0; i < (int(p.rotation.y() / 90) % 4 + 4) % 4; ++i) {
            v = {-v.z(), v.y(), v.x()};
            padding = {padding.z(), padding.y(), padding.x()};
        }
        v += p.position;
        for (int axis = 0; axis < 3; ++axis) {
            box.min[axis] = std::min(box.min[axis], v[axis] - padding[axis]);
            box.max[axis] = std::max(box.max[axis], v[axis] + padding[axis]);
        }
    }
    return box;
}
void collectHoles(const World &w, GeneratedCreation &out, int id) {
    const auto *page = out.cells.page(id);
    if (!page)
        return;
    std::map<int, std::set<int>> rows;
    for (const auto &[key, b] : *page) {
        if (b != Air)
            continue;
        int x, y, z;
        w.decode(key, x, y, z);
        auto terrain = out.heights.find(w.column(x, z));
        const int height = terrain == out.heights.end() ? int(w.heights[w.column(x, z)]) : terrain->second;
        if (y == height &&
            (out.caveAir.contains(key) || !w.explicitOverride(key) || w.isAir(w.overrides.at(key))))
            rows[z].insert(x);
    }
    for (const auto &[z, xs] : rows)
        for (auto it = xs.begin(); it != xs.end();) {
            int first = *it++, last = first;
            while (it != xs.end() && *it == last + 1)
                last = *it++;
            out.surfaceHoles.emplace_back(first, z, last - first + 1, 1);
        }
}
} // namespace
bool canReuseScatter(const World &w, const AssetPreset &before, const AssetPreset &after) {
    if (!w.generated || !w.generated->procedural)
        return false;
    const auto &base = *w.generated->procedural;
    const auto asset = std::find_if(w.creation.assets.begin(), w.creation.assets.end(),
                                    [&](const auto &a) { return a.id == before.assetId; });
    if (asset == w.creation.assets.end())
        return false;
    const auto old = reservation(before, &*asset), next = reservation(after, &*asset);
    for (const auto &point : base.reservationWitnesses)
        if (old.contains(point) && !next.contains(point))
            return false;
    const int x0 = std::max(0, floorDiv(int(std::floor(next.min.x())) - w.originX, 16));
    const int z0 = std::max(0, floorDiv(int(std::floor(next.min.z())) - w.originZ, 16));
    const int x1 = std::min(w.chunksX() - 1, floorDiv(int(std::ceil(next.max.x())) - w.originX, 16));
    const int z1 = std::min(w.chunksZ() - 1, floorDiv(int(std::ceil(next.max.z())) - w.originZ, 16));
    for (int cz = z0; cz <= z1; ++cz)
        for (int cx = x0; cx <= x1; ++cx)
            if (const auto *page = base.geometryCells.page(cz * w.chunksX() + cx))
                for (const auto &[key, flag] : *page) {
                    int x, y, z;
                    w.decode(key, x, y, z);
                    const QVector3D point(x, y, z);
                    if (next.contains(point) && !old.contains(point))
                        return false;
                }
    return true;
}
std::shared_ptr<const GeneratedCreation> generateCreation(World &w, QString &error,
                                                          const CreationProgress &progress,
                                                          bool reuseMaterials, bool reuseScatter) {
    if (!reuseScatter && !validateCreation(w.creation, error))
        return {};
    auto out = std::make_shared<GeneratedCreation>(w.width, w.depth);
    bool placingManual = false;
    if (w.creation.volumes.empty() && w.creation.presets.empty() && w.creation.roadEdges.empty())
        return out;
    try {
        size_t ticks = 0;
        auto checkpoint = [&](int percent, const QString &stage) {
            if (progress && !progress(percent, stage))
                throw std::runtime_error("已取消生成，工程保持不变");
        };
        checkpoint(0, "校验并生成创作对象");
        QHash<QString, Block> palette;
        auto block = [&](const QString &text) {
            auto found = palette.constFind(text);
            if (found != palette.end())
                return *found;
            BlockState state;
            QString e;
            if (!parseCreationBlock(text, state, e))
                throw std::runtime_error(e.toStdString());
            if (w.assets && w.assets->catalog.contains(state.name) && !w.assets->validate(state, e))
                throw std::runtime_error(e.toStdString());
            Block b = w.intern(state);
            palette[text] = b;
            return b;
        };
        auto put = [&](int x, int y, int z, Block b, bool geometry = false, bool terrainPatch = true) {
            if (!w.contains(x, y, z) || y == World::MinY)
                return;
            if (placingManual)
                out->placementPages.insert((z - w.originZ) / 16 * w.chunksX() + (x - w.originX) / 16);
            if ((++ticks & 2047) == 0)
                checkpoint(40, "生成方块 / 结构");
            if (out->cells.size() >= 2000000 && !out->cells.contains(w.key(x, y, z)))
                throw std::runtime_error("生成结果超过200万方块预算，请缩小范围/降低密度");
            out->cells[w.key(x, y, z)] = b;
            if (!out->caveAir.empty())
                out->caveAir.erase(w.key(x, y, z));
            out->objectCells.erase(w.key(x, y, z));
            out->blockEntities.erase(w.key(x, y, z));
            if (geometry)
                out->geometryCells[w.key(x, y, z)] = 1;
            if (geometry && terrainPatch)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int cx = (x - w.originX) / 16 + dx, cz = (z - w.originZ) / 16 + dz;
                        if (cx >= 0 && cz >= 0 && cx < w.chunksX() && cz < w.chunksZ())
                            out->geometryChunks.insert(cz * w.chunksX() + cx);
                    }
        };
        auto baseHeight = [&](int x, int z) {
            return w.contains(x, 0, z) ? int(w.heights[w.column(x, z)]) : World::MinY;
        };
        auto height = [&](int x, int z) {
            auto it = out->heights.find(w.column(x, z));
            return it == out->heights.end() ? baseHeight(x, z) : it->second;
        };
        QHash<QString, const StructureAsset *> assets;
        for (auto &a : w.creation.assets)
            assets[a.id] = &a;
        std::vector<ReservedBox> reserved;
        for (const auto &p : w.creation.presets) {
            const auto *asset = assets.value(p.assetId);
            if (asset && p.enabled && asset->placement != "ignore")
                reserved.push_back(reservation(p, asset));
        }
        QHash<QString, CaveAnalysis> caves;
        size_t cachedStampCells = 0;
        auto place = [&](const AssetPreset &p, bool scatter, int embed = 0, QString owner = {}) -> bool {
            auto a = assets.value(p.assetId);
            if (!a || a->placement == "ignore")
                return false;
            int turn = (int(p.rotation.y() / 90) % 4 + 4) % 4;
            bool mx = p.scale.x() < 0, mz = p.scale.z() < 0;
            int sx = std::clamp(int(std::round(std::abs(p.scale.x()))), 1, 8),
                sy = std::clamp(int(std::round(std::abs(p.scale.y()))), 1, 8),
                sz = std::clamp(int(std::round(std::abs(p.scale.z()))), 1, 8);
            auto pos = [&](int x, int y, int z) {
                x -= int(a->pivot.x()) * sx;
                y -= int(a->pivot.y()) * sy;
                z -= int(a->pivot.z()) * sz;
                if (mx)
                    x = -x;
                if (mz)
                    z = -z;
                for (int i = 0; i < turn; ++i) {
                    int q = x;
                    x = -z;
                    z = q;
                }
                return QVector3D(float(x) + p.position.x(), float(y) + p.position.y(),
                                 float(z) + p.position.z());
            };
            std::vector<std::pair<QVector3D, Block>> writes;
            std::vector<std::pair<QVector3D, QByteArray>> nbtWrites;
            auto stamp = reuseScatter && !scatter ? w.generated->stamps.value(p.id) : nullptr;
            if (stamp) {
                writes = stamp->voxels;
                const auto offset = p.position - stamp->transform.position;
                for (auto &[v, block] : writes)
                    v += offset;
                for (const auto &[from, raw] : stamp->blockEntities) {
                    const auto v = from + offset;
                    nbtWrites.emplace_back(v, offset.isNull()
                                                  ? raw
                                                  : patchNbtPayload(raw, {{"x", nbtInt("x", int(v.x()))},
                                                                          {"y", nbtInt("y", int(v.y()))},
                                                                          {"z", nbtInt("z", int(v.z()))}}));
                }
                out->stamps.insert(p.id, stamp);
                cachedStampCells += writes.size();
            } else {
                if (int64_t(a->blocks.size()) * sx * sy * sz > 2000000)
                    throw std::runtime_error("缩放结构超过生成预算");
                writes.reserve(a->blocks.size() * sx * sy * sz);
                for (auto &b : a->blocks) {
                    if (air(b.state))
                        continue;
                    BlockState state;
                    QString e;
                    if (!parseCreationBlock(b.state, state, e))
                        throw std::runtime_error(e.toStdString());
                    auto transformed = transformState(state, turn, mx, mz);
                    Block value = a->placement == "carve" ? Air : block(transformed.label());
                    for (int yy = 0; yy < sy; ++yy)
                        for (int zz = 0; zz < sz; ++zz)
                            for (int xx = 0; xx < sx; ++xx) {
                                auto v = pos(b.x * sx + xx, b.y * sy + yy, b.z * sz + zz);
                                writes.push_back({v, value});
                                if (!b.nbt.isEmpty() && value != Air)
                                    nbtWrites.push_back(
                                        {v, patchNbtPayload(b.nbt, {{"x", nbtInt("x", int(v.x()))},
                                                                    {"y", nbtInt("y", int(v.y()))},
                                                                    {"z", nbtInt("z", int(v.z()))}})});
                            }
                }
                if (a->placement == "cave") {
                    if (!caves.contains(a->id))
                        caves[a->id] = analyzeAssetCave(*a, w.creation.caveMarkers);
                    auto &analysis = caves[a->id];
                    if (!analysis.valid)
                        throw std::runtime_error(
                            ("洞穴 " + a->name + "：" + analysis.diagnostics.join("；")).toStdString());
                    for (int y = 0; y < a->height; ++y)
                        for (int z = 0; z < a->depth; ++z)
                            for (int x = 0; x < a->width; ++x) {
                                auto label = analysis.labels[(y * a->depth + z) * a->width + x];
                                if (label == 1 || label == 2)
                                    for (int yy = 0; yy < sy; ++yy)
                                        for (int zz = 0; zz < sz; ++zz)
                                            for (int xx = 0; xx < sx; ++xx)
                                                writes.push_back(
                                                    {pos(x * sx + xx, y * sy + yy, z * sz + zz), Air});
                            }
                }
                if (!scatter && cachedStampCells + writes.size() <= 262144) {
                    out->stamps.insert(p.id, std::make_shared<const GeneratedCreation::PlacementStamp>(
                                                 GeneratedCreation::PlacementStamp{p, writes, nbtWrites}));
                    cachedStampCells += writes.size();
                }
            }
            if (!scatter)
                for (auto &[v, b] : writes)
                    if (!w.contains(int(v.x()), int(v.y()), int(v.z())) || int(v.y()) == World::MinY)
                        throw std::runtime_error("结构超出地图或高度边界；移动范围后重试，未裁掉任何方块");
            // A whole scatter candidate is accepted or rejected; no clipped half-trees.
            if (scatter)
                for (auto &[v, b] : writes) {
                    int x = int(v.x()), y = int(v.y()), z = int(v.z());
                    if (!w.contains(x, y, z) || y == World::MinY ||
                        out->geometryCells.contains(w.key(x, y, z)) ||
                        out->heights.contains(w.column(x, z)) || w.explicitOverride(w.key(x, y, z)) ||
                        (y <= height(x, z) - embed && !w.isAir(b)))
                        return false;
                    for (const auto &box : reserved)
                        if (box.contains(v)) {
                            out->reservationWitnesses.push_back(v);
                            return false;
                        }
                }
            if (a->groundBlend != 0 && !a->isOpen() && a->placement == "place" && !scatter) {
                std::map<std::pair<int, int>, int> bottom;
                // Only the marked base/root plane may grow a foundation.
                // Canopies, overhangs and balconies must never sprout stone.
                const int baseY = int(p.position.y());
                for (auto &[v, b] : writes)
                    if (b != Air && int(v.y()) <= baseY) {
                        auto k = std::pair{int(v.x()), int(v.z())};
                        if (!bottom.contains(k))
                            bottom[k] = int(v.y());
                        else
                            bottom[k] = std::min(bottom[k], int(v.y()));
                    }
                for (auto [xz, y] : bottom) {
                    int h = height(xz.first, xz.second);
                    // Foundation fill must connect to real ground. Never suspend stone
                    // below a floating base, and keep plant roots exactly as authored.
                    if (y - h - 1 > a->groundBlend)
                        continue;
                    if (a->groundBlend == 1 && h >= y)
                        for (int yy = y; yy <= h; ++yy)
                            put(xz.first, yy, xz.second, Air, true);
                    for (int yy = std::max(h + 1, y - std::max(1, a->groundBlend)); yy < y; ++yy)
                        put(xz.first, yy, xz.second, Stone, true);
                }
            }
            const size_t objectIndex = out->objects.size();
            const bool solidInstance = a->placement == "place" || a->placement == "cave";
            if (solidInstance)
                out->objects.push_back({p.id, owner.isEmpty() ? p.id : owner, p});
            for (auto &[v, b] : writes) {
                put(int(v.x()), int(v.y()), int(v.z()), b, true, !solidInstance || b == Air);
                if (a->placement == "cave" && b == Air)
                    out->caveAir[w.key(int(v.x()), int(v.y()), int(v.z()))] = 1;
                if (solidInstance && b != Air)
                    out->objectCells[w.key(int(v.x()), int(v.y()), int(v.z()))] = objectIndex;
            }
            for (auto &[v, raw] : nbtWrites)
                out->blockEntities[w.key(int(v.x()), int(v.y()), int(v.z()))] = raw;
            if (a->placement == "place")
                for (auto &e : a->entities) {
                    QVector3D v((e.position.x() - a->pivot.x()) * sx, (e.position.y() - a->pivot.y()) * sy,
                                (e.position.z() - a->pivot.z()) * sz);
                    if (mx)
                        v.setX(-v.x());
                    if (mz)
                        v.setZ(-v.z());
                    for (int i = 0; i < turn; ++i)
                        v = {-v.z(), v.y(), v.x()};
                    v += p.position;
                    if (!w.contains(int(std::floor(v.x())), int(std::floor(v.y())), int(std::floor(v.z()))))
                        throw std::runtime_error("实体超出工程边界");
                    QByteArray identity = p.id.toUtf8() + QByteArray::number(v.x()) + "," +
                                          QByteArray::number(v.y()) + "," + QByteArray::number(v.z()) + e.nbt;
                    out->entities.push_back({v, transformEntityNbt(e.nbt, v, turn, mx, mz, identity)});
                }
            if (a->placement == "cave")
                for (auto &m : w.creation.caveMarkers)
                    if (m.assetId == a->id && m.enabled && (m.kind == "portal" || m.kind == "exit")) {
                        auto v =
                            pos(int(m.position.x()) * sx, int(m.position.y()) * sy, int(m.position.z()) * sz);
                        bool exposed = false;
                        for (QVector3D delta :
                             {QVector3D(-1, 0, 0), QVector3D(1, 0, 0), QVector3D(0, -1, 0),
                              QVector3D(0, 1, 0), QVector3D(0, 0, -1), QVector3D(0, 0, 1)}) {
                            auto q = v + delta;
                            int x = int(q.x()), y = int(q.y()), z = int(q.z());
                            if (w.contains(x, y, z) && y > height(x, z) && y > w.seaLevel &&
                                !out->cells.contains(w.key(x, y, z)))
                                exposed = true;
                        }
                        if (!exposed)
                            out->diagnostics
                                << "洞穴 " + a->name + " 的标记 " + m.id +
                                       " 在当前放置位置未直接连接地表；可移动实例或手工挖通出口。";
                    }
            return true;
        };
        if (reuseScatter) {
            const auto base = w.generated->procedural;
            *out = *base;
            out->procedural = base;
        } else {
            // Roads: explicit graph connections, surface-following or grade-controlled bridge/tunnel.
            for (auto &edge : w.creation.roadEdges)
                if (edge.enabled) {
                    auto line = buildRoadPolyline(w.creation.roadNodes, edge, .5f);
                    if (line.empty())
                        continue;
                    auto enabled = [&](QString id) {
                        for (auto &n : w.creation.roadNodes)
                            if (n.id == id)
                                return n.enabled;
                        return false;
                    };
                    if (!enabled(edge.from) || !enabled(edge.to))
                        continue;
                    Block road = block(edge.surface);
                    float radius = edge.width * .5f;
                    std::set<std::pair<int, int>> visited;
                    std::vector<int> grades;
                    grades.reserve(line.size());
                    for (auto p : line)
                        grades.push_back(
                            int(std::round(edge.bridge || edge.tunnel
                                               ? p.y()
                                               : .65 * baseHeight(int(p.x()), int(p.z())) + .35 * p.y())));
                    const double maxStep = std::tan(edge.maxSlope * 3.141592653589793 / 180) * .5;
                    std::vector<double> smooth(grades.begin(), grades.end());
                    for (size_t i = 1; i < smooth.size(); ++i)
                        smooth[i] = std::clamp(smooth[i], smooth[i - 1] - maxStep, smooth[i - 1] + maxStep);
                    for (size_t i = smooth.size() - 1; i > 0; --i)
                        smooth[i - 1] = std::clamp(smooth[i - 1], smooth[i] - maxStep, smooth[i] + maxStep);
                    size_t sampleIndex = 0;
                    for (auto p : line) {
                        int center = baseHeight(int(p.x()), int(p.z()));
                        int grade = int(std::round(p.y()));
                        int target = int(std::round(smooth[sampleIndex++]));
                        for (int z = int(std::floor(p.z() - radius)); z <= int(std::ceil(p.z() + radius));
                             ++z)
                            for (int x = int(std::floor(p.x() - radius)); x <= int(std::ceil(p.x() + radius));
                                 ++x) {
                                if (!w.contains(x, 0, z) || std::hypot(x - p.x(), z - p.z()) > radius ||
                                    !visited.insert({x, z}).second)
                                    continue;
                                target = std::clamp(target, World::MinY + 1, World::MaxY - 8);
                                if (!edge.bridge && !edge.tunnel) {
                                    out->heights[w.column(x, z)] = target;
                                    if (out->heights.size() > 1000000)
                                        throw std::runtime_error("道路影响超过100万地形列");
                                }
                                put(x, target, z, road, edge.bridge || edge.tunnel);
                                if (edge.tunnel)
                                    for (int y = target + 1; y <= target + edge.clearance; ++y)
                                        put(x, y, z, Air, true);
                            }
                    }
                }
            size_t columns = 0;
            auto runs = std::make_shared<std::vector<GeneratedCreation::MaterialRun>>();
            const auto cached = reuseMaterials && w.generated ? w.generated->materialRuns : nullptr;
            out->materialRuns = cached ? cached : runs;
            if (cached)
                for (const auto &run : *cached)
                    for (int i = 0; i < run.depth; ++i)
                        put(run.x, run.y - i, run.z, i ? run.under : run.surface);
            for (int phase = 0; phase < 2; ++phase)
                for (auto &volume : w.creation.volumes)
                    if (volume.enabled && volume.visible) {
                        auto graph = std::find_if(w.creation.graphs.begin(), w.creation.graphs.end(),
                                                  [&](auto &g) { return g.id == volume.graphId; });
                        if (graph == w.creation.graphs.end() || !graph->enabled)
                            continue;
                        PcgPlan plan;
                        if (!compileGraph(*graph, plan, error))
                            return {};
                        struct Output {
                            size_t index;
                            uint32_t seed;
                            int spacing;
                        };
                        std::vector<Output> outputs;
                        for (size_t i = 0; i < plan.instructions.size(); ++i) {
                            const auto &n = plan.instructions[i];
                            if (n.type != (phase == 0 ? "output_surface" : "output_scatter"))
                                continue;
                            uint32_t hash = 2166136261u;
                            for (auto c :
                                 ((volume.seedDomain.isEmpty() ? volume.id : volume.seedDomain) + "/" + n.id)
                                     .toUtf8())
                                hash = (hash ^ uint8_t(c)) * 16777619u;
                            outputs.push_back({i, volume.seed ^ hash,
                                               std::clamp(n.parameters["spacing"].toInt(12), 2, 128)});
                        }
                        const auto inverse = volume.orientation().inverted();
                        const auto footprint = volume.footprint();
                        const int x0 = std::max(w.originX, int(std::floor(footprint.left()))),
                                  z0 = std::max(w.originZ, int(std::floor(footprint.top()))),
                                  x1 = std::min(w.originX + w.width, int(std::ceil(footprint.right()))),
                                  z1 = std::min(w.originZ + w.depth, int(std::ceil(footprint.bottom())));
                        if (x0 >= x1 || z0 >= z1)
                            continue;
                        if (phase == 0) {
                            columns += size_t(x1 - x0) * (z1 - z0);
                            if (columns > 1000000)
                                throw std::runtime_error("PCG本次求值超过100万列预算，请将范围分拆或缩小");
                        }
                        if (outputs.empty() || (phase == 0 && cached))
                            continue;
                        std::vector<std::vector<int>> candidates;
                        if (phase == 1) {
                            candidates.resize(z1 - z0);
                            for (const auto &o : outputs) {
                                int step = o.spacing;
                                for (int gz = floorDiv(z0, step); gz <= floorDiv(z1 - 1, step); ++gz)
                                    for (int gx = floorDiv(x0, step); gx <= floorDiv(x1 - 1, step); ++gx) {
                                        int x = gx * step + step / 4 +
                                                int(random01(gx, gz, o.seed + 5) * step * .5);
                                        int z = gz * step + step / 4 +
                                                int(random01(gx, gz, o.seed + 7) * step * .5);
                                        if (x >= x0 && x < x1 && z >= z0 && z < z1)
                                            candidates[z - z0].push_back(x);
                                    }
                            }
                            for (auto &row : candidates) {
                                std::sort(row.begin(), row.end());
                                row.erase(std::unique(row.begin(), row.end()), row.end());
                            }
                        }
                        HeightField without = w.heights;
                        bool bound = !volume.sourceId.isEmpty(), sourceEnabled = false;
                        if (bound) {
                            for (auto &i : without.instances)
                                if (i.id == volume.sourceId) {
                                    sourceEnabled = i.enabled;
                                    i.enabled = false;
                                }
                            if (std::none_of(without.instances.begin(), without.instances.end(),
                                             [&](auto &i) { return i.id == volume.sourceId; }))
                                throw std::runtime_error("PCG绑定的地貌实例不存在");
                            if (!sourceEnabled)
                                continue;
                        }
                        for (int z = z0; z < z1; ++z) {
                            checkpoint(10 + (z - z0) * 70 / std::max(1, z1 - z0),
                                       phase == 0 ? "计算地表字段" : "计算植被分布");
                            const int count = phase == 1 ? int(candidates[z - z0].size()) : x1 - x0;
                            for (int k = 0; k < count; ++k) {
                                const int x = phase == 1 ? candidates[z - z0][k] : x0 + k;
                                int h = height(x, z);
                                const auto local = inverse.map(QVector3D(x + .5f, h, z + .5f));
                                if (local.x() < volume.min.x() || local.x() >= volume.max.x() ||
                                    local.z() < volume.min.z() || local.z() >= volume.max.z() ||
                                    local.y() < volume.min.y() || local.y() >= volume.max.y())
                                    continue;
                                double mask =
                                    volume.transition > 0
                                        ? std::clamp(std::min({local.x() - double(volume.min.x()),
                                                               double(volume.max.x()) - local.x(),
                                                               local.z() - double(volume.min.z()),
                                                               double(volume.max.z()) - local.z()}) /
                                                         volume.transition,
                                                     0., 1.)
                                        : 1;
                                mask = mask * mask * (3 - 2 * mask);
                                if (bound && volume.contributionOnly) {
                                    double change = std::abs(int(w.heights[w.column(x, z)]) -
                                                             int(without[w.column(x, z)]));
                                    mask *= std::clamp(change, 0., 1.);
                                }
                                if (mask <= 0)
                                    continue;
                                double slope = std::atan(std::hypot(
                                                   (baseHeight(std::min(x + 1, w.originX + w.width - 1), z) -
                                                    baseHeight(std::max(x - 1, w.originX), z)) *
                                                       .5,
                                                   (baseHeight(x, std::min(z + 1, w.originZ + w.depth - 1)) -
                                                    baseHeight(x, std::max(z - 1, w.originZ))) *
                                                       .5)) *
                                               180 / 3.141592653589793;
                                auto values = plan.evaluateValues(
                                    {double(x), double(z), double(h), double(w.heights.base(w.column(x, z))),
                                     slope, local.x() - .5 - volume.min.x(), local.z() - .5 - volume.min.z(),
                                     mask, volume.seed, 1, &volume.overrides});
                                for (const auto &output : outputs) {
                                    auto &n = plan.instructions[output.index];
                                    auto &p = n.parameters;
                                    double value = std::clamp(values[output.index], 0., 1.);
                                    uint32_t seed = output.seed;
                                    if (phase == 0 && n.type == "output_surface") {
                                        if (out->heights.contains(w.column(x, z)))
                                            continue;
                                        if (value < p["threshold"].toDouble(.5) ||
                                            random01(x, z, seed) > mask)
                                            continue;
                                        QString surface = p["block"].toString("minecraft:stone");
                                        auto alternatives = p["alternatives"].toArray();
                                        double total = 0;
                                        for (auto a : alternatives)
                                            total += std::max(0., a.toObject()["weight"].toDouble(1));
                                        if (total > 0) {
                                            double r = random01(x, z, seed + 3) * total;
                                            for (auto a : alternatives) {
                                                auto o = a.toObject();
                                                r -= std::max(0., o["weight"].toDouble(1));
                                                if (r <= 0) {
                                                    surface = o["block"].toString();
                                                    break;
                                                }
                                            }
                                        }
                                        Block top = block(surface),
                                              under = block(p["under"].toString("minecraft:dirt"));
                                        int depth = std::clamp(p["depth"].toInt(3), 1, 64);
                                        // Caching is optional; bound storage even when many material outputs
                                        // overlap.
                                        if (runs) {
                                            if (runs->size() < 2000000)
                                                runs->push_back({x, h, z, depth, top, under});
                                            else {
                                                out->materialRuns.reset();
                                                runs.reset();
                                            }
                                        }
                                        for (int y = h; y > h - depth; --y)
                                            put(x, y, z, y == h ? top : under);
                                    } else if (phase == 1 && n.type == "output_scatter") {
                                        int spacing = std::clamp(p["spacing"].toInt(12), 2, 128);
                                        int gx = floorDiv(x, spacing), gz = floorDiv(z, spacing);
                                        int px = gx * spacing + spacing / 4 +
                                                 int(random01(gx, gz, seed + 5) * spacing * .5),
                                            pz = gz * spacing + spacing / 4 +
                                                 int(random01(gx, gz, seed + 7) * spacing * .5);
                                        if (x != px || z != pz || h <= w.seaLevel ||
                                            random01(gx, gz, seed + 11) >
                                                value * mask * std::clamp(p["density"].toDouble(.5), 0., 1.))
                                            continue;
                                        QString id = p["assetId"].toString();
                                        auto weighted = p["assets"].toArray();
                                        double total = 0;
                                        for (auto a : weighted)
                                            total += std::max(0., a.toObject()["weight"].toDouble(1));
                                        if (total > 0) {
                                            double r = random01(gx, gz, seed + 13) * total;
                                            for (auto a : weighted) {
                                                auto o = a.toObject();
                                                r -= std::max(0., o["weight"].toDouble(1));
                                                if (r <= 0) {
                                                    id = o["id"].toString();
                                                    break;
                                                }
                                            }
                                        }
                                        if (id.isEmpty())
                                            continue;
                                        AssetPreset instance;
                                        instance.assetId = id;
                                        const int embed = std::clamp(p["embed"].toInt(), 0, 64);
                                        instance.position = {float(x), float(h + 1 - embed), float(z)};
                                        instance.rotation.setY(p["rotation"].toBool(true)
                                                                   ? int(random01(gx, gz, seed + 17) * 4) *
                                                                         90.f
                                                                   : 0);
                                        int low = std::clamp(p["minScale"].toInt(1), 1, 8),
                                            high = std::clamp(p["maxScale"].toInt(1), low, 8);
                                        float scale =
                                            float(low + int(random01(gx, gz, seed + 19) * (high - low + 1)));
                                        instance.scale = {scale, scale, scale};
                                        instance.id = volume.id + "/" + n.id + "/" + QString::number(x) +
                                                      "/" + QString::number(z);
                                        if (place(instance, true, embed, volume.id))
                                            out->scatterPoints.push_back(instance.position);
                                    }
                                }
                            }
                        }
                    }
            for (const auto &[id, page] : out->cells.pages())
                collectHoles(w, *out, id);
            out->procedural = std::make_shared<const GeneratedCreation>(*out);
        }
        placingManual = true;
        for (auto &p : w.creation.presets)
            if (p.enabled)
                place(p, false);
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
        return {};
    }
    std::erase_if(out->surfaceHoles, [&](const QRect &r) {
        return out->placementPages.contains((r.y() - w.originZ) / 16 * w.chunksX() +
                                            (r.x() - w.originX) / 16);
    });
    for (const int id : out->placementPages)
        collectHoles(w, *out, id);
    if (progress)
        progress(100, "创作结果完成");
    return out;
}
} // namespace wb
