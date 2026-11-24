#include "World.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wb {
const BlockInfo &info(Block b) {
    static const BlockInfo types[] = {{"空气", "minecraft:air", 0, 0, 0, 0, false},
                                      {"草方块", "minecraft:grass_block", .36f, .58f, .26f, 1, true},
                                      {"泥土", "minecraft:dirt", .43f, .31f, .21f, 1, true},
                                      {"石头", "minecraft:stone", .51f, .55f, .59f, 1, true},
                                      {"沙子", "minecraft:sand", .78f, .72f, .51f, 1, true},
                                      {"水", "minecraft:water", .19f, .48f, .66f, .72f, true},
                                      {"橡木木板", "minecraft:oak_planks", .62f, .44f, .25f, 1, true},
                                      {"玻璃", "minecraft:glass", .66f, .85f, .9f, .30f, true},
                                      {"橡木半砖", "minecraft:oak_slab", .66f, .49f, .29f, 1, false},
                                      {"基岩", "minecraft:bedrock", .22f, .24f, .27f, 1, true}};
    return types[b < BlockCount ? b : Air];
}
int floorDiv(int v, int d) {
    return v >= 0 ? v / d : -((-v + d - 1) / d);
}
World::World() {
    for (int i = 0; i < BlockCount; ++i) {
        BlockState state{QString::fromUtf8(info(i).minecraft), {}};
        if (i == Grass)
            state.properties["snowy"] = "false";
        if (i == Water)
            state.properties["level"] = "0";
        if (i == Slab) {
            state.properties["type"] = "bottom";
            state.properties["waterlogged"] = "false";
        }
        palette.push_back(state);
        models.push_back(fallbackModel(i));
    }
}
Block World::intern(const BlockState &state) {
    for (size_t i = 0; i < palette.size(); ++i)
        if (palette[i] == state)
            return Block(i);
    if (palette.size() >= 65536)
        throw std::runtime_error("工程方块状态数量超过 65536");
    palette.push_back(state);
    models.push_back(assets ? assets->model(state) : fallbackModel(BlockCount));
    return Block(palette.size() - 1);
}
void World::setAssets(std::shared_ptr<const AssetLibrary> library) {
    assets = std::move(library);
    models.clear();
    if (assets) {
        resourcePaths = assets->sources;
        resourceVersion = assets->version;
    } else {
        resourcePaths.clear();
        resourceVersion.clear();
    }
    for (size_t i = 0; i < palette.size(); ++i)
        models.push_back(assets ? assets->model(palette[i]) : fallbackModel(int(i)));
}
World World::create(int n, int randomSeed) {
    if (n < 32 || n > 256 || n % 32)
        throw std::invalid_argument("P0 world creation requires size 32..256, aligned to 32");
    World w;
    w.width = w.depth = n;
    w.originX = w.originZ = -n / 2;
    w.seed = randomSeed;
    w.heights.configure(WorldOptions{n, randomSeed}, w.originX, w.originZ);
    std::vector<int16_t> legacy(n * n);
    const double phase = (randomSeed % 997) * .009;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const double wx = x + w.originX, wz = z + w.originZ;
            const double ridge = 17 * std::exp(-((wx - 12) * (wx - 12) + (wz + 9) * (wz + 9)) / 650.0);
            legacy[z * n + x] =
                int16_t(std::lround(13 + ridge + 4 * std::sin(wx * .075 + phase) * std::cos(wz * .06) -
                                    3 * std::cos(wz * .09 + phase)));
        }
    w.heights.assign(std::move(legacy));
    // Small native-block reference strip exercises partial geometry and alpha passes.
    for (int x = -12; x <= 0; ++x) {
        int y = w.height(x, 4) + 1;
        w.setBlock(x, y, 4, x < -8 ? Planks : x < -4 ? Slab : Glass);
    }
    return w;
}
World World::create(const WorldOptions &o) {
    const int depth = o.depth ? o.depth : o.width;
    if (o.width < 32 || o.width > 16384 || depth < 32 || depth > 16384 || o.seed < -1000000 ||
        o.seed > 1000000 || o.seaLevel < MinY || o.seaLevel >= MaxY || o.baseHeight < MinY + 1 ||
        o.baseHeight > MaxY - 2 || o.variation < 0 || o.variation > 128)
        throw std::invalid_argument("建图参数超出当前支持范围");
    World w;
    w.width = o.width;
    w.depth = depth;
    w.originX = -(w.width / 32) * 16;
    w.originZ = -(w.depth / 32) * 16;
    w.seed = o.seed;
    w.seaLevel = o.seaLevel;
    w.heights.configure(o, w.originX, w.originZ);
    return w;
}
bool World::isAir(Block b) const {
    if (b == Air)
        return true;
    if (b >= palette.size())
        return false;
    const auto &n = palette[b].name;
    return n == "minecraft:air" || n == "minecraft:cave_air" || n == "minecraft:void_air";
}
std::set<int> affectedChunks(const World &w, const Command &command) {
    std::set<int> chunks;
    const int n = w.chunksX();
    if (command.deltaAfter && command.heightArea.isValid()) {
        const auto area = command.heightArea.adjusted(-1, -1, 1, 1)
                              .intersected(QRect(w.originX, w.originZ, w.width, w.depth));
        for (int z = (area.top() - w.originZ) / 16; z <= (area.bottom() - w.originZ) / 16; ++z)
            for (int x = (area.left() - w.originX) / 16; x <= (area.right() - w.originX) / 16; ++x)
                chunks.insert(z * n + x);
    }
    auto add = [&](int x, int z) {
        // Include adjacent columns for face visibility at chunk boundaries.
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = floorDiv(x + dx - w.originX, 16), cz = floorDiv(z + dz - w.originZ, 16);
                if (cx >= 0 && cz >= 0 && cx < n && cz < w.chunksZ())
                    chunks.insert(cz * n + cx);
            }
    };
    for (const auto &h : command.heights)
        add(h.index % w.width + w.originX, h.index / w.width + w.originZ);
    for (const auto &c : command.cells) {
        int x, y, z;
        w.decode(c.key, x, y, z);
        add(x, z);
    }
    return chunks;
}
bool World::contains(int x, int y, int z) const {
    return x >= originX && z >= originZ && x < originX + width && z < originZ + depth && y >= MinY &&
           y < MaxY;
}
int World::column(int x, int z) const {
    return (z - originZ) * width + x - originX;
}
int World::height(int x, int z) const {
    if (!contains(x, 0, z))
        return MinY - 1;
    const int c = column(x, z);
    if (generated) {
        auto it = generated->heights.find(c);
        if (it != generated->heights.end())
            return it->second;
    }
    return heights[c];
}
bool World::rebuildCreation(QString &error, const CreationProgress &progress, bool reuseMaterials) {
    auto next = generateCreation(*this, error, progress, reuseMaterials);
    if (!next)
        return false;
    generated = std::move(next);
    return true;
}
uint64_t World::key(int x, int y, int z) const {
    return (uint64_t(y - MinY) * depth + (z - originZ)) * width + x - originX;
}
void World::decode(uint64_t k, int &x, int &y, int &z) const {
    x = int(k % width) + originX;
    k /= width;
    z = int(k % depth) + originZ;
    y = int(k / depth) + MinY;
}
Block World::baseBlock(int x, int y, int z) const {
    if (!contains(x, y, z))
        return Air;
    if (y == MinY)
        return Bedrock;
    if (generated) {
        auto it = generated->cells.find(key(x, y, z));
        if (it != generated->cells.end())
            return it->second;
    }
    const int h = height(x, z);
    return naturalBlock(x, y, z, h);
}
Block World::block(int x, int y, int z) const {
    if (!contains(x, y, z))
        return Air;
    if (y == MinY)
        return Bedrock;
    const auto k = key(x, y, z);
    auto edit = overrides.find(k);
    if (edit != overrides.end() && generated && generated->caveAir.contains(k))
        return Air;
    if (edit != overrides.end() && (!generated || !terrainOverrideKeys.contains(k)))
        return edit->second;
    if (generated) {
        auto cell = generated->cells.find(k);
        if (cell != generated->cells.end())
            return cell->second;
    }
    return edit != overrides.end() ? edit->second : naturalBlock(x, y, z, height(x, z));
}
Block World::blockAtHeight(int x, int y, int z, int h) const {
    if (!contains(x, y, z))
        return Air;
    if (y == MinY)
        return Bedrock;
    auto k = key(x, y, z);
    auto edit = overrides.find(k);
    if (edit != overrides.end() && generated && generated->caveAir.contains(k))
        return Air;
    if (generated && (edit == overrides.end() || terrainOverrideKeys.contains(k))) {
        auto cell = generated->cells.find(k);
        if (cell != generated->cells.end())
            return cell->second;
    }
    return edit == overrides.end() ? naturalBlock(x, y, z, h) : edit->second;
}
Block World::terrainBlock(int x, int y, int z, int h) const {
    if (!contains(x, y, z))
        return Air;
    if (y == MinY)
        return Bedrock;
    auto edit = overrides.find(key(x, y, z));
    return edit == overrides.end() ? naturalBlock(x, y, z, h) : edit->second;
}
Block World::naturalBlock(int x, int y, int z, int h) const {
    if (y > h)
        return y <= seaLevel ? Water : Air;
    if (theme.enabled)
        return theme.block(x, y, z, h, seaLevel);
    if (y == h)
        return h <= seaLevel + 1 ? Sand : Grass;
    return y >= h - 3 ? (h <= seaLevel + 1 ? Sand : Dirt) : Stone;
}
void World::setBlock(int x, int y, int z, Block b) {
    if (!contains(x, y, z) || y == MinY || b >= palette.size())
        return;
    const auto k = key(x, y, z);
    terrainOverrideKeys.erase(k);
    if (b == baseBlock(x, y, z))
        overrides.erase(k);
    else
        overrides[k] = b;
}
static bool boxIntersect(QVector3D o, QVector3D d, QVector3D lo, QVector3D hi, float &t, QVector3D &normal) {
    float nearT = -1e30f, farT = 1e30f;
    QVector3D n;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-8f) {
            if (o[a] < lo[a] || o[a] > hi[a])
                return false;
            continue;
        }
        float aT = (lo[a] - o[a]) / d[a], bT = (hi[a] - o[a]) / d[a];
        float sign = -1;
        if (aT > bT) {
            std::swap(aT, bT);
            sign = 1;
        }
        if (aT > nearT) {
            nearT = aT;
            n = {};
            n[a] = sign;
        }
        farT = std::min(farT, bT);
        if (nearT > farT)
            return false;
    }
    if (farT < 0)
        return false;
    t = std::max(0.0f, nearT);
    normal = n;
    return true;
}
Hit World::raycast(QVector3D o, QVector3D d, float maxDistance, bool terrainOnly) const {
    if (d.lengthSquared() < 1e-12f || !std::isfinite(d.lengthSquared()) ||
        !std::isfinite(o.lengthSquared()) || !std::isfinite(maxDistance) || maxDistance < 0)
        return {};
    d.normalize();
    const QVector3D modelRayOrigin = o;
    const float modelMaxDistance = maxDistance;
    float start = 0;
    QVector3D norm;
    if (!boxIntersect(o, d, {float(originX), float(MinY), float(originZ)},
                      {float(originX + width), float(MaxY), float(originZ + depth)}, start, norm) ||
        start > maxDistance)
        return {};
    // Rebase at the world AABB so an overview camera tens of thousands of
    // blocks away does not accumulate that distance in every voxel DDA step.
    o += d * start;
    maxDistance -= start;
    QVector3D p = o + d * .002f;
    int c[3] = {int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z()))};
    c[0] = std::clamp(c[0], originX, originX + width - 1);
    c[1] = std::clamp(c[1], MinY, MaxY - 1);
    c[2] = std::clamp(c[2], originZ, originZ + depth - 1);
    int step[3];
    float next[3], delta[3];
    for (int a = 0; a < 3; ++a) {
        step[a] = d[a] >= 0 ? 1 : -1;
        delta[a] = std::abs(d[a]) < 1e-8 ? 1e30f : std::abs(1 / d[a]);
        next[a] = std::abs(d[a]) < 1e-8 ? 1e30f : ((c[a] + (step[a] > 0 ? 1 : 0)) - o[a]) / d[a];
    }
    int lastX = originX - 1, lastZ = originZ - 1, columnHeight = MinY;
    for (int i = 0; i < width + depth + (MaxY - MinY) + 3 && contains(c[0], c[1], c[2]); ++i) {
        // Instance blending and sculpt layers are evaluated once per crossed
        // column, not once per Y cell. Explicit air and model faces remain exact.
        if (c[0] != lastX || c[2] != lastZ) {
            lastX = c[0];
            lastZ = c[2];
            columnHeight = terrainOnly ? int(heights[column(c[0], c[2])]) : height(c[0], c[2]);
        }
        const Block b = terrainOnly ? terrainBlock(c[0], c[1], c[2], columnHeight)
                                    : blockAtHeight(c[0], c[1], c[2], columnHeight);
        float closest = modelMaxDistance + 1;
        QVector3D normal;
        if (b != Air && (!terrainOnly || b != Water) && b < models.size())
            for (const auto &face : models[b]->faces) {
                const QVector3D cell{float(c[0]), float(c[1]), float(c[2])};
                for (int triangle = 0; triangle < 2; ++triangle) {
                    auto a = cell + face.points[0], edge1 = face.points[triangle + 1] - face.points[0],
                         edge2 = face.points[triangle + 2] - face.points[0];
                    auto h = QVector3D::crossProduct(d, edge2);
                    float det = QVector3D::dotProduct(edge1, h);
                    if (std::abs(det) < 1e-8f)
                        continue;
                    float inv = 1 / det;
                    // JSON elements may protrude beyond the world AABB. Keep
                    // the original model ray even though DDA uses a local origin.
                    auto s = modelRayOrigin - a;
                    float u = inv * QVector3D::dotProduct(s, h);
                    if (u < 0 || u > 1)
                        continue;
                    auto q = QVector3D::crossProduct(s, edge1);
                    float v = inv * QVector3D::dotProduct(d, q);
                    if (v < 0 || u + v > 1)
                        continue;
                    float t = inv * QVector3D::dotProduct(edge2, q);
                    if (t >= 0 && t < closest) {
                        closest = t;
                        normal = face.normal;
                    }
                }
            }
        if (closest <= modelMaxDistance)
            return {true, c[0], c[1], c[2], modelRayOrigin + d * closest, normal, b};
        int a = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        if (next[a] > maxDistance)
            break;
        c[a] += step[a];
        next[a] += delta[a];
    }
    return {};
}
void World::apply(const Command &cmd, bool forward) {
    if (cmd.deltaBefore && cmd.deltaAfter) {
        const int index = heights.layerIndex(cmd.heightLayer);
        if (index >= 0)
            heights.layers[index].deltas = forward ? cmd.deltaAfter : cmd.deltaBefore;
    }
    if (cmd.environmentBefore && cmd.environmentAfter)
        environment = *(forward ? cmd.environmentAfter : cmd.environmentBefore);
    if (cmd.themeBefore && cmd.themeAfter)
        theme = *(forward ? cmd.themeAfter : cmd.themeBefore);
    if (cmd.terrainBefore && cmd.terrainAfter)
        heights.instances = *(forward ? cmd.terrainAfter : cmd.terrainBefore);
    if (cmd.layersBefore && cmd.layersAfter) {
        heights.layers = *(forward ? cmd.layersAfter : cmd.layersBefore);
        if (heights.layerIndex(heights.activeLayer) < 0)
            heights.activeLayer = heights.layers.front().id;
    }
    if (cmd.creationBefore && cmd.creationAfter)
        creation = *(forward ? cmd.creationAfter : cmd.creationBefore);
    for (auto c : cmd.heights)
        heights.restoreDelta(cmd.heightLayer.isEmpty() ? heights.activeLayer : cmd.heightLayer, c.index,
                             forward ? c.after : c.before);
    for (auto c : cmd.cells) {
        const int v = forward ? c.after : c.before;
        if (v < 0)
            overrides.erase(c.key);
        else
            overrides[c.key] = Block(v);
        if (forward ? c.terrainAfter : c.terrainBefore)
            terrainOverrideKeys.insert(c.key);
        else
            terrainOverrideKeys.erase(c.key);
    }
}
void Stroke::changeCell(int x, int y, int z, Block b) {
    if (!world.contains(x, y, z) || y == World::MinY)
        return;
    const int h = world.heights[world.column(x, z)];
    if (world.terrainBlock(x, y, z, h) == b)
        return;
    const auto k = world.key(x, y, z);
    // Explicit edits of the structure (e.g. MCP) remain above both layers.
    if (world.generated && world.generated->cells.contains(k) && world.explicitOverride(k))
        return;
    if (!beforeCells.contains(k)) {
        auto it = world.overrides.find(k);
        beforeCells[k] = {it == world.overrides.end() ? -1 : int(it->second),
                          world.terrainOverrideKeys.contains(k)};
    }
    if (world.naturalBlock(x, y, z, h) == b) {
        world.overrides.erase(k);
        world.terrainOverrideKeys.erase(k);
    } else {
        world.overrides[k] = b;
        world.terrainOverrideKeys.insert(k);
    }
    ++changes;
}
void Stroke::changeHeight(int i, int h) {
    h = std::clamp(h, World::MinY + 1, World::MaxY - 2);
    if (h == world.heights[i])
        return;
    beforeHeights.try_emplace(i, world.heights.delta(i));
    world.heights.set(i, h);
    ++changes;
}
void Stroke::reshapeSurface(int x, int z, int target, const BrushOptions &o) {
    target = std::clamp(target, World::MinY + 1, World::MaxY - 2);
    const int oldHeight = world.heights[world.column(x, z)];
    // Snapshot the authoritative column before changing the height-derived base.
    // Air overrides are meaningful: retain them below the new cap, never erase a cave.
    std::array<Block, World::MaxY - World::MinY> original;
    std::array<bool, World::MaxY - World::MinY> pinned;
    int upper = std::max(oldHeight, target);
    for (int y = World::MinY; y < World::MaxY; ++y) {
        const auto b = world.terrainBlock(x, y, z, oldHeight);
        original[y - World::MinY] = b;
        pinned[y - World::MinY] = world.overrides.contains(world.key(x, y, z));
        if (!world.isAir(b) && b != Water)
            upper = std::max(upper, y);
    }
    Block cap = original[oldHeight - World::MinY];
    if (world.isAir(cap) || cap == Water || !world.models[cap]->full)
        cap = target <= world.seaLevel + 1 ? Sand : Grass;
    if (world.theme.enabled && !pinned[oldHeight - World::MinY])
        cap = world.theme.block(x, target, z, target, world.seaLevel);
    changeHeight(world.column(x, z), target);
    for (int y = World::MinY + 1; y <= upper; ++y) {
        if (y == target)
            changeCell(x, y, z, cap);
        else if (y < target) {
            const auto b = original[y - World::MinY];
            // Generated solid soil follows the new theme; explicit material and air stay pinned.
            if (world.theme.enabled && !pinned[y - World::MinY] && !world.isAir(b) && b != Water)
                continue;
            changeCell(x, y, z,
                       o.fillHoles && target - y <= std::clamp(o.fillDepth, 1, 383) && world.isAir(b) ? Stone
                                                                                                      : b);
        } else
            changeCell(x, y, z, world.isAir(original[y - World::MinY]) ? original[y - World::MinY] : Air);
    }
}
bool Stroke::stamp(const Hit &hit, int tool, int radius, int strength, Block mat, bool reverse,
                   const BrushOptions &o) {
    if (!hit.valid || tool < 0 || tool > 5 ||
        (tool == 0 || tool == 1 || tool == 5) &&
            !world.heights.layers.at(world.heights.layerIndex(layer)).enabled)
        return false;
    const auto serial = changes;
    if (flattenHeight == -9999)
        flattenHeight = hit.y;
    radius = std::clamp(radius, 1, 16);
    strength = std::clamp(strength, 1, 4);
    ++stampIndex;
    const auto heights = world.heights; // Stable slopes and smooth kernel, independent of iteration order.
    auto height = [&](int x, int z) {
        x = std::clamp(x, world.originX, world.originX + world.width - 1);
        z = std::clamp(z, world.originZ, world.originZ + world.depth - 1);
        return int(heights[world.column(x, z)]);
    };
    auto allows = [&](int x, int y, int z) {
        if (!world.contains(x, y, z))
            return false;
        if (o.selectionEnabled &&
            (x < o.selectionMinX || x > o.selectionMaxX || z < o.selectionMinZ || z > o.selectionMaxZ))
            return false;
        if (!o.filterEnabled)
            return true;
        if (y < o.minHeight || y > o.maxHeight)
            return false;
        const float dx = (height(x + 1, z) - height(x - 1, z)) * .5f,
                    dz = (height(x, z + 1) - height(x, z - 1)) * .5f;
        const float slope = std::atan(std::hypot(dx, dz)) * 180.f / 3.14159265f;
        if (slope < o.minSlope || slope > o.maxSlope)
            return false;
        const bool match = world.palette[world.terrainBlock(x, y, z, height(x, z))].name == o.filterBlock;
        return o.materialFilter == 0 || (o.materialFilter == 1 ? match : !match);
    };
    if (tool <= 1 || tool == 5) {
        for (int z = hit.z - radius; z <= hit.z + radius; ++z)
            for (int x = hit.x - radius; x <= hit.x + radius; ++x) {
                if (!world.contains(x, 0, z))
                    continue;
                const float distance = o.shape == 1
                                           ? float(std::max(std::abs(x - hit.x), std::abs(z - hit.z)))
                                           : std::hypot(float(x - hit.x), float(z - hit.z));
                if (distance > radius)
                    continue;
                const int i = world.column(x, z), h = heights[i];
                if (!allows(x, h, z))
                    continue;
                const double inner = radius * (1.0 - std::clamp(o.decay, 0.0, 1.0));
                double weight =
                    distance <= inner || o.decay <= 0
                        ? 1.0
                        : std::clamp((radius - distance) / std::max(.001, radius - inner), 0.0, 1.0);
                if (o.falloff == 1)
                    weight = 1;
                if (o.falloff == 2)
                    weight = weight * weight * (3 - 2 * weight);
                if (weight <= 0)
                    continue;
                double &accumulated = m_accumulated[i];
                accumulated += strength * weight;
                const int amount = int(std::floor(accumulated + 1e-9));
                accumulated -= amount;
                if (tool == 0)
                    changeHeight(i, h + (reverse ? -amount : amount));
                else {
                    int target = flattenHeight;
                    if (tool == 5) {
                        int sum = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                                sum += height(x + dx, z + dz);
                        target = int(std::lround(sum / 9.));
                    }
                    if (tool == 1 &&
                        ((o.flattenMode == 1 && target < h) || (o.flattenMode == 2 && target > h)))
                        continue;
                    auto options = o;
                    if (tool == 5)
                        options.fillHoles = false;
                    reshapeSurface(x, z, h + std::clamp(target - h, -amount, amount), options);
                }
            }
    } else {
        const bool add = (tool == 3) ^ reverse;
        QVector3D center(hit.x + .5f, hit.y + .5f, hit.z + .5f);
        if (tool != 4 && add)
            center += hit.normal;
        const int cx = int(std::floor(center.x())), cy = int(std::floor(center.y())),
                  cz = int(std::floor(center.z()));
        for (int z = cz - radius; z <= cz + radius; ++z)
            for (int y = cy - radius; y <= cy + radius; ++y)
                for (int x = cx - radius; x <= cx + radius; ++x) {
                    if (o.shape != 1 &&
                        (QVector3D(x + .5f, y + .5f, z + .5f) - center).length() > radius + .05f)
                        continue;
                    if (!allows(x, y, z))
                        continue;
                    // Stable density per stamp for paint/volume tools; full
                    // strength preserves the previous solid stamp behavior.
                    if (strength < 4 && !(x == cx && y == cy && z == cz)) {
                        uint32_t hash = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^
                                        uint32_t(z) * 83492791u ^ stampIndex * 2654435761u;
                        hash ^= hash >> 16;
                        hash *= 0x7feb352du;
                        hash ^= hash >> 15;
                        if (int(hash & 3) >= strength)
                            continue;
                    }
                    if (tool == 4) {
                        if (!world.isAir(world.terrainBlock(x, y, z, height(x, z))))
                            changeCell(x, y, z, mat);
                    } else
                        changeCell(x, y, z, add ? mat : Air);
                }
    }
    return changes != serial;
}
Command Stroke::finish(const QString &label) {
    Command c;
    c.label = label;
    c.heightLayer = layer;
    for (auto [i, b] : beforeHeights)
        if (b != world.heights.delta(i))
            c.heights.push_back({i, b, world.heights.delta(i)});
    for (auto [k, b] : beforeCells) {
        auto it = world.overrides.find(k);
        int a = it == world.overrides.end() ? -1 : int(it->second);
        const bool terrain = world.terrainOverrideKeys.contains(k);
        if (a != b.first || b.second != terrain)
            c.cells.push_back({k, b.first, a, b.second, terrain});
    }
    return c;
}
} // namespace wb
